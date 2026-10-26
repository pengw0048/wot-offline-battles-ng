#include "pch.hpp"
#include "stock_space_decals.hpp"
#include "stock_space_decals_data.hpp"
#include "stock_space_decals_texture.hpp"
#include "stock_deferred_buffers.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/compiled_space.hpp"
#include "compiled_space/string_table.hpp"
#include "moo/draw_context.hpp"
#include "moo/effect_material.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/format_name_map.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/primitive.hpp"
#include "moo/render_context.hpp"
#include "moo/renderer.hpp"
#include "moo/texture_manager.hpp"
#include "moo/vertex_buffer.hpp"
#include "moo/vertex_declaration.hpp"
#include "moo/vertices.hpp"
#include "moo/visual.hpp"
#include "moo/visual_manager.hpp"
#include "resmgr/auto_config.hpp"
#include "resmgr/bwresource.hpp"
#include "scene/draw_operation.hpp"
#include "scene/scene.hpp"
#include "scene/scene_draw_context.hpp"
#include "scene/scene_object.hpp"
#include "scene/spatial_query_operation.hpp"
#include "space/dynamic_scene_provider.hpp"
#include "stock_dynamic_resolution.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <vector>

BW_BEGIN_NAMESPACE
namespace
{
AutoConfigString decalsConfiguration("system/decalsConfig");

bool decalFailure(const std::string& reason)
{
    ERROR_MSG("StockSpaceDecals: %s\n", reason.c_str());
    return false;
}

struct DecalResource
{
    std::array<BW::string, 4> names;
    std::array<Moo::BaseTexturePtr, 4> textures;
    uint32 material = 0;
    bool pbsReady = false;
};

struct DecalInstance
{
    DecalResource resource;
    D3DXVECTOR3 position, scale;
    D3DXQUATERNION rotation;
};

struct DecalGroup
{
    std::weak_ptr<CompiledSpace::StockSpaceDecalState> state;
    size_t index = 0;
    Matrix world;
    BoundingBox visibility;
    bool available = false;
};

struct QueuedDecal
{
    std::shared_ptr<CompiledSpace::StockSpaceDecalState> state;
    size_t index;
    float distanceSquared;
};

struct DecalQuality
{
    float lodBias;
    int mipSkip, materialDownsample;
    uint32 visibleLimit;
};

struct DecalChannel
{
    uint32 minimumWidth, minimumHeight, maximumWidth, maximumHeight;
    D3DFORMAT format;
};

struct DecalTextureView
{
    ComObjectWrap<DX::BaseTexture> source;
    ComObjectWrap<DX::Texture> texture;
};

class DecalRenderer : public Moo::DeviceCallback
{
public:
    std::array<DecalQuality, 5> quality = {{
        {1.f, 0, 0, 2500}, {.8f, 1, 1, 1600}, {.6f, 1, 1, 900},
        {.4f, 2, 1, 400}, {0.f, 4, 1, 1}}};
    int selection = 0;
    uint32 anisotropy = 2;
    bool active = false, frame = false;
    Moo::EffectMaterialPtr material;
    Moo::VisualPtr plane, cube;
    Moo::VertexBuffer instances;
    std::vector<QueuedDecal> visible;
    std::array<std::vector<DecalChannel>, 2> channels;
    std::vector<std::weak_ptr<CompiledSpace::StockSpaceDecalState>> mappings;
    std::mutex mappingsMutex;

    bool initialise();
    bool draw(unsigned phase);
    virtual void createManagedObjects();
    virtual void deleteManagedObjects();
    virtual void createUnmanagedObjects() {}
    virtual void deleteUnmanagedObjects();
    virtual bool recreateForD3DExDevice() const { return true; }
};

std::shared_ptr<DecalRenderer> renderer;
int selectedQuality = 0;

void clearDecalBinding(const Moo::EffectMaterialPtr& material)
{
    ID3DXEffect* binding = material ? material->properties().existingStockBinding() : NULL;
    if (binding)
        for (const char* name : {"g_map1", "g_map2", "g_map3", "g_map4", "g_texObjKind"})
            if (FAILED(binding->SetTexture(name, NULL))) decalFailure("Cannot retire an original decal texture binding");
}

uint32 nativeMaterial(uint32 authored, bool pbsReady)
{
    if (pbsReady) return authored;
    // a4b3d0 converts only the legacy material enum. This is not an effect
    // technique alias: each result has its own original shader and phase.
    switch (authored)
    {
    case 0: return 7;
    case 2: return 1;
    case 3: return 5;
    case 4: return 2;
    case 5: return 3;
    default: return 0;
    }
}

unsigned requiredTextures(unsigned authored, bool pbsReady)
{
    static const unsigned legacy[] = {1, 2, 3, 2, 1, 1, 1};
    static const unsigned pbs[] = {3, 3, 1, 1, 3};
    if (pbsReady) return authored < 5 ? pbs[authored] : 0;
    return authored < 7 ? legacy[authored] : 0;
}

void decomposeDecal(const wot::StockSpaceDecalRecord& source, DecalInstance& output)
{
    D3DXMATRIX world;
    std::memcpy(&world, source.bytes.data() + 4, sizeof(world));
    output.position = D3DXVECTOR3(world._41, world._42, world._43);
    D3DXVECTOR3 axes[] = {{world._11, world._12, world._13},
        {world._21, world._22, world._23}, {world._31, world._32, world._33}};
    output.scale = D3DXVECTOR3(D3DXVec3Length(&axes[0]), D3DXVec3Length(&axes[1]), D3DXVec3Length(&axes[2]));
    // a54790 uses D3DX LookAt and QuaternionRotationMatrix. In particular it
    // does not use the signed-scale BlendTransform decomposition used by trees.
    D3DXVec3Normalize(&axes[1], &axes[1]);
    D3DXVec3Normalize(&axes[2], &axes[2]);
    const D3DXVECTOR3 origin(0.f, 0.f, 0.f);
    D3DXMATRIX rotation;
    D3DXMatrixLookAtLH(&rotation, &origin, &axes[2], &axes[1]);
    D3DXQuaternionRotationMatrix(&output.rotation, &rotation);
}
}

