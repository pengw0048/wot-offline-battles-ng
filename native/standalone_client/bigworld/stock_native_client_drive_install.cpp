#include "stock_native_client_drive_install.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_local.hpp"
namespace stock_native_client_drive {
namespace {namespace p=stock_native_bot_python;}
std::vector<unsigned> readPlayerHazards(PyObject* values){
    std::vector<unsigned> result;
    // The pinned navigation loader packs byte-valued cells after validation.
    if(PyByteArray_Check(values)){
        const auto count=PyByteArray_GET_SIZE(values);
        const auto* bytes=reinterpret_cast<const unsigned char*>(PyByteArray_AS_STRING(values));
        result.assign(bytes,bytes+count);
    }else{
        const auto count=p::size(values);
        result.reserve(std::size_t(count));
        for(Py_ssize_t i=0;i<count;++i){
            auto value=p::item(values,i);const auto n=p::wide(value.get());
            if(n<0||n>0xffffffffLL)throw p::Error("Native player hazard mask is invalid");
            result.push_back(unsigned(n));
        }
    }
    return result;
}
PlayerDescriptor readPlayerDescriptor(const native_battle::Battle&b,PyObject*entity,PyObject*params){
    if(!PyDict_Check(params))throw p::Error("Native player effective physics are unavailable");
    PlayerDescriptor out;auto&d=out.motion;auto&law=d.drive;p::Ref physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime"));
    const char*names[]={"GRAVITY","GRAVITY_FACTOR","COHESION","COH_DECAY_Y","COH_DECAY_FACTOR","COH_DECAY_POW","SLOPE_COH_DECAY_Y","SLOPE_COH_DECAY","COH_DECAY_BOUND","POWER_FACTOR","BKWD_POWER_FRACTION","ENGINE_MIN_V","DRIVE_TRACTION","SLOPE_GRIP_LNG_MIN_Y","SLOPE_GRIP_LNG_FULL_Y","SLOPE_GRIP_LNG_MIN","SLOPE_GRIP_LNG_FULL","STEER_RESIST_MULT","SLIDE_HOLD_TAN","SLIDE_KINETIC","SLIP_THRESHOLD_TAN","SLIP_DRAG","COAST_BRAKE_SHARE","OVERSPEED_MAX_FACTOR","ANG_ACCELERATION_TIME","SPEED_AFFECT_ROT_DECREASE"};
    for(unsigned i=0;i<26;++i)law.t[i]=p::numeric(physics.get(),names[i]);
    double*values[]={&law.mass,&law.power,&law.ratio,&law.forward,&law.reverse,&law.friction,&law.brake,&law.rotation,&d.track_center};const char*keys[]={"mass","powerW","nativePowerRatio","speedFwd","speedBwd","specificFriction","brakeDecel","rotSpd","trackCenter"};for(unsigned i=0;i<9;++i)*values[i]=p::numeric(params,keys[i]);
    auto terrain=p::value(params,"terrainResist");const auto resistance=p::numbers<3>(terrain.get());std::copy(resistance.begin(),resistance.end(),law.terrain);out.rotation_around_center=p::truth(params,"rotationIsAroundCenter",true);
#define TUNE(member,name) d.member=p::numeric(physics.get(),name)
    TUNE(hard_brake_decay,"HARD_CONTACT_BRAKE_DECAY");TUNE(hard_stop_speed,"HARD_CONTACT_STOP_SPEED");TUNE(hard_entry_factor,"HARD_CONTACT_ENTRY_FACTOR");TUNE(hard_slide_decay,"HARD_CONTACT_SLIDE_DECAY");
    TUNE(side_min_y,"SLOPE_GRIP_SDW_MIN_Y");TUNE(side_full_y,"SLOPE_GRIP_SDW_FULL_Y");TUNE(side_min,"SLOPE_GRIP_SDW_MIN");TUNE(side_full,"SLOPE_GRIP_SDW_FULL");
#undef TUNE
    d.hard_grind_ticks=int(p::integral(physics.get(),"HARD_CONTACT_GRIND_TICKS"));d.constraint_iterations=int(p::integral(physics.get(),"SERVER_PHYSICS_CONSTRAINT_ITERATIONS"));auto glancing=p::value(physics.get(),"HARD_CONTACT_YAW_DELTAS");d.glancing_yaws=p::numbers<4>(glancing.get());
    auto resource=p::value(entity,"typeDescriptor");const auto body=stock_native_bot_descriptor::bodyBounds(resource.get()),hull=stock_native_bot_descriptor::hullBounds(resource.get());d.body_minimum=body.minimum;d.body_maximum=body.maximum;d.hull_minimum=hull.minimum;d.hull_maximum=hull.maximum;
    d.collision_shape=stock_native_bot_descriptor::chassisShape(resource.get());d.half_width=d.collision_shape[0];d.half_length=d.collision_shape[1];
    auto gun=p::value(resource.get(),"gun"),limits=p::value(gun.get(),"turretYawLimits");if(limits.get()!=Py_None)out.gun_yaw_limits=p::numbers<2>(limits.get());out.hydraulic=p::truth(resource.get(),"hasSiegeMode");
    out.network_seconds=p::numeric(module.get(),"NETWORK_INPUT_SECONDS");out.gun_traverse_epsilon=p::numeric(module.get(),"GUN_TRAVERSE_LIMIT_EPSILON");
    out.slide_drag=p::numeric(physics.get(),"SLIDE_DRAG");out.slide_max=p::numeric(physics.get(),"SLIDE_MAX");out.ground_follow_base=p::numeric(physics.get(),"GROUND_FOLLOW_BASE");out.ground_follow_min=p::numeric(physics.get(),"GROUND_FOLLOW_MIN");out.ground_follow_max=p::numeric(physics.get(),"GROUND_FOLLOW_MAX");
    out.team=int(p::integral(b.engine.client.get(),"team"));stock_native_bot_motion::validate(d);return out;
}
// The pose the local presentation published at attach time, and zero motion.
PlayerState readPlayerInitialState(const native_battle::Battle&b,const stock_native_client_state::Actor&actor){
    const auto&local=*b.local;if(!actor.local||!local.matrix)throw p::Error("Native player has no bound local physical pose");
    PlayerState out;out.position=local.position;out.yaw=local.yaw;out.pitch=out.terrain_pitch=local.pitch;out.roll=local.roll;out.speed=local.speed;out.turn_speed=local.turn_speed;
    out.hydraulic_pitch=local.siege_aim_pitch;out.air_lateral_x=local.air_lateral[0];out.air_lateral_z=local.air_lateral[2];out.airborne=local.airborne;out.service_brake=local.service_brake;out.has_presentation=true;return out;
}
Controls readPlayerInitialControls(const native_battle::Battle&b){const auto&c=b.local->controls;Controls out;out.forward=c.forward;out.turn=c.turn;out.aim_yaw=c.aim_yaw;out.gun_pitch=c.gun_pitch;out.aim_pitch=c.aim_pitch;out.handbrake=c.handbrake;out.aim_point=c.aim_point;return out;}
}
