#if !defined(WOT_REPLAY_CONTROLLER_HOST_BUILD)
#include "pch.hpp"
#endif
#if defined(snprintf)
#undef snprintf
#endif
#if defined(vsnprintf)
#undef vsnprintf
#endif
#if defined(hypot)
#undef hypot
#endif
#include "stock_replay_controller.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace wot { namespace {
class PacketReader {
    const std::vector<std::uint8_t>& bytes_;std::size_t offset_,end_;
public:
    PacketReader(const StockReplayStream& stream,const ReplayPacketIndex& packet)
        :bytes_(stream.bytes()),offset_(packet.payloadOffset),end_(offset_+packet.size){}
    const std::uint8_t* take(std::size_t size){
        if(size>end_-offset_)throw std::runtime_error("Truncated replay action payload");
        const auto* result=bytes_.data()+offset_;offset_+=size;return result;
    }
    std::uint32_t word(){const auto* p=take(4);return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
    std::uint16_t shortWord(){const auto* p=take(2);return static_cast<std::uint16_t>(p[0]|(unsigned(p[1])<<8));}
    bool boolean(){const auto value=*take(1);if(value>1)throw std::runtime_error("Invalid replay boolean");return value!=0;}
    float real(){const auto bits=word();float result=0;std::memcpy(&result,&bits,4);if(!std::isfinite(result))throw std::runtime_error("Non-finite replay action value");return result;}
    std::string string(){const auto size=word();const auto* data=take(size);return std::string(reinterpret_cast<const char*>(data),size);}
    std::vector<std::uint8_t> data(){const auto size=word();const auto* bytes=take(size);return {bytes,bytes+size};}
    double timeAnchor(){const auto low=word(),high=word();const std::uint64_t bits=low|(std::uint64_t(high)<<32);double value=0;std::memcpy(&value,&bits,8);if(!std::isfinite(value))throw std::runtime_error("Invalid replay time anchor");return value;}
    void end(){if(offset_!=end_)throw std::runtime_error("Replay action payload has unexpected trailing fields");}
};
StockReplayAction action(const StockReplayStream& stream,const ReplayPacketIndex& packet){
    PacketReader input(stream,packet);StockReplayAction value;value.type=packet.type;value.id=input.word();
    if(packet.type==0){value.entityType=input.shortWord();if(!input.string().empty())throw std::runtime_error("Replay base-player type-name extension is not recovered");value.data=input.data();}
    else if(packet.type==1){value.space=input.word();value.entityType=input.shortWord();value.vehicle=input.word();for(auto& component:value.position)component=input.real();for(auto& component:value.direction)component=input.real();value.data=input.data();}
    else if(packet.type==2)value.control=input.boolean();
    else throw std::runtime_error("Replay action has no recovered native consumer");
    input.end();return value;
}
bool replayFailure(std::string& output,const std::string& message){output=message;return false;}
} // namespace

std::string stockReplayAutoStartFile(const std::vector<std::string>& arguments){
    std::string result;
    if(arguments.size()>=32)return result;
    for(auto value:arguments){
        if(value.find(".wotreplay")==std::string::npos)continue;
        if(!value.empty()&&value.front()=='"')value.erase(value.begin());
        if(!value.empty()&&value.back()=='"')value.pop_back();
        result=std::move(value);
    }
    return result;
}

