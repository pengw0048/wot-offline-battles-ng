#include "room_strategy_ammo_policy.hpp"

namespace wot::room {
using namespace strategy;
void Planner::refreshAmmoOrders() {
    for(auto& order:state_.bot_orders.array) {
        const ID actor=id(order);auto state=state_.bots.find(actor);if(state==state_.bots.end())continue;
        auto& policy=ammo_policies_[actor];if(!policy) {
            Value profile=object();for(const auto& row:rows(state_.bot_manifest))if(id(row)==actor){profile=row.get("profile");break;}
            policy=std::make_shared<ShotPolicy>(profile);
        }
        const auto target=b(order,"target_id")?sequence({order.get("target_kind"),order.get("target_id")}):Value();policy->context(state->second,target);const int selected=policy->choose(state->second);
        state->second.object["ammo_policy_shell_index"]=integer(selected);order.object["shell_index"]=integer(selected);
    }
}
void Planner::drainAmmo() {
    auto pending=std::move(state_.pending_ammo);state_.pending_ammo=array();
    for(const auto& event:rows(pending)) {
        const ID actor=intField(event,"bot_id");auto& policy=ammo_policies_[actor];if(!policy) {
            Value profile=object();for(const auto& row:rows(state_.bot_manifest))if(id(row)==actor){profile=row.get("profile");break;}
            policy=std::make_shared<ShotPolicy>(profile);
        }
        const auto type=s(event,"type");
        if(type=="launch") {
            const auto state=state_.bots.find(actor);policy->choose(state==state_.bots.end()?object():state->second);Value target;
            for(const auto& order:rows(state_.bot_orders))if(id(order)==actor&&b(order,"target_id")){target=sequence({order.get("target_kind"),order.get("target_id")});break;}
            policy->launch(intField(event,"shot_seq"),int(intField(event,"shell_index")),intField(event,"burst_group_seq"),int(intField(event,"burst_count")),event.get("position"),target);
        } else if(type=="terminal")policy->resolve(intField(event,"shot_seq"),b(event,"hit"),n(event,"damage"),b(event,"penetrated"),b(event,"known",true));
        else throw std::invalid_argument("Unknown canonical ammunition policy event");
        refreshAmmoOrders();
    }
}
}
