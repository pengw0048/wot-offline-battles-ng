#include "room_core.hpp"
#include "room_bot_rows.hpp"
#include "room_inputs.hpp"
#include <algorithm>
#include <cmath>

namespace wot::room {
namespace {
using ID=std::int64_t;
bool present(const Value& v) {return v.resolved().kind!=Value::Kind::Null;}
std::string field(const Value& v,const char* key) {return v.get(key).resolved().text;}
bool flag(const Value& v,const char* key,bool fallback=true) {return has(v,key)?v.get(key).truth():fallback;}
Value shared(Value value) {Value out;out.kind=Value::Kind::Shared;out.reference=freeze(std::move(value));return out;}
bool diagnostic(const std::string& key) {return key=="route_attempt"||key=="route_assignment"||key=="tactical_evidence";}
bool sameOrders(const Value& raw,const Value& lean) {
    if(rows(raw).size()!=rows(lean).size())return false;
    for(std::size_t i=0;i<rows(raw).size();++i) {
        const auto& source=rows(raw)[i].resolved();const auto& target=rows(lean)[i].resolved();std::size_t count=0;
        for(const auto& entry:source.object)if(!diagnostic(entry.first)){++count;if(!has(target,entry.first)||entry.second!=target.get(entry.first))return false;}
        if(count!=target.object.size())return false;
    }
    return true;
}
Value leanOrders(const Value& raw) {
    auto out=array();for(const auto& order:rows(raw)){auto lean=object();for(const auto& entry:order.resolved().object)if(!diagnostic(entry.first))lean.object[entry.first]=entry.second;out.array.push_back(std::move(lean));}return out;
}
}
Value Core::snapshot() const {
    auto result=object();
    result.object["type"]=text("snapshot");result.object["protocol"]=state_.resources.get("protocol");
    if(state_.phase=="loading")result.object["client_build"]=state_.resources.get("client_build");result.object["server_tick"]=integer(state_.server_tick);
    result.object["round_id"]=integer(state_.round_id);result.object["state_revision"]=integer(state_.state_revision);
    result.object["map"]=text(state_.map_name);result.object["bot_authority_id"]=worker_ready_?integer(-1):Value();
    result.object["authority_epoch"]=integer(state_.authority_epoch);result.object["server_time_ms"]=integer(state_.server_time_ms);
    result.object["motion_time_us"]=integer(state_.motion_time_us);result.object["bot_state_time_us"]=integer(state_.bot_state_time_us);
    result.object["bot_state_revision"]=integer(state_.bot_state_revision);result.object["bot_order_revision"]=integer(state_.bot_order_revision);
    result.object["projectile_revision"]=integer(state_.projectile_revision);result.object["projectiles"]=rules_.projectiles();
    result.object["human_ram_probes"]=rules_.humanRamProbes();result.object["detached_turrets"]=detached_turrets_;
    result.object["battle_result"]=state_.battle_result;result.object["timing"]=timing();
    result.object["worker_status"]=text(worker_ready_?"connected":field(state_.config,"worker_failure_reason").empty()?"missing":"failed");
    result.object["worker_failure_reason"]=state_.config.get("worker_failure_reason");
    if(!present(result.get("worker_failure_reason")))result.object["worker_failure_reason"]=text("");
    auto players=array();for(const auto& actor:state_.players)if(flag(actor.second,"connected")&&flag(actor.second,"participating"))players.array.push_back(publicActor(actor.second,false,state_.phase=="loading"));
    result.object["players"]=std::move(players);auto bots=array();for(const auto& actor:state_.bots)bots.array.push_back(publicActor(actor.second,true,true));
    if(state_.phase=="loading")result.object["bots"]=std::move(bots);
    else {auto equipment=encodeSnapshotEquipment(bots);result.object["bots"]=std::move(equipment.first);if(!rows(equipment.second).empty())result.object["bot_equipment_contracts"]=std::move(equipment.second);}
    auto rules=object();rules.object["bases"]=state_.rules.get("bases");result.object["rules"]=std::move(rules);
    auto update=[&](FrozenSection& cached,ID revision,const Value& value,bool orders=false) {
        if(cached.round!=state_.round_id||cached.epoch!=state_.authority_epoch||cached.revision!=revision||
            (orders&&!sameOrders(value,cached.value))) {
            cached.value=shared(orders?leanOrders(value):value);cached.round=state_.round_id;cached.epoch=state_.authority_epoch;cached.revision=revision;
        }
        return cached.value;
    };
    result.object["bot_manifest"]=update(manifest_cache_,state_.bot_manifest_revision,state_.bot_manifest);
    result.object["bot_orders"]=update(orders_cache_,state_.bot_order_revision,state_.bot_orders,true);
    result.object["destructibles"]=update(destructibles_cache_,state_.destructible_revision,state_.destructibles);
    result.object["destructible_base_revision"]=integer(0);result.object["destructible_revision"]=integer(state_.destructible_revision);
    auto sections=object();sections.object["manifest_revision"]=integer(state_.bot_manifest_revision);sections.object["order_revision"]=integer(state_.bot_order_revision);sections.object["destructible_revision"]=integer(state_.destructible_revision);
    for(const char* name:{"manifest_refresh_ticks","order_refresh_ticks","destructible_refresh_ticks"}) {
        const std::string source=std::string(name)=="manifest_refresh_ticks"?"bot_manifest_refresh_ticks":std::string(name)=="order_refresh_ticks"?"bot_order_refresh_ticks":name;
        sections.object[name]=state_.resources.get("timing").get(source);
    }
    result.object["_room_sections"]=std::move(sections);
    if(state_.phase=="loading")for(const char* key:{"state_revision","timing","human_ram_probes","bot_orders"})result.object.erase(key);
    return result;
}
void Core::drainDeliveries() {
    // Rules::finish persisted the new ledger before publishing battle_result.
    // Queue the matching receipt before either its causes or terminal state.
    for(const auto& peer:peers_)if(!peer.second.probe&&!peer.second.sender.worker) {
        const auto player=state_.players.find(peer.second.sender.id);if(player==state_.players.end()||!flag(player->second,"connected"))continue;
        const auto receipts=rules_.pendingReceipts(field(player->second,"account_key"));Value receipt;
        if(present(state_.battle_result))for(const auto& row:rows(receipts))if(intField(row,"round_id")==state_.round_id){receipt=row;break;}
        if(!present(receipt)&&!rows(receipts).empty())receipt=rows(receipts).front();
        if(present(receipt)&&receipt.get("receipt_id")!=player->second.get("delivered_receipt_id")){emit(peer.first,receipt);player->second.object["delivered_receipt_id"]=receipt.get("receipt_id");}
    }
    auto deliveries=std::move(state_.pending_deliveries);state_.pending_deliveries.clear();
    for(auto& delivery:deliveries)for(const auto& peer:peers_)if(!peer.second.probe&&peer.second.sender.id==delivery.first){emit(peer.first,std::move(delivery.second));break;}
}
void Core::drainFacts() {
    for(std::size_t i=0;i<state_.events.size();) {
        if(field(state_.events[i],"kind")!="environment_push"){++i;continue;}
        // Consuming a push may append consequences. Remove its private source
        // event first and retain no vector references across the call.
        auto event=std::move(state_.events[i]);state_.events.erase(state_.events.begin()+static_cast<std::ptrdiff_t>(i));rules_.consumeEnvironmentPush(event);
    }
    auto damage=std::move(state_.pending_damage);state_.pending_damage=array();
    for(const auto& fact:rows(damage))planner_.admitDamage(intField(fact,"bot_id"),field(fact,"attacker_kind"),intField(fact,"attacker_id"),static_cast<int>(intField(fact,"damage")),state_.monotonic_seconds);
    planner_.drainAmmo();
}
void Core::publishState() {
    if(stopped_)return;drainFacts();
    std::vector<Value> public_events;auto pending=std::move(state_.events);state_.events.clear();
    for(auto& event:pending) {
        if(field(event,"kind")=="environment_push")continue;
        if(field(event,"type")=="player_destructible_contact"){emit(worker_peer,std::move(event));continue;}
        std::vector<std::string> internal;for(const auto& row:event.object)if(row.first.rfind("_room_",0)==0)internal.push_back(row.first);for(const auto& key:internal)event.object.erase(key);
        if(intField(state_.config,"event_id_round",-1)!=state_.round_id||intField(state_.config,"event_id_tick",-1)!=state_.server_tick){state_.config.object["event_id_round"]=integer(state_.round_id);state_.config.object["event_id_tick"]=integer(state_.server_tick);state_.config.object["event_id_ordinal"]=integer(0);}
        const auto ordinal=intField(state_.config,"event_id_ordinal");state_.config.object["event_id_ordinal"]=integer(ordinal+1);event.object["event_id"]=text(std::to_string(state_.round_id)+":"+std::to_string(state_.server_tick)+":"+std::to_string(ordinal));public_events.push_back(std::move(event));
    }
    drainDeliveries();
    for(std::size_t offset=0;offset<public_events.size();offset+=256) {
        auto message=object(),events=array();const auto count=std::min<std::size_t>(256,public_events.size()-offset);events.array.assign(public_events.begin()+static_cast<std::ptrdiff_t>(offset),public_events.begin()+static_cast<std::ptrdiff_t>(offset+count));
        message.object["type"]=text("events");message.object["protocol"]=state_.resources.get("protocol");message.object["round_id"]=integer(state_.round_id);message.object["server_tick"]=integer(state_.server_tick);message.object["events"]=std::move(events);message.object["authority_epoch"]=integer(state_.authority_epoch);message.object["server_time_ms"]=integer(state_.server_time_ms);message.object["bot_authority_id"]=worker_ready_?integer(-1):Value();
        message.object["worker_status"]=text(worker_ready_?"connected":field(state_.config,"worker_failure_reason").empty()?"missing":"failed");message.object["worker_failure_reason"]=text(field(state_.config,"worker_failure_reason"));broadcast(std::move(message));
    }
    const bool lineage=manifest_cache_.round!=state_.round_id||manifest_cache_.epoch!=state_.authority_epoch||manifest_cache_.revision!=state_.bot_manifest_revision;
    const ID cadence=std::max<ID>(1,intField(state_.resources.get("timing"),"replica_snapshot_ticks",2));
    const bool visible_due=state_.phase!="battle"||state_.server_tick<=1||state_.server_tick%cadence==0||!public_events.empty()||lineage;
    auto image=freeze(snapshot());
    // This is only offer cadence. Section frontiers belong to the writer's
    // completed TCP baseline; a refused/superseded offer advances none of them.
    for(auto& peer:peers_)if(!peer.second.probe&&(peer.second.sender.worker||visible_due)) {
        auto& outbox=peer.second.pending;const std::optional<std::string> key="snapshot";
        if(!outbox.empty()&&outbox.back().key==key)outbox.back().message=image;else outbox.push_back({image,key});flush(peer.second,peer.first);
    }
}
void Core::advance(double elapsed,ID microseconds) {
    if(stopped_)return;if(!std::isfinite(elapsed)||elapsed<0.)throw std::invalid_argument("Invalid native room elapsed time");clock(microseconds);
    for(auto& peer:peers_)flush(peer.second,peer.first);
    if(state_.phase=="battle"&&present(state_.battle_result)) {
        if(!result_reset_tick_)result_reset_tick_=intField(state_.config,"result_reset_tick",state_.server_tick+std::llround(5.*state_.resources.get("timing").get("tick_hz").numeric(30.)));
        if(state_.server_tick+1>=*result_reset_tick_){resetRound();broadcast(lobby());drainDeliveries();return;}
    }
    if(state_.phase!="battle") {drainFacts();drainDeliveries();return;}
    if(live_published_&&prebattle_us_==0){prebattle_us_=clock_us_+1;return;}
    ++state_.server_tick;const double hz=state_.resources.get("timing").get("tick_hz").numeric(30.);state_.server_time_ms=std::llround(double(state_.server_tick)*1000./hz);
    inputs_->advanceTicks();drainFacts();rules_.beginTick();
    for(auto& row:state_.players) {
        auto& actor=row.second;if(!flag(actor,"alive")||present(state_.battle_result))continue;const auto siege=intField(actor,"siege_state");
        if(siege==1||siege==3){actor.object["forward"]=number(0.);actor.object["turn"]=number(0.);actor.object["speed"]=number(0.);}
        else if(!flag(actor,"client_position",false)) {
            const double yaw=actor.get("yaw").numeric()+actor.get("turn").numeric()*.85*elapsed;actor.object["yaw"]=number(yaw);double limit=14.;const auto& params=rows(state_.resources.get("siege_vehicle_params").get(field(actor,"vehicle")));if(siege==2&&params.size()>=3)limit=params[2].numeric();const double speed=limit*actor.get("forward").numeric();
            actor.object["x"]=number(std::clamp(actor.get("x").numeric()+std::sin(yaw)*speed*elapsed,-220.,220.));actor.object["z"]=number(std::clamp(actor.get("z").numeric()+std::cos(yaw)*speed*elapsed,-220.,220.));
        }
    }
    rules_.advance(elapsed,false);drainFacts();planner_.advanceRadio();
    if(!present(state_.battle_result)&&(state_.server_tick>=next_planner_tick_||state_.bot_planner_dirty)) {
        planner_.build(state_.monotonic_seconds);state_.bot_planner_dirty=false;next_planner_tick_=state_.server_tick+std::max<ID>(1,intField(state_.resources.get("timing"),"bot_planner_interval_ticks",30));
    }
    rules_.expireStuns();publishState();
}
void Core::disconnected(Peer peer) {
    const auto found=peers_.find(peer);if(found==peers_.end())return;const auto sender=found->second.sender;
    if(found->second.probe){peers_.erase(found);return;}
    if(sender.worker) {
        peers_.erase(found);worker_ready_=false;++state_.authority_epoch;publication_clock_.producerChanged();planner_.clearObservations();rules_.authorityChanged();state_.bot_launch_clock_offset_us.reset();strategy_ready_=false;++state_.state_revision;
        if(state_.phase=="loading"||state_.phase=="battle") {
            state_.config.object["worker_failure_reason"]=text("worker_disconnected");state_.phase="battle";live_published_=false;
            auto event=object();event.object["kind"]=text("authority");event.object["player_id"]=Value();event.object["round_id"]=integer(state_.round_id);event.object["authority_epoch"]=integer(state_.authority_epoch);state_.events.push_back(std::move(event));rules_.finish(0,"worker_disconnected",0,false);
            if(state_.players.empty()){resetRound();broadcast(lobby());return;}publishState();
        }else broadcast(lobby());return;
    }
    const ID actor=sender.id;rules_.playerDisconnected(actor);planner_.advanceRadio();drainFacts();
    state_.players.erase(actor);catalogs_.erase(actor);peers_.erase(peer);if(host_player_==actor){host_player_=0;for(const auto& row:state_.players)if(flag(row.second,"connected")){host_player_=row.first;break;}}
    if(state_.phase=="waiting"){assignSlots();makeRoster();broadcast(lobby());}
    else if(state_.phase=="loading") {
        if(state_.players.empty()){resetRound();broadcast(lobby());}
        else {for(const auto& current:peers_)if(!current.second.probe&&current.second.ready_round==state_.round_id){auto message=object();message.object["round_id"]=integer(state_.round_id);ready(current.first,message);break;}broadcast(lobby());}
    } else {publishState();broadcast(lobby());}
    drainDeliveries();
}
void Core::stop() {
    if(stopped_)return;stopped_=true;
    // Process shutdown has no gameplay result in the source server. Persisted
    // receipts already belong to Rules' durable owner; do not mint new ones.
    for(const auto& peer:peers_)disconnect_(peer.first,false);peers_.clear();worker_ready_=false;
}
}
