#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
Value Planner::retreatPoint(const Value& bot,const Value& anchor) const {
    const auto& points=rows(route(bot).get("waypoints"));const auto& state=lookup(route_states_,id(bot));if(points.empty()||null(state))return anchor;
    const int index=std::clamp(int(intField(state,"index")),0,int(points.size())-1);
    if(intField(state,"join_index",-1)==index&&dict(state.get("join_anchor"))&&distance(state.get("join_anchor"),bot.get("state"))>6.)return point(state.get("join_anchor"));
    return point(points[std::max(0,index-1)]);
}
void Planner::stationaryAngle(Value& order,const Value& bot) {
    const auto& profile=bot.get("profile");const auto p=personality(id(bot));
    if(!oneOf(s(order,"combat_mode"),{"engage","support_hold","low_health_retreat","under_fire_withdraw","crossfire_withdraw"})||n(order,"throttle_override",1.)!=0.||!b(order,"fire_allowed")||!oneOf(s(profile,"class_tag"),{"heavyTank","mediumTank","lightTank"})||(n(profile,"armor")<60.&&n(profile.get("roles"),"brawler")<.55)||!capable(bot,"hull_angling"))return;
    const auto key=sequence({order.get("target_kind"),order.get("target_id")});auto& anchor=entry(engage_anchors_,id(bot));
    if(anchor.get("target")!=key||!dict(anchor.get("face_position"))) {
        const auto& pose=bot.get("state");const auto& target=order.get("aim_position");const double dx=n(target,"x")-n(pose,"x"),dz=n(target,"z")-n(pose,"z");double degrees=0.;auto face=point(target);
        if(std::hypot(dx,dz)>.1) {
            degrees=(12.+n(p,"caution")*18.)*(id(bot)%2?-1.:1.);const double angle=degrees*3.14159265358979323846/180.,c=std::cos(angle),sine=std::sin(angle);
            face=point(record({{"x",number(n(pose,"x")+dx*c-dz*sine)},{"y",number(n(pose,"y"))},{"z",number(n(pose,"z")+dx*sine+dz*c)}}));
        }
        anchor=record({{"target",key},{"position",point(pose)},{"face_position",face},{"hull_angle_degrees",number(rounded(degrees))}});
    }
    if(std::abs(n(anchor,"hull_angle_degrees"))<=.01)return;
    order.object["move_position"]=anchor.get("position");order.object["face_position"]=anchor.get("face_position");order.object["hull_angle_degrees"]=anchor.get("hull_angle_degrees");order.object["stable_hull_face"]=boolean(true);
}
std::string Planner::rangeMode(const Value& bot,const Value& focus,double d,double far_limit,std::string far_mode,double fire_range,double now) {
    const auto actor=id(bot);const auto key=identityValue(identity(focus));const double hysteresis=std::clamp(far_limit*.06,8.,20.);
    if(far_mode=="support_hold"&&d>fire_range)far_mode="advance_contact";auto preferred=d>far_limit?far_mode:"engage";
    auto current=combat_states_.find(actor);if(current==combat_states_.end()||current->second.get("target")!=key){combat_states_[actor]=record({{"target",key},{"mode",text(preferred)},{"since",number(now)}});return preferred;}
    auto& state=current->second;auto previous=s(state,"mode");if(previous=="support_hold"&&d>fire_range)previous="advance_contact";
    if(previous=="engage"&&d<=std::min(fire_range,far_limit+hysteresis))preferred="engage";
    else if(oneOf(previous,{"support_hold","advance_contact"})&&d>=far_limit-hysteresis)preferred=previous;
    if(now-n(state,"since")<2.&&std::abs(d-far_limit)<=hysteresis&&(previous!="support_hold"||d<=fire_range))preferred=previous;
    if(preferred!=s(state,"mode")){state.object["mode"]=text(preferred);state.object["since"]=number(now);}return preferred;
}
bool Planner::leasedMovement(Value& order,const Value& bot,const Value& focus) {
    const Value* previous=nullptr;for(const auto& candidate:rows(state_.bot_orders))if(id(candidate)==id(bot)&&sequence({candidate.get("target_kind"),candidate.get("target_id")})==identityValue(identity(focus))){previous=&candidate;break;}
    if(!previous||!oneOf(s(*previous,"combat_mode"),{"advance","advance_contact","support_hold","engage","flank","route","take_cover","cover_hold","cover_peek","cover_return"}))return false;
    const auto mode=s(*previous,"combat_mode");if(mode=="route"&&!b(lookup(firing_holds_,id(bot)),"destination"))return false;
    if(oneOf(mode,{"engage","support_hold"})&&!null(previous->get("throttle_override"))&&n(*previous,"throttle_override")==0.) {
        const bool pending=contains(focus.get("pending_lane_by_bot_ids"),integer(id(bot)));ensure(order,"tactical_evidence").object["blocked_hold_handoff"]=record({{"reason",text(pending?"lane_refresh_pending":"stationary_lane_lost")},{"previous_mode",text(mode)}});if(!pending)return false;
    }
    if(mode=="flank"&&distance(point(previous->get("move_position")),bot.get("state"))<=1.5){ensure(order,"tactical_evidence").object["flank_handoff"]=record({{"reason",text("completed_flank_without_lane")},{"point",point(previous->get("move_position"))}});return false;}
    for(const char* field:{"combat_mode","move_position","face_position","throttle_override","cover_id","hull_angle_degrees","stable_hull_face"})if(has(*previous,field)&&!(std::string(field)=="move_position"&&mode=="advance"))order.object[field]=previous->get(field);
    if(mode=="advance_contact")order.object["move_position"]=focus.get("position");order.object["fire_allowed"]=boolean(false);return true;
}
void Planner::retreatOrder(Value& order,const Value& bot,const Value& destination,const Value& face_point,double now,const std::string& moving,const std::string& hold_mode) {
    const ID actor=id(bot);auto retreat=retreat_states_.find(actor);auto goal=point(destination);const auto face=point(face_point);const auto& pose=bot.get("state");
    if(retreat!=retreat_states_.end()&&s(retreat->second,"moving_mode")==moving&&dict(retreat->second.get("target_point")))goal=retreat->second.get("target_point");
    const double d=distance(goal,pose),fire_range=n(bot.get("profile"),"fire_range");const bool threat=(!null(order.get("target_id"))&&(fire_range<=0.||distance(face,pose)<=fire_range*1.15))||recent_hits_.count(actor);
    if(retreat==retreat_states_.end()||s(retreat->second,"moving_mode")!=moving) {
        retreat_states_[actor]=record({{"moving_mode",text(moving)},{"target_point",goal},{"phase",text("withdraw")},{"best_distance",number(d)},{"last_progress_at",number(now)},{"face",face},{"hold_mode",text(hold_mode)}});retreat=retreat_states_.find(actor);
    } else if(d+2.<n(retreat->second,"best_distance",d)){retreat->second.object["best_distance"]=number(d);retreat->second.object["last_progress_at"]=number(now);}
    auto& state=retreat->second;
    if(s(state,"phase")=="resume"){if(!threat)return;retreat_states_.erase(actor);retreatOrder(order,bot,destination,face_point,now,moving,hold_mode);return;}
    if(s(state,"phase")!="hold"&&(d<=6.||now-n(state,"last_progress_at")>=10.)) {
        state.object["phase"]=text("hold");state.object["hold_since"]=number(now);state.object["anchor"]=d<=6.?goal:point(pose);state.object["face"]=face;
    }
    if(s(state,"phase")=="hold") {
        if(!threat&&now-n(state,"hold_since",now)>=15.){state.object["phase"]=text("resume");return;}
        order.object["combat_mode"]=text(moving);order.object["tactical_phase"]=text(hold_mode);order.object["move_position"]=state.get("anchor");order.object["face_position"]=state.get("face");order.object["throttle_override"]=number(0.);order.object["stable_hull_face"]=boolean(true);
    } else {order.object["combat_mode"]=text(moving);order.object["tactical_phase"]=text("withdraw");order.object["move_position"]=goal;order.object["throttle_override"]=Value();order.object["face_position"]=state.get("face");}
}
void Planner::releaseFiringHold(Value& order,const Value& bot,const Value&,const Value&,double now) {
    const ID actor=id(bot);const auto& pose=bot.get("state");auto active=firing_holds_.find(actor);const bool releasing=s(order,"combat_mode")=="advance_contact"&&active!=firing_holds_.end()&&!null(active->second.get("destination"));
    auto evidence=record({{"sampled_at",number(now)},{"mode",order.get("combat_mode")},{"fire_seq",pose.get("fire_seq")},{"reason",text("ineligible")}});auto finish=[&]{ensure(order,"tactical_evidence").object["firing_hold"]=evidence;};
    if(s(order.get("tactical_evidence").get("blocked_hold_handoff"),"reason")=="lane_refresh_pending"){firing_holds_.erase(actor);evidence.object["reason"]=text("lane_refresh_pending");finish();return;}
    if(b(pose,"local_combat_manoeuvre")){firing_holds_.erase(actor);evidence.object["reason"]=text("local_combat_manoeuvre_active");finish();return;}
    if((!releasing&&(!oneOf(s(order,"combat_mode"),{"engage","support_hold"})||null(order.get("throttle_override"))||n(order,"throttle_override")!=0.))||!has(pose,"fire_seq")||!weaponReady(bot)){firing_holds_.erase(actor);finish();return;}
    const auto key=sequence({order.get("target_kind"),order.get("target_id")});auto lease=target_assignments_.find(actor);
    if(!releasing&&lease!=target_assignments_.end()&&lease->second.get("target")==key&&!b(pose,"aim_feedback_aligned")&&targetAimProgress(bot,lease->second,now)){firing_holds_.erase(actor);evidence.object["reason"]=text("executed_aim_in_progress");evidence.object["aim_progress_at"]=lease->second.get("aim_progress_at");finish();return;}
    const auto position=point(pose);const auto sequence=intField(pose,"fire_seq");
    if(active==firing_holds_.end()||active->second.get("target")!=key||intField(active->second,"fire_seq")!=sequence||(!releasing&&distance(position,active->second.get("position"))>=2.)){firing_holds_[actor]=record({{"target",key},{"fire_seq",integer(sequence)},{"position",position},{"ready_since",number(now)}});active=firing_holds_.find(actor);}
    auto& state=active->second;evidence.object["ready_since"]=state.get("ready_since");evidence.object["ready_elapsed"]=number(now-n(state,"ready_since"));evidence.object["reason"]=text("ready_without_shot");
    if(now-n(state,"ready_since")<8.){finish();return;}
    if(null(state.get("destination"))) {
        const auto aim=order.get("aim_position");if(!dict(aim)||distance(aim,position)<=1.5){evidence.object["reason"]=text("no_combat_approach_destination");finish();return;}
        state.object["destination"]=aim;state.object["move_until"]=number(now+4.);
    }
    evidence.object["destination"]=state.get("destination");evidence.object["move_until"]=state.get("move_until");
    if(now>=n(state,"move_until")){evidence.object["reason"]=text("combat_approach_complete");if(releasing&&b(order,"fire_allowed")){order.object["combat_mode"]=text("engage");order.object["throttle_override"]=number(0.);order.object["move_position"]=position;}firing_holds_.erase(actor);finish();return;}
    evidence.object["reason"]=text("combat_approach_active");order.object["combat_mode"]=text("advance_contact");order.object["move_position"]=state.get("destination");order.object["throttle_override"]=Value();order.object["fire_allowed"]=boolean(false);ensure(order,"tactical_evidence").object["combat_handoff"]=record({{"owner",text("combat")},{"reason",text("ready_without_shot")},{"destination",state.get("destination")}});engage_anchors_.erase(actor);finish();
}
void Planner::artilleryOrder(Value& order,const Value& bot,const Value& axis) {
    const ID actor=id(bot);engage_anchors_.erase(actor);cover_states_.erase(actor);const auto& initial=bot.get("spg_initial");Value anchor;
    if(!null(initial))anchor=record({{"point",initial.get("point")},{"face",initial.get("face")},{"index",integer(0)},{"parking_radius",initial.get("radius")}});
    else {
        const auto& route=bot.get("route");const auto& points=rows(route.get("waypoints"));const auto signature=sequence({text(s(route,"id")),route.get("waypoints")});auto cached=artillery_anchors_.find(actor);
        if(cached!=artillery_anchors_.end()&&cached->second.get("route_signature")==signature)anchor=cached->second;
        else if(points.empty()){const auto pose=point(bot.get("state"));anchor=record({{"point",pose},{"face",pose},{"index",integer(0)}});}
        else {
            const auto& axes=rows(axis);auto own=axes.size()==2?axes[0]:points.front(),enemy=axes.size()==2?axes[1]:points.back();double dx=n(enemy,"x")-n(own,"x"),dz=n(enemy,"z")-n(own,"z"),len=std::hypot(dx,dz);
            if(len<1.){own=points.front();enemy=points.back();dx=n(enemy,"x")-n(own,"x");dz=n(enemy,"z")-n(own,"z");len=std::max(1.,std::hypot(dx,dz));}
            const double desired=std::clamp(len*.16,35.,120.);double best=std::numeric_limits<double>::infinity();int selected=0;
            for(std::size_t i=0;i<points.size();++i) {
                const double x=n(points[i],"x")-n(own,"x"),z=n(points[i],"z")-n(own,"z"),radius=std::hypot(x,z),progress=(x*dx+z*dz)/(len*len);
                const double score=std::max({0.,progress-.30,-.12-progress})*len*8.+std::max(0.,desired*.35-radius)*5.+std::abs(radius-desired)+std::abs(progress-.12)*len*.20;
                if(score<best){best=score;selected=int(i);}
            }
            anchor=record({{"point",point(points[selected])},{"face",point(enemy)},{"index",integer(selected)}});
        }
        artillery_anchors_[actor]=anchor;artillery_anchors_[actor].object["route_signature"]=signature;
    }
    const bool arrived=distance(anchor.get("point"),bot.get("state"))<=n(anchor,"parking_radius",15.);order.object["combat_mode"]=text(arrived?"artillery_hold":"artillery_deploy");order.object["move_position"]=anchor.get("point");order.object["route_index"]=anchor.get("index");order.object["route_anchor"]=anchor.get("point");order.object["route_join"]=boolean(false);order.object["throttle_override"]=arrived?number(0.):Value();if(null(order.get("target_id")))order.object["face_position"]=anchor.get("face");
}
}
