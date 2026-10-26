#pragma once

#include "compiled_space/loader.hpp"
#include "scene/scene_provider.hpp"
#include "scene/light_scene_view.hpp"
#include "scene/tick_scene_view.hpp"
#include <memory>

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
// Original BWLC1 owns four light classes; it is independent of BWSS/VLOS.
class StockLightSceneLoader : public ILoader, public SceneProvider,
    public ILightSceneViewProvider, public ITickSceneProvider
{
public:
    StockLightSceneLoader();
    virtual ~StockLightSceneLoader();
    virtual bool doLoadFromSpace(ClientSpace* space, BinaryFormat& reader,
        const DataSectionPtr& settings, const Matrix& transform, const StringTable& strings);
    virtual bool doBind();
    virtual void doUnload();
    virtual float percentLoaded() const;
    virtual void forEachObject(const ConstSceneObjectCallback& function) const;
    virtual void forEachObject(const SceneObjectCallback& function);
    virtual size_t intersect(const ConvexHull& hull, Moo::LightContainer& output) const;
    virtual size_t intersect(const AABB& box, Moo::LightContainer& output) const;
    virtual void debugDrawLights() const;
    virtual void tick(float dTime);
    virtual void updateAnimations(float dTime);
private:
    virtual void* getView(const SceneTypeSystem::RuntimeTypeID& type);
    struct State;
    std::shared_ptr<State> state_;
    uint32 unloadDepth_ = 0;
};
}
BW_END_NAMESPACE
