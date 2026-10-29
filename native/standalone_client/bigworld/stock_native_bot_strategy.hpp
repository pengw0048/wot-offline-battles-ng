#pragma once
#include "stock_native_bot_planner.hpp"
#include <optional>

namespace stock_native_bot_strategy {
using Point=stock_native_bot_planner::Point;
struct Shell {int index=0;std::string kind;double penetration=0.,damage=0.;};
struct Profile {
    std::string class_tag,dominant_role;
    double desired_range=0.,fire_range=0.,armor=0.;
    std::map<std::string,double> roles;
    std::vector<Shell> shells;
};
struct Personality {double aggression=0.,caution=0.,initiative=0.,teamwork=0.;};
struct Agent {
    std::int64_t id=0;
    int team=0;
    std::uint64_t seed=0;
    Profile profile;
    Personality personality;
    std::string route_id="direct";
    bool has_route=false,route_started=false;
    std::vector<std::array<double,2>> waypoints;
    int waypoint_index=0,route_join_index=-1;
    std::optional<Point> position,route_join_anchor;
    std::optional<std::int64_t> target;
    double health_fraction=1.;
};
struct Contact {
    std::int64_t id=0;
    int team=0;
    Point position{};
    double health=0.,max_health=1.,armor=0.,speed=0.,last_seen=0.;
    std::string class_tag="mediumTank";
    bool visible=false;
    std::optional<std::set<std::int64_t>> shootable;
};
struct Decision:stock_native_bot_planner::Order {
    std::string route_id;
    int route_index=0;
    bool route_join=false;
    Point route_anchor{};
    double desired_range=0.,force_balance=0.;
};
int selectShell(const Profile&,const Contact&,const Personality&);
// Installation copies the exact registered director agents, including seeded
// personalities and assigned routes. No Python decision runs during a step.
class Director {
    std::map<int,std::map<std::int64_t,Contact>> contacts_;
    std::vector<Contact*> known(int team,double now);
    Contact* choose(Agent&,const Point&,double yaw,double now);
    Point route(Agent&,const Point&,double speed,double yaw);
    Point fallback(const Agent&,const Point&)const;
    Point anchor(const Agent&,const Point&)const;
    std::optional<double> hullAngle(const Agent&)const;
public:
    std::map<std::int64_t,Agent> agents;
    std::map<int,std::array<double,2>> bases;
    void restoreContacts(std::map<int,std::map<std::int64_t,Contact>> values) { contacts_=std::move(values); }
    void clear();
    void forget(std::int64_t);
    void observe(int observing_team,const Contact&);
    Decision order(std::int64_t,const Point&,double yaw,double speed,double health,double maximum_health,double now);
};
}
