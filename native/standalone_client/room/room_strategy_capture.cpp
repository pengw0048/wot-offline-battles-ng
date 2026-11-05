#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
std::vector<Value> Planner::defensePoints(const Value& raw,int team) {
    std::vector<Value> out;int index=0;
    for(const auto& value:rows(raw.get(std::to_string(team)))) {
        if(index>=4)break;
        if(!dict(value)){++index;continue;}
        out.push_back(record({{"id",text(s(value,"id",std::to_string(team)+":"+std::to_string(index)))},{"point",point(value)},{"radius",number(std::max(1.,n(value,"radius",50.)))}}));++index;
    }
    return out;
}
Value Planner::captureTarget(const Value& context,int team) const {
    int index=0;for(const auto& raw:rows(context.get("capture_bases").get(std::to_string(3-team)))) {
        const int current=index++;if(!dict(raw))continue;const double x=n(raw,"x",std::numeric_limits<double>::quiet_NaN()),z=n(raw,"z",std::numeric_limits<double>::quiet_NaN());if(!std::isfinite(x)||!std::isfinite(z))continue;
        return record({{"id",text(s(raw,"id",std::to_string(3-team)+":"+std::to_string(current)))},{"point",point(raw)},{"radius",number(std::max(1.,n(raw,"radius",50.)))}});
    }
    return {};
}
bool Planner::insideBase(const Value& bot,const Value& area) {
    const auto& pose=bot.get("state");return b(pose,"world_pose")&&!b(pose,"airborne")&&!b(pose,"route_wreck_blocked")&&distance(area.get("point"),pose)<=n(area,"radius",50.);
}
void Planner::captureOrder(Value& order,const Value& bot,const Value& area) {
    engage_anchors_.erase(id(bot));cover_states_.erase(id(bot));order.object["combat_mode"]=text("base_capture");order.object["capture_base_id"]=text(s(area,"id"));
    for(const char* field:{"aim_position","face_position","move_position"})order.object[field]=area.get("point");order.object["throttle_override"]=Value();order.object["route_join"]=boolean(false);
}
void Planner::protectCapture(Value& order,const Value& bot,const Value& target) {
    const auto actor=id(bot);
    if(null(target)||travel_tasks_.count(actor)||route_recoveries_.count(actor)||b(order,"team_command")||b(order,"defense_base_id")){capture_arrivals_.erase(actor);return;}
    const auto area=record({{"id",target.get("id")},{"point",target.get("point")},{"radius",target.get("radius")}});const auto previous=capture_arrivals_.find(actor);
    if(previous!=capture_arrivals_.end()&&previous->second!=area)capture_arrivals_.erase(previous);
    const auto& pose=bot.get("state");if(!b(pose,"world_pose")||b(pose,"airborne")||b(pose,"route_wreck_blocked"))return;
    if(inside(pose,area))capture_arrivals_[actor]=area;if(!capture_arrivals_.count(actor))return;
    order.object["capture_area"]=area;const auto before=order.get("move_position");order=constrainCapture(order,pose);
    ensure(order,"tactical_evidence").object["capture_hold"]=record({{"base_id",area.get("id")},{"inside",boolean(inside(pose,area))},{"constrained",boolean(order.get("move_position")!=before)},{"source",text("actual_circle_arrival")}});
}
void Planner::terminalOrder(Value& order,const Value& bot,const Value& capture,double now) {
    if(emptyActive(bot))return;const ID actor=id(bot);const auto& points=rows(route(bot).get("waypoints"));auto cursor=route_states_.find(actor);
    auto evidence=record({{"at",number(now)},{"route_id",order.get("route_id")},{"route_index",order.get("route_index")},{"terminal_index",integer(ID(points.size())-1)},{"input_mode",order.get("combat_mode")},{"outcome",text("no_route")}});
    auto finish=[&]{ensure(order,"tactical_evidence").object["route_terminal"]=evidence;};
    if(route_recoveries_.count(actor)||travel_tasks_.count(actor)){evidence.object["outcome"]=text("route_recovery_in_progress");finish();return;}
    if(points.empty()||cursor==route_states_.end()) {
        if(s(order,"combat_mode")=="base_capture"&&capture.truth()) {
            const bool arrived=insideBase(bot,capture);evidence.object["arrival_kind"]=text("circle");evidence.object["inside_capture"]=boolean(arrived);evidence.object["circle_radius"]=capture.get("radius");evidence.object["outcome"]=text(arrived?"base_capture":"outside_arrival");
            if(arrived){order.object["move_position"]=point(bot.get("state"));order.object["face_position"]=point(bot.get("state"));order.object["throttle_override"]=number(0.);order.object["route_join"]=boolean(false);}
        }
        finish();return;
    }
    auto& state=cursor->second;const bool capturing=s(order,"combat_mode")=="base_capture";const auto final=point(points.back()),goal=point(capturing&&capture.truth()?capture.get("point"):points.back()),position=point(bot.get("state"));
    const double d=distance(goal,position),arrival=!null(routeConfig(bot,route(bot)))||points.size()>1?6.:1.5;
    const bool inside_capture=capture.truth()&&inside(position,capture),final_inside=capture.truth()&&inside(final,capture),circle=capturing||final_inside;
    const bool arrived=circle?inside_capture:(d<=arrival||state.get("terminal_crossing").get("point")==final);
    evidence.object["goal"]=goal;evidence.object["distance"]=number(d);evidence.object["arrival_radius"]=number(arrival);evidence.object["inside_capture"]=boolean(inside_capture);evidence.object["final_inside_capture"]=boolean(final_inside);evidence.object["arrival_kind"]=text(circle?"circle":"point");evidence.object["circle_radius"]=capture.get("radius");
    if(has(state,"terminal_hold")&&(state.get("terminal_hold").get("goal")!=goal||!arrived)) {
        evidence.object["released"]=text(state.get("terminal_hold").get("goal")!=goal?"goal_changed":"outside_arrival");state.object.erase("terminal_hold");
    }
    if(!arrived) {
        if(!capturing&&!final_inside&&capture.truth()&&state.get("completed_terminal")==final&&oneOf(s(order,"combat_mode"),{"route","advance"})&&!b(order,"target_id")&&!b(order,"team_command")&&!b(order,"parking_phase")&&!b(state,"holding")&&order.get("move_position")==final) {
            captureOrder(order,bot,capture);evidence.object["outcome"]=text("completed_terminal_to_base");
        }else evidence.object["outcome"]=text("outside_arrival");finish();return;
    }
    if(b(order,"team_command")||b(order,"parking_phase")||b(state,"holding")||!oneOf(s(order,"combat_mode"),{"route","advance","base_capture"})){evidence.object["outcome"]=text("tactical_owner");finish();return;}
    if(!capturing&&(intField(order,"route_index")!=ID(points.size())-1||intField(state,"index")!=ID(points.size())-1)){evidence.object["outcome"]=text("intermediate_point");finish();return;}
    if(distance(order.get("move_position"),goal)>.01){evidence.object["outcome"]=text("different_destination");finish();return;}
    const auto& pose=bot.get("state");if(!b(pose,"world_pose")||b(pose,"airborne")||b(pose,"route_wreck_blocked")||(capturing&&!inside_capture)){evidence.object["outcome"]=text("physical_or_capture_not_arrived");finish();return;}
    state.object["completed_terminal"]=final;
    if(!capturing&&capture.truth()&&!final_inside&&null(order.get("target_id"))){captureOrder(order,bot,capture);evidence.object["outcome"]=text("terminal_to_base");finish();return;}
    auto& hold=ensure(state,"terminal_hold",record({{"goal",goal},{"position",position},{"arrived_at",number(now)}}));order.object["move_position"]=hold.get("position");order.object["throttle_override"]=number(0.);order.object["route_join"]=boolean(false);
    if(!null(order.get("target_id"))){order.object["combat_mode"]=text("engage");stationaryAngle(order,bot);}
    else{order.object["combat_mode"]=text(capturing?"base_capture":"hold");order.object["face_position"]=position;}
    evidence.object["outcome"]=order.get("combat_mode");evidence.object["arrived_at"]=hold.get("arrived_at");evidence.object["hold_position"]=order.get("move_position");evidence.object["target_id"]=order.get("target_id");evidence.object["fire_allowed"]=order.get("fire_allowed");finish();
}
}
