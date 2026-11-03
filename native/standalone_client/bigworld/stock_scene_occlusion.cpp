#include "pch.hpp"
#include "stock_scene_occlusion.hpp"
#include "stock_static_model_scene.hpp"
#include "wot/stock_scene_occlusion_history.hpp"
#include "moo/device_callback.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/render_context.hpp"
#include "scene/intersection_set.hpp"
#include "scene/scene.hpp"
#include "scene/scene_object.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

BW_BEGIN_NAMESPACE
namespace
{
template<class T> struct OcclusionRelease { void operator()(T* value) const { if (value) value->Release(); } };
template<class T> using OcclusionPtr = std::unique_ptr<T, OcclusionRelease<T>>;
bool sameViewport(const D3DVIEWPORT9& a, const D3DVIEWPORT9& b)
{
    return a.X == b.X && a.Y == b.Y && a.Width == b.Width && a.Height == b.Height &&
        a.MinZ == b.MinZ && a.MaxZ == b.MaxZ;
}
bool sameMatrix(const Matrix& a, const Matrix& b)
{
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            if (a.row(row)[column] != b.row(row)[column]) return false;
    return true;
}
bool validView(const StockSceneOcclusionView& view)
{
    if (!view.viewport.Width || !view.viewport.Height || !std::isfinite(view.nearPlane) ||
        !std::isfinite(view.farPlane) || view.nearPlane <= 0.f || view.farPlane <= view.nearPlane ||
        !std::isfinite(view.camera.x) || !std::isfinite(view.camera.y) || !std::isfinite(view.camera.z)) return false;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column)
            if (!std::isfinite(view.viewProjection.row(row)[column])) return false;
    return true;
}
bool nearPlane(const BoundingBox& bounds, const Matrix& matrix)
{
    // #1513 ac25d0 plane[4] is view-projection column 2. ac1dd3 forces
    // visibility whenever that plane remains in the AABB/OBB intersection mask.
    const Vector3& lo = bounds.minBounds();
    const Vector3& hi = bounds.maxBounds();
    const Vector3 center = (lo + hi) * .5f;
    const Vector3 extent = (hi - lo) * .5f;
    const float distance = center.x * matrix.row(0).z + center.y * matrix.row(1).z +
        center.z * matrix.row(2).z + matrix.row(3).z;
    const float radius = extent.x * std::fabs(matrix.row(0).z) +
        extent.y * std::fabs(matrix.row(1).z) + extent.z * std::fabs(matrix.row(2).z);
    return !std::isfinite(distance) || !std::isfinite(radius) || distance - radius <= 0.f;
}
struct OcclusionDeviceScope
{
    IDirect3DStateBlock9* state;
    const Matrix* world;
    const Vector4* palette;
    uint32 paletteSize;
    const Matrix* previousWorld;
    uint32 previousPalette;
    const Matrix* instances;
    uint32 instanceCount;
    bool saved;
    explicit OcclusionDeviceScope(IDirect3DStateBlock9* block) : state(block),
        world(Moo::rc().effectVisualContext().worldMatrix()),
        palette(Moo::rc().effectVisualContext().worldMatrixPalette()),
        paletteSize(Moo::rc().effectVisualContext().worldMatrixPaletteSize()),
        previousWorld(Moo::rc().effectVisualContext().stockPreviousWorld()),
        previousPalette(Moo::rc().effectVisualContext().stockPreviousPaletteEntries()),
        instances(Moo::rc().effectVisualContext().stockInstancingMatrices()),
        instanceCount(Moo::rc().effectVisualContext().stockInstancingCount()),
        saved(block && SUCCEEDED(block->Capture())) {}
    ~OcclusionDeviceScope()
    {
        if (saved) { state->Apply(); Moo::rc().invalidateStateCache(); }
        auto& visual = Moo::rc().effectVisualContext();
        if (palette) visual.worldMatrixPalette(palette, paletteSize);
        else visual.worldMatrix(world);
        visual.stockPreviousWorld(previousWorld);
        visual.stockPreviousPaletteEntries(previousPalette);
        visual.stockInstancingStream(instances, instanceCount);
    }
};
Scene* preparedScene = NULL;
std::vector<std::weak_ptr<StockSceneOcclusionMapping>> preparedMappings;
struct OcclusionStatistics
{
    std::uint64_t candidates = 0, hidden = 0, submitted = 0, ready = 0;
    std::uint64_t pending = 0, errors = 0, capacity = 0, born = 0;
    uint32 frames = 0;
    void frame()
    {
        if (++frames != 600) return;
        // INFO_MSG is compiled out in Consumer. Append beside its frame/queue
        // statistics so a real run can prove query admission and visibility yield.
        FILE* log = fopen("d3d9_render_thread.log", "ab");
        if (log) {
            std::fprintf(log, "OCCLUSION frames=600 candidates/frame=%.2f hidden/frame=%.2f submitted/frame=%.2f "
                "ready/frame=%.2f pending/frame=%.2f errors/frame=%.2f capacity/frame=%.2f query_wrappers=%llu\n",
                candidates/600., hidden/600., submitted/600., ready/600., pending/600., errors/600., capacity/600.,
                static_cast<unsigned long long>(born));
            std::fclose(log);
        }
        *this = OcclusionStatistics();
    }
} statistics;

