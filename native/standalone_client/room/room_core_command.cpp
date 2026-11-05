#include "room_core.hpp"
#include "room_inputs.hpp"
#include "room_json.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>

namespace wot::room {
namespace {
using ID=std::int64_t;
std::string kind(const Value& value) {return value.get("type").resolved().text;}
bool connected(const Value& actor) {return actor.get("connected").truth(true);}
bool participating(const Value& actor) {return actor.get("participating").truth(true);}
bool exists(const Value& value) {return value.resolved().kind!=Value::Kind::Null;}
Value& book(Value& actor,const char* key) {auto& value=actor.object[key];if(value.resolved().kind!=Value::Kind::Object)value=object();return value;}
bool currentRound(const Value& message,ID round) {
    const auto& value=message.get("round_id").resolved();
    if(value.kind==Value::Kind::Integer||value.kind==Value::Kind::Number)return value.numeric(-1.)==double(round);
    if(value.kind==Value::Kind::Text)try {std::size_t end=0;const auto parsed=std::stoll(value.text,&end);while(end<value.text.size()&&std::isspace(static_cast<unsigned char>(value.text[end])))++end;return end==value.text.size()&&parsed==round;}catch(const std::exception&){}
    return false;
}
std::optional<ID> strict(const Value& raw,ID low=1,ID high=INT32_MAX) {const auto& value=raw.resolved();if(value.kind!=Value::Kind::Integer||value.unsigned_integer||value.integer<low||value.integer>high)return {};return value.integer;}
bool numeric(const Value& raw) {const auto& value=raw.resolved();return (value.kind==Value::Kind::Integer||value.kind==Value::Kind::Number)&&std::isfinite(value.numeric());}
bool pythonEqual(const Value& a,const Value& b) {
    const auto scalar=[](const Value& v){return v.resolved().kind==Value::Kind::Boolean||v.resolved().kind==Value::Kind::Integer||v.resolved().kind==Value::Kind::Number;};
    return scalar(a)&&scalar(b)?a.numeric()==b.numeric():a==b;
}
void trim(Value& values) {
    while(values.object.size()>64) {
        const auto oldest=std::min_element(values.object.begin(),values.object.end(),[](const auto& a,const auto& b){return std::stoll(a.first)<std::stoll(b.first);});const auto key=oldest->first;values.object.erase(key);
    }
}
bool chatText(const Value& raw,const Value& resources) {
    const auto& value=raw.resolved();if(value.kind!=Value::Kind::Text||value.text.empty())return false;
    std::size_t i=0,units=0;bool content=false;
    while(i<value.text.size()) {
        const auto lead=static_cast<unsigned char>(value.text[i++]);std::uint32_t cp=lead;unsigned tail=0;
        if(lead>=0xc2&&lead<=0xdf){cp=lead&31;tail=1;}else if(lead>=0xe0&&lead<=0xef){cp=lead&15;tail=2;}else if(lead>=0xf0&&lead<=0xf4){cp=lead&7;tail=3;}else if(lead>=128)return false;
        if(i+tail>value.text.size())return false;for(unsigned n=0;n<tail;++n){const auto next=static_cast<unsigned char>(value.text[i++]);if((next&0xc0)!=0x80)return false;cp=(cp<<6)|(next&63);}
        if(cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)||(tail==1&&cp<128)||(tail==2&&cp<2048)||(tail==3&&cp<65536))return false;
        units+=cp>65535?2u:1u;if(units>140)return false;
        const auto& space=rows(resources.get("unicode_whitespace"));if(std::none_of(space.begin(),space.end(),[&](const Value& v){return v.numeric()==double(cp);}))content=true;
    }
    return content;
}
Outcome teamChat(State& state,Sender sender,const Value& message) {
    const auto found=state.players.find(sender.id);if(found==state.players.end()||!connected(found->second))return Outcome::reject("unknown_player");auto& actor=found->second;
    auto ack=object();ack.object["type"]=text("team_chat_ack");ack.object["protocol"]=state.resources.get("protocol");ack.object["round_id"]=message.get("round_id");ack.object["chat_seq"]=message.get("chat_seq");
    auto result=[&](bool accepted,const char* code){ack.object["accepted"]=boolean(accepted);ack.object["code"]=text(code);auto outcome=accepted?Outcome::accept():Outcome::reject(code);outcome.replies.push_back(ack);return outcome;};
    if(!currentRound(message,state.round_id)||state.phase!="battle"||!participating(actor))return result(false,"inactive_round");
    if(message.resolved().object.size()!=4||!has(message,"type")||!has(message,"round_id")||!has(message,"chat_seq")||!has(message,"text"))return result(false,"invalid_shape");
    const auto sequence=strict(message.get("chat_seq"));if(!sequence)return result(false,"invalid_sequence");if(!chatText(message.get("text"),state.resources))return result(false,"invalid_text");
    const auto key=std::to_string(*sequence),fingerprint=writeJson(message);auto& fingerprints=book(actor,"team_chat_fingerprints");
    if(has(fingerprints,key)) {if(fingerprints.get(key)!=text(fingerprint))return result(false,"sequence_conflict");auto outcome=Outcome::accept();outcome.replies.push_back(actor.get("team_chat_acks").get(key));return outcome;}
    if(*sequence<=intField(actor,"team_chat_seq"))return result(false,"stale_sequence");
    auto outcome=result(true,"accepted");actor.object["team_chat_seq"]=integer(*sequence);fingerprints.object[key]=text(fingerprint);trim(fingerprints);book(actor,"team_chat_acks").object[key]=ack;trim(actor.object["team_chat_acks"]);book(actor,"team_chat_teams").object[key]=actor.get("team");trim(actor.object["team_chat_teams"]);
    const auto side=intField(actor,"team");if(side!=1&&side!=2)return outcome;
    auto relay=object();relay.object["type"]=text("team_chat");relay.object["protocol"]=state.resources.get("protocol");relay.object["round_id"]=integer(state.round_id);relay.object["chat_seq"]=integer(*sequence);relay.object["issuer_id"]=integer(sender.id);relay.object["team"]=integer(side);relay.object["text"]=message.get("text");book(actor,"team_chat_publications").object[key]=text(fingerprint);trim(actor.object["team_chat_publications"]);
    for(const auto& recipient:state.players)if(connected(recipient.second)&&participating(recipient.second)&&intField(recipient.second,"team")==side)state.pending_deliveries.emplace_back(recipient.first,relay);
    return outcome;
}
}
void Core::command(Peer peer,Connection& connection,const Value& message) {
    const auto type=kind(message);if(type.empty())return;
    if(connection.probe) {if(type=="vehicle_overlay_query"||type=="vehicle_overlay_member")overlay(peer,message);else if(type=="leave"){disconnect_(peer,false);disconnected(peer);}return;}
    static const std::set<std::string> scoped={"start_battle","input","bot_manifest","bot_state","bot_observation","bot_ram_report","fire_intent","fire_intent_result","projectile_launch","projectile_progress","projectile_ricochet","projectile_resolve","rules_state","destructible","player_destructible_contact_result","battle_result","leave_battle","battle_ready","simulation_progress","player_environment","landing_observation","track_repair","equipment_intent","team_command","team_chat"};
    if(scoped.count(type)&&!currentRound(message,state_.round_id))return;
    const auto sender=connection.sender;
    if(type=="leave"){disconnect_(peer,false);disconnected(peer);return;}
    if(type=="ping") {auto pong=object();pong.object["type"]=text("pong");pong.object["seq"]=message.get("seq");pong.object["client_time"]=message.get("client_time");pong.object["server_time"]=number(std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count());emit(peer,std::move(pong));return;}
    if(type=="snapshot_resync") {if(state_.phase=="loading"&&exists(worker_manifest_fingerprint_))emit(peer,snapshot());else resync_(peer,sender.id);return;}
    if(type=="battle_ready") {auto ready_message=message;ready_message.object["round_id"]=integer(state_.round_id);ready(peer,ready_message);return;}
    if(type=="worker_pong"&&sender.worker) {
        const auto actor=strict(message.get("player_id"));if(!actor||!worker_ready_||peer!=worker_peer||!pythonEqual(message.get("round_id"),integer(state_.round_id))||!pythonEqual(message.get("authority_epoch"),integer(state_.authority_epoch)))return;
        const auto* player=state_.actor("player",*actor);if(!player||!connected(*player))return;auto pong=object();pong.object["type"]=text("worker_pong");pong.object["seq"]=message.get("seq");pong.object["client_time"]=message.get("client_time");pong.object["round_id"]=integer(state_.round_id);pong.object["authority_epoch"]=integer(state_.authority_epoch);pong.object["frame_ms"]=numeric(message.get("frame_ms"))&&message.get("frame_ms").numeric()>0.?message.get("frame_ms"):Value();state_.pending_deliveries.emplace_back(*actor,std::move(pong));drainDeliveries();return;
    }
    if(sender.worker) {
        if(type=="bot_manifest"||type=="bot_state"||type=="simulation_progress") {
            const auto previous=state_.bot_manifest_revision;worker(message);drainFacts();
            if(type=="bot_manifest"&&state_.bot_manifest_revision!=previous&&state_.phase=="loading") {
                const auto image=snapshot();for(const auto& recipient:peers_)if(!recipient.second.probe&&(recipient.second.sender.worker||(state_.actor("player",recipient.second.sender.id)&&participating(*state_.actor("player",recipient.second.sender.id)))))emit(recipient.first,image);
                const auto current=peers_.find(worker_peer);if(current!=peers_.end()&&current->second.ready_round==state_.round_id){auto value=object();value.object["round_id"]=integer(state_.round_id);ready(worker_peer,value);}
            }
            drainDeliveries();return;
        }
        if(type=="bot_observation") {const auto outcome=planner_.dispatch(sender,message);if(outcome.status==Outcome::Status::accepted||outcome.code=="empty_observation")rules_.observeVisibility(state_.latest_observation);drainFacts();drainDeliveries();return;}
        static const std::set<std::string> allowed={"player_environment","projectile_launch","projectile_progress","projectile_ricochet","projectile_resolve","bot_ram_report","rules_state","battle_result","destructible","player_destructible_contact_result"};if(!allowed.count(type))return;
        const auto outcome=rules_.dispatch(sender,message);drainFacts();for(const auto& reply:outcome.replies)emit(peer,reply);drainDeliveries();return;
    }
    const auto actor=state_.players.find(sender.id);if(actor==state_.players.end()||!connected(actor->second))return;
    if(type=="start_battle"){start(peer,message);return;}if(lobbyCommand(peer,connection,message))return;
    if(type=="worker_ping") {
        const auto sequence=strict(message.get("seq"));if(!sequence||!numeric(message.get("client_time"))||message.get("client_time").numeric()<=0.||!worker_ready_||!peers_.count(worker_peer))return;
        auto probe=object();probe.object["type"]=text("worker_ping");probe.object["player_id"]=integer(sender.id);probe.object["seq"]=integer(*sequence);probe.object["client_time"]=message.get("client_time");probe.object["round_id"]=integer(state_.round_id);probe.object["authority_epoch"]=integer(state_.authority_epoch);emit(worker_peer,std::move(probe));return;
    }
    if(type=="leave_battle") {
        if(state_.phase!="loading"&&state_.phase!="battle")return;const auto& voluntary=message.get("voluntary").resolved();if(has(message,"voluntary")&&voluntary.kind!=Value::Kind::Boolean)return;const bool loading=state_.phase=="loading";
        if(participating(actor->second))rules_.playerLeftBattle(sender.id,!has(message,"voluntary")||voluntary.boolean);planner_.advanceRadio();drainFacts();
        if(loading) {if(std::none_of(state_.players.begin(),state_.players.end(),[](const auto& p){return connected(p.second)&&participating(p.second);}))resetRound();else for(const auto& current:peers_)if(!current.second.probe&&current.second.ready_round==state_.round_id){auto ready_message=object();ready_message.object["round_id"]=integer(state_.round_id);ready(current.first,ready_message);break;}broadcast(lobby());}
        drainDeliveries();return;
    }
    Outcome outcome;
    if(type=="input")outcome=inputs_->dispatch(sender,message);
    else if(type=="team_command")outcome=planner_.dispatch(sender,message);
    else if(type=="team_chat")outcome=teamChat(state_,sender,message);
    else {static const std::set<std::string> allowed={"track_repair","projectile_launch","projectile_progress","projectile_ricochet","projectile_resolve","equipment_intent","landing_observation","battle_receipt_ack"};if(!allowed.count(type))return;outcome=rules_.dispatch(sender,message);}
    drainFacts();if(type=="team_command"||type=="team_chat")drainDeliveries();
    for(const auto& reply:outcome.replies){if(kind(reply)=="battle_receipt")actor->second.object["delivered_receipt_id"]=reply.get("receipt_id");emit(peer,reply);}drainDeliveries();
}
}
