#pragma once
#include "compiled_space/loader.hpp"
#include <memory>
#include <atomic>

BW_BEGIN_NAMESPACE
class Scene;
namespace CompiledSpace
{
class StockFlareSceneLoader : public ILoader
{
public:
    explicit StockFlareSceneLoader(uint32 mappingMask);
    virtual ~StockFlareSceneLoader();
    static void registerHandlers(Scene& scene);
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
    std::atomic<bool> loaded_{false};
    uint32 mappingMask_, unloadDepth_ = 0;
};
}
BW_END_NAMESPACE