namespace CompiledSpace
{
struct StockSpaceDecalState
{
    wot::StockSpaceDecalsData data;
    std::vector<DecalInstance> instances;
    std::vector<DecalGroup> groups;
    std::vector<SceneObject> objects;
    std::vector<DynamicObjectHandle> membership;
    std::vector<bool> queued;
    std::map<std::pair<DX::BaseTexture*, int>, DecalTextureView> textureViews;
    CompiledSpace* space = NULL;
    uint32 mask = 0;
    bool active = false, bound = false;
};
}

namespace
{
class DecalDrawHandler : public IDrawOperationTypeHandler
{
public:
    virtual void doDraw(const SceneDrawContext& context, const SceneObject* objects, size_t count)
    {
        auto owner = renderer;
        if (!owner || !owner->active || !owner->frame ||
            context.drawContext().renderingPassType() != Moo::RENDERING_PASS_COLOR) return;
        const Vector3 camera = Moo::rc().invView().applyToOrigin();
        const float zoom = Moo::rc().lodZoomFactor();
        for (size_t index = 0; index < count; ++index)
        {
            const auto* group = objects[index].getAs<DecalGroup>();
            auto state = group->state.lock();
            if (!state || !state->active || state->queued[group->index]) continue;
            state->queued[group->index] = true;
            const auto& range = state->data.groups[group->index];
            for (uint32 decal = range.first(); decal <= range.last(); ++decal)
            {
                if (!(state->mask & state->data.decals[decal].visibilityMask())) continue;
                const auto& position = state->instances[decal].position;
                const float point[] = {position.x, position.y, position.z};
                const float eye[] = {camera.x, camera.y, camera.z};
                const float distance = wot::stockSpaceDecalDistance(point, eye, zoom);
                owner->visible.push_back({state, decal, distance});
            }
        }
    }
};

class DecalSpatialHandler : public ISpatialQueryOperationTypeHandler
{
public:
    virtual void getWorldTransform(const SceneObject& object, Matrix* transform)
    { *transform = object.getAs<DecalGroup>()->world; }
    virtual void getWorldVisibilityBoundingBox(const SceneObject& object, AABB* bounds)
    { *bounds = object.getAs<DecalGroup>()->visibility; }
};

void removeDecalMembership(const std::shared_ptr<CompiledSpace::StockSpaceDecalState>& state)
{
    state->active = false;
    state->bound = false;
    std::vector<DynamicObjectHandle> retired;
    retired.swap(state->membership);
    for (const auto& handle : retired) state->space->dynamicScene().removeObject(handle);
}

bool loadDecalResources(DecalInstance& instance, const wot::StockSpaceDecalRecord& source,
    const CompiledSpace::StringTable& strings, bool pbsReady)
{
    auto& resource = instance.resource;
    resource.pbsReady = pbsReady;
    resource.material = nativeMaterial(source.bytes[0x56], pbsReady);
    const unsigned count = requiredTextures(source.bytes[0x56], pbsReady);
    if (!count || source.type() > 3) return decalFailure("Invalid authored material or projection enum in WGSD2");
    // Resolve all four authored references, including currently unused ones.
    // A nonzero FNV reference to the empty string remains a valid empty slot.
    for (unsigned slot = 0; slot != 4; ++slot)
    {
        const uint32 hash = source.texture(slot);
        const char* name = hash ? strings.entry(hash) : "";
        if (!name) return decalFailure("An authored WGSD2 texture FNV key is absent from BWST");
        resource.names[slot] = name;
    }
    const bool primary = resource.material == 0 || resource.material == 1 || resource.material == 4 ||
        resource.material == 5 || resource.material == 7;
    const unsigned first = pbsReady && !primary ? 3 : 0;
    for (unsigned channel = 0; channel < count; ++channel)
    {
        const unsigned slot = first + channel;
        if (slot >= 4 || resource.names[slot].empty() || !BWResource::fileExists(resource.names[slot]))
            return decalFailure("A required authored WGSD2 texture is unavailable");
        // The atlas upload consumes system-memory DDS levels. A default-pool
        // source is not lockable/copyable by UpdateSurface under D3D9Ex.
        auto texture = Moo::TextureManager::instance()->getSystemMemoryTexture(resource.names[slot]);
        if (!texture || !texture->pTexture() || texture->status() != Moo::BaseTexture::STATUS_READY)
            return decalFailure("A required authored WGSD2 texture did not load");
        if (channel && (texture->width() != resource.textures[first]->width() ||
            texture->height() != resource.textures[first]->height()))
            return decalFailure("Required WGSD2 texture channels have different dimensions");
        resource.textures[slot] = texture;
    }
    decomposeDecal(source, instance);
    return true;
}
}

