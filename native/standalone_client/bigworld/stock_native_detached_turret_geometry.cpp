#include "pch.hpp"
#include "stock_native_detached_turret_world.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#include <cmath>
namespace stock_native_detached_turret {
std::optional<double> nativeTurretYaw(const stock_native_bot_geometry::Vehicles&vehicles,Owner owner){
    const auto*vehicle=vehicles.find({owner.human?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,owner.id});
    if(!vehicle||!vehicle->native_turret)return {};
    BW::Matrix sampled;vehicle->native_turret->matrix(sampled);
    const auto yaw=double(sampled.yaw());return std::isfinite(yaw)?std::optional<double>(yaw):std::nullopt;
}
}
