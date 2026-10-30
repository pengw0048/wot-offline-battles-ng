#if !defined(WOT_CLIENT_REPLICA_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script_math.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#endif
#include "stock_native_client_replica.hpp"
#include "stock_native_client_state.hpp"
#include "stock_wire_replica_store.hpp"
#include "stock_native_client_drive_presentation_python.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <new>
#include <set>
#include <chrono>

namespace stock_native_client_replica {
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
namespace {
using Pose=cs::Pose;
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
    auto key=p::text(n);auto*v=p::directAttribute(o,key.get());
    if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);
}
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
using Sample=cs::Sample;
Sample sample(PyObject*o){Sample s;s.time=p::numeric(o,"time_us");auto r=p::value(o,"revision");if(r.get()!=Py_None)s.revision=p::wide(r.get());auto v=p::value(o,"pose");s.value=pose(v.get());return s;}
p::Ref bracket(const Sample&a,const Sample&b){if(!a.revision||!b.revision||*a.revision<0||*b.revision<*a.revision||*b.revision>2147483647||*b.revision-*a.revision>255||a.time<0.||b.time<a.time||b.time>9007199254740991.||((*a.revision==*b.revision)!=(a.time==b.time)))return p::retain(Py_None);auto out=p::list();p::append(out.get(),p::integer(*a.revision));p::append(out.get(),p::integer(std::int64_t(a.time)));p::append(out.get(),p::integer(*b.revision));p::append(out.get(),p::integer(std::int64_t(b.time)));return out;}
struct Owner;
struct Retired {};
void ensureOpen(const Owner&);
struct Entry {
    std::string key,kind;
    p::Ref sync,record,entity,motion,overlay,descriptor,gun,track_params;
    std::shared_ptr<cs::Actor> data;
    Pose& current=data->current;Pose& target=data->target;Pose& velocity=data->velocity;
    Pose written{};
    std::vector<Sample>& samples=data->samples;
    double& target_time=data->target_time;double& sample_time=data->sample_time;
    double& anchor_time=data->anchor_time;double& anchor_local=data->anchor_local;
    double& ideal=data->ideal;double& delay=data->delay;double& observed=data->observed;
    std::optional<double>& cursor=data->cursor;
    std::optional<std::int64_t>& revision=data->sample_revision;
    bool& dead=data->dead;bool& settled=data->settled;bool& timed=data->timed;
    bool& teleport=data->teleport;bool& resume=data->resume;bool& has_observed=data->has_observed;
    bool ready=false,has_written=false,has_aim=false,track_pending=true,managed=false;
    bool resume_dirty=false,sample_dirty=false;
    double& resume_sample=data->resume_sample;double& speed=data->speed;
    double& drive=data->drive;double& rotation=data->rotation;
    double track_center=0.,speed_cap=0.;
    bool& alive=data->alive;bool& airborne=data->airborne;
    bool around_center=true,authority_geometry=false;
    std::int64_t engine_id=0;std::int64_t& siege_state=data->siege_state;
    std::optional<double> static_yaw,static_pitch,last_pose_time,last_track_time,next_track_update;
    Point last_position{},last_velocity{};
    double last_yaw=0.,last_track_yaw=0.;
    std::array<double,4> last_aim{};
    Entry(std::string key_,std::string kind_,PyObject*s,PyObject*r,std::shared_ptr<cs::Actor> actor={}):key(std::move(key_)),kind(std::move(kind_)),sync(p::retain(s)),record(p::retain(r)),entity(p::retain(Py_None)),motion(p::retain(Py_None)),overlay(p::retain(Py_None)),descriptor(p::retain(Py_None)),gun(p::retain(Py_None)),track_params(p::retain(Py_None)),data(actor?std::move(actor):std::make_shared<cs::Actor>()){}
};
struct StaticVersion {p::Ref source;std::shared_ptr<const cs::Value> value;StaticVersion(PyObject*s,cs::Value v):source(p::retain(s)),value(std::make_shared<const cs::Value>(std::move(v))){} };
struct Owner {
    std::map<std::string,std::unique_ptr<Entry>> entries;
    std::shared_ptr<cs::State> state=std::make_shared<cs::State>();
    bool direct=false;
    std::map<PyObject*,std::unique_ptr<StaticVersion>> static_versions;
    std::uint64_t received=0,static_reads=0,static_reuses=0,bindings_revision=0,lifecycle_events=0;
    std::map<std::string,cs::Value> pending_creates;
    p::Ref round;
    std::optional<double> last_advance;
    bool closed=false;
    std::uint64_t snapshot_calls=0,snapshot_parse_calls=0;
    double snapshot_parse_seconds=0.,snapshot_state_seconds=0.,snapshot_events_seconds=0.,snapshot_release_seconds=0.;
    std::vector<std::pair<std::string,std::string>> ingress_errors;
    Owner():round(p::retain(Py_None)){}
};
// Canonical snapshot static versions are immutable Python containers. Retain
// the exact source identity at this explicit bridge, then share owned native
// values across accepted snapshots and source-history receipts.
cs::Value readMessage(Owner&o,PyObject*raw,const std::string&field={}){
    if((field=="outfits"||field=="effective_params")&&(std::string(Py_TYPE(raw)->tp_name)=="_StaticDict"||std::string(Py_TYPE(raw)->tp_name)=="_StaticList")){auto&entry=o.static_versions[raw];if(!entry){entry=std::make_unique<StaticVersion>(raw,cs::read(raw));++o.static_reads;}else ++o.static_reuses;cs::Value result;result.kind=cs::Value::Kind::Shared;result.reference=entry->value;return result;}
    if(PyDict_Check(raw)){cs::Value result;result.kind=cs::Value::Kind::Object;Py_ssize_t cursor=0;PyObject*key=nullptr,*value=nullptr;while(PyDict_Next(raw,&cursor,&key,&value)){const auto name=p::string(key);result.object.emplace(name,readMessage(o,value,name));}return result;}
    if(PyList_Check(raw)||PyTuple_Check(raw)){cs::Value result;result.kind=cs::Value::Kind::Array;const auto count=PySequence_Size(raw);result.array.reserve(std::size_t(count));for(Py_ssize_t i=0;i<count;++i){auto value=p::item(raw,i);result.array.push_back(readMessage(o,value.get()));}return result;}
    return cs::read(raw);
}
// One immutable owned version per accepted actor. Histories and the current
// actor retain this version; they never clone its nested equipment/crit tree.
cs::Value readActorRows(Owner&o,PyObject*raw){
    if(!PyList_Check(raw)&&!PyTuple_Check(raw))return readMessage(o,raw);
    cs::Value rows;rows.kind=cs::Value::Kind::Array;rows.array.reserve(std::size_t(PySequence_Size(raw)));
    for(Py_ssize_t i=0;i<PySequence_Size(raw);++i){auto item=p::item(raw,i);auto value=readMessage(o,item.get());if(value.kind==cs::Value::Kind::Object){cs::Value version;version.kind=cs::Value::Kind::Shared;version.reference=std::make_shared<const cs::Value>(std::move(value));rows.array.push_back(std::move(version));}else rows.array.push_back(std::move(value));}
    return rows;
}
// Admission retains complete manifests/world/projectile payloads for their
// respective owners. Replica state consumes only this envelope projection;
// actor rows remain complete, including future/unknown presentation fields.
cs::Value readStateMessage(Owner&o,PyObject*raw,bool is_manifest){
    if(!PyDict_Check(raw))return readMessage(o,raw);
    cs::Value out;out.kind=cs::Value::Kind::Object;
    auto field=[&](const char*name){auto*value=PyDict_GetItemString(raw,name);if(value)out.object.emplace(name,(std::string(name)=="players"||std::string(name)=="bots")?readActorRows(o,value):readMessage(o,value,name));};
    field("round_id");for(const char*name:{"players","bots"})field(name);
    if(!is_manifest)for(const char*name:{"sequence","server_tick","bot_state_revision","motion_time_us","bot_state_time_us","timing","server_time_ms","_client_dispatch_delay","_client_coalesced_timing","bot_orders","bot_order_revision"})field(name);
    // Replica snapshots were merged on the receive thread; their rows never became Python.
    auto*native=is_manifest?nullptr:PyDict_GetItemString(raw,"_native_snapshot");
    // Admission completed only the recipient's own row (static inputs); it stays authoritative.
    if(native){auto complete=stock_wire::snapshot_from_capsule(native);if(!complete)throw p::Error("Native replica snapshot capsule is invalid");
        auto&players=out.object["players"];if(players.kind!=cs::Value::Kind::Array)players.kind=cs::Value::Kind::Array;std::set<std::int64_t>own;for(const auto&row:players.array)own.insert(std::int64_t(row.get("id").numeric()));
        for(const auto&row:complete->get("players").array)if(!own.count(std::int64_t(row.get("id").numeric())))players.array.push_back(row);out.object["bots"]=complete->get("bots");}
    return out;
}
void ensureOpen(const Owner&o){if(o.closed)throw Retired{};}
Owner& owner(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(!o)throw p::Error("Native client replica owner is invalid");if(o->closed)throw p::Error("Native client replica owner is closed");return *o;}
void destroy(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(o)delete o;else PyErr_Clear();}
std::optional<double> maybe(PyObject*o,const char*n){auto v=p::value(o,n);return v.get()==Py_None?std::optional<double>{}:p::scalar(v.get());}
// Called only when an entity/descriptor owner changes, never by render advance.
void attach(Owner&o,Entry&e,PyObject*battle){
    ensureOpen(o);
    e.ready=false;e.data->ready=false;e.has_written=false;e.has_aim=false;e.track_pending=true;
    if(!p::truth(e.record.get(),"ready")||p::truth(e.record.get(),"local")||!p::truth(e.record.get(),"native_remote"))return;
    e.engine_id=p::integral(e.record.get(),"engine_id");e.data->engine_id=e.engine_id;auto factory=field(battle,"_remote_factory"),states=field(factory.get(),"_states"),vehicles=field(factory.get(),"_vehicles");auto key=p::integer(e.engine_id);
    auto*m=PyDict_Check(states.get())?PyDict_GetItem(states.get(),key.get()):nullptr;
    auto*v=PyDict_Check(vehicles.get())?PyDict_GetItem(vehicles.get(),key.get()):nullptr;
    if(!m||!v||v==Py_None||!engineOwns(e.engine_id,v))return;
    replace(e.motion,p::retain(m));replace(e.entity,p::retain(v));
    auto animation=field(m,"animation");if(animation.get()!=Py_None)throw p::Error("Visible client replica requires the confirmed-pose Matrix owner");
    auto position=field(m,"position");e.last_position=nativeXYZ(position.get());e.last_yaw=p::numeric(m,"yaw");auto velocity=field(m,"velocity");e.last_velocity=nativeXYZ(velocity.get());e.last_pose_time=maybe(m,"_last_pose_time");
    auto descriptor=field(v,"typeDescriptor"),gun=field(descriptor.get(),"gun");if(e.descriptor.get()!=descriptor.get())++e.data->descriptor_revision;e.static_yaw=maybe(gun.get(),"staticTurretYaw");e.static_pitch=maybe(gun.get(),"staticPitch");e.authority_geometry=p::truth(m,"_authority_geometry");replace(e.descriptor,p::retain(descriptor.get()));replace(e.gun,p::retain(gun.get()));
    auto runtime=field(battle,"_runtime"),compat=field(runtime.get(),"compatibility"),overlays=field(compat.get(),"_vehicle_property_overlays");auto address=p::Ref(PyLong_FromVoidPtr(v));auto*overlay=PyDict_Check(overlays.get())?PyDict_GetItem(overlays.get(),address.get()):nullptr;
    if(!overlay||!PyDict_Check(overlay))throw p::Error("Native client pose overlay was not installed by attach");replace(e.overlay,p::retain(overlay));
    auto params=p::value(e.record.get(),"track_params");if(params.get()==Py_None){
        // Exact descriptor/crew extraction is an installation boundary. The
        // resulting three scalar belt inputs stay native thereafter.
        p::Ref args(PyTuple_Pack(2,e.record.get(),v));replace(params,p::call(battle,"_bot_track_params",args.get()));ensureOpen(o);}
    replace(e.track_params,p::retain(params.get()));e.track_center=p::numeric(params.get(),"trackCenter");e.speed_cap=p::numeric(params.get(),"speedFwd")*.995;e.around_center=p::truth(params.get(),"rotationIsAroundCenter",true);
    ensureOpen(o);e.ready=e.managed=true;e.data->ready=true;++o.bindings_revision;
}
void mirrorCursor(Entry&e){
    if(e.sync.get()==Py_None)return;
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
        if(fresh)item=std::make_unique<Entry>(name,p::string(p::value(record,"kind").get()),record,r);auto&e=*item;e.data->key=name;e.data->kind=e.kind;e.data->id=p::integral(record,"id");e.data->has_current=e.data->has_target=true;
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
// A rejected/stable Siege request can acknowledge without any stock state
// changing. Do not depend on an unrelated reload/cooldown materialization to
// release its legacy control mirror; the native driver reads the same receipt.
void acknowledgeLocalSiege(Owner&o,PyObject*battle){
    auto pending=field(battle,"_local_siege_pending");if(pending.get()==Py_None)return;
    if(!PyTuple_Check(pending.get())||PyTuple_GET_SIZE(pending.get())!=2)throw p::Error("Native local Siege request mirror is invalid");
    const auto*a=o.state->find("player:"+std::to_string(o.state->local_player_id));if(!a||!a->local||a->siege_state==1||a->siege_state==3)return;
    const auto&sequence=a->state.get("input_seq").resolved();auto*requested=PyTuple_GET_ITEM(pending.get(),1);
    if(requested!=Py_None&&(sequence.kind!=cs::Value::Kind::Integer||sequence.integer<p::wide(requested)))return;
    assign(battle,"_local_siege_pending",p::retain(Py_None));
    auto report=field(battle,"_report_local_siege_edge");if(report.get()!=Py_None){auto stage=p::text("ack"),mode=p::integer(a->siege_state),left=p::integer(std::int64_t(a->state.get("siege_time_left_ms").numeric())),seq=p::Ref(cs::write(sequence));p::Ref args(PyTuple_Pack(4,stage.get(),mode.get(),left.get(),seq.get()));p::Ref result(PyObject_CallObject(report.get(),args.get()));ensureOpen(o);}
}
// Lifecycle lookup is cold. Accepted poses and combat values are never read
// back from the Python mirror. Asynchronous creates are retried until ready.
void refreshBindings(Owner&o,PyObject*battle,bool cold=true){
    ensureOpen(o);if(cold)acknowledgeLocalSiege(o,battle);if(!cold){bool pending=false;for(const auto&row:o.state->actors)if(!row.second->local&&!row.second->ready&&!row.second->missing&&!row.second->tombstone){pending=true;break;}if(!pending)return;}auto records=field(battle,"_records");std::set<std::string> seen;
    for(const auto&row:o.state->actors){ensureOpen(o);auto data=row.second;if(data->missing||data->tombstone)continue;seen.insert(row.first);auto&item=o.entries[row.first];
        if(!cold&&item&&item->ready)continue;
        auto name=p::text(row.first);auto*record=PyDict_Check(records.get())?PyDict_GetItem(records.get(),name.get()):nullptr;if(!record||!PyDict_Check(record))continue;
        o.pending_creates.erase(row.first);
        if(!item||item->data!=data||item->record.get()!=record){item=std::make_unique<Entry>(row.first,data->kind,Py_None,record,data);++o.bindings_revision;}
        auto&e=*item;try{
            data->tombstone=p::truth(record,"tombstone");data->native_remote=p::truth(record,"native_remote");data->engine_id=p::integral(record,"engine_id");data->visible=p::truth(record,"spot_visible",true);
            if(!e.ready||cold){
                if(e.ready&&!engineOwns(e.engine_id,e.entity.get())){e.ready=false;data->ready=false;}
                if(!e.ready&&!data->local)attach(o,e,battle);else if(data->local)data->ready=p::truth(record,"ready");
                else {auto descriptor=field(e.entity.get(),"typeDescriptor"),gun=field(descriptor.get(),"gun");if(descriptor.get()!=e.descriptor.get()||gun.get()!=e.gun.get())attach(o,e,battle);}
                if(e.ready){auto draw=field(e.entity.get(),"_offlineNativeDrawVisible");if(draw.get()!=Py_None)data->visible=data->visible&&p::flag(draw.get());data->detached=p::flag(field(e.entity.get(),"isTurretDetached").get());}
            }
        }catch(const std::exception&error){ensureOpen(o);PyErr_Clear();e.ready=data->ready=false;o.ingress_errors.emplace_back(row.first,error.what());}
    }
    for(auto i=o.entries.begin();i!=o.entries.end();)if(!seen.count(i->first))i=o.entries.erase(i);else ++i;
}
Pose blend(const Pose&a,const Pose&b,double progress){Pose out=a;for(unsigned i=0;i<8;++i)out[i]+=(i>=3&&i<=6?wrap(b[i]-a[i]):b[i]-a[i])*progress;return out;}
bool interpolate(Entry&e,double now,double render_delta,double alpha,p::Ref&presentation_bracket,bool&snapped){
    if(e.dead){if(e.settled&&e.current==e.target)return false;double distance=0.,angle=0.;for(unsigned i=0;i<3;++i)distance+=std::pow(e.target[i]-e.current[i],2.);for(unsigned i=3;i<8;++i)angle=std::max(angle,std::abs(i<7?wrap(e.target[i]-e.current[i]):e.target[i]-e.current[i]));e.settled=distance<=.002*.002&&angle<=.001;e.current=e.settled?e.target:blend(e.current,e.target,alpha);snapped=e.settled;if(e.sync.get()!=Py_None)assign(e.sync.get(),"wreck_settled",p::boolean(e.settled));return true;}
    Pose desired=e.target;
    if(e.timed&&e.samples.size()>=2){
        if(render_delta>=.1){e.resume=true;e.resume_sample=e.sample_time;e.data->has_resume_sample=true;e.resume_dirty=e.sample_dirty=true;}e.data->render_delta=render_delta;if(e.sync.get()!=Py_None)put(e.sync.get(),"consumer_render_delta",render_delta);
        const double authority_now=e.anchor_time+std::max(0.,now-e.anchor_local)*1000000.;const double previous=e.cursor.value_or(e.samples.front().time);
        e.delay=std::max(e.delay,authority_now-render_delta*1000000.-previous);double rate=std::min(1.,std::pow((e.ideal-e.delay)/1000000.,2.));
        if(e.ideal<e.delay){rate=std::max(rate,e.delay>120000.?.10:.005);if(e.delay>120000.&&e.samples.back().time-previous>e.ideal){rate=std::max(rate,std::min(.10,(e.delay-e.ideal)/1000000.));if(e.resume&&render_delta<.1)rate=std::max(rate,.5);}}
        if(e.resume&&render_delta<.1)rate=std::min(rate,.5);
        const double step=render_delta*rate*1000000.;e.delay=e.ideal>e.delay?std::min(e.ideal,e.delay+step):std::max(e.ideal,e.delay-step);
        if(e.has_observed&&render_delta<.1&&e.sample_time>e.resume_sample&&std::max(e.delay,e.ideal)<=std::max(60000.,e.observed)+15000.){e.resume=false;e.resume_dirty=true;}
        e.cursor=std::min({e.samples.back().time,previous+render_delta*(1.+rate)*1000000.,std::max(authority_now-e.delay,previous)});
        if(e.kind=="bot"){const bool held=*e.cursor<=previous;bool moving=false;for(unsigned i=0;i<3;++i)moving=moving||std::abs(e.samples.back().value[i]-e.samples.front().value[i])>.01;++e.data->probe_frames;e.data->probe_held+=held;e.data->probe_moving_held+=held&&moving;e.data->probe_max_gap_ms=std::max(e.data->probe_max_gap_ms,render_delta*1000.);}
        if(e.kind=="bot"&&e.sync.get()!=Py_None){
            auto probe=p::value(e.sync.get(),"motion_probe");if(probe.get()==Py_None){replace(probe,p::dict());p::set(e.sync.get(),"motion_probe",probe);}const bool held=*e.cursor<=previous;bool moving=false;for(unsigned i=0;i<3;++i)moving=moving||std::abs(e.samples.back().value[i]-e.samples.front().value[i])>.01;
            put(probe.get(),"frames",p::numeric(probe.get(),"frames")+1.);put(probe.get(),"held_frames",p::numeric(probe.get(),"held_frames")+held);put(probe.get(),"moving_held_frames",p::numeric(probe.get(),"moving_held_frames")+(held&&moving));put(probe.get(),"max_render_gap_ms",std::max(p::numeric(probe.get(),"max_render_gap_ms"),render_delta*1000.));}
        std::size_t consumed=0;while(e.samples.size()-consumed>2&&*e.cursor>=e.samples[consumed+1].time)++consumed;
        if(consumed){e.samples.erase(e.samples.begin(),e.samples.begin()+consumed);if(e.sync.get()!=Py_None){auto samples=p::value(e.sync.get(),"timed_samples");if(PyList_SetSlice(samples.get(),0,Py_ssize_t(consumed),nullptr)<0)throw p::Error("Native client history prune failed");}}
        const Sample*a=&e.samples.front(),*b=&e.samples.back();for(std::size_t i=1;i<e.samples.size();++i)if(*e.cursor<=e.samples[i].time){a=&e.samples[i-1];b=&e.samples[i];break;}
        replace(presentation_bracket,bracket(*a,*b));desired=blend(a->value,b->value,clamp((*e.cursor-a->time)/std::max(1.,b->time-a->time),0.,1.));
    }else if(e.timed){const Sample endpoint{e.sample_time,e.revision,e.target};replace(presentation_bracket,bracket(endpoint,endpoint));if(!e.cursor)e.cursor=e.sample_time;}
    else {const double predict=std::min(std::max(0.,now-e.target_time),.05);for(unsigned i=0;i<3;++i)desired[i]+=e.velocity[i]*predict;}
    const Pose&snap=e.timed?(e.teleport?e.target:desired):e.target;double distance=0.;for(unsigned i=0;i<3;++i)distance+=std::pow(snap[i]-e.current[i],2.);
    snapped=distance>625.;if(snapped){e.current=snap;if(e.timed){Sample endpoint{e.sample_time,e.revision,e.target};e.samples={endpoint};e.cursor=e.sample_time;e.ideal=e.delay=0.;e.teleport=false;e.data->has_ideal=e.data->has_delay=false;e.data->warmup_intervals=0;e.data->warmup_active=false;if(e.sync.get()!=Py_None){auto samples=p::list(),row=p::dict();p::set(row.get(),"time_us",p::number(e.sample_time));p::set(row.get(),"revision",e.revision?p::integer(*e.revision):p::retain(Py_None));p::set(row.get(),"pose",pack(e.target));p::append(samples.get(),row);p::set(e.sync.get(),"timed_samples",samples);for(const char*n:{"interpolation_delay_us","presentation_delay_us"})p::set(e.sync.get(),n,p::retain(Py_None));put(e.sync.get(),"timed_warmup_intervals",0.);p::set(e.sync.get(),"timed_warmup_active",p::boolean(false));p::set(e.sync.get(),"timed_teleport",p::boolean(false));}replace(presentation_bracket,bracket(endpoint,endpoint));}}
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
    if(filter.get()==appearance_filter.get()&&movement.get()!=Py_None&&PyCFunction_Check(setter.get())){auto exact_l=p::number(left),exact_r=p::number(right);p::Ref args(PyTuple_Pack(4,exact_l.get(),lc.get(),exact_r.get(),rc.get()));p::Ref result(PyObject_CallObject(setter.get(),args.get()));ensureOpen(o);updated=true;}
    if(!hidden){auto old_mode=field(e.motion.get(),"track_mode");int same=PyObject_RichCompareBool(mode.get(),old_mode.get(),Py_EQ);if(same<0)throw p::Error("Native client engine cache invalid");if(!same){stock_native_client_drive::applyAppearanceTrackMode(appearance.get(),{power,e.alive?flags:0});ensureOpen(o);assign(e.motion.get(),"track_mode",mode);}}
    auto exact_l=p::number(left),exact_r=p::number(right);stock_native_client_drive::applyTrackScroll(appearance.get(),left,right);ensureOpen(o);if(updated)assign(e.motion.get(),"_track_feed",feed);
    p::Ref written(PyTuple_Pack(2,exact_l.get(),exact_r.get()));assign(e.motion.get(),"track_written",written);assign(e.motion.get(),"track_outcome",p::text(hidden?"hidden and settling":updated?"engine and python":"python only"));auto capabilities=field(e.motion.get(),"presentation_capabilities");if(PyDict_Check(capabilities.get()))p::set(capabilities.get(),"engine_owned_track_motion",p::boolean(updated));
}
p::Ref event(Entry&e,bool snapped,const p::Ref&bracket){auto out=p::dict();p::set(out.get(),"type",p::text("update"));p::set(out.get(),"entity",p::text(e.key));p::set(out.get(),"kind",p::text(e.kind));p::set(out.get(),"id",p::integer(e.data->id));p::set(out.get(),"pose",pack(e.current));for(const char*n:{"remote","interpolated"})p::set(out.get(),n,p::boolean(true));p::set(out.get(),"snap",p::boolean(snapped));p::set(out.get(),"presentation_time_us",e.timed&&!e.dead&&e.cursor?p::integer(std::int64_t(std::round(*e.cursor))):p::retain(Py_None));if(!e.dead)p::set(out.get(),"presentation_bracket",bracket);return out;}
PyObject* fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyObject* create(PyObject*,PyObject*args){if(!PyArg_ParseTuple(args,""))return nullptr;try{return PyCapsule_New(new Owner(),capsule_name,destroy);}catch(const std::exception&e){return fail(e);}}
PyObject* ingest(PyObject*,PyObject*args){PyObject*c,*sync,*battle;if(!PyArg_ParseTuple(args,"OOO",&c,&sync,&battle))return nullptr;try{auto&o=owner(c);syncIngress(o,sync,battle);auto errors=p::list();for(const auto&error:o.ingress_errors){auto row=p::dict();p::set(row.get(),"entity",p::text(error.first));p::set(row.get(),"error",p::text(error.second));p::append(errors.get(),row);}return errors.release();}catch(const Retired&){PyErr_Clear();return PyList_New(0);}catch(const std::exception&e){return fail(e);}}
PyObject* advance(PyObject*,PyObject*args){PyObject*c,*sync;double now;if(!PyArg_ParseTuple(args,"OOd",&c,&sync,&now))return nullptr;try{
    if(!std::isfinite(now))throw p::Error("Native client render time invalid");auto&o=owner(c);const double render_delta=o.last_advance?std::max(0.,now-*o.last_advance):.016;o.last_advance=now;if(!o.direct)put(sync,"_last_advance",now);o.state->last_advance=now;const double alpha=1.-std::exp(-20.*clamp(render_delta,.001,.1));auto pending=p::list(),errors=p::list();
    for(auto&item:o.entries){ensureOpen(o);if(!item.second)continue;auto&e=*item.second;if(!e.data->has_current||!e.data->has_target||e.data->local)continue;try{auto br=p::retain(Py_None);bool snapped=false;if(!interpolate(e,now,render_delta,alpha,br,snapped))continue;if(br.get()!=Py_None){const auto values=p::numbers<4>(br.get());std::array<std::int64_t,4> ids;for(unsigned i=0;i<4;++i)ids[i]=std::int64_t(values[i]);e.data->presentation_bracket=ids;}else e.data->presentation_bracket.reset();auto current=pack(e.current);if(!o.direct){p::set(e.sync.get(),"current",current);if(!e.dead)p::set(e.sync.get(),"presentation_bracket",br);}
        if(!e.ready){p::append(pending.get(),event(e,snapped,br));continue;}
        if(!engineOwns(e.engine_id,e.entity.get())){e.ready=e.data->ready=false;continue;}
        p::set(e.record.get(),"presented_pose",current);p::set(e.record.get(),"presentation_time_us",e.timed&&!e.dead&&e.cursor?p::integer(std::int64_t(std::round(*e.cursor))):p::retain(Py_None));p::set(e.record.get(),"presentation_bracket",br);presentPose(e,now);ensureOpen(o);tracks(o,e,now);ensureOpen(o);
    }catch(const std::exception&error){ensureOpen(o);e.has_written=false;e.has_aim=false;PyErr_Clear();auto row=p::dict();p::set(row.get(),"entity",p::text(e.key));p::set(row.get(),"error",p::text(error.what()));p::append(errors.get(),row);}}
    return Py_BuildValue("NN",pending.release(),errors.release());
}catch(const Retired&){PyErr_Clear();return Py_BuildValue("NN",PyList_New(0),PyList_New(0));}catch(const std::exception&e){return fail(e);}}
PyObject* stateHandle(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;try{return cs::capsule(owner(c).state);}catch(const std::exception&e){return fail(e);}}
PyObject* refreshImpl(PyObject*args,bool cold){PyObject*c,*battle;if(!PyArg_ParseTuple(args,"OO",&c,&battle))return nullptr;try{auto&o=owner(c);o.ingress_errors.clear();const auto before=o.bindings_revision;refreshBindings(o,battle,cold);auto errors=p::list();for(const auto&error:o.ingress_errors){auto row=p::dict();p::set(row.get(),"entity",p::text(error.first));p::set(row.get(),"error",p::text(error.second));p::append(errors.get(),row);}if(!cold)return Py_BuildValue("NO",errors.release(),o.bindings_revision!=before?Py_True:Py_False);return errors.release();}catch(const Retired&){PyErr_Clear();return cold?PyList_New(0):Py_BuildValue("NO",PyList_New(0),Py_False);}catch(const std::exception&e){return fail(e);}}
PyObject* refresh(PyObject*,PyObject*args){return refreshImpl(args,true);}
PyObject* refreshPending(PyObject*,PyObject*args){return refreshImpl(args,false);}
PyObject* flushPending(PyObject*,PyObject*args){
    PyObject*c,*battle;double now;if(!PyArg_ParseTuple(args,"OOd",&c,&battle,&now))return nullptr;
    try{auto&o=owner(c);ensureOpen(o);auto records=field(battle,"_records");if(!PyDict_Check(records.get()))throw p::Error("Native replica lifecycle records are unavailable");
        // Keep pending identities alive across synchronous entity callbacks.
        // Ready rows never materialize a list or enter a Python callback.
        std::vector<std::pair<p::Ref,p::Ref>> pending;Py_ssize_t position=0;PyObject*key,*record;
        while(PyDict_Next(records.get(),&position,&key,&record)){
            if(!PyDict_Check(record))throw p::Error("Native replica lifecycle record is invalid");
            if(p::truth(record,"tombstone")||!p::truth(record,"ready"))pending.emplace_back(p::retain(key),p::retain(record));
        }
        for(const auto&row:pending){ensureOpen(o);auto current=field(battle,"_records");if(!PyDict_Check(current.get())||PyDict_GetItem(current.get(),row.first.get())!=row.second.get())continue;
            auto args=p::Ref(PyTuple_Pack(1,row.second.get()));
            if(p::truth(row.second.get(),"tombstone")){p::call(battle,"_flush_tombstone",args.get());ensureOpen(o);continue;}
            if(p::truth(row.second.get(),"ready"))continue;
            auto materialized=p::call(battle,"_materialize_record",args.get());ensureOpen(o);if(p::flag(materialized.get()))continue;
            auto after=field(battle,"_records");if(!PyDict_Check(after.get())||PyDict_GetItem(after.get(),row.first.get())!=row.second.get())continue;
            auto deadline=field(row.second.get(),"ready_deadline");if(deadline.get()!=Py_None&&now>=p::scalar(deadline.get()))throw p::Error("Vehicle "+std::to_string(p::integral(row.second.get(),"engine_id"))+" did not enter world before timeout");
        }
        Py_RETURN_TRUE;
    }catch(const Retired&){PyErr_Clear();Py_RETURN_TRUE;}catch(const std::exception&e){return fail(e);}
}
PyObject* receiveValue(PyObject*c,PyObject*sync,const cs::Value&data,double now,bool is_manifest){try{
    auto&o=owner(c);const auto core_begin=std::chrono::steady_clock::now();o.direct=true;o.state->local_player_id=p::integral(sync,"local_player_id");const auto epoch=o.state->epoch;++o.received;if(!is_manifest)++o.snapshot_calls;
    auto events=is_manifest?cs::manifest(*o.state,data):cs::snapshot(*o.state,data,now);
    const auto events_begin=std::chrono::steady_clock::now();if(!is_manifest)o.snapshot_state_seconds+=std::chrono::duration<double>(events_begin-core_begin).count();
    if(epoch!=o.state->epoch){o.entries.clear();o.last_advance.reset();o.static_versions.clear();o.pending_creates.clear();}
    assign(sync,"round_id",p::Ref(cs::write(o.state->round)));auto out=p::list();
    for(const auto&pending:o.pending_creates){const auto*a=o.state->find(pending.first);if(a&&!a->missing){auto retry=pending.second;if(a->state.resolved().kind==cs::Value::Kind::Object)retry.object["state"]=a->state;p::append(out.get(),p::Ref(cs::write(retry)));}}
    o.lifecycle_events+=events.size();for(const auto&event:events){if(event.payload.get("type").text=="create")o.pending_creates[event.payload.get("entity").text]=event.payload;p::append(out.get(),p::Ref(cs::write(event.payload)));}
    events.clear();if(!is_manifest)o.snapshot_events_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-events_begin).count();return out.release();
}catch(const Retired&){PyErr_Clear();return PyList_New(0);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}

PyObject* receive(PyObject*args,bool is_manifest){PyObject*c,*sync,*message;double now;if(!PyArg_ParseTuple(args,"OOOd",&c,&sync,&message,&now))return nullptr;try{
    auto&o=owner(c);const auto parse_begin=std::chrono::steady_clock::now();PyObject*result=nullptr;auto release_begin=parse_begin;
    {auto data=readStateMessage(o,message,is_manifest);const auto parsed=std::chrono::steady_clock::now();if(!is_manifest){++o.snapshot_parse_calls;o.snapshot_parse_seconds+=std::chrono::duration<double>(parsed-parse_begin).count();}result=receiveValue(c,sync,data,now,is_manifest);release_begin=std::chrono::steady_clock::now();}
    if(!is_manifest)o.snapshot_release_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-release_begin).count();return result;
}catch(const std::exception&e){return fail(e);}}

