#include "pch.hpp"
#include "stock_native_client_drive_world.hpp"
#include "stock_native_client_drive_install.hpp"
#include "stock_native_battle_local.hpp"
namespace native_battle {namespace startup {
// Provided by the startup subsystem: the arena bounds (min x, min z, max x, max z) and the baked navigation graph.
std::optional<std::array<double,4>> arenaBounds(const Battle&);
PyObject* navigationGraph(const Battle&);
}}
#include "stock_native_bot_physics_snapshot.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
namespace stock_native_client_drive {
namespace {namespace p=stock_native_bot_python;}
NativeWorldConfig readNativeWorldConfig(const native_battle::Battle&b,PyObject*entity,const PlayerDescriptor&descriptor){
    NativeWorldConfig out;const auto arena=native_battle::startup::arenaBounds(b);if(arena)out.arena=*arena;
    auto graph=p::retain(native_battle::startup::navigationGraph(b));if(!PyDict_Check(graph.get()))throw p::Error("Native player navigation graph is unavailable");
    out.baked.width=int(p::integral(graph.get(),"width"));out.baked.height=int(p::integral(graph.get(),"height"));out.baked.cell_size=p::numeric(graph.get(),"cell_size");auto origin=p::value(graph.get(),"origin");const auto xy=p::numbers<2>(origin.get());out.baked.origin_x=xy[0];out.baked.origin_z=xy[1];auto hazards=p::value(graph.get(),"hazards");
    out.baked.hazards=readPlayerHazards(hazards.get());
    if(out.baked.width<=0||out.baked.height<=0||out.baked.cell_size<=0.||out.baked.hazards.size()!=std::size_t(out.baked.width)*std::size_t(out.baked.height))throw p::Error("Native player hazard grid is invalid");
    namespace law=native_battle::local::law;
    out.motion.maximum_angle_step=law::DESTRUCTIBLE_POSE_MAX_ANGLE_STEP;out.motion.maximum_steps=law::DESTRUCTIBLE_POSE_MAX_SWEEP_STEPS;out.motion.drivable_gradient=law::MAX_DRIVABLE_GRADIENT;out.ground_plane_epsilon=law::GROUND_PLANE_EPSILON;out.ground_pitch_limit=law::GROUND_PITCH_LIMIT;
    auto resource=p::value(entity,"typeDescriptor");auto travel=p::value(resource.get(),"defaultVehicleDescr");if(travel.get()!=Py_None){auto parameters=p::value(travel.get(),"physics"),limits=p::value(parameters.get(),"speedLimits");const auto speeds=p::numbers<2>(limits.get());out.motion.travel_forward=speeds[0];out.motion.travel_reverse=speeds[1];}
    const auto&shared=b.vehicles;auto*local=shared->find("player:"+std::to_string(shared->local_player_id));const auto*geometry=local?stock_native_client_query::geometry(*local):nullptr;if(!geometry||!geometry->ready)throw p::Error("Native player collision resources are unavailable");
    out.moving_descriptor=stock_native_bot_physics::geometryInput(*geometry);out.moving_descriptor.pivot_available=descriptor.motion.track_center>0.;out.moving_descriptor.rotation_around_center=descriptor.rotation_around_center;out.moving_descriptor.track_center=descriptor.motion.track_center;
    if(!stock_native_detached_turret::readVehicleGeometry(resource.get(),out.vehicle_geometry))throw p::Error("Native player detached contact geometry is unavailable");
    if(b.local->siege_body_matrix){auto relative=p::value(b.local->siege_body_matrix.get(),"a");if(BW::Script::setData(relative.get(),out.hydraulic_relative,"native player hydraulic body")!=0)throw p::Error("Native player hydraulic resource is unavailable");out.hydraulic_center_z=b.local->siege_aim_center_z;}
    return out;
}
ImpactConfig readPlayerImpactConfig(PyObject*entity){ImpactConfig out;auto descriptor=p::value(entity,"typeDescriptor");out.maximum_health=std::max(1,int(p::integral(descriptor.get(),"maxHealth",p::integral(entity,"health",1))));namespace law=native_battle::local::law;out.safe_speed=law::FALL_SAFE_SPEED;out.maximum_speed=law::PLAYER_LANDING_MAX_IMPACT_SPEED;out.maximum_pending=law::MAX_PENDING_LANDING_IMPACTS;return out;}
}
