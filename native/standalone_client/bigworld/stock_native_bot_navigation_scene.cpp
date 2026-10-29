#include "stock_native_bot_navigation_scene.hpp"
#include "navigation_query_stage.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_navigation {
namespace {
RayStatus rayStatus(stock_native_world::QueryStatus value)
{return value==stock_native_world::QueryStatus::Found?RayStatus::Found:value==stock_native_world::QueryStatus::Miss?RayStatus::Miss:RayStatus::Failed;}
}
NativeNavigationScene::NativeNavigationScene(stock_native_world::Pipeline&pipeline,RotationGeometry&rotations):pipeline_(pipeline),rotations_(rotations){}
std::optional<double> NativeNavigationScene::ground(double x,double z,double hint)
{
    double top=hint+8.;const double bottom=hint-18.;
    for(unsigned layer=0;layer<3;++layer){stock_native_world::Hit hit;
        if(pipeline_.navigation({x,top,z},{x,bottom,z},hit,true)!=stock_native_world::QueryStatus::Found)return {};
        const double height=hit.point[1];if(height<=hint+4.5&&hit.normal[1]>0.)return waterDepth({x,height,z})>.90?std::nullopt:std::optional<double>(height);
        const double next=height-.05;if(!(bottom<next&&next<top))return {};top=next;}return {};
}
Proof NativeNavigationScene::corridor(const Point&a,const Point&b,double width)
{
    const double dx=b[0]-a[0],dz=b[2]-a[2],length=std::sqrt(dx*dx+dz*dz);if(length<.1)return Proof::Clear;
    const double lx=dz/length,lz=-dx/length;
    for(double offset:{-width,0.,width})for(double height:{.9,1.6}){stock_native_world::Hit hit;
        auto status=pipeline_.navigation({a[0]+lx*offset,a[1]+height,a[2]+lz*offset},
            {b[0]+lx*offset,b[1]+height,b[2]+lz*offset},hit,true);
        if(status==stock_native_world::QueryStatus::Failed)return Proof::Unknown;
        if(status==stock_native_world::QueryStatus::Found)return Proof::Blocked;}
    return Proof::Clear;
}
Proof NativeNavigationScene::navigation(const Point&a,const Point&b,double cell,double up,double down,std::optional<double>slope)
{
    NavigationQuerySnapshot snapshot{};std::copy(a.begin(),a.end(),snapshot.start);std::copy(b.begin(),b.end(),snapshot.end);
    snapshot.cell_size=cell;snapshot.grade_up=up;snapshot.grade_down=down;snapshot.water_limit=.90;
    if(slope){snapshot.grade_up=snapshot.grade_down=std::min(*slope,std::tan(25.*3.14159265358979323846/180.));snapshot.sample_spacing=std::min(1.,cell*.42);}
    NavigationQueryEngine engine{};engine.context=this;
    engine.ray=[](void*raw,const double*start,const double*end,bool ground,double&height,double&normal){
        auto&self=*static_cast<NativeNavigationScene*>(raw);stock_native_world::Hit hit;
        auto status=self.pipeline_.navigation({start[0],start[1],start[2]},{end[0],end[1],end[2]},hit,true);
        if(status==stock_native_world::QueryStatus::Failed)throw std::runtime_error("native navigation query failed");
        if(status!=stock_native_world::QueryStatus::Found)return false;
        if(ground){height=hit.point[1];normal=hit.normal[1];}return true;};
    engine.water=[](void*raw,double x,double y,double z){return static_cast<NativeNavigationScene*>(raw)->waterDepth({x,y,z});};
    try{return static_cast<Proof>(navigation_query_run(snapshot,engine));}catch(...){return Proof::Unknown;}
}
PlanningRay NativeNavigationScene::ground(const Point&a,const Point&b,bool descriptor)
{
    stock_native_world::Hit hit;auto result=descriptor?pipeline_.navigation(a,b,hit,true):pipeline_.ground(a,b,hit);
    return {rayStatus(result),hit.point[1]};
}
RayStatus NativeNavigationScene::segment(const Point&a,const Point&b,bool descriptor)
{stock_native_world::Hit hit;return rayStatus(pipeline_.navigation(a,b,hit,descriptor));}
double NativeNavigationScene::waterDepth(const Point&p)
{
    double value=-1.;if(pipeline_.water({p[0],p[1]+20.,p[2]},{p[0],p[1]-5.,p[2]},value)!=stock_native_world::QueryStatus::Found||value<0.)return -1.;return 20.-value;
}
double NativeNavigationScene::rotationFraction(std::int64_t actor,const Point&p,double from,double to,bool wreck)
{return rotations_.fraction(actor,p,from,to,wreck);}
}

