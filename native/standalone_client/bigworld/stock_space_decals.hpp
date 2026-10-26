#pragma once
#include "compiled_space/loader.hpp"
#include <memory>
#include <atomic>

BW_BEGIN_NAMESPACE
// Queued groups retain their real space-data owner until frame retirement.
bool initialiseStockSpaceDecals();
void finaliseStockSpaceDecals();
void beginStockSpaceDecals();
void endStockSpaceDecals();
void setStockSpaceDecalsQuality(int option);
bool drawStockSpaceDecalsPhase(unsigned phase);

namespace CompiledSpace
{
struct StockSpaceDecalState;
class StockSpaceDecalsLoader : public ILoader
{
public:
    explicit StockSpaceDecalsLoader(uint32 visibilityMask);
    virtual ~StockSpaceDecalsLoader();
    virtual bool doLoadFromSpace(ClientSpace* space, BinaryFormat& reader,
        const DataSectionPtr& settings, const Matrix& transform, const StringTable& strings);
    virtual bool doBind();
    virtual void doUnload();
    virtual float percentLoaded() const;
private:
    uint32 visibilityMask_;
    uint32 unloadDepth_ = 0;
    std::shared_ptr<StockSpaceDecalState> state_;
    std::atomic<bool> loaded_{false};
};
}
BW_END_NAMESPACE
