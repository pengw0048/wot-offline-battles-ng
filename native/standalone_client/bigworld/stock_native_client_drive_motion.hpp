#pragma once
#include "stock_native_client_drive_publication.hpp"
namespace stock_native_client_drive {
struct MotionConfig {
    std::optional<double> travel_forward,travel_reverse;
    double maximum_angle_step=0.,drivable_gradient=0.;unsigned maximum_steps=0;
};
stock_native_world::ContactResult mergePlayerProposals(const stock_native_world::ContactResult&catalog,
    const stock_native_world::ContactResult&tree,bool tree_consumed);
// Concrete visible-player motion proposals on the sole native scene. Unlike
// the Bot resolver this preserves the player proposal/independent-tree/commit/
// input-barrier/native-recast order before any chassis pose is committed.
class SceneMotion {
    stock_native_world::Pipeline&pipeline_;
    PublicationLedger&publication_;
    const PlayerDescriptor&descriptor_;
    const Controls&controls_;
    MotionConfig config_;
    double now_=0.;
    std::uint64_t replacement_generation_=~std::uint64_t(0);
    bool replacement_active_=false;
    double driveCap(double speed)const;
    bool commitTree(PlayerState&,const stock_native_world::ContactResult&,
        const Point&,double,const Point&,double,double,double,double pivot);
    bool commitCatalog(PlayerState&,const stock_native_world::ContactResult&,
        const Point&,double,const Point&,double,double,double,std::optional<double>catalog_speed={});
    void barrier(const PlayerState&,const Point&,double);
    void trace(PlayerState&,const stock_motion_rotation::Trace&);
    void result(PlayerState&,const stock_native_world::ContactResult&);
public:
    SceneMotion(stock_native_world::Pipeline&,PublicationLedger&,const PlayerDescriptor&,
        const Controls&,MotionConfig);
    void clock(double now){now_=now;}
    bool translation(PlayerState&,const Point&,double travel_yaw,double speed,double dt,
        bool powered,std::optional<double>hull_yaw={});
    bool rotation(PlayerState&,const Point&,double from,const Point&,double to,
        double speed,double dt,double traverse_factor);
};
}
