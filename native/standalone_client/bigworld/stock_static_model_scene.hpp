#pragma once

#include "compiled_space/loader.hpp"
#include <memory>

BW_BEGIN_NAMESPACE
class Scene;
namespace CompiledSpace
{
// The original mapping owns BSMI2 instances, BSMO1 definitions and BSMA1
// materials directly. No public BWMD/BWSS section or source-model conversion.
class StockStaticModelSceneLoader : public ILoader
{
public:
    explicit StockStaticModelSceneLoader( uint32 mappingMask );
    virtual ~StockStaticModelSceneLoader();
    virtual bool loadMustSucceed() const { return true; }
    static void registerHandlers( Scene & scene );

protected:
    virtual bool doLoadFromSpace( ClientSpace * space, BinaryFormat & file,
        const DataSectionPtr & settings, const Matrix & mappingTransform,
        const StringTable & strings );
    virtual bool doBind();
    virtual void doUnload();
    virtual float percentLoaded() const;

private:
    StockStaticModelSceneLoader( const StockStaticModelSceneLoader & );
    StockStaticModelSceneLoader & operator=( const StockStaticModelSceneLoader & );
    struct State;
    void removeMembership( const std::shared_ptr<State> & state );
    std::shared_ptr<State> state_;
    uint32 mappingMask_;
    uint32 unloadDepth_ = 0;
};
}
BW_END_NAMESPACE
