#pragma once
#include "compiled_space/loader.hpp"
#include <cstdint>
#include <memory>

BW_BEGIN_NAMESPACE
namespace CompiledSpace
{
class StockTreeSceneLoader : public ILoader
{
public:
    // The mapping creation parameter supplies the actual mask. There is no
    // implicit all-visible mask or a fabricated runtime re-filter operation.
    explicit StockTreeSceneLoader( uint32 mask );
    virtual ~StockTreeSceneLoader();
    virtual bool loadMustSucceed() const { return true; }

protected:
    virtual bool doLoadFromSpace( ClientSpace * space, BinaryFormat & reader,
        const DataSectionPtr & settings, const Matrix & mappingTransform,
        const StringTable & strings );
    virtual bool doBind();
    virtual void doUnload();
    virtual float percentLoaded() const;

private:
    StockTreeSceneLoader( const StockTreeSceneLoader & );
    StockTreeSceneLoader & operator=( const StockTreeSceneLoader & );
    struct State;
    void removeMembership( const std::shared_ptr<State> & state );
    std::shared_ptr<State> state_;
    uint32 mask_;
    uint32 unloadDepth_ = 0;
};
}
BW_END_NAMESPACE
