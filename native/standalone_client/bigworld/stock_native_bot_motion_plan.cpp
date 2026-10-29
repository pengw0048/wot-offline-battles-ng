#include "stock_native_bot_motion_plan.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace stock_native_bot_navigation {
namespace {
constexpr double pi=3.14159265358979323846;
double angle(double x){while(x>pi)x-=2*pi;while(x<-pi)x+=2*pi;return x;}
double distance(const Point&a,const Point&b){const double x=a[0]-b[0],z=a[2]-b[2];return std::sqrt(x*x+z*z);}
double deadline(double now,std::int64_t id,bool first)
{if(!first)return now+.150;const auto positive=id<0?std::uint64_t(-(id+1))+1:std::uint64_t(id);const double phase=double(((positive%29*17+77)%29)+1)/29.;return now+.150*phase;}
double reach(double speed,double dt){return std::max(.4,std::abs(speed)*std::max(0.,std::min(.2,dt))+.2);}
}
bool receiptContains(const WorldReceipt&r,const Point&p,double yaw,double speed,double dt)
{
    if((r.direction!=1&&r.direction!=-1)||r.direction!=(speed<0.?-1:1))return false;
    const double dx=p[0]-r.origin[0],dz=p[2]-r.origin[2],dy=std::abs(p[1]-r.origin[1]),s=std::sin(r.yaw),c=std::cos(r.yaw);
    const double forward=dx*s+dz*c,lateral=std::abs(dx*c-dz*s),turn=std::abs(angle(yaw-r.yaw));
    return forward>=-.0001&&forward+std::max(0.,r.leading)+reach(speed,dt)<=std::max(0.,r.distance)&&dy<=.0001&&lateral<=.0001&&turn<=.00001;
}
bool receiptRefreshDue(const WorldReceipt&r,const Point&p,double yaw,double speed,double dt)
{
    if(!receiptContains(r,p,yaw,speed,dt))return false;const double forward=(p[0]-r.origin[0])*std::sin(r.yaw)+(p[2]-r.origin[2])*std::cos(r.yaw);
    return std::max(0.,r.distance)-forward-std::max(0.,r.leading)-reach(speed,dt)<=6.;
}
bool cacheCovers(const ProbeCache&c,std::optional<double>maximum)
{if(!maximum)return !c.maximum_distance;if(!c.maximum_distance)return true;return *c.maximum_distance+1.e-6>=*maximum;}
bool cacheReusable(const ProbeCache&cache,const Point&p,double yaw,double speed,double now,bool settled,double dt,bool ignore)
{
    if(cache.result.deferred)return false;const double dx=p[0]-cache.position[0],dz=p[2]-cache.position[2],dy=std::abs(p[1]-cache.position[1]);
    const double s=std::sin(cache.yaw),c=std::cos(cache.yaw),forward=dx*s+dz*c,lateral=std::abs(dx*c-dz*s),turn=std::abs(angle(yaw-cache.yaw));
    if(settled)return std::abs(forward)<=.05&&lateral<=.05&&dy<=.05&&turn<=.005;
    if(!ignore&&now>=cache.deadline)return false;const double lookahead=std::abs(speed)>5.?20.:15.;
    const double budget=ignore?std::max(0.,cache.distance-std::max(.5,cache.leading)-reach(speed,dt)):3.5;
    if(forward<-.1||forward>budget||lateral+lookahead*std::abs(std::sin(turn))>1.)return false;
    return !cache.result.receipt||receiptContains(*cache.result.receipt,p,yaw,speed,dt);
}
void MotionPlan::beginFrame(const std::set<std::int64_t>&live)
{
    Frame frame;std::set<std::int64_t> seen;std::vector<std::int64_t> initial;
    for(const auto&entry:waiting_)if(live.count(entry.first)&&seen.insert(entry.first).second){frame.waiting.push_back(entry);frame.waiting_initial[entry.first]=entry.second;if(entry.second)initial.push_back(entry.first);}
    frame.initial_first=!initial.empty();std::vector<std::int64_t> priority=initial;
    if(priority.empty())for(const auto&entry:frame.waiting)priority.push_back(entry.first);
    for(std::size_t n=0;n<std::min(std::size_t(13),priority.size());++n)frame.priority.insert(priority[n]);frame_=std::move(frame);
}
void MotionPlan::endFrame()
{
    if(!frame_)return;const auto&f=*frame_;std::vector<std::pair<std::int64_t,bool>> next;std::set<std::int64_t> seen;
    auto append=[&](std::int64_t id,bool uncached){if(seen.insert(id).second)next.push_back({id,uncached});};
    auto uncached=[&](std::int64_t id){auto found=f.requested_uncached.find(id);return found!=f.requested_uncached.end()&&found->second;};
    for(const auto&entry:f.waiting)if(f.requested_set.count(entry.first)&&!f.attempted.count(entry.first))append(entry.first,uncached(entry.first));
    for(auto id:f.requested)if(!f.attempted.count(id))append(id,uncached(id));std::set<std::int64_t> deferred(f.deferred.begin(),f.deferred.end());
    for(const auto&entry:f.waiting)if(deferred.count(entry.first))append(entry.first,false);
    for(auto id:f.requested)if(deferred.count(id))append(id,false);waiting_=std::move(next);frame_.reset();
}
void MotionPlan::clear(){cache.clear();waiting_.clear();frame_.reset();}
ReceiptResult MotionPlan::receipt(const MotionPlanInput&in,double yaw,double speed,bool uncached,std::optional<double>maximum)
{
    if(!frame_)return {ReceiptStatus::Deferred,{}};auto&f=*frame_;
    if(f.requested_set.insert(in.id).second){f.requested.push_back(in.id);auto waiting=f.waiting_initial.find(in.id);
        f.requested_uncached[in.id]=waiting==f.waiting_initial.end()?uncached:waiting->second;}
    if(f.attempted.count(in.id))return {f.results[in.id]==ReceiptStatus::Blocked?ReceiptStatus::Blocked:ReceiptStatus::Deferred,{}};
    if((f.initial_first&&!f.requested_uncached[in.id])||(!f.priority.empty()&&!f.priority.count(in.id)))return {ReceiptStatus::Deferred,{}};
    f.priority.erase(in.id);if(!f.budget)return {ReceiptStatus::Deferred,{}};--f.budget;f.attempted.insert(in.id);ReceiptResult result;
    try{result=world_.receipt(in.id,in.position,yaw,speed,in.descriptor,maximum,in.now);}catch(...){result={};}
    f.results[in.id]=result.status;if(result.status==ReceiptStatus::Deferred)f.deferred.push_back(in.id);return result;
}
ProbePlan MotionPlan::prepare(const MotionPlanInput&in)
{
    ProbePlan plan;plan.throttle=in.throttle;plan.turn=in.turn;
    plan.travel_sign=in.throttle<0.||(std::abs(in.throttle)<=.01&&in.speed<0.)?-1.:1.;plan.travel_yaw=in.yaw+(plan.travel_sign<0.?pi:0.);
    std::optional<double> horizon;if(in.baked)horizon=std::max(in.cell_size,std::abs(in.speed)*1.5);
    if(plan.travel_sign>0.&&horizon&&in.move_position&&in.movement_intent&&(in.recovery=="drive"||in.recovery=="avoid"))
        plan.maximum_distance=std::min(std::max(.5,distance(in.position,*in.move_position)-1.5),*horizon);
    else if(in.recovery=="short_forward_escape"||in.recovery=="short_reverse_escape")plan.maximum_distance=std::max(.5,in.recovery_probe_distance.value_or(2.));
    else if(in.recovery=="contact_escape"||in.recovery=="forward_escape"||in.recovery=="wreck_push")
        plan.maximum_distance=std::max(.5,in.recovery_probe_distance.value_or(std::max(.5,in.half_length)*1.6));
    else if(plan.travel_sign<0.&&horizon&&(in.recovery=="reverse_turn"||in.recovery=="reverse_withdraw"))plan.maximum_distance=horizon;
    else if(in.recovery=="friendly_yield"){
        plan.maximum_distance=horizon;if(in.clearance_distance){const double leading=std::max(.5,in.half_length),frame_reach=std::max(.4,std::abs(in.speed)*std::min(.2,in.step)+.2),endpoint=leading+*in.clearance_distance;
            plan.maximum_distance=std::max(leading+frame_reach,horizon?std::min(endpoint,*horizon):endpoint);
            if(*in.clearance_distance<=0.)plan.throttle=plan.turn=0.;}}
    auto found=cache.find(in.id);std::optional<ProbeCache> previous;if(found!=cache.end())previous=found->second;
    const bool settled=std::abs(plan.throttle)<=.01&&std::abs(plan.turn)<=.01&&std::abs(in.speed)<=.02&&in.grounded&&!in.airborne;
    const bool reusable=previous&&(!in.decision_due||cacheCovers(*previous,plan.maximum_distance))&&cacheReusable(*previous,in.position,plan.travel_yaw,in.speed,in.now,settled,in.step,!in.refresh_control);
    const bool geometry=previous&&probeClear(previous->result)&&cacheCovers(*previous,plan.maximum_distance)&&cacheReusable(*previous,in.position,plan.travel_yaw,in.speed,in.now,settled,in.step,true);
    plan.catchup_reprobe=!in.refresh_control&&!reusable&&previous&&probeClear(previous->result)&&
        (std::abs(plan.throttle)>.01||std::abs(in.speed)>.0001||std::abs(plan.turn)>.01||std::abs(in.turn_speed)>.01);
    plan.catchup_hold=!in.refresh_control&&!reusable&&!plan.catchup_reprobe;
    if(plan.catchup_hold){plan.throttle=plan.turn=0.;plan.pose_frozen=true;}
    else if(!reusable){
        try{plan.sample=world_.direction(in.id,in.position,plan.travel_yaw,in.speed,in.descriptor,plan.maximum_distance);}
        catch(...){MotionSample failure;failure.collision=true;failure.probe_failed=true;plan.sample=failure;}
        if(plan.sample&&probeClear(*plan.sample)&&!plan.sample->deferred&&std::abs(plan.sample->slope)<=.01&&
           (std::abs(plan.throttle)>.01||std::abs(in.speed)>.0001)&&std::abs(plan.turn)<=.01&&std::abs(in.turn_speed)<=.01&&!in.airborne){
            double receipt_speed=std::abs(in.speed);if(plan.throttle<0.||(std::abs(plan.throttle)<=.01&&in.speed<0.))receipt_speed=-std::max(receipt_speed,.000001);
            std::optional<WorldReceipt> old;if(previous&&!previous->result.deferred&&probeClear(previous->result)&&previous->result.receipt&&
                receiptContains(*previous->result.receipt,in.position,plan.travel_yaw,receipt_speed,in.step))old=previous->result.receipt;
            if(old){if(receiptRefreshDue(*old,in.position,plan.travel_yaw,receipt_speed,in.step)){
                    auto fresh=receipt(in,plan.travel_yaw,receipt_speed,false,plan.maximum_distance);
                    if(fresh.status==ReceiptStatus::Blocked){plan.sample->clear=false;plan.sample->collision=true;}
                    else if(fresh.status==ReceiptStatus::Deferred)plan.sample->receipt_pending=true;
                    else if(fresh.status==ReceiptStatus::Clear&&receiptContains(fresh.receipt,in.position,plan.travel_yaw,receipt_speed,in.step))old=fresh.receipt;}
                plan.sample->receipt=old;
            }else{auto fresh=receipt(in,plan.travel_yaw,receipt_speed,!previous||!previous->result.receipt,plan.maximum_distance);
                if(fresh.status==ReceiptStatus::Deferred)plan.sample->receipt_pending=true;
                else if(fresh.status==ReceiptStatus::Blocked){plan.sample->clear=false;plan.sample->collision=true;}
                else if(fresh.status==ReceiptStatus::Clear)plan.sample->receipt=fresh.receipt;}}
        if(plan.sample&&!plan.sample->deferred){ProbeCache stored;stored.result=*plan.sample;stored.position=in.position;stored.yaw=plan.travel_yaw;stored.maximum_distance=plan.maximum_distance;
            stored.distance=std::min(std::abs(in.speed)>5.?20.:15.,plan.maximum_distance.value_or(std::numeric_limits<double>::infinity()));stored.leading=std::max(.5,in.half_length);
            stored.deadline=plan.sample->receipt_pending?in.now:deadline(in.now,in.id,!previous);cache[in.id]=stored;
        }else if(plan.sample&&plan.sample->deferred){if(geometry)plan.sample=previous->result;else{cache.erase(in.id);if(previous||!in.exact_resolver){plan.throttle=plan.turn=0.;plan.pose_frozen=true;}}}
        else if(!previous||!previous->result.receipt)cache.erase(in.id);
    }else{plan.sample=previous->result;plan.cache_reused=true;}
    const bool deferred=plan.sample&&plan.sample->deferred;
    plan.exact_collision=in.exact_resolver&&plan.sample&&!deferred&&plan.sample->collision&&!plan.sample->water&&std::abs(plan.sample->slope)<=.55;
    plan.generic_collision=plan.sample&&plan.sample->collision;
    plan.path_clear=plan.catchup_hold||deferred||!plan.sample||plan.exact_collision||(std::abs(plan.throttle)<=.01&&std::abs(in.speed)<=.0001)||in.airborne||probeClear(*plan.sample);
    if(!plan.path_clear){plan.throttle=0.;plan.remember_failure=!deferred;plan.report_blocked_corridor=!deferred&&plan.sample&&!plan.sample->collision&&in.move_position.has_value();}
    plan.movement_direction=plan.throttle>.01?1:plan.throttle<-.01?-1:0;plan.rotation_direction=plan.turn>.01?1:plan.turn<-.01?-1:0;
    plan.slope_pitch=in.terrain_pitch;if(plan.sample&&plan.sample->slope_valid&&!plan.sample->probe_failed&&std::isfinite(plan.sample->slope)&&std::abs(plan.sample->slope)!=99.)
        plan.slope_pitch=plan.travel_sign*-std::atan(plan.sample->slope);
    return plan;
}
bool MotionPlan::worldReceiptReusable(std::int64_t id,const Point&p,double yaw,double speed,double now,double dt)const
{auto found=cache.find(id);return found!=cache.end()&&found->second.result.receipt&&cacheReusable(found->second,p,yaw,speed,now,false,dt);}
bool MotionPlan::worldCorridorReusable(std::int64_t id,const Point&p,double yaw,double speed,double now,double dt)const
{
    auto found=cache.find(id);if(found==cache.end())return false;const auto&result=found->second.result;if(!result.receipt&&!result.receipt_pending)return false;
    if(result.receipt)return !result.deferred&&probeClear(result)&&receiptContains(*result.receipt,p,yaw,speed,dt);
    return cacheReusable(found->second,p,yaw,speed,now,false,dt,true);
}
}
