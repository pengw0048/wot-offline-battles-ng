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
PlayerDescriptor readPlayerDescriptor(const native_battle::Battle&b,PyObject*resource,const native_battle::local::DriveParams&params){
    namespace l=native_battle::local::law;
    PlayerDescriptor out;auto&d=out.motion;auto&law=d.drive;
    const double laws[]={l::GRAVITY,l::GRAVITY_FACTOR,l::COHESION,l::COH_DECAY_Y,l::COH_DECAY_FACTOR,l::COH_DECAY_POW,l::SLOPE_COH_DECAY_Y,l::SLOPE_COH_DECAY,l::COH_DECAY_BOUND,l::POWER_FACTOR,l::BKWD_POWER_FRACTION,l::ENGINE_MIN_V,l::DRIVE_TRACTION,l::SLOPE_GRIP_LNG_MIN_Y,l::SLOPE_GRIP_LNG_FULL_Y,l::SLOPE_GRIP_LNG_MIN,l::SLOPE_GRIP_LNG_FULL,l::STEER_RESIST_MULT,l::SLIDE_HOLD_TAN,l::SLIDE_KINETIC,l::SLIP_THRESHOLD_TAN,l::SLIP_DRAG,l::COAST_BRAKE_SHARE,l::OVERSPEED_MAX_FACTOR,l::ANG_ACCELERATION_TIME,l::SPEED_AFFECT_ROT_DECREASE};
    static_assert(sizeof(laws)/sizeof(laws[0])==26,"drive law table width");for(unsigned i=0;i<26;++i)law.t[i]=laws[i];
    law.mass=params.mass;law.power=params.power;law.ratio=params.native_power_ratio;law.forward=params.forward;law.reverse=params.reverse;law.friction=params.friction;law.brake=params.brake;law.rotation=params.rotation;d.track_center=params.track_center;
    std::copy(params.terrain.begin(),params.terrain.end(),law.terrain);out.rotation_around_center=params.rotation_around_center;
    d.hard_brake_decay=l::HARD_CONTACT_BRAKE_DECAY;d.hard_stop_speed=l::HARD_CONTACT_STOP_SPEED;d.hard_entry_factor=l::HARD_CONTACT_ENTRY_FACTOR;d.hard_slide_decay=l::HARD_CONTACT_SLIDE_DECAY;
    d.side_min_y=l::SLOPE_GRIP_SDW_MIN_Y;d.side_full_y=l::SLOPE_GRIP_SDW_FULL_Y;d.side_min=l::SLOPE_GRIP_SDW_MIN;d.side_full=l::SLOPE_GRIP_SDW_FULL;
    d.hard_grind_ticks=l::HARD_CONTACT_GRIND_TICKS;d.constraint_iterations=l::SERVER_PHYSICS_CONSTRAINT_ITERATIONS;std::copy(l::HARD_CONTACT_YAW_DELTAS.begin(),l::HARD_CONTACT_YAW_DELTAS.end(),d.glancing_yaws.begin());
    const auto body=stock_native_bot_descriptor::bodyBounds(resource),hull=stock_native_bot_descriptor::hullBounds(resource);d.body_minimum=body.minimum;d.body_maximum=body.maximum;d.hull_minimum=hull.minimum;d.hull_maximum=hull.maximum;
    d.collision_shape=stock_native_bot_descriptor::chassisShape(resource);d.half_width=d.collision_shape[0];d.half_length=d.collision_shape[1];
    auto gun=p::value(resource,"gun"),limits=p::value(gun.get(),"turretYawLimits");if(limits.get()!=Py_None)out.gun_yaw_limits=p::numbers<2>(limits.get());out.hydraulic=p::truth(resource,"hasSiegeMode");
    out.network_seconds=l::NETWORK_INPUT_SECONDS;out.gun_traverse_epsilon=l::GUN_TRAVERSE_LIMIT_EPSILON;
    out.slide_drag=l::SLIDE_DRAG;out.slide_max=l::SLIDE_MAX;out.ground_follow_base=l::GROUND_FOLLOW_BASE;out.ground_follow_min=l::GROUND_FOLLOW_MIN;out.ground_follow_max=l::GROUND_FOLLOW_MAX;
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
