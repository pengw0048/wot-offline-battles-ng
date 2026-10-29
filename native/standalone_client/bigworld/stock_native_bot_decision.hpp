#pragma once
#include "stock_native_bot_planning_snapshot.hpp"
#include "stock_native_bot_parking.hpp"

namespace stock_native_bot_decision {
namespace plan=stock_native_bot_planner;
namespace nav=stock_native_bot_navigation;
namespace strategy=stock_native_bot_strategy;
namespace parking=stock_native_bot_parking;
using Point=plan::Point;
using StrategicOrder=stock_native_bot_planning::StrategicOrder;
struct Identity {
    std::string kind;
    std::int64_t id=0;
    bool operator==(const Identity&b)const{return kind==b.kind&&id==b.id;}
    bool operator!=(const Identity&b)const{return !(*this==b);}
    bool operator<(const Identity&b)const{return std::tie(kind,id)<std::tie(b.kind,b.id);}
};
struct Target:strategy::Contact {
    Identity identity;
    bool alive=true,direct_visible=false,fresh_visible=false;
    std::optional<stock_native_bot_perception::Pose> observed_pose;
};
using Targets=std::map<std::int64_t,Target>;
struct RearLease {Point origin{};double start=0.;std::optional<double> retry_until;};
struct RearEpisode {std::int64_t target=0,fire_sequence=0;Identity identity;double start=0.;};
struct AngleAdjustment {double start=0.;std::optional<Point> destination;bool selected=false,reverse=false;};
struct Reposition {std::int64_t target=0;Identity identity;Point destination{};int shell=0;double fire_range=0.,deadline=0.;};
struct Obstruction {Point origin{};double yaw=0.,since=0.,last=0.;Identity target;};
struct RearPosition {Point anchor{},point{};int index=0;std::vector<Point> visited;double since=0.;};
struct DeploymentProgress {parking::Plan plan;double best=0.,since=0.;};
struct FireFailure {
    parking::Plan plan;Point origin{};std::int64_t fire_sequence=0;
    double elapsed=0.,last=0.,last_failure=0.;bool counting=true;
    std::vector<std::tuple<Identity,std::optional<double>,std::string>> proofs;
};
struct Actor {
    std::string class_tag;
    std::vector<std::array<double,2>> route;
    std::string route_id;
    std::optional<RearLease> rear_lease;
    std::optional<RearEpisode> rear_episode;
    std::optional<std::pair<Identity,double>> rear_retry;
    std::optional<std::pair<Identity,double>> rear_hull_aim;
    bool rear_motion_blocked=false;
    std::map<int,nav::RotationRefusal> blocked_rotations;
    std::optional<Identity> gun_refusal;
    std::optional<AngleAdjustment> gun_adjustment;
    std::map<Identity,double> gun_rejected;
    std::optional<Reposition> reposition;
    std::optional<parking::Plan> initial,arrived;
    std::optional<Point> selection_origin;
    std::optional<DeploymentProgress> deployment;
    std::optional<FireFailure> fire_failure;
    std::vector<Point> failed_parking;
    std::vector<std::pair<Point,double>> fire_failed_parking;
    std::optional<RearPosition> rear_position;
    std::optional<Obstruction> obstruction;
    double parking_check_at=-1.e9,occupied_retry_at=-1.e9,fire_retry_at=-1.e9;
    int parking_retries=0;
    bool parking_occupied=false,displaced_safe=false,fire_relocating=false;
    std::string position_event;
};
struct Input {
    plan::Input planning;
    double health=0.,maximum_health=1.,step=0.,reload_time=0.;
    std::int64_t fire_sequence=0;
    int shell_index=0,detail_tier=0;
    bool refresh_control=true,fixed_control=true,airborne=false,overturned=false;
    std::optional<double> evaluation_time;
};
struct Cache {
    bool server=false;
    std::uint64_t token=0;
    double deadline=0.,at=0.,interval=0.;
    plan::Command command;
    std::vector<Target> contacts;
    Targets targets;
};
struct Output {
    plan::Command command;
    std::vector<Target> contacts;
    Targets targets;
    bool due=false,reused=false,physical_hold=false;
    std::optional<double> deadline;
};
class World:public plan::World {
public:
    virtual void useOrder(std::int64_t,const nav::Strategy&)=0;
    virtual void replan(std::int64_t,const Point&,double)=0;
    virtual void blockedPlanner(const Point&,const plan::Command&)=0;
    virtual void cancelArtillery(std::int64_t)=0;
};
struct LaneReceipt {double at=0.;bool clear=false;};
struct FirePositionEvidence {
    bool ready=false,launch_clear=false,planning_failed=false,planning_clear=false,launch_failed=false,local_blockage=false;
    std::string reason;
    std::optional<double> completed;
    std::string launch_reason;
    std::optional<double> launch_completed;
    std::optional<Identity> launch_target;
};
class Decisions {
    plan::Planner& planner_;
    plan::Traffic& traffic_;
    strategy::Director& director_;
    parking::Parking& parking_;
    std::optional<double> angle_candidate_tick_;
    bool valid(std::int64_t,const Cache&)const;
    StrategicOrder routeOrder(const Input&,const StrategicOrder&);
    StrategicOrder rearOrder(const Input&,const StrategicOrder&,const Targets&);
    StrategicOrder angleOrder(const Input&,const StrategicOrder&,const Targets&,World&);
    std::pair<std::optional<StrategicOrder>,bool> repositionOrder(const Input&,const Targets&);
    parking::Plan retryDeployment(const Input&,parking::Plan,double,const std::vector<parking::Occupied>&,bool obstructed,World&);
    parking::Plan retryFire(const Input&,parking::Plan,const std::vector<parking::Occupied>&,World&);
    StrategicOrder artilleryOrder(const Input&,const StrategicOrder&,const Targets&,const std::vector<parking::Occupied>&,World&);
public:
    std::map<std::int64_t,Actor> actors;
    std::map<std::int64_t,StrategicOrder> orders;
    std::map<std::int64_t,std::uint64_t> tokens;
    std::map<std::int64_t,Cache> cache;
    std::map<int,std::map<std::string,std::vector<std::array<double,2>>>> routes;
    Decisions(plan::Planner&,plan::Traffic&,strategy::Director&,parking::Parking&);
    void install(const stock_native_bot_planning::PlanningSnapshot&);
    bool due(const Input&)const;
    void invalidate(std::int64_t);
    bool clearReposition(std::int64_t);
    StrategicOrder adaptOrder(const Input&,StrategicOrder,const Targets&,const std::vector<parking::Occupied>&,World&);
    Output prepare(const Input&,const std::vector<Target>&contacts,const Targets&,
        const std::vector<plan::Body>&neighbours,const std::vector<parking::Occupied>&,World&);
    // Root calls these at the corresponding native aim/launch boundaries.
    bool markReposition(const Input&,const plan::Command&,const Target*,double shot_yaw,
        std::int64_t next_fire,const std::string&blocker_kind,std::int64_t blocker,
        int blocker_team,const Point&blocker_position,std::optional<double>blocker_radius);
    void observeFirePosition(const Input&,const plan::Command&,const Target*,const FirePositionEvidence&);
    static StrategicOrder overlay(StrategicOrder,const Target*,const Point&);
    static void overlay(plan::Command&,const Target*,const Point&,bool stable_hull_face=false);
    static bool blockedDirectAim(plan::Command&,const Target*&,const LaneReceipt*,const Point& source,double now,
        double control_seconds,bool spg,bool refreshed_clear=false,bool stable_hull_face=false);
};
}