struct OcclusionQueryPool;
struct OcclusionQueryLease
{
    std::shared_ptr<OcclusionQueryPool> owner;
    OcclusionPtr<IDirect3DQuery9> query;
    std::uint64_t epoch;
    bool reusable = true;
    OcclusionQueryLease(std::shared_ptr<OcclusionQueryPool> pool,
        OcclusionPtr<IDirect3DQuery9> value, std::uint64_t generation) :
        owner(std::move(pool)), query(std::move(value)), epoch(generation) {}
    ~OcclusionQueryLease();
    HRESULT Issue(DWORD flags) { return query->Issue(flags); }
    HRESULT GetData(void* data, DWORD size, DWORD flags) { return query->GetData(data, size, flags); }
    void discard() { reusable = false; }
};
struct OcclusionQueryPool : Moo::DeviceCallback, std::enable_shared_from_this<OcclusionQueryPool>
{
    static const uint32 capacity = 4000; // Original scene-wide ring, including cached queries.
    std::vector<OcclusionPtr<IDirect3DQuery9>> cached;
    std::vector<std::weak_ptr<StockSceneOcclusionMapping>> mappings;
    OcclusionPtr<IDirect3DStateBlock9> state;
    uint32 allocated = 0;
    std::uint64_t epoch = 1;
    OcclusionQueryPool() { cached.reserve(capacity); }
    void deleteUnmanagedObjects() override { ++epoch; cached.clear(); state.reset(); allocated = 0; }
    IDirect3DStateBlock9* stateBlock()
    {
        if (!state) {
            IDirect3DStateBlock9* block = NULL;
            if (SUCCEEDED(Moo::rc().device()->CreateStateBlock(D3DSBT_ALL, &block))) state.reset(block);
            else if (block) block->Release();
        }
        return state.get();
    }
    std::unique_ptr<OcclusionQueryLease> acquire()
    {
        OcclusionPtr<IDirect3DQuery9> query;
        if (!cached.empty()) { query = std::move(cached.back()); cached.pop_back(); }
        else {
            if (allocated == capacity) { ++statistics.capacity; return {}; }
            IDirect3DQuery9* native = NULL;
            const HRESULT result = Moo::rc().device()->CreateQuery(D3DQUERYTYPE_OCCLUSION, &native);
            query.reset(native);
            if (FAILED(result) || !query) { ++statistics.errors; return {}; }
            ++allocated; ++statistics.born;
        }
        return std::unique_ptr<OcclusionQueryLease>(new OcclusionQueryLease(
            shared_from_this(), std::move(query), epoch));
    }
    void release(OcclusionPtr<IDirect3DQuery9> query, std::uint64_t generation, bool reusable)
    {
        if (generation != epoch) return;
        if (reusable) cached.push_back(std::move(query));
        else --allocated;
    }
};
OcclusionQueryLease::~OcclusionQueryLease()
{ owner->release(std::move(query), epoch, reusable); }
std::shared_ptr<OcclusionQueryPool> sceneQueryPool(Scene* scene)
{
    struct ScenePool { Scene* scene; std::weak_ptr<OcclusionQueryPool> pool; };
    static std::vector<ScenePool> pools;
    for (auto item = pools.begin(); item != pools.end();) {
        if (auto pool = item->pool.lock()) {
            if (item->scene == scene) return pool;
            ++item;
        } else item = pools.erase(item);
    }
    auto pool = std::make_shared<OcclusionQueryPool>();
    pools.push_back({scene, pool});
    return pool;
}
}

