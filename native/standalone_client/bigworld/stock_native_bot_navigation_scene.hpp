#pragma once
#include "stock_native_bot_candidate.hpp"
#include "stock_native_world_pipeline.hpp"

namespace stock_native_bot_navigation {
class RotationGeometry {
public:
    virtual ~RotationGeometry()=default;
    virtual double fraction(std::int64_t actor,const Point&,double from,double to,bool wreck_push)=0;
};
// Same-thread bridge to the native world owner. Query-stage callbacks below
// are C++ stack capabilities; no Python object or interpreter call is involved.
class NativeNavigationScene final:public World,public CandidateWorld {
    stock_native_world::Pipeline& pipeline_;
    RotationGeometry& rotations_;
public:
    NativeNavigationScene(stock_native_world::Pipeline&,RotationGeometry&);
    std::optional<double> ground(double x,double z,double hint)override;
    Proof corridor(const Point&,const Point&,double half_width)override;
    Proof navigation(const Point&,const Point&,double cell_size,double up,double down,std::optional<double> slope)override;
    PlanningRay ground(const Point&,const Point&,bool descriptor)override;
    RayStatus segment(const Point&,const Point&,bool descriptor)override;
    double waterDepth(const Point&)override;
    double rotationFraction(std::int64_t,const Point&,double,double,bool)override;
};
}

#include "stock_native_bot_motion_plan.hpp"
namespace stock_native_bot_navigation {
class NativeMotionQueries final:public MotionQueryWorld {
    CandidateWorld& candidate_;
    stock_native_world::Pipeline& pipeline_;
    unsigned& recast_budget_;
public:
    NativeMotionQueries(CandidateWorld&,stock_native_world::Pipeline&,unsigned&);
    std::optional<MotionSample> direction(std::int64_t,const Point&,double,double,
        const ExactDescriptor&,std::optional<double>)override;
    ReceiptResult receipt(std::int64_t,const Point&,double,double,
        const ExactDescriptor&,std::optional<double>,double now)override;
};
}
