#pragma once
#include "stock_native_bot_planning_runtime.hpp"
#include "stock_native_bot_combat_runtime.hpp"
#include <deque>

namespace stock_native_bot_observation {
namespace planning=stock_native_bot_planning;
namespace decision=stock_native_bot_decision;
namespace nav=stock_native_bot_navigation;
namespace perception=stock_native_bot_perception;
namespace weapon=wot::offline_bot_step;
using ID=std::int64_t;
using Key=offline_simulation::ActorKey;
using Point=nav::Point;
using LaneKey=std::tuple<ID,std::string,ID>;
using TeamKey=std::tuple<int,std::string,ID>;
struct Actor {
    Key key;
    decision::Target target;
    weapon::AimPose pose;
    double speed=0.;
    bool pitch_locked=false;
    double static_pitch=0.;
};
struct Descriptor {
    bool valid=false,valid_pitch=false;
    std::optional<weapon::GunMount> mount;
    std::vector<weapon::AimSample> samples;
    Point hull_minimum{},hull_maximum{};
    offline_simulation::weapons::AimConfig aim;
    double minimum_yaw=0.,maximum_yaw=0.;
};
struct CoverCandidate {
    std::string id;
    Point position{},peek_position{};
    double travel_distance=0.,route_alignment=0.,enemy_occlusion=0.,exposure=0.,slope=0.,water=0.,ally_congestion=0.;
    bool peek_feasible=true,escape_feasible=true;
};
struct Affordance {ID bot_id=0,target_id=0;std::string target_kind;std::vector<CoverCandidate> candidates;};
struct RadioRecipient {Key actor;double time_left=0.;};
struct Contact {
    int observing_team=0,target_team=0;
    Key target;
    bool visible=false,fresh=false;
    double time_left=0.;
    std::vector<RadioRecipient> radio_recipients;
    std::vector<ID> visible_by_player_ids,visible_by_bot_ids,shootable_by_bot_ids,threatened_bot_ids;
    Point position{};
    int health=0,max_health=1;
    std::string class_tag;
    double armor=0.;
};
struct RadioLink {Key actor;std::vector<Key> allies;};
struct Observation {
    std::vector<Contact> contacts;
    std::vector<RadioLink> radio_links;
    std::vector<std::pair<ID,double>> player_vision_ranges;
    std::vector<Affordance> affordances;
};
class World {
public:
    virtual ~World()=default;
    // Uses the same direct exposed-part or SPG request owner as final fire.
    virtual bool outgoingLane(const Actor& source,const Actor& target,double now)=0;
    // Exact flags-128 tactical segment, without visibility-derived geometry.
    virtual bool staticClear(const Point&,const Point&)=0;
    virtual std::optional<double> ground(double x,double z,double hint)=0;
    virtual double waterDepth(const Point&)=0;
};
struct Schedule {
    bool collect_observation=false,refresh_shot_lanes=false,collect_cover_jobs=false,cover_reserved=false;
    bool shot_lane_refresh_due=false;
    double cycle_time=0.;
};
class Runtime {
    weapon::CombatSharedRuntime& shared_;
    perception::Perception& perception_;
    decision::Decisions& decisions_;
    nav::Grid& grid_;
    World& world_;
    std::map<Key,Actor> actors_;
    std::map<Key,Descriptor> descriptors_;
    std::map<TeamKey,bool> team_visibility_;
    struct Entry {Actor target;bool visible=false;std::set<ID> shootable,players,bots;};
    std::map<TeamKey,Entry> entries_;
    std::map<LaneKey,int> priorities_;
    std::map<LaneKey,double> deadlines_;
    std::map<LaneKey,weapon::CombatSharedRuntime::LaneReceipt> incoming_;
    std::deque<LaneKey> work_;
    std::set<LaneKey> work_set_;
    std::set<TeamKey> enqueued_targets_;
    std::optional<double> work_cycle_;
    std::map<int,std::vector<ID>> source_signature_;
    std::map<int,LaneKey> selected_cursors_;
    std::size_t work_cursor_=0,cover_cursor_=0;
    struct CoverJob {double ready_at=0.;ID id=0;Actor source,target;Point route{};std::vector<Point> allies;};
    std::vector<CoverJob> cover_queue_,cover_jobs_;
    std::vector<Affordance> cover_results_;
    std::map<ID,std::uint64_t> cover_phases_;
    double next_observation_=0.,next_lane_=0.,next_cover_=0.,now_=0.,control_seconds_=.05;
    bool refresh_=false,publish_=false,fixed_=true;
    int incoming_budget_=0;
    Schedule schedule_;
    void enqueue(const LaneKey&,double);
    void prepareLanes(double);
    std::size_t serviceLanes();
    bool current(const CoverJob&)const;
    std::optional<bool> incoming(const Actor&,const Actor&);
    std::vector<CoverCandidate> cover(const CoverJob&);
    Observation pack();
public:
    Runtime(weapon::CombatSharedRuntime&,perception::Perception&,decision::Decisions&,nav::Grid&,World&);
    void descriptor(Key key,Descriptor value){descriptors_[key]=std::move(value);}
    void replaceActors(std::vector<Actor>);
    void update(Actor actor){actors_[actor.key]=std::move(actor);}
    Schedule begin(double now,bool refresh_control,bool publish,bool fixed_control,double control_seconds);
    // Call after Perception.observeHumans(), before the ordered Bot loop.
    void observeHumans();
    // Call after current-pose target refresh, before blocked-direct-aim overlay.
    void observeActor(ID,const std::vector<decision::Target>& contacts,const stock_native_bot_planner::Command&);
    // Call after weapon/motion with that slice's selected target and command.
    void queueCover(ID,const decision::Target*,const stock_native_bot_planner::Command&);
    // Called before the final support/contact settlement. A value is emitted
    // only at the fixed source's observation publication boundary.
    std::optional<Observation> finish();
    static double phase(const LaneKey&);
    const Schedule& schedule()const{return schedule_;}
    std::size_t pendingLanes()const{return work_set_.size();}
    std::size_t pendingCover()const{return cover_queue_.size();}
};
}
