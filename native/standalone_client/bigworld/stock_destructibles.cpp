#include "pch.hpp"
#include "stock_destructibles.hpp"
#include "stock_destructible_falling_query.hpp"
#include <cmath>
#include "stock_space_sounds_data.hpp"
#include "compiled_space/binary_format.hpp"
#include "duplo/pyfashion.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "space/client_space.hpp"
#include "space/space_manager.hpp"
#include <algorithm>
#include <map>
#include <functional>
#include <vector>

BW_BEGIN_NAMESPACE
namespace
{
using Identities=wot::StockSpaceSoundIdentities;
std::map<ClientSpace*,std::weak_ptr<StockDestructiblesOwner>> owners;

bool gameEvent(const char* name,PyObject* arguments,const std::function<bool()>& valid={})
{
    if(!arguments)return false;
    if(!Py_IsInitialized()){Py_DECREF(arguments);return false;}
    PyObject* module=PyDict_GetItemString(PyImport_GetModuleDict(),"game");Py_XINCREF(module);
    PyObject* function=module?PyObject_GetAttrString(module,name):NULL;
    if(valid&&!valid()){Py_DECREF(arguments);Py_XDECREF(function);Py_XDECREF(module);return false;}
    PyObject* returned=function?PyObject_CallObject(function,arguments):NULL;
    Py_DECREF(arguments);Py_XDECREF(function);Py_XDECREF(module);
    if(!returned){if(PyErr_Occurred())PyErr_Print();else ERROR_MSG("StockDestructibles: game callback %s is unavailable\n",name);return false;}
    Py_DECREF(returned);return true;
}
Matrix chunkMatrix(int chunk)
{
    // 6ba890 uses signed high bits and the unsigned low byte, at100m cells.
    const int x=chunk>=0?chunk/256:-int((-(int64)chunk+255)/256);
    Matrix result;result.setTranslate(Vector3(float(x-127)*100.f,0.f,float((chunk&255)-127)*100.f));
    return result;
}
}

class StockDestructiblesOwner
{
public:
    struct Mapping
    {
        const void* identity;
        std::shared_ptr<const Identities> data;
        std::weak_ptr<StockDestructibleObjects> objects[2];
        bool bound=false,reverseReady=false;
    };
    ClientSpace* space;
    const SpaceID spaceID;
    std::vector<Mapping> mappings;
    explicit StockDestructiblesOwner(ClientSpace* value):space(value),spaceID(value->id()){}
    ~StockDestructiblesOwner(){const auto found=owners.find(space);if(found!=owners.end()&&found->second.expired())owners.erase(found);}
    Mapping& mapping(const void* identity)
    {
        for(auto& value:mappings)if(value.identity==identity)return value;
        mappings.push_back({identity,{},{},false,false});return mappings.back();
    }
    const Mapping* find(const void* identity)const
    {for(const auto& value:mappings)if(value.identity==identity)return &value;return NULL;}
    bool current()const
    {return SpaceManager::instance().space(spaceID)==space&&!space->cleared();}
    bool resolve(int chunk,int item,int module,std::shared_ptr<StockDestructibleObjects>& endpoint,uint32& index)const
    {
        if(!current()||item<0)return false;
        for(const auto& value:mappings)
        {
            if(!value.bound||!value.data)continue;
            for(const auto& record:value.data->chunks)
            {
                if(record.id!=uint32(chunk))continue;
                if(uint32(item)>=record.count)return false;
                const auto& row=value.data->items[std::size_t(record.first)+uint32(item)];
                const uint64 offset=uint64(row.object)+(module<0?0u:uint32(module));
                if(offset>=value.data->objects.size())return false;
                const uint32 object=value.data->objects[std::size_t(offset)];
                if(object==0xffffffffu)return false;
                endpoint=value.objects[object>>31].lock();index=object&0x7fffffffu;
                return endpoint!=NULL;
            }
        }
        return false;
    }
};

StockDestructibles acquireStockDestructibles(ClientSpace* space)
{
    if(!space||space->cleared())return {};
    auto& weak=owners[space];auto value=weak.lock();
    if(!value){value=std::make_shared<StockDestructiblesOwner>(space);weak=value;}
    return value;
}
void registerStockDestructibleObjects(const StockDestructibles& owner,const void* mapping,bool tree,
    const std::shared_ptr<StockDestructibleObjects>& objects)
{if(owner&&mapping)owner->mapping(mapping).objects[tree?1:0]=objects;}
void unregisterStockDestructibleObjects(const StockDestructibles& owner,const void* mapping,bool tree)
{if(owner)for(auto& value:owner->mappings)if(value.identity==mapping){value.objects[tree?1:0].reset();return;}}