namespace CompiledSpace
{
StockSpaceDecalsLoader::StockSpaceDecalsLoader(uint32 mask) : visibilityMask_(mask) {}
StockSpaceDecalsLoader::~StockSpaceDecalsLoader() { doUnload(); }

bool StockSpaceDecalsLoader::doLoadFromSpace(ClientSpace* space, BinaryFormat& reader,
    const DataSectionPtr& settings, const Matrix&, const StringTable& strings)
{
    if (state_ || unloadDepth_) return decalFailure("The previous WGSD2 mapping must unload before another load");
    const auto owner = renderer;
    const bool forward = Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING;
    if ((!owner || !owner->active) && !forward) return decalFailure("The actual deferred decal renderer is unavailable");
    auto next = std::make_shared<StockSpaceDecalState>();
    next->space = dynamic_cast<CompiledSpace*>(space);
    if (!next->space) return decalFailure("WGSD2 requires an actual compiled space");
    next->mask = visibilityMask_;
    const size_t section = reader.findSection(FourCC("WGSD"));
    if (section == BinaryFormat::NOT_FOUND || reader.sectionVersion(section) != 2)
        return decalFailure("The original WGSD2 section is unavailable");
    BinaryFormat::Stream* stream = reader.openSection(section);
    if (!stream) return decalFailure("Cannot map the original WGSD2 section");
    const size_t length = stream->length();
    const void* bytes = stream->readRaw(length);
    std::string error;
    const bool decoded = next->data.read(bytes, length, error);
    reader.closeSection(stream);
    if (!decoded) return decalFailure(error);
    // 6bcfd0 checks the real renderer's nullable decal owner before creating
    // any resource/group handles. The original forward .11 effect contains
    // only DummyTechnique, so this mode has no deferred decal consumer.
    if (forward)
    {
        state_ = next;
        loaded_.store(true, std::memory_order_release);
        return true;
    }
    next->instances.resize(next->data.decals.size());
    const bool pbsReady = settings && settings->readBool("PBSDecals", false);
    for (size_t index = 0; index < next->instances.size(); ++index)
        if (!loadDecalResources(next->instances[index], next->data.decals[index], strings, pbsReady)) return false;
    next->groups.resize(next->data.groups.size());
    next->objects.resize(next->groups.size());
    next->queued.assign(next->groups.size(), false);
    next->membership.reserve(next->groups.size());
    for (size_t index = 0; index < next->groups.size(); ++index)
    {
        const auto& source = next->data.groups[index];
        auto& group = next->groups[index];
        group.state = next;
        group.index = index;
        const Vector3 minimum(source.scalar(0), source.scalar(4), source.scalar(8));
        const Vector3 maximum(source.scalar(12), source.scalar(16), source.scalar(20));
        group.visibility.setBounds(minimum, maximum);
        // 6bcb90 uses the authored group box as world coordinates. Neither
        // this box nor each decal's matrix is multiplied by mappingTransform.
        group.world.setZero();
        if (!(maximum.x < minimum.x || maximum.y < minimum.y || maximum.z < minimum.z))
        {
            group.world.setScale(maximum - minimum);
            group.world.translation((minimum + maximum) * .5f);
        }
        // 6bcb90 passes flags word 0x0001 to ab9610 -> abb0a0.
        // Its second flag is false, so an all-zero scale is a disabled slot.
        group.available = group.world[0][0] != 0.f || group.world[1][1] != 0.f || group.world[2][2] != 0.f;
        next->objects[index].handleAndType(&group);
        next->objects[index].flags().isDynamic(false);
    }
    state_ = next;
    {
        std::lock_guard<std::mutex> lock(owner->mappingsMutex);
        owner->mappings.erase(std::remove_if(owner->mappings.begin(), owner->mappings.end(),
            [](const std::weak_ptr<StockSpaceDecalState>& mapping) { return mapping.expired(); }), owner->mappings.end());
        owner->mappings.push_back(next);
    }
    loaded_.store(true, std::memory_order_release);
    return true;
}

bool StockSpaceDecalsLoader::doBind()
{
    auto state = state_;
    if (!state) return false;
    if (state->bound) return true;
    auto& scene = state->space->scene();
    auto* draw = scene.getObjectOperation<DrawOperation>();
    auto* spatial = scene.getObjectOperation<SpatialQueryOperation>();
    if (!draw->getHandler<DecalGroup>()) draw->addHandler<DecalGroup, DecalDrawHandler>();
    if (!spatial->getHandler<DecalGroup>()) spatial->addHandler<DecalGroup, DecalSpatialHandler>();
    state->active = true;
    state->bound = true;
    for (size_t index = 0; index < state->groups.size(); ++index)
    {
        const auto& group = state->groups[index];
        if (!group.available) continue;
        const DynamicObjectHandle handle = state->space->dynamicScene().addObject(state->objects[index],
            group.world, BoundingBox::s_insideOut_, group.visibility);
        if (state_ != state)
        {
            state->space->dynamicScene().removeObject(handle);
            return false;
        }
        state->membership.push_back(handle);
    }
    return true;
}

void StockSpaceDecalsLoader::doUnload()
{
    loaded_.store(false, std::memory_order_release);
    auto retired = state_;
    if (!retired) return;
    state_.reset();
    struct Retiring
    {
        explicit Retiring(uint32& value) : depth(value) { ++depth; }
        ~Retiring() { --depth; }
        uint32& depth;
    } retiring(unloadDepth_);
    // Remove identities before observer callbacks. Queued records retain the
    // retired allocation but test active before using its GPU resources.
    removeDecalMembership(retired);
}

float StockSpaceDecalsLoader::percentLoaded() const
{ return loaded_.load(std::memory_order_acquire) ? 1.f : 0.f; }
}

