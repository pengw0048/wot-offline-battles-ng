#if !defined(WOT_CLIENT_INPUT_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_input.hpp"
#include "stock_native_client_landing.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <optional>
#include <utility>
namespace stock_native_client_input {
namespace p=stock_native_bot_python;
namespace {
struct Limits {
    std::int64_t message=0,queue_messages=0,queue_bytes=0,nodes=0,depth=0,projectile=0,motion_time=0;
    int ram=0,destructibles=0,clip=0;
    double speed=0.,pitch=0.,reload=0.,dispersion=0.;
    std::array<double,3> bounds{};
    std::string timeline,owner;
} limits;
void require(bool ok,const char*message){if(!ok)throw p::Error(message);}
void attr(PyObject*object,const char*name,const p::Ref&value){if(PyObject_SetAttrString(object,name,value.get())<0)throw p::Error("Native input attribute write failed");}
PyObject* get(PyObject*d,const char*k){auto*v=PyDict_Check(d)?PyDict_GetItemString(d,k):nullptr;return v?v:Py_None;}
bool equal(PyObject*a,PyObject*b){const int out=PyObject_RichCompareBool(a,b,Py_EQ);require(out>=0,"Native input identity comparison failed");return out!=0;}
bool integer(PyObject*v){return PyLong_Check(v)
#if PY_MAJOR_VERSION<3
    ||PyInt_Check(v)
#endif
    ;}
bool string(PyObject*v){return PyUnicode_Check(v)
#if PY_MAJOR_VERSION<3
    ||PyString_Check(v)
#endif
    ;}
bool numeric(PyObject*v){return !PyBool_Check(v)&&(integer(v)||PyFloat_Check(v));}
double finite(PyObject*v,double fallback=0.){
    auto*out=PyNumber_Float(v);if(!out){PyErr_Clear();return fallback;}
    p::Ref held(out);const auto n=PyFloat_AS_DOUBLE(out);return std::isfinite(n)?n:fallback;
}
std::optional<std::int64_t> exact(PyObject*v){
    if(v==Py_None||PyBool_Check(v))return {};
    auto*raw=PyNumber_Long(v);if(!raw){PyErr_Clear();return {};}
    p::Ref parsed(raw);auto n=PyLong_AsLongLong(raw);if(PyErr_Occurred()){PyErr_Clear();return {};}
    if(!integer(v)){auto*value=PyNumber_Float(v);if(!value){PyErr_Clear();return {};}p::Ref f(value);if(PyFloat_AS_DOUBLE(value)!=static_cast<double>(n))return {};}
    return n;
}
std::optional<std::int64_t> range(PyObject*v,std::int64_t min,std::int64_t max){if(!integer(v)||PyBool_Check(v))return {};auto n=exact(v);if(!n||*n<min||*n>max)return {};return n;}
double angle(PyObject*v){double out=finite(v);const double pi=3.14159265358979323846;if(out>=-pi&&out<pi)return out;out=std::fmod(out+pi,2.*pi);if(out<0.)out+=2.*pi;return out-pi;}
std::size_t textSize(PyObject*v){
    std::size_t total=2;const Py_ssize_t n=PyUnicode_Check(v)?PyUnicode_GET_SIZE(v):PyString_GET_SIZE(v);
    for(Py_ssize_t i=0;i<n;++i){const auto c=PyUnicode_Check(v)?static_cast<unsigned>(PyUnicode_AS_UNICODE(v)[i]):static_cast<unsigned>(static_cast<unsigned char>(PyString_AS_STRING(v)[i]));
        total+=(c=='"'||c=='\\'||c==8||c==9||c==10||c==12||c==13)?2:(c<32||c==127)?6:c<128?1:c<=65535?6:12;}
    return total;
}
struct Frozen {p::Ref value;std::size_t bytes;};
Frozen freezeValue(PyObject*v,std::size_t&nodes,std::size_t depth){
    require(depth<=std::size_t(limits.depth),"Outbound payload nesting exceeded limit");
    require(++nodes<=std::size_t(limits.nodes),"Outbound payload node count exceeded limit");
    if(v==Py_None)return {p::retain(v),4};if(PyBool_Check(v))return {p::retain(v),v==Py_True?4u:5u};
    if(integer(v)){p::Ref repr(PyObject_Str(v));return {p::retain(v),p::string(repr.get()).size()};}
    if(PyFloat_Check(v)){require(std::isfinite(PyFloat_AS_DOUBLE(v)),"Outbound float must be finite");p::Ref repr(PyObject_Repr(v));return {p::retain(v),p::string(repr.get()).size()+8};}
    if(string(v))return {p::retain(v),textSize(v)};
    if(PyList_Check(v)||PyTuple_Check(v)){const auto count=PySequence_Size(v);p::Ref out(PyTuple_New(count));std::size_t bytes=2;for(Py_ssize_t i=0;i<count;++i){auto held=p::item(v,i);auto item=freezeValue(held.get(),nodes,depth+1);bytes+=(i?1:0)+item.bytes;require(bytes+1<=std::size_t(limits.message),"Outbound payload exceeded wire limit");PyTuple_SET_ITEM(out.get(),i,item.value.release());}return {std::move(out),bytes};}
    if(PyDict_Check(v)){auto out=p::dict();Py_ssize_t pos=0;PyObject*k,*item;std::size_t bytes=2;while(PyDict_Next(v,&pos,&k,&item)){require(string(k),"Outbound mapping key must be text");auto frozen=freezeValue(item,nodes,depth+1);bytes+=(PyDict_Size(out.get())?1:0)+textSize(k)+1+frozen.bytes;require(bytes+1<=std::size_t(limits.message),"Outbound payload exceeded wire limit");if(PyDict_SetItem(out.get(),k,frozen.value.get())<0)throw p::Error("Outbound allocation failed");}return {std::move(out),bytes};}
    throw p::Error("Outbound payload contains non-plain data");
}
class Lock {
    p::Ref object_;
public:
    explicit Lock(PyObject*client):object_(p::value(client,"_outbound_lock")){
        p::Ref empty(PyTuple_New(0));p::call(object_.get(),"acquire",empty.get());
    }
    ~Lock(){auto*value=PyObject_CallMethod(object_.get(),const_cast<char*>("release"),nullptr);if(!value)PyErr_WriteUnraisable(object_.get());else Py_DECREF(value);}
};
bool available(PyObject*client){return p::truth(client,"connected")&&!p::truth(client,"_stopping")&&p::truth(client,"running")&&p::truth(client,"_outbound_accepting");}
bool capability(PyObject*client,const std::string&value){auto key=p::text(value),local=p::value(client,"capabilities"),remote=p::value(client,"server_capabilities");int a=PySequence_Contains(local.get(),key.get()),b=PySequence_Contains(remote.get(),key.get());require(a>=0&&b>=0,"Native input capabilities unavailable");return a&&b;}
p::Ref checkpoint(PyObject*v){
    if(!PyDict_Check(v)||PyDict_Size(v)!=5)return p::retain(Py_None);
    const double invalid=std::numeric_limits<double>::quiet_NaN();
    double reload=PyBool_Check(get(v,"reload_time"))?invalid:finite(get(v,"reload_time"),invalid),duration=PyBool_Check(get(v,"reload_duration"))?invalid:finite(get(v,"reload_duration"),invalid),dispersion=PyBool_Check(get(v,"dispersion"))?invalid:finite(get(v,"dispersion"),invalid);
    auto clip=exact(get(v,"clip")),size=exact(get(v,"clip_size"));
    if(!std::isfinite(reload)||!std::isfinite(duration)||!std::isfinite(dispersion)||duration<=0.||duration>limits.reload||reload<0.||reload>duration||!clip||!size||*size<1||*size>limits.clip||*clip<0||*clip>*size||dispersion<0.||dispersion>limits.dispersion)return p::retain(Py_None);
    auto out=p::dict();p::set(out.get(),"reload_time",p::number(reload));p::set(out.get(),"reload_duration",p::number(duration));p::set(out.get(),"dispersion",p::number(dispersion));p::set(out.get(),"clip",p::integer(*clip));p::set(out.get(),"clip_size",p::integer(*size));return out;
}
void contacts(PyObject*message,const char*key,PyObject*value,int maximum){if(!PyList_Check(value))return;auto out=p::list();for(Py_ssize_t i=0;i<std::min<Py_ssize_t>(PyList_GET_SIZE(value),maximum);++i){auto*row=PyList_GET_ITEM(value,i);if(PyDict_Check(row))p::append(out.get(),p::Ref(PyDict_Copy(row)));}p::set(message,key,out);}
}
PyObject* configure(PyObject*,PyObject*module){try{
    limits.message=p::integral(module,"MAX_MESSAGE_BYTES");limits.queue_messages=p::integral(module,"MAX_OUTBOUND_MESSAGES");limits.queue_bytes=p::integral(module,"MAX_OUTBOUND_BYTES");limits.nodes=p::integral(module,"MAX_OUTBOUND_NODES");limits.depth=p::integral(module,"MAX_OUTBOUND_DEPTH");limits.projectile=p::integral(module,"MAX_PROJECTILE_ID");limits.motion_time=p::integral(module,"MAX_MOTION_TIME_US");limits.ram=int(p::integral(module,"MAX_PLAYER_RAM_CONTACTS"));limits.destructibles=int(p::integral(module,"MAX_PLAYER_DESTRUCTIBLE_CONTACTS"));limits.clip=int(p::integral(module,"MAX_PLAYER_CLIP_SIZE"));limits.speed=p::numeric(module,"MAX_PLAYER_INPUT_SPEED");limits.pitch=p::numeric(module,"MAX_PLAYER_GUN_PITCH");limits.reload=p::numeric(module,"MAX_PLAYER_RELOAD_SECONDS");limits.dispersion=p::numeric(module,"MAX_PLAYER_DISPERSION_ANGLE");auto bounds=p::value(module,"PLAYER_INPUT_WORLD_BOUNDS");limits.bounds=p::numbers<3>(bounds.get());auto timeline=p::value(module,"HUMAN_RAM_TIMELINE_CAPABILITY"),owner=p::value(module,"PLAYER_PROJECTILE_OWNER_CAPABILITY");limits.timeline=p::string(timeline.get());limits.owner=p::string(owner.get());Py_RETURN_NONE;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
bool enqueue(PyObject*client,PyObject*message){
    auto generation=[&](){Lock lock(client);auto sock=p::value(client,"sock");return (!available(client)||sock.get()==Py_None)?p::retain(Py_None):p::value(client,"_transport_generation");}();
    if(generation.get()==Py_None)return false;
    std::size_t nodes=0;auto frozen=freezeValue(message,nodes,0);++frozen.bytes;if(frozen.bytes>std::size_t(limits.message))return false;
    {Lock lock(client);auto current=p::value(client,"_transport_generation");if(!equal(generation.get(),current.get())||!available(client))return false;
        auto queue=p::value(client,"_outbound_queue");require(PyList_Check(queue.get()),"Outbound queue is not a list");const auto bytes=p::integral(client,"_outbound_bytes");
        if(PyList_GET_SIZE(queue.get())>=limits.queue_messages||bytes+std::int64_t(frozen.bytes)>limits.queue_bytes)return false;
        const auto seq=p::integral(client,"_outbound_seq")+1;auto seq_value=p::integer(seq),size=p::integer(frozen.bytes);p::Ref item(PyTuple_Pack(3,seq_value.get(),frozen.value.get(),size.get()));p::append(queue.get(),item);attr(client,"_outbound_seq",seq_value);attr(client,"_outbound_bytes",p::integer(bytes+std::int64_t(frozen.bytes)));
    }
    // The visible transport has no Python sender thread. Hand the FIFO to its
    // native sender now; there is no intermediate wake event or polling loop.
    try {
        p::Ref args(PyTuple_Pack(1,generation.get()));p::call(client,"_flush_native_outbound",args.get());
    } catch (const std::exception&) {
        // A failed handoff must not leave a launch queued after its caller rolls back.
        PyErr_Clear();
        auto*done=PyObject_CallMethod(client,const_cast<char*>("_abort_outbound"),const_cast<char*>("sO"),"native outbound handoff failed",generation.get());
        if(done)Py_DECREF(done);else PyErr_Clear();
        return false;
    }
    Lock lock(client);auto current=p::value(client,"_transport_generation");return equal(generation.get(),current.get())&&available(client);
}
PyObject*send(PyObject*,PyObject*args){try{PyObject*client,*message;if(!PyArg_ParseTuple(args,"OO",&client,&message))return nullptr;return p::boolean(enqueue(client,message)).release();}catch(const std::exception&){PyErr_Clear();Py_RETURN_FALSE;}}
PyObject*freeze(PyObject*,PyObject*v){try{std::size_t nodes=0;auto frozen=freezeValue(v,nodes,0);return Py_BuildValue("NK",frozen.value.release(),static_cast<unsigned long long>(frozen.bytes));}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject*sendInput(PyObject*,PyObject*args,PyObject*kwargs){
    PyObject *client,*forward,*turn,*aim=Py_None,*pitch=Py_None,*position=Py_None,*yaw=Py_None,*fire=Py_None,*speed=Py_None,*shell=Py_None,*next=Py_None,*pending=Py_None,*pose=Py_None,*ram=Py_None,*tanks=Py_None,*turrets=Py_None,*destructibles=Py_None,*siege=Py_None,*body_pitch=Py_None,*body_roll=Py_None,*gun=Py_None,*up=Py_None;
    static const char*names[]={"client","forward","turn","aim_yaw","gun_pitch","position","yaw","fire_seq","speed","shell_index","next_shell_index","shell_change_pending","pose_time_us","ram_contacts","tank_pushes","turret_pushes","destructible_contacts","siege_enabled","pitch","roll","gun_checkpoint","up_cosine",nullptr};
    if(!PyArg_ParseTupleAndKeywords(args,kwargs,"OOO|OOOOOOOOOOOOOOOOOOO",const_cast<char**>(names),&client,&forward,&turn,&aim,&pitch,&position,&yaw,&fire,&speed,&shell,&next,&pending,&pose,&ram,&tanks,&turrets,&destructibles,&siege,&body_pitch,&body_roll,&gun,&up))return nullptr;
    try{
        if(!p::truth(client,"ready"))Py_RETURN_FALSE;auto phase=p::value(client,"phase");if(p::string(phase.get())!="battle")Py_RETURN_FALSE;
        auto round=p::value(client,"round_id"),input_round=p::value(client,"_input_seq_round"),landing_round=p::value(client,"_landing_observation_round");
        if(!equal(round.get(),input_round.get())){attr(client,"_input_seq_round",round);attr(client,"_input_seq",p::integer(0));}
        if(!equal(round.get(),landing_round.get())){attr(client,"_landing_observation_round",round);attr(client,"_landing_observation_seq",p::integer(0));attr(client,"_landing_observation_pending",p::retain(Py_None));attr(client,"_landing_observation_queue",p::list());}
        const auto seq=p::integral(client,"_input_seq")+1;if(seq>limits.projectile)Py_RETURN_FALSE;
        p::Ref parsed_fire(PyNumber_Long(p::flag(fire)?fire:Py_False));auto fire_seq=range(parsed_fire.get(),0,limits.projectile);if(!fire_seq){auto n=exact(parsed_fire.get());if(n&&*n<0)fire_seq=0;else Py_RETURN_FALSE;}
        auto message=p::dict();p::set(message.get(),"type",p::text("input"));p::set(message.get(),"round_id",round);p::set(message.get(),"forward",p::number(std::clamp(finite(forward),-1.,1.)));p::set(message.get(),"turn",p::number(std::clamp(finite(turn),-1.,1.)));p::set(message.get(),"aim_yaw",p::number(angle(aim)));p::set(message.get(),"gun_pitch",p::number(std::clamp(finite(pitch),-limits.pitch,limits.pitch)));p::set(message.get(),"fire_seq",p::integer(*fire_seq));
        const bool timeline=capability(client,limits.timeline);if(timeline)p::set(message.get(),"input_seq",p::integer(seq));
        if(position!=Py_None&&PySequence_Size(position)>=3){const char*keys[]={"x","y","z"};for(int i=0;i<3;++i){auto v=p::item(position,i);const double n=finite(v.get());if(std::abs(n)>limits.bounds[i])Py_RETURN_FALSE;p::set(message.get(),keys[i],p::number(n));}p::set(message.get(),"yaw",p::number(angle(yaw)));if(body_pitch!=Py_None)p::set(message.get(),"pitch",p::number(angle(body_pitch)));if(body_roll!=Py_None)p::set(message.get(),"roll",p::number(angle(body_roll)));if(up!=Py_None){if(!numeric(up))Py_RETURN_FALSE;const double n=finite(up,2.);if(n< -1.||n>1.)Py_RETURN_FALSE;p::set(message.get(),"up_cosine",p::number(n));}if(timeline&&pose!=Py_None){auto n=exact(pose);if(!n||*n<0||*n>limits.motion_time)Py_RETURN_FALSE;p::set(message.get(),"pose_time_us",p::integer(*n));}}
        if(speed!=Py_None)p::set(message.get(),"speed",p::number(std::clamp(finite(speed),-limits.speed,limits.speed)));
        if(shell!=Py_None){auto n=range(shell,0,9);if(!n)Py_RETURN_FALSE;p::set(message.get(),"shell_index",p::integer(*n));}
        if((next!=Py_None)!=(pending!=Py_None))Py_RETURN_FALSE;if(next!=Py_None){auto n=range(next,0,9);if(!n||!PyBool_Check(pending))Py_RETURN_FALSE;p::set(message.get(),"next_shell_index",p::integer(*n));p::set(message.get(),"shell_change_pending",p::retain(pending));}
        auto parsed=checkpoint(gun);if(capability(client,limits.owner)&&parsed.get()==Py_None)Py_RETURN_FALSE;
        if(gun!=Py_None){if(parsed.get()==Py_None||!timeline||shell==Py_None||next==Py_None||pending==Py_None)Py_RETURN_FALSE;p::set(message.get(),"gun_checkpoint",parsed);}
        if(tanks!=Py_None)p::set(message.get(),"tank_pushes",p::retain(tanks));if(turrets!=Py_None)p::set(message.get(),"turret_pushes",p::retain(turrets));contacts(message.get(),"ram_contacts",ram,limits.ram);contacts(message.get(),"destructible_contacts",destructibles,limits.destructibles);
        if(siege!=Py_None){if(!PyBool_Check(siege)){PyErr_SetString(PyExc_ValueError,"siege_enabled must be BOOL");return nullptr;}p::set(message.get(),"siege_enabled",p::retain(siege));}
        bool accepted=false;try{accepted=enqueue(client,message.get());}catch(const std::exception&){PyErr_Clear();}
        if(!accepted)Py_RETURN_FALSE;attr(client,"_input_seq",p::integer(seq));
        auto landing=p::value(client,"_landing_observation_pending");if(PyDict_Check(landing.get())&&p::flag(get(landing.get(),"retry_on_input"))&&!p::flag(get(landing.get(),"sent"))){if(p::flag(get(landing.get(),"refresh_input_on_retry"))){p::set(landing.get(),"input_seq",p::integer(seq));if(PyDict_DelItemString(landing.get(),"wire")<0)PyErr_Clear();}stock_native_client_landing::sendPending(client,true);}
        Py_RETURN_TRUE;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}
}
PyMethodDef*methods(){static PyMethodDef out[]={
    {"configure_input",configure,METH_O,"Install exact input envelope limits."},
    {"send_input",reinterpret_cast<PyCFunction>(reinterpret_cast<void(*)(void)>(sendInput)),METH_VARARGS|METH_KEYWORDS,"Freeze and enqueue an ordered player input."},
    {"send",send,METH_VARARGS,"Freeze a reliable player event."},
    {"freeze",freeze,METH_O,"Freeze a plain JSON message."},
    {nullptr,nullptr,0,nullptr}};return out;}
}
