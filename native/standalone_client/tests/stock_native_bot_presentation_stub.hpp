#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
namespace offline_simulation {enum class ActorKind { Bot,Human };struct ActorKey {ActorKind kind;std::int64_t id;};}
namespace stock_native_bot_geometry {struct Vehicle {bool has_siege=false;std::optional<double> static_turret_yaw,static_gun_pitch;};}
namespace stock_native_bot {
namespace geometry=stock_native_bot_geometry;
using Point=std::array<double,3>;struct ServiceEntity {std::array<stock_native_bot_geometry::Vehicle,2> geometry;int mode=0;};
class NativeServices {public:ServiceEntity entity;const ServiceEntity*find(offline_simulation::ActorKey key)const{return key.id==1?&entity:nullptr;}};
}
