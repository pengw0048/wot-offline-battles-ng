#include "pch.hpp"
#include "stock_native_client_drive_world.hpp"
#include "stock_native_client_drive_install.hpp"
#include "stock_native_bot_physics_snapshot.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
namespace stock_native_client_drive {
namespace {namespace p=stock_native_bot_python;}
NativeWorldConfig readNativeWorldConfig(PyObject*battle,PyObject*entity,const PlayerDescriptor&descriptor){
    NativeWorldConfig out;auto arena=p::value(battle,"_arena_bounds");if(arena.get()!=Py_None)out.arena=p::numbers<4>(arena.get());
    auto graph=p::value(battle,"_navigation_graph");if(!PyDict_Check(graph.get()))throw p::Error("Native player navigation graph is unavailable");
    out.baked.width=int(p::integral(graph.get(),"width"));out.baked.height=int(p::integral(graph.get(),"height"));out.baked.cell_size=p::numeric(graph.get(),"cell_size");auto origin=p::value(graph.get(),"origin");const auto xy=p::numbers<2>(origin.get());out.baked.origin_x=xy[0];out.baked.origin_z=xy[1];auto hazards=p::value(graph.get(),"hazards");
    out.baked.hazards=readPlayerHazards(hazards.get());
    if(out.baked.width<=0||out.baked.height<=0||out.baked.cell_size<=0.||out.baked.hazards.size()!=std::size_t(out.baked.width)*std::size_t(out.baked.height))throw p::Error("Native player hazard grid is invalid");
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime")),physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics")),collision(PyImport_ImportModule("gui.mods.offline_lan_0922.world_collision"));
    out.motion.maximum_angle_step=p::numeric(module.get(),"DESTRUCTIBLE_POSE_MAX_ANGLE_STEP");out.motion.maximum_steps=unsigned(p::integral(module.get(),"DESTRUCTIBLE_POSE_MAX_SWEEP_STEPS"));out.motion.drivable_gradient=p::numeric(collision.get(),"_MAX_DRIVABLE_GRADIENT");out.ground_plane_epsilon=p::numeric(module.get(),"GROUND_PLANE_EPSILON");out.ground_pitch_limit=p::numeric(physics.get(),"GROUND_PITCH_LIMIT");
    auto resource=p::value(entity,"typeDescriptor");auto travel=p::value(resource.get(),"defaultVehicleDescr");if(travel.get()!=Py_None){auto parameters=p::value(travel.get(),"physics"),limits=p::value(parameters.get(),"speedLimits");const auto speeds=p::numbers<2>(limits.get());out.motion.travel_forward=speeds[0];out.motion.travel_reverse=speeds[1];}
    const auto shared=stock_native_client_state::fromBattle(battle);auto*local=shared->find("player:"+std::to_string(shared->local_player_id));const auto*geometry=local?stock_native_client_query::geometry(*local):nullptr;if(!geometry||!geometry->ready)throw p::Error("Native player collision resources are unavailable");
    out.moving_descriptor=stock_native_bot_physics::geometryInput(*geometry);out.moving_descriptor.pivot_available=descriptor.motion.track_center>0.;out.moving_descriptor.rotation_around_center=descriptor.rotation_around_center;out.moving_descriptor.track_center=descriptor.motion.track_center;
    if(!stock_native_detached_turret::readVehicleGeometry(resource.get(),out.vehicle_geometry))throw p::Error("Native player detached contact geometry is unavailable");
    auto body=p::value(battle,"_local_siege_body_matrix");if(body.get()!=Py_None){auto relative=p::value(body.get(),"a");if(BW::Script::setData(relative.get(),out.hydraulic_relative,"native player hydraulic body")!=0)throw p::Error("Native player hydraulic resource is unavailable");out.hydraulic_center_z=p::numeric(battle,"_local_siege_aim_center_z");}
    return out;
}
ImpactConfig readPlayerImpactConfig(PyObject*battle,PyObject*entity){ImpactConfig out;auto descriptor=p::value(entity,"typeDescriptor");out.maximum_health=std::max(1,int(p::integral(descriptor.get(),"maxHealth",p::integral(entity,"health",1))));p::Ref physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics")),protocol(PyImport_ImportModule("gui.mods.offline_lan_0922.lan_client")),module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime"));out.safe_speed=p::numeric(physics.get(),"FALL_SAFE_SPEED");out.maximum_speed=p::numeric(protocol.get(),"PLAYER_LANDING_MAX_IMPACT_SPEED");out.maximum_pending=std::size_t(p::integral(module.get(),"MAX_PENDING_LANDING_IMPACTS"));(void)battle;return out;}
}
