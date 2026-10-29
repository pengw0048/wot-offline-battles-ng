#include "stock_native_bot_motion_plan.hpp"
#include <iostream>
#include <iomanip>
using namespace stock_native_bot_navigation;
struct Queries:MotionQueryWorld {
    MotionSample sample;int kind=0,receipt_kind=0,directions=0,receipts=0;
    std::optional<MotionSample> direction(std::int64_t,const Point&,double,double,const ExactDescriptor&,std::optional<double>)override {
        ++directions;if(kind<0)return {};return sample;
    }
    ReceiptResult receipt(std::int64_t,const Point&p,double yaw,double speed,const ExactDescriptor&,std::optional<double>maximum,double)override {
        ++receipts;return {static_cast<ReceiptStatus>(receipt_kind),{p,yaw,std::min(15.,maximum.value_or(15.)),1.6,3.5,speed<0.?-1:1}};
    }
};
int main(){std::cout<<std::setprecision(17);Queries world;MotionPlan plan(world);std::string op;
while(std::cin>>op){if(op=="begin"){std::set<std::int64_t> live;int n;std::cin>>n;for(int i=0;i<n;++i){std::int64_t id;std::cin>>id;live.insert(id);}plan.beginFrame(live);continue;}
if(op=="end"){plan.endFrame();std::cout<<"[";bool first=true;for(const auto&v:plan.waiting()){if(!first)std::cout<<",";first=false;std::cout<<"["<<v.first<<","<<v.second<<"]";}std::cout<<"]\n";continue;}
MotionPlanInput in;int has_move,has_probe,has_clearance;double probe,clearance;Point move;
std::cin>>in.id>>in.position[0]>>in.position[1]>>in.position[2]>>in.yaw>>in.speed>>in.turn_speed>>in.throttle>>in.turn>>in.step>>in.now>>in.half_length>>in.terrain_pitch>>in.grounded>>in.airborne>>in.decision_due>>in.refresh_control>>in.exact_resolver>>in.baked>>in.movement_intent>>in.cell_size>>in.recovery>>has_move>>move[0]>>move[1]>>move[2]>>has_probe>>probe>>has_clearance>>clearance;
if(has_move)in.move_position=move;if(has_probe)in.recovery_probe_distance=probe;if(has_clearance)in.clearance_distance=clearance;
std::cin>>world.kind>>world.sample.clear>>world.sample.collision>>world.sample.water>>world.sample.slope>>world.sample.slope_valid>>world.sample.probe_failed>>world.sample.deferred>>world.receipt_kind;
world.sample.receipt.reset();world.sample.receipt_pending=false;auto out=plan.prepare(in);
std::cout<<"["<<out.throttle<<","<<out.turn<<","<<out.travel_sign<<","<<out.travel_yaw<<","<<out.slope_pitch<<","<<out.movement_direction<<","<<out.rotation_direction<<","<<out.path_clear<<","<<out.pose_frozen<<","<<out.generic_collision<<","<<out.exact_collision<<","<<out.remember_failure<<","<<out.report_blocked_corridor<<","<<out.cache_reused<<","<<out.catchup_hold<<","<<out.catchup_reprobe<<",";
if(out.maximum_distance)std::cout<<*out.maximum_distance;else std::cout<<"null";
auto sample=[](const std::optional<MotionSample>&s){if(!s){std::cout<<"null";return;}std::cout<<"["<<s->clear<<","<<s->collision<<","<<s->water<<","<<s->slope<<","<<s->slope_valid<<","<<s->probe_failed<<","<<s->deferred<<","<<s->receipt_pending<<",";
if(s->receipt){auto&r=*s->receipt;std::cout<<"["<<r.origin[0]<<","<<r.origin[1]<<","<<r.origin[2]<<","<<r.yaw<<","<<r.distance<<","<<r.half_width<<","<<r.leading<<","<<r.direction<<"]";}else std::cout<<"null";std::cout<<"]";};
std::cout<<",";sample(out.sample);std::cout<<",";auto found=plan.cache.find(in.id);if(found==plan.cache.end())std::cout<<"null";else{auto&c=found->second;std::cout<<"["<<c.deadline<<","<<c.distance<<","<<c.leading<<",";sample(c.result);std::cout<<"]";}
std::cout<<","<<world.directions<<","<<world.receipts<<"]\n";
}}
