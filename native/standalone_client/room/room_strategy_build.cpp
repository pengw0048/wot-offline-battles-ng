#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
Identity storedIdentity(const Value& raw) {const auto& r=rows(raw);return r.size()==2?Identity{r[0].resolved().text,exactInteger(r[1]).value_or(0)}:Identity{};}
Value orderSignature(Value order) {
    erase(order,{"route_attempt","route_assignment","tactical_evidence"});if(!null(order.get("target_id"))){erase(order,{"aim_position","face_position"});if(s(order,"combat_mode")=="advance_contact")order.object.erase("move_position");}return order;
}
}
void Planner::pruneTactics(const std::vector<Value>& bots,double now) {
    const auto targets=knownTargets();Records live;double largest=std::hypot(3.5,1.7);for(const auto& bot:bots){live[id(bot)]=bot;largest=std::max(largest,n(bot.get("profile"),"parking_radius",std::hypot(3.5,1.7)));}
    for(Records* map:{&capture_arrivals_,&travel_tasks_,&previous_player_,&route_recoveries_,&recovery_route_visits_,&wreck_progress_,&wreck_avoid_,&route_states_,&route_history_,&route_assignments_,&target_assignments_,&combat_states_,&retreat_states_,&cover_failures_,&engage_anchors_,&firing_holds_,&artillery_anchors_})for(auto i=map->begin();i!=map->end();)if(!live.count(i->first))i=map->erase(i);else ++i;
    for(auto i=previous_combat_.begin();i!=previous_combat_.end();)if(!live.count(*i))i=previous_combat_.erase(i);else ++i;
    for(auto i=wait_claims_.begin();i!=wait_claims_.end();) {
        const auto actor=intField(i->second,"bot_id");const auto owner=live.find(actor);const auto& stage=lookup(route_states_,actor);bool departed=false;
        if(owner!=live.end()&&b(i->second,"departing"))departed=distance(owner->second.get("state"),point(i->second.get("point")))>n(i->second,"radius")+largest+2.;
        const bool changed=s(stage,"route_id")!=std::get<1>(i->first)||intField(stage,"index",-1)!=std::get<2>(i->first);
        if(owner==live.end()||departed||(changed&&!b(i->second,"departing")))i=wait_claims_.erase(i);else ++i;
    }
    for(auto i=recent_hits_.begin();i!=recent_hits_.end();) {
        auto target=targets.find(storedIdentity(i->second.get("attacker")));
        if(!live.count(i->first)||target==targets.end()||!b(target->second,"alive")||now-n(i->second,"reported_at")>6.)i=recent_hits_.erase(i);else ++i;
    }
    for(auto i=affordances_.begin();i!=affordances_.end();) {
        const auto bot=live.find(i->first);const auto key=storedIdentity(i->second.get("target"));const auto target=targets.find(key);bool visible=false;
        if(bot!=live.end()){auto side=contacts_.find(team(bot->second));if(side!=contacts_.end()){auto contact=side->second.find(key);visible=contact!=side->second.end()&&b(contact->second,"visible");}}
        if(bot==live.end()||target==targets.end()||!b(target->second,"alive")||!visible||now-n(i->second,"reported_at")>12.)i=affordances_.erase(i);else ++i;
    }
    for(auto i=cover_states_.begin();i!=cover_states_.end();) {const auto report=affordances_.find(i->first);if(!live.count(i->first)||report==affordances_.end()||i->second.get("target")!=report->second.get("target"))i=cover_states_.erase(i);else ++i;}
    for(auto i=cover_failures_.begin();i!=cover_failures_.end();) {
        std::vector<std::string> expired;for(const auto& row:i->second.object)if(now>=row.second.numeric())expired.push_back(row.first);for(const auto& key:expired)i->second.object.erase(key);if(i->second.object.empty())i=cover_failures_.erase(i);else ++i;
    }
    for(auto i=combat_states_.begin();i!=combat_states_.end();)if(!targets.count(storedIdentity(i->second.get("target"))))i=combat_states_.erase(i);else ++i;
}
void Planner::build(double now) {
    advanceRadio();setTeamCommands(state_.team_commands);
    wait_bodies_=array();std::map<ID,double> radii;for(const auto& row:rows(state_.bot_manifest))radii[id(row)]=n(row.get("profile"),"parking_radius",std::hypot(3.5,1.7));
    for(const auto& row:state_.bots){auto body=row.second;body.object["parking_kind"]=text("bot");body.object["parking_radius"]=number(radii.count(row.first)?radii[row.first]:std::hypot(3.5,1.7));wait_bodies_.array.push_back(std::move(body));}
    for(const auto& row:state_.players)if(b(row.second,"connected",true)&&b(row.second,"participating",true)){auto body=row.second;body.object["parking_kind"]=text("human");wait_bodies_.array.push_back(std::move(body));}
    prune(now);auto bots=aliveBots();pruneTactics(bots,now);emptyUpdate(bots);const auto& context=state_.rules.get("capture_context");auto defenders=updateDefense(bots,context,now);cover_reservations_.clear();const auto commands=teamOrders(bots);auto orders=array();target_decisions_.clear();
    for(int side:{1,2}) {
        std::vector<Value> allies,contacts;for(const auto& bot:bots)if(team(bot)==side)allies.push_back(bot);std::sort(allies.begin(),allies.end(),[](const Value& a,const Value& b){return id(a)<id(b);});
        for(const auto& row:contacts_[side])contacts.push_back(row.second);const auto axis=teamAxis(side,bots),capture=captureTarget(context,side);std::set<ID> protected_ids;for(const auto& row:defenders[side])protected_ids.insert(row.first);const auto capturing=contributors(context,3-side);
        for(const auto& bot:allies)if(capturing.count({"bot",id(bot)})||(!null(capture)&&(lookup(capture_arrivals_,id(bot))==capture||(b(bot.get("state"),"world_pose")&&!b(bot.get("state"),"airborne")&&inside(bot.get("state"),capture)))))protected_ids.insert(id(bot));
        rebalanceRoutes(side,allies,contacts,now,protected_ids);auto assignments=assignTargets(allies,contacts,now);prioritizeInvaders(side,allies,assignments,defenders[side],context,now);
        for(const auto& bot:allies) {
            const auto focus=teamFocus(lookup(commands,id(bot)),bot,now);if(!null(focus)&&!assignments.count(id(bot))&&!defenders[side].count(id(bot)))assignments[id(bot)]=focus;
        }
        std::map<Identity,std::vector<Value>> groups;for(const auto& assignment:assignments) {
            auto bot=std::find_if(allies.begin(),allies.end(),[&](const Value& b){return id(b)==assignment.first;});if(bot!=allies.end()&&!b(assignment.second,"movement_lease")&&weaponReady(*bot))groups[identity(assignment.second)].push_back(*bot);
        }
        for(const auto& group:groups)if(group.second.size()>=2) {
            const auto& focus=assignments.at(id(group.second.front()));const auto suppressor=*std::min_element(group.second.begin(),group.second.end(),[&](const Value& a,const Value& b){return std::make_tuple(n(a.get("profile").get("roles"),"flanker"),distance(a.get("state"),focus.get("position")),id(a))<std::make_tuple(n(b.get("profile").get("roles"),"flanker"),distance(b.get("state"),focus.get("position")),id(b));});auto assigned=array();for(const auto& bot:group.second)assigned.array.push_back(integer(id(bot)));
            for(const auto& bot:group.second){assignments[id(bot)].object["assigned_shooter_ids"]=assigned;assignments[id(bot)].object["flank_suppressor_bot_ids"]=sequence({integer(id(suppressor))});}
        }
        int index=0;for(const auto& bot:allies) {
            const ID actor=id(bot);const auto local_contacts=contactsFor(bot,now);auto order=orderFor(bot,index++,int(allies.size()),lookup(assignments,actor),local_contacts,now,lookup(defenders[side],actor),axis,allies,capture,local_contacts.empty(),capturing.count({"bot",actor}));
            const auto route_point=order.get("_route_position"),turnback=order.get("_turnback_position");erase(order,{"_route_position","_turnback_position"});auto player=lookup(commands,actor);
            const bool automatic=defenders[side].count(actor)||route_recoveries_.count(actor)||oneOf(s(lookup(travel_tasks_,actor),"kind"),{"defense","pressure"});
            if(taskCombat(order)||automatic||emptyActive(bot)){if(player.truth())order.object["player_command_cancelled"]=boolean(true);player=Value();}
            else applyTeamOrder(order,bot,player,context,route_point,turnback);
            taskApply(order,bot,now,lookup(defenders[side],actor),player,context);taskCaptureHandoff(order,bot,capture);authoredOrder(order,bot,route_point,now);terminalOrder(order,bot,capture,now);recoverRoute(order,bot,bots,now);
            if(oneOf(s(order.get("route_attempt"),"action"),{"recovery_complete","switch_at_start"})&&!route_recoveries_.count(actor))terminalOrder(order,bot,capture,now);protectCapture(order,bot,capture);
            auto& evidence=ensure(order,"tactical_evidence");evidence.object["retreat_after"]=null(lookup(retreat_states_,actor))?object():lookup(retreat_states_,actor);auto final=object();for(const char* key:{"combat_mode","tactical_phase","target_id","target_kind","fire_allowed","throttle_override","move_position","face_position"})final.object[key]=order.get(key);evidence.object["final"]=std::move(final);
            auto receipt=lookup(assignment_receipts_,actor);if(null(receipt))receipt=object();auto progress=lookup(route_states_,actor).get("progress_receipt");if(null(progress))progress=record({{"outcome",text("no_active_route_evaluation")},{"at",Value()}});progress.object["observation"]=text(!null(progress.get("at"))&&n(progress,"at")==now?"evaluated":"previous_or_unavailable");progress.object["age_seconds"]=null(progress.get("at"))?Value():number(now-n(progress,"at"));progress.object["final_order"]=record({{"mode",order.get("combat_mode")},{"route_id",order.get("route_id")},{"route_index",order.get("route_index")},{"move_position",order.get("move_position")}});receipt.object["progress"]=std::move(progress);
            auto balance=balance_receipts_[side];if(null(balance))balance=object();erase(balance,{"contact_inputs","contributor_inputs"});const auto candidates=balance.get("candidates");balance.object.erase("candidates");balance.object["observed_at"]=number(now);Value candidate;for(const auto& c:rows(candidates))if(intField(c,"bot")==actor){candidate=c;break;}balance.object["candidate"]=candidate;receipt.object["balance"]=balance;
            const auto& lease=lookup(route_assignments_,actor);if(n(lease,"until")>0.)receipt.object["lease"]=record({{"until",lease.get("until")},{"check",lease.get("_lease_check")},{"observation",lease.get("_lease_observation")}});order.object["route_assignment"]=std::move(receipt);orders.array.push_back(std::move(order));
        }
    }
    std::sort(orders.array.begin(),orders.array.end(),[](const Value& a,const Value& b){return id(a)<id(b);});auto signature=array();for(const auto& order:rows(orders))signature.array.push_back(orderSignature(order));if(signature!=last_signature_){++state_.bot_order_revision;last_signature_=std::move(signature);}state_.bot_orders=std::move(orders);
    std::set<ID> cancelled;for(const auto& order:rows(state_.bot_orders))if(b(order,"player_command_cancelled"))cancelled.insert(id(order));cancelRadio(cancelled);
    refreshAmmoOrders();
    for(const auto& cursor:route_states_)if(!s(cursor.second,"route_id").empty())entry(route_history_,cursor.first).object[s(cursor.second,"route_id")]=cursor.second;
}
}
