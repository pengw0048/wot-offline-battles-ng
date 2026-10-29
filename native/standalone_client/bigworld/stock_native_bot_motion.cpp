#include "stock_native_bot_motion.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_native_bot_motion {
namespace {
constexpr double pi=3.14159265358979323846;
double angle(double value)
{while(value>pi)value-=2.*pi;while(value<-pi)value+=2.*pi;return value;}
int sign(double value){return value>0.?1:value<0.?-1:0;}
double bleed(double value,double budget)
{return budget<=0.?value:value>budget?value-budget:value<-budget?value+budget:0.;}
bool pass(MotionStatus status){return status==MotionStatus::Clear||status==MotionStatus::Crushed;}
struct HardResponse {double speed=0.;Point position{};bool deflected=false;};
HardResponse hard_contact(State& state,const Descriptor& descriptor,const Point& position,
    double yaw,double speed,double dt,double now,World& world)
{
    std::optional<double> slide_yaw;
    for(const auto delta:descriptor.glancing_yaws) {
        const double candidate=yaw+delta;
        if(pass(world.motion(state,descriptor,position,candidate,speed,dt,now,true,false))) {
            if(pass(world.motion(state,descriptor,position,candidate,speed,dt,now,true,true)))
                slide_yaw=candidate;
            break;
        }
    }
    Point accepted=position;
    if(!slide_yaw) {
        speed*=std::pow(descriptor.hard_brake_decay,std::max(0.,dt)*60.);
        if(std::abs(speed)<descriptor.hard_stop_speed)speed=0.;
    } else {
        if(state.hard_grinds<=0)speed*=descriptor.hard_entry_factor;
        speed*=std::pow(descriptor.hard_slide_decay,std::max(0.,dt)*60.);
        accepted[0]+=std::sin(*slide_yaw)*speed*std::max(0.,dt);
        accepted[2]+=std::cos(*slide_yaw)*speed*std::max(0.,dt);
    }
    state.hard_grinds=descriptor.hard_grind_ticks;
    return {speed,accepted,bool(slide_yaw)};
}
}
void validate(const Descriptor& d)
{
    const auto& law=d.drive;
    for(double value:law.t)if(!std::isfinite(value))throw std::invalid_argument("Nonfinite drive tuning");
    for(double value:{law.mass,law.power,law.ratio,law.forward,law.reverse,law.friction,law.brake,
            law.rotation,d.half_length,d.half_width,d.track_center})
        if(!std::isfinite(value))throw std::invalid_argument("Nonfinite vehicle drive descriptor");
    if(law.mass<=0.||law.t[11]<=0.||law.t[14]<=law.t[13]||law.t[18]<=0.||law.t[24]<=0.||
        law.terrain[0]<=0.||law.terrain[1]<=0.||law.terrain[2]<=0.||
        d.half_length<=0.||d.half_width<=0.)throw std::invalid_argument("Invalid vehicle drive descriptor");
}
double retained_contact_speed(double speed,double forced)
{return speed>=0.?std::max(0.,std::min(speed,forced)):std::min(0.,std::max(speed,forced));}
double stopping_distance(const Descriptor& d,double speed,double pitch,double steering,
    double stop_speed,double time_step)
{
    double current=speed,distance=0.;
    if(std::abs(current)<=stop_speed)return 0.;
    for(int i=0;i<4096;++i) {
        const double next=d.drive.longitudinal(current,0.,steering!=0.,pitch,time_step,false,0,false,false);
        if(!std::isfinite(next))return std::numeric_limits<double>::infinity();
        if(std::abs(next)<=stop_speed)return distance+std::abs(next)*time_step;
        if(std::abs(next)>=std::abs(current))return std::numeric_limits<double>::infinity();
        distance+=std::abs(next)*time_step;current=next;
    }
    return std::numeric_limits<double>::infinity();
}
Trace advance(State& s,const Descriptor& d,const Input& in,World& world)
{
    Trace out;
    const Point position=s.position;
    double throttle=in.throttle,turn=in.turn,slope=in.slope_pitch;
    bool path_clear=in.path_clear;
    const int steer=std::abs(turn)>.01?sign(turn):0;
    s.movement_direction=throttle>.01?1:throttle<-.01?-1:0;
    s.rotation_direction=steer;s.contact_motor_turn.reset();
    double turn_speed=in.siege_locked?0.:d.drive.traverse(s.turn_speed,turn,s.speed,in.dt,0,throttle);
    const double old_yaw=s.yaw;
    double candidate=angle(old_yaw+turn_speed*in.dt);
    if(std::abs(angle(candidate-old_yaw))>1.e-9) {
        out.rotation_fraction=world.rotation_fraction(s,d,position,old_yaw,candidate);
        if(out.rotation_fraction<1.) {
            candidate=old_yaw+angle(candidate-old_yaw)*out.rotation_fraction;
            turn_speed=0.;s.rotation_contact_blocked=true;
            s.contact_motor_turn=steer;s.rotation_direction=0;
        }
    }
    out.rotation_blocked=!world.pose_progress(s,position,old_yaw,position,candidate)||
        (std::abs(angle(candidate-old_yaw))>1.e-8&&
         !world.turret_clear(s,position,old_yaw,position,candidate));
    bool rotation_refused=false;
    if(!out.rotation_blocked&&!s.airborne&&std::abs(angle(candidate-old_yaw))>1.e-8) {
        rotation_refused=!world.rotation(s,d,position,old_yaw,candidate,in.dt,in.now,d.drive.rotation);
        out.rotation_blocked=rotation_refused;
        if(rotation_refused)s.rotation_refused_until=in.now+2.;
    }
    if(out.rotation_blocked) {
        world.invalidate(s.id,candidate,false);
        const double delta=angle(candidate-old_yaw);
        if(std::abs(delta)>1.e-8)s.rejected_rotations[delta>0.?1:-1]={position,old_yaw,delta,in.now+2.};
        turn_speed=0.;candidate=old_yaw;s.rotation_direction=0;
        if(std::abs(turn)>.01&&std::abs(throttle)>.01) {
            throttle=0.;s.movement_direction=0;s.contact_motor_turn.reset();out.rotation_drive_held=true;
        }
    }
    s.turn_speed=turn_speed;s.yaw=candidate;
    out.attempted_yaw=candidate+(in.travel_sign>0.?0.:pi);
    if(!world.corridor(s,out.attempted_yaw,in.wet_escape,in.shallow_admitted,in.maximum_probe_distance)) {
        out.baked_veto=true;path_clear=false;throttle=0.;s.movement_direction=0;
    }
    if(s.ground_plane) {
        const double gradient=s.ground_plane->gradient_x*std::sin(candidate)+
            s.ground_plane->gradient_z*std::cos(candidate);
        slope=-std::atan(gradient);
    }
    const double previous=s.speed;
    double forced=retained_contact_speed(previous,s.contact_forward);
    const int direction=sign(throttle);
    s.service_brake=direction&&direction*(previous-forced)<0.&&
        (s.service_brake||direction!=sign(s.direction_command));
    s.direction_command=throttle;
    double speed=in.siege_locked?0.:d.drive.longitudinal(previous,throttle,steer?turn:0.,slope,
        in.dt,s.airborne,0,in.brake,s.service_brake);
    if(forced) {
        const double unforced=d.drive.longitudinal(previous-forced,throttle,steer?turn:0.,slope,
            in.dt,s.airborne,0,in.brake,s.service_brake);
        forced=retained_contact_speed(speed,speed-unforced);
    }
    s.contact_forward=forced;s.last_drive_pitch=slope;
    if(forced) {out.passive_forward=forced;speed-=forced;s.contact_forward=0.;}
    if(!path_clear) {
        if(in.probe_collision&&!in.exact_collision)out.hard_contact=true;
        else speed*=.2;
        s.destructible_contact_speed.reset();
    }
    double contact_speed=s.destructible_contact_speed.value_or(speed);
    const double contact_v0=speed;
    std::optional<double> realised_yaw;
    if(path_clear&&!in.pose_frozen&&std::abs(speed)>.0001) {
        out.resolved_motion=true;
        out.motion_status=world.motion(s,d,position,s.yaw,speed,in.dt,in.now,false,true);
        if(!pass(out.motion_status)) {
            path_clear=false;
            if(out.motion_status==MotionStatus::Soft) {
                contact_speed=std::min(std::abs(contact_speed),std::abs(speed));
                speed=speed<0.?-contact_speed:contact_speed;s.destructible_contact_speed=speed;
            } else if(out.motion_status==MotionStatus::CapCrushed) {
                speed=previous;s.destructible_contact_speed.reset();
            } else {
                world.impact(s,contact_v0,in.now);
                realised_yaw=s.yaw+(contact_v0<0.?pi:0.);
                world.invalidate(s.id,*realised_yaw,true);
                out.hard_contact=true;s.destructible_contact_speed.reset();
            }
        } else s.destructible_contact_speed.reset();
    }
    HardResponse response{speed,position,false};
    if(out.hard_contact&&!s.airborne) {
        response=hard_contact(s,d,position,s.yaw,speed,in.dt,in.now,world);speed=response.speed;
        if(in.move_target)world.report_contact(s,position,*in.move_target,
            realised_yaw.value_or(old_yaw+(in.travel_sign>0.?0.:pi)),in.now);
    } else if(out.motion_status==MotionStatus::Soft||out.motion_status==MotionStatus::CapCrushed)
        s.hard_grinds=1;
    if(out.resolved_motion)world.motion_finished(s,out.motion_status,contact_v0,speed);
    s.speed=speed;s.contact_forward=retained_contact_speed(speed,s.contact_forward);
    if(d.siege_speed_limit) {speed=std::max(-*d.siege_speed_limit,std::min(*d.siege_speed_limit,speed));s.speed=speed;}
    if(response.deflected)s.position=response.position;
    else if(path_clear&&!in.pose_frozen) {
        s.position[0]+=std::sin(s.yaw)*speed*in.dt;s.position[2]+=std::cos(s.yaw)*speed*in.dt;
        if(std::abs(speed)>.0001)s.hard_grinds=std::max(0,s.hard_grinds-1);
    }
    out.path_clear=path_clear;
    return out;
}
Vec contact_push_deceleration(const State& s,const Descriptor& d,bool rolling)
{
    const double normal=std::max(0.,std::min(1.,std::cos(s.pitch)*std::cos(s.roll)));
    const double grip=normal>=d.side_full_y?d.side_full:normal<=d.side_min_y?d.side_min:
        d.side_min+(normal-d.side_min_y)/(d.side_full_y-d.side_min_y)*(d.side_full-d.side_min);
    const double rr=d.drive.rolling(0,0.)*d.drive.mass/std::max(d.drive.mass,1.);
    const double hold=d.drive.t[18]*d.drive.t[0]*std::max(normal,.1);
    return {{rolling?rr:std::max(rr,hold),std::max(rr,hold*grip)}};
}
Vec contact_push_step(const State& s,const Descriptor& d,Vec push,double dt)
{
    if(s.airborne||dt<=0.)return push;
    const auto budget=contact_push_deceleration(s,d,s.alive&&(s.speed||s.movement_direction));
    const double sn=std::sin(s.yaw),cs=std::cos(s.yaw);
    const double forward=bleed(push[0]*sn+push[1]*cs,budget[0]*dt);
    const double side=bleed(push[0]*cs-push[1]*sn,budget[1]*dt);
    return {{forward*sn+side*cs,forward*cs-side*sn}};
}
Vec contact_traverse(const State& s,const Descriptor& d,double turn,double dt,double direction)
{
    if(turn==0.||dt<=0.)return {{0.,0.}};
    const double omega=d.drive.traverse(0.,turn,s.speed,std::max(dt,d.drive.t[24]),0,direction);
    const double torque=std::abs(d.drive.engine(std::abs(s.speed)+std::abs(omega)*d.half_width,turn,s.pitch))*
        (1.-.75*std::min(1.,std::abs(direction)))*d.half_width;
    return {{omega,torque}};
}
void contact(State& s,const Descriptor& d,const ContactResponse& r,double dt,double now,
    World& world,ContactOptions options)
{
    if(r.delta_velocity[0]||r.delta_velocity[1])s.contact_dynamics=true;
    const double yaw=s.yaw,speed=s.speed,sn=std::sin(yaw),cs=std::cos(yaw);
    const double forward=r.delta_velocity[0]*sn+r.delta_velocity[1]*cs;
    const double applied=s.alive?forward:0.;
    s.speed=speed+applied;s.contact_forward=retained_contact_speed(s.speed,s.contact_forward+applied);
    Vec push{{s.push_x+r.delta_velocity[0]-applied*sn,s.push_z+r.delta_velocity[1]-applied*cs}};
    if(options.advance_push&&options.apply_friction)push=contact_push_step(s,d,push,dt);
    Vec move=options.apply_correction?r.correction:Vec{};
    if(options.advance_push){move[0]+=push[0]*dt;move[1]+=push[1]*dt;}
    if(options.advance_forward){move[0]+=sn*applied*dt;move[1]+=cs*applied*dt;}
    if(options.advance_push&&!options.advance_forward) {
        const auto sweep=s.drive_sweep;s.drive_sweep.reset();
        if(sweep&&s.alive) {
            s.position[0]=sweep->start[0];s.position[2]=sweep->start[2];
            move[0]+=sweep->movement[0]+sn*applied*dt;move[1]+=sweep->movement[1]+cs*applied*dt;
        }
    }
    if(move[0]||move[1]){double fraction=1.;move=world.slide(s,d,move,fraction);}
    const double distance=std::hypot(move[0],move[1]);
    if(distance>.0001) {
        const double step=std::max(dt,1./120.),heading=std::atan2(move[0],move[1]);
        const Point position=s.position,candidate{{position[0]+move[0],position[1],position[2]+move[1]}};
        const bool blocked=world.motion(s,d,position,heading,distance/step,step,now,true,false)!=MotionStatus::Clear;
        if(blocked||!world.pose_progress(s,position,yaw,candidate,yaw)||
           !world.turret_clear(s,position,yaw,candidate,yaw)) {
            move={{0.,0.}};push={{0.,0.}};
            if(options.advance_forward) {
                s.speed=speed;s.contact_forward=retained_contact_speed(speed,s.contact_forward-applied);
            }
        }
    }
    s.position[0]+=move[0];s.position[2]+=move[1];
    if(options.advance_forward&&s.drive_sweep) {
        s.drive_sweep->movement[0]+=move[0];s.drive_sweep->movement[1]+=move[1];
    }
    s.push_x=push[0];s.push_z=push[1];
}
void wreck_contact(State& s,const Descriptor& d,const ContactResponse& r,double dt,double now,World& world)
{
    const Point before=s.position;const double old_yaw=s.yaw;
    double vx=s.push_x+r.delta_velocity[0],vz=s.push_z+r.delta_velocity[1];
    double omega=s.push_yaw+r.delta_yaw;
    const double width=d.collision_shape[0],length=d.collision_shape[1];
    const double inertia=(width*width+length*length)/3.;
    if(!s.airborne&&dt>0.) {
        const auto grip=contact_push_deceleration(s,d,false);
        const double sn=std::sin(old_yaw),cs=std::cos(old_yaw);
        std::array<double,3> velocity{{vx*sn+vz*cs,vx*cs-vz*sn,omega}};
        struct FrictionRow {int axis;double lever,budget,impulse;};
        std::vector<FrictionRow> rows;
        for(double x:{-width,width})for(double z:{-length/2.,length/2.}) {
            rows.push_back({0,x,grip[0]*dt/4.,0.});
            rows.push_back({1,-z,grip[1]*dt/4.,0.});
        }
        for(int pass_index=0;pass_index<d.constraint_iterations;++pass_index) {
            double largest=0.;
            for(auto& row:rows) {
                double delta=-(velocity[row.axis]+row.lever*velocity[2])/(1.+row.lever*row.lever/inertia);
                const double updated=std::max(-row.budget,std::min(row.budget,row.impulse+delta));
                delta=updated-row.impulse;row.impulse=updated;
                velocity[row.axis]+=delta;velocity[2]+=delta*row.lever/inertia;
                largest=std::max(largest,std::abs(delta));
            }
            if(largest<1.e-9)break;
        }
        for(double& value:velocity)if(std::abs(value)<1.e-9)value=0.;
        vx=velocity[0]*sn+velocity[1]*cs;vz=velocity[0]*cs-velocity[1]*sn;omega=velocity[2];
    }
    s.push_x=vx;s.push_z=vz;s.push_yaw=omega;
    const Vec correction=(vx||vz||omega)?r.correction:Vec{};
    const Vec movement{{vx*dt+correction[0],vz*dt+correction[1]}};
    bool swept=false;
    if(omega&&std::hypot(movement[0],movement[1])&&dt>0.) {
        const double fraction=world.rotation_fraction(s,d,before,old_yaw,old_yaw+omega*dt,movement);
        if(fraction>0.) {
            const Vec move{{movement[0]*fraction,movement[1]*fraction}};
            const Point end{{before[0]+move[0],before[1],before[2]+move[1]}};
            const double end_yaw=old_yaw+omega*dt*fraction;
            if(world.pose_progress(s,before,old_yaw,end,end_yaw)&&
               world.turret_clear(s,before,old_yaw,end,end_yaw)&&
               world.motion(s,d,before,std::atan2(move[0],move[1]),std::hypot(move[0],move[1])/dt,
                   dt,now,true,false)==MotionStatus::Clear&&
               world.rotation(s,d,before,old_yaw,end_yaw,dt,now,0.,move)) {
                s.position[0]=end[0];s.position[2]=end[2];
                s.yaw=std::atan2(std::sin(end_yaw),std::cos(end_yaw));
                if(fraction<1.)s.push_yaw=0.;
                swept=true;
            }
        }
    }
    if(!swept) {
        ContactResponse motion;motion.correction=r.correction;
        ContactOptions options;options.apply_friction=false;options.apply_correction=vx||vz||omega;
        contact(s,d,motion,dt,now,world,options);
        // The separate fallback uses the accepted post-translation centre.
        if(s.push_yaw&&inertia) {
            const Point position=s.position;
            const double yaw=s.yaw;
            double candidate=yaw+s.push_yaw*dt;
            if(std::abs(candidate-yaw)>1.e-9) {
                const double fraction=world.rotation_fraction(s,d,position,yaw,candidate);
                candidate=yaw+(candidate-yaw)*fraction;
                if(fraction<1.)s.push_yaw=0.;
                if(!world.pose_progress(s,position,yaw,position,candidate)||
                   !world.turret_clear(s,position,yaw,position,candidate)||
                   !world.rotation(s,d,position,yaw,candidate,dt,now,0.)) {
                    candidate=yaw;s.push_yaw=0.;
                }
            }
            s.yaw=std::atan2(std::sin(candidate),std::cos(candidate));
        }
    }
    if(world.vertical(s,d,before,old_yaw,dt)) {s.position=before;s.yaw=old_yaw;s.push_yaw=0.;}
    for(double* value:{&s.pitch,&s.terrain_pitch,&s.roll})
        *value=std::atan2(std::sin(*value),std::cos(*value));
}
}
