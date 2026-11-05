#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
bool Planner::coverOrder(Value& order,const Value& bot,const Value& focus,double now,bool urgent,bool hold_only) {
    const ID actor=id(bot);const auto key=identityValue(identity(focus)),p=personality(actor);
    const bool defensive=urgent||hold_only;
    if(!capable(bot,defensive?"urgent_cover":"tactical_cover",{focus.get("target_kind"),focus.get("id")})){cover_states_.erase(actor);return false;}
    auto begin=[&](Value& state,const std::string& phase,double distance){state.object["phase"]=text(phase);state.object["phase_until"]=number(0.);state.object["phase_started_at"]=number(now);state.object["best_distance"]=number(std::max(0.,distance));state.object["last_progress_at"]=number(now);};
    Value candidate;Value* state=nullptr;double cover_distance=0.,peek_distance=0.;std::string phase;
    for(int attempt=0;attempt<2;++attempt) {
        const auto& report=lookup(affordances_,actor);
        if(null(report)||report.get("target")!=key||now-n(report,"reported_at")>12.){cover_states_.erase(actor);return false;}
        auto weights=record({{"enemy_occlusion",number(26.+n(p,"caution")*18.+(defensive?22.:0.))},
            {"travel_distance",number(-.035-n(p,"caution")*.035-(defensive?.045:0.))},
            {"escape_feasible",number(8.+n(p,"patience")*10.+(defensive?10.:0.))},{"peek_feasible",number(6.+n(p,"aggression")*8.)}});
        if(defensive)weights.object["exposure"]=number(-48.);const auto ranked=scoreCovers(report.get("candidates"),weights);std::vector<Value> usable;
        for(const auto& c:rows(ranked))if(n(c,"water")<.5&&n(c,"slope")<=24.&&n(c,"enemy_occlusion")>=.45&&b(c,"peek_feasible")&&b(c,"escape_feasible")&&!null(c.get("peek_position"))&&lookup(cover_failures_,actor).get(s(c,"id")).numeric()<=now)usable.push_back(c);
        if(usable.empty()){cover_states_.erase(actor);return false;}
        auto current=cover_states_.find(actor);candidate=Value();
        if(current!=cover_states_.end()&&current->second.get("target")==key&&!b(current->second,"refresh_candidate")) {
            for(const auto& c:usable)if(c.get("id")==current->second.get("candidate_id")){candidate=c;current->second.object["candidate"]=c;break;}
        }
        if(current!=cover_states_.end())current->second.object.erase("refresh_candidate");
        auto reservation=[](const Value& point){return std::to_string(std::int64_t(std::nearbyint(n(point,"x")/8.)))+":"+std::to_string(std::int64_t(std::nearbyint(n(point,"z")/8.)));};
        if(null(candidate)) {
            for(const auto& c:usable)if(!cover_reservations_.count(reservation(c.get("position")))){candidate=c;break;}if(null(candidate))candidate=usable.front();
            cover_states_[actor]=record({{"target",key},{"candidate_id",candidate.get("id")},{"candidate",candidate},{"phase",text("approach")},{"phase_until",number(0.)},{"phase_started_at",number(now)},{"best_distance",number(distance(candidate.get("position"),bot.get("state")))},{"last_progress_at",number(now)}});
        }
        cover_reservations_.insert(reservation(candidate.get("position")));state=&cover_states_.at(actor);cover_distance=distance(candidate.get("position"),bot.get("state"));peek_distance=distance(candidate.get("peek_position"),bot.get("state"));phase=s(*state,"phase","approach");const double d=phase=="peek"?peek_distance:cover_distance;
        if(oneOf(phase,{"approach","return","peek"})&&d>4.5) {
            if(d+1.5<n(*state,"best_distance",d)){state->object["best_distance"]=number(d);state->object["last_progress_at"]=number(now);}
            if(now-n(*state,"last_progress_at")>=8.){entry(cover_failures_,actor).object[s(candidate,"id")]=number(now+12.);cover_states_.erase(actor);state=nullptr;continue;}
        }
        break;
    }
    if(!state)return false;
    if(defensive&&phase=="peek"){phase="return";begin(*state,phase,cover_distance);}
    if(oneOf(phase,{"approach","return"})&&cover_distance<=4.5) {
        const bool returning=phase=="return";phase="hold";begin(*state,phase,0.);if(returning)state->object["refresh_candidate"]=boolean(true);
        state->object["phase_until"]=number(now+.65+n(p,"patience")*1.35);state->object["face_position"]=point(order.get("face_position").truth()?order.get("face_position"):focus.get("position"));
    } else if(phase=="hold"&&!defensive&&now>=n(*state,"phase_until")&&weaponReady(bot)&&capable(bot,"cover_peek",{candidate.get("id")})) {
        phase="peek";begin(*state,phase,peek_distance);state->object["peek_fire_seq"]=integer(intField(bot.get("state"),"fire_seq"));
    } else if(phase=="peek"&&peek_distance<=4.5) {
        const auto& pose=bot.get("state");const auto sequence=intField(pose,"fire_seq");const bool shot=sequence>intField(*state,"peek_fire_seq",sequence);
        if(n(*state,"phase_until")<=0.)state->object["phase_until"]=number(now+1.+n(p,"aggression")*1.8);
        if(shot&&magazineFollowup(bot)){state->object["peek_fire_seq"]=integer(sequence);state->object["phase_until"]=number(now+std::max(n(pose,"reload_time"),1.+n(p,"aggression")*1.8));}
        else if(shot||(!b(pose,"burst_active")&&now>=n(*state,"phase_until"))){phase="return";begin(*state,phase,cover_distance);}
    }
    order.object["cover_id"]=candidate.get("id");const auto cover=candidate.get("position"),peek=candidate.get("peek_position");
    if(phase=="approach"){order.object["combat_mode"]=text("take_cover");order.object["move_position"]=cover;order.object["throttle_override"]=number(1.);}
    else if(phase=="hold") {
        order.object["combat_mode"]=text("cover_hold");order.object["move_position"]=cover;order.object["face_position"]=state->get("face_position").truth()?state->get("face_position"):order.get("face_position").truth()?order.get("face_position"):cover;order.object["throttle_override"]=number(0.);order.object["stable_hull_face"]=boolean(true);
    } else if(phase=="peek"){order.object["combat_mode"]=text("cover_peek");order.object["move_position"]=peek;order.object["throttle_override"]=number(peek_distance>4.5?1.:0.);}
    else{order.object["combat_mode"]=text("cover_return");order.object["move_position"]=cover;order.object["throttle_override"]=Value();}
    return true;
}
}
