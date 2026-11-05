#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
std::vector<Value> contributorRows(const Value& context,int side) {
    const auto& raw=context.get("contributors");if(dict(raw))return rows(raw.get(std::to_string(side)));
    std::vector<Value> out;for(const auto& row:rows(raw))if(team(row)==side)out.push_back(row);return out;
}
bool eligible(const Value& bot) {
    if(emptyActive(bot)||!b(bot.get("state"),"world_pose",true))return false;
    for(const char* key:{"engineHealth","leftTrackHealth","rightTrackHealth"})if(contains(bot.get("state").get("critical").get("destroyed"),text(key)))return false;return true;
}
double eta(const Value& bot,const Value& goal) { return 3.+distance(goal,bot.get("state"))*1.30/std::clamp(n(bot.get("profile"),"speed")*.65,4.,22.); }
Value keysValue(const std::set<Identity>& keys) { auto out=array();for(const auto& key:keys)out.array.push_back(identityValue(key));return out; }
}
std::set<Identity> Planner::contributors(const Value& context,int side) {
    std::set<Identity> result;for(const auto& row:contributorRows(context,side))if(oneOf(s(row,"kind"),{"human","bot"})&&id(row)>0)result.emplace(s(row,"kind"),id(row));return result;
}
bool Planner::defenseCombatProgress(const Value& bot,Value& record,const std::set<Identity>& contributor_keys,double now) {
    const auto& pose=bot.get("state");std::vector<Value> reachable;
    for(const auto& c:contactsFor(bot,now))if(contributor_keys.count(identity(c))&&b(c,"visible")&&distance(c.get("position"),pose)<=engagementRange(bot,c))reachable.push_back(c);
    const auto sequence=intField(pose,"fire_seq"),previous=intField(record,"observed_fire_seq",sequence);record.object["observed_fire_seq"]=integer(sequence);std::string reason="no_reachable_capturer";bool progressed=false;
    if(!reachable.empty()) {
        reason="no_executed_combat_progress";
        if(sequence>previous){record.object["defense_shot_seq"]=integer(sequence);record.object["reload_best"]=number(n(pose,"reload_time"));reason="shot_advanced";progressed=true;}
        else if(intField(record,"defense_shot_seq",-1)==sequence&&n(pose,"reload_time")>0.&&n(pose,"reload_time")<n(record,"reload_best")){record.object["reload_best"]=number(n(pose,"reload_time"));reason="reload_advanced";progressed=true;}
        const auto key=strategy::sequence({pose.get("aim_feedback_kind"),pose.get("aim_feedback_id")});
        for(const auto& c:reachable)if(identityValue(identity(c))==key&&contains(c.get("shootable_by_bot_ids"),integer(id(bot)))) {
            const double error=n(pose,"aim_feedback_error");const auto& old=rows(record.get("defense_aim"));
            if(old.size()!=2||old[0]!=key||error+3.14159265358979323846/180.<=old[1].numeric()){record.object["defense_aim"]=strategy::sequence({key,number(error)});reason="capturer_aim_advanced";progressed=true;}break;
        }
    }
    auto seen=array();for(const auto& c:reachable)seen.array.push_back(identityValue(identity(c)));
    record.object["combat_progress"]=strategy::record({{"at",number(now)},{"reason",text(reason)},{"progressed",boolean(progressed)},{"reachable",seen},{"fire_seq",integer(sequence)},{"previous_fire_seq",integer(previous)},{"reload_time",pose.get("reload_time")},{"aim_target",strategy::sequence({pose.get("aim_feedback_kind"),pose.get("aim_feedback_id")})},{"aim_error",pose.get("aim_feedback_error")}});return progressed;
}
std::map<int,Records> Planner::updateDefense(const std::vector<Value>& bots,const Value& context,double now) {
    std::map<int,Records> result{{1,{}},{2,{}}};if(!dict(context)){base_defense_.clear();return result;}
    for(int side:{1,2}) {
        auto& incident=base_defense_[side];if(!dict(incident))incident=object();auto& responders=ensure(incident,"responders");auto& blocked=ensure(incident,"blocked_until");
        const auto capturing=contributors(context,3-side),capturers=contributors(context,side);Records live;for(const auto& bot:bots)if(team(bot)==side&&eligible(bot))live[id(bot)]=bot;
        auto protected_ids=array(),released=array();for(const auto& bot:bots)if(team(bot)==side&&capturing.count({"bot",id(bot)})){protected_ids.array.push_back(integer(id(bot)));if(has(responders,std::to_string(id(bot))))released.array.push_back(integer(id(bot)));}
        std::sort(protected_ids.array.begin(),protected_ids.array.end(),[](const Value& a,const Value& b){return a.numeric()<b.numeric();});const auto previous=incident.get("capture_protection").get("protected");
        if(previous!=protected_ids||!released.array.empty()) {
            auto entered=array(),exited=array();for(const auto& id:rows(protected_ids))if(!contains(previous,id))entered.array.push_back(id);for(const auto& id:rows(previous))if(!contains(protected_ids,id))exited.array.push_back(id);
            auto& history=ensure(incident,"capture_protection_history",array());history.array.push_back(record({{"at",number(now)},{"entered",entered},{"exited",exited},{"released_responders",released}}));if(history.array.size()>16)history.array.erase(history.array.begin(),history.array.end()-16);
        }
        incident.object["capture_protection"]=record({{"at",number(now)},{"base_team",integer(3-side)},{"protected",protected_ids},{"released_responders",released},{"source",text("canonical_capture_contributors")}});
        std::vector<std::string> remove;for(const auto& row:blocked.object)if(now>=row.second.numeric())remove.push_back(row.first);for(const auto& key:remove)blocked.object.erase(key);
        remove.clear();for(const auto& row:responders.object){const ID actor=std::stoll(row.first);if(!live.count(actor)||capturing.count({"bot",actor}))remove.push_back(row.first);}for(const auto& key:remove)responders.object.erase(key);
        const auto& raw=context.get("states").get(std::to_string(side));const int invaders=std::max({0,int(intField(raw,"invaders")),int(capturers.size())});auto points=defensePoints(context.get("bases"),side);const int reserve=live.size()==1?1:std::max(0,int(live.size())-1);
        auto retention=[&](ID actor,const Value& goal) {
            const auto& bot=live.at(actor);bool can=false;if(weaponCanRecover(bot))for(const auto& c:contactsFor(bot,now))if(capturers.count(identity(c))&&b(c,"visible")&&contains(c.get("shootable_by_bot_ids"),integer(actor))&&distance(c.get("position"),bot.get("state"))<=engagementRange(bot,c)){can=true;break;}
            return std::make_tuple(can?0:1,eta(bot,goal),actor);
        };
        auto trim=[&](int limit) {
            if(int(responders.object.size())<=limit)return;std::vector<ID> ranked;for(const auto& row:responders.object)ranked.push_back(std::stoll(row.first));
            std::sort(ranked.begin(),ranked.end(),[&](ID a,ID b){return retention(a,responders.get(std::to_string(a)).get("point"))<retention(b,responders.get(std::to_string(b)).get("point"));});
            for(std::size_t i=std::max(0,limit);i<ranked.size();++i)responders.object.erase(std::to_string(ranked[i]));
        };
        trim(reserve);
        if(invaders<=0) {
            incident.object["release_since"]=Value();
            if(responders.object.empty()){incident.object["clear_since"]=Value();incident.object["need"]=integer(0);continue;}
            if(null(incident.get("clear_since")))incident.object["clear_since"]=number(now);
            else if(now-n(incident,"clear_since")>=30.){responders.object.clear();incident.object["clear_since"]=Value();incident.object["need"]=integer(0);continue;}
        } else {
            incident.object["clear_since"]=Value();remove.clear();
            for(auto& row:responders.object) {
                const ID actor=std::stoll(row.first);auto& r=row.second;const auto& bot=live.at(actor);const auto& pose=bot.get("state");
                auto base=std::find_if(points.begin(),points.end(),[&](const Value& p){return p.get("id")==r.get("base_id");});if(base!=points.end()){r.object["point"]=base->get("point");r.object["radius"]=base->get("radius");}
                const double d=distance(r.get("point"),pose);if(!has(r,"best_distance")||d+1.<=n(r,"best_distance")){r.object["best_distance"]=number(d);r.object["last_progress_at"]=number(now);}if(d<=n(r,"radius",50.))r.object["last_progress_at"]=number(now);
                const bool combat_progress=defenseCombatProgress(bot,r,capturers,now);
                if(s(lookup(travel_tasks_,actor),"kind")=="defense") {
                    if(!has(r,"task_pose"))r.object["task_pose"]=point(pose);
                    if(distance(pose,r.get("task_pose"))>=1.){r.object["last_progress_at"]=number(now);r.object["task_pose"]=point(pose);}
                }
                if(combat_progress)r.object["last_progress_at"]=number(now);bool backup=false;for(const auto& b:live)if(!has(responders,std::to_string(b.first))&&!has(blocked,std::to_string(b.first))&&!capturing.count({"bot",b.first})){backup=true;break;}
                if(d>n(r,"radius",50.)&&backup&&now-n(r,"last_progress_at")>=10.){blocked.object[row.first]=number(now+15.);remove.push_back(row.first);}
            }
            for(const auto& key:remove)responders.object.erase(key);
            int desired=std::min(3,std::max(1,invaders));desired=live.size()>1?std::min(desired,int(live.size())-1):live.empty()?0:1;const int previous_need=std::max(0,int(intField(incident,"need")));
            if(desired>=previous_need){incident.object["need"]=integer(desired);incident.object["release_since"]=Value();}
            else if(null(incident.get("release_since")))incident.object["release_since"]=number(now);
            else if(now-n(incident,"release_since")>=3.){incident.object["need"]=integer(std::max(desired,previous_need-1));incident.object["release_since"]=number(now);}
            trim(std::min(int(intField(incident,"need")),reserve));
            auto nearest=[&](const Value& bot)->Value {if(points.empty())return {};return *std::min_element(points.begin(),points.end(),[&](const Value& a,const Value& b){return std::make_pair(distance(a.get("point"),bot.get("state")),s(a,"id"))<std::make_pair(distance(b.get("point"),bot.get("state")),s(b,"id"));});};
            remove.clear();for(auto& row:responders.object) {
                if(std::any_of(points.begin(),points.end(),[&](const Value& p){return p.get("id")==row.second.get("base_id");}))continue;
                const auto base=nearest(live.at(std::stoll(row.first)));if(null(base))remove.push_back(row.first);else row.second=record({{"base_id",base.get("id")},{"point",base.get("point")},{"radius",base.get("radius")}});
            }
            for(const auto& key:remove)responders.object.erase(key);const int missing=std::max(0,std::min(int(intField(incident,"need")),reserve)-int(responders.object.size()));
            if(missing&&!points.empty()) {
                struct Candidate {std::tuple<int,double,double,ID> rank;ID actor;Value base;};std::vector<Candidate> candidates;const double deadline=std::max(0.,n(raw,"time_left")-2.);
                for(const auto& row:live) {
                    const ID actor=row.first;const auto key=std::to_string(actor);if(has(responders,key)||has(blocked,key)||capturing.count({"bot",actor}))continue;const auto& bot=row.second;const auto base=nearest(bot);const double travel=eta(bot,base.get("point"));double diversion=0.;
                    for(const auto& c:contactsFor(bot,now))if(b(c,"visible")&&contains(c.get("shootable_by_bot_ids"),integer(actor))){const double d=distance(c.get("position"),bot.get("state"));if(d<=50.)diversion=std::max(diversion,12.);else if(d<=150.)diversion=std::max(diversion,4.);}
                    const auto& pose=bot.get("state");diversion+=6.*(1.-std::min(1.,std::max(0.,n(pose,"health",1.))/std::max(1.,n(pose,"max_health",1.))));const auto tag=s(bot.get("profile"),"class_tag");diversion+=tag=="SPG"?8.:tag=="AT-SPG"?3.:0.;
                    candidates.push_back({travel<=deadline?std::make_tuple(0,travel+diversion,travel,actor):std::make_tuple(1,travel-deadline,travel+diversion,actor),actor,base});
                }
                std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return std::tie(a.rank,a.actor)<std::tie(b.rank,b.actor);});
                for(std::size_t i=0;i<std::min<std::size_t>(missing,candidates.size());++i){const auto& c=candidates[i];responders.object[std::to_string(c.actor)]=record({{"base_id",c.base.get("id")},{"point",c.base.get("point")},{"radius",c.base.get("radius")}});}
            }
        }
        for(const auto& row:responders.object)result[side][std::stoll(row.first)]=row.second;
    }
    return result;
}
void Planner::prioritizeInvaders(int side,const std::vector<Value>& bots,Records& assignments,const Records& defenders,const Value& context,double now) {
    if(defenders.empty())return;const auto keys=contributors(context,side);if(keys.empty())return;std::map<Identity,int> reservations;
    for(const auto& defender:defenders) {
        auto found=std::find_if(bots.begin(),bots.end(),[&](const Value& bot){return id(bot)==defender.first;});if(found==bots.end()||!insideBase(*found,defender.second))continue;const auto& bot=*found;const ID actor=id(bot);const auto current=lookup(assignments,actor);const auto current_key=null(current)?Value():identityValue(identity(current));
        struct Candidate {std::tuple<double,double,int,int,double,double,std::string,ID> rank;Value contact;};std::vector<Candidate> choices;
        for(const auto& c:contactsFor(bot,now)) {
            const auto key=identity(c);if(!keys.count(key)||!b(c,"visible")||!contains(c.get("shootable_by_bot_ids"),integer(actor)))continue;const double d=distance(c.get("position"),bot.get("state"));if(d>engagementRange(bot,c))continue;double points=0.;
            for(const auto& r:contributorRows(context,side))if(Identity{s(r,"kind"),id(r)}==key)points=std::max(points,n(r,"points"));double score=d;
            for(const auto& row:rows(lookup(target_decisions_,actor).get("candidates")))if(has(row,"score")&&row.get("target")==identityValue(key)){score=n(row,"score");break;}
            choices.push_back({{-points,score,reservations[key],identityValue(key)==current_key?0:1,n(c,"health",1.)/std::max(1.,n(c,"max_health",1.)),d,key.first,key.second},c});
        }
        auto& decision=entry(target_decisions_,actor);
        if(choices.empty()){decision.object["base_priority"]=record({{"reason",text("no_proved_capturer_lane")},{"contributors",keysValue(keys)},{"selected",current_key}});continue;}
        const auto chosen=*std::min_element(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.rank<b.rank;});const auto key=identity(chosen.contact);++reservations[key];assignments[actor]=chosen.contact;
        if(lookup(target_assignments_,actor).get("target")!=identityValue(key))target_assignments_[actor]=newTargetLease(bot,key,now);
        decision.object["selected"]=identityValue(key);decision.object["outcome"]=text("base_capturer");decision.object["lease_after"]=lookup(target_assignments_,actor);decision.object["base_priority"]=record({{"reason",text("proved_capturer_lane")},{"contributors",keysValue(keys)},{"selected",identityValue(key)}});
    }
}
}
