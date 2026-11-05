#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
double Planner::engagementRange(const Value& bot,const Value& contact) {
    const auto& profile=bot.get("profile");const double desired=std::max(40.,n(profile,"desired_range",180.));
    if(s(profile,"class_tag")=="SPG")return std::max(desired,std::min(2500.,n(profile,"fire_range",1250.)));
    const double mobility=std::max(n(profile.get("roles"),"scout"),n(profile.get("roles"),"flanker"));
    return b(contact,"visible")?std::max(340.,std::min(560.,desired*2.+mobility*300.)):std::max(240.,std::min(420.,desired*1.5+mobility*210.));
}
Value Planner::newTargetLease(const Value& bot,const Identity& key,double now) {
    return record({{"target",identityValue(key)},{"until",number(now+2.)},{"aim_start_seq",integer(intField(bot.get("state"),"fire_seq"))},{"aim_progress_at",number(now)},{"aim_best_error",Value()}});
}
bool Planner::targetAimProgress(const Value& bot,Value& lease,double now) {
    const auto& pose=bot.get("state");const auto key=sequence({pose.get("aim_feedback_kind"),pose.get("aim_feedback_id")});
    if(key!=lease.get("target")||intField(pose,"fire_seq")>intField(lease,"aim_start_seq",intField(pose,"fire_seq")))return false;
    const double error=n(pose,"aim_feedback_error");
    if(null(lease.get("aim_best_error"))||error+3.14159265358979323846/180.<=n(lease,"aim_best_error")){lease.object["aim_best_error"]=number(error);lease.object["aim_progress_at"]=number(now);}
    lease.object["aim_aligned"]=boolean(b(pose,"aim_feedback_aligned"));return now-n(lease,"aim_progress_at")<6.;
}
Records Planner::assignTargets(const std::vector<Value>& bots,const std::vector<Value>& contacts,double now) {
    Records assigned;
    if(bots.empty()||contacts.empty()) {for(const auto& bot:bots)target_decisions_[id(bot)]=record({{"at",number(now)},{"candidates",array()},{"selected",Value()},{"outcome",text("no_team_contacts")}});return assigned;}
    struct Candidate {double score=0.,distance=0.;ID bot=0;Value contact;bool ready=false;};
    auto less=[](const Candidate& a,const Candidate& b){return std::make_tuple(a.score,a.bot,identity(a.contact),a.distance)<std::make_tuple(b.score,b.bot,identity(b.contact),b.distance);};
    std::vector<Candidate> candidates;std::map<ID,std::vector<Candidate>> by_bot;Records reads;
    std::map<Identity,Value> reservations;
    auto limit=[](const Value& c,double d){const double health=n(c,"health");const int desired=health>=1800.?3:health>=900.||oneOf(s(c,"class_tag"),{"heavyTank","AT-SPG"})?2:1;return d<=50.?std::max(desired,4):desired;};
    auto saturated=[&](const Identity& key,const Value& contact,double d) {
        const auto i=reservations.find(key);if(i==reservations.end())return false;const auto& r=i->second;const auto count=intField(r,"count");
        if(d<=50.||b(r,"unknown_damage"))return count>=limit(contact,d);
        return !null(r.get("smallest_shot"))&&count>=2&&n(r,"nominal_damage")-n(r,"smallest_shot")>=std::max(1.,n(contact,"health",1.));
    };
    auto reserve=[&](const Identity& key,const Value& bot,const Value& contact) {
        auto i=reservations.find(key);if(i==reservations.end())i=reservations.emplace(key,record({{"count",integer(0)},{"nominal_damage",number(0.)},{"smallest_shot",Value()},{"unknown_damage",boolean(false)}})).first;
        auto& r=i->second;r.object["count"]=integer(intField(r,"count")+1);const int selected=selectedShell(bot,contact);double damage=-1.;
        for(const auto& shell:rows(bot.get("profile").get("shells")))if(intField(shell,"index",-1)==selected){damage=n(shell,"damage",-1.);break;}
        if(damage<=0.){r.object["unknown_damage"]=boolean(true);return;}
        r.object["nominal_damage"]=number(n(r,"nominal_damage")+damage);r.object["smallest_shot"]=number(null(r.get("smallest_shot"))?damage:std::min(n(r,"smallest_shot"),damage));
    };
    auto refusal=[&](ID actor,const Value& contact,double d,const char* stage) {
        ensure(entry(target_decisions_,actor),"focus_refusals",array()).array.push_back(record({{"target",identityValue(identity(contact))},{"distance",number(d)},{"health",contact.get("health")},{"limit",integer(limit(contact,d))},{"reservation",reservations[identity(contact)]},{"stage",text(stage)}}));
    };
    for(const auto& bot:bots) {
        const ID actor=id(bot);auto observation=record({{"at",number(now)},{"candidates",array()},{"selected",Value()},{"previous",null(lookup(target_assignments_,actor))?object():lookup(target_assignments_,actor)},{"outcome",text("evaluating")},{"focus_refusals",array()}});
        auto weapon=object();for(const char* key:{"shell_index","ammo_remaining","ammo_reload_pending","clip","clip_size","reload_time","burst_active"})weapon.object[key]=bot.get("state").get(key);
        observation.object["weapon"]=std::move(weapon);observation.object["destroyed"]=list(bot.get("state").get("critical").get("destroyed"))?bot.get("state").get("critical").get("destroyed"):array();
        const auto& lease=lookup(target_assignments_,actor);const Value occasion=null(lease)?Value():number(rounded(n(lease,"until")));auto capabilities=object();
        for(const char* name:{"target_nearest","target_priority","target_retaliation","target_human"})capabilities.object[name]=boolean(capable(bot,name,{occasion}));
        reads[actor]=capabilities;observation.object["target_capabilities"]=capabilities;auto known=contactsFor(bot,now);
        for(const auto& contact:contacts) {
            const bool knows=std::any_of(known.begin(),known.end(),[&](const Value& c){return identity(c)==identity(contact);});const bool lane=contains(contact.get("shootable_by_bot_ids"),integer(actor));
            auto row=record({{"target",identityValue(identity(contact))},{"visible",boolean(b(contact,"visible"))},{"local_lane",boolean(lane)},{"last_seen",contact.get("last_seen")},{"radio_until",contact.get("radio_bot_until").get(std::to_string(actor))},{"position",contact.get("position")},{"known",boolean(knows)},{"reason",text("not_known_to_bot")}});
            if(knows) {
                const bool artillery=s(bot.get("profile"),"class_tag")=="SPG";
                if(!b(contact,"visible")||(!artillery&&!lane))row.object["reason"]=text(!b(contact,"visible")?"not_visible":"no_local_lane");
                else {
                    const double d=distance(contact.get("position"),bot.get("state"));row.object["distance"]=number(d);row.object["range"]=number(engagementRange(bot,contact));
                    if(d>engagementRange(bot,contact))row.object["reason"]=text("outside_engagement_range");
                    else {
                        double score=d*.018;if(b(capabilities,"target_priority"))score+=n(contact,"health")/std::max(1.,n(contact,"max_health"))*28.;if(b(capabilities,"target_nearest"))score+=d*.12;
                        const auto& hit=lookup(recent_hits_,actor);if(b(capabilities,"target_retaliation")&&!null(hit)&&hit.get("attacker")==identityValue(identity(contact)))score-=140.;
                        if(b(capabilities,"target_human")&&s(contact,"target_kind")=="human")score-=60.;if(d<=50.)score-=100.;
                        Candidate c{score,d,actor,contact,weaponReady(bot)&&(!artillery||lane)};candidates.push_back(c);by_bot[actor].push_back(c);
                        row.object["reason"]=text("eligible");row.object["score"]=number(score);row.object["ready"]=boolean(c.ready);
                    }
                }
            }
            observation.object["candidates"].array.push_back(std::move(row));
        }
        target_decisions_[actor]=std::move(observation);
    }
    for(auto& row:by_bot)std::sort(row.second.begin(),row.second.end(),less);
    for(const auto& bot:bots) {
        const ID actor=id(bot);const auto& eligible=by_bot[actor];auto previous=target_assignments_.find(actor);auto& observation=target_decisions_[actor];
        auto oldContact=[&]()->Value {if(previous==target_assignments_.end())return {};for(const auto& c:contactsFor(bot,now))if(identityValue(identity(c))==previous->second.get("target")&&b(c,"visible")&&distance(c.get("position"),bot.get("state"))<=engagementRange(bot,c))return c;return {};};
        if(eligible.empty()) {
            const auto contact=oldContact();if(!null(contact)&&weaponCanRecover(bot)) {
                auto retained=contact;retained.object["movement_lease"]=boolean(true);assigned[actor]=retained;
                if(lookup(cover_states_,actor).get("target")==previous->second.get("target"))previous->second.object["until"]=number(now+2.);
                observation.object["tracking_reason"]=text("current_visible_contact_blocked");
            }else target_assignments_.erase(actor);continue;
        }
        if(previous==target_assignments_.end())continue;auto& lease=previous->second;
        auto existing=std::find_if(eligible.begin(),eligible.end(),[&](const Candidate& c){return identityValue(identity(c.contact))==lease.get("target");});
        if(existing==eligible.end()) {
            ensure(lease,"lane_missing_since",number(now));const auto old=oldContact();const auto& best=eligible.front();const auto& hit=lookup(recent_hits_,actor);
            bool urgent=!null(hit)&&identityValue(identity(best.contact))==hit.get("attacker")&&b(reads[actor],"target_retaliation",true);
            if(!null(old))urgent=urgent||(best.distance<=50.&&distance(old.get("position"),bot.get("state"))>50.);
            if(!null(old)&&!urgent&&weaponCanRecover(bot)&&now-n(lease,"lane_missing_since")<2.){assigned[actor]=old;assigned[actor].object["movement_lease"]=boolean(true);observation.object["tracking_reason"]=text("bounded_lane_transition");}
            else target_assignments_.erase(actor);continue;
        }
        lease.object.erase("lane_missing_since");
        if(!existing->ready){if(weaponCanRecover(bot)){assigned[actor]=existing->contact;assigned[actor].object["movement_lease"]=boolean(true);lease.object["until"]=number(now+2.);}else target_assignments_.erase(actor);continue;}
        const auto& best=eligible.front();const auto& hit=lookup(recent_hits_,actor);const auto best_key=identityValue(identity(best.contact));
        const bool close_override=best_key!=lease.get("target")&&best.distance<=50.&&existing->distance>50.;
        const bool retaliation=!null(hit)&&best_key==hit.get("attacker")&&best_key!=lease.get("target")&&b(reads[actor],"target_retaliation",true);
        if(close_override||retaliation)continue;const bool aiming=targetAimProgress(bot,lease,now),expired=now>=n(lease,"until");
        observation.object["aim_retention"]=record({{"active",boolean(aiming)},{"aligned",lease.get("aim_aligned")},{"best_error",lease.get("aim_best_error")},{"progress_at",lease.get("aim_progress_at")},{"start_seq",lease.get("aim_start_seq")}});
        if(expired&&!aiming&&existing->score>best.score+3.)continue;
        const auto key=identity(existing->contact);if(saturated(key,existing->contact,existing->distance)){refusal(actor,existing->contact,existing->distance,"retained_target");continue;}
        reserve(key,bot,existing->contact);assigned[actor]=existing->contact;if(expired)lease.object["until"]=number(now+2.);
    }
    std::sort(candidates.begin(),candidates.end(),less);
    for(const auto& c:candidates) {
        if(!c.ready||assigned.count(c.bot))continue;const auto key=identity(c.contact);
        if(saturated(key,c.contact,c.distance)){refusal(c.bot,c.contact,c.distance,"ranked_target");continue;}
        auto bot=std::find_if(bots.begin(),bots.end(),[&](const Value& b){return id(b)==c.bot;});reserve(key,*bot,c.contact);assigned[c.bot]=c.contact;target_assignments_[c.bot]=newTargetLease(*bot,key,now);
    }
    for(const auto& bot:bots) {
        const auto actor=id(bot);const auto& eligible=by_bot[actor];if(assigned.count(actor)||!weaponCanRecover(bot)||eligible.empty()||eligible.front().ready)continue;
        assigned[actor]=eligible.front().contact;assigned[actor].object["movement_lease"]=boolean(true);target_assignments_[actor]=newTargetLease(bot,identity(assigned[actor]),now);
    }
    for(const auto& bot:bots) {
        const auto actor=id(bot);if(!assigned.count(actor))target_assignments_.erase(actor);const auto& selected=lookup(assigned,actor);auto& observation=target_decisions_[actor];
        observation.object["selected"]=null(selected)?Value():identityValue(identity(selected));observation.object["outcome"]=text(!null(selected)?b(selected,"movement_lease")?"movement_lease":"assigned":by_bot[actor].empty()?"no_eligible_lane":!weaponReady(bot)?"weapon_unready":"focus_capacity");observation.object["lease_after"]=null(lookup(target_assignments_,actor))?object():lookup(target_assignments_,actor);
    }
    return assigned;
}
}
