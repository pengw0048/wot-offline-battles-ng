#include "pch.hpp"
#include "stock_dynamic_decals.hpp"
#include "stock_ground_decals.hpp"
#include "stock_deferred_decal_draw.hpp"
#include "stock_space_decals_texture.hpp"
#include "wot/stock_dynamic_decals.hpp"
#include "moo/device_callback.hpp"
#include "moo/dynamic_decal.hpp"
#include "moo/format_name_map.hpp"
#include "moo/graphics_settings.hpp"
#include "moo/render_context.hpp"
#include "moo/renderer.hpp"
#include "moo/texture_manager.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/collision_obstacle.hpp"
#include "resmgr/auto_config.hpp"
#include "resmgr/bwresource.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include <algorithm>
#include <map>
#include <memory>

BW_BEGIN_NAMESPACE
namespace
{
AutoConfigString dynamicDecalConfiguration("system/decalsConfig");
uint32 nextDynamicDecalPriority = 0;

bool dynamicFailure(std::string& error, const char* message)
{
    error = message;
    ERROR_MSG("StockDynamicDecals: %s\n", message);
    return false;
}

class DecalRayHit : public CollisionCallback
{
public:
    bool found = false;
    WorldTriangle hit;
    int operator()(const CollisionObstacle& obstacle, const WorldTriangle& triangle, float) override
    {
        // a54600 uses the same a54420 ClosestTriangle consumer as traces.
        if ((triangle.collisionFlags() & 0x12u) == 2u) return COLLIDE_ALL;
        hit = WorldTriangle(obstacle.transform().applyPoint(triangle.v0()),
            obstacle.transform().applyPoint(triangle.v1()), obstacle.transform().applyPoint(triangle.v2()));
        hit.flags(triangle.flags());
        found = true;
        return COLLIDE_BEFORE;
    }
};

struct TextureConstraint
{
    unsigned minimumWidth, minimumHeight, maximumWidth, maximumHeight;
    D3DFORMAT format;
};
struct DynamicTextureSet
{
    std::array<Moo::BaseTexturePtr, 3> sources;
    std::map<unsigned, std::array<ComObjectWrap<DX::Texture>, 3>> views;
};
struct DynamicDecal
{
    wot::StockDynamicDecalInstance data;
    BoundingBox bounds;
    std::shared_ptr<DynamicTextureSet> textures;
};
struct DynamicQuality { float lodBias; unsigned mipSkip; int downsample; };

class DynamicDecalRenderer : public Moo::DeviceCallback
{
public:
    std::vector<DynamicDecal> decals;
    std::map<std::array<BW::string, 3>, std::weak_ptr<DynamicTextureSet>> textures;
    std::array<std::vector<TextureConstraint>, 3> constraints;
    const std::array<DynamicQuality, 5> qualities = {{{1.f, 0, 0}, {.8f, 1, 0},
        {.6f, 1, 1}, {.4f, 2, 1}, {0.f, 3, 1}}};
    ClientSpacePtr space;
    unsigned anisotropy = 2;
    int quality = 0;
    float time = 0.f;

