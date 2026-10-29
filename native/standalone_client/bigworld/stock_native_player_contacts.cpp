#include "stock_native_player_contacts.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

namespace stock_native_player_contacts {
namespace {
constexpr double pi=3.14159265358979323846;
bool subset(const Token& a,const Token& b){return std::includes(b.begin(),b.end(),a.begin(),a.end());}
Token difference(const Token&a,const Token&b){Token out;std::set_difference(a.begin(),a.end(),b.begin(),b.end(),std::inserter(out,out.end()));return out;}
bool pending(const world::ContactResult& value){return value.pending||value.status==world::ContactStatus::Pending;}
bool acceptedStatus(const world::ContactResult& value){return value.status==world::ContactStatus::Crushed||value.status==world::ContactStatus::Hard;}
bool valid(const Receipt& r){
    if(r.player<=0||r.sequence<=0||r.token.empty()||r.token.size()>64||!(r.dt>0.&&r.dt<=.1)||std::abs(r.pitch)>.61||std::abs(r.roll)>.61)return false;
    for(double v:r.start)if(!std::isfinite(v))return false;
    for(double v:r.end)if(!std::isfinite(v))return false;
    for(double v:{r.yaw,r.end_yaw,r.speed,r.dt,r.pitch,r.roll})if(!std::isfinite(v))return false;
    return true;
}
double pivot(const Descriptor& d,const Receipt&r){
    if(!d.pivot_available||d.rotation_around_center)return 0.;
    const double x=std::cos(r.yaw)-std::cos(r.end_yaw),z=std::sin(r.end_yaw)-std::sin(r.yaw),norm=x*x+z*z;
    if(norm<=1.e-18)return 0.;
    const double offset=((r.end[0]-r.start[0])*x+(r.end[2]-r.start[2])*z)/norm;
    if(std::abs(offset)>d.track_center+1.e-7||std::abs(r.end[0]-r.start[0]-offset*x)>1.e-7||
        std::abs(r.end[2]-r.start[2]-offset*z)>1.e-7||std::abs(r.end[1]-r.start[1])>1.e-7)return 0.;
    return offset;
}
}
bool Receipt::operator<(const Receipt& other)const{
    return std::tie(player,sequence,token,start,yaw,end,end_yaw,speed,dt,pitch,roll)<
        std::tie(other.player,other.sequence,other.token,other.start,other.yaw,other.end,other.end_yaw,other.speed,other.dt,other.pitch,other.roll);
}
world::ContactResult PipelineWorld::trees(const Token& token,const Receipt&r,double now){
    Point start{},end{};for(unsigned i=0;i<3;++i){start[i]=float(r.start[i]);end[i]=float(r.end[i]);}
    return trees_.commit(token,start,end,r.end_yaw,r.speed,now);
}
world::ContactResult PipelineWorld::catalog(const Descriptor& d,const Receipt&r,double now,bool commit){
    for(double v:{d.mass,d.drive_forward,d.drive_reverse,d.rotation_speed})if(!std::isfinite(v))throw std::invalid_argument("Native player contact descriptor invalid");
    world::ActorInputs actor;actor.mass=d.mass;actor.now=now;actor.yaw=r.yaw;actor.speed=r.speed;
    actor.has_kinetic_speed=true;actor.kinetic_speed=r.speed<0.?d.drive_reverse:d.drive_forward;
    pipeline_.actor(actor);
    const double delta=stock_motion_rotation::angleDelta(r.yaw,r.end_yaw);
    if(std::abs(delta)>1.e-8){
        if(!d.traverse_valid)throw std::invalid_argument("Native player critical state invalid");
        world::RotationRequest request;auto&pose=request.world;pose={};pose.position=r.start;
        pose.body={d.body_minimum,d.body_maximum};pose.start_yaw=r.yaw;pose.end_yaw=r.end_yaw;
        pose.pitch=r.pitch;pose.roll=r.roll;pose.pivot_offset=pivot(d,r);
        pose.translation={r.end[0]-r.start[0]-pose.pivot_offset*(std::cos(r.yaw)-std::cos(r.end_yaw)),
            r.end[2]-r.start[2]-pose.pivot_offset*(std::sin(r.end_yaw)-std::sin(r.yaw))};
        pose.max_angle_step=pi/36.;pose.max_steps=16;pose.drivable_gradient=1.;
        request.geometry_end=r.end;request.dt=r.dt;request.has_rotation_speed_cap=true;
        request.rotation_speed_cap=d.rotation_speed;request.has_drive_speed_cap=true;request.drive_speed_cap=actor.kinetic_speed;
        return pipeline_.poseSweep(request,commit);
    }
    offline_destructible::Pose pose;for(unsigned i=0;i<3;++i)pose.position[i]=float(r.start[i]);
    pose.minimum=d.body_minimum;pose.maximum=d.body_maximum;pose.yaw=r.yaw;pose.pitch=r.pitch;pose.roll=r.roll;pose.travel=r.speed*r.dt;
    const double dx=r.end[0]-r.start[0],dz=r.end[2]-r.start[2],distance=std::sqrt(dx*dx+dz*dz);
    if(distance>1.e-8){const double actual=std::atan2(dx,dz),expected=r.speed>=0.?r.yaw:r.yaw+pi;
        if(std::abs(stock_motion_rotation::angleDelta(expected,actual))>.02){pose.explicit_motion_yaw=true;pose.motion_yaw=actual;}}
    return pipeline_.catalogMotion(pose,r.speed,r.dt,true,&pose,false,!commit);
}
std::optional<bool> Owner::resolve(const Request& request,double now,World& source){
    const auto&r=request.receipt;Token trees,pending_trees;
    for(const auto&item:r.token)if(std::get<2>(item)==-1){const auto status=source.tree({std::get<0>(item),std::get<1>(item)});
        if(status==TreeStatus::Tree)trees.insert(item);else if(status==TreeStatus::Pending)pending_trees.insert(item);}
    bool accepted=true,deferred=false;
    if(!trees.empty()){
        const auto result=source.trees(trees,r,now);
        if(result.status==world::ContactStatus::Pending)deferred=true;
        else if(result.status!=world::ContactStatus::Crushed||result.contacts!=trees)accepted=false;
    }
    const auto requested=difference(r.token,trees);Token proposed;
    if(!requested.empty()){
        if(!request.descriptor)throw std::invalid_argument("Native player contact descriptor unavailable");
        ++statistics_.proposals;const auto result=source.catalog(*request.descriptor,r,now,false);proposed=result.contacts;
        if(!difference(pending_trees,proposed).empty())deferred=true;
        const bool missing=!subset(requested,proposed);
        if(result.status==world::ContactStatus::Pending||(missing&&pending(result)))deferred=true;
        else if(missing||!acceptedStatus(result))accepted=false;
    }
    if(accepted&&!deferred&&!requested.empty()){
        ++statistics_.commits;const auto result=source.catalog(*request.descriptor,r,now,true);
        if(pending(result))deferred=true;
        else accepted=acceptedStatus(result)&&subset(requested,result.contacts);
    }
    if(deferred){++statistics_.deferred;return {};}
    return accepted;
}
std::vector<Verdict> Owner::update(const std::vector<Request>& requests,double now,World& source){
    if(!std::isfinite(now))throw std::invalid_argument("Native player contact time invalid");
    std::vector<Verdict> output;
    for(const auto& request:requests){const auto&r=request.receipt;if(!valid(r)){++statistics_.failures;continue;}
        auto cached=results_.find(r);
        if(cached!=results_.end()){
            if(now>=cached->second.next_retry){output.push_back(cached->second.verdict);++statistics_.retries;}
            continue;
        }
        try{
            const auto accepted=resolve(request,now,source);if(!accepted)continue;
            Verdict result{next_serial_++,r.player,r.sequence,*accepted,r.token};
            results_[r]={result,now};serials_[result.serial]=r;order_.push_back(r);++statistics_.terminals;
            while(order_.size()>1024){const auto old=results_.find(order_.front());if(old!=results_.end()){serials_.erase(old->second.verdict.serial);results_.erase(old);}order_.pop_front();}
            output.push_back(std::move(result));
        }catch(const std::exception&){++statistics_.failures;}
    }
    return output;
}
bool Owner::acknowledge(std::uint64_t serial,double now){
    if(!std::isfinite(now))return false;
    const auto key=serials_.find(serial);if(key==serials_.end())return false;
    auto value=results_.find(key->second);if(value==results_.end())return false;
    value->second.next_retry=now+.25;return true;
}
}