PyObject* snapshot(PyObject*,PyObject*args){return receive(args,false);}
PyObject* manifest(PyObject*,PyObject*args){return receive(args,true);}
PyObject* probes(PyObject*,PyObject*args){PyObject*c;double now;if(!PyArg_ParseTuple(args,"Od",&c,&now))return nullptr;try{auto&o=owner(c);auto out=p::dict();for(auto&entry:o.state->actors){auto&a=*entry.second;if(!a.probe_frames)continue;auto row=p::dict();p::set(row.get(),"frames",p::integer(a.probe_frames));p::set(row.get(),"held_frames",p::integer(a.probe_held));p::set(row.get(),"moving_held_frames",p::integer(a.probe_moving_held));p::set(row.get(),"max_render_gap_ms",p::number(a.probe_max_gap_ms));p::set(row.get(),"source_time_us",a.previous_sample?p::number(*a.previous_sample):p::retain(Py_None));p::set(row.get(),"source_revision",a.sample_revision?p::integer(*a.sample_revision):p::retain(Py_None));p::set(row.get(),"presentation_time_us",a.cursor?p::number(*a.cursor):p::retain(Py_None));p::set(row.get(),"source_age_ms",p::number(std::max(0.,now-a.target_time)*1000.));p::set(row.get(),"ideal_delay_us",a.has_ideal?p::number(a.ideal):p::retain(Py_None));p::set(row.get(),"actual_delay_us",a.has_delay?p::number(a.delay):p::retain(Py_None));p::set(row.get(),"source_pose",pack(a.target));p::set(row.get(),"presented_pose",pack(a.current));p::set(out.get(),entry.first.c_str(),row);a.probe_frames=a.probe_held=a.probe_moving_held=0;a.probe_max_gap_ms=0.;}return out.release();}catch(const std::exception&e){return fail(e);}}
PyObject* statistics(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;try{const auto&o=owner(c);auto out=p::dict();p::set(out.get(),"received",p::integer(o.received));p::set(out.get(),"static_reads",p::integer(o.static_reads));p::set(out.get(),"static_reuses",p::integer(o.static_reuses));p::set(out.get(),"static_versions",p::integer(o.static_versions.size()));p::set(out.get(),"lifecycle_events",p::integer(o.lifecycle_events));p::set(out.get(),"actors",p::integer(o.state->actors.size()));p::set(out.get(),"pending_creates",p::integer(o.pending_creates.size()));p::set(out.get(),"snapshot_calls",p::integer(o.snapshot_calls));p::set(out.get(),"snapshot_parse_calls",p::integer(o.snapshot_parse_calls));p::set(out.get(),"snapshot_parse_seconds",p::number(o.snapshot_parse_seconds));p::set(out.get(),"snapshot_state_seconds",p::number(o.snapshot_state_seconds));p::set(out.get(),"snapshot_events_seconds",p::number(o.snapshot_events_seconds));p::set(out.get(),"snapshot_release_seconds",p::number(o.snapshot_release_seconds));return out.release();}catch(const std::exception&e){return fail(e);}}
PyObject* inspect(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;try{auto&o=owner(c);auto out=p::dict();for(const auto&row:o.state->actors){const auto&a=*row.second;auto value=p::dict();p::set(value.get(),"current",a.has_current?pack(a.current):p::retain(Py_None));p::set(value.get(),"target",a.has_target?pack(a.target):p::retain(Py_None));p::set(value.get(),"state",p::Ref(cs::write(a.state)));p::set(value.get(),"presentation_time_us",a.cursor?p::number(*a.cursor):p::retain(Py_None));p::set(value.get(),"presentation_delay_us",a.has_delay?p::number(a.delay):p::retain(Py_None));p::set(value.get(),"consumer_resume_pending",p::boolean(a.resume));p::set(value.get(),"consumer_resume_sample_us",a.has_resume_sample?p::number(a.resume_sample):p::retain(Py_None));p::set(value.get(),"wreck_settled",p::boolean(a.settled));p::set(value.get(),"presentation_bracket",a.presentation_bracket?p::integers(*a.presentation_bracket):p::retain(Py_None));p::set(value.get(),"receipt_count",p::integer(a.receipts.size()));p::set(out.get(),row.first.c_str(),value);}return out.release();}catch(const std::exception&e){return fail(e);}}
PyObject* manages(PyObject*,PyObject*args){PyObject*c,*record;if(!PyArg_ParseTuple(args,"OO",&c,&record))return nullptr;try{auto&o=owner(c);for(auto&item:o.entries){if(!item.second)continue;auto&e=*item.second;if(e.record.get()==record){if(e.managed){if(e.ready&&!engineOwns(e.engine_id,e.entity.get()))e.ready=e.data->ready=false;Py_RETURN_TRUE;}break;}}Py_RETURN_FALSE;}catch(const std::exception&e){return fail(e);}}
// Stock presentation may synchronously call Battle._cleanup while advance
// holds an Entry reference. Retire now; the active CAPI argument keeps the
// capsule alive and its storage is released only by the capsule destructor.
PyObject* close(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(!o)return nullptr;o->closed=true;o->state->retired=true;Py_RETURN_NONE;}
}