    ~DynamicDecalRenderer() { deleteUnmanagedObjects(); }
    void syncSpace()
    {
        ClientSpacePtr current = DeprecatedSpaceHelpers::cameraSpace();
        if (current && current->cleared()) current = NULL;
        if (current != space) { decals.clear(); textures.clear(); space = current; }
    }
    bool load()
    {
        DataSectionPtr configuration = BWResource::openSection(dynamicDecalConfiguration.value());
        DataSectionPtr source = configuration ? configuration->openSection("dynamic") : NULL;
        std::string error;
        if (!source) return dynamicFailure(error, "The original dynamic decal configuration is unavailable");
        const int requested = source->readInt("anisotropy", 2);
        if (requested < 0 || requested > 256)
            return dynamicFailure(error, "Invalid original dynamic decal anisotropy");
        anisotropy = static_cast<unsigned>(requested);
        BW::vector<DataSectionPtr> types;
        source->openSections("type", types);
        for (const auto& type : types)
        {
            if (type->readString("name") != "GBufferDecals") continue;
            BW::vector<DataSectionPtr> atlases;
            type->openSections("atlas", atlases);
            if (atlases.size() != 3) return dynamicFailure(error, "Dynamic decals require their three original texture channels");
            for (unsigned channel = 0; channel != 3; ++channel)
            {
                BW::vector<DataSectionPtr> ranges;
                atlases[channel]->openSections("constrains", ranges);
                for (const auto& range : ranges)
                {
                    BW::string format = range->readString("format");
                    constraints[channel].push_back({range->readUInt("minWidth"), range->readUInt("minHeight"),
                        range->readUInt("maxWidth"), range->readUInt("maxHeight"), Moo::FormatNameMap::formatType(format)});
                }
                if (constraints[channel].empty()) return dynamicFailure(error, "A dynamic decal texture channel has no original constraints");
            }
        }
        for (const auto& channel : constraints)
            if (channel.empty()) return dynamicFailure(error, "The original dynamic decal atlas family is unavailable");
        const auto setting = Moo::GraphicsSetting::getFromLabel("DECALS_QUALITY");
        if (setting && setting->activeOption() >= 0) quality = setting->activeOption();
        return quality >= 0 && quality < 5;
    }
    std::shared_ptr<DynamicTextureSet> loadTextures(const std::array<BW::string, 3>& names, std::string& error)
    {
        auto found = textures.find(names);
        if (found != textures.end())
            if (auto live = found->second.lock()) return live;
        auto candidate = std::make_shared<DynamicTextureSet>();
        unsigned width = 0, height = 0;
        for (unsigned channel = 0; channel != 3; ++channel)
        {
            if (names[channel].empty() || !BWResource::fileExists(names[channel]))
            { dynamicFailure(error, "A required original dynamic decal texture is unavailable"); return {}; }
            auto source = Moo::TextureManager::instance()->getSystemMemoryTexture(names[channel]);
            if (!source || source->status() != Moo::BaseTexture::STATUS_READY || !source->pTexture() ||
                source->pTexture()->GetType() != D3DRTYPE_TEXTURE)
            { dynamicFailure(error, "An original dynamic decal DDS did not load"); return {}; }
            auto* texture = static_cast<DX::Texture*>(source->pTexture());
            D3DSURFACE_DESC desc;
            if (FAILED(texture->GetLevelDesc(0, &desc)))
            { dynamicFailure(error, "Cannot read the original dynamic decal DDS dimensions"); return {}; }
            bool accepted = false;
            for (const auto& range : constraints[channel])
                accepted |= desc.Format == range.format && desc.Width >= range.minimumWidth &&
                    desc.Width <= range.maximumWidth && desc.Height >= range.minimumHeight && desc.Height <= range.maximumHeight;
            if (!accepted || !desc.Width || !desc.Height || (desc.Width & (desc.Width - 1)) ||
                (desc.Height & (desc.Height - 1)) || (channel && (desc.Width != width || desc.Height != height)))
            { dynamicFailure(error, "A dynamic decal DDS violates its original format/size constraints"); return {}; }
            width = desc.Width; height = desc.Height;
            candidate->sources[channel] = source;
        }
        textures[names] = candidate;
        return candidate;
    }
    void tick(float elapsed)
    {
        syncSpace();
        time = wot::stockDynamicDecalAdd(time, elapsed);
        const Vector3 camera = Moo::rc().invView().applyToOrigin();
        const float eye[] = {camera.x, camera.y, camera.z};
        for (size_t index = 0; index < decals.size();)
        {
            auto& item = decals[index];
            if (!wot::stockDynamicDecalAlive(item.data, time))
            {
                if (index + 1 != decals.size()) item = std::move(decals.back());
                decals.pop_back();
                continue;
            }
            const float point[] = {item.data.position[0], item.data.position[1], item.data.position[2]};
            item.data.distanceSquared = wot::stockSpaceDecalDistance(point, eye, Moo::rc().lodZoomFactor());
            ++index;
        }
        for (auto it = textures.begin(); it != textures.end();)
            if (it->second.expired()) it = textures.erase(it); else ++it;
    }
    bool draw();
    void deleteUnmanagedObjects() override
    {
        clearStockDeferredDecalTextures();
        // DEFAULT-pool views must not remain in a device sampler at ResetEx.
        if (Moo::rc().device())
        {
            for (unsigned slot = 0; slot != 16; ++slot) Moo::rc().device()->SetTexture(slot, NULL);
            for (unsigned slot = 0; slot != 4; ++slot) Moo::rc().device()->SetTexture(D3DVERTEXTEXTURESAMPLER0 + slot, NULL);
            Moo::rc().invalidateStateCache();
        }
        for (auto& entry : textures) if (auto live = entry.second.lock()) live->views.clear();
    }
    bool recreateForD3DExDevice() const override { return true; }
};
std::shared_ptr<DynamicDecalRenderer> renderer;
}