void playStockModuleDestructionAnimation(const StockDestructibles& owner,const void* mapping,
    uint32 instance,bool shot,bool havok)
{
    if(!Py_IsInitialized()||!owner||!owner->current())return;
    const auto* value=owner->find(mapping);
    if(!value||!value->bound||!value->reverseReady||!value->data)return;
    // 6bab30's reverse map retains the last reference for an encoded object.
    bool found=false;uint32 chunk=0,item=0,module=0;
    for(const auto& record:value->data->chunks)
        for(uint32 row=0;row<record.count;++row)
        {
            const auto& span=value->data->items[std::size_t(record.first)+row];
            const uint64 count=uint32(span.unused-span.object+1u);
            for(uint64 index=0;index<count&&uint64(span.object)+index<value->data->objects.size();++index)
                if(value->data->objects[std::size_t(span.object)+std::size_t(index)]==instance)
                {chunk=record.id;item=row;module=uint32(index);found=true;}
        }
    if(found)gameEvent("wg_playModuleDestructionAnimation",Py_BuildValue("(IIIOO)",chunk,item,module,shot?Py_True:Py_False,havok?Py_True:Py_False),[owner,mapping] {const auto* value=owner->find(mapping);return owner->current()&&value&&value->bound;});
}

namespace
{
struct Resolved
{
    ClientSpacePtr space;
    StockDestructibles owner;
    std::shared_ptr<StockDestructibleObjects> objects;
    uint32 index=0;
    bool lookup(int spaceID,int chunk,int item,int module=-1)
    {
        space=SpaceManager::instance().space(spaceID);
        if(!space||space->cleared())return false;
        const auto found=owners.find(space.get());if(found==owners.end())return false;
        owner=found->second.lock();return owner&&owner->resolve(chunk,item,module,objects,index);
    }
    bool snapshot(StockDestructibleSnapshot& out)const{return objects&&objects->snapshot(index,out);}
};
PyObject* unavailable(const char* message)
{PyErr_SetString(PyExc_RuntimeError,message);return NULL;}
}

