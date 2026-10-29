#include "stock_native_bot_planner.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace stock_native_bot_planner;
namespace {
template<class T> void optional(std::istream&s,Optional<T>&v) {s>>v.present>>v.value;}
void point(std::istream&s,Point&p) {s>>p[0]>>p[1]>>p[2];}
void optional_point(std::istream&s,Optional<Point>&p) {s>>p.present;point(s,p.value);}
void body(std::istream&s,Body&b) {
    s>>b.id>>b.team>>b.alive;point(s,b.position);s>>b.yaw;point(s,b.velocity);
    s>>b.half_length>>b.half_width>>b.lower_y>>b.upper_y>>b.pitch>>b.roll;
}
struct TestWorld:World {
    int policy=0;
    Navigation navigation;
    struct Probe {int kind;double yaw,distance,sign,implicit;};
    std::vector<Probe> probes;
    bool direction(std::int64_t,double yaw,const Optional<double>&maximum,double sign,const Optional<double>&implicit)override {
        probes.push_back({0,yaw,maximum.present?maximum.value:-1.,sign,implicit.present?implicit.value:-1.});
        const double heading=std::atan2(std::sin(yaw),std::cos(yaw));
        if(policy==1)return false;
        if(policy==2)return std::abs(heading)<.25;
        if(policy==3)return std::abs(heading)>2.8;
        if(policy==4)return maximum.present&&maximum.value<=2.;
        return true;
    }
    bool pose(std::int64_t,double yaw,bool push)override {
        probes.push_back({push?2:1,yaw,0.,0.,0.});
        if(policy==5)return false;
        if(policy==6)return std::abs(yaw)<.2;
        return true;
    }
    Navigation navigate(const Input&,const Order&,const Point&)override {return navigation;}
};
void print_point(const Point&p) {std::cout<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';}
template<class T> void print_optional(const Optional<T>&v) {if(v.present)std::cout<<v.value;else std::cout<<"null";}
}
int main() {
    Planner planner;Traffic traffic;std::string line;
    std::cout<<std::setprecision(17);
    while(std::getline(std::cin,line)) {
        if(line.empty())continue;
        std::istringstream input(line);bool reset;Input in;Order order;TestWorld world;int traffic_stage;
        input>>reset;if(reset) {planner=Planner{};traffic=Traffic{};}
        body(input,in.body);input>>in.slot>>in.speed>>in.dt>>in.now>>in.decision_horizon;
        optional(input,in.stopping_distance);optional(input,in.turn_speed_limit);
        input>>in.rotation_refused>>in.navigation_recovery_allowed;
        optional_point(input,order.move);optional_point(input,order.aim);optional_point(input,order.face);
        optional(input,order.throttle);optional(input,order.arrival_radius);optional(input,order.target);
        input>>order.fire_range>>std::quoted(order.mode)>>std::quoted(order.parking_phase);optional(input,order.parking_heading);
        point(input,world.navigation.target);input>>std::quoted(world.navigation.status);
        optional(input,world.navigation.stop_at_target);optional_point(input,world.navigation.progress_target);
        input>>world.policy>>traffic_stage;std::size_t count;input>>count;std::vector<Body> peers(count);for(auto&peer:peers)body(input,peer);
        if(!input) {std::cerr<<"Malformed test input\n";return 2;}
        try {
            auto command=planner.decide(in,order,peers,world);
            in.implicit_probe_distance=command.navigation_probe_distance;
            if(traffic_stage&1)command=traffic.adjust(in,command,peers,world);
            if(traffic_stage&2)command=traffic.safe_controls(in,command,peers,in.stopping_distance.present?in.stopping_distance.value:0.);
            std::cout<<"{\"throttle\":"<<command.throttle<<",\"turn\":"<<command.turn<<",\"target_yaw\":"<<command.target_yaw
                <<",\"brake\":"<<(command.brake?"true":"false")<<",\"recovery_mode\":"<<std::quoted(command.recovery)
                <<",\"movement_intent\":"<<(command.movement_intent?"true":"false")<<",\"navigation_replan\":"<<(command.navigation_replan?"true":"false")
                <<",\"navigation_recovery\":"<<(command.navigation_recovery?"true":"false")<<",\"withdrawal_aim\":"<<(command.withdrawal_aim?"true":"false")
                <<",\"traffic_mode\":"<<std::quoted(command.traffic_mode)<<",\"move_position\":";print_point(command.move);
            std::cout<<",\"aim_position\":";print_point(command.aim);std::cout<<",\"face_position\":";print_point(command.face);
            std::cout<<",\"forward_blocked_by\":";print_optional(command.forward_blocker);
            std::cout<<",\"reverse_blocked_by\":";print_optional(command.reverse_blocker);
            std::cout<<",\"recovery_probe_distance\":";print_optional(command.probe_distance);
            const auto&s=planner.actor(in).driver;
            std::cout<<",\"driver\":["<<s.clock<<','<<s.stuck_time<<','<<s.recovery_time<<','<<s.recovery_count<<','<<s.recovery_side<<']';
            std::cout<<",\"probes\":[";bool first=true;for(const auto&p:world.probes) {
                if(!first)std::cout<<',';first=false;std::cout<<'['<<p.kind<<','<<p.yaw<<','<<p.distance<<','<<p.sign<<','<<p.implicit<<']';
            }
            std::cout<<"]}\n";
        } catch(const std::exception&e) {std::cerr<<e.what()<<'\n';return 3;}
    }
}