float stockDynamicDecalGroupLifetime(const BW::string& name)
{
    // 933040 preserves the original missing-group lifetime.
    auto* owner = DynamicDecalManager::pInstance();
    return owner ? owner->getLifeTime(name) : 1000000000.f;
}

bool addStockDynamicDecal(float lifetime, const Vector3& start, const Vector3& end,
    const Vector2& size, float yaw, const BW::string& diffuse, const BW::string& normal,
    const BW::string& specular, bool useSimpleCollision, std::string& error)
{
    auto owner = renderer;
    if (!owner) return dynamicFailure(error, "The original dynamic decal renderer is unavailable");
    owner->syncSpace();
    if (!owner->space) return dynamicFailure(error, "The dynamic decal has no current camera space");
    ClientSpacePtr space = owner->space;
    DecalRayHit hit;
    const float distance = space->collide(start, end, hit);
    if (distance == -1.f || !hit.found)
    { error.clear(); return true; } // Original collision miss creates no decal.
    Vector3 direction = end - start;
    D3DXVec3Normalize(&direction, &direction);
    Vector3 axis = -hit.hit.normal();
    D3DXVec3Normalize(&axis, &axis);
    Vector3 position = start + direction * distance;
    float width=(hit.hit.flags()&TRIANGLE_TERRAIN)?1.f:DynamicDecalManager::instance().decalObjectImpactSizeCoefficient;
    float depth=.5f;
    if(!useSimpleCollision && (hit.hit.flags()&TRIANGLE_TERRAIN))
    {
        wot::GroundDecalMath::Projection projection;bool admitted=false;
        if(!queryStockGroundDecalProjection(space,start,end,size,hit.hit,projection,admitted,error))return false;
        if(!admitted) {error.clear();return true;}
        axis=Vector3(projection.axis[0],projection.axis[1],projection.axis[2]);
        position=start+direction*projection.distance;depth=projection.depth;
        width=projection.object?DynamicDecalManager::instance().decalObjectImpactSizeCoefficient:1.f;
    }
    Vector3 up(1.f, 0.f, 0.f);
    const float parallel = 1.f - std::abs(axis.x);
    if (-.01f < parallel && parallel < .01f) up = Vector3(0.f, 1.f, 0.f);
    const Vector3 target = position + axis;
    Matrix world, rotation, scale;
    D3DXMatrixLookAtLH(&world, &position, &target, &up);
    D3DXMatrixRotationZ(&rotation, yaw);
    D3DXMatrixMultiply(&world, &world, &rotation);
    if (!D3DXMatrixInverse(&world, NULL, &world))
        return dynamicFailure(error, "The actual dynamic decal projection is singular");
    D3DXMatrixScaling(&scale, size.x * width, size.y * width, depth);
    D3DXMatrixMultiply(&world, &scale, &world);
    DynamicDecal candidate;
    candidate.textures = owner->loadTextures({diffuse, normal, specular}, error);
    if (!candidate.textures) return false;
    candidate.data.position = {{world[3][0], world[3][1], world[3][2]}};
    for (unsigned row = 0; row != 3; ++row)
        candidate.data.scale[row] = std::sqrt(world[row][0] * world[row][0] +
            world[row][1] * world[row][1] + world[row][2] * world[row][2]);
    Vector3 y(world[1][0], world[1][1], world[1][2]), z(world[2][0], world[2][1], world[2][2]);
    D3DXVec3Normalize(&y, &y); D3DXVec3Normalize(&z, &z);
    const Vector3 zero(0.f, 0.f, 0.f);
    D3DXMatrixLookAtLH(&rotation, &zero, &z, &y);
    D3DXQUATERNION quaternion;
    D3DXQuaternionRotationMatrix(&quaternion, &rotation);
    std::memcpy(candidate.data.rotation.data(), &quaternion, 16);
    candidate.data.created = owner->time;
    candidate.data.lifetime = lifetime;
    candidate.data.priority = nextDynamicDecalPriority++;
    candidate.bounds.setBounds(Vector3(-.5f, -.5f, -.5f), Vector3(.5f, .5f, .5f));
    candidate.bounds.transformBy(world);
    if (renderer != owner || owner->space != space || space->cleared())
        return dynamicFailure(error, "The dynamic decal space retired during creation");
    owner->decals.push_back(std::move(candidate));
    error.clear();
    return true;
}

