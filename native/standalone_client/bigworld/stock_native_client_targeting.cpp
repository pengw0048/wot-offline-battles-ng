#include "stock_native_client_targeting.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_client_targeting {
bool lockHolds(const LockStatus&s){return s.exists&&s.alive&&s.visible&&(!s.lan||(s.visual&&s.model&&s.in_world));}
DeviceSignature expertDevices(const stock_native_client_state::Value&critical,const std::map<std::string,int>&extras){
    std::set<int>damaged,destroyed;std::set<std::string>destroyed_names;
    for(const auto&v:critical.get("destroyed").resolved().array)destroyed_names.insert(v.resolved().text);
    for(const auto&device:critical.get("devices").resolved().array){if(device.resolved().kind!=stock_native_client_state::Value::Kind::Object)continue;const auto&name=device.get("name").resolved().text;const auto found=extras.find(name);if(found==extras.end())continue;const auto&phase=device.get("state").resolved().text;if(phase=="destroyed"||destroyed_names.count(name))destroyed.insert(found->second);else if(phase=="critical")damaged.insert(found->second);}
    if(critical.get("fire").truth()){const auto found=extras.find("fire");if(found!=extras.end())damaged.insert(found->second);}
    for(const auto&value:critical.get("crew_ko").resolved().array){auto name=value.resolved().text;if(name.size()<6||name.substr(name.size()-6)!="Health")name+="Health";const auto found=extras.find(name);if(found!=extras.end())destroyed.insert(found->second);}
    return {{damaged.begin(),damaged.end()},{destroyed.begin(),destroyed.end()}};
}
ExpertChanges Expert::monitor(ID id,double now,bool capable,bool allowed,const ExpertActor*actor,ID own_team,ID target_team,const std::string&reason){
    ExpertChanges out;const ID previous=target_;
    if(id<=0){target_=0;due_=0.;signature_.reset();out.accepted=true;if(previous)out.hide=ExpertHide{previous,reason};return out;}
    if(!capable||!allowed||!actor||actor->local||actor->tombstone||own_team==target_team)return out;
    out.accepted=true;if(previous==id)return out;
    target_=id;due_=now+delay_;signature_.reset();if(previous)out.hide=ExpertHide{previous,"target_changed"};return out;
}
ExpertChanges Expert::tick(double now,bool capable,const ExpertActor*actor,const stock_native_client_state::Value&critical,const std::map<std::string,int>&extras){
    if(!capable)return monitor(0,now,true,true,nullptr,0,0,"skill_unavailable");
    ExpertChanges out;if(target_<=0||now<due_)return out;
    std::string reason;if(!actor||actor->local)reason="entity_missing";else if(actor->tombstone)reason="dead";else if(!actor->visible)reason="spot_lost";else if(!actor->exists)reason="entity_missing";else if(!actor->alive)reason="dead";
    if(!reason.empty())return monitor(0,now,true,true,nullptr,0,0,reason);
    const auto devices=expertDevices(critical,extras);if(signature_&&*signature_==devices)return out;out.accepted=true;out.show=ExpertShow{target_,devices};return out;
}
void Expert::shown(ID target,const DeviceSignature&devices){if(target_==target)signature_=devices;}
OutlineChanges outlineChanges(std::optional<ID>held,const stock_native_client_query::OutlinePick&pick){OutlineChanges out;if(held==pick.chosen){out.unchanged=true;out.refresh=bool(held);return out;}if(held){const auto reason=pick.held_reason.empty()?(pick.held_seen?"is behind a nearer vehicle":"left the record set"):pick.held_reason;out.dropped=stock_native_client_query::OutlineDecline{*held,reason};}out.add=pick.chosen;return out;}
bool OutlineClock::begin(double now,bool blocked){if(!std::isfinite(now)||!std::isfinite(period_)||period_<0.)throw std::invalid_argument("Native target outline clock is invalid");if(now<next_||blocked)return false;next_=now+period_;return true;}
}