struct StockSceneOcclusionMapping::Impl : Moo::DeviceCallback
{
    static const uint32 capacity = 4000; // #1513 query ring 0xfa0.
    enum Status { free, reserved, pending };
    struct Entry
    {
        wot::StockSceneOcclusionHistory history;
        std::uint64_t revision = 0;
        int slot = -1;
    };
    struct Slot
    {
        std::unique_ptr<OcclusionQueryLease> query;
        std::shared_ptr<const StockSceneOcclusionView> view;
        BoundingBox bounds;
        std::uint64_t epoch = 0, revision = 0;
        uint32 instance = 0;
        uint32 lastPollFrame = 0;
        bool polled = false;
        std::chrono::steady_clock::time_point issued;
        Status status = free;
    };
    std::vector<Entry> entries;
    std::vector<Slot> slots;
    std::vector<uint32> available, requests;
    std::shared_ptr<const StockSceneOcclusionView> frameView;
    OcclusionPtr<IDirect3DSurface9> depth;
    Moo::EffectMaterialPtr material;
    std::shared_ptr<OcclusionQueryPool> pool;
    std::uint64_t epoch = 1;
    bool alive = true;

    explicit Impl(std::size_t count) : entries(count)
    {
        const auto limit = std::min(count, std::size_t(capacity));
        slots.reserve(limit); available.reserve(limit); requests.reserve(limit);
    }
    ~Impl() { deleteUnmanagedObjects(); }
    void release(uint32 index, bool terminal = false)
    {
        auto& slot = slots[index];
        if (slot.status == free) return;
        if (slot.instance < entries.size() && entries[slot.instance].slot == int(index))
            entries[slot.instance].slot = -1;
        // An abandoned pending query must never be handed to another identity.
        // Dropping its owner lets the render-thread adapter retire its late poll.
        if (slot.status == pending && !terminal && slot.query) slot.query->discard();
        slot.status = free; slot.view.reset(); slot.query.reset(); available.push_back(index);
    }
    void cancelRequests()
    {
        for (const auto index : requests)
            if (slots[index].status == reserved) {
                ++statistics.errors;
                entries[slots[index].instance].history.reset(); release(index);
            }
        requests.clear();
    }
    void deleteUnmanagedObjects() override
    {
        ++epoch;
        for (auto& slot : slots)
            if (slot.status == pending && slot.query) slot.query->discard();
        requests.clear(); available.clear(); slots.clear();
        for (auto& entry : entries) { entry.history.reset(); entry.slot = -1; }
        frameView.reset(); depth.reset();
    }
    void invalidate(uint32 index)
    {
        if (index >= entries.size()) return;
        auto& entry = entries[index]; ++entry.revision; entry.history.reset();
        // A pending query retains its slot until its terminal result. Its frozen
        // revision can no longer update this object's replacement/transform.
        if (entry.slot >= 0 && slots[entry.slot].status == reserved) release(uint32(entry.slot));
    }
    void beginFrame(const StockSceneOcclusionView& view)
    {
        if (!alive) return;
        if (!validView(view)) { deleteUnmanagedObjects(); return; }
        if (!pool || (frameView && frameView->scene != view.scene)) {
            deleteUnmanagedObjects(); pool = sceneQueryPool(view.scene);
        }
        if (frameView && frameView->frame == view.frame) return;
        if (frameView && (!sameViewport(frameView->viewport, view.viewport) || frameView->depth != view.depth))
            deleteUnmanagedObjects();
        cancelRequests();
        frameView = std::make_shared<const StockSceneOcclusionView>(view);
        if (view.depth) view.depth->AddRef();
        depth.reset(view.depth);
        // Read every pending receipt, including objects which just left the
        // frustum. Otherwise those invisible candidates would retain ring slots.
        for (uint32 index = 0; index < slots.size(); ++index)
            if (slots[index].status == pending) poll(index);
    }
    void poll(uint32 index)
    {
        auto& slot = slots[index];
        if (slot.status != pending || (slot.polled && slot.lastPollFrame == frameView->frame)) return;
        slot.polled = true; slot.lastPollFrame = frameView->frame;
        DWORD pixels = 1;
        HRESULT result = slot.query->GetData(&pixels, sizeof(pixels), 0);
        // The original driver ends a resolve batch at 0.1 seconds. Keep that
        // terminal bound without blocking or treating a late result as hidden.
        const bool expired = std::chrono::steady_clock::now() - slot.issued >= std::chrono::milliseconds(100);
        if (result == S_FALSE && !expired) {
            ++statistics.pending; return;
        }
        if (result == S_FALSE || expired) result = E_FAIL;
        if (result == S_OK) ++statistics.ready;
        else ++statistics.errors;
        auto& entry = entries[slot.instance];
        if (slot.epoch == epoch && slot.revision == entry.revision && slot.view)
            // ac2130/0xac2176 timestamps resolved history with currentFrame-1,
            // while the retained admission still identifies its actual pose/view.
            entry.history.observe(frameView->frame - 1, result == S_OK ? pixels != 0 : true);
        else entry.history.reset();
        if (FAILED(result)) slot.query->discard();
        release(index, true);
    }
    bool visible(uint32 index, const BoundingBox& bounds, bool queryAllowed, bool* admitted)
    {
        if (admitted) *admitted = false;
        if (!alive || !frameView || index >= entries.size() || bounds.insideOut() || !depth) return true;
        auto& entry = entries[index];
        if (entry.slot >= 0) {
            poll(uint32(entry.slot));
            if (entry.slot >= 0 && slots[entry.slot].status == pending)
                return entry.history.draw(frameView->frame, nearPlane(bounds, frameView->viewProjection), false);
            if (entry.slot >= 0 && slots[entry.slot].status == reserved) {
                return entry.history.draw(frameView->frame, nearPlane(bounds, frameView->viewProjection), true);
            }
        }
        const bool intersectsNear = nearPlane(bounds, frameView->viewProjection);
        if (intersectsNear) return entry.history.draw(frameView->frame, true, false);
        if (!queryAllowed || (available.empty() && slots.size() == capacity)) {
            ++statistics.capacity; return true;
        }
        uint32 slotIndex;
        if (available.empty()) { slotIndex = uint32(slots.size()); slots.emplace_back(); }
        else { slotIndex = available.back(); available.pop_back(); }
        auto& slot = slots[slotIndex];
        slot.query = pool->acquire();
        if (!slot.query) { available.push_back(slotIndex); return true; }
        slot.instance = index; slot.revision = entry.revision; slot.epoch = epoch;
        slot.bounds = bounds; slot.view = frameView; slot.status = reserved;
        slot.polled = false;
        entry.slot = int(slotIndex); requests.push_back(slotIndex);
        if (admitted) *admitted = true;
        return entry.history.draw(frameView->frame, false, true);
    }
    void issue(OcclusionDeviceScope* sharedScope = NULL)
    {
        if (!alive || requests.empty() || !frameView || !depth) { cancelRequests(); return; }
        auto* device = Moo::rc().device();
        D3DVIEWPORT9 viewport{}; IDirect3DSurface9* currentDepth = NULL;
        const HRESULT depthResult = device->GetDepthStencilSurface(&currentDepth);
        const bool sameDepth = SUCCEEDED(depthResult) && currentDepth == depth.get();
        if (currentDepth) currentDepth->Release();
        if (!sameDepth || FAILED(device->GetViewport(&viewport)) || !sameViewport(viewport, frameView->viewport) ||
            !sameMatrix(Moo::rc().viewProjection(), frameView->viewProjection)) {
            cancelRequests(); return;
        }
        std::unique_ptr<OcclusionDeviceScope> localScope;
        if (!sharedScope) {
            localScope.reset(new OcclusionDeviceScope(pool->stateBlock()));
            sharedScope = localScope.get();
        }
        if (!sharedScope->saved) { cancelRequests(); return; }
        if (!material) {
            material = new Moo::EffectMaterial;
            if (!material->initFromEffect("shaders/std_effects/position.fx")) material = NULL;
        }
        if (!material) { cancelRequests(); return; }
        const Matrix identity = Matrix::identity;
        Moo::rc().effectVisualContext().worldMatrix(&identity);
        if (!material->begin()) { cancelRequests(); return; }
        if (!material->beginPass(0)) { material->end(); cancelRequests(); return; }
        // Original ac0ea0/11c2e20 bounds proxy: eight world corners, 12 triangles.
        static const WORD indices[36] = {
            0,3,1, 0,2,3, 6,5,7, 6,4,5, 2,4,6, 2,0,4,
            1,7,5, 1,3,7, 6,7,2, 2,7,3, 0,1,4, 4,1,5
        };
        const bool streamReady = SUCCEEDED(device->SetFVF(D3DFVF_XYZ));
        for (const auto index : requests) {
            auto& slot = slots[index];
            if (slot.status != reserved) continue;
            auto& entry = entries[slot.instance];
            if (!streamReady || slot.epoch != epoch || slot.revision != entry.revision || !slot.view) {
                entry.history.reset(); release(index); continue;
            }
            Vector3 vertices[8];
            const auto& lo = slot.bounds.minBounds(); const auto& hi = slot.bounds.maxBounds();
            const float queryNear = slot.view->nearPlane, queryFar = slot.view->farPlane;
            const float offset = (queryFar-queryNear)/(queryFar*queryNear)*1.2e-6f;
            for (unsigned corner = 0; corner < 8; ++corner) {
                vertices[corner] = Vector3(corner & 1 ? hi.x : lo.x,
                    corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z);
                Vector3 toward = slot.view->camera - vertices[corner];
                const float length = toward.length();
                if (length > 0.f) vertices[corner] += toward * (offset/length);
            }
            slot.issued = std::chrono::steady_clock::now();
            const HRESULT begun = slot.query->Issue(D3DISSUE_BEGIN);
            HRESULT drawn = E_FAIL, ended = E_FAIL;
            if (SUCCEEDED(begun)) {
                drawn = device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 8, 12,
                    indices, D3DFMT_INDEX16, vertices, sizeof(Vector3));
                ended = slot.query->Issue(D3DISSUE_END);
            }
            if (FAILED(begun) || FAILED(drawn) || FAILED(ended)) {
                ++statistics.errors;
                entry.history.reset(); slot.query->discard(); release(index);
            } else { slot.status = pending; ++statistics.submitted; }
        }
        requests.clear();
        material->endPass(); material->end();
    }
};