bool DynamicDecalRenderer::draw()
{
    syncSpace();
    const auto& option = qualities[quality];
    if (!space || decals.empty() || option.lodBias <= 0.f) return true;
    std::vector<DynamicDecal*> visible;
    for (auto& item : decals)
    {
        item.bounds.calculateOutcode(Moo::rc().viewProjection());
        if (!item.bounds.combinedOutcode()) visible.push_back(&item);
    }
    std::sort(visible.begin(), visible.end(), [](const DynamicDecal* a, const DynamicDecal* b)
        { return a->data.distanceSquared < b->data.distanceSquared; });
    std::vector<StockDeferredDecalPacket> packets;
    const float start = wot::stockDynamicDecalMul(100.f, option.lodBias);
    const float end = wot::stockDynamicDecalMul(150.f, option.lodBias);
    const float startSquared = wot::stockDynamicDecalMul(start, start);
    const float inverseRange = wot::stockDynamicDecalDiv(1.f,
        wot::stockDynamicDecalSub(wot::stockDynamicDecalMul(end, end), startSquared));
    const unsigned levels = wot::stockSpaceDecalMipLevels(anisotropy, option.mipSkip);
    std::string error;
    for (auto* item : visible)
    {
        const float alpha = wot::stockDynamicDecalAlpha(item->data, time, startSquared, inverseRange);
        if (alpha <= .003921569f) continue;
        StockDeferredDecalPacket packet;
        packet.material.controls[4] = anisotropy;
        packet.material.controls[5] = ((anisotropy >> 1) << 4) / (1u << option.mipSkip);
        packet.material.controls[6] = 1;
        auto& images = item->textures->views[option.mipSkip];
        unsigned width = 0, height = 0;
        for (unsigned channel = 0; channel != 3; ++channel)
        {
            auto* source = static_cast<DX::Texture*>(item->textures->sources[channel]->pTexture());
            D3DSURFACE_DESC desc;
            if (!source || FAILED(source->GetLevelDesc(option.mipSkip, &desc)))
                return dynamicFailure(error, "The dynamic decal source mip is unavailable");
            if (!images[channel] && FAILED(wot::copyStockSpaceDecalMipTexture(
                Moo::rc().device(), source, option.mipSkip, levels, &images[channel])))
                return dynamicFailure(error, "Cannot upload the original dynamic decal mip view");
            width = desc.Width; height = desc.Height;
            packet.textures[channel] = images[channel].pComObject();
            packet.material.textureSizes[channel * 4] = float(width);
            packet.material.textureSizes[channel * 4 + 1] = float(height);
            packet.material.textureSizes[channel * 4 + 2] = 1.f / float(width);
            packet.material.textureSizes[channel * 4 + 3] = 1.f / float(height);
        }
        const std::array<std::uint16_t, 4> rectangle = {{0, 0,
            static_cast<std::uint16_t>(width - 1), static_cast<std::uint16_t>(height - 1)}};
        packet.record = wot::stockDynamicDecalRecord(item->data, alpha, 0, rectangle, option.downsample);
        packets.push_back(packet);
    }
    // The shared renderer keeps native material, projection and priority order.
    std::sort(packets.begin(), packets.end(), [](const StockDeferredDecalPacket& a, const StockDeferredDecalPacket& b)
    {
        return wot::stockDecalAdmissionLess(stockDeferredDecalAdmissionKey(a.record),
            stockDeferredDecalAdmissionKey(b.record));
    });
    // a49990 applies the renderer's cap independently to each provider's
    // sorted emitted survivors, before camera-kind conversion or GPU packing.
    const unsigned limit = stockDeferredDecalVisibleLimit();
    if (packets.size() > limit) packets.resize(limit);
    return drawStockDeferredDecalPackets(packets, 0);
}

bool initialiseStockDynamicDecals()
{
    // Only the deferred DecalsManager::init calls this, before Renderer is inited.
    if (renderer) return true;
    auto next = std::make_shared<DynamicDecalRenderer>();
    if (!next->load()) return false;
    renderer = next;
    return true;
}
void finaliseStockDynamicDecals() { renderer.reset(); }
void tickStockDynamicDecals(float elapsed) { auto owner = renderer; if (owner) owner->tick(elapsed); }
void setStockDynamicDecalQuality(int option)
{
    if (option < 0 || option >= 5) { ERROR_MSG("StockDynamicDecals: invalid quality\n"); return; }
    if (renderer) renderer->quality = option;
}
bool drawStockDynamicDecals() { auto owner = renderer; return owner && owner->draw(); }
// 931650 zeroes only the deferred_decal::DynamicManager record count.
void clearStockDynamicDecals() { auto owner = renderer; if (owner) owner->decals.clear(); }
BW_END_NAMESPACE
