#include "stock_native_bot_motion.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace stock_native_bot_motion;
namespace {
void point(std::istream& in,Point& p){for(auto& v:p)in>>v;}
template<class T>void optional(std::istream& in,std::optional<T>& value)
{bool present=false;T v{};in>>present>>v;if(present)value=v;}
struct TestWorld:World {
    double fraction=1.;bool pose=true,turret=true,rotate=true,clear=true,kill=false;
    std::vector<int> statuses;std::size_t next=0;
    std::vector<std::vector<double>> calls;
    double rotation_fraction(const State&,const Descriptor&,const Point& p,double a,double b,const Vec& move)override
    {calls.push_back({0,p[0],p[1],p[2],a,b,move[0],move[1]});return fraction;}
    bool pose_progress(const State&,const Point& a,double ay,const Point& b,double by)override
    {calls.push_back({1,a[0],a[1],a[2],ay,b[0],b[1],b[2],by});return pose;}
    bool turret_clear(const State&,const Point& a,double ay,const Point& b,double by)override
    {calls.push_back({2,a[0],a[1],a[2],ay,b[0],b[1],b[2],by});return turret;}
    bool rotation(const State&,const Descriptor&,const Point& p,double a,double b,double dt,double now,double cap,const Vec& move)override
    {calls.push_back({3,p[0],p[1],p[2],a,b,dt,now,cap,move[0],move[1]});return rotate;}
    bool corridor(const State&,double yaw,bool wet,bool shallow,const std::optional<double>&distance)override
    {calls.push_back({4,yaw,double(wet),double(shallow),distance.value_or(-1.)});return clear;}
    MotionStatus motion(const State&,const Descriptor&,const Point& p,double yaw,double speed,double dt,double now,bool passive,bool commit)override
    {calls.push_back({5,p[0],p[1],p[2],yaw,speed,dt,now,double(passive),double(commit)});
        return static_cast<MotionStatus>(next<statuses.size()?statuses[next++]:0);}
    void impact(State& s,double speed,double now)override
    {calls.push_back({6,speed,now});if(kill)s.alive=false;}
    void invalidate(std::int64_t,double yaw,bool motion)override
    {calls.push_back({7,yaw,double(motion)});}
    void report_contact(const State&,const Point& p,const Point& target,double yaw,double now)override
    {calls.push_back({8,p[0],p[1],p[2],target[0],target[1],target[2],yaw,now});}
    void motion_finished(const State&,MotionStatus status,double initial,double final)override
    {calls.push_back({9,double(int(status)),initial,final});}
    Vec slide(const State&,const Descriptor&,const Vec& move,double& f)override
    {calls.push_back({10,move[0],move[1]});f=fraction;return {{move[0]*fraction,move[1]*fraction}};}
    bool vertical(State&,const Descriptor&,const Point&,double,double)override{return false;}
};
void print_optional(const std::optional<double>& value)
{if(value)std::cout<<*value;else std::cout<<"null";}
}
int main()
{
    std::cout<<std::setprecision(17);std::string line;
    while(std::getline(std::cin,line)) {
        if(line.empty())continue;
        std::istringstream in(line);Descriptor d;State s;Input input;TestWorld world;int op=0;
        in>>op;for(auto& value:d.drive.t)in>>value;
        in>>d.drive.mass>>d.drive.power>>d.drive.ratio>>d.drive.forward>>d.drive.reverse>>d.drive.friction>>d.drive.brake>>d.drive.rotation;
        for(auto& value:d.drive.terrain)in>>value;
        in>>d.half_width>>d.half_length>>d.track_center;optional(in,d.siege_speed_limit);
        for(auto& value:d.glancing_yaws)in>>value;
        in>>d.hard_brake_decay>>d.hard_stop_speed>>d.hard_entry_factor>>d.hard_slide_decay>>d.hard_grind_ticks;
        in>>d.side_min_y>>d.side_full_y>>d.side_min>>d.side_full>>d.constraint_iterations;
        d.collision_shape={{d.half_width,d.half_length,-.8,2.}};
        s.id=1;point(in,s.position);
        in>>s.yaw>>s.pitch>>s.roll>>s.speed>>s.turn_speed>>s.contact_forward>>s.direction_command>>s.last_drive_pitch;
        in>>s.push_x>>s.push_z>>s.push_yaw>>s.alive>>s.airborne>>s.service_brake>>s.hard_grinds;
        optional(in,s.destructible_contact_speed);
        bool plane;double gx,gz;in>>plane>>gx>>gz;
        if(plane){s.ground_plane=wot::offline_bot_step::SupportPlane{};s.ground_plane->gradient_x=gx;s.ground_plane->gradient_z=gz;}
        in>>input.dt>>input.now>>input.throttle>>input.turn>>input.slope_pitch>>input.travel_sign;
        in>>input.path_clear>>input.pose_frozen>>input.probe_collision>>input.exact_collision>>input.brake>>input.siege_locked>>input.wet_escape>>input.shallow_admitted;
        optional(in,input.maximum_probe_distance);bool target;Point destination;in>>target;point(in,destination);if(target)input.move_target=destination;
        in>>world.fraction>>world.pose>>world.turret>>world.rotate>>world.clear>>world.kill;
        std::size_t count;in>>count;world.statuses.resize(count);for(auto& status:world.statuses)in>>status;
        ContactResponse response;ContactOptions options;
        for(auto& value:response.correction)in>>value;for(auto& value:response.delta_velocity)in>>value;
        in>>response.delta_yaw>>options.advance_push>>options.apply_correction>>options.apply_friction>>options.advance_forward;
        if(!in){std::cerr<<"Malformed motion test input\n";return 2;}
        Trace trace;
        if(op==0)trace=advance(s,d,input,world);
        else if(op==1)contact(s,d,response,input.dt,input.now,world,options);
        else if(op==2)wreck_contact(s,d,response,input.dt,input.now,world);
        else return 3;
        std::cout<<"{\"state\":["<<s.position[0]<<','<<s.position[1]<<','<<s.position[2]<<','<<s.yaw<<','<<s.speed<<','<<s.turn_speed
            <<','<<s.movement_direction<<','<<s.rotation_direction<<','<<s.contact_forward<<','<<s.direction_command<<','<<s.service_brake
            <<','<<s.last_drive_pitch<<','<<s.rotation_contact_blocked<<','<<s.rotation_refused_until<<','<<s.hard_grinds<<','<<s.push_x<<','<<s.push_z<<','<<s.push_yaw<<','<<s.alive<<',';
        print_optional(s.destructible_contact_speed);
        std::cout<<",";if(s.contact_motor_turn)std::cout<<*s.contact_motor_turn;else std::cout<<"null";
        std::cout<<"],\"passive\":"<<trace.passive_forward<<",\"calls\":[";
        bool first=true;for(const auto& call:world.calls) {
            if(!first)std::cout<<',';first=false;std::cout<<'[';
            for(std::size_t i=0;i<call.size();++i){if(i)std::cout<<',';std::cout<<call[i];}
            std::cout<<']';
        }
        std::cout<<"]}\n";
    }
}
