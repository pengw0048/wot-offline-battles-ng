#pragma once
#include "stock_native_bot_physics_world.hpp"
#include "stock_native_bot_contacts.hpp"
#include "stock_native_bot_planner.hpp"
#include "stock_native_bot_navigation_scene.hpp"
#include "stock_native_bot_turret_obstacles.hpp"

namespace stock_native_bot_physics {
using ID=std::int64_t;
// This is the original fall/environment-module transaction boundary, now
// native. It updates combat and canonical Motion::alive/terminal state before
// returning, so subsequent glancing candidates observe the accepted outcome.
class ImpactTransactions {
public:
    virtual ~ImpactTransactions()=default;
    virtual int fall_damage(motion::State&,double impact_speed,
        const std::vector<Point>& witnesses,const Point& normal,bool ground_observed)=0;
};
struct Geometry {
    stock_native_bot_turret::MovingDescriptor turret;
    bool detailed_suspension=false,pitch_hull_aiming=false;
};
struct HostMemory {
    std::optional<std::array<double,3>> pose_sample;
    double last_world_impact_time=-1.e30;
    std::uint64_t slope_attempt=0;
};
struct MotionObservation {ID actor;motion::MotionStatus status;double before,after;};
struct SuspensionSnapshot {
    step::VerticalState state;
    step::VerticalMemory memory;
    std::optional<std::array<double,3>> pose_sample;
};
struct Context {
    std::map<ID,stock_native_bot_contacts::Actor>& actors;
    std::vector<stock_native_bot_contacts::Human>& humans;
    const std::vector<ID>& insertion_order;
    std::map<ID,Geometry>& geometry;
    std::map<ID,HostMemory>& memory;
    std::map<ID,step::VerticalMemory>& vertical_memory;
    std::map<ID,int>& slots;
    stock_native_bot_navigation::Grid& grid;
    stock_native_bot_navigation::Navigator& navigator;
    stock_native_bot_navigation::CandidateWorld& candidates;
    stock_native_bot_navigation::MotionPlan& motion_plan;
    stock_native_bot_planner::Planner& planner;
    stock_native_bot_turret::Obstacles& turrets;
    ImpactTransactions& impacts;
    stock_native_bot_navigation::BakeProof bake;
    double now=0.,elapsed=0.,server_time_ms=0.;
    std::set<ID> invalidated_decisions;
    std::vector<MotionObservation> observations;
};
class NativeHost final:public Host {
    Context& context_;
    std::map<ID,PhysicsWorld*> worlds_;
    std::vector<offline_math::Body> peers(ID,bool rotation,bool planning=false,bool wreck_push=false)const;
    offline_math::Body body(const motion::State&,const motion::Descriptor&,bool rotation)const;
    std::optional<std::array<double,4>> overflow(const motion::State&,const Point&,double)const;
    bool pose_safe(const Point&)const;
    void landing(motion::State&,double,bool,const std::vector<Point>&,const Point&,bool);
public:
    explicit NativeHost(Context& context):context_(context){}
    void attach(ID id,PhysicsWorld& world){worlds_[id]=&world;}
    void detach(ID id){worlds_.erase(id);}
    bool turret_blocks(const stock_native_world::TurretSweep&)const;
    double planning_rotation_fraction(ID,const Point&,double,double,bool wreck_push=false)const;
    double rotation_fraction(const motion::State&,const motion::Descriptor&,const Point&,double,double,const Vec&)override;
    bool pose_progress(const motion::State&,const Point&,double,const Point&,double)override;
    stock_native_world::TurretSweep turret_sweep(const motion::State&,const motion::State&)override;
    bool corridor(const motion::State&,double,bool,bool,std::optional<double>)override;
    void impact(motion::State&,double,double)override;
    void invalidate(ID,double,bool)override;
    void report_contact(const motion::State&,const Point&,const Point&,double,double)override;
    void motion_finished(const motion::State&,motion::MotionStatus,double,double)override;
    Vec slide(const motion::State&,const motion::Descriptor&,const Vec&,double&)override;
    Vec passive_move(ID,step::VerticalState&,double,double,double)override;
    void vertical_outcome(motion::State&,const step::VerticalOutcome&)override;
    // Root retains the release's cohort/cursor scheduling; one invocation
    // performs exactly one actor's target/pose update.
    bool update_slope_pose(motion::State&,int detail_tier,bool allow_ungrounded=false);
    bool guard_realised_pose(motion::State&,const Point& tick_position,bool tick_was_safe,
        double attempted_yaw,const std::optional<SuspensionSnapshot>& suspension_snapshot,
        bool navigation_hazards=true,std::optional<Point> navigation_pose={});
};
}
