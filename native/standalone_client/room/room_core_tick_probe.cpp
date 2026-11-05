#include "room_test_resources.hpp"
// Portable tick/publication probe. No engine or network is instantiated.
#include "room_core.hpp"
#include "room_json.hpp"
#include <fstream>
#include <iostream>
namespace wot::room {
struct LobbyProbe {
    static Value run(const Value& input,const Value& resources) {
        auto delivered=array();bool writable=true;
        Core core(input.get("config"),resources,[&](Core::Peer peer,std::shared_ptr<const Value> message,std::optional<std::string> key){
            if(!writable)return false;auto row=object();row.object["peer"]=integer(static_cast<std::int64_t>(peer));row.object["message"]=*message;row.object["key"]=key?text(*key):Value();delivered.array.push_back(std::move(row));return true;
        },[](Core::Peer,bool){},[](Core::Peer,std::int64_t){},[](const Value&)->std::optional<Value>{return {};});
        core.state_.phase="battle";core.state_.map_name="04_himmelsdorf";core.worker_ready_=true;
        Core::Connection worker;worker.sender={-1,true};core.peers_.emplace(0,std::move(worker));
        Core::Connection player;player.sender={1,false};core.peers_.emplace(42,std::move(player));
        auto outputs=array();Value previous;
        for(const auto& op:rows(input.get("operations"))) {
            delivered=array();const auto type=op.get("type").text;
            if(type=="patch") {
                if(has(op,"orders"))core.state_.bot_orders=op.get("orders");
                if(has(op,"manifest")){core.state_.bot_manifest=op.get("manifest");++core.state_.bot_manifest_revision;}
                if(has(op,"events"))for(const auto& event:rows(op.get("events")))core.state_.events.push_back(event);
                if(has(op,"tick"))core.state_.server_tick=intField(op,"tick");
                if(has(op,"phase"))core.state_.phase=op.get("phase").text;
                if(has(op,"roster"))core.roster_=op.get("roster");
                if(has(op,"barrier")){core.live_published_=op.get("barrier").truth();core.prebattle_us_=0;}
                if(has(op,"writable"))writable=op.get("writable").truth();
                if(has(op,"round"))core.state_.round_id=intField(op,"round");
                if(has(op,"result"))core.state_.battle_result=op.get("result");
                for(const auto& player:op.get("players").object) {
                    const auto id=std::stoll(player.first);auto& actor=core.state_.players[id];
                    if(actor.resolved().kind==Value::Kind::Null){actor=resources.get("public_player_defaults").resolved();actor.object["connected"]=boolean(true);actor.object["account_key"]=text("probe");actor.object["id"]=integer(id);}
                    for(const auto& entry:player.second.object)actor.object[entry.first]=entry.second;
                }
                for(const auto& connection:op.get("connections").object) {Core::Connection joined;joined.sender={connection.second.numeric()==-1?-1:static_cast<std::int64_t>(connection.second.numeric()),connection.second.numeric()==-1};core.peers_[static_cast<Core::Peer>(std::stoll(connection.first))]=std::move(joined);}
            } else if(type=="configure") {
                auto cold=testResources(op.get("resources"));
                if(op.get("root_shared").truth())cold=borrowedResourceTree(cold);
                core.configureRound(core.state_.round_id,core.state_.authority_epoch,std::move(cold),{});
            } else if(type=="advance")core.advance(op.get("dt").numeric(1./30.),intField(op,"clock_us"));
            else if(type=="publish")core.publishState();
            else if(type=="finish"){core.rules_.beginTick();core.rules_.finish(static_cast<int>(intField(op,"winner")),"battle_timeout");}
            else if(type=="command")core.receive(static_cast<Core::Peer>(intField(op,"peer",42)),op.get("message"));
            else if(type=="disconnect")core.disconnected(static_cast<Core::Peer>(intField(op,"peer")));
            else if(type=="stop")core.stop();
            auto row=object();row.object["deliveries"]=delivered;row.object["tick"]=integer(core.state_.server_tick);row.object["server_time_ms"]=integer(core.state_.server_time_ms);row.object["epoch"]=integer(core.state_.authority_epoch);
            row.object["snapshot"]=core.snapshot();row.object["pending"]=integer(core.peers_.count(42)?static_cast<std::int64_t>(core.peers_.at(42).pending.size()):0);
            row.object["phase"]=text(core.state_.phase);row.object["strategy"]=core.state_.resources.get("strategy");row.object["arena_bounds"]=core.state_.resources.get("arena_bounds");row.object["capture_bases"]=core.state_.resources.get("capture_bases");
            auto actors=object();for(const auto& actor:core.state_.players)actors.object[std::to_string(actor.first)]=actor.second;row.object["actors"]=std::move(actors);
            if(!previous.get("bot_manifest").reference)row.object["manifest_reused"]=boolean(false);else row.object["manifest_reused"]=boolean(previous.get("bot_manifest").reference==row.get("snapshot").get("bot_manifest").reference);
            if(!previous.get("bot_orders").reference)row.object["orders_reused"]=boolean(false);else row.object["orders_reused"]=boolean(previous.get("bot_orders").reference==row.get("snapshot").get("bot_orders").reference);
            previous=row.get("snapshot");outputs.array.push_back(std::move(row));
        }
        return outputs;
    }
};
}
int main(int argc,char**argv) {
    using namespace wot::room;if(argc!=2)return 2;std::ifstream source(argv[1]);const auto resources=testResources(parseJson(std::string((std::istreambuf_iterator<char>(source)),{})));std::string line;
    while(std::getline(std::cin,line))try {std::cout<<writeJson(LobbyProbe::run(parseJson(line),resources))<<'\n';}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
