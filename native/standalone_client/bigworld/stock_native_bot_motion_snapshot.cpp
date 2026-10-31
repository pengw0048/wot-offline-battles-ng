#include "stock_native_bot_motion_snapshot.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_descriptor.hpp"
#include <Python.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_native_bot {
namespace {
using Point=stock_native_bot_motion::Point;
struct Ref {
    PyObject * p;
    explicit Ref(PyObject * value):p(value){if(!p)throw std::runtime_error("native motion snapshot read failed");}
    ~Ref(){Py_DECREF(p);}
    Ref(const Ref&)=delete;Ref&operator=(const Ref&)=delete;
};
PyObject * field(PyObject * d,const char * key)
{if(!d||!PyDict_Check(d))throw std::runtime_error("native motion snapshot expects a dictionary");return PyDict_GetItemString(d,key);}
double number(PyObject * p)
{
    if(!p||p==Py_None||PyBool_Check(p))throw std::runtime_error("native motion snapshot missing numeric field");
    const double value=PyFloat_AsDouble(p);
    if(PyErr_Occurred()||!std::isfinite(value))throw std::runtime_error("native motion snapshot nonfinite numeric field");
    return value;
}
double num(PyObject * d,const char * key,double fallback=0.)
{const auto p=field(d,key);return !p||p==Py_None?fallback:number(p);}
std::int64_t integer(PyObject * p)
{
    if(!p||PyBool_Check(p))throw std::runtime_error("native motion snapshot missing integer field");
    Ref exact(PyNumber_Index(p));const auto result=PyLong_AsLongLong(exact.p);
    if(PyErr_Occurred())throw std::runtime_error("native motion snapshot integer overflow");
    return result;
}
bool flag(PyObject * d,const char * key,bool fallback=false)
{const auto p=field(d,key);if(!p)return fallback;const int value=PyObject_IsTrue(p);if(value<0)throw std::runtime_error("invalid motion boolean");return value!=0;}
std::optional<double> optional(PyObject * d,const char * key)
{auto p=field(d,key);return !p||p==Py_None?std::nullopt:std::optional<double>(number(p));}
Py_ssize_t count(PyObject * p)
{if(!p||!(PyTuple_Check(p)||PyList_Check(p)))throw std::runtime_error("motion sequence shape");return PySequence_Size(p);}
double index(PyObject * p,Py_ssize_t n){Ref x(PySequence_GetItem(p,n));return number(x.p);}
Point point(PyObject * p)
{return {{index(p,0),index(p,1),index(p,2)}};}
double attribute(PyObject * p,const char * name){Ref x(PyObject_GetAttrString(p,name));return number(x.p);}
stock_native_bot_python::Ref deriveDrive(std::optional<int> crew_level,PyObject* vehicle,PyObject* physics){
 namespace p=stock_native_bot_python;
 auto descriptorPhysics=p::value(vehicle,"physics"),chassis=p::value(vehicle,"chassis");
 const double gravity=attribute(physics,"GRAVITY"),cohesion=attribute(physics,"COHESION");
 double mass=5730.,power=45.*735.49875,forward=32./3.6,reverse=12./3.6,rotation=38.*3.14159265358979323846/180.,friction=.6867,brake=cohesion*gravity,track=1.5,ratio=1.;
 std::array<double,3> terrain{{1.1,1.4,2.6}},rolling{{1.,1.,1.}};
 try{
  mass=p::numeric(descriptorPhysics.get(),"weight",mass);power=p::numeric(descriptorPhysics.get(),"enginePower",power);
  auto speeds=p::value(descriptorPhysics.get(),"speedLimits");if(speeds.get()!=Py_None){forward=index(speeds.get(),0);reverse=index(speeds.get(),1);}
  auto resist=p::value(descriptorPhysics.get(),"terrainResistance");if(resist.get()!=Py_None)for(unsigned i=0;i<3;++i)terrain[i]=index(resist.get(),i);
  friction=p::numeric(descriptorPhysics.get(),"specificFriction",friction);auto b=p::value(descriptorPhysics.get(),"brakeForce");if(b.get()!=Py_None){const double force=p::scalar(b.get());if(force>0.)brake=std::min(cohesion*gravity,force/std::max(mass,1.));}
  auto gauge=p::value(descriptorPhysics.get(),"trackCenterOffset");if(gauge.get()!=Py_None){const auto v=std::abs(p::scalar(gauge.get()));if(v>=.3&&v<=3.)track=v;}
 }catch(const std::exception&){PyErr_Clear();}
 try{const auto projected=p::numeric(descriptorPhysics.get(),"nativePowerRatio");if(projected>0.)ratio=projected;else throw std::runtime_error("missing ratio");}
 catch(const std::exception&){PyErr_Clear();try{auto engine=p::value(vehicle,"engine"),name=p::value(engine.get(),"name"),type=p::value(vehicle,"type"),xphysics=p::value(type.get(),"xphysics"),detailed=p::value(xphysics.get(),"detailed"),engines=p::value(detailed.get(),"engines");
   p::Ref entry(PyObject_GetItem(engines.get(),name.get()));const double selected=p::numeric(entry.get(),"smplEnginePower"),base=power*.001*attribute(physics,"GRAVITY_FACTOR");if(selected>0.&&base>0.)ratio=selected/base;
  }catch(const std::exception&){PyErr_Clear();}}
 if(chassis.get()!=Py_None){const bool hasAround=PyDict_Check(chassis.get())?PyDict_GetItemString(chassis.get(),"rotationIsAroundCenter")!=nullptr:PyObject_HasAttrString(chassis.get(),"rotationIsAroundCenter")!=0;
  auto around=p::value(chassis.get(),"rotationIsAroundCenter");const bool centred=!hasAround||p::flag(around.get());
  if(!centred){auto gauge=p::value(descriptorPhysics.get(),"trackCenterOffset");if(!PyBool_Check(around.get())||gauge.get()==Py_None)throw std::runtime_error("#1513 track pivot geometry is unavailable");const double v=std::abs(p::scalar(gauge.get()));if(v<=0.||v>100.)throw std::runtime_error("#1513 track pivot geometry is unavailable");track=v;}
  auto raw=p::value(chassis.get(),"rotationSpeed");if(raw.get()==Py_None)throw std::runtime_error("#1513 chassis rotation speed is unavailable");rotation=p::scalar(raw.get());if(rotation>6.3)rotation*=3.14159265358979323846/180.;}
 try{auto factors=p::value(descriptorPhysics.get(),"rollingFrictionFactors");if(factors.get()!=Py_None)for(unsigned i=0;i<3;++i)rolling[i]=index(factors.get(),i);}catch(const std::exception&){PyErr_Clear();rolling={{1.,1.,1.}};}
 std::array<double,3> crew{{1.,1.,1.}};
 if(crew_level){
 p::Ref factors(stock_native_bot_descriptor::defaultCrewFactors(vehicle,*crew_level));
 try{auto v=p::value(factors.get(),"chassis/terrainResistance");if(v.get()!=Py_None)for(unsigned i=0;i<3;++i)crew[i]=index(v.get(),i);}catch(const std::exception&){PyErr_Clear();crew={{1.,1.,1.}};}
 const auto factor=[&](const char* name){try{return std::max(0.,p::numeric(factors.get(),name,1.));}catch(const std::exception&){PyErr_Clear();return 1.;}};
 rotation*=factor("vehicle/rotationSpeed");power*=factor("engine/power");
 }
 for(unsigned i=0;i<3;++i)terrain[i]=terrain[i]*crew[i]*rolling[i];
 auto result=p::dict();p::set(result.get(),"mass",p::number(mass));p::set(result.get(),"powerW",p::number(power));p::set(result.get(),"nativePowerRatio",p::number(ratio));p::set(result.get(),"speedFwd",p::number(forward));p::set(result.get(),"speedBwd",p::number(reverse));p::set(result.get(),"specificFriction",p::number(friction));p::set(result.get(),"brakeDecel",p::number(brake));p::set(result.get(),"rotSpd",p::number(rotation));p::set(result.get(),"trackCenter",p::number(track));p::set(result.get(),"terrainResist",p::numbers(terrain));return result;
}
void readBodyBounds(PyObject* vehicle,stock_native_bot_motion::Descriptor& d){
 const auto hull=stock_native_bot_descriptor::hullBounds(vehicle),body=stock_native_bot_descriptor::bodyBounds(vehicle);
 d.hull_minimum=hull.minimum;d.hull_maximum=hull.maximum;d.body_minimum=body.minimum;d.body_maximum=body.maximum;
}
using SeedValue=stock_native_client_state::Value;
double num(const SeedValue& state,const char* key,double fallback=0.){
    const auto& v=state.get(key);if(v.kind==SeedValue::Kind::Null)return fallback;
    if(v.kind==SeedValue::Kind::Integer)return double(v.integer);
    if(v.kind!=SeedValue::Kind::Number||!std::isfinite(v.number))throw std::runtime_error("native motion snapshot invalid numeric field");return v.number;
}
bool flag(const SeedValue& state,const char* key,bool fallback=false){return state.object.count(key)?state.get(key).truth(false):fallback;}
std::optional<double> optional(const SeedValue& state,const char* key){return state.get(key).kind==SeedValue::Kind::Null?std::nullopt:std::optional<double>(num(state,key));}
}
// One Bot's motion state and descriptor laws at the round start. The seed row carries no in-flight
// contact, ground-memory or ram history: a fresh round starts with none.
static bool readSnapshot(const BotSeed& bot,PyObject * vehicle,MotionSnapshot & output,bool mode)
{
    try {
        const auto& raw=bot.state;
        MotionSnapshot value;auto & s=value.state;auto & d=value.descriptor;auto & u=value.suspension;
        s.id=std::int64_t(num(raw,"id"));value.team=int(num(raw,"team"));value.slot=int(num(raw,"slot"));
        s.position={{num(raw,"x"),num(raw,"y"),num(raw,"z")}};
#define NUMBER(member,key) s.member=num(raw,key)
        NUMBER(yaw,"yaw");NUMBER(pitch,"pitch");NUMBER(roll,"roll");NUMBER(speed,"speed");
        NUMBER(hydraulic_pitch,"suspension_pitch");
        s.terrain_pitch=optional(raw,"terrain_pitch").value_or(s.pitch-s.hydraulic_pitch);
        NUMBER(vertical_speed,"vertical_speed");NUMBER(pitch_speed,"suspension_pitch_velocity");NUMBER(roll_speed,"suspension_roll_velocity");
        NUMBER(push_x,"push_x");NUMBER(push_z,"push_z");NUMBER(push_yaw,"push_yaw");NUMBER(slide_speed,"slide_speed");
        NUMBER(air_lateral_x,"air_lateral_x");NUMBER(air_lateral_z,"air_lateral_z");NUMBER(last_drive_pitch,"last_drive_pitch");
        NUMBER(contact_forward,"_contact_forward_speed");NUMBER(direction_command,"_direction_command");
        NUMBER(rotation_refused_until,"_native_rotation_refused_until");NUMBER(support_repair_elapsed,"_support_repair_elapsed");
        NUMBER(support_vertical_speed,"_suspension_support_vertical_speed");
#undef NUMBER
        // battle_start installs a zero turn speed and no hard-contact grind.
        s.turn_speed=0.;s.hard_grinds=0;
        s.movement_direction=int(num(raw,"movement_dir"));s.rotation_direction=int(num(raw,"rotation_dir"));
        s.alive=flag(raw,"alive",true);s.airborne=flag(raw,"airborne");s.grounded_once=flag(raw,"grounded_once");
        s.left_flying=flag(raw,"left_flying");s.right_flying=flag(raw,"right_flying");s.service_brake=flag(raw,"service_brake");
        s.contact_dynamics=flag(raw,"_contact_dynamics");s.rotation_contact_blocked=flag(raw,"_rotation_contact_blocked");
        s.basic_fall_origin=optional(raw,"_basic_fall_origin_y");s.straddle_reference=optional(raw,"_legacy_straddle_reference_y");
        s.basic_continuous_support=flag(raw,"_basic_continuous_support");
        s.slope_basis_yaw=optional(raw,"_basic_slope_basis_yaw");s.ground_height=optional(raw,"ground_height");
        s.destructible_contact_speed=optional(raw,"destructible_contact_speed");
        d.half_length=num(raw,"half_length");d.half_width=num(raw,"half_width");
        const auto& shape=raw.get("collision_shape");if(shape.kind!=SeedValue::Kind::Array||shape.array.size()!=4)throw std::runtime_error("motion hull shape width");
        for(unsigned i=0;i<4;++i)d.collision_shape[i]=shape.array[i].numeric(NAN);
        Ref physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));
        auto params=deriveDrive(bot.crew_level,vehicle,physics.p);
        const char * names[]={"GRAVITY","GRAVITY_FACTOR","COHESION","COH_DECAY_Y","COH_DECAY_FACTOR","COH_DECAY_POW",
            "SLOPE_COH_DECAY_Y","SLOPE_COH_DECAY","COH_DECAY_BOUND","POWER_FACTOR","BKWD_POWER_FRACTION","ENGINE_MIN_V",
            "DRIVE_TRACTION","SLOPE_GRIP_LNG_MIN_Y","SLOPE_GRIP_LNG_FULL_Y","SLOPE_GRIP_LNG_MIN","SLOPE_GRIP_LNG_FULL",
            "STEER_RESIST_MULT","SLIDE_HOLD_TAN","SLIDE_KINETIC","SLIP_THRESHOLD_TAN","SLIP_DRAG","COAST_BRAKE_SHARE",
            "OVERSPEED_MAX_FACTOR","ANG_ACCELERATION_TIME","SPEED_AFFECT_ROT_DECREASE"};
        for(unsigned i=0;i<26;++i)d.drive.t[i]=attribute(physics.p,names[i]);
        d.drive.mass=number(field(params.get(),"mass"));d.drive.power=number(field(params.get(),"powerW"));
        d.drive.ratio=number(field(params.get(),"nativePowerRatio"));d.drive.forward=number(field(params.get(),"speedFwd"));
        d.drive.reverse=number(field(params.get(),"speedBwd"));d.drive.friction=number(field(params.get(),"specificFriction"));
        d.drive.brake=number(field(params.get(),"brakeDecel"));d.drive.rotation=number(field(params.get(),"rotSpd"));
        for(unsigned i=0;i<3;++i)d.drive.terrain[i]=index(field(params.get(),"terrainResist"),i);
        d.track_center=number(field(params.get(),"trackCenter"));
        Ref glancing(PyObject_GetAttrString(physics.p,"HARD_CONTACT_YAW_DELTAS"));
        if(count(glancing.p)!=4)throw std::runtime_error("motion contact yaw count");
        for(unsigned i=0;i<4;++i)d.glancing_yaws[i]=index(glancing.p,i);
        d.hard_brake_decay=attribute(physics.p,"HARD_CONTACT_BRAKE_DECAY");d.hard_stop_speed=attribute(physics.p,"HARD_CONTACT_STOP_SPEED");
        d.hard_entry_factor=attribute(physics.p,"HARD_CONTACT_ENTRY_FACTOR");d.hard_slide_decay=attribute(physics.p,"HARD_CONTACT_SLIDE_DECAY");
        d.hard_grind_ticks=int(attribute(physics.p,"HARD_CONTACT_GRIND_TICKS"));
        d.side_min_y=attribute(physics.p,"SLOPE_GRIP_SDW_MIN_Y");d.side_full_y=attribute(physics.p,"SLOPE_GRIP_SDW_FULL_Y");
        d.side_min=attribute(physics.p,"SLOPE_GRIP_SDW_MIN");d.side_full=attribute(physics.p,"SLOPE_GRIP_SDW_FULL");
        d.constraint_iterations=int(attribute(physics.p,"SERVER_PHYSICS_CONSTRAINT_ITERATIONS"));
        readBodyBounds(vehicle,d);
        const char * tuning[]={"GRAVITY","CONTACT_PENETRATION","AIRBORNE_ANGULAR_DAMPING","AIRBORNE_ANGULAR_SPEED_LIMIT",
            "FREEZE_ACCEL_EPSILON","FREEZE_VEL_EPSILON","FREEZE_ANG_ACCEL_EPSILON","FREEZE_ANG_VEL_EPSILON","SERVER_PHYSICS_MAX_SUBSTEPS"};
        for(unsigned i=0;i<9;++i)u.tuning[i]=attribute(physics.p,tuning[i]);
        u.maximum_steps=int(u.tuning[8]);u.half_length=d.half_length;u.half_width=d.half_width;
        u.slide_hold_tan=attribute(physics.p,"SLIDE_HOLD_TAN");u.cohesion=attribute(physics.p,"COHESION");
        u.slide_drag=attribute(physics.p,"SLIDE_DRAG");u.slide_max=attribute(physics.p,"SLIDE_MAX");
        // The pinned configuration keeps the detailed Bot suspension off (bot_runtime.BOT_FULL_SUPPORT_ENABLED).
        value.detailed_suspension=false;
        if(mode){namespace p=stock_native_bot_python;auto misc=p::value(vehicle,"miscAttrs");const double spall=p::numeric(misc.get(),"antifragmentationLiningFactor",1.);if(spall<1.)throw std::runtime_error("#1513 Spall Liner factor is invalid");value.ram.spall=spall;value.ram.bonus=0.;}
        else{const auto& ram=raw.get("ram_profile");if(ram.kind!=SeedValue::Kind::Null){value.ram.spall=num(ram,"spall_coefficient",1.);value.ram.bonus=num(ram,"ramming_bonus");value.ram.contact_armor=optional(ram,"contact_armor");}}
        value.ram_sequence=std::int64_t(num(raw,"_ram_motion_seq"));
        stock_native_bot_motion::validate(d);output=std::move(value);return true;
    } catch(const std::exception& error) {
        if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return false;
    }
}
PyObject* readUnmodifiedDrive(PyObject* vehicle){
 try{Ref physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));auto result=deriveDrive(std::nullopt,vehicle,physics.p);return result.release();}
 catch(const std::exception& error){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return nullptr;}
}
bool readMotionSnapshot(const BotSeed & bot,PyObject * vehicle,MotionSnapshot & output)
{return readSnapshot(bot,vehicle,output,false);}
bool readMotionMode(const BotSeed & bot,PyObject * vehicle,MotionModeSnapshot & output)
{
    MotionSnapshot snapshot;
    if(!readSnapshot(bot,vehicle,snapshot,true))return false;
    output={std::move(snapshot.descriptor),std::move(snapshot.suspension),snapshot.ram,snapshot.detailed_suspension};
    return true;
}
}