StockReplayController::StockReplayController(std::unique_ptr<StockReplayConnection> connection)
    :connection_(std::move(connection)),clock_(new StockReplayClock){
    if(!connection_)throw std::invalid_argument("A real replay connection owner is required");
}
StockReplayController::~StockReplayController(){stop();}
bool StockReplayController::preflight(const StockReplayStream& stream,std::string& output) const{
    try{
        std::uint32_t base=0;std::uint16_t baseType=0;bool cell=false;
        for(const auto& packet:stream.packets()){
            if(packet.type==0||packet.type==1||packet.type==2){
                const auto value=action(stream,packet);
                if(!connection_->validEntityID(value.id))throw std::runtime_error("Replay entity identity is outside the native server domain");
                if(value.type==0){
                    if(base)throw std::runtime_error("Replay player replacement requires a recovered reset packet");
                    if(!connection_->validEntityType(value.entityType))throw std::runtime_error("Replay entity type has no loaded native definition");
                    base=value.id;baseType=value.entityType;
                }else if(value.type==1){
                    if(!base||value.id!=base||cell)throw std::runtime_error("Replay cell-player lifecycle has no matching base player");
                    if(value.entityType!=baseType)throw std::runtime_error("Replay player type promotion is not recovered");
                    if(value.vehicle&&!connection_->validEntityID(value.vehicle))throw std::runtime_error("Replay vehicle identity is outside the native server domain");
                    cell=true;
                }else if(value.id!=base)throw std::runtime_error("Replay control packet has no created native entity");
            }else if(packet.type==0x18||packet.type==0x2d||packet.type==0x30){
                PacketReader input(stream,packet);input.string();input.end();
            }else if(packet.type==0x15){
                PacketReader input(stream,packet);input.timeAnchor();input.end();
            }else{
                std::ostringstream message;message<<"Replay packet 0x"<<std::hex<<packet.type<<" at byte "<<std::dec<<packet.headerOffset<<" has no recovered native owner";
                throw std::runtime_error(message.str());
            }
        }
        return true;
    }catch(const std::exception& exception){return replayFailure(output,exception.what());}
}
bool StockReplayController::startPlayback(const std::string& path,std::string& output){
    if(playing_){error_="A replay is already active";return replayFailure(output,error_);}
    StockReplayFile next;
    if(!next.open(path,output)||!preflight(next.stream(),output)){error_=output;return false;}
    if(next.stream().packets().empty()){error_="Replay has no playable packets";return replayFailure(output,error_);}
    std::shared_ptr<StockReplayClock> clock(new StockReplayClock);
    if(!connection_->begin(clock,output)){error_=output;return false;}
    file_=std::move(next);clock_=std::move(clock);next_=0;++identity_;playing_=true;ready_=false;finished_=false;speed_=1;versionPending_=false;error_.clear();controlMode="arcade";
    for(std::size_t packet=0;packet<file_.stream().packets().size();++packet){
        if(file_.stream().packets()[packet].type==0x18&&!clientVersion.empty()){
            std::string recorded,unused;file_.stream().packetString(packet,recorded,unused);
            if(recorded!=clientVersion){versionPending_=true;if(onVersionDiffers)onVersionDiffers();break;}
        }
    }
    output.clear();return true;
}
void StockReplayController::stop(){
    ++identity_;playing_=false;ready_=false;finished_=false;versionPending_=false;next_=0;
    const auto retiredClock=clock_;
    file_.close();
    // Native entity destruction may synchronously start another replay. Do
    // not clear the new file or clock after returning from that callback.
    connection_->end();
    retiredClock->attached=false;retiredClock->time=0;
}
bool StockReplayController::confirmVersion(bool accepted){if(!versionPending_)return false;versionPending_=false;if(!accepted)stop();return true;}
bool StockReplayController::setSpeed(float speed,std::string& output){
    if(!std::isfinite(speed)||speed<0)return replayFailure(output,"Replay speed must be finite and non-negative");
    speed_=speed;output.clear();return true;
}
bool StockReplayController::tick(float delta,std::string& output){
    if(!std::isfinite(delta)||delta<0)return replayFailure(output,"Replay delta must be finite and non-negative");
    if(!playing_||finished_||versionPending_||speed_==0)return true;
    if(!clock_->attached){error_="Native replay connection ended";stop();return replayFailure(output,error_);}
    if(ready_)clock_->time+=static_cast<double>(delta)*speed_;
    const auto identity=identity_;
    try{
        while(next_<file_.stream().packets().size()){
            const auto packet=file_.stream().packets()[next_];
            if(packet.time>clock_->time)return true;
            if(packet.type==0x15){
                if(!ready_)return true;
                PacketReader input(file_.stream(),packet);clock_->serverOffset=input.timeAnchor();clock_->time=0;
            }else if(packet.type<=2){
                const auto value=action(file_.stream(),packet);
                if(!connection_->deliver(value,output)){error_=output;stop();return false;}
            }
            // Native entity creation may synchronously stop/restart this owner.
            if(identity!=identity_||!playing_)return true;
            ++next_;
        }
        finished_=true;if(onFinished)onFinished();output.clear();return true;
    }catch(const std::exception& exception){error_=exception.what();stop();return replayFailure(output,error_);}
}
float StockReplayController::timeMark(std::uint32_t id) const{
    if(id==0x80000000)return 0;
    if(id==0x80000001)return playing_?duration():-1;
    if(id==0x80000002)return ready_?static_cast<float>(clock_->time):0;
    if(id==0x80000003)return ready_?static_cast<float>(clock_->time+clock_->serverOffset):0;
    return -1;
}
std::string StockReplayController::arenaInfo() const{return file_.stream().metadata().empty()?std::string():file_.stream().metadata()[0];}
} // namespace wot

