#include "stock_native_client_projectile_state.hpp"
#include <algorithm>
#include <stdexcept>
namespace stock_native_client_projectile {
namespace cs=stock_native_client_state;namespace damage=stock_native_projectile_damage;
namespace {
const std::vector<cs::Value>&array(const cs::Value&value){return value.resolved().array;}
void names(const cs::Value&rows,std::set<std::string>&out){for(const auto&row:array(rows))out.insert(row.text);}
}
damage::Target targetState(const cs::Actor&actor,const damage::TargetDescriptor&descriptor,const damage::StunConfig&stun){
    damage::Target out;out.identity={actor.kind=="player",actor.id};out.position={actor.current[0],actor.current[1],actor.current[2]};out.config=descriptor.critical;out.config.health=int(actor.state.get("health").numeric());out.spall=descriptor.spall;out.crew_roster=descriptor.crew_roster;out.track_front=descriptor.front_bound;out.track_rear=descriptor.rear_bound;out.track_bulk=descriptor.bulk_factor;out.stun_config=stun;out.stun_config.resist_duration=descriptor.stun_resistance_duration;out.stun_config.resist_effect=descriptor.stun_resistance_effect;
    const auto&state=actor.state;out.critical_target_base_revision=std::int64_t(state.get(actor.kind=="bot"?"combat_base_revision":"critical_base_revision").numeric(-1.));out.critical_target_ack_sequence=std::int64_t(state.get(actor.kind=="bot"?"combat_ack_seq":"critical_ack_seq").numeric(-1.));
    const auto&critical=state.get("critical");out.critical.fire=critical.get("fire").truth(state.get("fire").truth());out.critical.ammo_rack_death=critical.get("ammo_rack_death").truth();names(critical.get("crew_ko"),out.critical.crew_ko);if(!array(critical.get("crew_roster")).empty()){out.crew_roster.clear();for(const auto&value:array(critical.get("crew_roster")))out.crew_roster.push_back(value.text);}out.critical.crew_roster.clear();
    out.critical.crew_roster.insert(out.crew_roster.begin(),out.crew_roster.end());
    std::set<std::string> destroyed;names(critical.get("destroyed"),destroyed);
    for(const auto&row:array(critical.get("devices"))){const auto name=row.get("name").text;if(name.empty())continue;auto&device=out.critical.devices[name];device.hp=row.get("hp").numeric();const auto pool=descriptor.critical.maxima.find(name);if(pool!=descriptor.critical.maxima.end())device.maximum=pool->second;const auto status=row.get("state").text;device.destroyed=status=="destroyed"||destroyed.count(name);device.critical=status=="critical"&&!device.destroyed;}
    for(const auto&name:destroyed)out.critical.devices[name].destroyed=true;
    for(const auto&row:array(state.get("equipment_states"))){const auto&contract=row.get("equipment");const auto kind=contract.get("kind").text;const auto uses=std::int64_t(row.get("usesLeft").numeric(-1.));if(kind=="extinguisher")out.config.fire_chance_factor*=std::max(0.,contract.get("fireStartingChanceFactor").numeric(1.));else if(kind=="medkit"&&uses!=0)out.config.medkit_bonus+=contract.get("bonusValue").numeric();if(uses!=0){out.stun_config.resist_duration+=contract.get("stunResistanceDuration").numeric();out.stun_config.resist_effect+=contract.get("stunResistanceEffect").numeric();}}
    out.stun_config.resist_duration=std::max(0.,std::min(1.,out.stun_config.resist_duration));out.stun_config.resist_effect=std::max(0.,std::min(1.,out.stun_config.resist_effect));
    const auto&dynamic=state.get("effective_params").get("crew").get("dynamic_spotting");std::uint64_t mask=0;const auto&roster=array(dynamic.get("crew"));if(roster.size()<=63)for(std::size_t i=0;i<roster.size();++i)if(out.critical.crew_ko.count(roster[i].text))mask|=std::uint64_t(1)<<i;
    const auto&factors=dynamic.get("states").get(std::to_string(mask)+":"+(out.critical.fire?"1":"0")).get("battle_factors");out.config.fire_chance_factor*=factors.get("engine_fire_factor").numeric(1.);return out;
}
}
