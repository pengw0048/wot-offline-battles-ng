#include "pch.hpp"
#include "stock_space_lights.hpp"
#include "wot/stock_light_binary.hpp"
#include "compiled_space/binary_format.hpp"
#include "compiled_space/string_table.hpp"
#include "space/client_space.hpp"
#include "scene/scene.hpp"
#include "math/convex_hull.hpp"
#include "moo/omni_light.hpp"
#include "moo/spot_light.hpp"
#include "moo/light_container.hpp"
#include "moo/animation_manager.hpp"
#include "moo/node.hpp"
#include "moo/debug_draw.hpp"
#include <cmath>
#include <vector>

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
namespace
{
using Binary = wot::StockLightBinary;
Vector3 lightPosition(const float* values) { return Vector3(values[0], values[1], values[2]); }
Moo::Colour lightColour(const float* values) { return Moo::Colour(values[0], values[1], values[2], values[3]); }

class StockOmni : public Moo::OmniLight
{
public:
    explicit StockOmni(const Binary::Omni& source) :
        Moo::OmniLight(lightColour(source.colour), lightPosition(source.position), source.innerRadius, source.outerRadius) {}
    void pulsePosition(const Vector3& value) { worldPosition_ = value; }
};

// 6cf6f0/6cfe30 is shared by PulseLight and PulseSpotLight. The latter's
// 6d00d0 wrapper deliberately passes a temporary position, not the light field.
struct Pulse
{
    Vector3 basePosition;
    Moo::Colour baseColour;
    Moo::AnimationPtr animation;
    wot::StockLightCurve curve;
    float positionTime = 0.f, colourTime = 0.f;

    void initialise(const float* p, const float* c, const char* resource,
        const Binary::Pulse& source, const std::vector<Binary::Frame>& frames)
    {
        basePosition = lightPosition(p);
        baseColour = lightColour(c);
        Moo::NodePtr node = new Moo::Node;
        animation = Moo::AnimationManager::instance().get(resource, node);
        curve.reset();
        for (size_t index = source.firstFrame; index < size_t(source.firstFrame) + source.frameCount; ++index)
            curve.addKey(frames[index].time, frames[index].value);
        if (source.duration >= 0.f)
            curve.loop(true, source.duration == 0.f ? curve.getTotalTime() : source.duration);
    }
    void tick(float dTime, Vector3& outputPosition, Moo::Colour& outputColour)
    {
        if (animation)
        {
            positionTime += dTime * 30.f;
            if (positionTime >= animation->totalTime()) positionTime = fmodf(positionTime, animation->totalTime());
            Matrix result = Matrix::identity;
            for (uint32 index = 0; index < animation->nChannelBinders(); ++index)
            {
                Moo::AnimationChannelPtr channel = animation->channelBinder(index).channel();
                if (!channel) continue;
                Matrix local;
                channel->result(positionTime, local);
                result.preMultiply(local);
            }
            outputPosition = basePosition + result.applyToOrigin();
        }
        colourTime += dTime;
        colourTime = fmodf(colourTime, curve.getTotalTime());
        const float value = curve.animate(colourTime);
        outputColour = Moo::Colour(baseColour.r * value, baseColour.g * value, baseColour.b * value, 1.f);
    }
};

bool intersects(const AABB& box, const Vector3& p, float radius) { return box.distance(p) <= radius; }
bool intersects(const ConvexHull& hull, const Vector3& p, float radius) { return hull.intersects(p, radius); }
void add(const Moo::OmniLightPtr& light, Moo::LightContainer& output) { output.addOmni(light); }
void add(const Moo::SpotLightPtr& light, Moo::LightContainer& output) { output.addSpot(light); }
template<class Query, class Lights>
size_t queryLightArray(const Query& query, const Lights& lights, Moo::LightContainer& output)
{
    size_t count = 0;
    for (const auto& light : lights)
    {
        // 6bf3c0/6bf680 read the local position/radius even after worldTransform.
        if (intersects(query, light->position(), light->outerRadius())) { add(light, output); ++count; }
    }
    return count;
}
template<class Lights> void debug(const Lights& lights, uint32 colour)
{
    for (const auto& light : lights)
    {
        const float radius = light->outerRadius();
        const Vector3 extent(radius, radius, radius);
        DebugDraw::bboxAdd(BoundingBox(light->position() - extent, light->position() + extent), colour);
    }
}
}

struct StockLightSceneLoader::State
{
    Binary binary;
    Scene* scene = NULL;
    bool bound = false;
    std::vector<Moo::OmniLightPtr> omnis, pulseOmnis;
    std::vector<Moo::SpotLightPtr> spots, pulseSpots;
    std::vector<std::unique_ptr<Pulse>> omniAnimations, spotAnimations;

