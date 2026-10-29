#include "stock_native_bot_physics_host.hpp"
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
using namespace stock_native_bot_physics;
namespace nav=stock_native_bot_navigation;
namespace world=stock_native_world;
struct Scene:world::Scene {
    world::QueryStatus segment(const Point&a,const Point&b,std::uint16_t,const world::Filter&,world::Hit&hit)override {
        const double y=float(.05*a[0]+.1*a[2]);if(a[1]>=y&&b[1]<=y){hit.point={{a[0],y,a[2]}};hit.normal={{0.,1.,0.}};return world::QueryStatus::Found;}return world::QueryStatus::Miss;
    }
    world::QueryStatus material(const Point&,const Point&,const Point&,world::MaterialHit&)override{return world::QueryStatus::Miss;}
    world::QueryStatus water(const Point&,const Point&,double&)override{return world::QueryStatus::Miss;}
    world::Admission refresh(const world::Key&,world::Registry&)override{return world::Admission::Pending;}
    bool unresolved(const world::Key&,std::vector<world::Box>&)override{return false;}
    bool destroy(const world::Destruction&)override{return false;}
    bool treeChunks(const std::vector<world::Box>&,std::vector<std::uint32_t>&)override{return false;}
    bool turretSweep(const world::TurretSweep&)override{return true;}
};
struct Queries:nav::World,nav::CandidateWorld,nav::MotionQueryWorld {
    std::optional<double> ground(double,double,double)override{return 0.;}
    nav::Proof corridor(const Point&,const Point&,double)override{return nav::Proof::Clear;}
    nav::Proof navigation(const Point&,const Point&,double,double,double,std::optional<double>)override{return nav::Proof::Clear;}
    nav::PlanningRay ground(const Point&,const Point&,bool)override{return {};}
    nav::RayStatus segment(const Point&,const Point&,bool)override{return nav::RayStatus::Miss;}
    double waterDepth(const Point&)override{return -1.;}
    double rotationFraction(std::int64_t,const Point&,double,double,bool)override{return 1.;}
    std::optional<nav::MotionSample> direction(std::int64_t,const Point&,double,double,const nav::ExactDescriptor&,std::optional<double>)override{return {};}
    nav::ReceiptResult receipt(std::int64_t,const Point&,double,double,const nav::ExactDescriptor&,std::optional<double>,double)override{return {};}
};
struct Impacts:ImpactTransactions {double speed=-1.;bool observed=false;int calls=0;
    int fall_damage(motion::State&,double value,const std::vector<Point>&,const Point&,bool ground)override {speed=value;observed=ground;++calls;return 1;}
};
int main(int argc,char**) {
    std::cout<<std::setprecision(17);Queries queries;offline_navigation::MapInput map;map.width=map.height=41;map.cell_size=5.;map.origin_x=map.origin_z=-100.;map.has_bounds=true;map.bounds={{-100.,-100.,100.,100.}};map.heights.resize(1681,0.);map.hazards.resize(1681,0);map.links.resize(1681,255);
    nav::Grid grid(map,queries,.5,.5);nav::Navigator navigator(grid);nav::MotionPlan plan(queries);stock_native_bot_planner::Planner planner;
    motion::State state;state.id=7;motion::Descriptor descriptor;descriptor.half_length=3.5;descriptor.half_width=1.8;descriptor.collision_shape={{1.8,3.5,0.,2.}};
    std::map<ID,stock_native_bot_contacts::Actor> actors{{7,{&state,&descriptor,1,{},{},{},{},0}}};std::vector<stock_native_bot_contacts::Human> humans;std::vector<ID> order{7};
    std::map<ID,Geometry> geometry;std::map<ID,HostMemory> memory;std::map<ID,step::VerticalMemory> vertical;std::map<ID,int> slots{{7,0}};
    stock_native_bot_turret::Obstacles turrets;Impacts impacts;
    Context context{actors,humans,order,geometry,memory,vertical,slots,grid,navigator,queries,plan,planner,turrets,impacts,{},0.,0.,0.,{}, {}};NativeHost host(context);
    Scene scene;world::Registry registry;world::Pipeline pipeline(registry,scene,0);step::SuspensionDescriptor suspension;int budget=0;
    PhysicsWorld physics(pipeline,host,state,descriptor,vertical[7],suspension,budget,{},0.);host.attach(7,physics);
    auto& body=geometry[7].turret;body.chassis.bounds={{{-1.,0.,-2.}},{{1.,1.,2.}}};body.hull=body.chassis;
    if(argc>1) {
        state.position={{99.,0.,0.}};state.yaw=0.;state.speed=17.;state.push_x=6.;state.alive=false;state.pitch=.8;
        SuspensionSnapshot snapshot;snapshot.state.pitch=.3;snapshot.state.alive=true;snapshot.state.speed=40.;snapshot.state.basic_continuous_support=true;snapshot.pose_sample={{1.,2.,3.}};
        assert(host.guard_realised_pose(state,{{98.,0.,0.}},false,.9,snapshot));assert(!state.alive&&state.speed==0.&&state.pitch==.3&&state.basic_continuous_support&&memory[7].pose_sample==snapshot.pose_sample);
        state.alive=true;state.air_lateral_x=3.;state.air_lateral_z=4.;step::VerticalOutcome outcome;outcome.landing_impact=12.;outcome.ground_observed=true;
        host.vertical_outcome(state,outcome);assert(impacts.calls==1&&impacts.speed==13.&&impacts.observed&&state.slide_speed==5.&&state.air_lateral_x==0.);
        state.air_lateral_x=3.;state.air_lateral_z=4.;outcome.normal_impact=true;host.vertical_outcome(state,outcome);assert(impacts.speed==12.);
        state.alive=false;host.vertical_outcome(state,outcome);assert(impacts.calls==2);
        assert(context.invalidated_decisions.count(7));std::cout<<"ok\n";return 0;
    }
    int tier,exact,marker;
    while(std::cin>>state.position[0]>>state.position[1]>>state.position[2]>>state.yaw>>state.terrain_pitch>>state.roll>>state.hydraulic_pitch>>descriptor.half_length>>descriptor.half_width>>tier>>exact>>marker) {
        double a,b;std::cin>>a>>b;state.pitch=state.terrain_pitch+state.hydraulic_pitch;state.alive=true;state.airborne=false;state.grounded_once=true;
        geometry[7].pitch_hull_aiming=!exact;memory[7].pose_sample.reset();state.slope_target.reset();state.slope_basis_yaw.reset();
        if(marker){memory[7].pose_sample={{state.position[0],state.position[2],state.yaw}};state.slope_target={{a,b}};}
        const bool changed=host.update_slope_pose(state,tier);
        std::cout<<'['<<changed<<','<<state.terrain_pitch<<','<<state.pitch<<','<<state.roll<<',';
        if(state.slope_target)std::cout<<'['<<(*state.slope_target)[0]<<','<<(*state.slope_target)[1]<<']';else std::cout<<"null";
        std::cout<<',';if(memory[7].pose_sample){const auto&m=*memory[7].pose_sample;std::cout<<'['<<m[0]<<','<<m[1]<<','<<m[2]<<']';}else std::cout<<"null";std::cout<<"]\n";
    }
}
