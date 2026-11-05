#include "room_strategy_data.hpp"
#include "room_strategy_ammo_data.hpp"
#include <cctype>

namespace wot::room::strategy {
std::vector<Value> orderedShells(const Value& profile,const Value& inventory,bool stages) {
    auto shells=rows(profile.get("shells"));
    const bool manual=has(profile,"ammo_ratios");const auto vehicle=s(profile,"vehicle_name"),nation=vehicle.substr(0,vehicle.find(':'));
    const auto special=ammo_data::guns.find(nation+":"+s(profile,"gun_name"));
    auto upper=[](std::string value){for(char& c:value)c=char(std::toupper(static_cast<unsigned char>(c)));return value;};
    if(!manual&&special!=ammo_data::guns.end()) {
        std::vector<Value> ordered;
        for(const auto& name:special->second) {
            auto found=std::find_if(shells.begin(),shells.end(),[&](const Value& shell){return s(shell,"name")==name;});
            if(found==shells.end()||std::count_if(shells.begin(),shells.end(),[&](const Value& shell){return s(shell,"name")==name;})!=1)throw std::invalid_argument("Special loadout shell missing or duplicated");
            ordered.push_back(*found);
        }
        shells=std::move(ordered);
    } else {
        auto priority=[&](const Value& shell){const auto kind=upper(s(shell,"kind"));const int score=kind=="ARMOR_PIERCING"?0:kind=="ARMOR_PIERCING_CR"?1:kind=="HOLLOW_CHARGE"?2:kind=="HIGH_EXPLOSIVE"?3:4;return std::make_pair(4*int(ammo_data::gold.count(nation+":"+s(shell,"name")))+score,intField(shell,"index"));};
        std::stable_sort(shells.begin(),shells.end(),[&](const Value& a,const Value& b){return priority(a)<priority(b);});
    }
    if(manual) {
        const auto& ratios=rows(profile.get("ammo_ratios"));std::vector<Value> filtered;
        for(std::size_t i=0;i<shells.size()&&i<ratios.size();++i)if(ratios[i].numeric()>0.)filtered.push_back(shells[i]);shells=std::move(filtered);
    } else if(s(profile,"class_tag")=="SPG")shells.erase(std::remove_if(shells.begin(),shells.end(),[&](const Value& shell){return upper(s(shell,"kind"))!="HIGH_EXPLOSIVE";}),shells.end());
    else if(!shells.empty()&&upper(s(shells.front(),"kind"))=="HIGH_EXPLOSIVE") {
        std::vector<Value> filtered{shells.front()};for(std::size_t i=1;i<shells.size();++i)if(upper(s(shells[i],"kind"))!="HIGH_EXPLOSIVE"){filtered.push_back(shells[i]);break;}shells=std::move(filtered);
    }
    if(stages&&!manual&&s(profile,"class_tag")!="SPG"&&!shells.empty()&&upper(s(shells.front(),"kind"))!="HIGH_EXPLOSIVE")std::stable_partition(shells.begin(),shells.end(),[&](const Value& shell){return upper(s(shell,"kind"))!="HIGH_EXPLOSIVE";});
    if(!null(inventory)) {
        const auto& remaining=rows(inventory);shells.erase(std::remove_if(shells.begin(),shells.end(),[&](const Value& shell){const auto index=intField(shell,"index");return index<0||static_cast<std::uint64_t>(index)>=remaining.size()||remaining[static_cast<std::size_t>(index)].numeric()<=0.;}),shells.end());
    }
    return shells;
}
}
namespace wot::room {
using namespace strategy;
int Planner::selectedShell(const Value& bot,const Value&) {
    const auto& state=bot.get("state");const auto shells=orderedShells(bot.get("profile"),state.get("ammo_remaining"));
    const auto selected=intField(state,"ammo_policy_shell_index",-1);for(const auto& shell:shells)if(intField(shell,"index")==selected)return int(selected);
    return shells.empty()?0:int(intField(shells.front(),"index"));
}
void Planner::setTarget(Value& order,const Value& bot,const Value& contact,bool fire) {
    target(order,contact);order.object["fire_allowed"]=boolean(fire&&weaponReady(bot));order.object["shell_index"]=integer(selectedShell(bot,contact));
}
}
