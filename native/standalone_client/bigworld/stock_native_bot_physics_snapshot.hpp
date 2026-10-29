#pragma once
#include "stock_native_bot_physics_host.hpp"
struct _object;
namespace stock_native_bot_physics {
struct PhysicsSnapshot {Geometry geometry;HostMemory memory;Config config;};
// Vehicle is the native, strongly owned descriptor geometry. This template
// copies its numeric envelope without importing BW ownership into the DTO.
template<class Vehicle> stock_native_bot_turret::MovingDescriptor geometryInput(const Vehicle& vehicle) {
    stock_native_bot_turret::MovingDescriptor result;
    result.chassis.bounds={vehicle.components[0].minimum,vehicle.components[0].maximum};
    result.hull.bounds={vehicle.components[1].minimum,vehicle.components[1].maximum};
    result.hull.offset=vehicle.hull_offset;return result;
}
template<class Vehicle> stock_native_bot_turret::DetachedDescriptor detachedDescriptor(const Vehicle& vehicle) {
    stock_native_bot_turret::DetachedDescriptor result;
    result.turret.bounds={vehicle.components[2].minimum,vehicle.components[2].maximum};
    result.gun.bounds={vehicle.components[3].minimum,vehicle.components[3].maximum};
    result.gun.offset=vehicle.gun_offset;return result;
}
// Installation/mode-change only. No Python object survives this call.
bool readPhysicsSnapshot(_object * battle_runtime,_object * bot_state,_object * descriptor,
    stock_native_bot_turret::MovingDescriptor,bool detailed_suspension,PhysicsSnapshot&);
// Called only while decoding an accepted cold/event-ingress row. Query paths
// retain the resulting value and never consult the original Python mapping.
bool readDetachedTurretRow(_object * row,stock_native_bot_turret::Record&);
}