    bool load(BinaryFormat& file, const StringTable& strings, const Matrix& transform)
    {
        const size_t section = file.findSection(FourCC("BWLC"));
        if (section == BinaryFormat::NOT_FOUND || file.sectionVersion(section) != 1) return false;
        auto* stream = file.openSection(section);
        if (!stream) return false;
        const size_t size = stream->size();
        const void* data = stream->readRaw(size);
        std::string error;
        const bool ok = binary.read(data, size, error);
        file.closeSection(stream);
        if (!ok) { ASSET_MSG("StockLightScene: %s\n", error.c_str()); return false; }
        for (const auto& source : binary.omnis) omnis.push_back(omni(source, transform));
        for (const auto& source : binary.spots) spots.push_back(spot(source, transform));
        omniAnimations.resize(binary.pulseOmnis.size());
        spotAnimations.resize(binary.pulseSpots.size());
        for (size_t index = 0; index < binary.pulseOmnis.size(); ++index)
        {
            const auto& source = binary.pulseOmnis[index];
            const char* resource = strings.entry(source.pulse.animation);
            if (!resource) return false;
            pulseOmnis.push_back(omni(source.light, transform));
            omniAnimations[index].reset(new Pulse);
            omniAnimations[index]->initialise(source.light.position, source.light.colour, resource, source.pulse, binary.frames);
        }
        for (size_t index = 0; index < binary.pulseSpots.size(); ++index)
        {
            const auto& source = binary.pulseSpots[index];
            const char* resource = strings.entry(source.pulse.animation);
            if (!resource) return false;
            pulseSpots.push_back(spot(source.light, transform));
            spotAnimations[index].reset(new Pulse);
            spotAnimations[index]->initialise(source.light.position, source.light.colour, resource, source.pulse, binary.frames);
        }
        return true;
    }
    static Moo::OmniLightPtr omni(const Binary::Omni& source, const Matrix& transform)
    {
        Moo::OmniLightPtr result = new StockOmni(source);
        result->multiplier(source.multiplier);
        result->stockSourceFlags(static_cast<uint8>(source.flags));
        result->stockCompiledWorldTransform(transform);
        return result;
    }
    static Moo::SpotLightPtr spot(const Binary::Spot& source, const Matrix& transform)
    {
        // Native converts the stored cosine to radians in 6bed00 and back in
        // 97b9d0. Preserve that round trip rather than assuming exact inversion.
        Moo::SpotLightPtr result = new Moo::SpotLight(lightColour(source.colour), lightPosition(source.position),
            lightPosition(source.direction), source.innerRadius, source.outerRadius, cosf(acosf(source.cosConeAngle)));
        result->multiplier(source.multiplier);
        result->stockSourceFlags(static_cast<uint8>(source.flags));
        result->stockCompiledWorldTransform(transform);
        return result;
    }
};

StockLightSceneLoader::StockLightSceneLoader() {}
StockLightSceneLoader::~StockLightSceneLoader() { doUnload(); }
bool StockLightSceneLoader::doLoadFromSpace(ClientSpace*, BinaryFormat& reader,
    const DataSectionPtr&, const Matrix& transform, const StringTable& strings)
{
    if (state_ || unloadDepth_)
    {
        ASSET_MSG("StockLightScene: previous mapping has not unloaded\n");
        return false;
    }
    std::shared_ptr<State> next(new State);
    if (!next->load(reader, strings, transform)) return false;
    state_ = next;
    return true;
}
bool StockLightSceneLoader::doBind()
{
    auto live = state_;
    if (!live) return false;
    if (live->bound) return true;
    live->scene = &space().scene();
    live->bound = true;
    live->scene->addProvider(this);
    return state_ == live && live->bound;
}
void StockLightSceneLoader::doUnload()
{
    auto live = state_;
    if (!live) return;
    state_.reset();
    struct Retiring
    {
        uint32& depth;
        explicit Retiring(uint32& value) : depth(value) { ++depth; }
        ~Retiring() { --depth; }
    } retiring(unloadDepth_);
    if (live->bound)
    {
        live->bound = false;
        live->scene->removeProvider(this);
    }
    live.reset();
}
float StockLightSceneLoader::percentLoaded() const { return 1.f; }
void StockLightSceneLoader::forEachObject(const ConstSceneObjectCallback&) const {}
void StockLightSceneLoader::forEachObject(const SceneObjectCallback&) {}
void* StockLightSceneLoader::getView(const SceneTypeSystem::RuntimeTypeID& type)
{
    void* output = NULL;
    exposeView<ILightSceneViewProvider>(this, type, output);
    exposeView<ITickSceneProvider>(this, type, output);
    return output;
}
size_t StockLightSceneLoader::intersect(const ConvexHull& hull, Moo::LightContainer& output) const
{
    auto live = state_;
    return live && live->bound ? queryLightArray(hull, live->omnis, output) +
        queryLightArray(hull, live->spots, output) + queryLightArray(hull, live->pulseOmnis, output) +
        queryLightArray(hull, live->pulseSpots, output) : 0;
}
size_t StockLightSceneLoader::intersect(const AABB& box, Moo::LightContainer& output) const
{
    auto live = state_;
    return live && live->bound ? queryLightArray(box, live->omnis, output) +
        queryLightArray(box, live->spots, output) + queryLightArray(box, live->pulseOmnis, output) +
        queryLightArray(box, live->pulseSpots, output) : 0;
}
void StockLightSceneLoader::debugDrawLights() const
{
    auto live = state_;
    if (!live || !live->bound) return;
    debug(live->omnis, Moo::PackedColours::RED);
    debug(live->spots, Moo::PackedColours::GREEN);
    debug(live->pulseOmnis, Moo::PackedColours::MAGENTA);
    debug(live->pulseSpots, Moo::PackedColours::MAGENTA);
}
void StockLightSceneLoader::tick(float dTime)
{
    auto live = state_;
    if (!live || !live->bound) return;
    for (size_t index = 0; index < live->omniAnimations.size(); ++index)
    {
        auto* light = static_cast<StockOmni*>(live->pulseOmnis[index].getObject());
        Vector3 p = light->worldPosition();
        Moo::Colour c = light->colour();
        live->omniAnimations[index]->tick(dTime, p, c);
        light->pulsePosition(p);
        light->colour(c);
        if (!live->bound) return;
    }
    for (size_t index = 0; index < live->spotAnimations.size(); ++index)
    {
        auto light = live->pulseSpots[index];
        Vector3 discarded = light->worldPosition();
        Moo::Colour c = light->colour();
        live->spotAnimations[index]->tick(dTime, discarded, c);
        light->colour(c);
        if (!live->bound) return;
    }
}
void StockLightSceneLoader::updateAnimations(float) {}
}
BW_END_NAMESPACE
