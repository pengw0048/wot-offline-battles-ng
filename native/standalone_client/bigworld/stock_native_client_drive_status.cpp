#include "stock_native_client_drive_status.hpp"
#include <stdexcept>
namespace stock_native_client_drive {
namespace {
namespace shared=stock_native_client_state;
namespace step=wot::offline_bot_step;
std::map<const shared::Actor*,std::weak_ptr<Status>> installed;
const char* name(CriticalStat stat){switch(stat){case CriticalStat::reload:return "reload";case CriticalStat::dispersion:return "dispersion";case CriticalStat::aim_time:return "aim_time";case CriticalStat::turret_speed:return "turret_speed";case CriticalStat::mobility:return "mobility";case CriticalStat::vision:return "vision";case CriticalStat::signal:return "signal";}throw std::invalid_argument("Unknown local critical stat");}
const std::vector<shared::Value>& rows(const shared::Value&raw){const auto&v=raw.resolved();if(v.kind!=shared::Value::Kind::Array&&v.kind!=shared::Value::Kind::Null)throw std::invalid_argument("Local critical list is malformed");return v.array;}
}
step::CriticalState decodeCritical(const shared::Value&raw,const StatusDescriptor&descriptor,std::set<std::string>*physical_casualties){
    step::CriticalState critical;critical.fire=raw.get("fire").truth();critical.ammo_rack_death=raw.get("ammo_rack_death").truth();
    for(const auto&v:rows(raw.get("crew_roster")))critical.crew_roster.insert(v.resolved().text);
    std::set<std::string>local_casualties;auto&casualties=physical_casualties?*physical_casualties:local_casualties;casualties.clear();for(const auto&v:rows(raw.get("crew_ko"))){const auto&key=v.resolved().text;casualties.insert(key);const auto roles=descriptor.crew_roles.find(key);if(roles==descriptor.crew_roles.end())critical.crew_ko.insert(key);else critical.crew_ko.insert(roles->second.begin(),roles->second.end());}
    for(const auto&v:rows(raw.get("destroyed")))critical.devices[v.resolved().text].destroyed=true;
    for(const auto&v:rows(raw.get("devices"))){const auto key=v.get("name").resolved().text;if(key.empty())throw std::invalid_argument("Local critical device name is missing");auto&device=critical.devices[key];device.hp=v.get("hp").numeric();
        const auto maximum=descriptor.maxima.find(key);if(maximum!=descriptor.maxima.end())device.maximum=maximum->second;
        const auto regeneration=descriptor.regeneration.find(key);if(regeneration!=descriptor.regeneration.end())device.regeneration=regeneration->second;
        if(v.get("max_hp").resolved().kind!=shared::Value::Kind::Null)device.wire_maximum=v.get("max_hp").numeric();const auto&status=v.get("state").resolved().text;device.destroyed=device.destroyed||status=="destroyed";device.critical=status=="critical";}
    return critical;
}
Status::Status(std::shared_ptr<shared::Actor>actor,StatusDescriptor descriptor):actor_(actor),descriptor_(std::move(descriptor)){}
void Status::update(std::uint64_t revision,std::optional<double>server_ms){if(installed_&&revision==revision_)return;auto actor=actor_.lock();if(!actor)throw std::runtime_error("Native local status actor retired");const auto&state=actor->state;
    auto critical=decodeCritical(state.get("critical"),descriptor_,&casualties_);
    if(actor->local)if(const auto overlay=track_overlay_.lock())overlay->reconcile(*actor,critical);
    critical_=std::move(critical);
    shared::Value signature;signature.kind=shared::Value::Kind::Object;for(const char*k:{"stun_end_server_time_ms","stun_attacker_kind","stun_attacker_id","stun_factors"})signature.object[k]=state.get(k);
    if(!has_stun_||signature!=stun_signature_){const double end=state.get("stun_end_server_time_ms").numeric();if(end&&!server_ms)throw std::runtime_error("Native local stun has no accepted server clock");factors_.clear();
        if(end&&end>*server_ms){const auto&values=state.get("stun_factors").resolved();if(values.kind!=shared::Value::Kind::Null&&values.kind!=shared::Value::Kind::Object)throw std::invalid_argument("Native local stun factors are malformed");
            for(const auto&v:values.object)factors_[v.first]=v.second.numeric(1.);}
        stun_signature_=std::move(signature);has_stun_=true;}
    installed_=true;revision_=revision;
}
double Status::factor(CriticalStat stat)const{if(!installed_)throw std::runtime_error("Native local status has not accepted a snapshot");const auto stun=factors_.find(name(stat));return step::critical_factor(critical_,stat)*(stun==factors_.end()?1.:stun->second);}
double Status::factor(const std::string&stat)const{for(const auto value:{CriticalStat::reload,CriticalStat::dispersion,CriticalStat::aim_time,CriticalStat::turret_speed,CriticalStat::mobility,CriticalStat::vision,CriticalStat::signal})if(stat==name(value))return factor(value);if(!installed_)throw std::runtime_error("Native local status has not accepted a snapshot");const auto value=factors_.find(stat);return value==factors_.end()?1.:value->second;}
double Status::stunFactor(const std::string&stat)const{if(!installed_)throw std::runtime_error("Native local status has not accepted a snapshot");const auto value=factors_.find(stat);return value==factors_.end()?1.:value->second;}
std::shared_ptr<Status> installStatus(std::shared_ptr<shared::Actor>actor,StatusDescriptor descriptor){if(!actor)throw std::invalid_argument("Native local status requires an actor");auto status=std::make_shared<Status>(actor,std::move(descriptor));installed[actor.get()]=status;
    for(auto at=installed.begin();at!=installed.end();)if(at->second.expired())at=installed.erase(at);else ++at;return status;}
std::shared_ptr<const Status> status(const shared::Actor&actor){const auto found=installed.find(&actor);if(found==installed.end())throw std::runtime_error("Native client status producer is unavailable");auto owner=found->second.lock();if(!owner)throw std::runtime_error("Native client status producer retired");return owner;}
std::shared_ptr<Status> localStatus(const shared::Actor&actor){if(!actor.local)throw std::invalid_argument("Native track override requires the local actor");const auto found=installed.find(&actor);if(found==installed.end())throw std::runtime_error("Native client status producer is unavailable");auto owner=found->second.lock();if(!owner)throw std::runtime_error("Native client status producer retired");return owner;}
void Status::localTracks(const step::CriticalState&value){if(!installed_)throw std::runtime_error("Native local status has not accepted a snapshot");for(const char*name:{"leftTrackHealth","rightTrackHealth"}){const auto source=value.devices.find(name);if(source==value.devices.end())continue;auto&target=critical_.devices[name];target.hp=source->second.hp;target.destroyed=source->second.destroyed;target.critical=source->second.critical;}}
double statusFactor(const shared::Actor&actor,CriticalStat stat){return status(actor)->factor(stat);}
}
