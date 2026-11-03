#pragma once
#include "stock_native_client_state.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
namespace offline_simulation {enum class ActorKind {Bot,Human};struct ActorKey {ActorKind kind;std::int64_t id;};}
namespace native_battle {
using Value=stock_native_client_state::Value;using Point=std::array<double,3>;
struct Actor {bool ready=true;std::int64_t engine_id=10;struct {Value collision_pose;} presented;};
struct BotAuthority {
    struct PresentedBot {const Actor*vehicle=nullptr;std::int64_t engine_id=0;std::uint64_t generation=0;
        std::optional<std::array<double,3>> clock;std::optional<std::array<double,6>> sample;
        std::optional<std::array<double,11>> pose;std::optional<std::array<double,5>> aim;std::optional<std::array<double,9>> projectile;};
    std::map<std::int64_t,PresentedBot> presented;std::map<std::int64_t,double> yaw_rates;
};
struct Battle {std::uint64_t generation=1;std::unique_ptr<BotAuthority> bots=std::make_unique<BotAuthority>();Actor actor;
    int poses=0,aims=0;bool fail_pose=false;Point position{};double yaw=0.,aim=0.,pitch=0.;std::optional<double> relax;
    Actor*vehicle(const std::string&) {return &actor;}};
namespace remote {
inline bool setPose(Battle&b,std::int64_t,const Point&p,double yaw,double,double,std::optional<double>relax,double){if(b.fail_pose)throw std::runtime_error("retired native pose");++b.poses;b.position=p;b.yaw=yaw;b.relax=relax;return true;}
inline void setAim(Battle&b,std::int64_t,double,double aim,double pitch){++b.aims;b.aim=aim;b.pitch=pitch;}
}
}
namespace stock_native_bot {
using Point=std::array<double,3>;
struct Actor {int slot=0,team=1,mode=0;struct Motion {std::int64_t id=1;Point position{};double yaw=0.,pitch=0.,roll=0.,speed=0.,vertical_speed=0.,slide_speed=0.,push_x=0.,push_z=0.,air_lateral_x=0.,air_lateral_z=0.;int movement_direction=0,rotation_direction=0;bool airborne=false;} motion;
    struct {bool alive=true;double health=100.;} combat;struct {double aim_yaw=0.;} weapon;struct {int mode=0;} siege;};
struct Weapon {struct {double gun_pitch=0.,turret_yaw=0.;} aim;};
struct WeaponStore {Weapon value;const Weapon&at(offline_simulation::ActorKey)const{return value;}};
class Runtime {public:std::map<std::int64_t,Actor> values{{1,Actor{}}};WeaponStore store;struct Clock {std::int64_t sample_us=1;} time;
    const auto&actors()const{return values;}const auto&weapons()const{return store;}const auto&clock()const{return time;}};
struct Geometry {std::optional<double> static_turret_yaw,static_gun_pitch;};
struct ServiceEntity {std::array<Geometry,2> geometry;int mode=0;};
class NativeServices {public:ServiceEntity entity;const ServiceEntity*find(offline_simulation::ActorKey key)const{return key.id==1?&entity:nullptr;}};
}