namespace stock_native_bot_navigation {
NativeMotionQueries::NativeMotionQueries(CandidateWorld&candidate,stock_native_world::Pipeline&pipeline,unsigned&budget):
    candidate_(candidate),pipeline_(pipeline),recast_budget_(budget){}
std::optional<MotionSample> NativeMotionQueries::direction(std::int64_t,const Point&p,double yaw,double speed,
    const ExactDescriptor&descriptor,std::optional<double>maximum)
{return MotionSample(probeDirection(candidate_,p,yaw,speed,descriptor.candidate,maximum));}
ReceiptResult NativeMotionQueries::receipt(std::int64_t,const Point&p,double yaw,double speed,
    const ExactDescriptor&descriptor,std::optional<double>maximum,double now)
{
    if(!descriptor.valid)return {};const double half_width=std::max(std::abs(descriptor.hull_minimum[0]),std::abs(descriptor.hull_maximum[0]))-.1;
    const double leading=speed<0.?-descriptor.hull_minimum[2]:descriptor.hull_maximum[2];if(half_width<=0.||leading<=0.)return {};
    double distance=15.;if(maximum){if(!std::isfinite(*maximum))return {ReceiptStatus::Blocked,{}};distance=std::min(distance,std::max(.5,*maximum));}
    auto old=pipeline_.actor();struct Restore {stock_native_world::Pipeline&pipeline;stock_native_world::ActorInputs actor;~Restore(){pipeline.actor(actor);}}restore{pipeline_,old};
    stock_native_world::ActorInputs actor;actor.mass=descriptor.mass;actor.now=now;actor.yaw=yaw;actor.speed=std::abs(speed);
    actor.kinetic_speed=speed<0.?descriptor.speed_reverse:descriptor.speed_forward;actor.has_kinetic_speed=true;actor.allow_kinetic=true;actor.commit_enabled=false;pipeline_.actor(actor);
    const double sine=std::sin(yaw),cosine=std::cos(yaw);
    for(double offset:{-half_width,0.,half_width}){
        const double sx=p[0]+cosine*offset-sine*.5,sz=p[2]-sine*offset-cosine*.5,ex=p[0]+cosine*offset+sine*distance,ez=p[2]-sine*offset+cosine*distance;
        for(double height:{.6,1.1,1.6}){const Point start{sx,p[1]+height,sz},end{ex,p[1]+height,ez};stock_native_world::Hit hit;
            const auto status=pipeline_.navigation(start,end,hit,false);if(status==stock_native_world::QueryStatus::Miss)continue;
            if(status==stock_native_world::QueryStatus::Failed)return {ReceiptStatus::Blocked,{}};
            const auto soft=pipeline_.softStatic(start,end,hit,false,true,recast_budget_);
            if(soft==stock_native_world::SoftStatus::Clear||soft==stock_native_world::SoftStatus::Kinetic)continue;
            if(soft==stock_native_world::SoftStatus::Deferred)return {ReceiptStatus::Deferred,{}};return {ReceiptStatus::Blocked,{}};}}
    return {ReceiptStatus::Clear,{p,yaw,distance,half_width,leading,speed<0.?-1:1}};
}
}
