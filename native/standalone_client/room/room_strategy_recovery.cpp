#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
void Planner::cycleLeg(const Value& bot,Value& recovery,const std::string& phase,int index,double now) {
    const auto& points=rows(route(bot).get("waypoints"));index=std::clamp(index,0,int(points.size())-1);
    recovery.object["phase"]=text(phase);recovery.object["index"]=integer(index);recovery.object["anchor"]=point(bot.get("state"));erase(recovery,{"last_pose","last_at"});dropParking(bot);
    auto& state=entry(route_states_,id(bot));state.object["route_id"]=recovery.get("route_id");state.object["index"]=integer(index);state.object["join_index"]=integer(index);state.object["join_anchor"]=recovery.get("anchor");state.object.erase("blocked_skip_to");
    const auto goal=point(points[index]),pose=point(bot.get("state"));
    wreck_progress_[id(bot)]=record({{"key",sequence({recovery.get("route_id"),integer(index)})},{"goal",goal},{"since",number(now)},
        {"origin",pose},{"best_distance",number(distance(goal,pose))},{"physical_block",boolean(b(bot.get("state"),"route_wreck_blocked"))}});
}
void Planner::cycleFinish(const Value& bot) {
    const ID actor=id(bot);auto i=route_recoveries_.find(actor);
    if(i!=route_recoveries_.end()) {
        auto& state=entry(route_states_,actor);state.object["route_id"]=i->second.get("route_id");state.object["index"]=i->second.get("index");state.object["join_index"]=i->second.get("index");state.object["join_anchor"]=point(bot.get("state"));route_recoveries_.erase(i);
    }
    wreck_progress_.erase(actor);dropParking(bot);
}
void Planner::cycleRestart(const Value& bot,Value& recovery,double now) {
    recovery.object["round"]=integer(intField(recovery,"round")+1);cycleLeg(bot,recovery,"forward",int(intField(recovery,"failed")),now);
}
void Planner::cyclePublish(Value& order,const Value& bot,double now) {
    const auto position=routePosition(bot,now);order.object["route_id"]=text(position.route);order.object["route_index"]=integer(position.index);
    order.object["combat_mode"]=text("route");order.object["move_position"]=position.move;order.object["face_position"]=position.move;
    order.object["route_anchor"]=position.anchor;order.object["route_join"]=boolean(position.join);order.object["throttle_override"]=Value();order.object["arrival_radius"]=Value();order.object["parking_phase"]=Value();order.object["parking_slot"]=Value();erase(order,{"parking_heading","move_area_bounds"});
    if(has(order,"route_attempt")) {
        auto& trace=order.object["route_attempt"];const auto& recovery=lookup(route_recoveries_,id(bot));trace.object["phase"]=text(null(recovery)?"complete":s(recovery,"phase"));
        trace.object["next_route"]=text(position.route);trace.object["next_index"]=integer(position.index);trace.object["cycle"]=recovery.get("round");
    }
    if(!route_recoveries_.count(id(bot)))authoredOrder(order,bot,position.move,now);
}
std::string Planner::cycleChangeRoute(Value& order,const Value& bot,const std::vector<Value>& manifest,Value& recovery,double now) {
    const auto previous=s(recovery,"route_id");std::vector<Value> allies;for(const auto& row:manifest)if(team(row)==team(bot))allies.push_back(row);
    const auto catalog=routeCatalog(allies);auto& visits=entry(recovery_route_visits_,id(bot));visits.object[previous]=integer(intField(visits,previous)+1);
    const auto tag=s(bot.get("profile"),"class_tag");using Candidate=std::tuple<ID,double,std::string,Value>;std::vector<Candidate> candidates;
    for(const auto& row:catalog) {
        const auto& selected=row.second;if(row.first==previous||rows(selected.get("waypoints")).empty())continue;
        const auto& config=routeConfig(bot,selected);
        if(config.truth()&&(b(config,"disabled")||team(config)!=team(bot)||!contains(config.get("classes"),text(tag))||(!rows(config.get("slots")).empty()&&!contains(config.get("slots"),bot.get("slot")))))continue;
        const double affinity=selected.get("class_weights").get(tag).numeric(.5);
        if(affinity<.20||contains(selected.get("_editor_disabled_classes"),text(tag)))continue;
        candidates.emplace_back(intField(visits,row.first),-affinity,row.first,selected);
    }
    if(candidates.empty()){cycleRestart(bot,recovery,now);return "retry_no_alternative";}
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return std::tie(std::get<0>(a),std::get<1>(a),std::get<2>(a))<std::tie(std::get<0>(b),std::get<1>(b),std::get<2>(b));});
    const auto selected=std::get<3>(candidates.front());const auto next=s(selected,"id");recordAssignment(bot,now,previous,next,"recovery_at_start");
    auto checks=array();for(const auto& c:candidates)checks.array.push_back(record({{"route",text(std::get<2>(c))},{"outcome",text("eligible")},{"visits",integer(std::get<0>(c))},{"affinity",number(-std::get<1>(c))},{"entry_index",integer(0)},{"entry_distance",number(distance(point(bot.get("state")),point(rows(std::get<3>(c).get("waypoints"))[0])))}}));
    entry(assignment_receipts_,id(bot)).object["entry_selection"]=record({{"sampled_at",number(now)},{"origin",point(bot.get("state"))},{"policy",text("least_tried_then_affinity_at_first_point")},{"candidates",std::move(checks)},{"selected_route",text(next)},{"selected_index",integer(0)},{"selected_point",point(rows(selected.get("waypoints"))[0])}});
    route_assignments_[id(bot)]=record({{"route",selected},{"until",number(0.)}});route_states_[id(bot)]=record({{"route_id",text(next)},{"index",integer(0)}});entry(route_history_,id(bot)).object.erase(next);
    recovery.object["route_id"]=text(next);recovery.object["failed"]=integer(std::min(1,int(rows(selected.get("waypoints")).size())-1));cycleLeg(bot,recovery,"transfer",0,now);
    if(distance(point(bot.get("state")),point(rows(selected.get("waypoints"))[0]))<=6.)cycleFinish(bot);
    order.object["previous_route_id"]=text(previous);order.object["route_switch_reason"]=text("recovery_at_start");return "switch_at_start";
}
void Planner::recoverRoute(Value& order,const Value& bot,const std::vector<Value>& manifest,double now) {
    const ID actor=id(bot);const auto& points=rows(route(bot).get("waypoints"));auto recovery=route_recoveries_.find(actor),progress=wreck_progress_.find(actor);
    const auto mode=s(order,"combat_mode");const auto& pose=bot.get("state");
    const bool route_motion=oneOf(mode,{"route","advance"})&&!s(order,"route_id").empty()&&!b(order,"parking_phase")&&!b(order,"team_command")&&!b(order,"defense_base_id");
    const bool aiming=!emptyActive(bot)&&!null(order.get("target_id"))&&!route_motion;
    const bool paused=aiming||b(order,"team_command")||b(order,"defense_base_id")||!oneOf(mode,{"route","advance","parking_approach","base_capture"})||!routeMobile(bot)||!b(pose,"world_pose")||b(pose,"airborne")||(!null(order.get("throttle_override"))&&n(order,"throttle_override")<=0.);
    if(paused) {
        if(progress!=wreck_progress_.end()) {
            auto& p=progress->second;ensure(p,"paused_at",number(now));ensure(p,"pause_pose",point(pose));const auto& key=rows(p.get("key"));
            order.object["route_attempt"]=record({{"route_id",key.at(0)},{"route_index",key.at(1)},{"action",text("pause_"+mode)},
                {"elapsed",number(rounded(n(p,"paused_at")-n(p,"since")))},{"phase",recovery==route_recoveries_.end()?Value():recovery->second.get("phase")}});
        }
        return;
    }
    if(points.size()<=1&&recovery==route_recoveries_.end()){automaticGoal(order,bot,manifest,now);return;}
    if(points.empty())return;const auto position=point(pose);
    int index=recovery!=route_recoveries_.end()?int(intField(recovery->second,"index")):std::clamp(int(intField(order,"route_index")),0,int(points.size())-1);
    const auto goal=recovery!=route_recoveries_.end()?point(points[index]):point(order.get("move_position").truth()?order.get("move_position"):points[index]);
    const auto route_id=s(route(bot),"id",s(order,"route_id"));const auto key=sequence({text(route_id),integer(index)});const double d=distance(goal,position);const bool physical=b(pose,"route_wreck_blocked");
    order.object["route_attempt"]=record({{"route_id",text(route_id)},{"route_index",integer(index)},{"distance",number(rounded(d))},{"arrival",number(6.)},{"physical_block",boolean(physical)},{"action",text("attempt")},{"elapsed",number(0.)},{"phase",text(recovery==route_recoveries_.end()?"normal":s(recovery->second,"phase"))}});
    auto& trace=order.object["route_attempt"];
    if(progress!=wreck_progress_.end()&&progress->second.get("key")!=key)progress=wreck_progress_.erase(progress),progress=wreck_progress_.end();
    if(progress!=wreck_progress_.end()&&has(progress->second,"paused_at")) {
        auto& p=progress->second;const double duration=std::max(0.,now-n(p,"paused_at"));p.object["since"]=number(n(p,"since")+duration);erase(p,{"paused_at","pause_pose"});p.object["origin"]=position;p.object["best_distance"]=number(std::min(n(p,"best_distance",d),d));
        if(recovery!=route_recoveries_.end()){recovery->second.object["anchor"]=position;auto& state=entry(route_states_,actor);state.object["join_index"]=recovery->second.get("index");state.object["join_anchor"]=position;erase(recovery->second,{"last_pose","last_at"});}
        trace.object["resumed_after_pause"]=number(duration);
    }
    Value previous;
    if(recovery!=route_recoveries_.end()) {
        if(now-n(recovery->second,"last_at",now-3.)>0.&&now-n(recovery->second,"last_at",now-3.)<=2.)previous=recovery->second.get("last_pose");
        recovery->second.object["last_pose"]=position;recovery->second.object["last_at"]=number(now);
    }
    if(diskReached(position,goal,Value(),previous)&&mode!="parking_approach") {
        if(recovery==route_recoveries_.end()){wreck_progress_.erase(actor);trace.object["action"]=text("arrived_or_unowned");return;}
        const auto phase=s(recovery->second,"phase");
        if(oneOf(phase,{"forward","skip","transfer"})){cycleFinish(bot);cyclePublish(order,bot,now);order.object["route_attempt"].object["action"]=text("recovery_complete");return;}
        if(index==0)trace.object["action"]=text(cycleChangeRoute(order,bot,manifest,recovery->second,now));
        else{cycleLeg(bot,recovery->second,"return",index-1,now);trace.object["action"]=text("return_previous_point");}
        cyclePublish(order,bot,now);return;
    }
    if(progress==wreck_progress_.end()) {
        wreck_progress_[actor]=record({{"key",key},{"goal",goal},{"since",number(now)},{"origin",position},{"best_distance",number(d)},{"physical_block",boolean(physical)}});progress=wreck_progress_.find(actor);
    }
    if(recovery==route_recoveries_.end()&&physical&&oneOf(mode,{"route","advance"})) {
        route_recoveries_[actor]=record({{"route_id",text(route_id)},{"phase",text("forward")},{"index",integer(index)},{"failed",integer(index)},{"anchor",point(order.get("route_anchor").truth()?order.get("route_anchor"):position)},{"round",integer(0)}});recovery=route_recoveries_.find(actor);dropParking(bot);
    }
    auto& p=progress->second;const double elapsed=std::max(0.,now-n(p,"since")),progress_distance=distance(p.get("goal"),position),best=n(p,"best_distance",distance(p.get("goal"),p.get("origin"))),improvement=best-progress_distance;
    trace.object["attempt_started_at"]=p.get("since");trace.object["elapsed"]=number(rounded(elapsed));trace.object["improvement"]=number(rounded(improvement));trace.object["best_distance"]=number(rounded(best));trace.object["progress_goal"]=p.get("goal");trace.object["progress_distance"]=number(rounded(progress_distance));
    if(physical&&improvement>=.5) {
        p.object["since"]=number(now);p.object["origin"]=position;p.object["best_distance"]=number(progress_distance);trace.object["action"]=text("physical_progress_renewed");trace.object["elapsed"]=number(0.);trace.object["attempt_started_at"]=number(now);
    } else if(!physical&&distance(position,p.get("origin"))>=6.&&distance(goal,p.get("origin"))-d>=4.) {
        p.object["since"]=number(now);p.object["origin"]=position;p.object["goal"]=goal;p.object["best_distance"]=number(std::min(best,d));trace.object["action"]=text("travel_progress_renewed");trace.object["elapsed"]=number(0.);trace.object["attempt_started_at"]=number(now);
    } else if(elapsed>=20.) {
        if(mode=="parking_approach"&&recovery==route_recoveries_.end()){automaticGoal(order,bot,manifest,now);return;}
        if(recovery==route_recoveries_.end()) {
            route_recoveries_[actor]=record({{"route_id",text(route_id)},{"phase",text("forward")},{"index",integer(index)},{"failed",integer(index)},{"anchor",position},{"round",integer(0)}});recovery=route_recoveries_.find(actor);
        }
        auto& r=recovery->second;const auto phase=s(r,"phase");
        if(phase=="forward"&&index+1<int(points.size())){cycleLeg(bot,r,"skip",index+1,now);order.object["route_point_skip_reason"]=text("blocked_timeout");order.object["previous_route_index"]=integer(index);trace.object["action"]=text("skip_gate");}
        else if(oneOf(phase,{"forward","skip"})){cycleLeg(bot,r,"return",std::max(0,int(intField(r,"failed"))-1),now);trace.object["action"]=text("return_to_anchor");}
        else if(phase=="transfer"){r.object["round"]=integer(intField(r,"round")+1);cycleLeg(bot,r,"transfer",0,now);trace.object["action"]=text("retry_transfer");}
        else if(phase=="return"&&index>0){cycleLeg(bot,r,"return_skip",index-1,now);trace.object["action"]=text("skip_return_point");}
        else{cycleRestart(bot,r,now);trace.object["action"]=text("retry_cycle");}
    }
    recovery=route_recoveries_.find(actor);
    if(recovery!=route_recoveries_.end()){trace.object["phase"]=recovery->second.get("phase");trace.object["cycle"]=recovery->second.get("round");trace.object["next_route"]=recovery->second.get("route_id");trace.object["next_index"]=recovery->second.get("index");cyclePublish(order,bot,now);}
}
}