PyObject* wg_getChunkMatrix(int space,int chunk)
{
    if(!SpaceManager::instance().space(space))Py_RETURN_NONE;
    return Script::getData(chunkMatrix(chunk));
}
PyObject* wg_getDestructibleMatrix(int space,int chunk,int item)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item)||!object.snapshot(state))Py_RETURN_NONE;
    Matrix cell=chunkMatrix(chunk),inverse,local;D3DXMatrixInverse(&inverse,NULL,&cell);
    D3DXMatrixMultiply(&local,&state.world,&inverse);return Script::getData(local);
}
PyObject* wg_getChunkDestrFilenames(int space,int chunk)
{
    ClientSpacePtr native=SpaceManager::instance().space(space);
    if(!native||native->cleared())Py_RETURN_NONE;
    const auto found=owners.find(native.get());auto owner=found==owners.end()?StockDestructibles():found->second.lock();
    if(!owner)Py_RETURN_NONE;
    const Identities::Chunk* record=NULL;
    for(const auto& value:owner->mappings)
    {
        if(!value.bound||!value.data)continue;
        for(const auto& row:value.data->chunks)if(row.id==uint32(chunk)){record=&row;break;}
        if(record)break;
    }
    const uint32 count=record?record->count:0;
    PyObject* result=PyList_New(0);if(!result)return NULL;
    for(uint32 index=0;index<count;++index)
    {
        Resolved object;StockDestructibleSnapshot state;
        if(!object.lookup(space,chunk,int(index))||!object.snapshot(state))continue;
        PyObject* name=Script::getData(state.filename);
        if(!name||PyList_Append(result,name)<0){Py_XDECREF(name);Py_DECREF(result);return NULL;}
        Py_DECREF(name);
    }
    return result;
}
PyObject* wg_getDestructibleFilename(int space,int chunk,int item)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item)||!object.snapshot(state))Py_RETURN_NONE;
    return Script::getData(state.filename);
}
PyObject* wg_getDestructibleEffectCategory(int space,int chunk,int item,int module)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item,module)||!object.snapshot(state))Py_RETURN_NONE;
    return Script::getData(state.category);
}
PyObject* wg_getDestructibleEffectName(int space,int chunk,int item,int module,const BW::string& effect)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item,module)||!object.snapshot(state))Py_RETURN_NONE;
    int slot=-1;
    if(effect=="lifetimeEffect")slot=0;else if(effect=="fractureEffect")slot=1;
    else if(effect=="touchdownEffect")slot=2;else if(effect=="decayEffect")slot=3;
    else if(effect=="effect"||effect=="ramEffect")slot=4;else if(effect=="hitEffect")slot=5;
    return Script::getData(slot<0||state.effects[slot].empty()?BW::string("none"):state.effects[slot]);
}
PyObject* wg_getDestructibleEffectScale(int space,int chunk,int item,int module)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item,module)||!object.snapshot(state))Py_RETURN_NONE;
    return Script::getData(state.effectScale);
}
PyObject* wg_getDestructibleHeight(int space,int chunk,int item)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item)||!object.snapshot(state))Py_RETURN_NONE;
    return Script::getData(state.height);
}
PyObject* wg_getFallingParams(int space,int chunk,int item)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item)||!object.snapshot(state))Py_RETURN_NONE;
    return Script::getData(state.fallingParameters);
}
PyObject* wg_getNMHardPointMatrix(int space,int chunk,int item,int module)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item,module)||!object.snapshot(state))Py_RETURN_NONE;
    return Script::getData(state.hardpointWorld);
}
PyObject* wg_getDestructibleFallPitchConstr(uint32 space,uint32 chunk,uint32 item,float yaw)
{
    Resolved object;StockDestructibleSnapshot state;
    if(!object.lookup(space,chunk,item)||!object.snapshot(state))Py_RETURN_NONE;
    if(!std::isfinite(yaw))return unavailable("A destructible fall yaw must be finite");
    Matrix cell=chunkMatrix(chunk),inverse,local;D3DXMatrixInverse(&inverse,NULL,&cell);
    D3DXMatrixMultiply(&local,&state.world,&inverse);
    // Preserve 6b5119..6b5143's local + chunk translation round trip.
    const Vector3 origin=local.applyToOrigin()+cell.applyToOrigin();
    const float pitch=queryStockDestructibleFallPitch(*object.space,origin,state.height,yaw);
    return Py_BuildValue("(fI)",pitch,0u); // 6b4f73 deliberately returns kind0.
}
float wg_getDestructibleLifetimeEffectChance(int,int,int,int){return 1.f;} // 6b2990 validates four ints then returns1.
PyObject* wg_getChunkModelFashion(int,int,int){Py_RETURN_NONE;} // Both original compiled type handlers return NULL.
bool wg_setChunkModelFashion(int space,int chunk,int item,PyObjectPtr fashion,bool)
{Resolved object;return PyFashion::Check(fashion.get())&&object.lookup(space,chunk,item);}
PyObject* wg_setDestructibleMatrix(int space,int chunk,int item,MatrixProviderPtr local,float wind)
{
    if(!local)return unavailable("A destructible transform requires a MatrixProvider");
    Resolved object;if(!object.lookup(space,chunk,item))Py_RETURN_NONE;
    Matrix value,cell=chunkMatrix(chunk),world;local->matrix(value);D3DXMatrixMultiply(&world,&value,&cell);
    std::string error;if(!object.objects->transform(object.index,world,wind,error))return unavailable(error.c_str());
    Py_RETURN_NONE;
}
PyObject* wg_destroyFragile(int space,int chunk,int item,bool animate,bool shot,bool havok)
{
    Resolved object;if(!object.lookup(space,chunk,item))Py_RETURN_NONE;
    std::string error;if(!object.objects->destroy(object.index,animate,shot,havok,error))return unavailable(error.c_str());
    Py_RETURN_NONE;
}
PyObject* wg_destroyModule(int space,int chunk,int item,int module,bool animate,bool shot,bool havok)
{
    Resolved object;if(!object.lookup(space,chunk,item,module))Py_RETURN_NONE;
    std::string error;if(!object.objects->destroy(object.index,animate,shot,havok,error))return unavailable(error.c_str());
    Py_RETURN_NONE;
}
PyObject* wg_restoreDestructibles(int spaceID)
{
    ClientSpacePtr space=SpaceManager::instance().space(spaceID);
    if(!space||space->cleared())Py_RETURN_NONE;
    const auto found=owners.find(space.get());auto owner=found==owners.end()?StockDestructibles():found->second.lock();
    if(!owner)Py_RETURN_NONE;
    // Pin callbacks independently before restoration can re-enter scene owners.
    std::vector<std::shared_ptr<StockDestructibleObjects>> objects;
    for(const auto& mapping:owner->mappings)for(const auto& weak:mapping.objects)
        if(auto object=weak.lock())objects.push_back(object);
    std::string error;for(const auto& object:objects)
        if(!object->restore(error))return unavailable(error.c_str());
    Py_RETURN_NONE;
}

