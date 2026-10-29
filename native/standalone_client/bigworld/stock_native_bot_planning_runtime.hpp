#pragma once
#include "stock_native_bot_decision.hpp"
#include "stock_native_bot_navigation_scene.hpp"

namespace stock_native_bot_planning {
namespace nav=stock_native_bot_navigation;
namespace decision=stock_native_bot_decision;
namespace plan=stock_native_bot_planner;
namespace perception=stock_native_bot_perception;
using ID=std::int64_t;
using Key=offline_simulation::ActorKey;
using TargetMetadata=std::map<Key,decision::Target>;
class PlanningHost {
public:
    virtual ~PlanningHost()=default;
    virtual double planningRotationFraction(ID,const nav::Point&,double,double,bool wreck_push)=0;
    virtual void cancelArtillery(ID)=0;
};
struct LiveActor {
    decision::Input decision;
    double water_depth=-1.;
    int hard_grinds=0;
    bool artillery_intent=false,artillery_reproof=false;
    std::map<int,nav::RotationRefusal> rejected_rotations;
};
// One native round owner. Update rows contain live fields only; route/lane,
// driver, candidate, receipt, visibility and decision histories stay here.
class PlanningRuntime final:public decision::World,public nav::RotationGeometry {
    PlanningHost& host_;
    perception::Perception& perception_;
    nav::BakeProof bake_;
    unsigned recast_budget_=24;
    nav::NativeNavigationScene scene_;
    nav::Grid grid_;
    nav::Navigator navigator_;
    nav::NavigationRuntime navigation_;
    nav::NativeMotionQueries motion_queries_;
    nav::MotionPlan motion_plan_;
    plan::Planner planner_;
    plan::Traffic traffic_;
    stock_native_bot_strategy::Director director_;
    stock_native_bot_parking::Parking parking_;
    decision::Decisions decisions_;
    std::map<ID,LiveActor> actors_;
    std::map<ID,nav::ExactDescriptor> descriptors_;
    std::map<ID,nav::CandidateState> candidate_states_;
    std::map<ID,std::unique_ptr<nav::CandidateProbe>> candidate_probes_;
    std::map<ID,nav::Strategy> selected_orders_;
    bool open_=false;
    nav::CandidateProbe& candidate(ID);
    std::vector<stock_native_bot_parking::Occupied> occupancy(ID)const;
    void syncParking(ID);
public:
    PlanningRuntime(const PlanningSnapshot&,stock_native_world::Pipeline&,
        perception::Perception&,PlanningHost&);
    ~PlanningRuntime();
    PlanningRuntime(const PlanningRuntime&)=delete;
    PlanningRuntime& operator=(const PlanningRuntime&)=delete;
    void replaceOrders(std::map<ID,StrategicOrder>,std::map<ID,std::uint64_t> tokens);
    void descriptor(ID,const nav::ExactDescriptor&);
    void update(LiveActor);
    void humans(std::vector<nav::NavigationHuman>);
    void beginFrame(double elapsed,const std::set<ID>& live);
    void endFrame();
    // Perception accepts typed frozen descriptor/config and current pose rows.
    // No Python contact materialization occurs at this boundary.
    void beginPerception(const offline_simulation::TickToken&,const std::vector<perception::Actor>&,
        double now,int budget,bool include_humans,std::uint64_t phase);
    void updatePerception(perception::Actor);
    void finishPerception(bool observe_humans=true);
    decision::Output prepare(ID,const TargetMetadata&,const std::vector<plan::Body>& neighbours);
    plan::Command safeControls(ID,const plan::Command&,const std::vector<plan::Body>&,double stopping_distance);
    nav::ProbePlan selectedMotion(nav::MotionPlanInput);
    void invalidate(ID id){decisions_.invalidate(id);}
    nav::Grid& grid(){return grid_;}
    nav::Navigator& navigator(){return navigator_;}
    nav::NavigationRuntime& navigation(){return navigation_;}
    nav::CandidateWorld& candidates(){return scene_;}
    nav::MotionPlan& motionPlan(){return motion_plan_;}
    plan::Planner& planner(){return planner_;}
    plan::Traffic& traffic(){return traffic_;}
    decision::Decisions& decisions(){return decisions_;}
    const decision::Decisions& decisions()const{return decisions_;}
    perception::Perception& perception(){return perception_;}
    const nav::BakeProof& bake()const{return bake_;}
    bool direction(ID,double,const plan::Optional<double>&,double,const plan::Optional<double>&)override;
    bool pose(ID,double,bool)override;
    plan::Navigation navigate(const plan::Input&,const plan::Order&,const nav::Point&)override;
    void useOrder(ID,const nav::Strategy&)override;
    void replan(ID,const nav::Point&,double)override;
    void blockedPlanner(const nav::Point&,const plan::Command&)override;
    void cancelArtillery(ID)override;
    double fraction(ID,const nav::Point&,double,double,bool)override;
};
}