#if !defined(WOT_REPLAY_CONTROLLER_HOST_BUILD)
#include "connection_control.hpp"
#include "entity_type.hpp"
#include "app_config.hpp"
#include "connection_model/bw_null_connection.hpp"
#include "connection_model/bw_server_connection.hpp"
#include "connection/server_message_handler.hpp"
#include "cstdmf/memory_stream.hpp"
#include "cstdmf/debug.hpp"
#include <shellapi.h>
#include <Python.h>

BW_BEGIN_NAMESPACE
namespace StockReplayDetail {
class NativeConnection : public BWNullConnection {
    std::shared_ptr<wot::StockReplayClock> clock_;
    double initialTime_;
public:
    NativeConnection(BWEntityFactory& factory,BWSpaceDataStorage& storage,
        const EntityDefConstants& definitions,double time,SpaceID space,
        const std::shared_ptr<wot::StockReplayClock>& clock)
        :BWNullConnection(factory,storage,definitions,time,space),clock_(clock),initialTime_(time){clock_->attached=true;}
    ~NativeConnection() override {clock_->attached=false;}
    double clientTime() const override {return initialTime_+clock_->time;}
    double serverTime() const override {return clock_->serverOffset+clock_->time;}
    double lastMessageTime() const override {return clientTime();}
    ServerMessageHandler& handler(){return this->serverMessageHandler();}
};
class Endpoint : public wot::StockReplayConnection {
    NativeConnection* connection_=NULL;
    std::shared_ptr<wot::StockReplayClock> clock_;
public:
    bool validEntityID(std::uint32_t id) const override{return id>=static_cast<std::uint32_t>(FIRST_ENTITY_ID)&&id<static_cast<std::uint32_t>(FIRST_LOCAL_ENTITY_ID);}
    bool validEntityType(std::uint16_t type) const override{return EntityType::find(type)!=NULL;}
    bool begin(const std::shared_ptr<wot::StockReplayClock>& clock,std::string& error) override{
        auto* control=ConnectionControl::pInstance();
        if(!control||control->pConnection()){error="Replay playback requires a disconnected native client";return false;}
        auto* connection=control->startStockReplayConnection(clock);
        if(!connection){error="Cannot create the native replay connection";return false;}
        connection_=static_cast<NativeConnection*>(connection);clock_=clock;return true;
    }
    bool deliver(const wot::StockReplayAction& action,std::string& error) override{
        auto* control=ConnectionControl::pInstance();
        if(!connection_||!clock_->attached||!control||control->pConnection()!=connection_){error="Native replay connection identity changed";return false;}
        auto* connection=connection_;const EntityID id=static_cast<EntityID>(action.id);MemoryIStream data(action.data.data(),static_cast<int>(action.data.size()));
        if(action.type==0){
            connection->handler().onBasePlayerCreate(id,action.entityType,data);
            if(connection_!=connection||!clock_->attached)return true;
            if(!connection->pPlayer()||connection->pPlayer()->entityID()!=id){error="Native replay base-player creation failed";return false;}
        }else if(action.type==1){
            const Vector3 position(action.position[0],action.position[1],action.position[2]);
            connection->handler().onCellPlayerCreate(id,action.space,static_cast<EntityID>(action.vehicle),position,action.direction[0],action.direction[1],action.direction[2],data);
        }else if(action.type==2)connection->handler().onEntityControl(id,action.control);
        else{error="Replay action has no native entity consumer";return false;}
        if(data.error()||PyErr_Occurred()){error="Native replay entity payload was rejected";return false;}
        return true;
    }
    void end() override{
        auto* connection=connection_;connection_=NULL;
        if(!connection||!clock_||!clock_->attached)return;
        clock_->attached=false;auto* control=ConnectionControl::pInstance();
        if(control&&control->pConnection()==connection)control->stopStockReplayConnection(connection);
    }
};
const char* const callbackNames[]={"replayFinishedCallback","controlModeChangedCallback","ammoButtonPressedCallback",
    "playerVehicleIDChangedCallback","clientVersionDiffersCallback","battleChatMessageCallback","lockTargetCallback",
    "cruiseModeCallback","equipmentIdCallback","warpFinishedCallback","sniperModeCallback"};
constexpr std::size_t callbackCount=sizeof(callbackNames)/sizeof(callbackNames[0]);
struct State {
    wot::StockReplayController controller{std::unique_ptr<wot::StockReplayConnection>(new Endpoint)};
    std::string resultName,mapName,vehicleName,autoStart;
    bool overwrite=false,modalWindows=true;
    std::vector<std::pair<int,int>> streamFilters;
    std::set<std::string> methodFilters;
    State(){
        auto config=AppConfig::instance().pRoot();
        if(!config)throw std::runtime_error("Replay controller requires the native application settings");
        modalWindows=!config->readBool("enableEndStopApp",false)&&!config->readBool("skipReplayDialogs",false);
        int count=0;wchar_t** values=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!values)throw std::runtime_error("Cannot read replay command line");
        try{std::vector<std::string> arguments;
            for(int i=0;i<count;++i){
                PyObject* unicode=PyUnicode_FromWideChar(values[i],
                    static_cast<Py_ssize_t>(std::wcslen(values[i])));
                if(!unicode)throw std::runtime_error("Cannot decode replay command line");
                PyObject* encoded=PyUnicode_AsUTF8String(unicode);
                Py_DECREF(unicode);
                if(!encoded)throw std::runtime_error("Replay command line contains invalid UTF-16");
                try { arguments.emplace_back(PyString_AS_STRING(encoded),
                    static_cast<std::size_t>(PyString_GET_SIZE(encoded))); }
                catch(...) { Py_DECREF(encoded);throw; }
                Py_DECREF(encoded);
            }
            autoStart=wot::stockReplayAutoStartFile(arguments);
        }catch(...){LocalFree(values);throw;}
        LocalFree(values);
    }
};
struct PyController {PyObject_HEAD State* state;PyObject* callbacks[callbackCount];};
PyTypeObject type={PyObject_HEAD_INIT(NULL) 0};
std::set<PyController*> owners;
bool registered=false;
bool text(PyObject* object,std::string& result){
    PyObject* encoded=PyUnicode_Check(object)?PyUnicode_AsUTF8String(object):NULL;
    if(PyUnicode_Check(object)&&!encoded)return false;
    char* bytes=NULL;Py_ssize_t size=0;
    if(PyString_AsStringAndSize(encoded?encoded:object,&bytes,&size)!=0){Py_XDECREF(encoded);return false;}
    result.assign(bytes,static_cast<std::size_t>(size));Py_XDECREF(encoded);return true;
}
PyObject* string(const std::string& value){return PyString_FromStringAndSize(value.data(),static_cast<Py_ssize_t>(value.size()));}
void notify(PyController* owner,std::size_t index){
    PyObject* callback=owner->callbacks[index];if(!callback||callback==Py_None)return;Py_INCREF(callback);
    PyObject* result=PyObject_CallObject(callback,NULL);if(!result)PyErr_WriteUnraisable(callback);else Py_DECREF(result);Py_DECREF(callback);
}
int traverse(PyController* self,visitproc visit,void* arg){for(auto* callback:self->callbacks)Py_VISIT(callback);return 0;}
int clear(PyController* self){if(self->state)self->state->controller.stop();for(auto*& callback:self->callbacks)Py_CLEAR(callback);return 0;}
void dealloc(PyController* self){PyObject_GC_UnTrack(self);owners.erase(self);clear(self);delete self->state;Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));}
PyObject* construct(PyTypeObject* cls,PyObject* args,PyObject* kwargs){
    if(PyTuple_Size(args)||(kwargs&&PyDict_Size(kwargs))){PyErr_SetString(PyExc_TypeError,"WGReplayController takes no arguments");return NULL;}
    auto* self=reinterpret_cast<PyController*>(cls->tp_alloc(cls,0));if(!self)return NULL;
    self->state=NULL;for(auto*& callback:self->callbacks)callback=NULL;
    try{self->state=new State;owners.insert(self);self->state->controller.onFinished=[self](){notify(self,0);};self->state->controller.onVersionDiffers=[self](){notify(self,4);};}
    catch(const std::bad_alloc&){Py_DECREF(self);return PyErr_NoMemory();}
    catch(const std::exception& exception){Py_DECREF(self);PyErr_SetString(PyExc_RuntimeError,exception.what());return NULL;}return reinterpret_cast<PyObject*>(self);
}
bool noArgs(PyObject* args,PyObject* kwargs){static char* names[]={NULL};return PyArg_ParseTupleAndKeywords(args,kwargs,"",names)!=0;}
PyObject* unsupported(PyController*,PyObject*,PyObject*){PyErr_SetString(PyExc_NotImplementedError,"This replay operation requires packet owners that have not yet been recovered");return NULL;}
PyObject* play(PyController* self,PyObject* args,PyObject* kwargs){
    PyObject* path=NULL;char* names[]={const_cast<char*>("fileName"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"O",names,&path))return NULL;
    std::string value,error;if(!text(path,value))return NULL;
    if(!self->state->controller.startPlayback(value,error)){ERROR_MSG("WGReplayController::startPlayback: %s\n",error.c_str());Py_RETURN_FALSE;}
    Py_RETURN_TRUE;
}
PyObject* record(PyController*,PyObject* args,PyObject* kwargs){
    PyObject* path=NULL;char* names[]={const_cast<char*>("fileName"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"O",names,&path))return NULL;
    std::string value;if(!text(path,value))return NULL;
    auto* control=ConnectionControl::pInstance();if(!control||!control->pServerConnection())Py_RETURN_FALSE;
    PyErr_SetString(PyExc_NotImplementedError,"Replay network recording requires the remaining native message producers");return NULL;
}
PyObject* stop(PyController* self,PyObject* args,PyObject* kwargs){int remove=0;char* names[]={const_cast<char*>("delete"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"|i",names,&remove))return NULL;self->state->controller.stop();Py_RETURN_NONE;}
PyObject* playing(PyController* self,PyObject* args,PyObject* kwargs){if(!noArgs(args,kwargs))return NULL;return PyBool_FromLong(self->state->controller.playing());}
PyObject* ready(PyController* self,PyObject* args,PyObject* kwargs){if(!noArgs(args,kwargs))return NULL;self->state->controller.onClientReady();Py_RETURN_NONE;}
PyObject* commonUnloaded(PyController* self,PyObject* args,PyObject* kwargs){if(!noArgs(args,kwargs))return NULL;std::string error;self->state->controller.setSpeed(1,error);Py_RETURN_NONE;}
PyObject* compressed(PyController* self,PyObject* args,PyObject* kwargs){if(!noArgs(args,kwargs))return NULL;return PyBool_FromLong(self->state->controller.playing()&&self->state->controller.compressed());}
PyObject* timeMark(PyController* self,PyObject* args,PyObject* kwargs){unsigned int id=0;char* names[]={const_cast<char*>("id"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"I",names,&id))return NULL;return PyFloat_FromDouble(self->state->controller.timeMark(id));}
PyObject* confirm(PyController* self,PyObject* args,PyObject* kwargs){int accepted=0;char* names[]={const_cast<char*>("accepted"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"i",names,&accepted))return NULL;self->state->controller.confirmVersion(accepted!=0);Py_RETURN_NONE;}
PyObject* arenaInfo(PyController* self,PyObject* args,PyObject* kwargs){if(!noArgs(args,kwargs))return NULL;return string(self->state->controller.arenaInfo());}
// The exact setters validate their strings but only mutate an attached
// recording file (0x6ffa80/0x6ffb70). This playback-only subset has none.
PyObject* metadata(PyController*,PyObject* args,PyObject* kwargs){PyObject* object=NULL;char* names[]={const_cast<char*>("value"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"O",names,&object))return NULL;std::string value;if(!text(object,value))return NULL;Py_RETURN_NONE;}
PyObject* resultName(PyController* self,PyObject* args,PyObject* kwargs){PyObject* name=NULL;int overwrite=0;char* names[]={const_cast<char*>("fileName"),const_cast<char*>("overwriteExisting"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"Oi",names,&name,&overwrite))return NULL;if(!text(name,self->state->resultName))return NULL;self->state->overwrite=overwrite!=0;Py_RETURN_NONE;}
PyObject* streamFilter(PyController* self,PyObject* args,PyObject* kwargs){int low=0,high=0;char* names[]={const_cast<char*>("low"),const_cast<char*>("high"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"ii",names,&low,&high))return NULL;if(low<0||high>0xffff){PyErr_SetString(PyExc_ValueError,"Replay stream filter is outside the uint16 domain");return NULL;}self->state->streamFilters.emplace_back(low,high);Py_RETURN_NONE;}
PyObject* methodFilter(PyController* self,PyObject* args,PyObject* kwargs){PyObject* name=NULL;char* names[]={const_cast<char*>("method"),NULL};if(!PyArg_ParseTupleAndKeywords(args,kwargs,"O",names,&name))return NULL;std::string value;if(!text(name,value))return NULL;self->state->methodFilters.insert(value);Py_RETURN_NONE;}
PyObject* autoStart(PyController* self,PyObject* args,PyObject* kwargs){if(!noArgs(args,kwargs))return NULL;return string(self->state->autoStart);}
PyObject* getCallback(PyController* self,void* index){PyObject* value=self->callbacks[reinterpret_cast<std::size_t>(index)];if(!value)value=Py_None;Py_INCREF(value);return value;}
int setCallback(PyController* self,PyObject* value,void* index){if(!value||(value!=Py_None&&!PyCallable_Check(value))){PyErr_SetString(PyExc_TypeError,"Replay callback must be callable or None");return -1;}auto*& slot=self->callbacks[reinterpret_cast<std::size_t>(index)];Py_INCREF(value);PyObject* previous=slot;slot=value;Py_XDECREF(previous);return 0;}
PyObject* getFlag(PyController* self,void* index){const auto& owner=self->state->controller;const auto field=reinterpret_cast<std::size_t>(index);return PyBool_FromLong(field==0?false:field==1?owner.ready():field==2?owner.playing():false);}
PyObject* getSpeed(PyController* self,void*){return PyFloat_FromDouble(self->state->controller.speed());}
int setSpeed(PyController* self,PyObject* value,void*){if(!value){PyErr_SetString(PyExc_TypeError,"Cannot delete replay speed");return -1;}const double number=PyFloat_AsDouble(value);if(PyErr_Occurred())return -1;std::string error;if(!self->state->controller.setSpeed(static_cast<float>(number),error)){PyErr_SetString(PyExc_ValueError,error.c_str());return -1;}return 0;}
PyObject* getVersion(PyController* self,void*){return string(self->state->controller.clientVersion);}
int setVersion(PyController* self,PyObject* value,void*){if(!value){PyErr_SetString(PyExc_TypeError,"Cannot delete replay client version");return -1;}return text(value,self->state->controller.clientVersion)?0:-1;}
PyObject* getName(PyController* self,void* index){return string(reinterpret_cast<std::size_t>(index)?self->state->vehicleName:self->state->mapName);}
int setName(PyController* self,PyObject* value,void* index){if(!value){PyErr_SetString(PyExc_TypeError,"Cannot delete replay name");return -1;}return text(value,reinterpret_cast<std::size_t>(index)?self->state->vehicleName:self->state->mapName)?0:-1;}
PyObject* getDuration(PyController* self,void*){if(self->state->controller.playing()){PyErr_SetString(PyExc_NotImplementedError,"Replay arena-period markers have not yet been recovered");return NULL;}return PyFloat_FromDouble(0);}
PyObject* getError(PyController* self,void*){return string(self->state->controller.error());}
PyObject* getVehicle(PyController* self,void*){return PyInt_FromLong(self->state->controller.playerVehicleID);}
int setVehicle(PyController* self,PyObject* value,void*){if(!value){PyErr_SetString(PyExc_TypeError,"Cannot delete replay player identity");return -1;}const long id=PyInt_AsLong(value);if(PyErr_Occurred())return -1;if(id<INT32_MIN||id>INT32_MAX){PyErr_SetString(PyExc_OverflowError,"Replay player identity exceeds int32");return -1;}auto& owner=self->state->controller;if(owner.playerVehicleID!=id){owner.playerVehicleID=static_cast<int>(id);if(owner.playing())notify(self,3);}return 0;}
PyObject* getMode(PyController* self,void*){return string(self->state->controller.controlMode);}
int setMode(PyController* self,PyObject* value,void*){if(!value){PyErr_SetString(PyExc_TypeError,"Cannot delete replay control mode");return -1;}if(!text(value,self->state->controller.controlMode))return -1;if(self->state->controller.playing())notify(self,1);return 0;}
PyObject* getCamera(PyController* self,void*){return PyInt_FromLong(self->state->controller.controllingCamera?1:0);}
int setCamera(PyController* self,PyObject* value,void*){if(!value){PyErr_SetString(PyExc_TypeError,"Cannot delete replay camera control");return -1;}const int enabled=PyObject_IsTrue(value);if(enabled<0)return -1;if(self->state->controller.playing()){PyErr_SetString(PyExc_NotImplementedError,"Replay camera packet owner has not yet been recovered");return -1;}self->state->controller.controllingCamera=enabled!=0;return 0;}
PyObject* getModal(PyController* self,void*){return PyBool_FromLong(self->state->modalWindows);}
#define REPLAY_METHOD(name,method) {name,reinterpret_cast<PyCFunction>(method),METH_VARARGS|METH_KEYWORDS,NULL}
PyMethodDef methods[]={REPLAY_METHOD("startRecording",record),REPLAY_METHOD("startPlayback",play),REPLAY_METHOD("beginTimeWarp",unsupported),REPLAY_METHOD("getTimeMark",timeMark),REPLAY_METHOD("stop",stop),REPLAY_METHOD("onClientReady",ready),REPLAY_METHOD("onCommonSfwUnloaded",commonUnloaded),REPLAY_METHOD("onAmmoButtonPressed",unsupported),REPLAY_METHOD("registerWotReplayFileExtension",unsupported),REPLAY_METHOD("getAutoStartFileName",autoStart),REPLAY_METHOD("setResultingFileName",resultName),REPLAY_METHOD("getGunReloadAmountLeft",unsupported),REPLAY_METHOD("setGunReloadTime",unsupported),REPLAY_METHOD("getArcadeGunMarkerSize",unsupported),REPLAY_METHOD("setArcadeGunMarkerSize",unsupported),REPLAY_METHOD("getSPGGunMarkerParams",unsupported),REPLAY_METHOD("setSPGGunMarkerParams",unsupported),REPLAY_METHOD("getAimClipPosition",unsupported),REPLAY_METHOD("setAimClipPosition",unsupported),REPLAY_METHOD("getConsumableSlotCooldownAmount",unsupported),REPLAY_METHOD("setActiveConsumableSlot",unsupported),REPLAY_METHOD("setArenaInfoStr",metadata),REPLAY_METHOD("getArenaInfoStr",arenaInfo),REPLAY_METHOD("setArenaStatisticsStr",metadata),REPLAY_METHOD("onBattleChatMessage",unsupported),REPLAY_METHOD("onLockTarget",unsupported),REPLAY_METHOD("onSniperMode",unsupported),REPLAY_METHOD("onSetCruiseMode",unsupported),REPLAY_METHOD("isPlaying",playing),REPLAY_METHOD("isEffectNeedToPlay",unsupported),REPLAY_METHOD("onServerAim",unsupported),REPLAY_METHOD("onSetEquipmentID",unsupported),REPLAY_METHOD("resetArenaPeriod",unsupported),REPLAY_METHOD("confirmDlgAccepted",confirm),REPLAY_METHOD("setDataCallback",unsupported),REPLAY_METHOD("serializeCallbackData",unsupported),REPLAY_METHOD("getCallbackHandler",unsupported),REPLAY_METHOD("isFileCompressed",compressed),REPLAY_METHOD("setupStreamExcludeFilter",streamFilter),REPLAY_METHOD("setupAvatarMethodExcludeFilter",methodFilter),{NULL,NULL,0,NULL}};
#undef REPLAY_METHOD
PyGetSetDef properties[callbackCount+16];
bool prepare(){
    if(registered)return true;
    type.tp_name=const_cast<char*>("WGReplayController");type.tp_basicsize=sizeof(PyController);type.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC;
    type.tp_new=construct;type.tp_dealloc=reinterpret_cast<destructor>(dealloc);type.tp_traverse=reinterpret_cast<traverseproc>(traverse);type.tp_clear=reinterpret_cast<inquiry>(clear);type.tp_methods=methods;
    for(std::size_t i=0;i<callbackCount;++i)properties[i]={const_cast<char*>(callbackNames[i]),reinterpret_cast<getter>(getCallback),reinterpret_cast<setter>(setCallback),NULL,reinterpret_cast<void*>(i)};
    std::size_t i=callbackCount;const char* flags[]={"isRecording","isClientReady","isOfflinePlaybackMode","isTimeWarpInProgress"};
    for(std::size_t f=0;f<4;++f)properties[i++]={const_cast<char*>(flags[f]),reinterpret_cast<getter>(getFlag),NULL,NULL,reinterpret_cast<void*>(f)};
    properties[i++]={const_cast<char*>("playbackSpeed"),reinterpret_cast<getter>(getSpeed),reinterpret_cast<setter>(setSpeed),NULL,NULL};
    properties[i++]={const_cast<char*>("clientVersion"),reinterpret_cast<getter>(getVersion),reinterpret_cast<setter>(setVersion),NULL,NULL};
    properties[i++]={const_cast<char*>("recMapName"),reinterpret_cast<getter>(getName),reinterpret_cast<setter>(setName),NULL,NULL};
    properties[i++]={const_cast<char*>("recPlayerVehicleName"),reinterpret_cast<getter>(getName),reinterpret_cast<setter>(setName),NULL,reinterpret_cast<void*>(1)};
    properties[i++]={const_cast<char*>("arenaLength"),reinterpret_cast<getter>(getDuration),NULL,NULL,NULL};
    properties[i++]={const_cast<char*>("lastError"),reinterpret_cast<getter>(getError),NULL,NULL,NULL};
    properties[i++]={const_cast<char*>("playerVehicleID"),reinterpret_cast<getter>(getVehicle),reinterpret_cast<setter>(setVehicle),NULL,NULL};
    properties[i++]={const_cast<char*>("controlMode"),reinterpret_cast<getter>(getMode),reinterpret_cast<setter>(setMode),NULL,NULL};
    properties[i++]={const_cast<char*>("isControllingCamera"),reinterpret_cast<getter>(getCamera),reinterpret_cast<setter>(setCamera),NULL,NULL};
    properties[i++]={const_cast<char*>("scriptModalWindowsEnabled"),reinterpret_cast<getter>(getModal),NULL,NULL,NULL};properties[i]={NULL,NULL,NULL,NULL,NULL};type.tp_getset=properties;
    if(PyType_Ready(&type)<0)return false;registered=true;return true;
}
} // namespace StockReplayDetail
BWNullConnection* createStockReplayNativeConnection(BWEntityFactory& factory,BWSpaceDataStorage& storage,
    const EntityDefConstants& definitions,double initialTime,unsigned int space,const std::shared_ptr<wot::StockReplayClock>& clock){
    return new StockReplayDetail::NativeConnection(factory,storage,definitions,initialTime,space,clock);
}
bool initialiseStockReplayController(){using namespace StockReplayDetail;if(!prepare())return false;PyObject* module=PyImport_AddModule("BigWorld");return module&&PyObject_SetAttrString(module,"WGReplayController",reinterpret_cast<PyObject*>(&type))==0;}
void tickStockReplayController(float delta){using namespace StockReplayDetail;std::vector<PyController*> snapshot(owners.begin(),owners.end());for(auto* owner:snapshot)Py_INCREF(owner);for(auto* owner:snapshot){std::string error;if(!owner->state->controller.tick(delta,error))ERROR_MSG("WGReplayController: %s\n",error.c_str());Py_DECREF(owner);}}
void finiStockReplayController(){using namespace StockReplayDetail;std::vector<PyController*> snapshot(owners.begin(),owners.end());for(auto* owner:snapshot)Py_INCREF(owner);for(auto* owner:snapshot){clear(owner);Py_DECREF(owner);}}
BW_END_NAMESPACE
#endif
