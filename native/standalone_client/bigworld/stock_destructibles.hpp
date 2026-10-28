#pragma once
#include "compiled_space/loader.hpp"
#include "cstdmf/bw_string.hpp"
#include "math/matrix.hpp"
#include <array>
#include <memory>
#include <string>

BW_BEGIN_NAMESPACE
class ClientSpace;
struct StockDestructibleSnapshot
{
    Matrix world,hardpointWorld,fallingParameters;
    BW::string filename;
    int category=-1;
    float height=0.f,effectScale=1.f;
    // lifetime, fracture, touchdown, decay, ram/effect, hit.
    std::array<BW::string,6> effects;
    StockDestructibleSnapshot() {fallingParameters.setZero();}
};

class StockDestructibleObjects
{
public:
    virtual ~StockDestructibleObjects() {}
    virtual bool snapshot(uint32 index,StockDestructibleSnapshot& output)const=0;
    virtual bool transform(uint32 index,const Matrix& world,float wind,std::string& error)=0;
    virtual bool destroy(uint32 index,bool animate,bool shot,bool havok,std::string& error)=0;
    virtual bool restore(std::string& error)=0;
};
class StockDestructiblesOwner;
using StockDestructibles=std::shared_ptr<StockDestructiblesOwner>;
StockDestructibles acquireStockDestructibles(ClientSpace* space);
// Registrations retain actual mapping identity and weak native object endpoints.
void registerStockDestructibleObjects(const StockDestructibles& owner,const void* mapping,bool tree,
    const std::shared_ptr<StockDestructibleObjects>& objects);
void unregisterStockDestructibleObjects(const StockDestructibles& owner,const void* mapping,bool tree);
void playStockModuleDestructionAnimation(const StockDestructibles& owner,const void* mapping,
    uint32 instance,bool shot,bool havok);

namespace CompiledSpace
{
class StockDestructiblesSceneLoader : public ILoader
{
public:
    StockDestructiblesSceneLoader();
    virtual ~StockDestructiblesSceneLoader();
    bool loadMustSucceed()const override{return true;}
protected:
    bool doLoadFromSpace(ClientSpace* space,BinaryFormat& file,const DataSectionPtr& settings,
        const Matrix& transform,const StringTable& strings)override;
    bool doBind()override;
    void doUnload()override;
    float percentLoaded()const override;
private:
    struct State;
    std::shared_ptr<State> state_;
    uint32 unloading_=0;
};
}
BW_END_NAMESPACE