namespace
{
bool DecalRenderer::initialise()
{
    DataSectionPtr configuration = BWResource::openSection(decalsConfiguration.value());
    DataSectionPtr scene = configuration ? configuration->openSection("scene") : NULL;
    DataSectionPtr render = configuration ? configuration->openSection("render") : NULL;
    if (!scene || !render) return decalFailure("The configured original decal scene/render settings are unavailable");
    const int requestedAnisotropy = scene->readInt("anisotropy", 2);
    if (requestedAnisotropy < 0 || requestedAnisotropy > 256)
        return decalFailure("The authored decal anisotropy is outside the native texture range");
    anisotropy = static_cast<uint32>(requestedAnisotropy);
    const char* const names[] = {"MAX", "HIGH", "MEDIUM", "LOW", "OFF"};
    for (unsigned pass = 0; pass != 2; ++pass)
    {
        DataSectionPtr source = (pass ? render : scene)->openSection("quality");
        BW::vector<DataSectionPtr> options;
        if (source) source->openSections("option", options);
        for (const auto& option : options)
        {
            const BW::string name = option->asString();
            for (unsigned index = 0; index != 5; ++index)
            {
                if (name != names[index]) continue;
                if (pass)
                    quality[index].visibleLimit = option->readInt("maxVisibleDecals", quality[index].visibleLimit);
                else
                {
                    quality[index].mipSkip = option->readInt("mip", quality[index].mipSkip);
                    const Vector2 downsample = option->readVector2("matTypeDownsample", Vector2(-1.f, -1.f));
                    // a4c2d0 consumes x only, retaining the ctor value for x<=0.
                    if (downsample.x > 0.f) quality[index].materialDownsample = static_cast<int>(downsample.x);
                }
            }
        }
    }
    BW::vector<DataSectionPtr> types;
    scene->openSections("type", types);
    for (const auto& type : types)
    {
        const BW::string name = type->readString("name");
        unsigned family;
        if (name == "GBufferDecals") family = 0;
        else if (name == "OtherDecals") family = 1;
        else continue;
        BW::vector<DataSectionPtr> atlases;
        type->openSections("atlas", atlases);
        for (const auto& atlas : atlases)
        {
            DataSectionPtr limits = atlas->openSection("constrains");
            if (!limits) return decalFailure("A configured decal atlas has no authored texture constraints");
            BW::string format = limits->readString("format");
            DecalChannel channel = {limits->readUInt("minWidth"), limits->readUInt("minHeight"),
                limits->readUInt("maxWidth"), limits->readUInt("maxHeight"), Moo::FormatNameMap::formatType(format)};
            if (!channel.minimumWidth || !channel.minimumHeight || channel.format == D3DFMT_UNKNOWN)
                return decalFailure("A configured decal texture constraint is invalid");
            channels[family].push_back(channel);
        }
    }
    if (channels[0].empty() || channels[1].empty()) return decalFailure("The two original decal atlas families are unavailable");
    createManagedObjects();
    active = material && plane && cube;
    return active;
}

void DecalRenderer::createManagedObjects()
{
    material = NULL; plane = NULL; cube = NULL;
    if (!Moo::createEffect(material, "shaders/std_effects/wg_deferred_decal.fx"))
    { decalFailure("Cannot create the original deferred decal effect"); return; }
    plane = Moo::VisualManager::instance()->get("system/models/fx_unit_plane_05.visual");
    cube = Moo::VisualManager::instance()->get("system/models/fx_unite_cube_decal.visual");
    if (!plane || !cube || !plane->isOK() || !cube->isOK())
    {
        plane = NULL; cube = NULL;
        decalFailure("The original decal plane/cube geometry is unavailable");
    }
}

void DecalRenderer::deleteManagedObjects()
{
    deleteUnmanagedObjects();
    material = NULL; plane = NULL; cube = NULL;
}

void DecalRenderer::deleteUnmanagedObjects()
{
    clearDecalBinding(material);
    instances.release();
    std::lock_guard<std::mutex> lock(mappingsMutex);
    for (auto it = mappings.begin(); it != mappings.end();)
    {
        auto state = it->lock();
        if (!state) it = mappings.erase(it);
        else { state->textureViews.clear(); ++it; }
    }
}

// A device state block includes every stream/frequency, declaration, index
// buffer and effect render state. Render targets are saved separately by Moo.
// Invalidate the Moo cache after Apply, including on a failed partial draw.
class DecalDeviceScope
{
public:
    DecalDeviceScope()
    {
        world_ = Moo::rc().effectVisualContext().worldMatrix();
        valid_ = SUCCEEDED(Moo::rc().device()->CreateStateBlock(D3DSBT_ALL, &state_)) &&
            state_ && SUCCEEDED(state_->Capture());
    }
    ~DecalDeviceScope()
    {
        if (valid_)
        {
            if (FAILED(state_->Apply())) decalFailure("Cannot restore the decal device state");
            Moo::rc().invalidateStateCache();
        }
        Moo::rc().effectVisualContext().worldMatrix(world_);
    }
    bool valid() const { return valid_; }
private:
    ComObjectWrap<IDirect3DStateBlock9> state_;
    bool valid_ = false;
    const Matrix* world_ = NULL;
};

bool decalTextureView(DecalRenderer& owner, CompiledSpace::StockSpaceDecalState& state,
    const DecalResource& resource, unsigned slot,
    unsigned channel, unsigned family, unsigned skip, DX::BaseTexture*& output, uint32& width, uint32& height)
{
    if (!resource.textures[slot] || channel >= owner.channels[family].size())
        return decalFailure("A required decal texture channel has no original atlas configuration");
    DX::BaseTexture* input = resource.textures[slot]->pTexture();
    if (!input || input->GetType() != D3DRTYPE_TEXTURE)
        return decalFailure("A decal atlas source must be an actual 2D texture");
    auto* texture = static_cast<DX::Texture*>(input);
    const auto& limits = owner.channels[family][channel];
    D3DSURFACE_DESC description;
    if (FAILED(texture->GetLevelDesc(0, &description)) || description.Format != limits.format ||
        description.Width < limits.minimumWidth || description.Height < limits.minimumHeight ||
        description.Width > limits.maximumWidth || description.Height > limits.maximumHeight ||
        (description.Width & (description.Width - 1)) || (description.Height & (description.Height - 1)))
        return decalFailure("An authored decal texture violates its original atlas format/size constraints");
    const unsigned levels = wot::stockSpaceDecalMipLevels(owner.anisotropy, skip);
    if (skip > texture->GetLevelCount() || levels > texture->GetLevelCount() - skip ||
        FAILED(texture->GetLevelDesc(skip, &description)))
        return decalFailure("An authored decal texture has too few real mip levels");
    width = description.Width; height = description.Height;
    auto& view = state.textureViews[std::make_pair(input, static_cast<int>(skip))];
    if (!view.texture)
    {
        DecalTextureView candidate;
        candidate.source = input;
        if (FAILED(wot::copyStockSpaceDecalMipTexture(Moo::rc().device(), texture, skip, levels, &candidate.texture)))
            return decalFailure("Cannot allocate/copy the original authored decal mip view");
        candidate.texture.addAlloc(BW::StringRef("Art/Decals/Original Mip View"));
        // 13f64e0/13f5310 copy each complete source mip without added padding.
        // The original four-pixel border is inside the authored DDS. A private
        // mip view retains its UVs, explicit gradients and capped mip tail.
        view = candidate;
    }
    output = view.texture.pComObject();
    return true;
}

bool drawDecalGeometry(DecalRenderer& owner, const Moo::VisualPtr& visual,
    const std::array<float, 24>& constants)
{
    if (!visual || !visual->isOK()) return false;
    bool submitted = false;
    void* destination = NULL;
    if (!owner.instances.valid() && FAILED(owner.instances.create(96, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
        0, D3DPOOL_DEFAULT, "Art/Decals/Original 96-byte Instances"))) return false;
    if (FAILED(owner.instances.lock(0, 96, &destination, D3DLOCK_DISCARD))) return false;
    std::memcpy(destination, constants.data(), 96);
    if (FAILED(owner.instances.unlock())) return false;
    auto* device = Moo::rc().device();
    for (const auto& set : visual->renderSets())
    {
        for (const auto& geometry : set.geometry_)
        {
            if (!geometry.vertices_ || !geometry.primitives_ ||
                FAILED(geometry.vertices_->setVertices(false, false)) || !geometry.vertices_->pDecl() ||
                FAILED(geometry.primitives_->setPrimitives())) return false;
            D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH + 1];
            UINT count = MAXD3DDECLLENGTH + 1;
            if (FAILED(geometry.vertices_->pDecl()->declaration()->GetDeclaration(elements, &count)) ||
                !count || count + 6 > MAXD3DDECLLENGTH + 1) return false;
            --count;
            for (UINT index = 0; index < count; ++index)
                if (elements[index].Stream != 0) return decalFailure("Original decal helper has an unexpected vertex stream");
            for (unsigned index = 0; index != 6; ++index)
            {
                const D3DVERTEXELEMENT9 element = {9, static_cast<WORD>(index * 16), D3DDECLTYPE_FLOAT4,
                    D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, static_cast<BYTE>(4 + index)};
                elements[count++] = element;
            }
            const D3DVERTEXELEMENT9 end = D3DDECL_END();
            elements[count] = end;
            ComObjectWrap<DX::VertexDeclaration> declaration;
            if (FAILED(device->CreateVertexDeclaration(elements, &declaration)) ||
                FAILED(owner.instances.set(9, 0, 96)) ||
                FAILED(device->SetStreamSourceFreq(0, static_cast<UINT>(D3DSTREAMSOURCE_INDEXEDDATA) | 1u)) ||
                FAILED(device->SetStreamSourceFreq(9, static_cast<UINT>(D3DSTREAMSOURCE_INSTANCEDATA) | 1u))) return false;
            if (FAILED(static_cast<HRESULT>(Moo::rc().setVertexDeclaration(declaration.pComObject())))) return false;
            for (const auto& group : geometry.primitiveGroups_)
            {
                if (group.groupIndex_ >= geometry.primitives_->nPrimGroups()) return false;
                const auto& primitive = geometry.primitives_->primitiveGroup(group.groupIndex_);
                if (!primitive.nVertices_ || !primitive.nPrimitives_) continue;
                if (FAILED(geometry.primitives_->drawInstancedPrimitiveGroup(group.groupIndex_, 1))) return false;
                submitted = true;
            }
        }
    }
    return submitted;
}

