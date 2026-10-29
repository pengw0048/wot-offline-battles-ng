#pragma once
#include "stock_native_bot_combat_runtime.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#include "stock_native_bot_armor_geometry.hpp"
#include "stock_native_bot_turret_obstacles.hpp"
#include "stock_native_bot_physics_host.hpp"

namespace stock_native_bot {
namespace geometry=stock_native_bot_geometry;
namespace step=wot::offline_bot_step;
struct GeometryRow {
    geometry::Key key;
    geometry::Pose pose;
    int team=0;
    bool alive=false,available=false;
};
struct WeaponWorldContext {
    stock_native_world::Scene& scene;
    geometry::Vehicles& vehicles;
    std::map<std::int64_t,GeometryRow>& rows;
    stock_native_bot_turret::Obstacles& turrets;
    stock_native_bot_physics::NativeHost& physics;
    double server_time_ms=0.;
    // The pinned implementation freezes its wreck view per render callback
    // and refreshes it after a record lifecycle revision.
    std::vector<std::int64_t> wrecks;
};
class NativeWeaponWorld final:public step::WeaponWorld,public step::DirectCombatWorld,
    public step::ArcWorld,public stock_native_bot_armor::Geometry,
    public stock_native_bot_turret::DetachedHits {
    WeaponWorldContext& context_;
    std::int64_t source_=0,target_=0;
    geometry::Pose target_pose_;
    const stock_native_bot_armor::Shell* shell_=nullptr;
    const GeometryRow* row(std::int64_t)const;
    std::vector<geometry::Hit> intersections(const GeometryRow&,const geometry::Pose&,
        const step::Point&,const step::Point&)const;
public:
    explicit NativeWeaponWorld(WeaponWorldContext& context):context_(context){}
    void bind(std::int64_t source,std::int64_t target,geometry::Pose observed,
        const stock_native_bot_armor::Shell* shell);
    void freezeWrecks();
    bool turret_clear(const step::AimPose&,const step::AimPose&)override;
    bool barrel_under_water(const step::Point&)override;
    bool vehicle_segment(const step::LaneBody&,const step::Point&,const step::Point&)override;
    bool debris_segment(const step::Point&,const step::Point&)override;
    bool static_segment_blocked(const step::Point&,const step::Point&)override;
    bool wreck_segment_blocked(const step::Point&,const step::Point&)override;
    std::optional<double> nominal_damage(const step::Point&,const step::Point&)override;
    step::ArcQuery segment(const step::Point&,const step::Point&,step::Point&)override;
    bool ready(const stock_native_bot_armor::Body&)override;
    stock_native_bot_armor::Body physical_body(const stock_native_bot_armor::Body&)override;
    step::Point native_point(const step::Point&)override;
    std::optional<stock_native_bot_armor::Hull> hull(const stock_native_bot_armor::Body&)override;
    std::vector<stock_native_bot_armor::Layer> segment(const stock_native_bot_armor::Body&,
        const step::Point&,const step::Point&)override;
    std::vector<double> segment(stock_native_bot_turret::Owner,bool gun,
        const step::Point&,const step::Point&)override;
};
}
