#if !defined(WOT_CLIENT_SELF_HOST_BUILD)
#include "pch.hpp"
#include "stock_vehicle_visuals.hpp"
#include "stock_wot_module.hpp"
#else
#include <Python.h>
#include <functional>
#include <cstdint>
namespace BW {using StockFloatLinkReader=std::function<bool(float&)>;PyObject* makeStockFloatLink(StockFloatLinkReader);
struct StockAuxVehiclePhysicsData {float yaw,pitch,roll,leftScroll,rightScroll,normalisedRPM;};
StockAuxVehiclePhysicsData unpackStockAuxVehiclePhysicsData(std::uint64_t);}
#endif
#include "stock_native_client_self_presentation.hpp"
#include "stock_native_client_drive_presentation.hpp"
#include "stock_native_client_drive_presentation_python.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_bot_turret_obstacles.hpp"
#include "stock_native_bot_python.hpp"
#include <memory>

namespace stock_native_client_drive {
namespace {
namespace p=stock_native_bot_python;
namespace state=stock_native_client_state;
namespace query=stock_native_client_query;
namespace world=stock_native_world;
const char* capsuleName="wot.native.client.self.presentation";
p::Ref field(PyObject*o,const char*n) {
    if(!o||o==Py_None)return p::retain(Py_None);
    if(PyDict_Check(o))return p::value(o,n);
    auto name=p::text(n);auto*v=p::directAttribute(o,name.get());
    if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}
    return p::Ref(v);
}
void put(PyObject*o,const char*n,const p::Ref&v) {
    if(PyDict_Check(o)){p::set(o,n,v);return;}
    auto name=p::text(n);
    if(PyObject_GenericSetAttr(o,name.get(),v.get())<0)throw p::Error("Native self presentation write failed");
}
double number(PyObject*o,const char*n,double fallback=0.) {
    auto v=field(o,n);return v.get()==Py_None?fallback:p::scalar(v.get());
}
bool truth(PyObject*o,const char*n,bool fallback=false) {
    auto v=field(o,n);return v.get()==Py_None?fallback:p::flag(v.get());
}
Point xyz(PyObject*o) {
    Point v{};if(PyTuple_Check(o)||PyList_Check(o))return p::numbers<3>(o);
    const char*names[]={"x","y","z"};for(unsigned i=0;i<3;++i)v[i]=number(o,names[i]);return v;
}
p::Ref invoke(PyObject*o,const char*n,PyObject*args) {
    auto fn=field(o,n);
    if(!PyCFunction_Check(fn.get()))throw p::Error("Self presentation requires the reviewed native method");
    return p::Ref(PyObject_CallObject(fn.get(),args));
}
p::Ref invoke(PyObject*o,const char*n) {p::Ref args(PyTuple_New(0));return invoke(o,n,args.get());}
p::Ref construct(PyObject*math,const char*n,PyObject*args) {
    auto type=field(math,n);
    bool native=PyCFunction_Check(type.get());
#if !defined(WOT_CLIENT_SELF_HOST_BUILD)
    native=native||type.get()==reinterpret_cast<PyObject*>(&BW::PyMatrix::s_type_)||
        type.get()==reinterpret_cast<PyObject*>(&BW::PyVector<BW::Vector3>::s_type_);
#endif
    if(!native)throw p::Error("Self presentation requires native Math providers");
    return p::Ref(PyObject_CallObject(type.get(),args));
}
p::Ref vector(PyObject*math,const Point&value) {auto args=p::tupleNumbers(value);return construct(math,"Vector3",args.get());}
p::Ref matrix(PyObject*math,PyObject*provider=nullptr) {
    p::Ref args(provider?PyTuple_Pack(1,provider):PyTuple_New(0));return construct(math,"Matrix",args.get());
}
void rotate(PyObject*matrix,const Point&ypr) {auto value=p::tupleNumbers(ypr);p::Ref args(PyTuple_Pack(1,value.get()));invoke(matrix,"setRotateYPR",args.get());}
void multiply(PyObject*matrix,PyObject*before) {p::Ref args(PyTuple_Pack(1,before));invoke(matrix,"preMultiply",args.get());}
void copyMatrix(PyObject*target,PyObject*provider) {p::Ref args(PyTuple_Pack(1,provider));invoke(target,"set",args.get());}
p::Ref poseProvider(PyObject*battle,const char*name) {
    auto value=field(battle,name);return value.get()==Py_None?field(battle,"_local_matrix"):std::move(value);
}
struct Owner {
    std::shared_ptr<state::State> shared;
    std::uint64_t epoch;
    p::Ref entity,math,bigworld;
    CameraHistory camera;
    Point velocity{};
    double speed=0.,turn=0.,move_seconds=0.,rotate_seconds=0.,track_min=0.,track_max=0.;
    bool retired=false;
    std::unique_ptr<p::Ref> engine,swinging,speed_link,turn_link,strafe_link,strafe_source,descriptor,rpm_key;
    std::shared_ptr<query::ProjectileTurrets> turrets;
    HydraulicPitch pitch;
    bool pitch_valid=false;
    struct Probe {std::int64_t event=0;double elapsed=0.;std::size_t next=0;p::Ref animator;explicit Probe(PyObject*a):animator(p::retain(a)) {}};
    std::unique_ptr<Probe> probe;
    std::vector<double> probe_times;
    std::int64_t report_limit=0;
    double rpm_seconds=0.;
    Owner(PyObject*b,PyObject*e):shared(state::fromBattle(b)),epoch(shared->epoch),entity(p::retain(e)),
        math(field(field(b,"_runtime").get(),"math")),bigworld(field(field(b,"_runtime").get(),"bigworld")),
        camera(number(field(field(b,"_runtime").get(),"constants").get(),"SERVER_TICK_LENGTH")) {
        const auto*actor=shared->find("player:"+std::to_string(shared->local_player_id));
        if(!actor||!actor->local||!actor->has_current)throw p::Error("Native self presentation has no admitted player");
        auto history=field(b,"_local_motion_history");
        if(history.get()!=Py_None&&PySequence_Size(history.get())>0){auto row=p::item(history.get(),0),position=p::item(row.get(),1);camera.reset(xyz(position.get()));}
        else camera.reset();
        auto previous=field(b,"_local_camera_velocity");if(previous.get()!=Py_None)velocity=xyz(previous.get());
        p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime"));
        move_seconds=number(module.get(),"LOCAL_ACCEL_SWING_MOVE_SECONDS");rotate_seconds=number(module.get(),"LOCAL_ACCEL_SWING_ROTATE_SECONDS");
        report_limit=std::int64_t(number(module.get(),"LOCAL_ACCEL_SWING_REPORT_LIMIT"));
        rpm_seconds=number(module.get(),"RPM_PRESENTATION_SECONDS");
        auto times=field(module.get(),"LOCAL_ACCEL_SWING_PROBE_TIMES");
        if(times.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(times.get());++i)probe_times.push_back(p::scalar(p::item(times.get(),i).get()));
        auto limits=field(module.get(),"TRACK_SCROLL_LIMITS");auto bounds=p::numbers<2>(limits.get());track_min=bounds[0];track_max=bounds[1];
        turrets=query::projectileTurrets(b);
    }
    bool current()const{return !retired&&!shared->retired&&shared->epoch==epoch;}
};
using Handle=std::shared_ptr<Owner>;
void destroy(PyObject*c) {auto*h=static_cast<Handle*>(PyCapsule_GetPointer(c,capsuleName));if(h){if(*h)(*h)->retired=true;delete h;}else PyErr_Clear();}
Handle acquire(PyObject*b,PyObject*e) {
    auto raw=field(b,"_native_client_self_presentation");
    if(raw.get()!=Py_None) {
        auto*h=static_cast<Handle*>(PyCapsule_GetPointer(raw.get(),capsuleName));if(!h)throw p::Error("Invalid native self presentation handle");
        if(*h&&(*h)->current()&&(*h)->entity.get()==e)return *h;
        if(*h)(*h)->retired=true;
    }
    auto owner=std::make_shared<Owner>(b,e);std::unique_ptr<Handle> handle(new Handle(owner));
    p::Ref capsule(PyCapsule_New(handle.get(),capsuleName,destroy));handle.release();put(b,"_native_client_self_presentation",capsule);return owner;
}
void optional(Owner&owner,PyObject*b,const char*feature,const std::function<void()>&work) {
    if(!owner.current())return;
    auto disabled=field(b,"_disabled_optional_features");auto name=p::text(feature);
    if(disabled.get()!=Py_None&&PySet_Contains(disabled.get(),name.get())==1)return;
    try {work();}
    catch(const std::exception&e) {
        PyErr_Clear();if(!owner.current())return;
        if(disabled.get()==Py_None){p::Ref set(PySet_New(nullptr));put(b,"_disabled_optional_features",set);if(PySet_Add(set.get(),name.get())<0)throw p::Error("Native optional feature state failed");}
        else if(PySet_Add(disabled.get(),name.get())<0)throw p::Error("Native optional feature state failed");
        PySys_WriteStdout("[Offline LAN 0.9.22] optional %s disabled for this round: %s\n",feature,e.what());
    }
}
void readPitch(Owner&o,PyObject*d) {
    if(o.descriptor&&o.descriptor->get()==d)return;
    o.descriptor.reset(new p::Ref(p::retain(d)));o.pitch_valid=false;
    try {
        auto type=field(d,"type");auto params=[&]{auto value=field(type.get(),"hullAimingParams");return value.get()==Py_None?field(d,"hullAimingParams"):std::move(value);}();
        auto pitch=field(params.get(),"pitch"),angles=field(pitch.get(),"wheelsCorrectionAngles");
        if(angles.get()==Py_None)return;
        HydraulicPitch value;value.minimum=p::scalar(field(angles.get(),"pitchMin").get());value.maximum=p::scalar(field(angles.get(),"pitchMax").get());
        value.speed=p::scalar(field(pitch.get(),"wheelsCorrectionSpeed").get());value.center_z=number(pitch.get(),"wheelCorrectionCenterZ");
        value.available=truth(pitch.get(),"isAvailable");value.enabled=truth(pitch.get(),"isEnabled");
        auto gun=field(d,"gun"),limits=field(gun.get(),"pitchLimits"),absolute=field(limits.get(),"absolute");
        const auto bounds=p::numbers<2>(absolute.get()==Py_None?limits.get():absolute.get());value.gun_minimum=bounds[0];value.gun_maximum=bounds[1];
        if(value.minimum>value.maximum||value.speed<0.||value.gun_minimum>value.gun_maximum)return;
        o.pitch=value;o.pitch_valid=true;
    }catch(const std::exception&){PyErr_Clear();}
}
std::array<double,2> targetAngles(Owner&o,PyObject*b,PyObject*d,const Point&point,PyObject*flat) {
    auto hull=field(d,"hull"),positions=field(hull.get(),"turretPositions"),origin=p::item(positions.get(),0);
    auto chassis=field(d,"chassis"),offset=field(chassis.get(),"hullPosition");p::Ref turretOffs(PyNumber_Add(origin.get(),offset.get()));
    auto turret=field(d,"turret"),gunOffs=field(turret.get(),"gunPosition"),shot=field(d,"shot");
    auto avatar=field(b,"_avatar"),rotator=field(avatar.get(),"gunRotator");
    auto yaw=field(rotator.get(),"_VehicleGunRotator__turretYaw"),pitch=field(rotator.get(),"_VehicleGunRotator__gunPitch");
    auto speed=field(shot.get(),"speed"),gravity=field(shot.get(),"gravity"),m=matrix(o.math.get(),flat),target=vector(o.math.get(),point);
    p::Ref args(PyTuple_Pack(9,turretOffs.get(),gunOffs.get(),m.get(),speed.get(),gravity.get(),yaw.get(),pitch.get(),target.get(),Py_True));
    auto angles=invoke(o.bigworld.get(),"wg_getShotAngles",args.get());return p::numbers<2>(angles.get());
}
world::TurretPose turretPose(Owner&o,PyObject*b,const PlayerState&s,double correction,PyObject*body) {
    world::TurretPose result;result.position=s.position;result.yaw=s.yaw;result.pitch=s.pitch;result.roll=s.roll;
    auto pose=field(b,"_local_pose_matrix"),selected=field(pose.get(),"a");
    if(body==Py_None||selected.get()!=body||!o.turrets->count())return result;
    auto base=matrix(o.math.get()),aim=matrix(o.math.get());rotate(base.get(),{s.yaw,s.pitch,s.roll});put(base.get(),"translation",vector(o.math.get(),s.position));
    rotate(aim.get(),{0.,correction,0.});put(aim.get(),"translation",vector(o.math.get(),{0.,o.pitch.center_z*std::sin(correction),o.pitch.center_z*(1.-std::cos(correction))}));
    multiply(base.get(),aim.get());auto relative=field(body,"a");auto relativeMatrix=matrix(o.math.get(),relative.get());multiply(base.get(),relativeMatrix.get());
    auto position=field(base.get(),"translation");result.hull_position=xyz(position.get());result.hull_yaw=number(base.get(),"yaw");result.hull_pitch=number(base.get(),"pitch");result.hull_roll=number(base.get(),"roll");result.separate_hull=true;return result;
}
void hydraulic(Owner&o,PyObject*b,PyObject*e,PlayerDriver&driver,double elapsed) {
    auto aim=field(b,"_local_siege_aim_matrix"),descriptor=field(e,"typeDescriptor");
    if(aim.get()==Py_None||!driver.descriptor().hydraulic)return;
    readPitch(o,descriptor.get());const auto current=number(b,"_local_siege_aim_pitch");double correction=0.;
    if(o.pitch_valid) {
        put(b,"_local_siege_aim_center_z",p::number(o.pitch.center_z));
        try {
            const bool active=number(e,"siegeState")==2.;std::optional<double> target;
            auto flat=field(b,"_local_siege_flat_body_matrix");
            if(active&&driver.controls().aim_point&&flat.get()!=Py_None)target=targetAngles(o,b,descriptor.get(),*driver.controls().aim_point,flat.get())[1];
            correction=hydraulicCorrection(o.pitch,current,elapsed,active,target,driver.controls().aim_pitch,driver.state().pitch).correction;
        }catch(const std::exception&){PyErr_Clear();correction=0.;}
    }
    if(correction!=current&&o.turrets->count()) {
        auto body=field(b,"_local_siege_body_matrix");world::TurretSweep sweep;
        sweep.actor=std::uint64_t(o.shared->local_player_id);sweep.human=true;
        sweep.start=turretPose(o,b,driver.state(),current,body.get());sweep.end=turretPose(o,b,driver.state(),correction,body.get());
        stock_native_bot_turret::MovingDescriptor moving;
        const char*names[]={"chassis","hull"};stock_native_bot_turret::Component*components[]={&moving.chassis,&moving.hull};
        for(unsigned i=0;i<2;++i){auto part=field(descriptor.get(),names[i]),tester=field(part.get(),"hitTester"),bounds=field(tester.get(),"bbox");auto low=p::item(bounds.get(),0),high=p::item(bounds.get(),1);components[i]->bounds={xyz(low.get()),xyz(high.get())};}
        auto chassis=field(descriptor.get(),"chassis"),offset=field(chassis.get(),"hullPosition");moving.hull.offset=xyz(offset.get());moving.pivot_available=true;moving.track_center=driver.descriptor().motion.track_center;moving.rotation_around_center=driver.descriptor().rotation_around_center;
        p::Ref noargs(PyTuple_New(0));auto now=invoke(o.bigworld.get(),"time",noargs.get());
        auto anchor=field(b,"_projectile_server_time_ms"),local=field(b,"_projectile_server_local_time");
        const double server=anchor.get()==Py_None||local.get()==Py_None?-1.:std::max(0.,std::trunc(p::scalar(anchor.get())+1000.*(p::scalar(now.get())-p::scalar(local.get()))));
        if(!o.turrets->clear(sweep,moving,server))correction=current;
    }
    driver.state().hydraulic_pitch=correction;put(b,"_local_siege_aim_pitch",p::number(correction));rotate(aim.get(),{0.,correction,0.});
    put(aim.get(),"translation",vector(o.math.get(),{0.,o.pitch.center_z*std::sin(correction),o.pitch.center_z*(1.-std::cos(correction))}));
}
void sniper(Owner&o,PyObject*b,PyObject*e,double previous_yaw,double yaw) {
    double delta=std::fmod(yaw-previous_yaw+3.141592653589793,6.283185307179586);if(delta<0.)delta+=6.283185307179586;delta-=3.141592653589793;
    if(std::abs(delta)<=.000001)return;
    auto runtime=field(b,"_runtime"),avatar=field(b,"_avatar"),handler=field(avatar.get(),"inputHandler");
    auto input=field(runtime.get(),"avatar_input_handler"),modes=field(input.get(),"_CTRL_MODE"),name=field(handler.get(),"_AvatarInputHandler__ctrlModeName"),expected=field(modes.get(),"SNIPER");
    if(handler.get()==Py_None||modes.get()==Py_None||PyObject_RichCompareBool(name.get(),expected.get(),Py_EQ)!=1)return;
    auto descriptor=[&]{auto value=field(b,"_local_descriptor");return value.get()==Py_None?field(e,"typeDescriptor"):std::move(value);}();auto gun=field(descriptor.get(),"gun"),limits=field(gun.get(),"turretYawLimits");
    if(gun.get()==Py_None||limits.get()!=Py_None)return;
    auto control=field(handler.get(),"_AvatarInputHandler__curCtrl");
    auto camera=field(control.get(),"_cam"),aiming=field(camera.get(),"_SniperCamera__aimingSystem");
    if(aiming.get()==Py_None)throw p::Error("Native sniper aiming owner is unavailable");
    auto provider=field(aiming.get(),"_SniperAimingSystem__vehicleMProv"),inverse=matrix(o.math.get(),provider.get());invoke(inverse.get(),"invert");
    auto rotation=matrix(o.math.get());rotate(rotation.get(),{number(aiming.get(),"_SniperAimingSystem__worldYaw"),number(aiming.get(),"_SniperAimingSystem__worldPitch"),0.});multiply(inverse.get(),rotation.get());
    double turret=number(inverse.get(),"yaw"),pitch=number(inverse.get(),"pitch");
    put(aiming.get(),"_SniperAimingSystem__idealTurretYaw",p::number(turret));put(aiming.get(),"_SniperAimingSystem__idealGunPitch",p::number(pitch));
    auto yawLimits=field(aiming.get(),"_SniperAimingSystem__yawLimits");if(yawLimits.get()!=Py_None){const auto bounds=p::numbers<2>(yawLimits.get());turret=std::min(bounds[1],std::max(bounds[0],turret));}
    auto type=field(aiming.get(),"_SniperAimingSystem__vehicleTypeDescriptor"),activeGun=field(type.get(),"gun"),combined=field(activeGun.get(),"combinedPitchLimits");
    auto low=field(combined.get(),"minPitch"),high=field(combined.get(),"maxPitch"),turretValue=p::number(turret);p::Ref args(PyTuple_Pack(3,turretValue.get(),low.get(),high.get()));
    auto range=invoke(o.bigworld.get(),"wg_calcGunPitchLimits",args.get());auto lower=p::item(range.get(),0),upper=p::item(range.get(),1);const std::array<double,2> bounds={p::scalar(lower.get()),p::scalar(upper.get())};
    auto oscillator=field(aiming.get(),"_SniperAimingSystem__returningOscillator"),deviation=field(oscillator.get(),"deviation");const double adjustment=std::max(0.,number(deviation.get(),"y"));
    const double minimum=bounds[0]-adjustment,maximum=bounds[1]+adjustment+number(aiming.get(),"_SniperAimingSystem__pitchCompensating");
    pitch=std::min(maximum,std::max(minimum,pitch));put(aiming.get(),"_SniperAimingSystem__idealTurretYaw",p::number(turret));put(aiming.get(),"_SniperAimingSystem__idealGunPitch",p::number(pitch));
}
void overlay(Owner&o,PyObject*b,const PlayerState&s,PyObject*position,PyObject*velocity,PyObject*acceleration) {
    auto runtime=field(b,"_runtime"),compat=field(runtime.get(),"compatibility"),overlays=field(compat.get(),"_vehicle_property_overlays");
    if(!truth(compat.get(),"_battle_active")||!PyDict_Check(overlays.get()))throw p::Error("Native self overlay requires an active battle");
    p::Ref key(PyLong_FromVoidPtr(o.entity.get()));auto*raw=PyDict_GetItem(overlays.get(),key.get());
    if(!raw||!PyDict_Check(raw))throw p::Error("Native self overlay was not cold installed");
    auto body=poseProvider(b,"_local_pose_matrix");
    auto steady=[&]{auto value=field(b,"_local_steady_rotation_matrix");return value.get()==Py_None?p::retain(body.get()):std::move(value);}();
    auto stabilised=[&]{auto value=field(b,"_local_stabilised_snapshot");return value.get()==Py_None?p::retain(body.get()):std::move(value);}();
    p::set(raw,"_pose_active",p::boolean(true));p::set(raw,"position",p::retain(position));p::set(raw,"yaw",p::number(s.yaw));p::set(raw,"matrix",body);p::set(raw,"steady_rotation_matrix",steady);p::set(raw,"stabilised_matrix",stabilised);
    p::set(raw,"speed",p::number(s.speed));p::set(raw,"turn_speed",p::number(s.turn_speed));p::set(raw,"velocity",p::retain(velocity));p::set(raw,"acceleration",p::retain(acceleration));
}
void bindSwing(Owner&o,PyObject*b,PyObject*appearance,PyObject*model) {
    auto swinging=field(appearance,"swingingAnimator");
    if(swinging.get()==Py_None){o.swinging.reset();put(b,"_local_swinging_animator",p::retain(Py_None));put(b,"_local_swinging_restore",p::retain(Py_None));return;}
    auto current=field(b,"_local_swinging_animator");if(current.get()==swinging.get()){o.swinging.reset(new p::Ref(p::retain(swinging.get())));return;}
    auto provider=field(model,"matrix");if(provider.get()==Py_None){o.swinging.reset();put(b,"_local_swinging_animator",p::retain(Py_None));put(b,"_local_swinging_restore",p::retain(Py_None));return;}
    auto compensation=[&]{auto value=field(b,"_local_placing_compensation");if(value.get()!=Py_None)return value;auto created=matrix(o.math.get());invoke(created.get(),"setIdentity");put(b,"_local_placing_compensation",created);return created;}();
    auto oldCompensation=field(swinging.get(),"placingCompensationMatrix"),oldWorld=field(swinging.get(),"worldMatrix");p::Ref restore(PyTuple_Pack(3,swinging.get(),oldCompensation.get(),oldWorld.get()));
    try {put(swinging.get(),"placingCompensationMatrix",compensation);put(swinging.get(),"worldMatrix",provider);}
    catch(...) {
        bool failed=false;try{put(swinging.get(),"placingCompensationMatrix",oldCompensation);}catch(...){PyErr_Clear();failed=true;}
        try{put(swinging.get(),"worldMatrix",oldWorld);}catch(...){PyErr_Clear();failed=true;}
        put(b,"_local_swinging_animator",failed?p::retain(swinging.get()):p::retain(Py_None));put(b,"_local_swinging_restore",failed?p::retain(restore.get()):p::retain(Py_None));throw;
    }
    o.swinging.reset(new p::Ref(p::retain(swinging.get())));put(b,"_local_swinging_animator",swinging);put(b,"_local_swinging_restore",restore);
}
void bindAudio(const Handle&o,PyObject*b,PyObject*appearance) {
    auto engine=field(appearance,"detailedEngineState");if(engine.get()==Py_None){o->engine.reset();put(b,"_local_engine_state",p::retain(Py_None));return;}
    if(o->engine&&o->engine->get()==engine.get())return;
    std::weak_ptr<Owner> weak=o;
    if(!o->speed_link)o->speed_link.reset(new p::Ref(BW::makeStockFloatLink([weak](float&v){auto owner=weak.lock();if(!owner||!owner->current())return false;v=float(owner->speed);return true;})));
    if(!o->turn_link)o->turn_link.reset(new p::Ref(BW::makeStockFloatLink([weak](float&v){auto owner=weak.lock();if(!owner||!owner->current())return false;v=float(owner->turn);return true;})));
    auto filter=field(appearance,"_CompoundAppearance__filter");
    o->strafe_source.reset(new p::Ref(p::retain(filter.get())));
    o->strafe_link.reset(new p::Ref(BW::makeStockFloatLink([weak](float&v){auto owner=weak.lock();if(!owner||!owner->current()||!owner->strafe_source||owner->strafe_source->get()==Py_None)return false;try{v=float(number(owner->strafe_source->get(),"strafeSpeed"));return true;}catch(...){PyErr_Clear();return false;}})));
    put(engine.get(),"vehicleSpeedLink",*o->speed_link);put(engine.get(),"rotationSpeedLink",*o->turn_link);
    auto model=field(appearance,"_CompoundAppearance__compoundModel");if(model.get()!=Py_None){auto root=field(model.get(),"root");put(engine.get(),"vehicleMatrixLink",root);}
    auto audition=field(appearance,"engineAudition");if(audition.get()!=Py_None){if(model.get()!=Py_None){p::Ref args(PyTuple_Pack(1,model.get()));invoke(audition.get(),"attachToModel",args.get());}if(filter.get()!=Py_None){p::Ref args(PyTuple_Pack(2,o->turn_link->get(),o->strafe_link->get()));invoke(audition.get(),"setSpeedInfo",args.get());}}
    p::Ref motion(PyTuple_Pack(2,o->speed_link->get(),o->turn_link->get()));put(appearance,"_offlineEngineMotion",motion);
    put(b,"_local_vehicle_speed_link",*o->speed_link);put(b,"_local_vehicle_rotation_speed_link",*o->turn_link);put(b,"_local_engine_state",engine);o->engine.reset(new p::Ref(p::retain(engine.get())));
}
std::uint64_t packAux(const std::array<double,6>&values) {
    constexpr double pi=3.1415926535897932384626433832795;
    const unsigned bits[]={15,12,13,8,8,8};const double low[]={-pi,-pi*.5,-pi,-15.,-15.,0.},high[]={pi,pi*.5,pi,30.,30.,1.2};
    std::uint64_t result=0;unsigned shift=0;
    for(unsigned i=0;i<6;++i){if(!std::isfinite(values[i]))throw p::Error("Nonfinite player auxiliary presentation");const auto mask=(std::uint64_t(1)<<bits[i])-1;const double ratio=std::max(0.,std::min(1.,(values[i]-low[i])/(high[i]-low[i])));result|=(std::uint64_t(std::round(double(mask)*ratio))&mask)<<shift;shift+=bits[i];}
    return result;
}
void rpm(Owner&o,PyObject*b,PyObject*e,PyObject*appearance,const PlayerDriver&driver,double now) {
    if(now<number(b,"_next_rpm_time"))return;
    auto descriptor=field(b,"_local_descriptor");if(descriptor.get()==Py_None)throw p::Error("Native RPM descriptor is unavailable");
    auto physics=field(descriptor.get(),"physics"),limits=field(physics.get(),"speedLimits");
    auto forward=p::item(limits.get(),0),reverse=p::item(limits.get(),1);const double range=(std::abs(p::scalar(forward.get()))+std::abs(p::scalar(reverse.get())))/3.;
    if(range<=0.)throw p::Error("Native RPM speed range is invalid");
    const auto&s=driver.state();const auto belts=trackScroll(driver.descriptor(),s,-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity());
    const double speed=std::max(std::abs(s.speed),std::max(std::abs(belts[0]),std::abs(belts[1])));double normalized=0.;unsigned gear=0;
    if(speed>=.05){const double selected=std::max(1.,std::ceil(std::floor(speed*50.)/50./range));normalized=std::max(0.,std::min(1.2,std::abs(1.+(speed-selected*range)/range)));gear=unsigned(std::min(255.,selected));}
    put(b,"_next_rpm_time",p::number(now+o.rpm_seconds));
    const auto scroll=trackScroll(driver.descriptor(),s,o.track_min,o.track_max);
    p::Ref payload(Py_BuildValue("(ddddddi)",s.yaw,s.pitch,s.roll,scroll[0],scroll[1],normalized,int(gear)));
    auto previous=field(b,"_last_presented_rpm");const int equal=PyObject_RichCompareBool(payload.get(),previous.get(),Py_EQ);if(equal<0)throw p::Error("Native RPM signature comparison failed");if(equal)return;
    const auto packed=packAux({s.yaw,s.pitch,s.roll,scroll[0],scroll[1],normalized});auto avatar=field(b,"_avatar");
    put(avatar.get(),"ownVehicleGear",p::integer(gear));p::Ref encoded(PyLong_FromUnsignedLongLong(packed));put(avatar.get(),"ownVehicleAuxPhysicsData",encoded);
    if(truth(e,"isStarted")) {
        const auto decoded=BW::unpackStockAuxVehiclePhysicsData(packed);applyTrackScroll(appearance,decoded.leftScroll,decoded.rightScroll);
        if(!o.rpm_key){p::Ref module(PyImport_ImportModule("Avatar"));auto states=field(module.get(),"VEHICLE_VIEW_STATE");o.rpm_key.reset(new p::Ref(field(states.get(),"RPM")));}
        auto provider=field(avatar.get(),"guiSessionProvider"),notify=field(provider.get(),"invalidateVehicleState");p::Ref args(Py_BuildValue("Od",o.rpm_key->get(),double(decoded.normalisedRPM)));
        // The state and quantized track producer are native. This one stock
        // event is the existing HUD/Scaleform presentation boundary.
        p::Ref published(PyObject_CallObject(notify.get(),args.get()));if(!o.current())return;
        // The accepted client-only filter's syncStabilisedYPR is a deliberate
        // no-op. Do not feed its absent retail interpolation owner here.
    }
    put(b,"_last_presented_rpm",payload);
    if(now>=number(b,"_engine_audio_report_at")) {
        put(b,"_engine_audio_report_at",p::number(now+5.));auto detailed=field(appearance,"detailedEngineState"),audition=field(appearance,"engineAudition"),mode=field(detailed.get(),"mode"),state=field(detailed.get(),"rpm");
        const auto modeText=p::Ref(PyObject_Str(mode.get())),rpmText=p::Ref(PyObject_Str(state.get()));
        PySys_WriteStdout("[Offline LAN 0.9.22] ENGINE_AUDIO local replay=%s owner=%s mode=%s rpm_input=%.3f rpm_state=%s speed=%.3f\n",truth(b,"_replay_mode")?"True":"False",audition.get()!=Py_None?"True":"False",p::string(modeText.get()).c_str(),normalized,p::string(rpmText.get()).c_str(),s.speed);
    }
}

void sampleSwing(Owner&o,PyObject*b,PyObject*appearance,double elapsed) {
    if(!o.probe)return;
    const auto event=o.probe->event;
    try {
        auto&probe=*o.probe;probe.elapsed+=std::max(0.,elapsed);auto index=probe.next;
        if(index>=o.probe_times.size()||probe.elapsed+1.e-9<o.probe_times[index])return;
        while(index+1<o.probe_times.size()&&probe.elapsed+1.e-9>=o.probe_times[index+1])++index;
        auto active=field(appearance,"swingingAnimator"),bound=field(b,"_local_swinging_animator");
        if(active.get()!=probe.animator.get()||bound.get()!=probe.animator.get()){o.probe.reset();put(b,"_local_swinging_probe",p::retain(Py_None));return;}
        auto compound=field(appearance,"_CompoundAppearance__compoundModel"),runtime=field(b,"_runtime"),parts=field(runtime.get(),"tank_part_names"),name=field(parts.get(),"HULL");
        if(compound.get()==Py_None||name.get()==Py_None)throw p::Error("Native HULL diagnostic boundary is unavailable");
        p::Ref args(PyTuple_Pack(1,name.get()));auto node=invoke(compound.get(),"node",args.get());
        if(node.get()==Py_None)throw p::Error("Native HULL diagnostic node is unavailable");
        auto hull=matrix(o.math.get(),node.get()),body=poseProvider(b,"_local_pose_matrix"),root=matrix(o.math.get(),body.get());
        Point roots{},hulls{},delta{};const char*names[]={"yaw","pitch","roll"};const double radians=180./3.14159265358979323846;
        for(unsigned i=0;i<3;++i){const auto a=number(root.get(),names[i]),v=number(hull.get(),names[i]);double d=std::fmod(v-a+3.14159265358979323846,6.28318530717958647692);if(d<0.)d+=6.28318530717958647692;roots[i]=a*radians;hulls[i]=v*radians;delta[i]=(d-3.14159265358979323846)*radians;}
        PySys_WriteStdout("[Offline LAN 0.9.22] SWING_FRAME event=%lld sample=%u elapsed=%.3f period=%.3f direction=%.1f root_ypr_deg=(%.3f, %.3f, %.3f) hull_ypr_deg=(%.3f, %.3f, %.3f) delta_ypr_deg=(%.3f, %.3f, %.3f)\n",
            static_cast<long long>(event),unsigned(index),probe.elapsed,number(active.get(),"accelSwingingPeriod"),number(active.get(),"accelSwingingDirection"),roots[0],roots[1],roots[2],hulls[0],hulls[1],hulls[2],delta[0],delta[1],delta[2]);
        probe.next=index+1;if(probe.next>=o.probe_times.size()){o.probe.reset();put(b,"_local_swinging_probe",p::retain(Py_None));}
    }catch(const std::exception&e){PyErr_Clear();PySys_WriteStdout("[Offline LAN 0.9.22] SWING_FRAME event=%lld error=%s\n",static_cast<long long>(event),e.what());o.probe.reset();put(b,"_local_swinging_probe",p::retain(Py_None));}
}
void tracks(Owner&o,PyObject*b,PyObject*e,PyObject*appearance,const PlayerDriver&driver) {
    auto*actor=o.shared->find("player:"+std::to_string(o.shared->local_player_id));if(!actor)throw p::Error("Native track actor is unavailable");
    const auto mode=engineMode(actor->alive,driver.controls().forward,driver.state().drive_turn,driver.state().speed);auto previous=field(b,"_local_engine_mode");
    bool changed=previous.get()==Py_None;if(!changed){const auto values=p::numbers<2>(previous.get());changed=values[0]!=mode.power||values[1]!=mode.flags;}
    if(changed){applyTrackMode(e,mode);p::Ref value(Py_BuildValue("(ii)",mode.power,mode.flags));put(b,"_local_engine_mode",value);
        optional(o,b,"local acceleration swinging",[&]{const int previousFlags=int(number(b,"_local_swinging_move_flags"));put(b,"_local_swinging_move_flags",p::integer(mode.flags));const auto edge=accelerationSwing(mode.flags,previousFlags,o.move_seconds,o.rotate_seconds);if(!edge)return;
            auto animator=field(appearance,"swingingAnimator"),bound=field(b,"_local_swinging_animator");if(animator.get()==Py_None||animator.get()!=bound.get())return;
            auto disabled=field(b,"_disabled_optional_features"),feature=p::text("local body swinging");if(disabled.get()!=Py_None&&PySet_Contains(disabled.get(),feature.get())==1)return;
            const double oldPeriod=number(animator.get(),"accelSwingingPeriod"),oldDirection=number(animator.get(),"accelSwingingDirection");
            try{put(animator.get(),"accelSwingingDirection",p::number(edge->direction));if(edge->period>oldPeriod)put(animator.get(),"accelSwingingPeriod",p::number(edge->period));}
            catch(...){try{put(animator.get(),"accelSwingingDirection",p::number(oldDirection));put(animator.get(),"accelSwingingPeriod",p::number(oldPeriod));}catch(...){PyErr_Clear();}throw;}
            const auto event=std::int64_t(number(b,"_local_swinging_event_seq"))+1;put(b,"_local_swinging_event_seq",p::integer(event));
            const auto reported=std::int64_t(number(b,"_local_swinging_events_reported"));
            if(reported<o.report_limit){put(b,"_local_swinging_events_reported",p::integer(reported+1));
                PySys_WriteStdout("[Offline LAN 0.9.22] SWING_ARM event=%lld previous_flags=%d flags=%d requested=%.1f before=%.3f active=%.3f direction=%.1f\n",static_cast<long long>(event),previousFlags,mode.flags,edge->period,oldPeriod,number(animator.get(),"accelSwingingPeriod"),edge->direction);
                o.probe.reset(new Owner::Probe(animator.get()));o.probe->event=event;sampleSwing(o,b,appearance,0.);
            }else{o.probe.reset();put(b,"_local_swinging_probe",p::retain(Py_None));}
        });
    }
    if(actor->alive){const auto speeds=trackScroll(driver.descriptor(),driver.state(),o.track_min,o.track_max);applyTrackScroll(appearance,speeds[0],speeds[1]);}
}
}
void selfPresentation(PyObject*b,PyObject*e,PlayerDriver&driver,double elapsed,const SliceTrace&,bool stopped) {
    auto owner=acquire(b,e);auto&self=*owner;auto root=field(b,"_local_matrix"),model=field(b,"_local_model");
    if(root.get()==Py_None||model.get()==Py_None)throw p::Error("Native self presentation is not attached");
    const auto&s=driver.state();const auto previousYaw=number(root.get(),"yaw",s.yaw);const auto dt=std::max(0.,elapsed);
    auto position=vector(self.math.get(),s.position);auto motion=self.camera.sample(xyz(position.get()),dt,self.velocity);
    auto velocity=vector(self.math.get(),motion.velocity),acceleration=vector(self.math.get(),motion.acceleration);rotate(root.get(),{s.yaw,s.pitch,s.roll});put(root.get(),"translation",position);
    if(!truth(b,"_replay_mode"))hydraulic(self,b,e,driver,dt);
    auto snapshot=field(b,"_local_stabilised_snapshot");if(snapshot.get()!=Py_None){auto provider=field(b,"_local_stabilised_matrix");copyMatrix(snapshot.get(),provider.get());}
    overlay(self,b,s,position.get(),velocity.get(),acceleration.get());sniper(self,b,e,previousYaw,s.yaw);put(b,"_local_camera_velocity",velocity);self.velocity=xyz(velocity.get());self.speed=s.speed;self.turn=s.turn_speed;
    auto appearance=field(e,"appearance");optional(self,b,"local body swinging",[&]{bindSwing(self,b,appearance.get(),model.get());});if(!self.current())return;optional(self,b,"local engine audio motion",[&]{bindAudio(owner,b,appearance.get());});if(!self.current())return;
    sampleSwing(self,b,appearance.get(),dt);
    optional(self,b,"local track animation",[&]{tracks(self,b,e,appearance.get(),driver);});if(!self.current())return;
    if(!stopped){auto avatar=field(b,"_avatar");p::Ref speeds(Py_BuildValue("(dd)",s.speed,s.turn_speed));put(avatar.get(),"_PlayerAvatar__lastVehicleSpeeds",speeds);
        auto binding=field(b,"_binding");if(binding.get()!=Py_None)optional(self,b,"engine RPM presentation",[&]{auto now=invoke(self.bigworld.get(),"time");rpm(self,b,e,appearance.get(),driver,p::scalar(now.get()));});
    }
}
std::optional<double> selfAimYaw(PyObject*b,PyObject*e,const PlayerDriver&driver) {
    if(!driver.controls().aim_point)return {};
    auto avatar=field(b,"_avatar"),rotator=field(avatar.get(),"gunRotator");if(rotator.get()==Py_None)return {};
    auto owner=acquire(b,e);auto descriptor=field(e,"typeDescriptor");
    auto snapshot=field(b,"_local_stabilised_snapshot");auto body=poseProvider(b,"_local_pose_matrix");
    const auto angles=targetAngles(*owner,b,descriptor.get(),*driver.controls().aim_point,snapshot.get()==Py_None?body.get():snapshot.get());
    return angles[0];
}
void retireSelfPresentation(PyObject*b) {auto raw=field(b,"_native_client_self_presentation");if(raw.get()==Py_None)return;auto*h=static_cast<Handle*>(PyCapsule_GetPointer(raw.get(),capsuleName));if(!h)throw p::Error("Invalid native self presentation handle");if(*h)(*h)->retired=true;h->reset();put(b,"_native_client_self_presentation",p::retain(Py_None));}
}
