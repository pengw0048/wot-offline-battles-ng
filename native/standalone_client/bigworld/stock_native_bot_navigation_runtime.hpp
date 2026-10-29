#pragma once
#include "stock_native_bot_navigator.hpp"

namespace stock_native_bot_navigation {
using Vec=std::array<double,2>;
using LaneGroup=std::pair<int,std::string>;
using LaneGate=std::pair<int,bool>;
struct InitialSPG {
    std::string catalog,map,zone,cell;
    Point point{};
    double clearance=0.;
};
struct Strategy {
    std::string mode="route",route_id="direct",defense_base="own_base",parking_phase;
    std::optional<int> route_index;
    std::optional<bool> route_join;
    std::optional<Point> route_anchor;
    std::optional<double> throttle;
    std::optional<std::int64_t> target;
    std::optional<std::array<double,4>> move_area;
    std::string team_command;
};
struct OccupiedReceipt {double until=0;std::optional<Point> result;bool blocked=false;};
using OccupiedKey=std::pair<Cell,bool>;
struct NavigationActor {
    std::int64_t id=0;
    int team=0,slot=0;
    bool alive=true,traffic_waiting=false,artillery_intent=false,artillery_reproof=false;
    Point position{};
    double yaw=0.,speed=0.,half_length=0.,half_width=0.;
    double collision_half_width=0.,collision_half_length=0.;
    std::string local_route_id;
    std::vector<Vec> local_route;
    std::optional<int> local_route_index;
    std::optional<bool> local_route_join;
    std::optional<InitialSPG> spg;
    std::optional<LaneGroup> lane_group;
    std::optional<LaneGate> lane_gate;
    std::optional<Point> lane_origin,lane_goal;
    std::optional<Vec> lane_forward;
    double lane_desired=0.,row_desired=0.,lane_offset=0.,lane_row=0.;
    std::optional<std::tuple<LaneGroup,int,std::optional<bool>>> lane_segment;
    std::optional<std::pair<OccupiedKey,Point>> occupied_leg;
    std::map<OccupiedKey,OccupiedReceipt> occupied_cache;
    std::optional<std::pair<std::pair<std::string,std::array<double,4>>,std::optional<Point>>> radio_goal;
    std::optional<std::tuple<std::string,int,Point,std::uint64_t,double,bool>> wreck_receipt;
    bool route_wreck_blocked=false,occupied_route_point=false;
};
struct NavigationHuman {
    Point position{};
    double collision_half_width=0.,collision_half_length=0.;
    bool world_pose=true,participating=true;
};
struct TargetResult {
    Point target{};
    bool stop_at_target=false;
    std::string status="pending";
    std::optional<Point> progress;
};
std::vector<double> laneFallbacks(double);
std::vector<double> rowFallbacks(double);
std::vector<double> laneLayout(int count);
std::vector<double> rowLayout(int count);
class NavigationRuntime {
    Navigator& navigator_;
    Grid& grid_;
    std::vector<std::int64_t> cohort(std::int64_t,const LaneGroup&,const LaneGate&);
    Vec laneForward(const NavigationActor&,const Point&,const Strategy&)const;
    std::optional<Vec> legForward(const NavigationActor&,const Point&,const Point&,const Strategy&,bool)const;
    std::optional<Point> safeLaneGoal(const NavigationActor&,const Point&,Vec,double,double,double);
    std::vector<std::pair<Point,double>> physicalOccupancy(const NavigationActor&)const;
    std::optional<std::array<double,4>> overflow(const NavigationActor&,const Point&,double yaw)const;
    static std::string spgIdentity(const InitialSPG&);
public:
    std::map<std::int64_t,NavigationActor> actors;
    // The server replaces this complete snapshot atomically. Missing actors
    // in a nonempty snapshot cannot inherit stale local route membership.
    std::map<std::int64_t,Strategy> orders;
    std::vector<NavigationHuman> humans;
    explicit NavigationRuntime(Navigator&);
    std::pair<double,double> laneBinding(std::int64_t,const LaneGroup&,const Point&,const Strategy&);
    std::pair<Point,std::pair<int,int>> laneGoal(std::int64_t,const Point&,const Point&,const Strategy&,double,bool joining);
    Point laneTarget(std::int64_t,const Point&,const Point&,const Point&,const Strategy&,double);
    std::optional<Point> radioGoal(std::int64_t,const Point&,const Point&,const Strategy&);
    std::optional<Point> occupiedTarget(std::int64_t,const Point&,const Point&,double,bool direct_only=false);
    TargetResult target(std::int64_t,const Point&,Point goal,const Strategy&,double now);
    bool recoveryAllowed(std::int64_t,const Point&,int hard_grinds)const;
};
}
