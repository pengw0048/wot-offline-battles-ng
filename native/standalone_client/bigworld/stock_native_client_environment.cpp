#include "stock_native_client_environment.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_environment {
namespace {
double distance(const Point&a,const Point&b){return std::hypot(a[0]-b[0],a[2]-b[2]);}
double still(std::optional<double>&since,double speed,double now){if(std::abs(speed)>.5){since.reset();return 0.;}if(!since)since=now;return std::max(0.,now-*since);}
bool due(double&deadline,std::int64_t id,double now){
    if(deadline<=0.){const double phase=((std::uint64_t(id<0?-id:id)*17)%5)*.1;deadline=std::floor(now/.5)*.5+phase;if(deadline+1.e-9<now)deadline+=.5;}
    if(now+1.e-9<deadline)return false;deadline+=(std::floor((std::max(0.,now-deadline)+1.e-9)/.5)+1.)*.5;return true;
}
double view(const Profile&p,double factor,double stationary){
    double out=std::max(50.,p.base_view)*std::max(0.,p.misc_vision*std::max(.5,std::min(1.,factor)))*std::max(0.,p.crew_vision);
    if(p.binoculars&&stationary>=std::max(0.,p.binocular_delay))out*=std::max(1.,p.binocular_factor);return std::max(50.,out);
}
}
void Spotting::seed(const ActorView&a){if(memory_.count(a.key))return;auto&m=memory_[a.key];m.until=a.direct_until;m.radio_until=radio_deadlines_.count(a.key)?radio_deadlines_.at(a.key):a.radio_until;m.next_probe=a.next_probe;m.desired_model=m.applied_model=a.model;m.desired_marker=m.applied_marker=a.marker;m.applied=true;}
void Spotting::radio(const std::map<std::string,double>&deadlines,std::set<std::string>allies){radio_deadlines_=deadlines;radio_allies_=std::move(allies);for(auto&row:memory_){const auto found=deadlines.find(row.first);row.second.radio_until=found==deadlines.end()?0.:found->second;}}
void Spotting::acknowledge(const Transition&t){const auto found=memory_.find(t.key);if(found==memory_.end())return;auto&m=found->second;m.applied=true;m.applied_model=t.model;m.applied_marker=t.marker;}
void Spotting::forget(const std::set<std::string>&present){for(auto i=memory_.begin();i!=memory_.end();)if(!present.count(i->first))i=memory_.erase(i);else ++i;}
Output Spotting::frame(const Frame&input,const std::vector<ActorView>&actors,const std::map<std::string,Profile>&profiles,const Sight&sight){
    if(!std::isfinite(input.now))throw std::invalid_argument("Native spotting clock is invalid");Output out;
    if(!input.radio_only&&input.now<next_update_)return out;if(!input.radio_only&&next_update_<=0.)next_update_=input.now;
    if(!input.radio_only)next_update_+=(std::floor((std::max(0.,input.now-next_update_)+1.e-9)/.1)+1.)*.1;
    const ActorView*local=nullptr;for(const auto&a:actors)if(a.key==input.local_key){local=&a;break;}
    const auto local_profile=profiles.find(input.local_key);double local_still=0.;
    if(!input.radio_only&&local&&local->alive){local_still=still(local_still_since_,local->speed,input.now);if(local_profile!=profiles.end()){
        const auto&p=local_profile->second;const double radius=view(p,input.local_vision_factor,local_still);
        if(!published_vision_||std::abs(radius-*published_vision_)>.5){published_vision_=radius;out.vision_radius=radius;}
        for(const auto&device:p.optional_devices){const bool active=local_still>=std::max(0.,device.second);const auto old=devices_.find(device.first);if(old==devices_.end()||old->second!=active){devices_[device.first]=active;out.devices.push_back({device.first,active});}}
    }}
    if(input.hud_only)return out;
    for(const auto&a:actors){if(a.local||!a.ready||a.missing||a.tombstone||(input.radio_only&&a.team==input.local_team))continue;seed(a);auto&m=memory_.at(a.key);const auto target_profile=profiles.find(a.key);
        bool remembered=false;
        if(a.team==input.local_team){remembered=radio_allies_.count(a.key)!=0;if(!local||!local->alive)remembered=true;
            else if(!remembered){if(due(m.next_probe,a.id,input.now)&&local_profile!=profiles.end()&&target_profile!=profiles.end())m.direct=sight(*local,a,local_profile->second,target_profile->second,local_still,0.,false);remembered=m.direct;}}
        else {
            if(!input.radio_only&&a.alive&&due(m.next_probe,a.id,input.now)){const double target_still=still(m.still_since,a.speed,input.now);m.direct=local&&local->alive&&local_profile!=profiles.end()&&target_profile!=profiles.end()&&sight(*local,a,local_profile->second,target_profile->second,local_still,target_still,input.now<a.shot_until);
                if(m.direct)m.until=input.now+10.;}
            else if(!a.alive)m.direct=false;
            remembered=input.now<std::max(m.until,m.radio_until);
        }
        if(!a.alive&&a.dead_marker_known)remembered=true;
        const double radius=565.+(m.applied_model?5.:0.);
        const bool model=remembered&&(distance(input.presentation_origin,a.position)<=radius||input.spg_aiming);
        m.desired_model=model;m.desired_marker=remembered;
        if(!m.applied||model!=m.applied_model||remembered!=m.applied_marker)out.transitions.push_back({a.key,a.engine_id,model,remembered});
    }
    return out;
}
}
