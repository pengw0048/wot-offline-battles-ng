#pragma once
#include "stock_native_bot_motion.hpp"
#include "stock_native_world_pipeline.hpp"

namespace stock_native_bot_physics {
namespace motion=stock_native_bot_motion;
namespace step=wot::offline_bot_step;
using Point=motion::Point;
using Vec=motion::Vec;
// Root-owned contact/nav/terminal services. Every implementation is native;
// none of these methods is an interpreter or per-query capability callback.
class Host {
public:
    virtual ~Host()=default;
    virtual double rotation_fraction(const motion::State&,const motion::Descriptor&,
        const Point&,double,double,const Vec&)=0;
    virtual bool pose_progress(const motion::State&,const Point&,double,const Point&,double)=0;
    virtual stock_native_world::TurretSweep turret_sweep(const motion::State& before,
        const motion::State& after)=0;
    virtual bool corridor(const motion::State&,double,bool,bool,std::optional<double>)=0;
    virtual void impact(motion::State&,double,double)=0;
    virtual void invalidate(std::int64_t,double,bool)=0;
    virtual void report_contact(const motion::State&,const Point&,const Point&,double,double)=0;
    virtual void motion_finished(const motion::State&,motion::MotionStatus,double,double)=0;
    virtual Vec slide(const motion::State&,const motion::Descriptor&,const Vec&,double&)=0;
    virtual Vec passive_move(std::int64_t,step::VerticalState&,double,double,double)=0;
    virtual void vertical_outcome(motion::State&,const step::VerticalOutcome&)=0;
};
struct Config {
    std::optional<double> travel_forward,travel_reverse;
    std::optional<Point> suspension_motion_position;
    double turret_yaw=0.;
    bool detailed_suspension=false;
};
class PhysicsWorld final:public motion::World,public step::VerticalWorld {
    stock_native_world::Pipeline& pipeline_;
    Host& host_;
    motion::State& actor_;
    step::VerticalMemory& vertical_memory_;
    const step::SuspensionDescriptor& suspension_;
    int& support_repair_budget_;
    Config config_;
    double now_=0.;
    stock_native_world::Resolution last_resolution_;
    double driveCap(const motion::Descriptor&,double)const;
public:
    PhysicsWorld(stock_native_world::Pipeline&,Host&,motion::State&,
        const motion::Descriptor&,step::VerticalMemory&,
        const step::SuspensionDescriptor&,int& support_repair_budget,Config,double now);
    const stock_native_world::Resolution& last_resolution()const{return last_resolution_;}
    void configure(Config config){config_=std::move(config);}
    stock_motion_rotation::Trace consume_trace(){auto value=last_resolution_.trace;last_resolution_.trace.present=false;return value;}
    double rotation_fraction(const motion::State&,const motion::Descriptor&,const Point&,
        double,double,const Vec&)override;
    bool pose_progress(const motion::State&,const Point&,double,const Point&,double)override;
    bool turret_clear(const motion::State&,const Point&,double,const Point&,double)override;
    bool rotation(const motion::State&,const motion::Descriptor&,const Point&,double,double,
        double,double,double,const Vec&)override;
    bool corridor(const motion::State&,double,bool,bool,const std::optional<double>&)override;
    motion::MotionStatus motion(const motion::State&,const motion::Descriptor&,const Point&,
        double,double,double,double,bool,bool)override;
    void impact(motion::State&,double,double)override;
    void invalidate(std::int64_t,double,bool)override;
    void report_contact(const motion::State&,const Point&,const Point&,double,double)override;
    void motion_finished(const motion::State&,motion::MotionStatus,double,double)override;
    Vec slide(const motion::State&,const motion::Descriptor&,const Vec&,double&)override;
    bool vertical(motion::State&,const motion::Descriptor&,const Point&,double,double)override;
    std::optional<double> initial_ground(double,double,double)override;
    std::optional<double> ground(double,double,double,double,std::optional<double>)override;
    std::optional<double> basic_ground(double,double,double,std::optional<double>)override;
    bool turret_clear(const step::VerticalState&,const step::VerticalState&)override;
    Vec passive_move(step::VerticalState&,double,double,double)override;
};
}