#define STOCK_DESTR_THREE ARG(int,ARG(int,ARG(int,END)))
#define STOCK_DESTR_FOUR ARG(int,ARG(int,ARG(int,ARG(int,END))))
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getChunkMatrix,ARG(int,ARG(int,END)),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getChunkDestrFilenames,ARG(int,ARG(int,END)),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getDestructibleMatrix,STOCK_DESTR_THREE,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getDestructibleFilename,STOCK_DESTR_THREE,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getDestructibleHeight,STOCK_DESTR_THREE,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getFallingParams,STOCK_DESTR_THREE,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getChunkModelFashion,STOCK_DESTR_THREE,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getDestructibleEffectCategory,STOCK_DESTR_FOUR,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getDestructibleEffectScale,STOCK_DESTR_FOUR,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getNMHardPointMatrix,STOCK_DESTR_FOUR,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getDestructibleFallPitchConstr,ARG(uint32,ARG(uint32,ARG(uint32,ARG(float,END)))),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETDATA,wg_getDestructibleLifetimeEffectChance,STOCK_DESTR_FOUR,BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_getDestructibleEffectName,ARG(int,ARG(int,ARG(int,ARG(int,ARG(BW::string,END))))),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETDATA,wg_setChunkModelFashion,ARG(int,ARG(int,ARG(int,ARG(PyObjectPtr,ARG(bool,END))))),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_setDestructibleMatrix,ARG(int,ARG(int,ARG(int,ARG(MatrixProviderPtr,ARG(float,END))))),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_destroyFragile,ARG(int,ARG(int,ARG(int,ARG(bool,ARG(bool,ARG(bool,END)))))),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_destroyModule,ARG(int,ARG(int,ARG(int,ARG(int,ARG(bool,ARG(bool,ARG(bool,END))))))),BigWorld)
PY_AUTO_MODULE_FUNCTION(RETOWN,wg_restoreDestructibles,ARG(int,END),BigWorld)
#undef STOCK_DESTR_THREE
#undef STOCK_DESTR_FOUR

namespace CompiledSpace
{
struct StockDestructiblesSceneLoader::State
{
    ClientSpace* space=NULL;
    SpaceID spaceID=0;
    const void* identity=NULL;
    StockDestructibles owner;
    std::shared_ptr<Identities> data;
    bool bound=false;
};
StockDestructiblesSceneLoader::StockDestructiblesSceneLoader(){}
StockDestructiblesSceneLoader::~StockDestructiblesSceneLoader(){doUnload();}
bool StockDestructiblesSceneLoader::doLoadFromSpace(ClientSpace* space,BinaryFormat& file,
    const DataSectionPtr&,const Matrix&,const StringTable&)
{
    if(state_||unloading_)return false;
    const size_t section=file.findSection(FourCC("WGDE"));
    if(section==BinaryFormat::NOT_FOUND||file.sectionVersion(section)!=1)return false;
    auto* stream=file.openSection(section,true);if(!stream)return false;
    const size_t size=stream->size();const void* bytes=stream->readRaw(size);
    auto data=std::make_shared<Identities>();std::string error;
    const bool valid=bytes&&data->read(bytes,size,error);file.closeSection(stream);
    if(!valid){ERROR_MSG("StockDestructibles: %s\n",error.c_str());return false;}
    auto state=std::make_shared<State>();state->space=space;state->spaceID=space->id();state->identity=&file;state->data=data;state_=state;return true;
}
bool StockDestructiblesSceneLoader::doBind()
{
    auto state=state_;if(!state)return false;if(state->bound)return true;
    if(!Py_IsInitialized())return false;
    state->owner=acquireStockDestructibles(state->space);if(!state->owner)return false;
    auto& mapping=state->owner->mapping(state->identity);mapping.data=state->data;mapping.bound=true;mapping.reverseReady=false;state->bound=true;
    // The original call is synchronous and can replay queued destruction.
    // Install the complete mapping before exposing the first chunk callback.
    for(const auto& chunk:state->data->chunks)
    {
        gameEvent("wg_onChunkLoad",Py_BuildValue("(iIIO)",state->spaceID,chunk.id,chunk.count,Py_True),[state]{return state->bound&&state->owner->current();});
        if(state_!=state||!state->bound)return false;
    }
    // 6bab30 builds the reverse animation map after every chunk-load callback.
    // Forward lookup is already live so queued static destruction can replay.
    state->owner->mapping(state->identity).reverseReady=true;
    return true;
}
void StockDestructiblesSceneLoader::doUnload()
{
    auto state=state_;if(!state)return;state_.reset();++unloading_;
    const bool notify=state->bound;state->bound=false;
    if(state->owner)
    {
        auto& mapping=state->owner->mapping(state->identity);mapping.bound=false;mapping.reverseReady=false;mapping.data.reset();
    }
    if(notify&&Py_IsInitialized())for(const auto& chunk:state->data->chunks)
        gameEvent("wg_onChunkLoose",Py_BuildValue("(iIO)",state->spaceID,chunk.id,Py_True));
    --unloading_;
}
float StockDestructiblesSceneLoader::percentLoaded()const{return state_?1.f:0.f;}
}
BW_END_NAMESPACE
