#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
void completed(Value& state,const char* field,int index) { auto& a=ensure(state,field,array());if(!contains(a,integer(index)))a.array.push_back(integer(index)); }
}
void Planner::automaticGoal(Value& order,const Value& bot,const std::vector<Value>& manifest,double now) {
    const auto actor=id(bot);const auto& pose=bot.get("state");const auto route_id=s(order,"route_id"),mode=s(order,"combat_mode");
    const bool donor=routeMobile(bot)&&!emptyActive(bot);auto progress=wreck_progress_.find(actor);
    if((oneOf(mode,{"support_hold","base_defense","advance_contact"})||b(order,"defense_base_id"))&&!b(order,"team_command")&&donor) {
        if(progress!=wreck_progress_.end()){ensure(progress->second,"paused_at",number(now));const auto& key=rows(progress->second.get("key"));order.object["route_attempt"]=record({{"route_id",key[0]},{"route_index",key[1]},{"action",text("pause_"+mode)},{"elapsed",number(rounded(n(progress->second,"paused_at")-n(progress->second,"since")))}});}return;
    }
    if(!oneOf(mode,{"route","advance","parking_approach","base_capture","withdraw","under_fire_withdraw","low_health_retreat"})||b(order,"team_command")||(!null(order.get("throttle_override"))&&n(order,"throttle_override")<=0.)||!donor){wreck_progress_.erase(actor);return;}
    const auto position=point(pose),goal=point(order.get("move_position"));const bool physical=b(pose,"route_wreck_blocked");const double d=distance(goal,position);const auto& points=rows(route(bot).get("waypoints"));const double arrival=std::max(points.size()<=1?1.5:6.,n(order,"arrival_radius"));
    auto trace=record({{"route_id",text(route_id)},{"route_index",order.get("route_index")},{"distance",number(rounded(d))},{"arrival",number(arrival)},{"physical_block",boolean(physical)},{"action",text("attempt")},{"elapsed",number(0.)}});
    auto publish=[&]{order.object["route_attempt"]=trace;};
    if(!physical&&(!b(pose,"world_pose")||d<=arrival)){trace.object["action"]=text("arrived_or_unowned");wreck_progress_.erase(actor);publish();return;}
    const auto key=sequence({text(route_id),order.get("route_index")});
    if(progress==wreck_progress_.end()||progress->second.get("key")!=key) {
        wreck_progress_[actor]=record({{"key",key},{"goal",goal},{"since",number(now)},{"best_distance",number(d)},{"origin",position},{"physical_block",boolean(physical)}});trace.object["attempt_started_at"]=number(now);trace.object["reset_reason"]=text("new_gate");publish();return;
    }
    auto& p=progress->second;const bool previous=b(p,"physical_block");p.object["physical_block"]=boolean(physical);trace.object["previous_physical_block"]=boolean(previous);trace.object["blocker_changed"]=boolean(previous!=physical);
    if(has(p,"paused_at")){const double duration=std::max(0.,now-n(p,"paused_at"));p.object["since"]=number(n(p,"since")+duration);p.object.erase("paused_at");trace.object["resumed_after_pause"]=number(duration);}
    trace.object["attempt_started_at"]=p.get("since");trace.object["attempt_origin"]=p.get("origin");
    const double best=n(p,"best_distance",distance(goal,p.get("origin"))),progress_distance=distance(p.get("goal"),position),improvement=best-progress_distance;
    trace.object["best_distance"]=number(rounded(best));trace.object["improvement"]=number(rounded(improvement));trace.object["progress_goal"]=p.get("goal");trace.object["progress_distance"]=number(rounded(progress_distance));
    if(physical&&improvement>=.5) {
        p.object["since"]=number(now);p.object["origin"]=position;p.object["best_distance"]=number(progress_distance);trace.object["action"]=text("physical_progress_renewed");trace.object["elapsed"]=number(0.);trace.object["attempt_started_at"]=number(now);trace.object["attempt_origin"]=position;publish();return;
    }
    if(!physical&&distance(position,p.get("origin"))>=6.&&distance(goal,p.get("origin"))-d>=4.){p.object["since"]=number(now);p.object["origin"]=position;p.object["goal"]=goal;p.object["best_distance"]=number(std::min(best,d));publish();return;}
    trace.object["elapsed"]=number(rounded(now-n(p,"since")));if(now-n(p,"since")<20.){publish();return;}
    auto& state=entry(route_states_,actor);const int index=int(intField(order,"route_index"));
    auto clear_claim=[&]{for(auto i=wait_claims_.begin();i!=wait_claims_.end();)if(intField(i->second,"bot_id")==actor&&std::get<0>(i->first)==team(bot)&&std::get<1>(i->first)==route_id&&std::get<2>(i->first)==index)i=wait_claims_.erase(i);else ++i;};
    auto republish=[&](const char* action) {
        const auto next=routePosition(bot,now);order.object["route_id"]=text(next.route);order.object["route_index"]=integer(next.index);order.object["combat_mode"]=text("route");order.object["parking_phase"]=Value();order.object["parking_slot"]=Value();order.object["arrival_radius"]=Value();order.object["move_position"]=next.move;order.object["face_position"]=next.move;order.object["route_anchor"]=next.anchor;order.object["route_join"]=boolean(next.join);authoredOrder(order,bot,next.move,now);
        trace.object["action"]=text(action);trace.object["next_route"]=text(next.route);trace.object["next_index"]=integer(next.index);
        wreck_progress_[actor]=record({{"key",sequence({text(next.route),integer(next.index)})},{"goal",point(next.move)},{"since",number(now)},{"origin",position},{"physical_block",boolean(physical)}});
    };
    if(mode=="parking_approach") {
        completed(state,"parking_completed",index);completed(state,"parking_skipped",index);erase(state,{"parking_phase","parking_slot"});clear_claim();
        republish("skip_parking");order.object["parking_skip_reason"]=text("blocked_approach_timeout");publish();return;
    }
    if(intField(state,"blocked_skip_to",-1)!=index&&index+1<int(points.size())) {
        state.object["index"]=integer(index+1);state.object["blocked_skip_to"]=integer(index+1);state.object["join_index"]=integer(index+1);state.object["join_anchor"]=position;completed(state,"parking_completed",index);erase(state,{"parking_phase","parking_slot"});clear_claim();
        republish("skip_gate");order.object["route_point_skip_reason"]=text("blocked_timeout");order.object["previous_route_index"]=integer(index);publish();return;
    }
    auto& avoided=entry(wreck_avoid_,actor);avoided.object[route_id]=number(now+120.);std::vector<Value> allies;for(const auto& row:manifest)if(team(row)==team(bot))allies.push_back(row);const auto catalog=routeCatalog(allies);const auto class_tag=s(bot.get("profile"),"class_tag");
    struct Candidate {double affinity,nearest;std::string name;Value route;int entry;};std::vector<Candidate> candidates;std::vector<double> retry;auto checks=array();
    for(const auto& row:catalog) {
        const auto& selected=row.second;auto check=record({{"route",text(row.first)},{"outcome",text("eligible")}});
        if(row.first==route_id){check.object["outcome"]=text("current_route");checks.array.push_back(check);continue;}
        const auto& config=routeConfig(bot,selected);
        if(config.truth()&&(b(config,"disabled")||team(config)!=team(bot)||!contains(config.get("classes"),text(class_tag))||(!rows(config.get("slots")).empty()&&!contains(config.get("slots"),bot.get("slot"))))) {check.object["outcome"]=text("class_mismatch");checks.array.push_back(check);continue;}
        const double affinity=selected.get("class_weights").get(class_tag).numeric(.5);
        if(affinity<.20){check.object["outcome"]=text("class_affinity");check.object["affinity"]=number(affinity);checks.array.push_back(check);continue;}
        const auto& waypoints=rows(selected.get("waypoints"));if(waypoints.empty())continue;int entry=0;for(std::size_t i=1;i<waypoints.size();++i)if(distance(waypoints[i],position)<distance(waypoints[entry],position))entry=int(i);
        double nearest=distance(waypoints[entry],position);while(entry<int(waypoints.size())-1&&distance(waypoints[entry],position)<30.)++entry;
        check.object["entry"]=integer(entry);check.object["point"]=point(waypoints[entry]);
        if(distance(waypoints[entry],goal)<16.) {check.object["outcome"]=text("same_failed_gate");checks.array.push_back(check);continue;}
        check.object["affinity"]=number(affinity);check.object["nearest_distance"]=number(nearest);check.object["entry_distance"]=number(distance(waypoints[entry],position));check.object["retry_after"]=number(avoided.get(row.first).numeric());checks.array.push_back(check);
        if(avoided.get(row.first).numeric()>now)retry.push_back(avoided.get(row.first).numeric());else candidates.push_back({-affinity,nearest,row.first,selected,entry});
    }
    trace.object["entry_selection"]=record({{"sampled_at",number(now)},{"origin",position},{"policy",text("existing_affinity_then_nearest_route_point")},{"candidates",checks}});
    if(candidates.empty()) {
        if(!retry.empty())trace.object["retry_after"]=number(*std::min_element(retry.begin(),retry.end()));trace.object["action"]=text("no_alternative");p.object["since"]=number(now);publish();return;
    }
    const auto chosen=*std::min_element(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return std::tie(a.affinity,a.nearest,a.name)<std::tie(b.affinity,b.nearest,b.name);});recordAssignment(bot,now,route_id,chosen.name,"wreck_stall");
    entry(assignment_receipts_,actor).object["entry_selection"]=record({{"sampled_at",number(now)},{"origin",position},{"policy",text("existing_affinity_then_nearest_route_point")},{"candidates",checks},{"selected_route",text(chosen.name)},{"selected_index",integer(chosen.entry)},{"selected_point",point(rows(chosen.route.get("waypoints"))[chosen.entry])}});
    route_assignments_[actor]=record({{"route",chosen.route},{"until",number(0.)}});const auto saved=lookup(route_history_,actor).get(chosen.name);
    route_states_[actor]=record({{"route_id",text(chosen.name)},{"index",integer(chosen.entry)},{"join_index",integer(chosen.entry)},{"join_anchor",position},{"parking_completed",saved.get("parking_completed")},{"parking_skipped",saved.get("parking_skipped")}});wreck_progress_.erase(actor);
    const auto next=routePosition(bot,now);order.object["route_id"]=text(next.route);order.object["route_index"]=integer(next.index);order.object["combat_mode"]=text("route");order.object["parking_phase"]=Value();order.object["parking_slot"]=Value();order.object["arrival_radius"]=Value();order.object["move_position"]=next.move;order.object["face_position"]=next.move;order.object["route_anchor"]=next.anchor;order.object["route_join"]=boolean(next.join);order.object["route_switch_reason"]=text(physical?"wreck_stall":"no_route_progress");order.object["previous_route_id"]=text(route_id);trace.object["action"]=text("switch_route");trace.object["next_route"]=text(next.route);trace.object["next_index"]=integer(next.index);authoredOrder(order,bot,next.move,now);publish();
}
}
