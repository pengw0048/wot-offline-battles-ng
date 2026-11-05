#include "room_test_resources.hpp"
// Pure-data differential probe; no worker, engine, sockets or Python runtime.
#include "room_json.hpp"
#include "room_strategy_graph.hpp"
#include "room_strategy_ammo_policy.hpp"
#include <iostream>

namespace wot::room {
using namespace strategy;
struct StrategyProbe {
    static int shell(const Value& bot) { return Planner::selectedShell(bot,Value()); }
    static void ingest(Planner& planner,const Value& message,double now) {
        Value visibility=array();planner.reportContacts(message.get("contacts"),now,visibility);planner.reportAffordances(message.get("affordances"),now);
    }
    static Value memory(const Planner& planner) {
        auto result=array();for(const auto& side:planner.contacts_)for(const auto& row:side.second) {
            result.array.push_back(record({{"team",integer(side.first)},{"kind",text(row.first.first)},{"id",integer(row.first.second)},{"contact",row.second}}));
        }
        return result;
    }
    static Value policies(const Planner& planner) {auto result=object();for(const auto& row:planner.ammo_policies_)result.object[std::to_string(row.first)]=row.second->snapshot();return result;}
};
}

int main() {
    using namespace wot::room;using namespace strategy;
    std::string line;
    while(std::getline(std::cin,line))try {
        const auto request=parseJson(line);const auto op=s(request,"op");Value out;
        if(op=="cover")out=scoreCovers(request.get("candidates"),request.get("weights"));
        else if(op=="empty")out=boolean(emptyConfirmed(request.get("state"),request.get("profile")));
        else if(op=="capture")out=constrainCapture(borrowedResourceTree(request.get("order")),request.get("pose"));
        else if(op=="disk")out=boolean(diskReached(point(request.get("pose")),point(request.get("target")),null(request.get("following"))?Value():point(request.get("following")),null(request.get("previous"))?Value():point(request.get("previous"))));
        else if(op=="join") {const auto selected=Graph(request.get("graph")).reachableJoin(request.get("pose"),request.get("points"));out=selected?integer(*selected):Value();}
        else if(op=="personality")out=personality(intField(request,"id"));
        else if(op=="capability")out=boolean(capability(n(request,"rating"),s(request,"name"),rows(request.get("occasion"))));
        else if(op=="shell")out=integer(StrategyProbe::shell(request.get("bot")));
        else if(op=="plan") {
            State room;room.round_id=intField(request,"round_id",1);room.phase="battle";room.map_name=s(request,"map","01_karelia");room.bot_manifest=request.get("manifest");room.resources=testResources(request.get("resources"));room.rules=request.get("rules");
            Planner planner(room);planner.reset();if(!null(request.get("graph")))planner.setGraph(GraphData::fromValue(request.get("graph")));out=array();
            for(const auto& step:rows(request.get("steps"))) {
                room.monotonic_seconds=n(step,"now");if(has(step,"bots")){room.bots.clear();for(const auto& bot:rows(step.get("bots")))room.bots[id(bot)]=bot;}
                if(has(step,"players")){room.players.clear();for(const auto& player:rows(step.get("players")))room.players[id(player)]=player;}
                if(has(step,"rules"))room.rules=step.get("rules");
                if(has(step,"commands"))planner.setTeamCommands(step.get("commands"));
                if(has(step,"contacts")||has(step,"affordances"))StrategyProbe::ingest(planner,step,room.monotonic_seconds);
                for(const auto& damage:rows(step.get("damage")))planner.admitDamage(intField(damage,"victim"),s(damage,"kind"),intField(damage,"attacker"),int(intField(damage,"damage")),room.monotonic_seconds);
                planner.build(room.monotonic_seconds);out.array.push_back(record({{"revision",integer(room.bot_order_revision)},{"orders",room.bot_orders}}));
            }
        }
        else if(op=="observation") {
            State room;room.round_id=1;room.server_tick=300;room.phase="battle";room.monotonic_seconds=100.;room.bot_manifest=request.get("manifest");
            for(const auto& row:rows(request.get("players")))room.players[id(row)]=row;
            for(const auto& row:rows(request.get("bots")))room.bots[id(row)]=row;
            Planner planner(room);planner.reset();out=array();
            for(const auto& step:rows(request.get("messages"))) {
                room.pending_deliveries.clear();if(has(step,"now"))room.monotonic_seconds=n(step,"now");
                if(has(step,"tick"))room.server_tick=intField(step,"tick");
                for(const auto& change:step.get("players").resolved().object)for(const auto& field:change.second.resolved().object)room.players[std::stoll(change.first)].object[field.first]=field.second;
                for(const auto& change:step.get("bots").resolved().object)for(const auto& field:change.second.resolved().object)room.bots[std::stoll(change.first)].object[field.first]=field.second;
                const auto outcome=planner.dispatch(Sender{intField(step,"sender",-1),b(step,"worker",true)},step.get("message"));
                auto direct=[&](const auto& source){auto result=object();for(const auto& row:source){auto targets=array();for(const auto& target:row.second)targets.array.push_back(identityValue(target));result.object[std::to_string(row.first)]=targets;}return result;};
                auto deliveries=array();for(const auto& row:room.pending_deliveries)deliveries.array.push_back(sequence({integer(row.first),row.second}));
                out.array.push_back(record({{"accepted",boolean(outcome.status==Outcome::Status::accepted)},{"canonical",room.latest_observation},{"memory",StrategyProbe::memory(planner)},
                    {"bot_spotted",direct(room.bot_spotted)},{"player_spotted",direct(room.player_spotted)},{"deliveries",deliveries}}));
            }
        }
        else if(op=="ammo") {
            ShotPolicy policy(request.get("profile"));out=array();
            for(const auto& step:rows(request.get("operations"))) {
                const auto operation=s(step,"operation");Value selected;
                if(operation=="choose")selected=integer(policy.choose(step.get("state")));
                else if(operation=="context")policy.context(step.get("position"),step.get("target"));
                else if(operation=="launch")policy.launch(intField(step,"seq"),int(intField(step,"shell")),intField(step,"group"),int(intField(step,"count")),step.get("position"),step.get("target"));
                else if(operation=="resolve")policy.resolve(intField(step,"seq"),b(step,"hit"),n(step,"damage"),b(step,"penetrated"),b(step,"known",true));
                else throw std::invalid_argument("Unknown ammunition probe operation");
                auto snapshot=policy.snapshot();snapshot.object["selected"]=selected;out.array.push_back(std::move(snapshot));
            }
        }
        else if(op=="spg")out=canonicalSpgPlan(borrowedResourceTree(request.get("plan")),testResources(request.get("resources")),s(request,"map"),request.get("vehicle"),int(intField(request,"team")));
        else if(op=="ammo_room") {
            State room;room.bot_manifest=request.get("manifest");for(const auto& row:rows(request.get("bots")))room.bots[id(row)]=row;Planner planner(room);planner.reset();room.bot_orders=request.get("orders");out=array();
            for(const auto& step:rows(request.get("operations"))) {
                for(const auto& row:step.get("states").resolved().object)for(const auto& field:row.second.resolved().object)room.bots[std::stoll(row.first)].object[field.first]=field.second;
                if(has(step,"orders"))room.bot_orders=step.get("orders");
                if(s(step,"type")=="refresh")planner.refreshAmmoOrders();else{room.pending_ammo.array.push_back(step);planner.drainAmmo();}
                auto states=array();for(const auto& row:room.bots)states.array.push_back(row.second);
                out.array.push_back(record({{"bots",states},{"orders",room.bot_orders},{"policies",StrategyProbe::policies(planner)}}));
            }
        }
        else throw std::invalid_argument("Unknown strategy probe operation");
        std::cout<<writeJson(out)<<'\n';
    }catch(const std::exception& e){std::cout<<writeJson(record({{"error",text(e.what())}}))<<'\n';}
}
