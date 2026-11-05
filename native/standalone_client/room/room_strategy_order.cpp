#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
std::vector<Value> flankShooters(const Value& contact,const std::vector<Value>& allies) {
    std::vector<Value> out;for(const auto& ally:allies)if(contains(contact.get("shootable_by_bot_ids"),integer(id(ally)))&&(!list(contact.get("assigned_shooter_ids"))||contains(contact.get("assigned_shooter_ids"),integer(id(ally))))&&weaponReady(ally))out.push_back(ally);return out;
}
bool shouldFlank(const Value& bot,const Value& contact,const std::vector<Value>& allies) {
    const auto shooters=flankShooters(contact,allies);if(shooters.size()<2||contains(contact.get("flank_suppressor_bot_ids"),integer(id(bot))))return false;
    std::vector<std::tuple<double,double,ID,ID>> candidates;
    for(const auto& ally:shooters){const double flanker=n(ally.get("profile").get("roles"),"flanker"),initiative=n(personality(id(ally)),"initiative");if(flanker>=.68&&initiative>.42)candidates.emplace_back(flanker,initiative,-id(ally),id(ally));}
    if(candidates.empty())return false;std::sort(candidates.begin(),candidates.end(),std::greater<>());const auto count=std::min(candidates.size(),std::max<std::size_t>(1,shooters.size()/2));
    if(count>=shooters.size())return false;for(std::size_t i=0;i<count;++i)if(std::get<3>(candidates[i])==id(bot))return true;return false;
}
Value flankPoint(const Value& bot,const Value& contact,double desired,const std::vector<Value>& allies) {
    const auto& pose=bot.get("state");const auto& target=contact.get("position");double dx=n(pose,"x")-n(target,"x"),dz=n(pose,"z")-n(target,"z");const double len=std::hypot(dx,dz);dx/=len?len:1.;dz/=len?len:1.;const double offset=std::min(95.,desired*.38);Value best_point;double best=-1.,best_side=-2.;
    for(double side:{-1.,1.}) {
        const auto candidate=point(record({{"x",number(n(target,"x")+dx*desired*.72+dz*offset*side)},{"y",target.get("y")},{"z",number(n(target,"z")+dz*desired*.72-dx*offset*side)}}));const double bearing=std::atan2(n(candidate,"x")-n(target,"x"),n(candidate,"z")-n(target,"z"));double separation=std::numeric_limits<double>::infinity();
        for(const auto& ally:flankShooters(contact,allies))if(id(ally)!=id(bot)&&distance(ally.get("state"),target)>1.) {
            const double other=std::atan2(n(ally.get("state"),"x")-n(target,"x"),n(ally.get("state"),"z")-n(target,"z"));const double delta=bearing-other;separation=std::min(separation,std::abs(std::atan2(std::sin(delta),std::cos(delta))));
        }
        if(!std::isfinite(separation))separation=0.;if(std::make_pair(separation,-side)>std::make_pair(best,best_side)){best=separation;best_side=-side;best_point=candidate;}
    }
    return best_point;
}
double supportScore(const Value& bot,const std::vector<Value>& allies,const Value& focus) {
    double score=0.;for(const auto& ally:allies) {
        if(id(ally)==id(bot)||!contains(focus.get("shootable_by_bot_ids"),integer(id(ally)))||!weaponReady(ally))continue;
        const auto& pose=ally.get("state");const double health=n(pose,"health",1.)/std::max(1.,n(pose,"max_health",1.));
        score+=health*std::max(0.,1.-distance(pose,bot.get("state"))/130.)*.55;score+=health*std::max(0.,1.-distance(pose,focus.get("position"))/220.)*.35;
    }
    return rounded(std::clamp(score,0.,1.));
}
void approach(Value& order,const Value& position,const char* reason) {
    order.object["combat_mode"]=text("advance_contact");order.object["move_position"]=position;order.object["throttle_override"]=Value();order.object["fire_allowed"]=boolean(false);ensure(order,"tactical_evidence").object["combat_handoff"]=record({{"owner",text("combat")},{"reason",text(reason)},{"destination",position}});
}
}
bool Planner::captureStaged(const Value& bot,int route_index,const Value& capture) {
    if(route_recoveries_.count(id(bot)))return false;const auto& selected=route(bot);const auto& points=rows(selected.get("waypoints"));if(points.empty())return true;if(null(capture))return false;if(insideBase(bot,capture))return true;
    if(route_index!=int(points.size())-1)return false;const auto& pose=bot.get("state");if(!b(pose,"world_pose")||b(pose,"airborne")||b(pose,"route_wreck_blocked"))return false;
    const auto final=point(points.back());if(inside(final,capture))return insideBase(bot,capture);auto& state=entry(route_states_,id(bot));if(state.get("completed_terminal")==final||state.get("terminal_crossing").get("point")==final)return true;
    const double arrival=!null(routeConfig(bot,selected))||points.size()>1?6.:1.5;const bool arrived=distance(final,pose)<=arrival;if(arrived)state.object["completed_terminal"]=final;return arrived;
}
void Planner::baseDefenseOrder(Value& order,const Value& bot,const Value& base) {
    engage_anchors_.erase(id(bot));cover_states_.erase(id(bot));order.object["combat_mode"]=text("base_defense");order.object["defense_base_id"]=text(s(base,"base_id"));order.object["move_position"]=base.get("point");order.object["throttle_override"]=Value();order.object["route_join"]=boolean(false);if(null(order.get("target_id")))order.object["face_position"]=base.get("point");const bool arrived=insideBase(bot,base);
    ensure(order,"tactical_evidence").object["base_arrival"]=record({{"kind",text("defense")},{"base_id",base.get("base_id")},{"point",base.get("point")},{"radius",number(n(base,"radius",50.))},{"arrived",boolean(arrived)}});
    if(arrived) {
        order.object["move_position"]=point(bot.get("state"));order.object["throttle_override"]=number(0.);
        if(!null(order.get("target_id"))){order.object["combat_mode"]=text("engage");stationaryAngle(order,bot);}else order.object["face_position"]=point(bot.get("state"));
    }
}
Value Planner::orderFor(const Value& bot,int,int,const Value& focus,const std::vector<Value>& contacts,double now,const Value& defense,const Value& axis,const std::vector<Value>& allies,const Value& capture,bool no_enemies,bool active_capture) {
    const ID actor=id(bot);const auto position=routePosition(bot,now);const auto retreat_point=retreatPoint(bot,position.anchor);const auto& profile=bot.get("profile");const auto p=personality(actor);const auto& pose=bot.get("state");const double desired=std::max(10.,n(profile,"desired_range",180.)),fire_range=std::max(desired,n(profile,"fire_range",500.)),health=n(pose,"health",1.)/std::max(1.,n(pose,"max_health",1.));const auto recent_hit=lookup(recent_hits_,actor);Value threat;
    for(const auto& contact:contacts)if(identityValue(identity(contact))==recent_hit.get("attacker")){threat=contact;break;}
    const bool followup=magazineFollowup(bot),reload=intField(pose,"clip_size",1)>1&&intField(pose,"clip")<=0&&intField(pose,"fire_seq")>0&&weaponCanRecover(bot);const auto retreat_before=lookup(retreat_states_,actor);Value release;
    if(!null(retreat_before)&&(s(retreat_before,"moving_mode")!="magazine_reload_retreat"||!reload||active_capture)) {
        release=text(active_capture?"active_capture_reload":s(retreat_before,"moving_mode")=="magazine_reload_retreat"?"magazine_ready_or_exhausted":"retreat_trigger_removed");retreat_states_.erase(actor);
    }
    auto order=record({{"id",integer(actor)},{"team",integer(team(bot))},{"target_id",Value()},{"aim_position",Value()},{"face_position",Value()},{"move_position",position.move},{"fire_allowed",boolean(false)},{"combat_mode",text("route")},{"throttle_override",Value()},{"desired_range",number(rounded(desired))},{"fire_range",number(rounded(fire_range))},{"route_id",text(position.route)},{"route_index",integer(position.index)},{"route_anchor",position.anchor},{"route_join",boolean(position.join)},{"_route_position",position.move},{"_turnback_position",retreat_point},{"personality",p},{"profile",profile},{"shell_index",integer(0)}});
    auto selection=lookup(target_decisions_,actor);if(null(selection))selection=record({{"at",number(now)},{"outcome",text("direct_order_call")},{"candidates",array()},{"selected",Value()}});
    order.object["tactical_evidence"]=record({{"at",number(now)},{"clock_basis",text("server_monotonic_seconds")},{"selection",selection},{"focus",null(focus)?Value():identityValue(identity(focus))},{"recent_hit",null(recent_hit)?object():recent_hit},{"withdraw_after_hit",boolean(false)},{"withdrawal",record({{"reason",text("empty_magazine_only")},{"magazine_followup",boolean(followup)},{"magazine_reload",boolean(reload)},{"active_capture",boolean(active_capture)},{"released",release},{"clip",pose.get("clip")},{"clip_size",pose.get("clip_size")},{"reload_time",pose.get("reload_time")},{"fire_seq",pose.get("fire_seq")}})},{"threat",null(threat)?Value():identityValue(identity(threat))},{"health_fraction",number(health)},{"weapon_ready",boolean(weaponReady(bot))},{"retreat_before",null(retreat_before)?object():retreat_before}});
    if(emptyActive(bot)){emptyOrder(order,bot,contacts,capture,now);return order;}
    if(!null(defense)) {
        if(!null(focus))setTarget(order,bot,focus,b(focus,"visible")&&(contains(focus.get("shootable_by_bot_ids"),integer(actor))||(s(profile,"class_tag")=="SPG"&&insideBase(bot,defense))));
        baseDefenseOrder(order,bot,defense);firing_holds_.erase(actor);return order;
    }
    const auto& route_state=lookup(route_states_,actor);const bool capture_wait=b(route_state,"holding")||oneOf(s(route_state,"parking_phase"),{"approach","waiting","queue"});
    if(no_enemies&&!null(capture)&&!capture_wait&&s(profile,"class_tag")!="SPG"&&captureStaged(bot,position.index,capture)){captureOrder(order,bot,capture);firing_holds_.erase(actor);return order;}
    if(s(profile,"class_tag")=="SPG"){if(!null(focus))setTarget(order,bot,focus,b(focus,"visible"));artilleryOrder(order,bot,axis);firing_holds_.erase(actor);return order;}
    if(active_capture&&reload){if(!null(focus))setTarget(order,bot,focus,false);order.object["combat_mode"]=text("magazine_reload_hold");order.object["move_position"]=point(pose);order.object["throttle_override"]=number(0.);firing_holds_.erase(actor);return order;}
    if(reload&&(!null(focus)||s(lookup(retreat_states_,actor),"moving_mode")=="magazine_reload_retreat")) {
        if(!null(focus)){setTarget(order,bot,focus,false);if(cover_states_.count(actor)&&coverOrder(order,bot,focus,now,true)){retreat_states_.erase(actor);firing_holds_.erase(actor);return order;}}
        engage_anchors_.erase(actor);combat_states_.erase(actor);cover_states_.erase(actor);retreatOrder(order,bot,retreat_point,null(focus)?lookup(retreat_states_,actor).get("face"):focus.get("position"),now,"magazine_reload_retreat","magazine_reload_hold");firing_holds_.erase(actor);return order;
    }
    if(null(focus)) {
        engage_anchors_.erase(actor);combat_states_.erase(actor);const auto retreat=lookup(retreat_states_,actor);
        if(!null(retreat)&&oneOf(s(retreat,"phase"),{"withdraw","hold"})){retreatOrder(order,bot,retreat.get("target_point"),retreat.get("face"),now,s(retreat,"moving_mode"),s(retreat,"hold_mode"));firing_holds_.erase(actor);return order;}
        cover_states_.erase(actor);retreat_states_.erase(actor);firing_holds_.erase(actor);return order;
    }
    const bool lane=b(focus,"visible")&&contains(focus.get("shootable_by_bot_ids"),integer(actor));setTarget(order,bot,focus,lane);const auto retreat=lookup(retreat_states_,actor);
    if(!null(retreat)&&s(retreat,"phase")=="withdraw"){retreatOrder(order,bot,retreat.get("target_point"),focus.get("position"),now,s(retreat,"moving_mode"),s(retreat,"hold_mode"));firing_holds_.erase(actor);return order;}
    const double d=distance(focus.get("position"),pose);if(d>fire_range)order.object["fire_allowed"]=boolean(false);const auto dominant=s(profile,"dominant_role","support");const double far_limit=desired*(1.08+n(p,"caution")*.18),close_limit=desired*(.48+n(p,"aggression")*.10),support=supportScore(bot,allies,focus);
    if(!lane) {
        if(b(focus,"movement_lease")) {
            if(cover_states_.count(actor)&&coverOrder(order,bot,focus,now)){engage_anchors_.erase(actor);combat_states_.erase(actor);firing_holds_.erase(actor);return order;}
            if(leasedMovement(order,bot,focus)){releaseFiringHold(order,bot,position.move,position.anchor,now);return order;}
        }
        engage_anchors_.erase(actor);combat_states_.erase(actor);retreat_states_.erase(actor);cover_states_.erase(actor);approach(order,focus.get("position"),"local_lane_lost");firing_holds_.erase(actor);return order;
    }
    retreat_states_.erase(actor);
    if(d>close_limit&&d<=fire_range&&capable(bot,"flank")&&shouldFlank(bot,focus,allies)) {
        engage_anchors_.erase(actor);combat_states_.erase(actor);cover_states_.erase(actor);order.object["combat_mode"]=text("flank");order.object["move_position"]=flankPoint(bot,focus,desired,allies);order.object["throttle_override"]=number(1.);firing_holds_.erase(actor);return order;
    }
    const double advance=n(p,"aggression")*.85+n(p,"initiative")*.30+support*(.35+n(p,"teamwork")*.25)-n(p,"caution")*.55;const auto far_mode=advance>=(dominant=="brawler"?.24:.34)?"advance_contact":"support_hold";const auto mode=rangeMode(bot,focus,d,far_limit,far_mode,fire_range,now);
    auto engage=[&]{order.object["combat_mode"]=text("engage");auto& anchor=entry(engage_anchors_,actor);const auto key=identityValue(identity(focus));if(anchor.get("target")!=key)anchor=record({{"target",key},{"position",point(pose)}});order.object["move_position"]=anchor.get("position");order.object["throttle_override"]=number(0.);};
    if(cover_states_.count(actor)&&d<=fire_range*1.15&&coverOrder(order,bot,focus,now)){engage_anchors_.erase(actor);combat_states_.erase(actor);}
    else if(d>far_limit||mode!="engage") {
        cover_states_.erase(actor);
        if(mode=="advance_contact"){engage_anchors_.erase(actor);order.object["combat_mode"]=text(position.route.empty()?"advance_contact":"advance");order.object["move_position"]=position.route.empty()?focus.get("position"):position.move;order.object["throttle_override"]=number(1.);}
        else if(mode=="support_hold"){order.object["combat_mode"]=text("support_hold");order.object["move_position"]=point(pose);order.object["throttle_override"]=number(0.);}else engage();
    } else if(d<=fire_range*1.15&&coverOrder(order,bot,focus,now)){engage_anchors_.erase(actor);combat_states_.erase(actor);}
    else{cover_states_.erase(actor);engage();}
    stationaryAngle(order,bot);releaseFiringHold(order,bot,position.move,position.anchor,now);return order;
}
}
