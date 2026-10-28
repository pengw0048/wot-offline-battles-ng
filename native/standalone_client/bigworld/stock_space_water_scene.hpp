#pragma once
#include "compiled_space/loader.hpp"
#include <memory>

BW_BEGIN_NAMESPACE
class Scene;
class Water;
class Vector3;
namespace CompiledSpace
{
// BWWa2 owns complete Water surfaces and a separate set of visible local cells.
class StockWaterSceneLoader : public ILoader
{
public:
    StockWaterSceneLoader();
    virtual ~StockWaterSceneLoader();
    virtual bool loadMustSucceed() const { return true; }
    static void registerHandlers(Scene& scene);
    static float collide(const Water* water,const Vector3& start,const Vector3& end);
protected:
    virtual bool doLoadFromSpace(ClientSpace* space, BinaryFormat& file,
        const DataSectionPtr& settings, const Matrix& mappingTransform, const StringTable& strings);
    virtual bool doBind();
    virtual void doUnload();
    virtual float percentLoaded() const;
private:
    struct State;
    void removeMembership(const std::shared_ptr<State>& state);
    std::shared_ptr<State> state_;
    uint32 unloadDepth_ = 0;
};
}
BW_END_NAMESPACE
