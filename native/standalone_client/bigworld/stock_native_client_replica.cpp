#if !defined(WOT_CLIENT_REPLICA_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#endif
#include "stock_native_client_replica.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <new>

namespace stock_native_client_replica {
namespace p=stock_native_bot_python;
namespace {
using Pose=std::array<double,8>;
using Point=std::array<double,3>;
constexpr double pi=3.1415926535897932384626433832795;
constexpr const char* axes[]={"x","y","z","yaw","pitch","roll","aim_yaw","gun_pitch"};
constexpr const char* capsule_name="offline.client.replica";
void replace(p::Ref& destination,p::Ref value){destination.~Ref();new(&destination)p::Ref(std::move(value));}
double wrap(double v){return v-std::floor((v+pi)/(2.*pi))*(2.*pi);}
double principal(double v){return v<-pi||v>pi?wrap(v):v;}
double clamp(double v,double low,double high){return std::max(low,std::min(high,v));}
// Bypass the compatibility __getattribute__ overlay: these are the actual
// installed Python/native owners, not a callback into the gameplay adapter.
p::Ref field(PyObject*o,const char*n){
    if(!o||o==Py_None)return p::retain(Py_None);
    if(PyDict_Check(o))return p::value(o,n);
    auto key=p::text(n);auto*v=PyObject_GenericGetAttr(o,key.get());
    if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);
}
p::Ref stockCall(PyObject*o,const char*n,PyObject*args){auto fn=field(o,n);return p::Ref(PyObject_CallObject(fn.get(),args));}
void assign(PyObject*o,const char*n,const p::Ref&v){if(PyDict_Check(o)){p::set(o,n,v);return;}auto key=p::text(n);if(PyObject_GenericSetAttr(o,key.get(),v.get())<0)throw p::Error("Native client mirror assignment failed");}
void erase(PyObject*o,const char*n){if(PyDict_GetItemString(o,n)&&PyDict_DelItemString(o,n)<0)throw p::Error("Native client mirror removal failed");}
void put(PyObject*o,const char*n,double v){assign(o,n,p::number(v));}
Pose pose(PyObject*o){Pose result;for(unsigned i=0;i<8;++i)result[i]=p::numeric(o,axes[i]);return result;}
p::Ref pack(const Pose&v){auto out=p::dict();for(unsigned i=0;i<8;++i)p::set(out.get(),axes[i],p::number(v[i]));return out;}
Point location(const Pose&v){return {v[0],v[1],v[2]};}
#if defined(WOT_CLIENT_REPLICA_HOST_BUILD)
extern PyObject* nativeVector(const Point&);
extern Point nativeXYZ(PyObject*);
extern void nativeMatrix(PyObject*,const Point&,const Point&,bool);
extern bool engineOwns(std::int64_t,PyObject*);
#else
PyObject* nativeVector(const Point&v){return BW::Script::getData(BW::Vector3(float(v[0]),float(v[1]),float(v[2])));}
Point nativeXYZ(PyObject*o){BW::Vector3 v;if(BW::Script::setData(o,v)!=0)throw p::Error("Native client Vector3 is invalid");return {v.x,v.y,v.z};}
void nativeMatrix(PyObject*o,const Point&v,const Point&r,bool rotate){if(!BW::PyMatrix::Check(o))throw p::Error("Native client provider is not a writable Matrix");auto*m=static_cast<BW::PyMatrix*>(o);if(rotate)m->setRotate(float(r[0]),float(r[1]),float(r[2]));m->translation(BW::Vector3(float(v[0]),float(v[1]),float(v[2])));}
bool engineOwns(std::int64_t id,PyObject*python){auto*c=BW::ConnectionControl::pInstance();auto*e=c?c->findEntity(static_cast<BW::EntityID>(id)):nullptr;if(!e||e->isDestroyed()||!e->isInWorld())return false;p::Ref actual(e->pPyEntity().newRef());return actual.get()==python&&p::flag(field(python,"isStarted").get());}
#endif
struct Sample {double time=0.;std::optional<std::int64_t> revision;Pose value{};};
Sample sample(PyObject*o){Sample s;s.time=p::numeric(o,"time_us");auto r=p::value(o,"revision");if(r.get()!=Py_None)s.revision=p::wide(r.get());auto v=p::value(o,"pose");s.value=pose(v.get());return s;}
p::Ref bracket(const Sample&a,const Sample&b){if(!a.revision||!b.revision||*a.revision<0||*b.revision<*a.revision||*b.revision>2147483647||*b.revision-*a.revision>255||a.time<0.||b.time<a.time||b.time>9007199254740991.||((*a.revision==*b.revision)!=(a.time==b.time)))return p::retain(Py_None);auto out=p::list();p::append(out.get(),p::integer(*a.revision));p::append(out.get(),p::integer(std::int64_t(a.time)));p::append(out.get(),p::integer(*b.revision));p::append(out.get(),p::integer(std::int64_t(b.time)));return out;}
struct Owner;
struct Retired {};
void ensureOpen(const Owner&);
struct Entry {
    std::string key,kind;
    p::Ref sync,record,entity,motion,overlay,descriptor,gun,track_params;
    Pose current{},target{},velocity{},written{};
    std::vector<Sample> samples;
    double target_time=0.,sample_time=0.,anchor_time=0.,anchor_local=0.,ideal=0.,delay=0.,observed=0.;
    std::optional<double> cursor;
    std::optional<std::int64_t> revision;
    bool dead=false,settled=false,timed=false,teleport=false,resume=false,ready=false,has_written=false;
    bool has_observed=false,has_aim=false,track_pending=true,managed=false,resume_dirty=false,sample_dirty=false;
    double resume_sample=0.,speed=0.,drive=0.,rotation=0.,track_center=0.,speed_cap=0.;
    bool alive=true,airborne=false,around_center=true,authority_geometry=false;
    std::int64_t engine_id=0,siege_state=0;
    std::optional<double> static_yaw,static_pitch,last_pose_time,last_track_time,next_track_update;
    Point last_position{},last_velocity{};
    double last_yaw=0.,last_track_yaw=0.;
    std::array<double,4> last_aim{};
    Entry(std::string key_,std::string kind_,PyObject*s,PyObject*r):key(std::move(key_)),kind(std::move(kind_)),sync(p::retain(s)),record(p::retain(r)),entity(p::retain(Py_None)),motion(p::retain(Py_None)),overlay(p::retain(Py_None)),descriptor(p::retain(Py_None)),gun(p::retain(Py_None)),track_params(p::retain(Py_None)){}
};
struct Owner {
    std::map<std::string,std::unique_ptr<Entry>> entries;
    p::Ref round;
    std::optional<double> last_advance;
    bool closed=false;
    std::vector<std::pair<std::string,std::string>> ingress_errors;
    Owner():round(p::retain(Py_None)){}
};
void ensureOpen(const Owner&o){if(o.closed)throw Retired{};}
Owner& owner(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(!o)throw p::Error("Native client replica owner is invalid");if(o->closed)throw p::Error("Native client replica owner is closed");return *o;}
void destroy(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(o)delete o;else PyErr_Clear();}
std::optional<double> maybe(PyObject*o,const char*n){auto v=p::value(o,n);return v.get()==Py_None?std::optional<double>{}:p::scalar(v.get());}
// Called only when an entity/descriptor owner changes, never by render advance.
void attach(Owner&o,Entry&e,PyObject*battle){
    ensureOpen(o);
    e.ready=false;e.has_written=false;e.has_aim=false;e.track_pending=true;
    if(!p::truth(e.record.get(),"ready")||p::truth(e.record.get(),"local")||!p::truth(e.record.get(),"native_remote"))return;
    e.engine_id=p::integral(e.record.get(),"engine_id");auto factory=field(battle,"_remote_factory"),states=field(factory.get(),"_states"),vehicles=field(factory.get(),"_vehicles");auto key=p::integer(e.engine_id);
    auto*m=PyDict_Check(states.get())?PyDict_GetItem(states.get(),key.get()):nullptr;
    auto*v=PyDict_Check(vehicles.get())?PyDict_GetItem(vehicles.get(),key.get()):nullptr;
    if(!m||!v||v==Py_None||!engineOwns(e.engine_id,v))return;
    replace(e.motion,p::retain(m));replace(e.entity,p::retain(v));
    auto animation=field(m,"animation");if(animation.get()!=Py_None)throw p::Error("Visible client replica requires the confirmed-pose Matrix owner");
    auto position=field(m,"position");e.last_position=nativeXYZ(position.get());e.last_yaw=p::numeric(m,"yaw");auto velocity=field(m,"velocity");e.last_velocity=nativeXYZ(velocity.get());e.last_pose_time=maybe(m,"_last_pose_time");
    auto descriptor=field(v,"typeDescriptor"),gun=field(descriptor.get(),"gun");e.static_yaw=maybe(gun.get(),"staticTurretYaw");e.static_pitch=maybe(gun.get(),"staticPitch");e.authority_geometry=p::truth(m,"_authority_geometry");replace(e.descriptor,p::retain(descriptor.get()));replace(e.gun,p::retain(gun.get()));
    auto runtime=field(battle,"_runtime"),compat=field(runtime.get(),"compatibility"),overlays=field(compat.get(),"_vehicle_property_overlays");auto address=p::Ref(PyLong_FromVoidPtr(v));auto*overlay=PyDict_Check(overlays.get())?PyDict_GetItem(overlays.get(),address.get()):nullptr;
    if(!overlay||!PyDict_Check(overlay))throw p::Error("Native client pose overlay was not installed by attach");replace(e.overlay,p::retain(overlay));
    auto params=p::value(e.record.get(),"track_params");if(params.get()==Py_None){
        // Exact descriptor/crew extraction is an installation boundary. The
        // resulting three scalar belt inputs stay native thereafter.
        p::Ref args(PyTuple_Pack(2,e.record.get(),v));replace(params,p::call(battle,"_bot_track_params",args.get()));ensureOpen(o);}
    replace(e.track_params,p::retain(params.get()));e.track_center=p::numeric(params.get(),"trackCenter");e.speed_cap=p::numeric(params.get(),"speedFwd")*.995;e.around_center=p::truth(params.get(),"rotationIsAroundCenter",true);
    ensureOpen(o);e.ready=e.managed=true;
}
void mirrorCursor(Entry&e){
    if(e.cursor)put(e.sync.get(),"presentation_time_us",*e.cursor);
    if(e.timed&&e.samples.size()>=2){put(e.sync.get(),"presentation_delay_us",e.delay);if(e.resume_dirty)assign(e.sync.get(),"consumer_resume_pending",p::boolean(e.resume));if(e.sample_dirty)put(e.sync.get(),"consumer_resume_sample_us",e.resume_sample);e.resume_dirty=e.sample_dirty=false;}
}
void syncIngress(Owner&o,PyObject*sync,PyObject*battle){
    ensureOpen(o);
    auto round=p::value(sync,"round_id");int same=PyObject_RichCompareBool(round.get(),o.round.get(),Py_EQ);if(same<0)throw p::Error("Native client round comparison failed");if(!same){o.entries.clear();o.last_advance.reset();replace(o.round,std::move(round));}
    auto records=p::value(sync,"_entities"),battle_records=p::value(battle,"_records");std::map<std::string,bool> seen;o.ingress_errors.clear();
    Py_ssize_t cursor=0;PyObject*key=nullptr,*record=nullptr;
    while(PyDict_Next(records.get(),&cursor,&key,&record)){
        ensureOpen(o);
        const auto name=p::string(key);seen[name]=true;try{auto target=p::value(record,"target"),current=p::value(record,"current");if(target.get()==Py_None||current.get()==Py_None){o.entries.erase(name);continue;}
        auto*r=PyDict_Check(battle_records.get())?PyDict_GetItem(battle_records.get(),key):nullptr;if(!r||!PyDict_Check(r)||p::truth(r,"tombstone")){o.entries.erase(name);continue;}
        auto&item=o.entries[name];const bool fresh=!item||item->sync.get()!=record||item->record.get()!=r;
        if(fresh)item=std::make_unique<Entry>(name,p::string(p::value(record,"kind").get()),record,r);auto&e=*item;
        e.current=pose(current.get());e.target=pose(target.get());auto v=p::value(record,"velocity");auto vel=p::numbers<3>(v.get());std::copy(vel.begin(),vel.end(),e.velocity.begin());e.target_time=p::numeric(record,"target_time");e.timed=p::truth(record,"timed_prediction");e.dead=p::truth(record,"dead");e.settled=p::truth(record,"wreck_settled");e.teleport=p::truth(record,"timed_teleport");
        e.sample_time=p::numeric(record,"target_sample_time_us");auto revision=p::value(record,"target_sample_revision");e.revision=revision.get()==Py_None?std::optional<std::int64_t>{}:p::wide(revision.get());
        e.anchor_time=p::numeric(record,"motion_anchor_time_us");e.anchor_local=p::numeric(record,"motion_anchor_local_time");e.ideal=p::numeric(record,"interpolation_delay_us");e.delay=p::numeric(record,"presentation_delay_us");auto observed=maybe(record,"observed_delay_us");e.has_observed=bool(observed);e.observed=observed.value_or(0.);e.cursor=maybe(record,"presentation_time_us");e.resume=p::truth(record,"consumer_resume_pending");e.resume_sample=p::numeric(record,"consumer_resume_sample_us");
        e.samples.clear();auto samples=p::value(record,"timed_samples");if(samples.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(samples.get());++i){auto s=p::item(samples.get(),i);e.samples.push_back(sample(s.get()));}
        auto state=p::value(r,"state");e.speed=p::numeric(state.get(),"speed");e.drive=p::numeric(state.get(),"movement_dir");e.rotation=p::numeric(state.get(),"rotation_dir");e.alive=p::truth(state.get(),"alive",true)&&p::numeric(state.get(),"health",1.)>0.;e.airborne=p::truth(state.get(),"airborne");e.siege_state=p::integral(state.get(),"siege_state");
        if(fresh||!e.ready||!p::truth(r,"ready"))attach(o,e,battle);
        // Snapshot visibility and model replacement remain stock lifecycle
        // operations. Reacquire native ownership after those cold operations.
        else if(!engineOwns(e.engine_id,e.entity.get())){e.ready=false;}
        else {auto factory=field(battle,"_remote_factory"),states=field(factory.get(),"_states");auto id=p::integer(e.engine_id);auto*m=PyDict_GetItem(states.get(),id.get());auto descriptor=field(e.entity.get(),"typeDescriptor"),gun=field(descriptor.get(),"gun"),params=p::value(r,"track_params");if(m!=e.motion.get()||descriptor.get()!=e.descriptor.get()||gun.get()!=e.gun.get()||params.get()!=e.track_params.get())attach(o,e,battle);}
        }catch(const std::exception&error){ensureOpen(o);PyErr_Clear();o.entries.erase(name);o.ingress_errors.emplace_back(name,error.what());}
    }
    for(auto i=o.entries.begin();i!=o.entries.end();)if(!seen.count(i->first))i=o.entries.erase(i);else ++i;
}
Pose blend(const Pose&a,const Pose&b,double progress){Pose out=a;for(unsigned i=0;i<8;++i)out[i]+=(i>=3&&i<=6?wrap(b[i]-a[i]):b[i]-a[i])*progress;return out;}
bool interpolate(Entry&e,double now,double render_delta,double alpha,p::Ref&presentation_bracket,bool&snapped){
    if(e.dead){if(e.settled&&e.current==e.target)return false;double distance=0.,angle=0.;for(unsigned i=0;i<3;++i)distance+=std::pow(e.target[i]-e.current[i],2.);for(unsigned i=3;i<8;++i)angle=std::max(angle,std::abs(i<7?wrap(e.target[i]-e.current[i]):e.target[i]-e.current[i]));e.settled=distance<=.002*.002&&angle<=.001;e.current=e.settled?e.target:blend(e.current,e.target,alpha);snapped=e.settled;assign(e.sync.get(),"wreck_settled",p::boolean(e.settled));return true;}
    Pose desired=e.target;
    if(e.timed&&e.samples.size()>=2){
        if(render_delta>=.1){e.resume=true;e.resume_sample=e.sample_time;e.resume_dirty=e.sample_dirty=true;}put(e.sync.get(),"consumer_render_delta",render_delta);
        const double authority_now=e.anchor_time+std::max(0.,now-e.anchor_local)*1000000.;const double previous=e.cursor.value_or(e.samples.front().time);
        e.delay=std::max(e.delay,authority_now-render_delta*1000000.-previous);double rate=std::min(1.,std::pow((e.ideal-e.delay)/1000000.,2.));
        if(e.ideal<e.delay){rate=std::max(rate,e.delay>120000.?.10:.005);if(e.delay>120000.&&e.samples.back().time-previous>e.ideal){rate=std::max(rate,std::min(.10,(e.delay-e.ideal)/1000000.));if(e.resume&&render_delta<.1)rate=std::max(rate,.5);}}
        if(e.resume&&render_delta<.1)rate=std::min(rate,.5);
        const double step=render_delta*rate*1000000.;e.delay=e.ideal>e.delay?std::min(e.ideal,e.delay+step):std::max(e.ideal,e.delay-step);
        if(e.has_observed&&render_delta<.1&&e.sample_time>e.resume_sample&&std::max(e.delay,e.ideal)<=std::max(60000.,e.observed)+15000.){e.resume=false;e.resume_dirty=true;}
        e.cursor=std::min({e.samples.back().time,previous+render_delta*(1.+rate)*1000000.,std::max(authority_now-e.delay,previous)});
        if(e.kind=="bot"){
            auto probe=p::value(e.sync.get(),"motion_probe");if(probe.get()==Py_None){replace(probe,p::dict());p::set(e.sync.get(),"motion_probe",probe);}const bool held=*e.cursor<=previous;bool moving=false;for(unsigned i=0;i<3;++i)moving=moving||std::abs(e.samples.back().value[i]-e.samples.front().value[i])>.01;
            put(probe.get(),"frames",p::numeric(probe.get(),"frames")+1.);put(probe.get(),"held_frames",p::numeric(probe.get(),"held_frames")+held);put(probe.get(),"moving_held_frames",p::numeric(probe.get(),"moving_held_frames")+(held&&moving));put(probe.get(),"max_render_gap_ms",std::max(p::numeric(probe.get(),"max_render_gap_ms"),render_delta*1000.));}
        std::size_t consumed=0;while(e.samples.size()-consumed>2&&*e.cursor>=e.samples[consumed+1].time)++consumed;
        if(consumed){e.samples.erase(e.samples.begin(),e.samples.begin()+consumed);auto samples=p::value(e.sync.get(),"timed_samples");if(PyList_SetSlice(samples.get(),0,Py_ssize_t(consumed),nullptr)<0)throw p::Error("Native client history prune failed");}
        const Sample*a=&e.samples.front(),*b=&e.samples.back();for(std::size_t i=1;i<e.samples.size();++i)if(*e.cursor<=e.samples[i].time){a=&e.samples[i-1];b=&e.samples[i];break;}
        replace(presentation_bracket,bracket(*a,*b));desired=blend(a->value,b->value,clamp((*e.cursor-a->time)/std::max(1.,b->time-a->time),0.,1.));
    }else if(e.timed){const Sample endpoint{e.sample_time,e.revision,e.target};replace(presentation_bracket,bracket(endpoint,endpoint));if(!e.cursor)e.cursor=e.sample_time;}
    else {const double predict=std::min(std::max(0.,now-e.target_time),.05);for(unsigned i=0;i<3;++i)desired[i]+=e.velocity[i]*predict;}
    const Pose&snap=e.timed?(e.teleport?e.target:desired):e.target;double distance=0.;for(unsigned i=0;i<3;++i)distance+=std::pow(snap[i]-e.current[i],2.);
    snapped=distance>625.;if(snapped){e.current=snap;if(e.timed){Sample endpoint{e.sample_time,e.revision,e.target};e.samples={endpoint};e.cursor=e.sample_time;e.ideal=e.delay=0.;e.teleport=false;auto samples=p::list(),row=p::dict();p::set(row.get(),"time_us",p::number(e.sample_time));p::set(row.get(),"revision",e.revision?p::integer(*e.revision):p::retain(Py_None));p::set(row.get(),"pose",pack(e.target));p::append(samples.get(),row);p::set(e.sync.get(),"timed_samples",samples);for(const char*n:{"interpolation_delay_us","presentation_delay_us"})p::set(e.sync.get(),n,p::retain(Py_None));put(e.sync.get(),"timed_warmup_intervals",0.);p::set(e.sync.get(),"timed_warmup_active",p::boolean(false));p::set(e.sync.get(),"timed_teleport",p::boolean(false));replace(presentation_bracket,bracket(endpoint,endpoint));}}
    else e.current=e.timed?desired:blend(e.current,desired,alpha);
    mirrorCursor(e);return true;
}
void publishMotion(Entry&e,const Point&position,const Point&velocity,const Point&acceleration,double speed,double turn,double now){
    auto m=e.motion.get();p::Ref xyz(nativeVector(position)),v(nativeVector(velocity)),a(nativeVector(acceleration));assign(m,"position",xyz);assign(m,"velocity",v);assign(m,"acceleration",a);put(m,"speed",speed);put(m,"turn_speed",turn);put(m,"_last_pose_time",now);
    auto matrix=field(m,"provider");for(const char*n:{"matrix","steady_rotation_matrix","stabilised_matrix"})p::set(e.overlay.get(),n,matrix);p::set(e.overlay.get(),"_pose_active",p::boolean(true));p::set(e.overlay.get(),"position",xyz);put(e.overlay.get(),"yaw",principal(e.current[3]));put(e.overlay.get(),"speed",speed);put(e.overlay.get(),"turn_speed",turn);p::set(e.overlay.get(),"velocity",v);p::set(e.overlay.get(),"acceleration",a);
    e.last_position=position;e.last_velocity=velocity;e.last_yaw=principal(e.current[3]);e.last_pose_time=now;
}
void presentPose(Entry&e,double now){
    const bool changed=!e.has_written||e.written!=e.current;
    if(changed){
        // Clear before the first native write: a partial failure must retry
        // even when the next sample returns to the former pose.
        e.has_written=false;auto matrix=field(e.motion.get(),"matrix");p::Ref vector(nativeVector(location(e.current)));const Point actual=nativeXYZ(vector.get());Point rotation{principal(e.current[3]),principal(e.current[4]),principal(e.current[5])};
        auto cache=field(e.motion.get(),"_matrix_pose");const auto xyz=p::tupleNumbers(actual),angles=p::tupleNumbers(rotation);bool rotated=true,translated=true;
        if(PyTuple_Check(cache.get())&&PyTuple_GET_SIZE(cache.get())==3&&PyTuple_GET_ITEM(cache.get(),0)==matrix.get()){
            int equal=PyObject_RichCompareBool(PyTuple_GET_ITEM(cache.get(),2),angles.get(),Py_EQ);if(equal<0)throw p::Error("Native client matrix cache invalid");rotated=!equal;equal=PyObject_RichCompareBool(PyTuple_GET_ITEM(cache.get(),1),xyz.get(),Py_EQ);if(equal<0)throw p::Error("Native client matrix cache invalid");translated=rotated||!equal;}
        assign(e.motion.get(),"_matrix_pose",p::retain(Py_None));if(translated)nativeMatrix(matrix.get(),actual,rotation,rotated);p::Ref cached(PyTuple_Pack(3,matrix.get(),xyz.get(),angles.get()));assign(e.motion.get(),"_matrix_pose",cached);
        Point velocity{},acceleration{};double speed=0.,turn=0.;if(e.last_pose_time&&now>*e.last_pose_time){const float inverse=float(1./(now-*e.last_pose_time));for(unsigned i=0;i<3;++i){velocity[i]=float(float(actual[i]-e.last_position[i])*inverse);acceleration[i]=float(float(velocity[i]-e.last_velocity[i])*inverse);}speed=velocity[0]*std::sin(rotation[0])+velocity[2]*std::cos(rotation[0]);turn=wrap(rotation[0]-e.last_yaw)/(now-*e.last_pose_time);}
        put(e.motion.get(),"yaw",rotation[0]);put(e.motion.get(),"pitch",rotation[1]);put(e.motion.get(),"roll",rotation[2]);auto rendered=p::tupleNumbers(std::array<double,6>{actual[0],actual[1],actual[2],rotation[0],rotation[1],rotation[2]});assign(e.motion.get(),"_render_pose",rendered);publishMotion(e,actual,velocity,acceleration,speed,turn,now);e.written=e.current;e.has_written=true;erase(e.record.get(),"_remote_motion_settled_signature");
    }else if(std::abs(e.speed)<=.05&&std::abs(e.drive)<=.5&&std::abs(e.rotation)<=.5&&(e.last_velocity!=Point{}||p::numeric(e.motion.get(),"turn_speed")!=0.)){
        publishMotion(e,e.last_position,Point{},Point{},0.,0.,now);
    }
    const std::array<double,4> input{e.current[3],e.current[4],e.current[6],e.current[7]};
    if(!e.has_aim||input!=e.last_aim){
        e.has_aim=false;const double relative=e.static_yaw.value_or(wrap(e.current[6]-e.current[3])),pitch=e.static_pitch.value_or(e.current[7]);auto aim=field(e.motion.get(),"aim"),turret=field(aim.get(),"turretMatrix"),gun=field(aim.get(),"gunMatrix");
        auto old_yaw=maybe(e.motion.get(),"_aim_relative_yaw"),old_pitch=maybe(e.motion.get(),"_aim_gun_pitch");if(!old_yaw||*old_yaw!=relative){nativeMatrix(turret.get(),Point{},{relative,0.,0.},true);put(e.motion.get(),"_aim_relative_yaw",relative);}if(!old_pitch||*old_pitch!=pitch){nativeMatrix(gun.get(),Point{},{0.,pitch,0.},true);put(e.motion.get(),"_aim_gun_pitch",pitch);}put(e.motion.get(),"_aim_desired_gun_pitch",pitch);put(e.entity.get(),"_aim_yaw",e.current[6]);put(e.entity.get(),"_gun_pitch",e.current[7]);e.last_aim=input;e.has_aim=true;
    }
    // Collision and RAM consumers read the same current pose as the renderer.
    auto collision=pack(e.current);erase(collision.get(),"aim_yaw");p::set(collision.get(),"turret_yaw",p::number(wrap(e.current[6]-e.current[3])));p::set(collision.get(),"siege_state",p::integer(e.siege_state));p::set(e.record.get(),"projectile_collision_pose",collision);
}
void tracks(Owner&o,Entry&e,double now){
    ensureOpen(o);
    double turn=0.;if(e.last_track_time&&now>*e.last_track_time)turn=wrap(e.current[3]-e.last_track_yaw)/(now-*e.last_track_time);e.last_track_time=now;e.last_track_yaw=e.current[3];
    if(e.next_track_update&&now+1.e-9<*e.next_track_update)return;
    const double next=e.next_track_update.value_or(now);e.next_track_update=next+std::max(1.,std::floor(std::max(0.,now-next)/.05)+1.)*.05;
    // Stock may replace the appearance/filter while applying damage. Read
    // these owners only at the stock 20 Hz feed boundary, after native entity
    // identity has been checked; never hold a stale native component pointer.
    auto draw_visible=field(e.entity.get(),"_offlineNativeDrawVisible");const bool hidden=draw_visible.get()!=Py_None&&!p::flag(draw_visible.get());const bool was_hidden=p::truth(e.motion.get(),"_track_feed_hidden");
    if(hidden&&was_hidden)return;
    if(hidden){assign(e.motion.get(),"_track_feed_hidden",p::boolean(true));assign(e.motion.get(),"_track_feed",p::retain(Py_None));}
    else if(was_hidden){assign(e.motion.get(),"_track_feed_hidden",p::boolean(false));assign(e.motion.get(),"_track_feed",p::retain(Py_None));assign(e.motion.get(),"track_mode",p::retain(Py_None));}
    double speed=e.speed;const int flags=(speed>.05?1:speed<-.05?2:0)|(turn<-.02?4:turn>.02?8:0),power=e.alive?(flags?2:1):0;
    if(!e.airborne&&!e.around_center&&std::abs(turn)>1.e-9){const double radius=std::max(0.,e.track_center-std::abs(speed/turn)),direction=speed<-1.e-9||(std::abs(speed)<=1.e-9&&e.drive<0.)?-1.:1.;speed+=radius*direction*(turn>0.?1.:-1.)*turn;}
    double left=clamp(clamp(speed+turn*e.track_center,-e.speed_cap,e.speed_cap),-15.,30.),right=clamp(clamp(speed-turn*e.track_center,-e.speed_cap,e.speed_cap),-15.,30.);if(hidden)left=right=0.;
    auto appearance=field(e.entity.get(),"appearance");if(appearance.get()==Py_None)return;
    auto flying=field(appearance.get(),"flyingInfoProvider");const bool left_contact=!p::truth(flying.get(),"isLeftSideFlying"),right_contact=!p::truth(flying.get(),"isRightSideFlying");auto power_value=p::integer(power),flags_value=p::integer(e.alive?flags:0);p::Ref mode(PyTuple_Pack(2,power_value.get(),flags_value.get()));auto l=p::number(std::round(left*1000.)/1000.),r=p::number(std::round(right*1000.)/1000.),lc=p::boolean(left_contact),rc=p::boolean(right_contact);p::Ref feed(PyTuple_Pack(5,l.get(),r.get(),mode.get(),lc.get(),rc.get()));auto old=field(e.motion.get(),"_track_feed");const int equal=PyObject_RichCompareBool(feed.get(),old.get(),Py_EQ);if(equal<0)throw p::Error("Native client track cache invalid");if(equal){assign(e.motion.get(),"track_outcome",p::text("deduplicated"));return;}
    auto filter=field(e.entity.get(),"filter"),appearance_filter=field(appearance.get(),"filter"),movement=field(filter.get(),"movementInfo"),setter=field(filter.get(),"setTracksSpeed");bool updated=false;
    if(filter.get()==appearance_filter.get()&&movement.get()!=Py_None&&PyCallable_Check(setter.get())){auto exact_l=p::number(left),exact_r=p::number(right);p::Ref args(PyTuple_Pack(4,exact_l.get(),lc.get(),exact_r.get(),rc.get()));p::Ref result(PyObject_CallObject(setter.get(),args.get()));ensureOpen(o);updated=true;}
    if(!hidden){auto old_mode=field(e.motion.get(),"track_mode");int same=PyObject_RichCompareBool(mode.get(),old_mode.get(),Py_EQ);if(same<0)throw p::Error("Native client engine cache invalid");if(!same){p::Ref args(PyTuple_Pack(2,mode.get(),Py_True));stockCall(appearance.get(),"changeEngineMode",args.get());ensureOpen(o);assign(e.motion.get(),"track_mode",mode);}}
    auto exact_l=p::number(left),exact_r=p::number(right);p::Ref args(PyTuple_Pack(2,exact_l.get(),exact_r.get()));stockCall(appearance.get(),"updateTracksScroll",args.get());ensureOpen(o);if(updated)assign(e.motion.get(),"_track_feed",feed);
    p::Ref written(PyTuple_Pack(2,exact_l.get(),exact_r.get()));assign(e.motion.get(),"track_written",written);assign(e.motion.get(),"track_outcome",p::text(hidden?"hidden and settling":updated?"engine and python":"python only"));auto capabilities=field(e.motion.get(),"presentation_capabilities");if(PyDict_Check(capabilities.get()))p::set(capabilities.get(),"engine_owned_track_motion",p::boolean(updated));
}
p::Ref event(Entry&e,bool snapped,const p::Ref&bracket){auto out=p::dict();p::set(out.get(),"type",p::text("update"));p::set(out.get(),"entity",p::text(e.key));p::set(out.get(),"kind",p::text(e.kind));p::set(out.get(),"id",p::value(e.sync.get(),"id"));p::set(out.get(),"pose",pack(e.current));for(const char*n:{"remote","interpolated"})p::set(out.get(),n,p::boolean(true));p::set(out.get(),"snap",p::boolean(snapped));p::set(out.get(),"presentation_time_us",e.timed&&!e.dead&&e.cursor?p::integer(std::int64_t(std::round(*e.cursor))):p::retain(Py_None));if(!e.dead)p::set(out.get(),"presentation_bracket",bracket);return out;}
PyObject* fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyObject* create(PyObject*,PyObject*args){if(!PyArg_ParseTuple(args,""))return nullptr;try{return PyCapsule_New(new Owner(),capsule_name,destroy);}catch(const std::exception&e){return fail(e);}}
PyObject* ingest(PyObject*,PyObject*args){PyObject*c,*sync,*battle;if(!PyArg_ParseTuple(args,"OOO",&c,&sync,&battle))return nullptr;try{auto&o=owner(c);syncIngress(o,sync,battle);auto errors=p::list();for(const auto&error:o.ingress_errors){auto row=p::dict();p::set(row.get(),"entity",p::text(error.first));p::set(row.get(),"error",p::text(error.second));p::append(errors.get(),row);}return errors.release();}catch(const Retired&){PyErr_Clear();return PyList_New(0);}catch(const std::exception&e){return fail(e);}}
PyObject* advance(PyObject*,PyObject*args){PyObject*c,*sync;double now;if(!PyArg_ParseTuple(args,"OOd",&c,&sync,&now))return nullptr;try{
    if(!std::isfinite(now))throw p::Error("Native client render time invalid");auto&o=owner(c);const double render_delta=o.last_advance?std::max(0.,now-*o.last_advance):.016;o.last_advance=now;put(sync,"_last_advance",now);const double alpha=1.-std::exp(-20.*clamp(render_delta,.001,.1));auto pending=p::list(),errors=p::list();
    for(auto&item:o.entries){ensureOpen(o);auto&e=*item.second;try{auto br=p::retain(Py_None);bool snapped=false;if(!interpolate(e,now,render_delta,alpha,br,snapped))continue;auto current=pack(e.current);p::set(e.sync.get(),"current",current);if(!e.dead)p::set(e.sync.get(),"presentation_bracket",br);
        if(!e.ready){p::append(pending.get(),event(e,snapped,br));continue;}
        if(!engineOwns(e.engine_id,e.entity.get())){e.ready=false;continue;}
        p::set(e.record.get(),"presented_pose",current);p::set(e.record.get(),"presentation_time_us",e.timed&&!e.dead&&e.cursor?p::integer(std::int64_t(std::round(*e.cursor))):p::retain(Py_None));p::set(e.record.get(),"presentation_bracket",br);presentPose(e,now);ensureOpen(o);tracks(o,e,now);ensureOpen(o);
    }catch(const std::exception&error){ensureOpen(o);e.has_written=false;e.has_aim=false;PyErr_Clear();auto row=p::dict();p::set(row.get(),"entity",p::text(e.key));p::set(row.get(),"error",p::text(error.what()));p::append(errors.get(),row);}}
    return Py_BuildValue("NN",pending.release(),errors.release());
}catch(const Retired&){PyErr_Clear();return Py_BuildValue("NN",PyList_New(0),PyList_New(0));}catch(const std::exception&e){return fail(e);}}
PyObject* manages(PyObject*,PyObject*args){PyObject*c,*record;if(!PyArg_ParseTuple(args,"OO",&c,&record))return nullptr;try{auto&o=owner(c);for(auto&item:o.entries){auto&e=*item.second;if(e.record.get()==record){if(e.managed){if(e.ready&&!engineOwns(e.engine_id,e.entity.get()))e.ready=false;Py_RETURN_TRUE;}break;}}Py_RETURN_FALSE;}catch(const std::exception&e){return fail(e);}}
// Stock presentation may synchronously call Battle._cleanup while advance
// holds an Entry reference. Retire now; the active CAPI argument keeps the
// capsule alive and its storage is released only by the capsule destructor.
PyObject* close(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(!o)return nullptr;o->closed=true;Py_RETURN_NONE;}
}
