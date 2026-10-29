#pragma once
#include "stock_native_bot_step_state.hpp"
#include "offline_simulation_weapons.h"

namespace wot::offline_bot_step {
struct HydraulicConfig {
    bool present=false, available=false, enabled=false;
    double minimum=0, maximum=0, speed=0;
};
struct WeaponFrame {
    AimPose pose;
    double terrain_pitch=0, hydraulic_pitch=0, elapsed=0;
    double speed=0, turn_speed=0, crew_factor=1, turret_factor=1, gun_factor=1;
    double dispersion_factor=1, aim_factor=1, rear_turn=0;
    bool target=false, planning_pending=false, siege_enabled=false;
    bool limited_yaw=false, valid_pitch=false, pitch_locked=false;
    double minimum_yaw=0, maximum_yaw=0, static_pitch=0;
};
struct WeaponAimResult {
    bool origin_available=true, aligned=false, hydraulic_reachable=false;
    double desired_world_yaw=0, horizontal=0, hydraulic_pitch=0;
    std::vector<offline_simulation::weapons::BurstEdge> edges;
};
struct FireConditions {
    bool alive=true, publish=false, action_fresh=false, fire_allowed=false;
    bool has_target=false, has_ballistic=false, drowning=false, overturned=false;
    bool gun_destroyed=false, siege_switching=false, wire_failure=false;
    bool shot_clear=false, gunner_ready=false, artillery=false;
    double target_distance=0, fire_range=0, reload_factor=1, dispersion_factor=1;
};
struct LaneBody {
    std::int64_t id=0;
    std::string kind;
    int team=0;
    bool source=false, alive=true, available=true;
    Point position{};
    double radius=0;
};
struct FireResult { int committed=0; bool cancelled=false, started=false; std::optional<LaneBody> blocker; };
struct DirectShot {
    std::int64_t round_id=0;
    int team=0;
    double speed=0, gravity=0, maximum_distance=0, flight_time=0, splash_radius=0;
    double dispersion=0;
    Point barrel_point{};
    std::string class_tag;
};
std::array<double,2> dispersed_shot_angles(std::int64_t round_id,std::int64_t bot_id,
    const offline_simulation::weapons::BurstEdge&, const AimPose&, double dispersion);
struct LaneResult {
    bool clear=false, has_blocker=false;
    LaneBody blocker;
    std::optional<offline_simulation::weapons::LaunchRecord> launch;
};
class DirectWorld {
public:
    virtual ~DirectWorld()=default;
    virtual bool barrel_under_water(const Point&)=0;
    virtual bool vehicle_segment(const LaneBody&,const Point&,const Point&)=0;
    virtual bool debris_segment(const Point&,const Point&)=0;
};
LaneResult prove_direct_launch(offline_simulation::ActorKey,const AimPose&,
    const GunMount&,const DirectShot&,const offline_simulation::weapons::BurstEdge&,
    std::int64_t launch_time_us,const std::vector<LaneBody>&,DirectWorld&);
LaneResult prove_friendly_path(const std::vector<Point>& path, int team,
    double splash_radius, const std::vector<LaneBody>&, DirectWorld&);
struct BallisticInput {
    Point origin{}, target{}, velocity{};
    double speed=0, gravity=0, maximum_distance=0;
};
bool world_solution_reachable(const WeaponFrame&,const HydraulicConfig&,
    const offline_simulation::weapons::AimConfig&,double yaw,double pitch);
struct GunnerConfig {
    double reaction_seconds=0, patience_seconds=0, converged_factor=0;
    double aim_bias_factor=0, lead_error=0;
};
struct GunnerState {
    bool held=false, fired=false, error_valid=false;
    std::string target_kind;
    std::int64_t target_id=0, fire_sequence=0, epoch=0;
    double since=0, laid_since=0, radius=0, azimuth=0, lead_scale=1;
};
struct GunnerTarget { std::string kind; std::int64_t id=0; Point position{},velocity{}; };
struct GunnerResult { GunnerTarget aimed; bool ready=false; };
struct AimBox { Point lower{},upper{},mount{}; bool turret=false; std::optional<double> static_yaw; };
struct AimSample { Point mount{},point{}; bool turret=false; std::optional<double> static_yaw; };
struct AimSelection { std::vector<AimSample> samples; std::size_t cursor=0; std::optional<std::size_t> selected; };
std::vector<AimSample> vehicle_aim_samples(const std::vector<AimBox>&);
Point vehicle_aim_point(const AimSample&,const AimPose&);
class AimSelectionWorld {
public:
    virtual ~AimSelectionWorld()=default;
    virtual bool static_segment_blocked(const Point&,const Point&)=0;
    virtual bool wreck_segment_blocked(const Point&,const Point&)=0;
    // Null means material evidence was unavailable, not zero damage.
    virtual std::optional<double> nominal_damage(const Point&,const Point&)=0;
};
bool select_exposed_aim(AimSelection&,const AimPose& target,const Point& origin,
    double rating,std::int64_t round_id,std::int64_t bot_id,
    const GunnerTarget&,const GunnerState&,AimSelectionWorld&);
std::optional<GunnerResult> advance_gunner(GunnerState&, const GunnerConfig&,
    std::optional<GunnerTarget>, const Point& source, double fully_aimed_dispersion,
    double current_dispersion_factor, std::int64_t round_id, std::int64_t bot_id,
    std::int64_t fire_sequence, double now);
std::optional<AimTarget> direct_ballistic_solution(const BallisticInput&,
    const WeaponFrame&, const HydraulicConfig&,
    const offline_simulation::weapons::AimConfig&);
struct BallisticSignature {
    std::string target_kind;
    std::optional<std::int64_t> target_id;
    std::int64_t descriptor_revision=0, fire_sequence=0;
    int shell=0, siege_state=0;
    bool target_alive=true;
    bool operator==(const BallisticSignature&) const;
};
struct BallisticCache {
    bool initialized=false;
    BallisticSignature signature;
    double deadline=0;
    std::optional<AimTarget> solution;
};
struct BallisticResult { bool fresh=false; std::optional<AimTarget> solution; };
BallisticResult advance_direct_ballistic(BallisticCache&, const BallisticSignature&,
    const BallisticInput&, const WeaponFrame&, const HydraulicConfig&,
    const offline_simulation::weapons::AimConfig&, std::int64_t bot_id,
    double now, bool refresh_control, bool force);
class WeaponWorld {
public:
    virtual ~WeaponWorld()=default;
    virtual bool turret_clear(const AimPose& before,const AimPose& after)=0;
};
// The store holds ammo/reload/aim/burst state across slices. All arguments are
// frozen typed descriptor/command values; world operations stay native.
WeaponAimResult advance_weapon_aim(offline_simulation::weapons::Store&,
    offline_simulation::ActorKey, WeaponFrame&, const AimTarget&,
    const std::optional<GunMount>&, const HydraulicConfig&, WeaponWorld&);
FireResult advance_weapon_fire(offline_simulation::weapons::Store&,
    offline_simulation::ActorKey, const WeaponFrame&, const FireConditions&,
    const std::vector<offline_simulation::weapons::BurstEdge>& due_edges,
    std::int64_t start_us, std::int64_t end_us, const GunMount&,
    const DirectShot&, const std::vector<LaneBody>&, DirectWorld&,
    const std::optional<offline_simulation::weapons::LaunchRecord>& artillery_launch);
} // namespace wot::offline_bot_step
