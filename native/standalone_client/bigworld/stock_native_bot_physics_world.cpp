#include "stock_native_bot_physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_physics {
namespace world=stock_native_world;
namespace {
constexpr double pi=3.14159265358979323846;
world::ActorInputs inputs(const motion::State & state,const motion::Descriptor & descriptor,double speed,double now) {
    world::ActorInputs out;out.mass=descriptor.drive.mass;out.now=now;out.yaw=state.yaw;out.speed=speed;return out;
}
world::TurretSweep turret(Host & host,const motion::State & state,const Point & before,double from,const Point & after,double to) {
    auto start=state,end=state;start.position=before;start.yaw=from;end.position=after;end.yaw=to;return host.turret_sweep(start,end);
}
}
PhysicsWorld::PhysicsWorld(world::Pipeline & pipeline,Host & host,motion::State & actor,
    const motion::Descriptor &,step::VerticalMemory & memory,
    const step::SuspensionDescriptor & suspension,int & repair,Config config,double now):
    pipeline_(pipeline),host_(host),actor_(actor),vertical_memory_(memory),
    suspension_(suspension),support_repair_budget_(repair),config_(std::move(config)),now_(now) {}
double PhysicsWorld::driveCap(const motion::Descriptor & descriptor,double speed)const {
    double value=speed<0.?descriptor.drive.reverse:descriptor.drive.forward;
    const auto travel=speed<0.?config_.travel_reverse:config_.travel_forward;
    if(travel&&value>0.)value=std::max(value,*travel);
    if(!std::isfinite(value)||value<0.)throw std::runtime_error("Destructible drive speed cap is invalid");
    return speed<0.?-value:value;
}
double PhysicsWorld::rotation_fraction(const motion::State & state,const motion::Descriptor & descriptor,const Point & position,double from,double to,const Vec & translation) {
    return host_.rotation_fraction(state,descriptor,position,from,to,translation);
}
bool PhysicsWorld::pose_progress(const motion::State & state,const Point & start,double from,const Point & end,double to) {
    return host_.pose_progress(state,start,from,end,to);
}
bool PhysicsWorld::turret_clear(const motion::State & state,const Point & start,double from,const Point & end,double to) {
    return pipeline_.scene().turretSweep(turret(host_,state,start,from,end,to));
}
bool PhysicsWorld::rotation(const motion::State & state,const motion::Descriptor & descriptor,const Point & position,double from,double to,double dt,double now,double speed_cap,const Vec & translation) {
    now_=now;auto actor=inputs(state,descriptor,0.,now);pipeline_.actor(actor);
    world::RotationRequest request;auto & input=request.world;
    input.position=position;input.body.minimum=descriptor.body_minimum;input.body.maximum=descriptor.body_maximum;
    input.start_yaw=from;input.end_yaw=to;input.pitch=state.terrain_pitch;input.roll=state.roll;input.pivot_offset=0.;input.translation=translation;
    input.max_angle_step=pi/36.;input.max_steps=16;input.drivable_gradient=1.28;input.include_static=true;
    request.hull_minimum=descriptor.hull_minimum;request.hull_maximum=descriptor.hull_maximum;request.dt=dt;
    request.has_rotation_speed_cap=true;request.rotation_speed_cap=speed_cap;
    request.has_drive_speed_cap=speed_cap!=0.;request.drive_speed_cap=request.has_drive_speed_cap?std::abs(driveCap(descriptor,0.)):0.;
    // The fixed Bot caller already made its detached-turret proof immediately
    // before this resolver. Repeating it here would duplicate that query.
    last_resolution_=pipeline_.resolveRotation(request);
    return last_resolution_.status==world::ContactStatus::Clear||last_resolution_.status==world::ContactStatus::Crushed||last_resolution_.status==world::ContactStatus::Approach;
}
bool PhysicsWorld::corridor(const motion::State & state,double yaw,bool wet,bool shallow,const std::optional<double> & distance) {
    return host_.corridor(state,yaw,wet,shallow,distance);
}
motion::MotionStatus PhysicsWorld::motion(const motion::State & state,const motion::Descriptor & descriptor,const Point & position,double yaw,double speed,double dt,double now,bool passive,bool commit) {
    now_=now;auto actor=inputs(state,descriptor,speed,now);
    const bool active=!passive&&!state.airborne&&state.movement_direction*speed>0.;
    actor.has_kinetic_speed=active;actor.kinetic_speed=active?driveCap(descriptor,speed):0.;pipeline_.actor(actor);
    world::MotionRequest request;auto & input=request.world;
    request.geometry_position=position;
    for(unsigned i=0;i<3;++i)input.position[i]=float(position[i]);
    input.yaw=passive?state.yaw:yaw;input.speed=speed;input.dt=dt;input.pitch=state.terrain_pitch;input.roll=state.roll;
    input.motion_yaw=yaw+(speed<0.?pi:0.);input.passive=passive?1u:0u;input.airborne=state.airborne?1u:0u;
    input.bounds[0]=descriptor.body_minimum[0];input.bounds[1]=descriptor.body_maximum[0];input.bounds[2]=-descriptor.body_minimum[2];input.bounds[3]=descriptor.body_maximum[2];
    input.heights[0]=descriptor.body_minimum[1];input.heights[1]=descriptor.body_maximum[1];
    request.body_minimum=descriptor.body_minimum;request.body_maximum=descriptor.body_maximum;
    request.hull_minimum=descriptor.hull_minimum;request.hull_maximum=descriptor.hull_maximum;
    request.allow_crush_drive=active;request.commit_enabled=commit;
    const double travel=std::abs(speed)*dt;const double direction=passive?input.motion_yaw:input.yaw+(speed<0.?pi:0.);
    const Point end{{position[0]+std::sin(direction)*travel,position[1],position[2]+std::cos(direction)*travel}};
    const auto proof=turret(host_,state,position,input.yaw,end,input.yaw);request.turret=&proof;
    last_resolution_=pipeline_.resolveMotion(request);
    switch(last_resolution_.status) {
    case world::ContactStatus::Clear:case world::ContactStatus::Approach:return motion::MotionStatus::Clear;
    case world::ContactStatus::Crushed:return motion::MotionStatus::Crushed;
    default:return motion::MotionStatus::Hard;
    }
}
void PhysicsWorld::impact(motion::State & state,double speed,double now){host_.impact(state,speed,now);}
void PhysicsWorld::invalidate(std::int64_t id,double yaw,bool motion){host_.invalidate(id,yaw,motion);}
void PhysicsWorld::report_contact(const motion::State & state,const Point & before,const Point & after,double yaw,double now){host_.report_contact(state,before,after,yaw,now);}
void PhysicsWorld::motion_finished(const motion::State & state,motion::MotionStatus status,double before,double after){host_.motion_finished(state,status,before,after);}
Vec PhysicsWorld::slide(const motion::State & state,const motion::Descriptor & descriptor,const Vec & move,double & fraction){return host_.slide(state,descriptor,move,fraction);}
std::optional<double> PhysicsWorld::basic_ground(double x,double z,double hint,std::optional<double> maximum) {
    double start=hint+2.;
    for(unsigned layer=0;layer<4;++layer) {
        world::Hit hit;const auto status=pipeline_.ground({{x,start,z}},{{x,-1000.,z}},hit);
        if(status!=world::QueryStatus::Found)return {};
        const double height=hit.point[1];if((!maximum||height<=*maximum)&&hit.normal[1]>.5)return height;
        const double next=height-.05;if(next<=-1000.+.01)return {};start=next;
    }
    return {};
}
std::optional<double> PhysicsWorld::initial_ground(double x,double z,double hint){return basic_ground(x,z,hint,{});}
std::optional<double> PhysicsWorld::ground(double x,double z,double low,double high,std::optional<double> flat) {
    if(high<low)std::swap(high,low);double start=high;
    for(unsigned layer=0;layer<3;++layer) {
        world::Hit hit;const auto status=pipeline_.ground({{x,start,z}},{{x,low,z}},hit);
        if(status==world::QueryStatus::Failed)throw std::runtime_error("Native suspension ground query failed");
        if(status==world::QueryStatus::Miss)return {};
        const double height=hit.point[1],norm=std::sqrt(hit.normal[0]*hit.normal[0]+hit.normal[1]*hit.normal[1]+hit.normal[2]*hit.normal[2]);
        if(!std::isfinite(height)||!std::isfinite(norm)||norm<=.000001)throw std::runtime_error("Native suspension ground hit is malformed");
        const double up=hit.normal[1]/norm;
        if(low-.01<=height&&height<=high+.01&&up>.5&&(!flat||height<=*flat+.01||up<.995))return height;
        const double next=height-.02;if(next<=low+.01||next>=start)break;start=next;
    }
    return {};
}
bool PhysicsWorld::turret_clear(const step::VerticalState & before,const step::VerticalState & after) {
    auto a=actor_,b=actor_;static_cast<step::VerticalState &>(a)=before;static_cast<step::VerticalState &>(b)=after;
    return pipeline_.scene().turretSweep(host_.turret_sweep(a,b));
}
Vec PhysicsWorld::passive_move(step::VerticalState & state,double x,double z,double dt){return host_.passive_move(actor_.id,state,x,z,dt);}
bool PhysicsWorld::vertical(motion::State & state,const motion::Descriptor &,const Point & previous,double previous_yaw,double dt) {
    step::VerticalOutcome outcome;
    if(config_.detailed_suspension&&!state.suspension_disabled)outcome=step::advance_vertical(state,vertical_memory_,suspension_,*this,state.yaw,config_.turret_yaw,previous,config_.suspension_motion_position,dt);
    else outcome=step::advance_basic_vertical(state,suspension_,*this,previous,dt,support_repair_budget_);
    host_.vertical_outcome(state,outcome);(void)previous_yaw;return outcome.blocked;
}
}
