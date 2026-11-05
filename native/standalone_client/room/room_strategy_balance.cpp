#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
bool matches(const Value& config,const Value& bot) { return !b(config,"disabled")&&team(config)==team(bot)&&contains(config.get("classes"),bot.get("profile").get("class_tag"))&&(rows(config.get("slots")).empty()||contains(config.get("slots"),bot.get("slot"))); }
Value numericMap(const std::map<std::string,double>& values) { auto out=object();for(const auto& row:values)out.object[row.first]=number(row.second);return out; }
}
std::string Planner::taskFamily(const Value& selected,int side) const {
    const auto bot=record({{"team",integer(side)}}),config=routeConfig(bot,selected);
    if(config.truth())return !b(config,"default")?"user_"+s(config,"family",s(config,"id")):s(config,"source_id");
    return s(selected,"_route_family",s(selected,"_editor_source",s(selected,"id")));
}
Value Planner::taskClassRoute(const Value& selected,const Value& bot,const std::map<std::string,Value>& catalog) const {
    const auto family=taskFamily(selected,team(bot)),tag=s(bot.get("profile"),"class_tag");std::vector<Value> choices;
    for(const auto& row:catalog) {
        const auto config=routeConfig(bot,row.second);
        if(taskFamily(row.second,team(bot))==family&&(!config.truth()||matches(config,bot))&&row.second.get("class_weights").get(tag).numeric(1.)>=.20)choices.push_back(row.second);
    }
    if(!choices.empty())return *std::min_element(choices.begin(),choices.end(),[&](const Value& a,const Value& b){return std::make_pair(s(a,"id")!=s(selected,"id"),s(a,"id"))<std::make_pair(s(b,"id")!=s(selected,"id"),s(b,"id"));});
    // Validated class variants are cold admission results from the exact
    // authored/local-mesh graph contract. A coarse graph cannot replace them.
    const auto& proof=state_.resources.get("strategy").get("class_routes");
    const auto& variants=rows(proof.get(std::to_string(team(bot))).get(family));
    for(const auto& variant:variants)if(s(variant,"class_tag","all")==tag)return variant.get("route");
    for(const auto& variant:variants)if(s(variant,"class_tag","all")=="all")return variant.get("route");
    const auto& settings=state_.resources.get("strategy").get("tactics").get("maps").get(state_.map_name);
    if(graph_&&!has(state_.resources.get("strategy"),"class_routes"))for(const auto& row:rows(settings.get("default_routes")))
        if(team(row)==team(bot)&&s(row,"id")==family&&!b(row,"disabled")&&oneOf(s(row,"class_tag","all"),{"all",tag.c_str()}))throw std::logic_error("Missing cold authored class-route admission proofs");
    return {};
}
std::string Planner::nearestRoute(const Value& contact,const std::map<std::string,Value>& catalog) {
    std::string best;std::pair<double,std::string> key{std::numeric_limits<double>::infinity(),{}};
    for(const auto& row:catalog) {
        double d=1.e18;for(const auto& point:rows(row.second.get("waypoints"))){const double current=distance(point,contact.get("position"));d=std::min(d,current*current);}
        const auto candidate=std::make_pair(d,row.first);if(candidate<key){key=candidate;best=row.first;}
    }
    return best;
}
void Planner::observeLeases(const std::vector<Value>& bots,double now,const std::string& outcome,bool evaluation,const Value& extra) {
    for(const auto& bot:bots) {
        const ID actor=id(bot);if(route_recoveries_.count(actor)||travel_tasks_.count(actor))continue;auto lease=route_assignments_.find(actor);if(lease==route_assignments_.end()||n(lease->second,"until")<=0.)continue;
        auto receipt=dict(extra)?extra:object();receipt.object["evaluated_at"]=number(now);receipt.object["outcome"]=text(outcome);receipt.object["until"]=lease->second.get("until");lease->second.object["_lease_check"]=receipt;if(evaluation)lease->second.object["_lease_observation"]=receipt;
    }
}
void Planner::rebalanceRoutes(int side,const std::vector<Value>& bots,const std::vector<Value>& contacts,double now,std::set<ID> protected_ids) {
    if(now<next_rebalance_[side]){observeLeases(bots,now,"rebalance_not_due",false);return;}next_rebalance_[side]=now+180.;protected_ids.insert(previous_combat_.begin(),previous_combat_.end());
    for(const auto& bot:bots){const auto config=routeConfig(bot,bot.get("route"));if(config.truth()&&s(config,"policy")=="fixed")protected_ids.insert(id(bot));}
    auto catalog=routeCatalog(bots);auto retained=array();
    for(const auto& bot:bots) {
        const auto& assigned=lookup(route_assignments_,id(bot));const auto& selected=assigned.get("route");
        if((b(assigned,"ordered_task")||(b(assigned,"radio_scoped")&&n(assigned,"until")>now))&&dict(selected)&&!rows(selected.get("waypoints")).empty()) {const auto name=s(selected,"id");if(!name.empty()&&!catalog.count(name)){catalog[name]=selected;retained.array.push_back(text(name));}}
        const auto& task=lookup(travel_tasks_,id(bot));if(!null(task)){protected_ids.insert(id(bot));for(const char* field:{"route","target_route"})if(task.get(field).truth()&&!rows(task.get(field).get("waypoints")).empty())catalog[s(task.get(field),"id")]=task.get(field);}
    }
    auto family=[&](const std::string& name){auto found=catalog.find(name);return taskFamily(found==catalog.end()?record({{"id",text(name)}}):found->second,side);};
    for(const auto& bot:bots)if(route_recoveries_.count(id(bot))){const auto& selected=route(bot);catalog[s(selected,"id")]=selected;protected_ids.insert(id(bot));}
    for(const auto& bot:bots) {
        const ID actor=id(bot);if(route_recoveries_.count(actor)||travel_tasks_.count(actor)||emptyActive(bot))continue;auto assigned=route_assignments_.find(actor);const auto default_id=s(bot.get("route"),"id");const auto old=assigned==route_assignments_.end()?Value():assigned->second;const auto selected=old.get("route");const auto name=s(selected,"id");const double until=n(old,"until");bool knowledge=false;
        if(dict(old)&&b(old,"radio_scoped")){knowledge=true;for(const auto& c:contactsFor(bot,now))if(nearestRoute(c,catalog)==name){knowledge=false;break;}}
        if(knowledge||!catalog.count(name)||selected!=catalog[name]||(until>0.&&until<=now)) {
            if(!null(old)) {
                auto evidence=record({{"evaluated_at",number(now)},{"assigned_until",number(until)},{"knowledge_lost",boolean(knowledge)},{"catalog_contains_route",boolean(catalog.count(name))},{"catalog_route_matches",boolean(catalog.count(name)&&selected==catalog[name])},{"retained_assignment_routes",retained}});auto known=array();for(const auto& c:contactsFor(bot,now))known.array.push_back(record({{"contact",c},{"nearest_route",text(nearestRoute(c,catalog))}}));evidence.object["knowledge_contacts"]=known;auto causes=array();if(knowledge)causes.array.push_back(text("radio_knowledge_lost"));if(!catalog.count(name))causes.array.push_back(text("catalog_missing"));if(catalog.count(name)&&selected!=catalog[name])causes.array.push_back(text("catalog_changed"));if(until>0.&&until<=now)causes.array.push_back(text("lease_expired"));evidence.object["invalidation_causes"]=causes;recordAssignment(bot,now,name,default_id,knowledge?"radio_knowledge_lost":until>0.&&until<=now?"lease_expired":"catalog_reset",evidence);
            }
            if(catalog.count(default_id))route_assignments_[actor]=record({{"route",catalog[default_id]},{"until",number(0.)}});else route_assignments_.erase(actor);route_states_.erase(actor);
        }
    }
    if(catalog.size()<2){balance_receipts_[side]=record({{"sampled_at",number(now)},{"outcome",text("catalog_has_no_alternative")},{"candidates",array()}});observeLeases(bots,now,"catalog_has_no_alternative");return;}
    auto& balance=balance_receipts_[side];balance=record({{"sampled_at",number(now)},{"outcome",text("no_known_pressure")},{"selected_bot",Value()},{"candidates",array()}});
    std::map<std::string,double> pressure,counts,reservations;for(const auto& row:catalog){pressure[row.first]=0.;counts[row.first]=0.;reservations[row.first]=0.;}
    for(const auto& c:contacts) {
        bool knows=false;for(const auto& bot:bots)if(!has(c,"radio_bot_until")||c.get("radio_bot_until").get(std::to_string(id(bot))).numeric()>now){knows=true;break;}if(!knows)continue;
        const auto lane=nearestRoute(c,catalog);if(!lane.empty())pressure[lane]+=std::max(.3,n(c,"health",1.)/std::max(1.,n(c,"max_health",1.)));
    }
    std::map<std::string,double> totals;for(const auto& row:pressure)totals[family(row.first)]+=row.second;for(auto& row:pressure)row.second=totals[family(row.first)];
    if(pressure.empty()||std::max_element(pressure.begin(),pressure.end(),[](const auto& a,const auto& b){return a.second<b.second;})->second<=0.) {
        observeLeases(bots,now,"no_known_pressure");return;
    }
    auto contributes=[&](const Value& bot) {
        if(!emptyActive(bot)&&routeMobile(bot))return true;if(!b(bot.get("state"),"world_pose")||!weaponReady(bot))return false;
        for(const auto& c:contactsFor(bot,now))if(b(c,"visible")&&contains(c.get("shootable_by_bot_ids"),integer(id(bot))))return true;return false;
    };
    for(const auto& bot:bots) {
        if(s(bot.get("profile"),"class_tag")=="SPG"||travel_tasks_.count(id(bot)))continue;const auto lane=s(route(bot),"id");if(counts.count(lane)&&contributes(bot))++counts[lane];
    }
    totals.clear();for(const auto& row:counts)totals[family(row.first)]+=row.second;for(auto& row:counts)row.second=totals[family(row.first)];
    for(const auto& bot:bots){const auto& task=lookup(travel_tasks_,id(bot));const auto target=s(task.get("target_route"),"id");if(s(task,"kind")=="pressure"&&reservations.count(target))for(auto& row:reservations)if(family(row.first)==family(target))++row.second;}
    std::string target_route;double target_score=-std::numeric_limits<double>::infinity();for(const auto& row:catalog){const double score=pressure[row.first]-(counts[row.first]+reservations[row.first])*.45;if(score>target_score){target_route=row.first;target_score=score;}}
    balance.object["outcome"]=text("no_improving_donor");balance.object["target_route"]=text(target_route);balance.object["pressure"]=numericMap(pressure);balance.object["contributors"]=numericMap(counts);balance.object["reservations"]=numericMap(reservations);balance.object["target_score"]=number(target_score);
    auto contact_inputs=array(),contributor_inputs=array();for(const auto& c:contacts) {
        auto known_by=array();for(const auto& bot:bots)if(!has(c,"radio_bot_until")||c.get("radio_bot_until").get(std::to_string(id(bot))).numeric()>now)known_by.array.push_back(integer(id(bot)));
        contact_inputs.array.push_back(record({{"target_kind",c.get("target_kind")},{"target_id",c.get("id")},{"position",point(c.get("position"))},{"route",text(nearestRoute(c,catalog))},{"health",c.get("health")},{"max_health",c.get("max_health")},{"visible",c.get("visible")},{"radio_bot_until",has(c,"radio_bot_until")?c.get("radio_bot_until"):object()},{"known_by",known_by}}));
    }
    for(const auto& bot:bots)if(s(bot.get("profile"),"class_tag")!="SPG")contributor_inputs.array.push_back(record({{"bot",integer(id(bot))},{"route",text(s(route(bot),"id"))},{"contributes",boolean(contributes(bot))},{"position",point(bot.get("state"))}}));balance.object["contact_inputs"]=contact_inputs;balance.object["contributor_inputs"]=contributor_inputs;
    observeLeases(bots,now,target_score<=0.?"pressure_already_covered":"pressure_evaluated",true,record({{"target_route",text(target_route)},{"target_pressure",number(pressure[target_route])},{"target_contributors",number(counts[target_route])},{"target_score",number(target_score)}}));
    for(const auto& bot:bots) {
        const ID actor=id(bot);if(route_recoveries_.count(actor)||travel_tasks_.count(actor))continue;auto assigned=route_assignments_.find(actor);if(assigned==route_assignments_.end())continue;auto& assignment=assigned->second;const auto selected=assignment.get("route");const auto own=s(selected,"id");bool knows_target=false,knows_own=false;
        for(const auto& c:contactsFor(bot,now)){const auto lane=nearestRoute(c,catalog);knows_target=knows_target||family(lane)==family(target_route);knows_own=knows_own||lane==own;}
        const double own_score=pressure[own]-counts[own]*.45;const int contribution=contributes(bot)&&s(bot.get("profile"),"class_tag")!="SPG"?1:0;const double departure=own_score+contribution*.45;
        if(n(assignment,"until")>0.)assignment.object["_lease_observation"]=record({{"evaluated_at",number(now)},{"target_route",text(target_route)},{"own_route",text(own)},{"own_pressure",number(pressure[own])},{"target_pressure",number(pressure[target_route])},{"own_contributors",number(counts[own])},{"target_contributors",number(counts[target_route])},{"knows_target",boolean(knows_target)},{"knows_own",boolean(knows_own)},{"own_score",number(own_score)},{"own_contribution",integer(contribution)},{"departure_score",number(departure)},{"until",assignment.get("until")},{"outcome",text(target_score<=0.?"pressure_already_covered":!knows_target?"no_target_contact":"different_pressure_target")}});
        if(dict(selected)&&knows_own&&departure>0.&&n(assignment,"until")>0.){assignment.object["until"]=number(now+181.);assignment.object["_lease_observation"].object["outcome"]=text("renewed");assignment.object["_lease_observation"].object["until"]=assignment.get("until");}
    }
    if(target_score<=0.){balance.object["outcome"]=text("pressure_already_covered");return;}
    std::vector<std::tuple<double,ID,Value>> candidates;
    for(const auto& bot:bots) {
        const ID actor=id(bot);auto check=record({{"bot",integer(actor)},{"outcome",text("eligible")}});std::string refusal;
        if(travel_tasks_.count(actor)||route_recoveries_.count(actor)||b(bot.get("state"),"route_wreck_blocked"))refusal="route_recovery_in_progress";
        bool knows=false;for(const auto& c:contactsFor(bot,now))if(family(nearestRoute(c,catalog))==family(target_route)){knows=true;break;}
        if(refusal.empty()&&!knows)refusal="no_received_target_contact";if(refusal.empty()&&protected_ids.count(actor))refusal="protected";
        if(refusal.empty()&&oneOf(s(lookup(route_states_,actor),"parking_phase"),{"approach","waiting","queue"}))refusal="parking_in_progress";
        Value recipient;if(refusal.empty()){recipient=taskClassRoute(catalog[target_route],bot,catalog);if(null(recipient))refusal="authored_class_mismatch";}
        if(refusal.empty()){const auto config=routeConfig(bot,recipient);if(config.truth()&&!matches(config,bot))refusal="authored_class_mismatch";}
        if(refusal.empty()&&s(bot.get("profile"),"class_tag")=="SPG")refusal="artillery";if(refusal.empty()&&(emptyActive(bot)||!routeMobile(bot)))refusal="immobile_donor";
        const auto current=s(route(bot),"id");if(refusal.empty()){check.object["current_route"]=text(current);if(family(current)==family(target_route)||counts[current]<=1.)refusal=family(current)==family(target_route)?"already_on_target":"last_line_contributor";}
        const double own_score=pressure[current]-counts[current]*.45,before=std::max({0.,own_score,target_score}),after=std::max({0.,own_score+.45,target_score-.45});
        if(refusal.empty()){check.object["current_pressure_score"]=number(own_score);check.object["target_pressure_score"]=number(target_score);check.object["before_transfer"]=number(before);check.object["after_transfer"]=number(after);if(after+1.e-9>=before)refusal="no_pressure_improvement";}
        double affinity=.5;const auto tag=s(bot.get("profile"),"class_tag");if(refusal.empty()&&has(recipient.get("class_weights"),tag)){affinity=std::clamp(recipient.get("class_weights").get(tag).numeric(),0.,1.);if(affinity<.20)refusal="class_affinity_too_low";}
        if(refusal.empty()) {
            const auto& roles=bot.get("profile").get("roles");double role_affinity=0.;for(const auto& role:roles.resolved().object)role_affinity+=role.second.numeric()*catalog[target_route].get("role_weights").get(role.first).numeric();const double mobility=std::max({n(roles,"support"),n(roles,"flanker"),n(roles,"scout")}),adaptability=n(personality(actor),"adaptability"),brawler=n(roles,"brawler");const double score=mobility*2.+adaptability-brawler*.65-pressure[current]*.7+affinity*1.8+role_affinity*1.2;candidates.emplace_back(score,-actor,bot);
            check.object["score"]=number(score);check.object["tie_break"]=integer(-actor);check.object["mobility"]=number(mobility);check.object["adaptability"]=number(adaptability);check.object["brawler"]=number(brawler);check.object["current_pressure"]=number(pressure[current]);check.object["class_affinity"]=number(affinity);check.object["role_affinity"]=number(role_affinity);
        } else check.object["outcome"]=text(refusal);balance.object["candidates"].array.push_back(check);
    }
    if(candidates.empty())return;const auto donor=std::get<2>(*std::max_element(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return std::tie(std::get<0>(a),std::get<1>(a))<std::tie(std::get<0>(b),std::get<1>(b));}));balance.object["outcome"]=text("assigned");balance.object["selected_bot"]=integer(id(donor));recordAssignment(donor,now,s(route(donor),"id"),target_route,"pressure_rebalance",record({{"evaluated_at",number(now)},{"target_route",text(target_route)},{"pressure",numericMap(pressure)},{"contributors",numericMap(counts)},{"target_score",number(pressure[target_route]-counts[target_route]*.45)},{"contact_inputs",contact_inputs},{"contributor_inputs",contributor_inputs},{"candidates",balance.get("candidates")},{"selected_bot",integer(id(donor))}}));
    taskBegin(donor,"pressure",Value(),taskClassRoute(catalog[target_route],donor,catalog),now);
}
}
