#if !defined(WOT_CLIENT_SURVIVAL_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#else
#include <Python.h>
#endif
#include "stock_native_client_survival.hpp"
#include "stock_native_client_survival_python.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_player_environment.hpp"
#if !defined(WOT_CLIENT_SURVIVAL_HOST_BUILD)
#include "stock_native_bot_world_adapter.hpp"
#endif
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_bot_python.hpp"
#include <memory>

namespace stock_native_client_survival {
namespace {
namespace p=stock_native_bot_python;
namespace shared=stock_native_client_state;
namespace drive=stock_native_client_drive;
namespace water=stock_native_player_environment;
const char*capsuleName="wot.native.client.survival";
p::Ref field(PyObject*o,const char*n){
    if(!o||o==Py_None)return p::retain(Py_None);
    if(PyDict_Check(o))return p::value(o,n);
    auto key=p::text(n);auto*v=p::directAttribute(o,key.get());
    if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}
    return p::Ref(v);
}
void put(PyObject*o,const char*n,const p::Ref&v){
    auto key=p::text(n);if(PyObject_GenericSetAttr(o,key.get(),v.get())<0)throw p::Error("Native survival field write failed");
}
double number(PyObject*o,const char*n,double fallback=0.){auto v=field(o,n);return v.get()==Py_None?fallback:p::scalar(v.get());}
std::optional<double> optional(PyObject*o,const char*n){auto v=field(o,n);return v.get()==Py_None?std::nullopt:std::optional<double>(p::scalar(v.get()));}
water::Point xyz(PyObject*o){water::Point out{};for(unsigned i=0;i<3;++i)out[i]=p::scalar(p::item(o,i).get());return out;}
Config configuration(PyObject*b){
    auto runtime=field(b,"_runtime"),constants=field(runtime.get(),"constants"),condition=field(constants.get(),"OVERTURN_CONDITION");
    Config c;c.ignore_delay=number(condition.get(),"IGNOR_DELAY",.1);
    c.warning_cosine=number(condition.get(),"WARNING_COSINE",std::cos(70.*3.141592653589793/180.));
    c.onboard_cosine=number(condition.get(),"ONBOARD_COSINE",std::cos(80.*3.141592653589793/180.));return c;
}
State initial(PyObject*b){
    State s;s.drown_check=number(b,"_drown_check");s.drown_time=number(b,"_drown_time");s.drown_level=int(number(b,"_drown_level"));s.drown_started=optional(b,"_drown_started");
    s.overturn_check=number(b,"_overturn_check");s.overturn_time=number(b,"_overturn_time");s.overturn_level=int(number(b,"_overturn_level"));s.overturn_started=optional(b,"_overturn_started");return s;
}
struct NativeOwner {
    std::shared_ptr<shared::State> state;
    std::shared_ptr<shared::Actor> actor;
    std::uint64_t epoch;
    p::Ref entity,bigworld;
    std::unique_ptr<p::Ref> descriptor;
    std::uint64_t descriptor_revision=0;
    water::Metadata metadata;
    bool metadata_valid=false,retired=false;
    Owner clock;
    NativeOwner(PyObject*b,std::shared_ptr<shared::State>s,std::shared_ptr<shared::Actor>a,PyObject*e):
        state(std::move(s)),actor(std::move(a)),epoch(state->epoch),entity(p::retain(e)),
        bigworld(field(field(b,"_runtime").get(),"bigworld")),clock(configuration(b),initial(b)){}
    bool current()const {auto it=state->actors.find(actor->key);return !retired&&!state->retired&&epoch==state->epoch&&it!=state->actors.end()&&it->second==actor;}
};
using Handle=std::shared_ptr<NativeOwner>;
void destroy(PyObject*c){auto*h=static_cast<Handle*>(PyCapsule_GetPointer(c,capsuleName));if(h){if(*h)(*h)->retired=true;delete h;}else PyErr_Clear();}
Handle existing(PyObject*b){auto raw=field(b,"_native_client_survival");if(raw.get()==Py_None)return {};auto*h=static_cast<Handle*>(PyCapsule_GetPointer(raw.get(),capsuleName));if(!h)throw p::Error("Invalid native survival owner");return *h;}
Handle acquire(PyObject*b){
    auto state=shared::fromBattle(b);const auto found=state->actors.find("player:"+std::to_string(state->local_player_id));
    if(state->retired||found==state->actors.end()||!found->second->local)return {};
    const auto&actor=found->second;p::Ref entity(stock_native_bot::readNativeEntity(b,actor->engine_id));if(entity.get()==Py_None)return {};
    auto previous=existing(b);if(previous&&previous->current()&&previous->entity.get()==entity.get())return previous;
    if(previous)previous->retired=true;
    auto owner=std::make_shared<NativeOwner>(b,std::move(state),actor,entity.get());std::unique_ptr<Handle> handle(new Handle(owner));
    p::Ref capsule(PyCapsule_New(handle.get(),capsuleName,destroy));handle.release();put(b,"_native_client_survival",capsule);return owner;
}
double clock(NativeOwner&o,double now){
    for(const char*name:{"serverTime","time"})try{auto fn=field(o.bigworld.get(),name);if(!PyCFunction_Check(fn.get()))continue;p::Ref args(PyTuple_New(0)),value(PyObject_CallObject(fn.get(),args.get()));return p::scalar(value.get());}catch(const std::exception&){PyErr_Clear();}
    return now;
}
bool eligible(NativeOwner&o,PyObject*b,double dt){
    if(dt<=0.||!o.current()||!o.actor->alive||o.actor->state.get("health").numeric(1.)<=0.||field(b,"_server").get()==Py_None)return false;
    p::Ref health(stock_native_bot::readNativeEntityField(b,o.entity.get(),"health"));return health.get()!=Py_None&&p::scalar(health.get())>0.;
}
void mirror(PyObject*b,const State&s){
    put(b,"_drown_check",p::number(s.drown_check));put(b,"_drown_time",p::number(s.drown_time));put(b,"_drown_level",p::integer(s.drown_level));put(b,"_drown_started",s.drown_started?p::number(*s.drown_started):p::retain(Py_None));
    put(b,"_overturn_check",p::number(s.overturn_check));put(b,"_overturn_time",p::number(s.overturn_time));put(b,"_overturn_level",p::integer(s.overturn_level));put(b,"_overturn_started",s.overturn_started?p::number(*s.overturn_started):p::retain(Py_None));
}
void flags(NativeOwner&o,PyObject*b,bool drown,bool enabled){
    auto avatar=field(b,"_avatar");const char*name=drown?"_offh_drowning":"_offh_overturned";
    put(o.entity.get(),name,p::boolean(enabled));put(avatar.get(),name,p::boolean(enabled));
}
void present(NativeOwner&o,PyObject*b,bool drown,int level,double now){
    auto runtime=field(b,"_runtime"),constants=field(runtime.get(),"constants"),group=field(constants.get(),"VEHICLE_MISC_STATUS"),levels=field(constants.get(),drown?"DROWN_WARNING_LEVEL":"OVERTURN_WARNING_LEVEL"),avatar=field(b,"_avatar"),server=field(b,"_server"),callback=field(avatar.get(),"updateVehicleMiscStatus");
    if(group.get()==Py_None||levels.get()==Py_None||server.get()==Py_None||!PyCallable_Check(callback.get()))return;
    auto status=field(group.get(),drown?"VEHICLE_DROWN_WARNING":"VEHICLE_IS_OVERTURNED");if(status.get()==Py_None)return;
    auto warning=field(levels.get(),level==2?"DANGER":level==1?"CAUTION":"SAFE"),vehicle=field(server.get(),"vehicle_id");
    const auto started=drown?o.clock.state().drown_started:o.clock.state().overturn_started;
    const double start=level==2?(started?*started:clock(o,now)):0.;
    p::Ref period(Py_BuildValue("(dd)",start,level==2?(drown?10.:30.):0.));
    p::Ref args(PyTuple_Pack(4,vehicle.get(),status.get(),warning.get(),period.get()));
    // Only a changed native warning crosses the stock HUD/Scaleform boundary.
    p::Ref result(PyObject_CallObject(callback.get(),args.get()));
}
bool metadata(NativeOwner&o){
    auto descriptor=field(o.entity.get(),"typeDescriptor");
    if(o.metadata_valid&&o.descriptor&&o.descriptor->get()==descriptor.get()&&o.descriptor_revision==o.actor->descriptor_revision)return true;
    o.metadata_valid=false;
    try{auto hull=field(descriptor.get(),"hull"),tester=field(hull.get(),"hitTester"),bbox=field(tester.get(),"bbox"),low=p::item(bbox.get(),0),high=p::item(bbox.get(),1),chassis=field(descriptor.get(),"chassis"),mount=field(chassis.get(),"hullPosition");
        o.metadata.minimum=xyz(low.get());o.metadata.maximum=xyz(high.get());o.metadata.mount=xyz(mount.get());
        o.descriptor.reset(new p::Ref(p::retain(descriptor.get())));o.descriptor_revision=o.actor->descriptor_revision;o.metadata_valid=true;
    }catch(const std::exception&){PyErr_Clear();}
    return o.metadata_valid;
}
#if defined(WOT_CLIENT_SURVIVAL_HOST_BUILD)
water::Pose nativeBody(PyObject*,const drive::PlayerState&);
double nativeDepth(PyObject*,const water::Point&);
#else
water::Pose nativeBody(PyObject*b,const drive::PlayerState&s){
    water::Pose result{s.position,s.yaw,s.pitch,s.roll};
    try{auto value=[&]{auto pose=field(b,"_local_pose_matrix");return pose.get()==Py_None?field(b,"_local_matrix"):std::move(pose);}();
        if(!BW::MatrixProvider::Check(value.get()))return result;BW::Matrix matrix;static_cast<BW::MatrixProvider*>(value.get())->matrix(matrix);const auto point=matrix.applyToOrigin();
        const water::Pose candidate{{point.x,point.y,point.z},matrix.yaw(),matrix.pitch(),matrix.roll()};
        for(const auto v:{candidate.position[0],candidate.position[1],candidate.position[2],candidate.yaw,candidate.pitch,candidate.roll})if(!std::isfinite(v))return result;
        return candidate;
    }catch(const std::exception&){PyErr_Clear();return result;}
}
double nativeDepth(PyObject*b,const water::Point&p){
    try{auto access=drive::sceneAccess(b);double distance=-1.;const water::Point start{float(p[0]),float(p[1]+20.),float(p[2])},end{float(p[0]),float(p[1]-5.),float(p[2])};
        if(access.scene->water(start,end,distance)!=stock_native_world::QueryStatus::Found||distance<0.)return -1.;return 20.-distance;
    }catch(const std::exception&){PyErr_Clear();return -1.;}
}
#endif
std::optional<int> level(NativeOwner&o,PyObject*b,const drive::PlayerState&s){
    if(!metadata(o))return {};const auto sample=water::hullSample(o.metadata,nativeBody(b,s));if(!sample)return {};
    const double depth=nativeDepth(b,sample->first);if(!std::isfinite(depth))return {};return depth>sample->second?2:depth>sample->second*.5?1:0;
}
PyObject*advance(PyObject*args,bool drown){PyObject*b;double dt,now;if(!PyArg_ParseTuple(args,"Odd",&b,&dt,&now))return nullptr;
    try{auto owner=acquire(b);if(!owner)return p::boolean(false).release();auto&o=*owner;
        const bool ready=eligible(o,b,dt);Effect effect;
        if(ready){auto access=drive::driverAccess(b);const auto&s=access.driver->state();const auto serverClock=[&]{return clock(o,now);};
            effect=drown?o.clock.drowning(dt,true,[&]{return level(o,b,s);},serverClock):o.clock.overturn(dt,true,s.surface_up_cosine.value_or(std::cos(s.pitch)*std::cos(s.roll)),serverClock);
        }
        if(!o.current())return p::boolean(false).release();mirror(b,o.clock.state());
        if(drown&&effect.sampled)flags(o,b,true,effect.flag);
        if(effect.event)present(o,b,drown,*effect.event,now);
        if(o.current()&&!drown&&effect.sampled)flags(o,b,false,effect.flag);
        return p::boolean(drown&&bool(effect.event)).release();
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
}
std::array<int,2> levels(PyObject*b){auto o=existing(b);return o&&o->current()?std::array<int,2>{o->clock.state().drown_level,o->clock.state().overturn_level}:std::array<int,2>{0,0};}
void retire(PyObject*b){auto o=existing(b);if(o)o->retired=true;put(b,"_native_client_survival",p::retain(Py_None));}
PyObject*drowningPython(PyObject*,PyObject*args){return advance(args,true);}
PyObject*overturnPython(PyObject*,PyObject*args){return advance(args,false);}
PyObject*retirePython(PyObject*,PyObject*args){PyObject*b;if(!PyArg_ParseTuple(args,"O",&b))return nullptr;try{retire(b);Py_RETURN_NONE;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
}