StockSceneOcclusionMapping::StockSceneOcclusionMapping(std::size_t instances) : impl_(new Impl(instances)) {}
StockSceneOcclusionMapping::~StockSceneOcclusionMapping() = default;
void StockSceneOcclusionMapping::beginFrame(const StockSceneOcclusionView& view) { impl_->beginFrame(view); }
bool StockSceneOcclusionMapping::visible(uint32 instance, const BoundingBox& bounds, bool allowed, bool* admitted)
{ return impl_->visible(instance, bounds, allowed, admitted); }
void StockSceneOcclusionMapping::issue() { impl_->issue(); }
void StockSceneOcclusionMapping::invalidate(uint32 instance) { impl_->invalidate(instance); }
void StockSceneOcclusionMapping::retire()
{ impl_->alive = false; impl_->deleteUnmanagedObjects(); impl_->pool.reset(); }

void filterStockSceneOcclusion(Scene& scene, IntersectionSet& objects, const Matrix& viewProjection)
{
    preparedScene = &scene; preparedMappings.clear();
    StockSceneOcclusionView view;
    view.scene = &scene;
    view.viewProjection = viewProjection; view.camera = Moo::rc().invView().applyToOrigin();
    view.nearPlane = Moo::rc().camera().nearPlane(); view.farPlane = Moo::rc().camera().farPlane();
    view.frame = Moo::rc().frameTimestamp();
    auto* device = Moo::rc().device();
    if (FAILED(device->GetViewport(&view.viewport))) return;
    const HRESULT depthResult = device->GetDepthStencilSurface(&view.depth);
    OcclusionPtr<IDirect3DSurface9> frameDepth(view.depth);
    if (FAILED(depthResult) || !view.depth) return;
    const auto pool = sceneQueryPool(&scene);
    // A mapping can leave the frustum with outstanding queries. Continue its
    // nonblocking readback so it cannot retain the whole scene ring indefinitely.
    for (auto item = pool->mappings.begin(); item != pool->mappings.end();) {
        if (auto mapping = item->lock()) {
            preparedMappings.push_back(mapping); mapping->beginFrame(view); ++item;
        } else item = pool->mappings.erase(item);
    }
    uint32 budget = 4000; // One primary view shares the original ring admission bound across mappings.
    for (IntersectionSet::SceneObjectType type = 0; type < objects.maxType(); ++type) {
        auto& bucket = objects.objectsOfType(type);
        std::size_t output = 0;
        for (std::size_t index = 0; index < bucket.size(); ++index) {
            std::shared_ptr<StockSceneOcclusionMapping> mapping;
            uint32 instance = 0; BoundingBox bounds;
            bool draw = true;
            if (CompiledSpace::StockStaticModelSceneLoader::occlusionTarget(bucket[index], mapping, instance, bounds)) {
                ++statistics.candidates;
                bool known = false;
                for (const auto& weak : preparedMappings) if (weak.lock() == mapping) { known = true; break; }
                if (!known) {
                    preparedMappings.push_back(mapping); pool->mappings.push_back(mapping); mapping->beginFrame(view);
                }
                bool admitted = false;
                draw = mapping->visible(instance, bounds, budget != 0, &admitted);
                if (admitted) --budget;
                if (!draw) ++statistics.hidden;
            }
            if (draw) bucket[output++] = bucket[index];
        }
        bucket.resize(output);
    }
}
void issueStockSceneOcclusion(Scene& scene)
{
    if (preparedScene != &scene) return;
    std::vector<std::shared_ptr<StockSceneOcclusionMapping>> mappings;
    for (const auto& weak : preparedMappings)
        if (auto mapping = weak.lock())
            if (!mapping->impl_->requests.empty()) mappings.push_back(mapping);
    if (!mappings.empty()) {
        const auto pool = sceneQueryPool(&scene);
        // All mappings borrow one primary-view device state, so one capture and
        // restore encloses the complete bounds-query pass rather than each tile.
        OcclusionDeviceScope saved(pool->stateBlock());
        for (const auto& mapping : mappings) mapping->impl_->issue(&saved);
    }
    preparedMappings.clear(); preparedScene = NULL;
    statistics.frame();
}
BW_END_NAMESPACE