struct PreparedDecal
{
    QueuedDecal queued;
    std::array<std::uint8_t, 124> record = {};
    uint32 material = 0;
    unsigned kind = 0;
};

void putDecalWord(std::array<std::uint8_t, 124>& record, unsigned offset, uint32 value)
{ std::memcpy(record.data() + offset, &value, 4); }
void putDecalFloat(std::array<std::uint8_t, 124>& record, unsigned offset, float value)
{ std::memcpy(record.data() + offset, &value, 4); }

bool DecalRenderer::draw(unsigned phase)
{
    if (phase > 3) return decalFailure("Invalid original decal phase");
    if (!active || !frame) return decalFailure("Decal drawing requires its active frame owner");
    const auto& option = quality[selection];
    if (visible.empty() || !option.visibleLimit || option.lodBias <= 0.f || option.mipSkip < 0) return true;
    if (option.mipSkip > 16) return decalFailure("The authored decal mip skip exceeds its texture dimensions");
    if (!material || !plane || !cube) return decalFailure("The original decal drawing resources are unavailable");
    std::vector<QueuedDecal> candidates;
    for (const auto& entry : visible) if (entry.state->active) candidates.push_back(entry);
    std::sort(candidates.begin(), candidates.end(), [](const QueuedDecal& a, const QueuedDecal& b)
    { return a.distanceSquared < b.distanceSquared; });
    std::vector<PreparedDecal> prepared;
    const float start = wot::stockDecalMultiply(100.f, option.lodBias);
    const float end = wot::stockDecalMultiply(150.f, option.lodBias);
    const float startSquared = wot::stockDecalMultiply(start, start);
    const float inverseRange = wot::stockDecalDivide(1.f,
        wot::stockDecalSubtract(wot::stockDecalMultiply(end, end), startSquared));
    for (const auto& entry : candidates)
    {
        const float fade = std::max(0.f, std::min(1.f, wot::stockDecalSubtract(1.f,
            wot::stockDecalMultiply(wot::stockDecalSubtract(entry.distanceSquared, startSquared), inverseRange))));
        if (fade <= .003921569f) continue;
        PreparedDecal value;
        value.queued = entry;
        const auto& instance = entry.state->instances[entry.index];
        const auto& source = entry.state->data.decals[entry.index];
        value.material = instance.resource.material;
        float parallax = 0.f;
        if (value.material == 1)
        {
            value.material = static_cast<uint32>(std::max(0, 1 - option.materialDownsample));
            if (value.material == 1 && entry.distanceSquared >= 2500.f)
            {
                if (entry.distanceSquared >= 4900.f) value.material = 0;
                else parallax = wot::stockDecalMultiply(wot::stockDecalSubtract(entry.distanceSquared, 2500.f), .00041666668f);
            }
        }
        value.kind = source.type();
        if (value.kind == 1)
        {
            const Vector3 camera = Moo::rc().invView().applyToOrigin();
            const float scale[] = {instance.scale.x, instance.scale.y, instance.scale.z};
            const float position[] = {instance.position.x, instance.position.y, instance.position.z};
            const float eye[] = {camera.x, camera.y, camera.z};
            if (wot::stockSpaceDecalCameraOutside(scale, position, eye,
                Moo::rc().camera().nearPlane(), Moo::rc().camera().fov())) value.kind = 0;
        }
        putDecalWord(value.record, 0, value.material);
        putDecalWord(value.record, 4, source.bytes[0x54]);
        putDecalWord(value.record, 8, source.bytes[0x55] | (source.type() << 24));
        putDecalFloat(value.record, 12, entry.distanceSquared);
        putDecalFloat(value.record, 16, source.scalar(0x68));
        putDecalFloat(value.record, 20, source.scalar(0x6c));
        std::memcpy(value.record.data() + 32, &instance.rotation, 16);
        std::memcpy(value.record.data() + 48, &instance.position, 12);
        std::memcpy(value.record.data() + 60, &instance.scale, 12);
        std::memcpy(value.record.data() + 72, source.bytes.data() + 0x58, 16);
        for (unsigned index = 0; index != 4; ++index) putDecalFloat(value.record, 88 + index * 4, fade);
        putDecalFloat(value.record, 108, parallax);
        prepared.push_back(value);
        if (prepared.size() == option.visibleLimit) break;
    }
    // a8e1c0 sorts the original single scene-provider material group by type,
    // signed projection byte, authored priority and ascending distance. Each
    // direct texture draw stays in that order; textures never reorder overlap.
    std::sort(prepared.begin(), prepared.end(), [](const PreparedDecal& a, const PreparedDecal& b)
    {
        if (a.material != b.material) return a.material < b.material;
        const auto& x = a.queued.state->data.decals[a.queued.index];
        const auto& y = b.queued.state->data.decals[b.queued.index];
        if (x.type() != y.type()) return x.type() < y.type();
        if (x.bytes[0x54] != y.bytes[0x54]) return x.bytes[0x54] < y.bytes[0x54];
        return a.queued.distanceSquared < b.queued.distanceSquared;
    });
    static const unsigned phases[][5] = {{7, 0, 1, 4, 6}, {2, 8, 8, 8, 8}, {3, 8, 8, 8, 8}, {5, 8, 8, 8, 8}};
    static const char* const techniques[] = {"PBS", "PBS_PARALLAX", "AMBIENT_OCCLUSION", "SHADOWS",
        "PBS_ROAD", "ENV_MAPPING", "SHADOWS_FORWARD", "DIFFUSE"};
    if (prepared.empty()) return true;
    DecalDeviceScope states;
    if (!states.valid()) return decalFailure("Cannot preserve the actual decal device state");
    auto* buffers = stockDeferredBuffers();
    DX::BaseTexture* kinds = buffers ? buffers->objectKindCopy() : NULL;
    if (!kinds) return decalFailure("The original immutable object-kind input is unavailable");
    Moo::rc().effectVisualContext().worldMatrix(NULL);
    Moo::rc().effectVisualContext().updateSharedConstants(Moo::CONSTANTS_PER_VIEW);
    const auto* resolution = stockDynamicResolution();
    const auto& size = Moo::rc().backBufferDesc();
    const auto projection = resolution ? resolution->shaderProjection() : wot::StockDynamicResolution().project(size.Width, size.Height);
    const Vector4 sourceProjection(projection.parameters[0], projection.parameters[1], projection.parameters[2], projection.parameters[3]);
    for (unsigned materialIndex = 0; materialIndex != 5 && phases[phase][materialIndex] != 8; ++materialIndex)
    {
        for (unsigned kind = 0; kind != 4; ++kind)
        {
            for (auto& value : prepared)
            {
                if (value.material != phases[phase][materialIndex] || value.kind != kind || !value.queued.state->active) continue;
                const auto& instance = value.queued.state->instances[value.queued.index];
                const auto& resource = instance.resource;
                const bool primary = value.material == 0 || value.material == 1 || value.material == 4 || value.material == 5 || value.material == 7;
                const unsigned first = primary ? 0 : (resource.pbsReady ? 3 : 0);
                const unsigned family = primary ? 0 : 1;
                const auto& authored = value.queued.state->data.decals[value.queued.index];
                const unsigned count = requiredTextures(authored.bytes[0x56], resource.pbsReady);
                wot::StockSpaceDecalMaterialInput input = {};
                input.controls[4] = anisotropy;
                input.controls[5] = (resource.pbsReady ? ((anisotropy >> 1) << 4) : 4) / (1u << option.mipSkip);
                input.controls[6] = resource.pbsReady ? 1 : 0;
                DX::BaseTexture* textures[4] = {};
                uint32 width = 0, height = 0;
                for (unsigned channel = 0; channel < count; ++channel)
                {
                    uint32 w, h;
                    if (!decalTextureView(*this, *value.queued.state, resource, first + channel, channel, family,
                        static_cast<unsigned>(option.mipSkip), textures[(primary ? 0 : 3) + channel], w, h)) return false;
                    width = w; height = h;
                    const unsigned base = ((primary ? 0 : 3) + channel) * 4;
                    input.textureSizes[base] = static_cast<float>(w);
                    input.textureSizes[base + 1] = static_cast<float>(h);
                    input.textureSizes[base + 2] = 1.f / static_cast<float>(w);
                    input.textureSizes[base + 3] = 1.f / static_cast<float>(h);
                }
                if (width > 65536 || height > 65536 || !width || !height) return decalFailure("Invalid original decal texture rectangle");
                const uint16 rect[] = {0, 0, static_cast<uint16>(width - 1), static_cast<uint16>(height - 1)};
                std::memcpy(value.record.data() + 116, rect, 8);
                const auto packed = wot::packStockSpaceDecal(input, value.record);
                const char* const maps[] = {"g_map1", "g_map2", "g_map3", "g_map4"};
                const Moo::VisualPtr visual = kind == 3 ? plane : cube;
                auto drawTechnique = [&](const char* name)
                {
                    if (!material->setTechnique(name) || !material->begin()) return false;
                    bool success = true;
                    for (uint32 pass = 0; pass < material->numPasses(); ++pass)
                    {
                        if (!material->beginPass(pass)) { success = false; break; }
                        // Material begin/pass selection can activate another ARIE
                        // binding. Set parameters on that actual pass binding.
                        ID3DXEffect* effect = material->pEffect()->pEffect();
                        bool bound = effect && SUCCEEDED(effect->SetTexture("g_texObjKind", kinds)) &&
                            SUCCEEDED(effect->SetVector("g_texObjKindDRRParams", &sourceProjection)) &&
                            SUCCEEDED(effect->SetBool("g_cameraIsInsideDecal", kind == 1)) &&
                            SUCCEEDED(effect->SetBool("g_useStencil", kind == 0)) &&
                            SUCCEEDED(effect->SetBool("g_pbsReady", resource.pbsReady)) &&
                            SUCCEEDED(effect->SetBool("g_enableSSAO", buffers->ambientOcclusionEnabled())) &&
                            SUCCEEDED(effect->SetInt("g_anisotropy", anisotropy)) &&
                            SUCCEEDED(effect->SetFloat("g_depthMipSkip", phase == 1 && buffers->depthEffectsHalfResolution() ? 1.f : 0.f)) &&
                            setStockDeferredDecalConversion(effect);
                        for (unsigned index = 0; bound && index != 4; ++index)
                            bound = SUCCEEDED(effect->SetTexture(maps[index], textures[index]));
                        const bool drawn = bound && SUCCEEDED(effect->CommitChanges()) && drawDecalGeometry(*this, visual, packed);
                        const bool ended = material->endPass();
                        if (!drawn || !ended) { success = false; break; }
                    }
                    const bool ended = material->end();
                    return success && ended;
                };
                if ((kind == 0 && !drawTechnique("STENCIL")) || !drawTechnique(techniques[value.material]))
                    return decalFailure("Cannot draw the original decal instance/technique");
            }
        }
    }
    return true;
}
}

