// Production space lifecycle methods and original engine bodies, with bounded callback owners.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <Python.h>
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <cstdlib>
#else
#include <strings.h>
#define _stricmp strcasecmp
#endif
#define WOT_STOCK_SPACE_LIFECYCLE_PORTABLE
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define PY_MODULE_FUNCTION(...)
#define BW_GUARD
#define MF_ASSERT assert
#define ERROR_MSG(...) BW::testError(__VA_ARGS__)
#define BW_STATIC_ASSERT(EXPR,MESSAGE) static_assert(EXPR,#MESSAGE)
#define WARNING_MSG(...) BW::testError(__VA_ARGS__)
#define MF_ASSERT_DEV(EXPR) do{if(!(EXPR))BW::testError("MF_ASSERT_DEV");}while(0)
#define IF_NOT_MF_ASSERT_DEV(EXPR) if(!(EXPR)&&(BW::testError("IF_NOT_MF_ASSERT_DEV"),true))
#define MF_VERIFY(EXPR) do{if(!(EXPR))BW::testError("MF_VERIFY");}while(0)
#define MEMTRACKER_SCOPED(NAME)
#define INLINE inline

namespace BW
{
using SpaceID=int;
using uint=unsigned int;
using string=std::string;
const SpaceID NULL_SPACE_ID=0;
using uint64=std::uint64_t;
template<class T>using vector=std::vector<T>;
static unsigned errors=0;
static void testError(const char*,...){++errors;}
static std::vector<std::string> events;
static std::vector<PyMethodDef>& moduleMethods(){static std::vector<PyMethodDef> methods;return methods;}
struct PyModuleMethodLink
{
    PyModuleMethodLink(const char*,const char* name,PyCFunction method,const char* doc)
    {moduleMethods().push_back({const_cast<char*>(name),method,METH_VARARGS,const_cast<char*>(doc)});}
};
struct ClientSpace
{
    int identifier; unsigned references=0,clears=0; bool retired=false,repeatable=false; std::function<void()> onClear;
    explicit ClientSpace(int id):identifier(id){}
    ~ClientSpace();
    int id()const{return identifier;}
    bool cleared()const{return retired;}
    void clear()
    {assert(repeatable||!retired);retired=true;++clears;events.push_back("space:"+std::to_string(id()));if(onClear)onClear();}
};
struct ClientSpacePtr
{
    ClientSpace* value=NULL;
    ClientSpacePtr(){}
    ClientSpacePtr(ClientSpace* p):value(p){if(value)++value->references;}
    ClientSpacePtr(const ClientSpacePtr& other):ClientSpacePtr(other.value){}
    ~ClientSpacePtr(){if(value&&!--value->references)delete value;}
    ClientSpacePtr& operator=(const ClientSpacePtr& other)
    {ClientSpacePtr pin(other);std::swap(value,pin.value);return *this;}
    explicit operator bool()const{return value!=NULL;}
    ClientSpace* get()const{return value;}
    ClientSpace* operator->()const{return value;}
};
static bool retainConnectionStorage=false;
static void* reusableConnectionStorage=NULL;
struct BWConnection
{
    static void* operator new(std::size_t bytes)
    {
        if(reusableConnectionStorage){void* memory=reusableConnectionStorage;reusableConnectionStorage=NULL;return memory;}
        return ::operator new(bytes);
    }
    static void operator delete(void* memory)
    {
        if(retainConnectionStorage){assert(!reusableConnectionStorage);reusableConnectionStorage=memory;}
        else ::operator delete(memory);
    }
    int id;unsigned clears=0,updates=0,sends=0,entities=0;bool lastKeep=false;std::function<void()> onClear,onUpdate,onDelete;
    // Script entities created through the public createEntity body.
    std::vector<struct Entity*> live;
    explicit BWConnection(int value):id(value){}
    virtual ~BWConnection();
    double clientTime()const{return 123.;}
    virtual SpaceID spaceID()const{return NULL_SPACE_ID;}
    virtual void clearAllEntities(bool keepLocalEntities=false)
    {
        ++clears;lastKeep=keepLocalEntities;if(!keepLocalEntities)entities=0;
        events.push_back("entities:"+std::to_string(id));if(onClear)onClear();
        if(!keepLocalEntities)destroyEntities();
    }
    void destroyEntities();
    struct Entity* createLocalEntity(int,SpaceID,int,const struct Position3D&,const struct Direction3D&,struct MemoryOStream&);
    void update(float){++updates;if(onUpdate)onUpdate();}
    void updateServer(){++sends;}
};
struct BWNullConnection:BWConnection
{
    SpaceID space=NULL_SPACE_ID;int playerID=0;
    explicit BWNullConnection(int id,SpaceID spaceID=NULL_SPACE_ID):BWConnection(id),space(spaceID){}
    SpaceID spaceID()const override{return space;}
    struct Entity* pPlayer()const{return NULL;}
    // Public 14.4.1 and #1513 7d7a80 keep a player-owning null connection intact.
    void clearAllEntities(bool keepLocalEntities=false) override
    {if(!playerID)BWConnection::clearAllEntities(keepLocalEntities);}
    void destroyPlayerCellEntity(){events.push_back("cell");}
    void destroyPlayerBaseEntity(){events.push_back("base");}
};
struct BWServerConnection:BWConnection
{
    bool logging=false,online=false;
    explicit BWServerConnection(int id):BWConnection(id){}
    bool isLoggingIn()const{return logging;} bool isOnline()const{return online;}
};
struct BWReplayConnection:BWConnection
{
    bool playing=false;explicit BWReplayConnection(int id):BWConnection(id){}
    void* pReplayController()const{return playing?reinterpret_cast<void*>(1):NULL;}
};
struct ConnectionControl
{
    enum Mode{NONE,ONLINE,OFFLINE,REPLAY};
    using Connections=std::vector<BWConnection*>;
    using ClientSpaceConnections=std::map<int,BWNullConnection*>;
    using ClientSpaces=std::map<int,ClientSpacePtr>;
    BWConnection* pConnection_=NULL; Mode mode_=NONE;bool isInTick_=false;double offlineTime_=0.;
    std::uint64_t stockMainSpaceRevision_=0;
    Connections condemnedConnections_;ClientSpaceConnections clientSpaceConnections_;ClientSpaces clientSpaces_;
    struct Helper
    {
        int nextConnection=1000;
        void update(BWConnection* connection,float dt){if(connection)connection->update(dt);}
        BWNullConnection* createNullConnection(double,SpaceID spaceID){return new BWNullConnection(nextConnection++,spaceID);}
    }connectionHelper_;
    struct Dispatcher{int processOnce(bool){return 0;}}dispatcher_;
    static ConnectionControl& instance(){static ConnectionControl owner;return owner;}
    BWNullConnection* pNullConnection(){return mode_==OFFLINE?static_cast<BWNullConnection*>(pConnection_):NULL;}
    BWServerConnection* pServerConnection(){return mode_==ONLINE?static_cast<BWServerConnection*>(pConnection_):NULL;}
    BWReplayConnection* pReplayConnection(){return mode_==REPLAY?static_cast<BWReplayConnection*>(pConnection_):NULL;}
    void clearStockMainSpace(bool keepLocalEntities=false);void clearSpaces();void tick(float);
    SpaceID nextLocalSpaceID();SpaceID createLocalSpace();double gameTime()const;void fini();
    unsigned stockClientSpacesClosedAt_=~0u;
    BWConnection* pConnection()const{return pConnection_;}BWConnection* pSpaceConnection(SpaceID)const;
    void legacyClearSpaces();void destroyMainConnection();
};
struct SpaceManager
{
    static const int LOCAL_SPACE_START=1<<30;
    using SpaceMap=std::map<int,ClientSpace*>;
    SpaceMap spaces_,localSpaces_;
    static SpaceManager& instance(){static SpaceManager owner;return owner;}
    std::function<void(ClientSpace*)> onCreate;
    unsigned stockInitGeneration_=0;void init();
    ClientSpace* space(int id){const auto& map=id>=LOCAL_SPACE_START?localSpaces_:spaces_;auto p=map.find(id);return p==map.end()?NULL:p->second;}
    bool isLocalSpace(SpaceID id)const{return id>=LOCAL_SPACE_START;}
    // Public SpaceManager::createSpace publishes a raw pointer, then notifies.
    ClientSpace* createSpace(SpaceID id)
    {
        assert(!space(id));auto* result=new ClientSpace(id);
        (isLocalSpace(id)?localSpaces_:spaces_)[id]=result;events.push_back("create:"+std::to_string(id));
        if(onCreate)onCreate(result);return result;
    }
};
struct App
{
    bool quiting=false;
    static App& instance(){static App owner;return owner;}
    bool isQuiting(){return quiting;}
};
ClientSpace::~ClientSpace()
{
    auto& manager=SpaceManager::instance();
    auto& map=id()>=SpaceManager::LOCAL_SPACE_START?manager.localSpaces_:manager.spaces_;
    auto found=map.find(id());if(found!=map.end()&&found->second==this)map.erase(found);
    events.push_back("destroy:"+std::to_string(id()));
}
namespace DeprecatedSpaceHelpers
{
static ClientSpacePtr current;
static ClientSpacePtr cameraSpace(){return current;}
}
namespace Script
{
int setData(PyObject*,bool&,const char*);int setData(PyObject*,int&,const char*);
PyObject* getData(const bool);PyObject* getData(const int);
PyObject* argCountError(const char*,int,int,...);
@RETDATA@
}
@CONVERTER@
@INT_CONVERTER@
@GETDATA_BOOL@
@GETDATA_INT@
@ARGCOUNT@
@PYAUTO@
namespace Script
{
bool isFinalised(){return false;}
inline void resolvePythonModuleAndFunctionNames(PyObject*,char*,size_t){}
PyObject* ask(PyObject*,PyObject*,const char*,bool,bool);
@SCRIPT_CALL@
}
@SCRIPT_ASK@
}
@SOUND_TEST_SUPPORT@
#include "@PRODUCTION_FILE@"
namespace BW
{
@CLEAR_SPACES@
@TICK@
@NEXT_LOCAL_SPACE_ID@
@SPACE_CONNECTION@
@SPACE_MANAGER_INIT@
@FINI@
// Entity layer: fakes around the extracted public player/entity bodies.
using EntityID=int;
const EntityID NULL_ENTITY_ID=0;
struct Position3D{float x=0,y=0,z=0;};
struct Direction3D{explicit Direction3D(const Vector3&){}};
struct MemoryOStream{explicit MemoryOStream(int){}};
struct ScriptDict{PyObject* object=NULL;};
struct ScriptObject
{
    PyObject* value;
    static ScriptObject createFrom(EntityID id){return {PyInt_FromLong(id)};}
    PyObject* newRef(){return value;}
};
struct Entity;
struct PyEntity;
struct EntityType
{
    std::string name_;PyTypeObject* class_=NULL;PyTypeObject* playerClass_=NULL;int index_=0;
    static std::vector<EntityType*>& all(){static std::vector<EntityType*> types;return types;}
    static EntityType* find(const string& name){for(auto* type:all())if(type->name_==name)return type;return NULL;}
    PyTypeObject* pClass()const{return class_;}PyTypeObject* pPlayerClass()const{return playerClass_;}
    int index()const{return index_;}const std::string& name()const{return name_;}
    void prepareCreationStream(MemoryOStream&,const ScriptDict&){}
};
struct EntityPtr
{
    Entity* value;Entity* get()const{return value;}Entity* operator->()const{return value;}
    explicit operator bool()const{return value!=NULL;}
};
struct PyEntityPtr
{
    PyEntity* value=NULL;
    PyEntityPtr(){}
    explicit PyEntityPtr(PyEntity* entity);
    PyEntityPtr(const PyEntityPtr& other):PyEntityPtr(other.value){}
    PyEntityPtr& operator=(const PyEntityPtr& other){PyEntityPtr pin(other);std::swap(value,pin.value);return *this;}
    ~PyEntityPtr();
    PyEntity* get()const{return value;}PyEntity* operator->()const{return value;}
    explicit operator bool()const{return value!=NULL;}
    bool operator==(const PyEntity* other)const{return value==other;}
    PyObject* newRef()const;
};
struct PyEntity:PyObject
{
    Entity* pEntity_;PyObject* cell_;PyObject* base_;
    static PyTypeObject s_type_;
    EntityPtr pEntity()const{return {pEntity_};}
    const char* typeName()const{return ob_type->tp_name;}
    bool isDestroyed()const;
    // Public setClass assigns __class__ through pySetAttribute.
    void setClass(PyTypeObject* pClass)
    {
        MF_ASSERT_DEV(PyObject_IsSubclass((PyObject*)pClass,(PyObject*)&s_type_));
        MF_VERIFY(PyObject_SetAttrString(this,"__class__",(PyObject*)pClass)==0);
    }
    bool makePlayer();bool makeNonPlayer();void onEntityDestroyed();
};
PyTypeObject PyEntity::s_type_={PyVarObject_HEAD_INIT(NULL,0)"BigWorld.Entity",sizeof(PyEntity)};
PyEntityPtr::PyEntityPtr(PyEntity* entity):value(entity){Py_XINCREF(value);}
PyEntityPtr::~PyEntityPtr(){Py_XDECREF(value);}
PyObject* PyEntityPtr::newRef()const{Py_XINCREF(value);return value;}
static PyObject* entityDictionary=NULL;
struct EntitySpace{struct Enviro{int playerAttachments(){return 0;}};Enviro enviro(){return {};}};
struct Entity
{
    struct Census
    {
        void insert(Entity* entity);void erase(Entity* entity);
    };
    EntityID id_;EntityType* type_;BWConnection* owner_;PyEntityPtr pPyEntity_;bool destroyed_=false;
    static Census census_;
    const EntityType& type()const{return *type_;}
    PyEntityPtr pPyEntity()const{return pPyEntity_;}
    bool isDestroyed()const{return destroyed_;}
    EntityID entityID()const{return id_;}
    EntitySpace* pSpace()const{return NULL;}
    void abortLoadingPrerequisites(){}
    void onDestroyed();
};
using BWEntity=Entity;
Entity::Census Entity::census_;
void Entity::Census::insert(Entity* entity)
{PyObject* key=PyInt_FromLong(entity->id_);PyDict_SetItem(entityDictionary,key,entity->pPyEntity_.get());Py_DECREF(key);}
void Entity::Census::erase(Entity* entity)
{PyObject* key=PyInt_FromLong(entity->id_);if(PyDict_DelItem(entityDictionary,key))PyErr_Clear();Py_DECREF(key);}
bool PyEntity::isDestroyed()const{return pEntity_->isDestroyed();}
struct PyAttachmentPtr{explicit PyAttachmentPtr(int){}};
struct AttachedPS{struct Node{void detach(PyAttachmentPtr){}}*pNode_;int pAttachment_;};
struct EntityPhotonOccluder{explicit EntityPhotonOccluder(Entity&){}};
struct LensEffectManager
{
    static LensEffectManager& instance(){static LensEffectManager owner;return owner;}
    void delPhotonOccluder(EntityPhotonOccluder*){}void addPhotonOccluder(EntityPhotonOccluder*){}
};
struct ZBufferOccluder{static bool isAvailable(){return true;}};
struct ScriptPlayer
{
    Entity* pEntity_=NULL;std::vector<AttachedPS> attachedPSs_;EntityPhotonOccluder* playerFlareCollider_=NULL;
    static ScriptPlayer& instance(){static ScriptPlayer owner;return owner;}
    static Entity* entity(){return instance().pEntity_;}
    bool setPlayer(Entity* newPlayer);
    void updateWeatherParticleSystems(int){}
};
@SET_PLAYER@
@MAKE_PLAYER@
@MAKE_NON_PLAYER@
@ON_ENTITY_DESTROYED@
@ON_DESTROYED@
static PyObject* createPlayerEntity(const string&,const Position3D&,const Vector3&,const ScriptDict&)
{PyErr_SetString(PyExc_AssertionError,"main offline createPlayerEntity is outside this fixture");return NULL;}
@CREATE_ENTITY@
@PLAYER@
// PyEntity keeps its Entity alive (EntityPtr), as in the engine.
static void deallocEntity(PyObject* object)
{delete static_cast<PyEntity*>(object)->pEntity_;Py_TYPE(object)->tp_free(object);}
// Entity creation and destruction in a connection: fixture, not engine code.
Entity* BWConnection::createLocalEntity(int typeIndex,SpaceID,int,const Position3D&,const Direction3D&,MemoryOStream&)
{
    static EntityID nextEntity=500;
    EntityType* type=EntityType::all()[typeIndex];
    PyObject* object=type->class_->tp_alloc(type->class_,0);
    if(!object)return NULL;
    auto* entity=new Entity{nextEntity++,type,this,PyEntityPtr()};
    static_cast<PyEntity*>(object)->pEntity_=entity;
    entity->pPyEntity_=PyEntityPtr(static_cast<PyEntity*>(object));Py_DECREF(object);
    live.push_back(entity);Entity::census_.insert(entity);
    // The native factory prints script constructor errors and keeps the entity.
    Script::call(PyObject_GetAttrString(object,"__init__"),PyTuple_New(0),"Entity::__init__: ",false);
    return entity;
}
void BWConnection::destroyEntities()
{
    const std::vector<Entity*> snapshot=live;
    for(Entity* entity:snapshot)
    {
        if(std::find(live.begin(),live.end(),entity)==live.end())continue;
        Script::call(PyObject_GetAttrString(entity->pPyEntity_.get(),"onLeaveWorld"),PyTuple_New(0),
            "Entity::onLeaveWorld: ",true);
        if(std::find(live.begin(),live.end(),entity)==live.end())continue;
        live.erase(std::find(live.begin(),live.end(),entity));
        // BWEntity::destroy marks the entity destroyed before onDestroyed.
        entity->destroyed_=true;entity->onDestroyed();
    }
}
BWConnection::~BWConnection()
{
    events.push_back("delete:"+std::to_string(id));if(onDelete)onDelete();
    destroyEntities();
}
@CREATE_LOCAL_SPACE@
@GAME_TIME@
@LEGACY_CLEAR@
@LEGACY_DESTROY@
}
static std::vector<BW::ClientSpacePtr> pins;
static BW::ClientSpacePtr addSpace(int id)
{
    BW::ClientSpacePtr result(new BW::ClientSpace(id));
    auto& manager=BW::SpaceManager::instance();
    auto& map=id>=BW::SpaceManager::LOCAL_SPACE_START?manager.localSpaces_:manager.spaces_;
    map[id]=result.get();pins.push_back(result);return result;
}
static void reset()
{
    BW::onAudioCreate={};BW::onAudioUpdate={};BW::onAudioPlay={};BW::onAudioStop={};
    BW::DeprecatedSpaceHelpers::current=BW::ClientSpacePtr();
    BW::finiStockSpaceSounds();BW::audioVoices.clear();
    auto& owner=BW::ConnectionControl::instance();owner.isInTick_=false;
    if(owner.pConnection_){owner.pConnection_->onClear={};owner.pConnection_->onDelete={};owner.clearSpaces();}
    for(auto& p:owner.clientSpaceConnections_){p.second->onClear={};p.second->onDelete={};}
    for(const auto& space:pins)space->onClear={};
    BW::clearStockEntitiesAndSpaces(false);
    for(auto* connection:owner.condemnedConnections_){connection->onDelete={};delete connection;}
    owner.condemnedConnections_.clear();
    pins.clear();auto& manager=BW::SpaceManager::instance();manager.spaces_.clear();manager.localSpaces_.clear();
    BW::events.clear();BW::errors=0;
    // A later initialisation phase reopens creation after a fini test.
    if(!BW::stockClientSpacesOpen())BW::SpaceManager::instance().init();
}
static void ownerTests()
{
    (void)&BW::fail;
    using namespace BW;
    auto& owner=ConnectionControl::instance();
    const int local=SpaceManager::LOCAL_SPACE_START;
    // Main connection and callback ordering; explicit keep still clears local connections.
    auto remote=addSpace(1),connected=addSpace(local+1),orphan=addSpace(local+2);
    owner.pConnection_=new BWNullConnection(1);owner.mode_=ConnectionControl::OFFLINE;
    owner.clientSpaceConnections_[local+1]=new BWNullConnection(2);owner.clientSpaces_[local+1]=connected;
    clearStockEntitiesAndSpaces(true);
    assert((events==std::vector<std::string>{"cell","base","entities:1","delete:1","entities:2",
        "space:"+std::to_string(local+1),"delete:2","space:1"}));
    assert(remote->cleared()&&connected->cleared()&&!orphan->cleared());
    clearStockEntitiesAndSpaces(false);assert(orphan->cleared());
    clearStockEntitiesAndSpaces(false);reset();
    // Real static group preclear is current-space-only and precedes entities.
    auto sounding=addSpace(local+12),untouched=addSpace(local+13);
    auto soundOwner=acquireStockSpaceSounds(sounding.get());
    auto otherOwner=acquireStockSpaceSounds(untouched.get());
    auto def=std::make_shared<Definition>();def->name="static_event";def->events={"event"};
    soundOwner->add(def,Vector3());otherOwner->add(def,Vector3());
    DeprecatedSpaceHelpers::current=sounding;
    owner.pConnection_=new BWNullConnection(20);owner.mode_=ConnectionControl::OFFLINE;
    owner.pConnection_->onClear=[&](){assert(soundOwner->groups.empty());assert(otherOwner->groups.size()==1);};
    onAudioStop=[&](){events.push_back("audio");};
    clearStockEntitiesAndSpaces(true);onAudioStop={};
    assert(events.front()=="audio"&&soundOwner->groups.empty()&&otherOwner->groups.size()==1);
    assert(!sounding->cleared()&&!untouched->cleared());reset();
    // A synchronous preclear backend can retire the entry-time main and
    // publish a replacement before the wrapper reaches its main clear stage.
    for(unsigned retirement=0;retirement<2;++retirement)
    {
    auto preclearSpace=addSpace(21);
    auto preclearOwner=acquireStockSpaceSounds(preclearSpace.get());
    preclearOwner->add(def,Vector3());DeprecatedSpaceHelpers::current=preclearSpace;
    auto* preclearMain=new BWNullConnection(21);owner.pConnection_=preclearMain;owner.mode_=ConnectionControl::OFFLINE;
    BWNullConnection* afterPreclear=NULL;ClientSpacePtr afterSpace,afterLocalSpace;
    auto* afterLocal=new BWNullConnection(23);bool oldRetired=false,replaced=false;
    preclearMain->onDelete=[&](){oldRetired=true;};
    onAudioStop=[&](){if(replaced)return;replaced=true;
        retainConnectionStorage=true;
        if(retirement==0)owner.clearSpaces();else owner.destroyMainConnection();
        retainConnectionStorage=false;assert(oldRetired);
        afterPreclear=new BWNullConnection(22);assert(afterPreclear==preclearMain);
        afterSpace=addSpace(22);owner.pConnection_=afterPreclear;owner.mode_=ConnectionControl::OFFLINE;
        afterLocalSpace=addSpace(local+22);owner.clientSpaceConnections_[local+22]=afterLocal;
        owner.clientSpaces_[local+22]=afterLocalSpace;};
    clearStockEntitiesAndSpaces(false);onAudioStop={};
    assert(replaced&&owner.pConnection_==afterPreclear&&afterPreclear->clears==0&&owner.mode_==ConnectionControl::OFFLINE);
    assert(preclearSpace->cleared()&&!afterSpace->cleared()&&!afterLocalSpace->cleared()&&afterLocal->clears==0);
    reset();
    }
    // The original online/login/replay guards refuse connection clearing,
    // while the wrapper still continues its later local/geometry stages.
    for(unsigned mode=0;mode<3;++mode)
    {
        auto* server=new BWServerConnection(3);server->online=mode==0;server->logging=mode==1;
        auto* replay=new BWReplayConnection(4);replay->playing=true;
        owner.pConnection_=mode<2?static_cast<BWConnection*>(server):replay;
        owner.mode_=mode<2?ConnectionControl::ONLINE:ConnectionControl::REPLAY;
        if(mode<2)delete replay;else delete server;
        auto space=addSpace(2);clearStockEntitiesAndSpaces(false);
        assert(owner.pConnection_&&owner.pConnection_->clears==0&&errors==1&&space->cleared());
        if(mode<2){server->online=false;server->logging=false;}else replay->playing=false;
        reset();
    }
    // A synchronous leave callback starts another main connection. Neither
    // the retiring clear nor its later space stage may remove the new owner.
    auto oldSpace=addSpace(10);auto* old=new BWNullConnection(5);
    owner.pConnection_=old;owner.mode_=ConnectionControl::OFFLINE;
    ClientSpacePtr replacementSpace,replacementLocalSpace;
    auto* replacement=new BWNullConnection(6);
    auto* replacementLocal=new BWNullConnection(60);bool replacementLocalDeleted=false;
    replacementLocal->onDelete=[&](){replacementLocalDeleted=true;};
    old->onClear=[&](){assert(!owner.pConnection_&&owner.mode_==ConnectionControl::NONE);
        replacementSpace=addSpace(11);owner.pConnection_=replacement;owner.mode_=ConnectionControl::OFFLINE;
        replacementLocalSpace=addSpace(local+30);owner.clientSpaceConnections_[local+30]=replacementLocal;
        owner.clientSpaces_[local+30]=replacementLocalSpace;};
    clearStockEntitiesAndSpaces(false);
    assert(owner.pConnection_==replacement&&owner.mode_==ConnectionControl::OFFLINE);
    assert(oldSpace->cleared()&&!replacementSpace->cleared()&&replacement->clears==0);
    assert(!replacementLocalDeleted&&!replacementLocalSpace->cleared()&&replacementLocal->clears==0);reset();
    // During the actual original tick body, clear replaces the local map.
    // The old tick snapshot must neither send on a retired connection nor tick
    // newly published state. Deferred deletion happens after update returns.
    auto a=addSpace(local+4),b=addSpace(local+5);
    auto* first=new BWNullConnection(7);auto* second=new BWNullConnection(8);
    owner.clientSpaceConnections_[local+4]=first;owner.clientSpaces_[local+4]=a;
    owner.clientSpaceConnections_[local+5]=second;owner.clientSpaces_[local+5]=b;
    auto* newborn=new BWNullConnection(9);ClientSpacePtr newbornSpace;
    bool returned=false;
    first->onUpdate=[&](){clearStockEntitiesAndSpaces(false);assert(owner.condemnedConnections_.size()==2);
        newbornSpace=addSpace(local+6);owner.clientSpaceConnections_[local+6]=newborn;
        owner.clientSpaces_[local+6]=newbornSpace;returned=true;};
    first->onDelete=[&](){assert(returned);};
    owner.tick(.1f);
    assert(owner.condemnedConnections_.empty()&&!owner.isInTick_);
    assert(newborn->updates==0&&newborn->sends==0&&!newbornSpace->cleared());
    owner.tick(.1f);assert(newborn->updates==1&&newborn->sends==1);reset();
    // A destructor can itself retire another connection into the tick queue.
    auto* doomed=new BWNullConnection(12);auto* next=new BWNullConnection(13);
    owner.condemnedConnections_.push_back(doomed);
    doomed->onDelete=[&](){owner.pConnection_=next;owner.mode_=ConnectionControl::OFFLINE;owner.clearSpaces();};
    owner.tick(.1f);assert(owner.condemnedConnections_.empty()&&!owner.pConnection_);reset();
    // A space clear callback replaces a later same-ID map entry. The old
    // snapshot pins its former owner, but must not clear the replacement.
    auto firstSpace=addSpace(1),later=addSpace(2);ClientSpacePtr nextSpace;
    firstSpace->onClear=[&](){nextSpace=addSpace(2);};
    clearStockEntitiesAndSpaces(false);
    assert(firstSpace->cleared()&&!later->cleared()&&!nextSpace->cleared());reset();
}
static BW::BWNullConnection* held(BW::SpaceID id)
{
    auto& map=BW::ConnectionControl::instance().clientSpaceConnections_;
    auto found=map.find(id);return found==map.end()?NULL:found->second;
}
static bool takeError(PyObject* type,const char* text)
{
    PyObject *kind=NULL,*value=NULL,*trace=NULL;PyErr_Fetch(&kind,&value,&trace);
    bool matched=kind==type&&value&&PyString_Check(value)&&std::string(PyString_AsString(value))==text;
    if(!matched&&value&&!PyString_Check(value))
    {PyObject* rendered=PyObject_Str(value);matched=kind==type&&rendered&&std::string(PyString_AsString(rendered))==text;Py_XDECREF(rendered);}
    Py_XDECREF(kind);Py_XDECREF(value);Py_XDECREF(trace);return matched;
}
static std::string id(BW::SpaceID value){return std::to_string(value);}
static BW::SpaceID create()
{
    PyObject* result=BW::createSpace();assert(result&&PyInt_Check(result));
    const BW::SpaceID value=BW::SpaceID(PyInt_AsLong(result));Py_DECREF(result);return value;
}
static void clientSpaceTests()
{
    using namespace BW;
    auto& owner=ConnectionControl::instance();auto& manager=SpaceManager::instance();
    // Multiple simultaneous spaces: distinct IDs, held spaces and connections.
    const SpaceID a=create(),b=create(),c=create();
    assert(a>=SpaceManager::LOCAL_SPACE_START&&a<b&&b<c);
    for(SpaceID space:{a,b,c})assert(isClientSpace(space)&&held(space)&&held(space)->spaceID()==space&&
        owner.clientSpaces_.at(space).get()==manager.space(space)&&manager.space(space)->references==1);
    held(a)->entities=2;held(b)->entities=3;
    // clearSpace is geometry only and repeatable, exactly like ac2bb0.
    manager.space(b)->repeatable=true;
    assert(clearSpace(b)&&clearSpace(b)&&manager.space(b)->clears==2);
    assert(isClientSpace(b)&&held(b)->entities==3&&held(b)->clears==0);
    // releaseSpace drops the held owner without clearing geometry.
    events.clear();const std::string aConnection=std::to_string(held(a)->id);
    assert(releaseSpace(a));
    assert((events==std::vector<std::string>{"entities:"+aConnection,"delete:"+aConnection,"destroy:"+id(a)}));
    assert(!manager.space(a)&&!held(a)&&!owner.clientSpaces_.count(a)&&!isClientSpace(a));
    assert(!releaseSpace(a)&&takeError(PyExc_ValueError,("BigWorld.releaseSpace(): No space ID "+id(a)).c_str()));
    assert(!clearSpace(a)&&takeError(PyExc_ValueError,("BigWorld.clearSpace(): No space ID "+id(a)).c_str()));
    // An externally pinned space survives release and is no longer held.
    ClientSpacePtr pinned(manager.space(c));
    assert(releaseSpace(c)&&manager.space(c)==pinned.get()&&!pinned->cleared()&&!isClientSpace(c));
    const std::string notHeld=" is not a client space (or is no longer held)";
    assert(!releaseSpace(c)&&takeError(PyExc_ValueError,("BigWorld.releaseSpace(): Space ID "+id(c)+notHeld).c_str()));
    assert(!clearSpace(c)&&takeError(PyExc_ValueError,("BigWorld.clearSpace(): Space ID "+id(c)+notHeld).c_str()));
    pinned=ClientSpacePtr();assert(!manager.space(c));
    // Server spaces and the main offline space are never client spaces.
    const SpaceID probe=create();assert(releaseSpace(probe));
    auto server=addSpace(5);auto mainSpace=addSpace(probe+1);
    owner.pConnection_=new BWNullConnection(50,mainSpace->id());owner.mode_=ConnectionControl::OFFLINE;
    for(SpaceID space:{server->id(),mainSpace->id()})
    {
        assert(!isClientSpace(space));
        assert(!clearSpace(space)&&takeError(PyExc_ValueError,("BigWorld.clearSpace(): Space ID "+id(space)+notHeld).c_str()));
        assert(!releaseSpace(space)&&takeError(PyExc_ValueError,("BigWorld.releaseSpace(): Space ID "+id(space)+notHeld).c_str()));
    }
    assert(!isClientSpace(424242)&&!PyErr_Occurred());
    // nextLocalSpaceID skips the main offline space and held IDs.
    const SpaceID next=create();assert(next==probe+2&&isClientSpace(next));
    // clearAllSpaces keep=true clears only non-client spaces; no connection work.
    held(next)->entities=4;
    clearAllSpaces(true);
    assert(server->clears==1&&manager.space(b)->clears==2&&!manager.space(next)->cleared()&&mainSpace->clears==0);
    assert(owner.pConnection_&&held(b)->entities==3&&held(next)->entities==4&&held(next)->clears==0);
    server->repeatable=true;manager.space(b)->repeatable=true;
    clearAllSpaces(false);
    assert(server->clears==2&&manager.space(b)->clears==3&&manager.space(next)->clears==1&&mainSpace->clears==1);
    assert(isClientSpace(b)&&isClientSpace(next)&&held(next)->entities==4&&owner.pConnection_);
    // resetEntityManager validates keepPlayerAround after both conversions.
    assert(!resetEntityManager(true,false)&&takeError(PyExc_ValueError,"Setting keepPlayerAround to True is now invalid"));
    assert(owner.pConnection_);
    BWConnection* main=owner.pConnection_;bool mainKept=false;
    main->onClear=[&](){mainKept=main->lastKeep;assert(!owner.pConnection_);};
    PyObject* none=resetEntityManager(false,true);assert(none==Py_None);Py_DECREF(none);
    assert(mainKept&&!owner.pConnection_&&owner.mode_==ConnectionControl::NONE&&isClientSpace(b)&&held(b)->entities==3);
    // An online owner refuses the 63f610 clear without a forced disconnect.
    auto* online=new BWServerConnection(51);online->online=true;owner.pConnection_=online;owner.mode_=ConnectionControl::ONLINE;
    errors=0;none=resetEntityManager(false,false);Py_DECREF(none);
    assert(owner.pConnection_==online&&online->clears==0&&errors==1);
    online->online=false;reset();
    // A space-created notification may create another space before the
    // outer space is held; both end up held under distinct IDs.
    SpaceID nested=NULL_SPACE_ID;
    manager.onCreate=[&](ClientSpace* created){manager.onCreate={};
        assert(!isClientSpace(created->id()));nested=create();};
    const SpaceID outer=create();
    assert(nested==outer+1&&isClientSpace(outer)&&isClientSpace(nested));
    reset();
    // Leave callbacks during release see a retired identity and may create,
    // clear, release or reset; the replacement is preserved.
    const SpaceID first=create(),second=create();held(first)->entities=1;
    SpaceID replacement=NULL_SPACE_ID;bool nestedRelease=true;
    held(first)->onClear=[&](){
        assert(!isClientSpace(first)&&!held(first)&&manager.space(first));
        nestedRelease=releaseSpace(first);PyErr_Clear();
        replacement=create();assert(releaseSpace(second));
        clearAllSpaces(false);clearStockEntitiesAndSpaces(true);};
    events.clear();assert(releaseSpace(first));
    assert(!nestedRelease&&!manager.space(first)&&!manager.space(second)&&!held(replacement)&&!PyErr_Occurred());
    assert(std::count(events.begin(),events.end(),"destroy:"+id(first))==1);
    reset();
    const SpaceID survivor=create(),leaving=create();SpaceID created=NULL_SPACE_ID;
    held(leaving)->onClear=[&](){created=create();held(created)->entities=9;};
    assert(releaseSpace(leaving)&&isClientSpace(created)&&held(created)->entities==9&&isClientSpace(survivor));
    reset();
    // Release inside the original tick body defers deletion and never ticks
    // a retired or newborn connection in the old snapshot.
    const SpaceID ticking=create(),other=create();
    BWNullConnection* tickingConnection=held(ticking);BWNullConnection* otherConnection=held(other);
    SpaceID born=NULL_SPACE_ID;bool returned=false;
    tickingConnection->onUpdate=[&](){assert(releaseSpace(other)&&releaseSpace(ticking));
        born=create();assert(owner.condemnedConnections_.size()==2);returned=true;};
    tickingConnection->onDelete=[&](){assert(returned&&tickingConnection->sends==0);};
    otherConnection->onDelete=[&](){assert(otherConnection->updates==0&&otherConnection->sends==0);};
    owner.tick(.1f);
    assert(returned&&owner.condemnedConnections_.empty()&&held(born)->updates==0&&held(born)->sends==0);
    assert(tickingConnection!=otherConnection);
    owner.tick(.1f);assert(held(born)->updates==1&&held(born)->sends==1);
    reset();
    // A held map entry missing its ClientSpace is the 63ff90 MF_ASSERT_DEV
    // branch: the connection is still retired exactly once.
    const SpaceID partial=create();ClientSpacePtr kept=owner.clientSpaces_.at(partial);
    owner.clientSpaces_.erase(partial);errors=0;events.clear();
    assert(releaseSpace(partial)&&errors==1&&!held(partial)&&kept->clears==0);
    kept=ClientSpacePtr();reset();
    // A player-owning local null connection keeps its entities, as both the
    // public and #1513 BWNullConnection::clearAllEntities do.
    const SpaceID battle=create();held(battle)->playerID=7;held(battle)->entities=2;
    BWNullConnection* battleConnection=held(battle);unsigned battleEntities=0;
    battleConnection->onDelete=[&](){battleEntities=battleConnection->entities;};
    assert(releaseSpace(battle)&&battleEntities==2);
    reset();
    std::puts("Client-space create/clear/release/isClientSpace/clearAllSpaces/resetEntityManager owner paths PASS");
}

