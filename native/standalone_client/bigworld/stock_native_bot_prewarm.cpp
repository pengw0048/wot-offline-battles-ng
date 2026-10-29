#include "stock_native_bot_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot {
bool Runtime::clearSpawnOverlap(ID id,const Point&p,double yaw,const Point&minimum,
    const Point&maximum,double mass,const std::optional<Point>&anchor,
    const std::optional<double>&cap,double now)
{
    if(spawn_overlap_done_.count(id)||!anchor)return false;
    const double dx=p[0]-(*anchor)[0],dz=p[2]-(*anchor)[2];
    if(dx*dx+dz*dz>1.){spawn_overlap_done_.insert(id);return false;}
    const auto retry=spawn_overlap_retry_.find(id);
    if(retry!=spawn_overlap_retry_.end()&&now<retry->second)return false;
    spawn_overlap_retry_[id]=now+1.;
    if(!cap)return false;
    if(!std::isfinite(*cap)||*cap<0.)throw std::invalid_argument("Native spawn drive cap invalid");
    stock_native_world::ActorInputs input;input.mass=mass;input.now=now;
    input.yaw=yaw;input.has_kinetic_speed=true;input.kinetic_speed=*cap;
    pipeline_.actor(input);
    // The source calls its unpitched body envelope with zero travel and
    // uses the mounted drive cap only for crush eligibility.
    offline_destructible::Pose pose;for(unsigned i=0;i<3;++i)pose.position[i]=float(p[i]);pose.yaw=yaw;
    pose.minimum=minimum;pose.maximum=maximum;
    const auto result=pipeline_.catalogMotion(pose,0.,.04,true,&pose,true);
    if(result.status!=stock_native_world::ContactStatus::Crushed)return false;
    spawn_overlap_done_.insert(id);spawn_overlap_retry_.erase(id);
    return result.accepted_now;
}
std::size_t Runtime::prewarm(double now)
{
    if(callback_open_)throw std::logic_error("Native registry prewarm inside a physical callback");
    if(!std::isfinite(now))throw std::invalid_argument("Native registry prewarm time invalid");
    pipeline_.retryTreeContacts(now);
    std::vector<ID> live;
    for(ID id:publication_order_)if(actors_.at(id).motion.alive)live.push_back(id);
    if(live.empty())return 0;
    const auto start=registry_cursor_%live.size();std::size_t ready=0;
    for(std::size_t offset=0;offset<std::min<std::size_t>(2,live.size());++offset) {
        auto&a=actors_.at(live[(start+offset)%live.size()]);
        if(!services_.prewarmDestructibles(a,now))continue;
        ++ready;
        clearSpawnOverlap(a.motion.id,a.motion.position,a.motion.yaw,a.descriptor.body_minimum,
            a.descriptor.body_maximum,a.descriptor.drive.mass,a.spawn_anchor,a.modes[a.mode].spawn_drive_cap,now);
    }
    registry_cursor_=(start+2)%live.size();return ready;
}
std::size_t Runtime::prewarmPlayers(double now,bool battle_live)
{
    if(callback_open_)throw std::logic_error("Native player registry prewarm inside a physical callback");
    if(!std::isfinite(now))throw std::invalid_argument("Native player registry prewarm time invalid");
    std::size_t ready=0;
    for(const auto&human:humans_) {
        const ID id=human.contacts.network_id;const auto&body=human.contacts.body;
        const Point p{body.x,body.y,body.z};
        if(battle_live) {
            const auto retry=spawn_overlap_retry_.find(id);
            if(spawn_overlap_done_.count(id)||(retry!=spawn_overlap_retry_.end()&&now<retry->second))continue;
            if(!(body.flags&offline_contact::Alive)){spawn_overlap_done_.insert(id);continue;}
            if(!human.spawn_anchor)continue;
            const auto&anchor=*human.spawn_anchor;const double dx=p[0]-anchor[0],dz=p[2]-anchor[2];
            if(dx*dx+dz*dz>1.){spawn_overlap_done_.insert(id);continue;}
        }
        bool admitted=false;
        try{admitted=services_.prewarmPlayerDestructibles(human,now);}catch(const std::exception&){continue;}
        if(!admitted)continue;
        ++ready;
        clearSpawnOverlap(id,p,body.yaw,human.body_minimum,human.body_maximum,
            human.contact_mass,human.spawn_anchor,human.spawn_drive_cap,now);
    }
    return ready;
}
}