bool initialiseStockSpaceDecals()
{
    if (renderer && renderer->active) return true;
    auto next = std::make_shared<DecalRenderer>();
    next->selection = selectedQuality;
    if (!next->initialise()) return false;
    renderer = next;
    return true;
}

void finaliseStockSpaceDecals()
{
    auto retired = renderer;
    renderer.reset();
    if (!retired) return;
    retired->active = false;
    retired->frame = false;
    retired->visible.clear();
    retired->deleteUnmanagedObjects();
    retired->deleteManagedObjects();
}

void beginStockSpaceDecals()
{
    auto owner = renderer;
    if (!owner || !owner->active) return;
    for (const auto& item : owner->visible)
        std::fill(item.state->queued.begin(), item.state->queued.end(), false);
    owner->visible.clear();
    owner->frame = true;
}

void endStockSpaceDecals()
{
    auto owner = renderer;
    if (!owner) return;
    owner->frame = false;
    clearDecalBinding(owner->material);
    for (const auto& item : owner->visible)
        std::fill(item.state->queued.begin(), item.state->queued.end(), false);
    owner->visible.clear();
}

void setStockSpaceDecalsQuality(int option)
{
    if (option < 0 || option >= 5) { decalFailure("Invalid native decal quality option"); return; }
    selectedQuality = option;
    auto owner = renderer;
    if (!owner) return;
    owner->selection = option;
}

bool drawStockSpaceDecalsPhase(unsigned phase)
{
    auto owner = renderer;
    return owner ? owner->draw(phase) : decalFailure("The original decal renderer is unavailable");
}
BW_END_NAMESPACE
