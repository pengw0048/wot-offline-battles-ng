#pragma once
#include "compiled_space/loader.hpp"
#include "space/client_space.hpp"
#include <atomic>
#include <cstdint>
#include <memory>

BW_BEGIN_NAMESPACE
struct StockSpaceSoundOwner;
using StockSpaceSounds = std::shared_ptr<StockSpaceSoundOwner>;
using StockSpaceSoundHandle = std::uint64_t;
static const StockSpaceSoundHandle INVALID_STOCK_SPACE_SOUND = ~StockSpaceSoundHandle(0);
StockSpaceSounds acquireStockSpaceSounds(ClientSpace* space);
StockSpaceSoundHandle addStockSpaceModelSound(const StockSpaceSounds& owner,
    uint32 definitionIndex, const Matrix& world);
StockSpaceSoundHandle addStockSpaceNamedSound(const StockSpaceSounds& owner,
    const BW::string& name, const Matrix& world);
void removeStockSpaceSound(const StockSpaceSounds& owner, StockSpaceSoundHandle handle);
// The mapping identity is its actual BinaryFormat owner, never an object
// pointer masquerading as a compiled instance index. WGDE supplies the index.
void registerStockSpaceObjectSound(const StockSpaceSounds& owner,const void* mapping,
    bool tree,uint32 index,StockSpaceSoundHandle handle);
bool removeStockSpaceObjectSound(const StockSpaceSounds& owner,uint32 chunk,uint32 item);
// Original clearEntitiesAndSpaces preclear affects only the captured current space.
void clearStockCurrentSpaceSounds(const ClientSpacePtr& expectedSpace);
// Retire before ClientSpace clear/destruction; no owner retains the space.
void retireStockSpaceSounds(ClientSpace* space);
void finiStockSpaceSounds();
extern int StockSpaceSounds_token;
namespace CompiledSpace
{
class StockSpaceSoundLoader : public ILoader
{
public:
    StockSpaceSoundLoader();
    virtual ~StockSpaceSoundLoader();
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
    unsigned unloadDepth_ = 0;
};
}
BW_END_NAMESPACE
