#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
Value botIds(const Value& raw) {
    auto out=array(); std::set<ID> seen;
    for (std::size_t i=0;i<std::min<std::size_t>(32,rows(raw).size());++i) {
        const auto value=exactInteger(rows(raw)[i]).value_or(0);
        if(value>0 && seen.insert(value).second) out.array.push_back(integer(value));
    }
    return out;
}
bool trueBoolean(const Value& raw) { return raw.resolved().kind==Value::Kind::Boolean; }
}
void Planner::reset() {
    clearRadio("round_complete");state_.team_commands=array();
    for(auto& row:state_.players) {
        std::vector<std::string> fields;for(const auto& field:row.second.object)if(field.first.rfind("team_command_",0)==0||field.first.rfind("_team_command_",0)==0||field.first.rfind("team_chat_",0)==0)fields.push_back(field.first);
        for(const auto& field:fields)row.second.object.erase(field);
    }
    contacts_.clear(); contacts_[1]={}; contacts_[2]={};
    for (Records* values : {&affordances_,&recent_hits_,&route_states_,&route_history_,&route_recoveries_,&recovery_route_visits_,
            &wreck_progress_,&wreck_avoid_,&route_assignments_,&assignment_receipts_,&travel_tasks_,&previous_player_,&engage_anchors_,
            &firing_holds_,&cover_states_,&cover_failures_,&target_assignments_,&target_decisions_,&combat_states_,&retreat_states_,
            &capture_arrivals_,&artillery_anchors_}) values->clear();
    empty_bots_.clear(); previous_combat_.clear(); cover_reservations_.clear();
    base_defense_.clear(); balance_receipts_.clear(); next_rebalance_.clear(); wait_claims_.clear();
    team_commands_=array(); wait_bodies_=array(); last_signature_=Value();
    state_.bot_orders=array(); state_.bot_order_revision=0;
    ammo_policies_.clear();state_.pending_ammo=array();
    state_.bot_planner_dirty=false;
}
void Planner::clearObservations() {
    contacts_.clear();contacts_[1]={};contacts_[2]={};
    for(Records* values:{&affordances_,&cover_states_,&cover_failures_,&engage_anchors_,&firing_holds_,&combat_states_,&retreat_states_})values->clear();
    cover_reservations_.clear();
}
Value Planner::debugSummary(double now) const {
    auto result=object(),teams=object();for(int side:{1,2}) {
        int known=0,visible=0,targeted=0,fire=0,count=0;auto modes=object();const auto memory=contacts_.find(side);
        if(memory!=contacts_.end())for(const auto& row:memory->second)if(now-n(row.second,"last_seen")<=8.){++known;if(b(row.second,"visible"))++visible;}
        for(const auto& order:rows(state_.bot_orders))if(team(order)==side){++count;if(!null(order.get("target_id")))++targeted;if(b(order,"fire_allowed"))++fire;const auto mode=s(order,"combat_mode","unknown");modes.object[mode]=integer(intField(modes,mode)+1);}
        teams.object[std::to_string(side)]=record({{"contacts",integer(known)},{"visible",integer(visible)},{"orders",integer(count)},{"targeted",integer(targeted)},{"fire",integer(fire)},{"modes",modes}});
    }
    result.object["teams"]=teams;return result;
}
std::map<Identity,Value> Planner::knownTargets() const {
    std::map<Identity,Value> out;
    for (const auto& row : state_.players) if(b(row.second,"connected",true)&&b(row.second,"participating",true))out[{"human",row.first}]=record({{"kind",text("human")},{"team",row.second.get("team")},{"alive",boolean(b(row.second,"alive",true))}});
    for (const auto& row : state_.bots) out[{"bot",row.first}]=record({{"kind",text("bot")},{"team",row.second.get("team")},{"alive",boolean(b(row.second,"alive",true))}});
    return out;
}
std::vector<Value> Planner::aliveBots() const {
    std::vector<Value> out;
    for (const auto& raw : rows(state_.bot_manifest)) {
        const auto actor=id(raw); if(!actor) continue;
        const auto state=state_.bots.find(actor); const Value pose=state==state_.bots.end()?object():state->second;
        if(!b(pose,"alive",n(raw,"health",1.)>0.)) continue;
        double rating=1.;
        if(has(raw,"skill_rating")) rating=std::clamp(n(raw,"skill_rating",.34),0.,1.);
        else { const auto skill=s(raw,"skill"); if(skill=="rookie")rating=0.;else if(skill=="regular")rating=.34;else if(skill=="veteran")rating=.67;else if(skill=="elite")rating=1.; }
        // Canonical SPG plans are installed once with the validated manifest.
        out.push_back(record({{"id",integer(actor)},{"team",integer(team(raw))},{"slot",raw.get("slot")},
            {"rating",number(rating)},{"spg_initial",canonicalSpgPlan(raw.get("spg_initial"),state_.resources.get("strategy"),state_.map_name,raw.get("vehicle"),team(raw))},{"profile",dict(raw.get("profile"))?raw.get("profile"):object()},
            {"route",dict(raw.get("route"))?raw.get("route"):object()},{"state",pose}}));
    }
    return out;
}
void Planner::admitDamage(ID victim,const std::string& raw_kind,ID attacker,int damage,double now) {
    const auto kind=raw_kind=="player"?"human":raw_kind;
    if(victim<=0||attacker<=0||damage<=0||!oneOf(kind,{"human","bot"}))return;
    recent_hits_[victim]=record({{"attacker",identityValue({kind,attacker})},{"reported_at",number(now)},{"damage",integer(damage)}});
}
std::size_t Planner::reportContacts(const Value& reports,double now,Value& visibility) {
    const auto known=knownTargets();std::size_t accepted=0;
    for(std::size_t i=0;i<std::min<std::size_t>(64,rows(reports).size());++i) {
        const auto& raw=rows(reports)[i];
        if(!dict(raw)||!list(raw.get("shootable_by_bot_ids"))||!trueBoolean(raw.get("visible")))continue;
        const int observing=int(intField(raw,"observing_team"));const ID actor=intField(raw,"target_id");auto kind=s(raw,"target_kind");
        auto found=known.find({kind,actor});
        if(found==known.end()&&kind.empty()) {
            std::vector<decltype(found)> matches;for(auto j=known.begin();j!=known.end();++j)if(j->first.second==actor)matches.push_back(j);
            if(matches.size()==1){found=matches.front();kind=found->first.first;}
        }
        if((observing!=1&&observing!=2)||found==known.end())continue;
        if(team(found->second)==observing||intField(raw,"target_team",team(found->second))!=team(found->second)||!b(found->second,"alive",true))continue;
        const Identity key{kind,actor};auto& memory=contacts_[observing];
        const bool visible=b(raw,"visible");if(!visible&&!memory.count(key))continue;
        auto receipt=record({{"observing_team",integer(observing)},{"target_kind",text(kind)},{"target_id",integer(actor)},
            {"target_team",found->second.get("team")},{"visible",boolean(visible)},
            {"fresh",boolean(visible&&b(raw,"fresh",visible))},{"time_left",number(visible?std::max(0.,n(raw,"time_left",10.)):0.)}});
        if(visible) {
            auto contact=record({{"id",integer(actor)},{"target_kind",text(kind)},{"team",found->second.get("team")},
                {"visible",boolean(true)},{"last_seen",number(now)},{"position",point(raw)},
                {"health",integer(std::max<ID>(0,intField(raw,"health",1)))},{"max_health",integer(std::max<ID>(1,intField(raw,"max_health",1)))},
                {"class_tag",text(s(raw,"class_tag","unknown").substr(0,24))},{"armor",number(std::max(0.,n(raw,"armor")))},
                {"shootable_by_bot_ids",botIds(raw.get("shootable_by_bot_ids"))},{"pending_lane_by_bot_ids",botIds(raw.get("pending_lane_by_bot_ids"))},
                {"threatened_bot_ids",list(raw.get("threatened_bot_ids"))?botIds(raw.get("threatened_bot_ids")):Value()}});
            if(has(raw,"radio_recipients")) {
                auto until=object();for(const auto& row:rows(raw.get("radio_recipients")))if(dict(row)&&s(row,"kind")=="bot")
                    until.object[std::to_string(id(row))]=number(now+std::clamp(n(row,"time_left"),0.,10.));
                contact.object["radio_bot_until"]=std::move(until);
            }
            memory[key]=std::move(contact);
            for(const char* field:{"visible_by_bot_ids","visible_by_player_ids","shootable_by_bot_ids"})receipt.object[field]=botIds(raw.get(field));
            if(list(raw.get("threatened_bot_ids")))receipt.object["threatened_bot_ids"]=botIds(raw.get("threatened_bot_ids"));
            if(has(raw,"radio_recipients"))receipt.object["radio_recipients"]=raw.get("radio_recipients");
        } else {
            auto& contact=memory[key];contact.object["visible"]=boolean(false);
            for(const char* field:{"shootable_by_bot_ids","pending_lane_by_bot_ids","threatened_bot_ids"})contact.object[field]=array();
            if(has(raw,"radio_recipients")||has(contact,"radio_bot_until"))contact.object["radio_bot_until"]=object();
            for(const char* field:{"visible_by_bot_ids","visible_by_player_ids","shootable_by_bot_ids","threatened_bot_ids"})receipt.object[field]=array();
            if(has(raw,"radio_recipients"))receipt.object["radio_recipients"]=array();
        }
        visibility.array.push_back(std::move(receipt));++accepted;
    }
    return accepted;
}
std::size_t Planner::reportAffordances(const Value& reports,double now) {
    const auto known=knownTargets();Records known_bots;for(const auto& raw:rows(state_.bot_manifest)) {
        const ID actor=id(raw);if(!actor)continue;const auto found=state_.bots.find(actor);const auto pose=found==state_.bots.end()?Value():found->second;
        known_bots[actor]=record({{"team",integer(team(raw))},{"alive",boolean(b(pose,"alive",n(raw,"health",1.)>0.))},{"x",number(n(pose,"x",n(raw,"x")))},{"z",number(n(pose,"z",n(raw,"z")))}});
    }
    std::size_t accepted=0;
    for(std::size_t i=0;i<std::min<std::size_t>(16,rows(reports).size());++i) {
        const auto& raw=rows(reports)[i];if(!dict(raw))continue;
        const ID actor=intField(raw,"bot_id");auto bot=known_bots.find(actor);const Identity key{s(raw,"target_kind"),intField(raw,"target_id")};
        auto target=known.find(key);if(bot==known_bots.end()||!b(bot->second,"alive",true)||target==known.end()||team(bot->second)==team(target->second))continue;
        auto& source=contacts_[team(bot->second)];auto contact=source.find(key);
        if(contact==source.end()||!b(contact->second,"visible")||now-n(contact->second,"last_seen")>8.)continue;
        auto candidates=array();
        for(std::size_t j=0;j<std::min<std::size_t>(12,rows(raw.get("candidates")).size());++j) {
            auto c=normalizeCover(rows(raw.get("candidates"))[j]);if(null(c.get("position")))continue;
            if(n(c,"travel_distance")>180.||n(c,"water")>=.5||n(c,"slope")>28.||distance(c.get("position"),bot->second)>180.)continue;
            if(!null(c.get("peek_position"))&&distance(c.get("peek_position"),bot->second)>200.)continue;
            c.object["position"]=point(c.get("position"));if(!null(c.get("peek_position")))c.object["peek_position"]=point(c.get("peek_position"));
            candidates.array.push_back(std::move(c));
        }
        if(candidates.array.empty())continue;
        affordances_[actor]=record({{"target",identityValue(key)},{"reported_at",number(now)},{"candidates",std::move(candidates)}});++accepted;
    }
    return accepted;
}
void Planner::prune(double now) {
    const auto known=knownTargets();
    for(auto& source:contacts_)for(auto i=source.second.begin();i!=source.second.end();) {
        auto target=known.find(i->first);
        if(target==known.end()||!b(target->second,"alive")||now-n(i->second,"last_seen")>8.){i=source.second.erase(i);continue;}
        auto& c=i->second;
        if(has(c,"radio_bot_until")) {
            auto until=object(),shooters=array();
            for(const auto& row:c.get("radio_bot_until").resolved().object)if(row.second.numeric()>now)until.object[row.first]=row.second;
            for(const auto& actor:rows(c.get("shootable_by_bot_ids")))if(has(until,std::to_string(exactInteger(actor).value_or(0))))shooters.array.push_back(actor);
            c.object["radio_bot_until"]=std::move(until);c.object["shootable_by_bot_ids"]=std::move(shooters);
        }
        ++i;
    }
    for(auto i=recent_hits_.begin();i!=recent_hits_.end();)if(now-n(i->second,"reported_at")>6.)i=recent_hits_.erase(i);else ++i;
}
std::vector<Value> Planner::contactsFor(const Value& bot,double now) const {
    std::vector<Value> out;auto source=contacts_.find(team(bot));if(source==contacts_.end())return out;
    for(const auto& row:source->second) {
        const auto& c=row.second;
        if(!has(c,"radio_bot_until")||c.get("radio_bot_until").get(std::to_string(id(bot))).numeric()>now)out.push_back(c);
    }
    return out;
}
Outcome Planner::dispatch(Sender sender,const Value& message) {
    if(s(message,"type")=="team_command")return dispatchRadio(sender,message);
    if(s(message,"type")=="bot_observation")return admitObservation(sender,message);
    return {};
}
}
