// Production clear methods and original tick body, with bounded callback owners.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <Python.h>
#include <algorithm>
#include <cassert>
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

namespace BW
{
using SpaceID=int;
using uint64=std::uint64_t;
template<class T>using vector=std::vector<T>;
static unsigned errors=0;
static void testError(const char*,...){++errors;}
static std::vector<std::string> events;
struct ClientSpace
{
    int identifier; unsigned references=0; bool retired=false; std::function<void()> onClear;
    explicit ClientSpace(int id):identifier(id){}
    ~ClientSpace();
    int id()const{return identifier;}
    bool cleared()const{return retired;}
    void clear(){assert(!retired);retired=true;events.push_back("space:"+std::to_string(id()));if(onClear)onClear();}
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
    int id;unsigned clears=0,updates=0,sends=0;std::function<void()> onClear,onUpdate,onDelete;
    explicit BWConnection(int value):id(value){}
    virtual ~BWConnection(){events.push_back("delete:"+std::to_string(id));if(onDelete)onDelete();}
    double clientTime()const{return 123.;}
    virtual void clearAllEntities(){++clears;events.push_back("entities:"+std::to_string(id));if(onClear)onClear();}
    void update(float){++updates;if(onUpdate)onUpdate();}
    void updateServer(){++sends;}
};
struct BWNullConnection:BWConnection
{
    explicit BWNullConnection(int id):BWConnection(id){}
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
    struct Helper{void update(BWConnection* connection,float dt){if(connection)connection->update(dt);}}connectionHelper_;
    struct Dispatcher{int processOnce(bool){return 0;}}dispatcher_;
    static ConnectionControl& instance(){static ConnectionControl owner;return owner;}
    BWNullConnection* pNullConnection(){return mode_==OFFLINE?static_cast<BWNullConnection*>(pConnection_):NULL;}
    BWServerConnection* pServerConnection(){return mode_==ONLINE?static_cast<BWServerConnection*>(pConnection_):NULL;}
    BWReplayConnection* pReplayConnection(){return mode_==REPLAY?static_cast<BWReplayConnection*>(pConnection_):NULL;}
    void clearStockMainSpace();void clearSpaces();void tick(float);
    void legacyClearSpaces();void destroyMainConnection();
};
struct SpaceManager
{
    static const int LOCAL_SPACE_START=1<<30;
    using SpaceMap=std::map<int,ClientSpace*>;
    SpaceMap spaces_,localSpaces_;
    static SpaceManager& instance(){static SpaceManager owner;return owner;}
    ClientSpace* space(int id){const auto& map=id>=LOCAL_SPACE_START?localSpaces_:spaces_;auto p=map.find(id);return p==map.end()?NULL:p->second;}
};
ClientSpace::~ClientSpace()
{
    auto& manager=SpaceManager::instance();
    auto& map=id()>=SpaceManager::LOCAL_SPACE_START?manager.localSpaces_:manager.spaces_;
    auto found=map.find(id());if(found!=map.end()&&found->second==this)map.erase(found);
}
namespace DeprecatedSpaceHelpers
{
static ClientSpacePtr current;
static ClientSpacePtr cameraSpace(){return current;}
}
namespace Script{int setData(PyObject*,bool&,const char*);}
@CONVERTER@
}
@SOUND_TEST_SUPPORT@
#include "@PRODUCTION_FILE@"
namespace BW
{
@CLEAR_SPACES@
@TICK@
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
static PyObject* clear(PyObject*,PyObject* args){return BW::py_clearEntitiesAndSpaces(args);}
static PyObject* prepare(PyObject*,PyObject*){reset();addSpace(1);addSpace((1<<30)+1);Py_RETURN_NONE;}
static PyObject* state(PyObject*,PyObject*)
{
    unsigned uncleared=0;for(const auto& space:pins)if(!space->cleared())++uncleared;
    return PyInt_FromLong(uncleared);
}
static PyMethodDef methods[]={
    {"clearEntitiesAndSpaces",clear,METH_VARARGS,NULL},{"_prepare",prepare,METH_NOARGS,NULL},
    {"_state",state,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
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
    ownerTests();assert(argc==2);Py_NoSiteFlag=1;Py_Initialize();assert(Py_InitModule("BigWorld",methods));
    FILE* script=fopen(argv[1],"rb");assert(script);const int result=PyRun_SimpleFile(script,argv[1]);fclose(script);
    if(result){PyErr_Print();reset();Py_Finalize();return 1;}reset();Py_Finalize();
    std::puts("Production space lifecycle/tick owner fixture and real CPython optional-bool boundary PASS");return 0;
}