// The production ConnectionControl::fini body and its terminal fence.
static void finiTests()
{
    using namespace BW;
    auto& owner=ConnectionControl::instance();auto& manager=SpaceManager::instance();
    // A leave callback releases the current and another space, clears every
    // space and the entities/spaces, and tries to create one: each detached
    // connection is retired exactly once and nothing new is admitted.
    const SpaceID a=create(),b=create(),c=create();
    owner.pConnection_=new BWNullConnection(90,NULL_SPACE_ID);owner.mode_=ConnectionControl::OFFLINE;
    BWNullConnection *first=held(a),*second=held(b);
    std::vector<int> deleted;for(SpaceID space:{a,b,c}){BWNullConnection* connection=held(space);
        connection->onDelete=[&deleted,connection](){deleted.push_back(connection->id);};}
    bool created=true,releasedCurrent=true,releasedOther=true;
    owner.pConnection_->onClear=[&](){PyObject* refused=createSpace();created=refused!=NULL;Py_XDECREF(refused);
        assert(takeError(PyExc_RuntimeError,"BigWorld.createSpace(): client spaces are finalised"));};
    first->onClear=[&](){
        releasedCurrent=releaseSpace(a);PyErr_Clear();releasedOther=releaseSpace(b);PyErr_Clear();
        PyObject* refused=createSpace();created=created||refused!=NULL;Py_XDECREF(refused);PyErr_Clear();
        manager.space(a)->repeatable=true;clearAllSpaces(false);clearStockEntitiesAndSpaces(false);owner.fini();};
    events.clear();owner.fini();
    assert(!created&&!releasedCurrent&&!releasedOther&&!PyErr_Occurred());
    assert(owner.clientSpaceConnections_.empty()&&owner.clientSpaces_.empty()&&!owner.pConnection_);
    std::sort(deleted.begin(),deleted.end());
    assert(deleted.size()==3&&std::adjacent_find(deleted.begin(),deleted.end())==deleted.end());
    for(SpaceID space:{a,b,c})assert(!manager.space(space)&&std::count(events.begin(),events.end(),"destroy:"+id(space))==1);
    (void)second;
    // Terminal: repeated fini is harmless and creation stays closed until the
    // next SpaceManager::init, which reopens it.
    owner.fini();
    PyObject* refused=createSpace();assert(!refused&&takeError(PyExc_RuntimeError,"BigWorld.createSpace(): client spaces are finalised"));
    errors=0;assert(owner.createLocalSpace()==NULL_SPACE_ID&&errors==1&&owner.clientSpaces_.empty());
    manager.init();const SpaceID reopened=create();assert(isClientSpace(reopened));
    // Fini geometry: 63fe70 clears each held space; an external pin survives
    // with its geometry cleared and is no longer held.
    ClientSpacePtr pinned(manager.space(reopened));owner.fini();
    assert(pinned->clears==1&&manager.space(reopened)==pinned.get()&&!isClientSpace(reopened));
    pinned=ClientSpacePtr();assert(!manager.space(reopened));
    // Partial startup: fini before any space or before any init.
    reset();owner.fini();assert(owner.clientSpaceConnections_.empty());reset();
    // Fini inside tick defers deletion to the end of that tick.
    const SpaceID ticking=create(),other=create();BWNullConnection* tickConnection=held(ticking);
    bool returned=false;
    tickConnection->onUpdate=[&](){owner.fini();assert(owner.condemnedConnections_.size()==2);returned=true;};
    tickConnection->onDelete=[&](){assert(returned);};
    owner.tick(.1f);assert(returned&&owner.condemnedConnections_.empty()&&!manager.space(ticking)&&!manager.space(other));
    reset();
    std::puts("Production ConnectionControl::fini: retire-first, once-only, terminal fence and init reopen PASS");
}
static PyObject* clear(PyObject*,PyObject* args){return BW::py_clearEntitiesAndSpaces(args);}
static PyObject* prepare(PyObject*,PyObject*){reset();addSpace(1);addSpace((1<<30)+1);Py_RETURN_NONE;}
static PyObject* state(PyObject*,PyObject*)
{
    unsigned uncleared=0;for(const auto& space:pins)if(!space->cleared())++uncleared;
    return PyInt_FromLong(uncleared);
}
static PyObject* resetOwners(PyObject*,PyObject*){reset();Py_RETURN_NONE;}
static PyObject* server(PyObject*,PyObject* args)
{int value=0;if(!PyArg_ParseTuple(args,"i",&value))return NULL;addSpace(value);Py_RETURN_NONE;}
static PyObject* offline(PyObject*,PyObject* args)
{
    int value=0;if(!PyArg_ParseTuple(args,"i",&value))return NULL;
    auto& owner=BW::ConnectionControl::instance();assert(!owner.pConnection_);
    addSpace(value);owner.pConnection_=new BW::BWNullConnection(77,value);owner.mode_=BW::ConnectionControl::OFFLINE;
    owner.pConnection_->entities=1;Py_RETURN_NONE;
}
static PyObject* space(PyObject*,PyObject* args)
{
    int value=0;if(!PyArg_ParseTuple(args,"i",&value))return NULL;
    BW::ClientSpace* found=BW::SpaceManager::instance().space(value);
    if(!found)Py_RETURN_NONE;
    found->repeatable=true;
    auto& owner=BW::ConnectionControl::instance();
    return Py_BuildValue("(iii)",int(found->clears),int(owner.clientSpaces_.count(value)),int(held(value)!=NULL));
}
static PyObject* connection(PyObject*,PyObject* args)
{
    int value=0;if(!PyArg_ParseTuple(args,"i",&value))return NULL;
    BW::BWNullConnection* found=held(value);if(!found)Py_RETURN_NONE;
    return Py_BuildValue("(ii)",int(found->entities),int(found->clears));
}
static PyObject* entity(PyObject*,PyObject* args)
{
    int value=0;if(!PyArg_ParseTuple(args,"i",&value))return NULL;
    BW::BWNullConnection* found=held(value);
    if(!found){PyErr_Format(PyExc_ValueError,"No server connection or client space with ID %d",value);return NULL;}
    ++found->entities;Py_RETURN_NONE;
}
static PyObject* mainConnection(PyObject*,PyObject*)
{
    auto& owner=BW::ConnectionControl::instance();if(!owner.pConnection_)Py_RETURN_NONE;
    return Py_BuildValue("(ii)",int(owner.pConnection_->entities),int(owner.pConnection_->clears));
}
// Hand-written argument parsing around the extracted createEntity/player
// bodies; their PY_AUTO converters are outside this family.
static PyObject* createEntityBinding(PyObject*,PyObject* args)
{
    const char* name=NULL;int space=0,vehicle=0;PyObject *position=NULL,*direction=NULL,*properties=NULL;
    if(!PyArg_ParseTuple(args,"siiOOO!",&name,&space,&vehicle,&position,&direction,&PyDict_Type,&properties))return NULL;
    BW::Position3D at;BW::Vector3 facing;BW::ScriptDict dictionary{properties};
    return BW::createEntity(name,space,vehicle,at,facing,dictionary);
}
static PyObject* playerBinding(PyObject*,PyObject* args)
{
    PyObject* value=NULL;if(!PyArg_ParseTuple(args,"|O:player",&value))return NULL;
    if(value&&value!=Py_None&&!PyObject_TypeCheck(value,&BW::PyEntity::s_type_))
    {PyErr_SetString(PyExc_TypeError,"() argument 1 optionally must be set to a PyEntity or None");return NULL;}
    return BW::player(BW::PyEntityPtr(value&&value!=Py_None?static_cast<BW::PyEntity*>(value):NULL));
}
static PyObject* registerEntityType(PyObject*,PyObject* args)
{
    const char* name=NULL;PyObject *script=NULL,*player=Py_None;
    if(!PyArg_ParseTuple(args,"sO!|O",&name,&PyType_Type,&script,&player))return NULL;
    auto* type=new BW::EntityType;type->name_=name;type->class_=(PyTypeObject*)script;Py_INCREF(script);
    if(player!=Py_None){type->playerClass_=(PyTypeObject*)player;Py_INCREF(player);}
    type->index_=int(BW::EntityType::all().size());BW::EntityType::all().push_back(type);Py_RETURN_NONE;
}
static PyObject* finiOwners(PyObject*,PyObject*){BW::ConnectionControl::instance().fini();Py_RETURN_NONE;}
static PyObject* initSpaces(PyObject*,PyObject*){BW::SpaceManager::instance().init();Py_RETURN_NONE;}
static PyObject* connectionPlayer(PyObject*,PyObject* args)
{
    int value=0;if(!PyArg_ParseTuple(args,"i",&value))return NULL;
    BW::BWNullConnection* found=held(value);if(!found)Py_RETURN_NONE;
    return Py_BuildValue("(ii)",found->playerID,int(found->live.size()));
}
static PyObject* quiting(PyObject*,PyObject* args)
{int value=0;if(!PyArg_ParseTuple(args,"i",&value))return NULL;BW::App::instance().quiting=value!=0;Py_RETURN_NONE;}
int main(int argc,char** argv)
{
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif
    if(argc==2&&std::string(argv[1])=="--legacy")
    {
        auto& owner=BW::ConnectionControl::instance();auto* old=new BW::BWNullConnection(100);
        auto* replacement=new BW::BWNullConnection(101);bool replacementDeleted=false;
        owner.pConnection_=old;owner.mode_=BW::ConnectionControl::OFFLINE;
        replacement->onDelete=[&](){replacementDeleted=true;};
        old->onClear=[&](){owner.pConnection_=replacement;owner.mode_=BW::ConnectionControl::OFFLINE;};
        owner.legacyClearSpaces();
        assert(replacementDeleted&&!owner.pConnection_);old->onClear={};delete old;reset();
        std::puts("Pristine original clearSpaces destroys a callback-created replacement: reproduced");return 0;
    }
    assert(argc==2);Py_NoSiteFlag=1;Py_Initialize();
    BW::PyEntity::s_type_.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE;BW::PyEntity::s_type_.tp_new=PyType_GenericNew;BW::PyEntity::s_type_.tp_dealloc=BW::deallocEntity;
    assert(PyType_Ready(&BW::PyEntity::s_type_)==0);
    ownerTests();clientSpaceTests();finiTests();
    std::vector<PyMethodDef> methods=BW::moduleMethods();
    methods.insert(methods.end(),{
        {"clearEntitiesAndSpaces",clear,METH_VARARGS,NULL},{"_prepare",prepare,METH_NOARGS,NULL},
        {"_state",state,METH_NOARGS,NULL},{"_reset",resetOwners,METH_NOARGS,NULL},
        {"_server",server,METH_VARARGS,NULL},{"_offline",offline,METH_VARARGS,NULL},
        {"_space",space,METH_VARARGS,NULL},{"_connection",connection,METH_VARARGS,NULL},
        {"_entity",entity,METH_VARARGS,NULL},{"_main",mainConnection,METH_NOARGS,NULL},
        {"_quiting",quiting,METH_VARARGS,NULL},{"createEntity",createEntityBinding,METH_VARARGS,NULL},
        {"player",playerBinding,METH_VARARGS,NULL},{"_registerEntityType",registerEntityType,METH_VARARGS,NULL},
        {"_fini",finiOwners,METH_NOARGS,NULL},{"_init",initSpaces,METH_NOARGS,NULL},
        {"_connectionPlayer",connectionPlayer,METH_VARARGS,NULL},{NULL,NULL,0,NULL}});
    PyObject* module=Py_InitModule("BigWorld",methods.data());assert(module);
    BW::entityDictionary=PyDict_New();PyModule_AddObject(module,"entities",BW::entityDictionary);Py_INCREF(BW::entityDictionary);
    PyModule_AddObject(module,"Entity",(PyObject*)&BW::PyEntity::s_type_);Py_INCREF(&BW::PyEntity::s_type_);
    FILE* script=fopen(argv[1],"rb");assert(script);const int result=PyRun_SimpleFile(script,argv[1]);fclose(script);
    if(result){PyErr_Print();reset();Py_Finalize();return 1;}reset();Py_Finalize();
    std::puts("Production space lifecycle/tick owner fixture and real CPython boundary PASS");return 0;
}
