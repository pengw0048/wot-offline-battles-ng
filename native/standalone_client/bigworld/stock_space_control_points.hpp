#pragma once
#include "compiled_space/loader.hpp"
#include <atomic>
#include <memory>

BW_BEGIN_NAMESPACE
class Scene;
extern int StockControlPoints_token;
namespace CompiledSpace
{
class StockControlPointSceneLoader : public ILoader
{
public:
    explicit StockControlPointSceneLoader(uint32 mappingMask);
    virtual ~StockControlPointSceneLoader();
    static void registerHandlers(Scene& scene);
protected:
    virtual bool doLoadFromSpace(ClientSpace* space, BinaryFormat& file,
        const DataSectionPtr& settings, const Matrix& transform, const StringTable& strings);
    virtual bool doBind();
    virtual void doUnload();
    virtual float percentLoaded() const;
private:
    struct State;
    std::shared_ptr<State> state_;
    std::atomic<bool> loaded_{false};
    uint32 mappingMask_, unloadDepth_ = 0;
};
}
BW_END_NAMESPACE
