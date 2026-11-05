#include "room_strategy_data.hpp"
#include <functional>

namespace wot::room {
using namespace strategy;
namespace {
ID boundedID(const Value& raw,ID low=1,ID high=2147483647) {
    const auto& v=raw.resolved();if(v.kind!=Value::Kind::Integer||v.unsigned_integer||v.integer<low||v.integer>high)throw std::invalid_argument("Invalid observation identity");return v.integer;
}
double boundedNumber(const Value& raw,double low,double high) {
    const auto& v=raw.resolved();if(v.kind!=Value::Kind::Integer&&v.kind!=Value::Kind::Number)throw std::invalid_argument("Invalid observation number");const double value=v.numeric(std::numeric_limits<double>::quiet_NaN());if(!std::isfinite(value)||value<low||value>high)throw std::invalid_argument("Observation number outside bounds");return value;
}
bool exactBool(const Value& v) { return v.resolved().kind==Value::Kind::Boolean; }
Value idsValue(const std::set<ID>& ids) {auto out=array();for(ID id:ids)out.array.push_back(integer(id));return out;}
}
Outcome Planner::admitObservation(Sender sender,const Value& message) {
    if(!sender.worker)return Outcome::reject("not_authority");
    const auto round=exactInteger(message.get("round_id"));if(message.get("round_id").resolved().kind==Value::Kind::Boolean||!round||*round!=state_.round_id)return Outcome::reject("stale_round");
    if(!null(state_.battle_result))return Outcome::accept();
    const double hz=state_.resources.get("timing").get("tick_hz").numeric(30.),prebattle=state_.config.get("prebattle_seconds").numeric(10.);
    if(state_.phase!="battle"||state_.server_tick<std::llround(prebattle*hz))return Outcome::reject("inactive_round");
    if(!list(message.get("contacts"))||(has(message,"affordances")&&!list(message.get("affordances"))))return Outcome::reject("invalid_observation");
    // Everything up to the commit below is local owned data. A bad last row
    // cannot partially replace contacts, direct spotting or radio provenance.
    try {
        Records players,bots;for(const auto& row:state_.players)if(b(row.second,"connected",true)&&b(row.second,"participating",true))players[row.first]=row.second;
        for(const auto& raw:rows(state_.bot_manifest)) {
            const ID actor=id(raw);if(!actor)continue;const auto found=state_.bots.find(actor);const auto pose=found==state_.bots.end()?Value():found->second;
            bots[actor]=record({{"team",integer(team(raw))},{"alive",boolean(b(pose,"alive",n(raw,"health",1.)>0.))},{"x",number(n(pose,"x",n(raw,"x")))},{"z",number(n(pose,"z",n(raw,"z")))}});
        }
        std::map<Identity,std::pair<int,bool>> actors;
        for(const auto& row:players)actors[{"human",row.first}]={team(row.second),b(row.second,"alive",true)};
        for(const auto& row:bots)actors[{"bot",row.first}]={team(row.second),b(row.second,"alive",true)};
        const auto known=knownTargets();
        std::function<Value(const Value&,int,std::optional<double>,std::optional<Identity>,bool,bool)> radioRows;
        radioRows=[&](const Value& raw,int side,std::optional<double> duration,std::optional<Identity> exclude,bool sources,bool allow_dead)->Value {
            if(!list(raw)||rows(raw).size()>actors.size())throw std::invalid_argument("Invalid radio recipient list");auto result=array();std::set<Identity> seen;
            for(const auto& row:rows(raw)) {
                if(!dict(row))throw std::invalid_argument("Invalid radio recipient");const auto kind=s(row,"kind");const ID actor=boundedID(row.get("id"));const Identity key{kind,actor};const auto found=actors.find(key);
                if(!oneOf(kind,{"human","bot"})||found==actors.end()||found->second.first!=side||!seen.insert(key).second||(exclude&&key==*exclude))throw std::invalid_argument("Invalid radio recipient identity");
                auto clean=record({{"kind",text(kind)},{"id",integer(actor)}});
                if(duration) {
                    const double lease=boundedNumber(row.get("time_left"),0.,*duration);if(lease<=0.)throw std::invalid_argument("Nonpositive radio recipient lease");clean.object["time_left"]=number(lease);
                    if(sources&&has(row,"sources"))clean.object["sources"]=radioRows(row.get("sources"),side,lease,std::nullopt,false,true);
                }
                if(found->second.second||allow_dead)result.array.push_back(std::move(clean));
            }
            return result;
        };
        auto ranges=array();
        if(has(message,"player_vision_ranges")) {
            const auto& raw=message.get("player_vision_ranges");if(!list(raw)||rows(raw).size()>30)throw std::invalid_argument("Invalid player vision range list");std::set<ID> seen;
            for(const auto& row:rows(raw)){if(!dict(row))throw std::invalid_argument("Invalid player vision range");const ID actor=boundedID(row.get("id"));const double radius=boundedNumber(row.get("radius"),0.,100000.);if(!seen.insert(actor).second)throw std::invalid_argument("Duplicate player vision range");if(players.count(actor))ranges.array.push_back(record({{"id",integer(actor)},{"radius",number(radius)}}));}
        }
        Value links;
        if(has(message,"radio_links")) {
            const auto& raw=message.get("radio_links");if(!list(raw)||rows(raw).size()>players.size())throw std::invalid_argument("Invalid radio link list");links=array();std::set<ID> seen;
            for(const auto& row:rows(raw)) {
                if(!dict(row)||s(row,"kind")!="human")throw std::invalid_argument("Invalid radio receiver");const ID actor=boundedID(row.get("id"));const auto found=players.find(actor);if(found==players.end()||!seen.insert(actor).second)throw std::invalid_argument("Invalid radio receiver identity");
                const auto allies=radioRows(row.get("allies"),team(found->second),std::nullopt,Identity{"human",actor},false,false);
                if(b(found->second,"alive",true))links.array.push_back(record({{"kind",text("human")},{"id",integer(actor)},{"allies",allies}}));
            }
        }
        std::map<ID,std::set<Identity>> direct_bots,direct_players;for(const auto& row:bots)direct_bots[row.first]={};for(const auto& row:players)direct_players[row.first]={};
        auto contacts=array();bool stale=false,valid_hidden=false;
        for(const auto& raw:rows(message.get("contacts"))) {
            if(!dict(raw)||!exactBool(raw.get("visible"))||!exactBool(raw.get("fresh"))||!list(raw.get("visible_by_bot_ids"))||!list(raw.get("visible_by_player_ids"))||!list(raw.get("shootable_by_bot_ids"))||rows(raw.get("visible_by_bot_ids")).size()>bots.size()||rows(raw.get("visible_by_player_ids")).size()>players.size())throw std::invalid_argument("Invalid contact observation shape");
            auto contact=raw;const int side=int(boundedID(raw.get("observing_team"),1,2)),target_team=int(boundedID(raw.get("target_team"),1,2));const ID target_id=boundedID(raw.get("target_id"));const auto kind=s(raw,"target_kind");const double duration=boundedNumber(raw.get("time_left"),0.,14.);const auto target=known.find({kind,target_id});
            if(target==known.end()) {
                bool retired=kind=="human"&&state_.players.count(target_id);if(kind=="bot"){retired=state_.bots.count(target_id);if(!retired)for(const auto& row:rows(state_.bot_manifest))if(id(row)==target_id){retired=true;break;}}
                if(retired){stale=true;continue;}throw std::invalid_argument("Unknown observation target");
            }
            if(!b(target->second,"alive",true)){stale=true;continue;}
            if(!oneOf(kind,{"human","bot"})||team(target->second)==side||target_team!=team(target->second))throw std::invalid_argument("Invalid observation target team");
            const bool reported_fresh=!rows(raw.get("visible_by_bot_ids")).empty()||!rows(raw.get("visible_by_player_ids")).empty();const bool visible=b(raw,"visible");
            if(b(raw,"fresh")!=reported_fresh||visible!=(duration>0.)||(reported_fresh&&!visible)||(!visible&&!rows(raw.get("shootable_by_bot_ids")).empty()))throw std::invalid_argument("Incoherent observation visibility");
            if(has(contact,"radio_recipients"))contact.object["radio_recipients"]=radioRows(contact.get("radio_recipients"),side,duration,std::nullopt,true,false);
            auto cleanIDs=[&](const Value& ids,const Records& source)->std::set<ID> {
                std::set<ID> seen,alive;
                for(const auto& raw_id:rows(ids)){const ID actor=boundedID(raw_id);const auto found=source.find(actor);if(found==source.end()||team(found->second)!=side||!seen.insert(actor).second)throw std::invalid_argument("Invalid contact observer identity");if(b(found->second,"alive",true))alive.insert(actor);else stale=true;}
                return alive;
            };
            const auto observers=cleanIDs(contact.get("visible_by_bot_ids"),bots),humans=cleanIDs(contact.get("visible_by_player_ids"),players);auto shooters=cleanIDs(contact.get("shootable_by_bot_ids"),bots);std::set<ID> radio_bots;
            if(has(contact,"radio_recipients")) {
                for(const auto& row:rows(contact.get("radio_recipients")))if(s(row,"kind")=="bot")radio_bots.insert(id(row));
                for(auto i=shooters.begin();i!=shooters.end();)if(!radio_bots.count(*i))i=shooters.erase(i);else ++i;
            }
            contact.object["visible_by_bot_ids"]=idsValue(observers);contact.object["visible_by_player_ids"]=idsValue(humans);contact.object["shootable_by_bot_ids"]=idsValue(shooters);
            std::set<ID> pending;const auto& raw_pending=rows(contact.get("pending_lane_by_bot_ids"));for(std::size_t i=0;i<std::min(raw_pending.size(),bots.size());++i)try {
                const ID actor=boundedID(raw_pending[i]);const auto found=bots.find(actor);if(found!=bots.end()&&b(found->second,"alive")&&team(found->second)==side&&!shooters.count(actor)&&(!has(contact,"radio_recipients")||radio_bots.count(actor)))pending.insert(actor);
            }catch(const std::invalid_argument&){}
            contact.object["pending_lane_by_bot_ids"]=idsValue(pending);
            if(has(contact,"threatened_bot_ids")) {
                std::set<ID> threatened;for(const auto& raw_id:rows(contact.get("threatened_bot_ids")))try{const ID actor=boundedID(raw_id);const auto found=bots.find(actor);if(found!=bots.end()&&b(found->second,"alive")&&team(found->second)==side)threatened.insert(actor);}catch(const std::invalid_argument&){}
                contact.object["threatened_bot_ids"]=visible?idsValue(threatened):array();
            }
            const bool fresh=!observers.empty()||!humans.empty();contact.object["fresh"]=boolean(fresh);if(!fresh&&has(contact,"threatened_bot_ids"))contact.object["threatened_bot_ids"]=array();contact.object["time_left"]=number(duration);
            const Identity marker{kind=="human"?"player":"bot",target_id};for(ID actor:observers)direct_bots[actor].insert(marker);for(ID actor:humans)direct_players[actor].insert(marker);
            if(!visible)valid_hidden=true;contacts.array.push_back(std::move(contact));
        }
        // Commit one fully validated batch, then share the same canonical data
        // with tactical memory, statistics and the visible peer relay.
        Value accepted=array();const double now=state_.monotonic_seconds;const auto accepted_contacts=reportContacts(contacts,now,accepted);const auto accepted_affordances=reportAffordances(message.get("affordances"),now);
        state_.bot_spotted=std::move(direct_bots);state_.player_spotted=std::move(direct_players);state_.accepted_visibility=accepted;
        auto canonical=message;canonical.object["contacts"]=contacts;canonical.object["player_vision_ranges"]=ranges;if(!null(links))canonical.object["radio_links"]=links;state_.latest_observation=std::move(canonical);
        if(!rows(accepted).empty()||!null(links)) {
            auto relay=record({{"type",text("bot_observation")},{"protocol",integer(5)},{"round_id",integer(state_.round_id)},{"contacts",accepted}});if(!null(links))relay.object["radio_links"]=links;
            for(const auto& row:state_.players)if(b(row.second,"connected",true)&&b(row.second,"participating",true))state_.pending_deliveries.emplace_back(row.first,relay);
            return Outcome::accept();
        }
        return accepted_contacts||accepted_affordances||valid_hidden||stale?Outcome::accept():Outcome::reject("empty_observation");
    } catch(const std::invalid_argument& e){return Outcome::reject("invalid_observation",e.what());}
}
}
