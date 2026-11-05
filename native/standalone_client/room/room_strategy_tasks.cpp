#include "room_strategy_graph.hpp"

namespace wot::room {
using namespace strategy;
bool Planner::taskCombat(const Value& order) {
    const auto mode=s(order,"combat_mode");
    return oneOf(mode,{"low_health_retreat","low_health_defend","magazine_reload_retreat","magazine_reload_hold","under_fire_withdraw","under_fire_hold","crossfire_withdraw","crossfire_hold","withdraw"})||
        (!null(order.get("target_id"))&&oneOf(mode,{"engage","flank","advance_contact","take_cover","cover_hold","cover_peek","cover_return","support_hold"}));
}
const Value& Planner::route(const Value& bot) const {
    const auto& assigned=lookup(route_assignments_,id(bot)).get("route");return assigned.truth()?assigned:bot.get("route");
}
Value Planner::routeConfig(const Value& bot,const Value& selected,bool check_geometry) const {
    const auto& strategy=state_.resources.get("strategy");const auto name=s(selected,"id");
    const auto& resolved=strategy.get("route_configs").get(std::to_string(team(bot))).get(name);if(!null(resolved)) {
        if(check_geometry&&b(resolved,"default")&&has(selected,"waypoints")) {
            const auto& actual=rows(selected.get("waypoints"));const auto& expected=rows(resolved.get("points"));if(actual.size()!=expected.size())return {};
            for(std::size_t i=0;i<actual.size();++i){const auto& goal=rows(expected[i]);if(goal.size()<2||std::abs(n(actual[i],"x")-goal[0].numeric())>.000501||std::abs(n(actual[i],"z")-goal[1].numeric())>.000501)return {};}
        }
        return resolved;
    }
    const auto& settings=strategy.get("tactics").get("maps").get(state_.map_name);
    for(const auto& row:rows(settings.get("routes")))if("user_"+s(row,"id")==name)return row;
    for(const auto& row:rows(settings.get("default_routes"))) {
        const auto tag=s(row,"class_tag","all");const auto code=tag=="lightTank"?"lt":tag=="mediumTank"?"mt":tag=="heavyTank"?"ht":tag=="AT-SPG"?"td":"spg";
        const auto route_id=tag=="all"?s(row,"id"):"class_"+std::string(code)+"_"+s(row,"id");if(route_id!=name||team(row)!=team(bot))continue;
        const auto& actual=rows(selected.get("waypoints"));const auto& expected=rows(row.get("points"));if(check_geometry&&has(selected,"waypoints")) {
            if(actual.size()!=expected.size())return {};
            for(std::size_t i=0;i<actual.size();++i){const auto& goal=rows(expected[i]);if(goal.size()<2||std::abs(n(actual[i],"x")-goal[0].numeric())>.000501||std::abs(n(actual[i],"z")-goal[1].numeric())>.000501)return {};}
        }
        auto result=row.resolved();result.object["id"]=text(name);result.object["default"]=boolean(true);result.object["source_id"]=row.get("id");
        result.object["classes"]=tag=="all"?sequence({text("lightTank"),text("mediumTank"),text("heavyTank"),text("AT-SPG"),text("SPG")}):sequence({text(tag)});result.object["slots"]=array();result.object["policy"]=text("preferred");return result;
    }
    return {};
}
std::map<std::string,Value> Planner::routeCatalog(const std::vector<Value>& bots) const {
    std::map<std::string,Value> out;for(const auto& bot:bots)if(!rows(bot.get("route").get("waypoints")).empty())out[s(bot.get("route"),"id")]=bot.get("route");return out;
}
void Planner::dropParking(const Value& bot) {
    auto state=route_states_.find(id(bot));if(state!=route_states_.end())erase(state->second,{"parking_phase","parking_slot","holding","terminal_hold","terminal_crossing","completed_terminal"});
    for(auto i=wait_claims_.begin();i!=wait_claims_.end();)if(intField(i->second,"bot_id")==id(bot))i=wait_claims_.erase(i);else ++i;
}
void Planner::taskRejoin(const Value& bot) {
    const auto& selected=route(bot);const auto& graph=graph_;
    if(rows(selected.get("waypoints")).empty()||!graph)return;
    const auto pose=point(bot.get("state"));const auto index=Graph(graph).reachableJoin(pose,selected.get("waypoints"));if(!index)return;
    dropParking(bot);auto& state=entry(route_states_,id(bot));
    state.object["route_id"]=selected.get("id");state.object["index"]=integer(*index);state.object["join_index"]=integer(*index);state.object["join_anchor"]=pose;
    state.object.erase("arrival_previous_pose");
}
Value& Planner::taskBegin(const Value& bot,const std::string& kind,const Value& base,const Value& target_route,double now) {
    const auto selected=route(bot);const auto& source=rows(selected.get("waypoints"));const auto& state=lookup(route_states_,id(bot));
    const int index=std::max(0,std::min(int(intField(state,"index"))-1,int(source.size())-1));
    const auto& config=routeConfig(bot,selected,true);int junction=-1;
    if(config.get("defense").truth()) {
        const auto& nodes=rows(config.get("node_ids"));const auto identity=config.get("defense").get("junction");
        for(std::size_t i=0;i<rows(config.get("points")).size();++i)if((nodes.empty()?text("node_"+std::to_string(i)):nodes.at(i))==identity){junction=int(i);break;}
    }
    const int end=oneOf(kind,{"defense","guard"})&&index>=junction&&junction>=0?junction:0;auto path=array();
    if(!source.empty())for(int i=index;i>=end;--i)path.array.push_back(point(source[i]));
    if(oneOf(kind,{"defense","guard"})) {
        if(end==junction&&config.truth())for(std::size_t i=1;i<rows(config.get("defense").get("points")).size();++i)path.array.push_back(point(rows(config.get("defense").get("points"))[i]));
        path.array.push_back(point(base.get("point")));
    } else if(target_route.truth()&&!rows(target_route.get("waypoints")).empty())path.array.push_back(point(rows(target_route.get("waypoints"))[0]));
    if(path.array.empty())path.array.push_back(point(bot.get("state")));
    auto task=record({{"kind",text(kind)},{"route_id",text(s(selected,"id"))},{"route",selected},{"source_index",integer(index)},
        {"points",std::move(path)},{"index",integer(0)},{"since",number(now)},{"best",number(std::numeric_limits<double>::infinity())},
        {"target_route",target_route},{"until",number(now+180.)},{"base",base},{"root_index",integer(std::max(0,index-end))},
        {"last_source",integer(index)},{"original_parking",bot.get("spg_initial")}});
    travel_tasks_[id(bot)]=std::move(task);
    if(oneOf(kind,{"defense","guard"})){route_recoveries_.erase(id(bot));wreck_progress_.erase(id(bot));}
    dropParking(bot);return travel_tasks_.at(id(bot));
}
static void applyPosition(Value& order,const RoutePosition& position) {
    order.object["route_id"]=text(position.route);order.object["route_index"]=integer(position.index);
    order.object["move_position"]=position.move;order.object["route_anchor"]=position.anchor;order.object["route_join"]=boolean(position.join);
}
void Planner::taskPublish(Value& order,const Value& bot,double now) {
    const ID actor=id(bot);auto& task=travel_tasks_.at(actor);const auto kind=s(task,"kind");const auto base=task.get("base");
    const bool at=base.truth()&&insideBase(bot,base);if(at)task.object["arrived"]=boolean(true);
    if(kind=="pressure"&&now>=n(task,"until")) {
        const auto route_id=s(task,"route_id");const auto last=task.get("last_source");travel_tasks_.erase(actor);
        auto& state=entry(route_states_,actor);state.object["route_id"]=text(route_id);state.object["index"]=last;state.object["join_index"]=last;state.object["join_anchor"]=point(bot.get("state"));
        const auto position=routePosition(bot,now);order.object["route_id"]=text(route_id);order.object["route_index"]=lookup(route_states_,actor).get("index");order.object["move_position"]=position.move;
        order.object["combat_mode"]=text("route");order.object["throttle_override"]=Value();return;
    }
    if(b(task,"arrived")) {
        order.object["move_position"]=at?point(bot.get("state")):point(base.get("point"));order.object["throttle_override"]=at?number(0.):Value();
        order.object["combat_mode"]=text(at&&!null(order.get("target_id"))?"engage":kind=="defense"?"base_defense":"team_guard");return;
    }
    int index=int(intField(task,"index"));auto& points=task.object["points"].array;auto goal=points.at(index);const double d=distance(point(bot.get("state")),goal);
    const double best=has(task,"best")?task.get("best").number:std::numeric_limits<double>::infinity();
    if(d<best-.5){task.object["best"]=number(d);task.object["since"]=number(now);}
    const int previous_index=index;
    if(d<=6.) {
        task.object["failures"]=integer(0);if(index<=intField(task,"root_index"))task.object["last_source"]=integer(std::max<ID>(0,intField(task,"source_index")-index));
        ++index;task.object["best"]=number(std::numeric_limits<double>::infinity());task.object["since"]=number(now);
    } else if(now-n(task,"since")>=20.) {
        const auto failures=intField(task,"failures")+1;task.object["failures"]=integer(failures);task.object["best"]=number(std::numeric_limits<double>::infinity());task.object["since"]=number(now);
        if(oneOf(kind,{"defense","guard"})&&failures>=2){points={point(base.get("point"))};index=0;}
        else if(index<int(points.size())-1&&!(kind=="pressure"&&index>=intField(task,"root_index")))++index;
    }
    task.object["index"]=integer(index);
    if(index>=int(points.size())) {
        if(kind=="pressure") {
            const auto target_route=task.get("target_route");const auto route_id=s(target_route,"id");
            route_assignments_[actor]=record({{"route",target_route},{"until",number(0.)},{"ordered_task",boolean(true)}});
            route_states_[actor]=record({{"route_id",text(route_id)},{"index",integer(0)}});entry(route_history_,actor).object.erase(route_id);travel_tasks_.erase(actor);
            order.object["route_id"]=text(route_id);order.object["route_index"]=integer(0);order.object["move_position"]=routePosition(bot,now).move;
            order.object["combat_mode"]=text("route");order.object["throttle_override"]=Value();return;
        }
        index=int(points.size())-1;task.object["index"]=integer(index);
    }
    goal=points[index];if(index!=previous_index)task.object["best"]=number(distance(point(bot.get("state")),goal));
    order.object["move_position"]=goal;order.object["route_anchor"]=point(bot.get("state"));order.object["route_join"]=boolean(true);
    order.object["throttle_override"]=Value();order.object["parking_phase"]=Value();order.object["parking_slot"]=Value();
    order.object["combat_mode"]=text(kind=="defense"?"base_defense":kind=="guard"?"team_back_to_base":"route_transfer");
    if(kind=="defense")order.object["defense_base_id"]=text(s(base,"base_id"));
    if(kind=="pressure"){order.object["target_id"]=Value();order.object["fire_allowed"]=boolean(false);}
    order.object["travel_task"]=record({{"kind",text(kind)},{"index",integer(index)},{"route_id",task.get("route_id")}});
}
void Planner::taskApply(Value& order,const Value& bot,double now,const Value& defense,const Value& player_order,const Value& context) {
    if(emptyActive(bot))return;const ID actor=id(bot);auto player=player_order;bool combat=taskCombat(order);
    auto prior=previous_player_.find(actor);const auto command_id=player.get("command_id");
    if(prior!=previous_player_.end()&&prior->second.get("id")!=command_id) {
        previous_player_.erase(prior);
        if(!combat&&!route_recoveries_.count(actor)&&!travel_tasks_.count(actor)){taskRejoin(bot);if(!player.truth())applyPosition(order,routePosition(bot,now));}
    }
    if(player.truth()) {
        prior=previous_player_.find(actor);
        if(prior==previous_player_.end()||prior->second.get("id")!=command_id)
            previous_player_[actor]=record({{"id",command_id},{"command",player.get("command")},{"goal",order.get("move_position")}});
        const auto previous=previous_player_.at(actor);
        if(s(previous,"command")=="TURNBACK") {
            order.object["move_position"]=previous.get("goal");
            if(distance(point(bot.get("state")),point(previous.get("goal")))<=6.) {
                order.object["player_command_cancelled"]=boolean(true);previous_player_.erase(actor);taskRejoin(bot);applyPosition(order,routePosition(bot,now));
                order.object["combat_mode"]=text("route");order.object["throttle_override"]=Value();player=Value();
            }
        }
    }
    bool released=false;auto task=travel_tasks_.find(actor);combat=taskCombat(order);
    if(!null(defense)) {
        if(task==travel_tasks_.end()||s(task->second,"kind")!="defense")taskBegin(bot,"defense",defense,Value(),now);
    } else if(task!=travel_tasks_.end()&&s(task->second,"kind")=="defense") {travel_tasks_.erase(task);taskRejoin(bot);released=true;}
    task=travel_tasks_.find(actor);
    if(task==travel_tasks_.end()&&player.truth()&&s(player,"command")=="BACKTOBASE"&&!combat) {
        auto bases=defensePoints(context.get("capture_bases"),team(bot));
        if(!bases.empty()) {auto base=*std::min_element(bases.begin(),bases.end(),[&](const Value& a,const Value& c){return distance(point(bot.get("state")),a.get("point"))<distance(point(bot.get("state")),c.get("point"));});
            base.object["base_id"]=base.get("id");taskBegin(bot,"guard",base,Value(),now);}
    }
    task=travel_tasks_.find(actor);
    if(task!=travel_tasks_.end()&&s(task->second,"kind")=="guard"&&(!player.truth()||s(player,"command")!="BACKTOBASE"||combat)){travel_tasks_.erase(task);taskRejoin(bot);released=true;}
    task=travel_tasks_.find(actor);
    if(combat&&(task==travel_tasks_.end()||s(task->second,"kind")!="defense")) {
        previous_combat_.insert(actor);if(task!=travel_tasks_.end()&&!has(task->second,"paused_at"))task->second.object["paused_at"]=number(now);return;
    }
    if(previous_combat_.erase(actor)&&task==travel_tasks_.end()&&!route_recoveries_.count(actor)){taskRejoin(bot);if(!player.truth())applyPosition(order,routePosition(bot,now));}
    if(released&&task==travel_tasks_.end()&&!player.truth()&&oneOf(s(order,"combat_mode"),{"route","advance","base_screen"}))applyPosition(order,routePosition(bot,now));
    if(task!=travel_tasks_.end()) {
        if(has(task->second,"paused_at")){task->second.object["since"]=number(n(task->second,"since")+now-n(task->second,"paused_at"));task->second.object.erase("paused_at");}
        taskPublish(order,bot,now);
    }
}
void Planner::taskCaptureHandoff(Value& order,const Value& bot,const Value& target) {
    const ID actor=id(bot);
    if(null(target)||travel_tasks_.count(actor)||route_recoveries_.count(actor)||b(order,"team_command")||!null(order.get("target_id"))||s(bot.get("profile"),"class_tag")=="SPG"||!oneOf(s(order,"combat_mode"),{"route","advance","base_screen"}))return;
    const auto& selected=route(bot);const auto& state=lookup(route_states_,actor);
    if(null(routeConfig(bot,selected,true))||b(state,"holding")||oneOf(s(state,"parking_phase"),{"approach","waiting","queue"}))return;
    if(insideBase(bot,target)||intField(state,"index")==ID(rows(selected.get("waypoints")).size())-1)captureOrder(order,bot,target);
}
void Planner::emptyUpdate(std::vector<Value>& bots) {
    std::set<ID> live;for(const auto& bot:bots)live.insert(id(bot));
    for(auto i=empty_bots_.begin();i!=empty_bots_.end();)if(!live.count(*i))i=empty_bots_.erase(i);else ++i;
    for(auto& bot:bots) {
        const ID actor=id(bot);if(!empty_bots_.count(actor)&&emptyActive(bot)) {
            empty_bots_.insert(actor);const auto task=lookup(travel_tasks_,actor);travel_tasks_.erase(actor);const auto selected=task.get("route").truth()?task.get("route"):route(bot);
            if(selected.truth())route_assignments_[actor]=record({{"route",selected},{"until",number(0.)},{"ordered_task",boolean(true)}});
            dropParking(bot);
            for(Records* map:{&route_states_,&route_history_,&route_recoveries_,&wreck_progress_,&wreck_avoid_,&recovery_route_visits_,&previous_player_,&target_assignments_,&combat_states_,&retreat_states_,&engage_anchors_,&cover_states_,&firing_holds_,&artillery_anchors_,&capture_arrivals_})map->erase(actor);
            previous_combat_.erase(actor);for(auto& incident:base_defense_)incident.second.object["responders"].object.erase(std::to_string(actor));taskRejoin(bot);
        }
        bot.object["empty_ammo"]=boolean(empty_bots_.count(actor)!=0);
    }
}
void Planner::emptyOrder(Value& order,const Value& bot,const std::vector<Value>& contacts,const Value& target_area,double now) {
    const ID actor=id(bot);order.object["empty_ammo"]=boolean(true);order.object["player_command_cancelled"]=boolean(true);order.object["movement_priority"]=text("empty_ammo_capture");
    for(const auto& contact:contacts)if(b(contact,"visible")){target(order,contact);break;}
    order.object["combat_mode"]=text("route");order.object["throttle_override"]=Value();order.object["face_position"]=order.get("move_position");
    const auto& selected=route(bot);const auto& stage=lookup(route_states_,actor);
    if(!null(target_area)) {
        auto area=record({{"id",target_area.get("id")},{"point",target_area.get("point")},{"radius",target_area.get("radius")}});
        const auto& pose=bot.get("state");
        if(inside(pose,area)&&b(pose,"world_pose")&&!b(pose,"airborne")){capture_arrivals_[actor]=area;route_recoveries_.erase(actor);wreck_progress_.erase(actor);}
        const bool terminal=rows(selected.get("waypoints")).empty()||(intField(stage,"index")==ID(rows(selected.get("waypoints")).size())-1&&!route_recoveries_.count(actor));
        if(terminal||capture_arrivals_.count(actor)||s(bot.get("profile"),"class_tag")=="SPG") {
            order.object["combat_mode"]=text("base_capture");order.object["move_position"]=target_area.get("point");order.object["route_join"]=boolean(false);order.object["face_position"]=target_area.get("point");
        }
        if(capture_arrivals_.count(actor)){order.object["capture_area"]=area;order=constrainCapture(order,pose);}
    }
    ensure(order,"tactical_evidence").object["empty_ammo"]=record({{"active",boolean(true)},{"inventory",bot.get("state").get("ammo_remaining")},{"policy",text("travel_before_aim")},{"at",number(now)}});
}
}
