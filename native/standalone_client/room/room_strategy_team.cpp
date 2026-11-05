#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
bool command(const std::string& code) {return oneOf(code,{"ATTACK","BACKTOBASE","HELPME","FOLLOWME","TURNBACK","HELPMEEX","STOP","ATTACKENEMY","SUPPORTMEWITHFIRE","ATTENTIONTOCELL","POSITIVE","NEGATIVE"});}
}
Records Planner::teamOrders(const std::vector<Value>& bots) const {
    Records live,out;for(const auto& bot:bots)live[id(bot)]=bot;auto commands=rows(team_commands_);
    std::stable_sort(commands.begin(),commands.end(),[](const Value& a,const Value& b){return intField(a,"issued_tick")<intField(b,"issued_tick");});
    for(const auto& raw:commands) {
        if(!dict(raw)||!command(s(raw,"command"))||intField(raw,"expires_tick")<=intField(raw,"issued_tick"))continue;
        for(const auto& v:rows(raw.get("recipient_bot_ids"))) {
            const ID actor=exactInteger(v).value_or(0);auto bot=live.find(actor);if(bot!=live.end()&&team(bot->second)==team(raw)&&!emptyActive(bot->second))out[actor]=raw;
        }
    }
    return out;
}
Value Planner::teamFocus(const Value& order,const Value& bot,double now) const {
    if(!oneOf(s(order,"command"),{"ATTACKENEMY","SUPPORTMEWITHFIRE"}))return {};auto kind=s(order,"target_kind");if(kind=="player")kind="human";const Identity key{kind,intField(order,"target_id")};
    for(const auto& contact:contactsFor(bot,now))if(identity(contact)==key&&b(contact,"visible")&&contains(contact.get("shootable_by_bot_ids"),integer(id(bot)))&&weaponCanRecover(bot))return contact;return {};
}
void Planner::applyTeamOrder(Value& order,const Value& bot,const Value& request,const Value& context,const Value& route_point,const Value& turnback_point) {
    const auto code=s(request,"command");if(!dict(request)||!command(code))return;
    order.object["team_command_id"]=text(s(request,"command_id"));order.object["team_command"]=text(code);if(has(request,"cell_index"))order.object["team_command_cell_index"]=integer(intField(request,"cell_index"));
    const bool navigation=oneOf(code,{"ATTACK","FOLLOWME","HELPME","HELPMEEX","STOP","TURNBACK","BACKTOBASE","ATTENTIONTOCELL"});
    if((oneOf(s(order,"combat_mode"),{"base_defense","low_health_retreat","low_health_defend","magazine_reload_retreat","magazine_reload_hold","under_fire_withdraw","under_fire_hold","crossfire_withdraw","crossfire_hold"})||b(order,"defense_base_id"))&&!navigation)return;
    if(code=="ATTACK") {
        const auto goal=dict(route_point)?route_point:order.get("move_position");order.object["combat_mode"]=text("team_attack");order.object["move_position"]=goal;if(null(order.get("target_id")))order.object["face_position"]=goal;order.object["throttle_override"]=Value();
    } else if(code=="STOP"){order.object["combat_mode"]=text("team_stop");order.object["move_position"]=point(bot.get("state"));order.object["throttle_override"]=number(0.);}
    else if(code=="TURNBACK") {
        const auto goal=dict(turnback_point)?turnback_point:order.get("route_anchor");order.object["combat_mode"]=text("team_turnback");order.object["move_position"]=goal;order.object["face_position"]=goal;order.object["throttle_override"]=Value();
    } else if(code=="BACKTOBASE") {
        const auto bases=defensePoints(context.get("capture_bases"),team(bot));if(!bases.empty()) {
            const auto base=*std::min_element(bases.begin(),bases.end(),[&](const Value& a,const Value& b){return distance(a.get("point"),bot.get("state"))<distance(b.get("point"),bot.get("state"));});order.object["combat_mode"]=text("team_back_to_base");order.object["move_position"]=base.get("point");order.object["face_position"]=base.get("point");order.object["throttle_override"]=Value();
        }
    } else if(code=="ATTENTIONTOCELL") {
        const auto& bounds=rows(context.get("arena_bounds"));const auto cell=exactInteger(request.get("cell_index"));
        if(bounds.size()==4&&cell&&*cell>=0&&*cell<=99&&bounds[0].numeric()<bounds[2].numeric()&&bounds[1].numeric()<bounds[3].numeric()) {
            const int column=int(*cell/10),row=int(*cell%10);const double width=(bounds[2].numeric()-bounds[0].numeric())/10.,height=(bounds[3].numeric()-bounds[1].numeric())/10.,minx=bounds[0].numeric()+column*width,maxx=minx+width,maxz=bounds[3].numeric()-row*height,minz=maxz-height;
            const auto goal=record({{"x",number(rounded((minx+maxx)*.5))},{"y",number(rounded(n(bot.get("state"),"y")))},{"z",number(rounded((minz+maxz)*.5))}});
            order.object["combat_mode"]=text("team_attention_cell");order.object["move_position"]=goal;order.object["move_area_bounds"]=sequence({number(rounded(minx)),number(rounded(minz)),number(rounded(maxx)),number(rounded(maxz))});order.object["face_position"]=goal;order.object["throttle_override"]=Value();
        }
    } else if(oneOf(code,{"HELPME","HELPMEEX","FOLLOWME"})) {
        const auto issuer=state_.players.find(intField(request,"issuer_id"));if(issuer!=state_.players.end()&&team(issuer->second)==team(bot)&&b(issuer->second,"alive",true)&&b(issuer->second,"world_pose")) {
            auto goal=point(issuer->second);const auto& recipients=rows(request.get("recipient_bot_ids"));int slot=0;for(std::size_t i=0;i<recipients.size();++i)if(recipients[i]==integer(id(bot))){slot=int(i);break;}
            const double lateral=(slot-(std::max<std::size_t>(1,recipients.size())-1)*.5)*12.,yaw=n(issuer->second,"yaw");goal.object["x"]=number(n(goal,"x")-std::sin(yaw)*18.+std::cos(yaw)*lateral);goal.object["z"]=number(n(goal,"z")-std::cos(yaw)*18.-std::sin(yaw)*lateral);
            order.object["combat_mode"]=text("team_follow");order.object["move_position"]=goal;order.object["face_position"]=goal;order.object["throttle_override"]=Value();
        }
    }
}
Value Planner::teamAxis(int side,const std::vector<Value>& bots) {
    std::map<int,std::vector<Value>> starts;std::vector<Value> ends;
    for(const auto& bot:bots) {
        const auto& points=rows(bot.get("route").get("waypoints"));if(points.size()<2||(team(bot)!=1&&team(bot)!=2))continue;starts[team(bot)].push_back(point(points.front()));if(team(bot)==side)ends.push_back(point(points.back()));
    }
    if(starts[side].empty()||ends.empty())return {};
    auto average=[](const std::vector<Value>& values){auto out=object();for(const char* axis:{"x","y","z"}){double sum=0.;for(const auto& v:values)sum+=n(v,axis);out.object[axis]=number(rounded(sum/values.size()));}return out;};
    const auto own=average(starts[side]);auto enemy_ends=starts[3-side];if(enemy_ends.empty()){double farthest=0.;for(const auto& p:ends)farthest=std::max(farthest,distance(p,own));const double threshold=std::max(1.,farthest*.02);for(const auto& p:ends)if(farthest-distance(p,own)<=threshold)enemy_ends.push_back(p);}
    const auto enemy=average(enemy_ends);return distance(enemy,own)<1.?Value():sequence({own,enemy});
}
}
