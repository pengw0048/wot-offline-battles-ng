// Actual canonical rules -> Core publication -> external wire test bridge.
#include "room_core.hpp"
#include "room_json.hpp"
#include "room_test_resources.hpp"
#include <fstream>
#include <iostream>
namespace wot::room {
struct LobbyProbe {
 static Value run(const Value& input,const Value& resources) {
  auto delivered=array();auto config=object();config.object["schema"]=integer(1);
  Core core(config,resources,[&](Core::Peer peer,std::shared_ptr<const Value> message,std::optional<std::string>){auto row=object();row.object["peer"]=integer(static_cast<std::int64_t>(peer));row.object["message"]=*message;delivered.array.push_back(std::move(row));return true;},[](auto,auto){},[](auto,auto){},[](const Value& value)->std::optional<Value>{return value;});
  auto& state=core.state_;state.phase="battle";state.server_tick=300;state.server_time_ms=10000;state.authority_epoch=1;state.monotonic_seconds=100.;state.map_name="04_himmelsdorf";core.worker_ready_=true;state.config.object["receipt_namespace"]=text("test-room");state.config.object["receipt_arena_prefix"]=integer(1);state.config.object["round_start_time"]=integer(123);
  Core::Connection worker;worker.sender={-1,true};core.peers_.emplace(0,std::move(worker));
  for(bool bot:{false,true})for(const auto& raw:rows(input.get(bot?"bots":"players"))){auto actor=resources.get(bot?"public_bot_defaults":"public_player_defaults").resolved();for(const auto& field:raw.resolved().object)actor.object[field.first]=field.second;const auto id=intField(actor,"id");(bot?state.bots:state.players)[id]=std::move(actor);if(!bot){Core::Connection connection;connection.sender={id,false};core.peers_.emplace(static_cast<Core::Peer>(id+40),std::move(connection));}}
  if(has(input,"manifest"))state.bot_manifest=input.get("manifest");core.rules_.reset();auto out=array();
  for(const auto& op:rows(input.get("operations"))){delivered=array();Outcome outcome=Outcome::accept();const auto kind=op.get("op").resolved().text;
   if(kind=="finish")core.rules_.finish(static_cast<int>(intField(op,"winner")),"team_eliminated");
   else if(kind=="advance"){state.server_tick=intField(op,"tick",state.server_tick+1);state.server_time_ms=intField(op,"time",state.server_time_ms+33);core.rules_.advance(op.get("dt").numeric(1./30.));}
   else if(kind=="push")core.rules_.consumeEnvironmentPush(op.get("event"));
   else if(kind=="observe"){state.player_spotted.clear();state.bot_spotted.clear();for(const auto& contact:rows(op.get("message").get("contacts"))){const Rules::ActorKey target{contact.get("target_kind").resolved().text=="human"?"player":"bot",intField(contact,"target_id")};for(const auto& id:rows(contact.get("visible_by_player_ids")))state.player_spotted[*exactInteger(id)].insert(target);for(const auto& id:rows(contact.get("visible_by_bot_ids")))state.bot_spotted[*exactInteger(id)].insert(target);}core.rules_.observeVisibility(op.get("message"));}
   else if(kind=="actor"){const auto actor_kind=op.get("kind").resolved().text;const auto id=intField(op,"id");auto* actor=state.actor(actor_kind,id);if(!actor)throw std::invalid_argument("Unknown probe actor");const auto previous=*actor;for(const auto& field:op.get("changes").resolved().object)actor->object[field.first]=field.second;core.rules_.actorAdvanced(actor_kind,id,previous,op.get("publication"));}
   else {outcome=core.rules_.dispatch({intField(op,"sender",-1),op.get("worker").truth()},op.get("worker").truth()&&!input.get("owned_worker").truth()?objectView(freeze(op.get("message"))):op.get("message"));for(const auto& reply:outcome.replies)core.emit(static_cast<Core::Peer>(intField(op,"sender",1)+40),reply);}
   core.drainFacts();core.publishState();auto row=object();row.object["status"]=integer(static_cast<std::int64_t>(outcome.status));row.object["code"]=text(outcome.code);row.object["detail"]=text(outcome.detail);row.object["deliveries"]=delivered;row.object["snapshot"]=core.snapshot();out.array.push_back(std::move(row));
  }return out;
 }
};
}
int main(int argc,char**argv){using namespace wot::room;if(argc!=2)return 2;std::ifstream source(argv[1]);const auto resources=testResources(parseJson(std::string((std::istreambuf_iterator<char>(source)),{})));std::string line;while(std::getline(std::cin,line))try{std::cout<<writeJson(LobbyProbe::run(parseJson(line),resources))<<'\n';}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
