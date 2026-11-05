#include "room_strategy_data.hpp"

namespace wot::room {
using namespace strategy;
namespace {
void addIndex(Value& state,const char* key,int index) {
    auto& values=ensure(state,key,array());if(!contains(values,integer(index)))values.array.push_back(integer(index));
}
bool indexPresent(const Value& state,const char* key,int index) { return contains(state.get(key),integer(index)); }
Value waits(const Value& instruction) { return rows(instruction).size()>4?rows(instruction)[4]:array(); }
bool scriptedRoute(const Value& authored) {
    if(null(authored))return false;if(!b(authored,"default"))return true;
    for(const auto& p:rows(authored.get("points")))if(rows(p).size()>3)return true;return false;
}
}
bool Planner::routeMobile(const Value& bot) const {
    const auto& state=bot.get("state");if(!b(state,"world_pose"))return false;
    for(const char* field:{"engineHealth","leftTrackHealth","rightTrackHealth"})if(contains(state.get("critical").get("destroyed"),text(field)))return false;return true;
}
bool Planner::waitOccupied(ID actor,const Value& place,double radius,double height) const {
    const auto& coords=rows(place);if(coords.size()<2)return false;
    for(const auto& body:rows(wait_bodies_)) {
        if((s(body,"parking_kind")=="bot"&&id(body)==actor)||!b(body,"world_pose"))continue;
        const auto& shape=rows(body.get("collision_shape"));const double other=shape.size()>=2?std::hypot(shape[0].numeric(),shape[1].numeric()):n(body,"parking_radius",std::hypot(3.5,1.7));
        if(std::abs(n(body,"y")-height)<=std::max(3.,radius)&&std::hypot(coords[0].numeric()-n(body,"x"),coords[1].numeric()-n(body,"z"))<radius+other+.5)return true;
    }
    return false;
}
std::pair<bool,Value> Planner::waitParking(const Value& bot,Value& state,const std::string& route_id,int index,const Value& places_value,double now) {
    const auto actor=id(bot);const double radius=std::max(.5,n(bot.get("profile"),"parking_radius",std::hypot(3.5,1.7)));const auto& places=rows(places_value);
    if(indexPresent(state,"parking_completed",index))return {true,{}};
    auto claim=wait_claims_.end();for(auto i=wait_claims_.begin();i!=wait_claims_.end();++i)if(std::get<0>(i->first)==team(bot)&&std::get<1>(i->first)==route_id&&std::get<2>(i->first)==index&&intField(i->second,"bot_id")==actor){claim=i;break;}
    const auto& pose=bot.get("state");const auto key=std::to_string(index);const auto& arrived=state.get("parking_arrived").get(key);const auto& arrival=state.get("parking_pose").get(key);
    if(!null(arrived)&&!null(arrival)) {
        const double displaced=distance(pose,arrival);const bool safe=!b(pose,"airborne")&&displaced<=radius*2.&&std::abs(n(pose,"y")-n(arrival,"y"))<=1.+displaced*std::tan(27.5*3.14159265358979323846/180.);
        const double duration=state.get("parking_duration").get(key).numeric();
        if(duration>=0.&&now-arrived.numeric()>=duration) {
            addIndex(state,"parking_completed",index);erase(state,{"parking_phase","parking_slot"});if(claim!=wait_claims_.end())claim->second.object["departing"]=boolean(true);return {true,{}};
        }
        if(safe){state.object["holding"]=boolean(true);state.object["parking_phase"]=text("waiting");if(claim!=wait_claims_.end())claim->second.object["point"]=sequence({number(n(pose,"x")),number(n(pose,"z"))});return {false,point(pose)};}
    }
    if(claim!=wait_claims_.end()&&waitOccupied(actor,places.at(std::get<3>(claim->first)),radius,n(pose,"y"))){wait_claims_.erase(claim);claim=wait_claims_.end();}
    if(claim==wait_claims_.end())for(std::size_t slot=0;slot<places.size();++slot) {
        const auto candidate=std::make_tuple(team(bot),route_id,index,int(slot));const auto& place=rows(places[slot]);
        if(wait_claims_.count(candidate)||waitOccupied(actor,places[slot],radius,n(pose,"y")))continue;
        bool busy=false;for(const auto& c:wait_claims_)if(intField(c.second,"bot_id")!=actor) {
            const auto& p=rows(c.second.get("point"));if(p.size()>=2&&std::hypot(place[0].numeric()-p[0].numeric(),place[1].numeric()-p[1].numeric())<radius+n(c.second,"radius")+2.){busy=true;break;}
        }
        if(busy)continue;
        wait_claims_[candidate]=record({{"bot_id",integer(actor)},{"point",sequence({place[0],place[1]})},{"radius",number(radius)}});claim=wait_claims_.find(candidate);break;
    }
    if(claim==wait_claims_.end()) {
        addIndex(state,"parking_completed",index);addIndex(state,"parking_skipped",index);state.object["holding"]=boolean(false);erase(state,{"parking_phase","parking_slot"});return {true,{}};
    }
    const int slot=std::get<3>(claim->first);state.object["parking_slot"]=integer(slot);state.object["parking_phase"]=text("approach");const auto& place=rows(places[slot]);
    auto goal=record({{"x",place[0]},{"y",number(n(pose,"y"))},{"z",place[1]}});const double d=distance(goal,pose);
    if(b(pose,"airborne")||d>6.||(!null(arrival)&&std::abs(n(pose,"y")-n(arrival,"y"))>1.+d*std::tan(27.5*3.14159265358979323846/180.)))return {false,goal};
    auto& arrival_map=ensure(state,"parking_arrived");if(!has(arrival_map,key))arrival_map.object[key]=number(now);const double since=arrival_map.get(key).numeric();
    ensure(state,"parking_pose").object[key]=point(pose);auto& durations=ensure(state,"parking_duration");if(!has(durations,key))durations.object[key]=place.at(2);const double duration=durations.get(key).numeric();
    if(duration<0.||now-since<duration){state.object["holding"]=boolean(true);state.object["parking_phase"]=text("waiting");return {false,goal};}
    addIndex(state,"parking_completed",index);erase(state,{"parking_phase","parking_slot"});claim->second.object["departing"]=boolean(true);return {true,{}};
}
RoutePosition Planner::routePosition(const Value& bot,double now) {
    const ID actor=id(bot);auto task=travel_tasks_.find(actor);
    if(task!=travel_tasks_.end()) {
        const auto index=intField(task->second,"index");const auto& path=rows(task->second.get("points"));
        if(index<0||static_cast<std::uint64_t>(index)>=path.size())throw std::logic_error("Invalid ordered travel task cursor");
        return {s(task->second,"route_id"),int(intField(task->second,"source_index")),path.at(static_cast<std::size_t>(index)),point(bot.get("state")),true};
    }
    auto recovery=route_recoveries_.find(actor);
    if(recovery!=route_recoveries_.end()) {
        const auto& r=recovery->second;const int index=int(intField(r,"index"));auto& state=entry(route_states_,actor);state.object["route_id"]=r.get("route_id");state.object["index"]=integer(index);
        state.object["progress_receipt"]=record({{"at",number(now)},{"route_id",r.get("route_id")},{"selected_index",integer(index)},{"outcome",text("recovery_"+s(r,"phase"))}});
        return {s(r,"route_id"),index,point(rows(route(bot).get("waypoints")).at(index)),r.get("anchor"),true};
    }
    const auto& selected=route(bot);const auto& points=rows(selected.get("waypoints"));
    if(points.empty()) {
        static const char* names[]={"left_flank","center_line","right_flank"};const int index=int((intField(bot,"slot")+actor)%3);const double side=index==0?-1.:index==2?1.:0.;
        auto goal=record({{"x",number(side*115.)},{"y",number(0.)},{"z",number(team(bot)==1?18.:-18.)}});return {names[index],0,goal,goal,false};
    }
    const auto name=s(selected,"id","uploaded_route");const auto& authored=routeConfig(bot,selected,true);const bool scripted=!emptyActive(bot)&&scriptedRoute(authored);const int limit=int(points.size())-1;
    auto& history=entry(route_history_,actor);auto current=route_states_.find(actor);const auto pose=point(bot.get("state"));
    if(scripted&&(current==route_states_.end()||s(current->second,"route_id")!=name)) {
        const auto& saved=history.get(name);if(!null(saved)) {
            route_states_[actor]=record({{"index",saved.get("index")},{"route_id",text(name)},
                {"entry_receipt",record({{"sampled_at",number(now)},{"source",text("restored_scripted_index")},{"position",pose},{"selected_index",saved.get("index")},{"speed",bot.get("state").get("speed")},{"yaw",bot.get("state").get("yaw")}})},
                {"parking_completed",saved.get("parking_completed")},{"parking_skipped",saved.get("parking_skipped")},{"blocked_skip_to",saved.get("blocked_skip_to")},
                {"join_index",saved.get("index")},{"join_anchor",pose}});current=route_states_.find(actor);
        }
    }
    if(current==route_states_.end()||s(current->second,"route_id")!=name) {
        int nearest=0;for(int i=1;i<=limit;++i)if(distance(points[i],pose)<distance(points[nearest],pose))nearest=i;int index=nearest;auto skipped=array();
        auto entry_receipt=record({{"sampled_at",number(now)},{"source",text("nearest_route_point")},{"position",pose},{"nearest",integer(nearest)},{"speed",bot.get("state").get("speed")},{"yaw",bot.get("state").get("yaw")}});
        if(nearest==0&&limit>0){index=1;skipped.array.push_back(record({{"index",integer(0)},{"reason",text("spawn_flag")}}));}
        while(index<limit&&distance(points[index],pose)<=6.){skipped.array.push_back(record({{"index",integer(index)},{"reason",text("inside_arrival_disk")}}));++index;}
        if(n(bot.get("state"),"speed")>.5)while(index<limit) {
            const double bearing=std::atan2(n(points[index],"x")-n(pose,"x"),n(points[index],"z")-n(pose,"z"));const double angle=bearing-n(bot.get("state"),"yaw"),delta=std::atan2(std::sin(angle),std::cos(angle));
            if(std::abs(delta)<=1.75)break;skipped.array.push_back(record({{"index",integer(index)},{"reason",text("behind_moving_hull")},{"bearing",number(bearing)},{"delta",number(delta)}}));++index;
        }
        if(history.object.empty()&&scripted){index=0;entry_receipt.object["source"]=text("first_scripted_instruction");}
        if(!emptyActive(bot)&&!null(authored)&&history.object.empty()&&limit>0&&!rows(authored.get("points")).empty()&&rows(rows(authored.get("points"))[0]).size()>2&&!rows(rows(authored.get("points"))[0])[2].truth()) {index=1;entry_receipt.object["source"]=text("fixed_spawn_next_node");}
        entry_receipt.object["skipped"]=std::move(skipped);entry_receipt.object["selected_index"]=integer(index);entry_receipt.object["selected_point"]=point(points[index]);
        route_states_[actor]=record({{"index",integer(index)},{"route_id",text(name)},{"entry_receipt",std::move(entry_receipt)},{"join_index",integer(index)},{"join_anchor",pose}});current=route_states_.find(actor);
    }
    auto& state=current->second;const int previous_index=std::max(0,int(intField(state,"index")));int index=std::clamp(int(intField(state,"index")),0,limit);state.object["index"]=integer(index);
    const bool grounded=b(bot.get("state"),"world_pose")&&!b(bot.get("state"),"airborne");Value previous;
    if(grounded&&b(state,"arrival_previous_grounded")&&now-n(state,"arrival_previous_at",now-3.)>0.&&now-n(state,"arrival_previous_at",now-3.)<=2.)previous=state.get("arrival_previous_pose");
    state.object["arrival_previous_pose"]=pose;state.object["arrival_previous_at"]=number(now);state.object["arrival_previous_grounded"]=boolean(grounded);state.object["holding"]=boolean(false);erase(state,{"parking_phase","parking_slot"});
    auto goal=point(points[index]);
    while(true) {
        const auto& instructions=rows(authored.get("points"));const auto places=emptyActive(bot)||std::size_t(index)>=instructions.size()?array():waits(instructions[index]);
        if(!rows(places).empty()) {
            const auto result=waitParking(bot,state,name,index,places,now);if(!result.first){goal=result.second;break;}
            if(indexPresent(state,"parking_skipped",index)&&!diskReached(pose,points[index],index<limit?points[index+1]:Value(),previous))break;
        } else if(!diskReached(pose,points[index],index<limit?points[index+1]:Value(),previous))break;
        if(index>=limit) {
            if(grounded&&!b(bot.get("state"),"route_wreck_blocked")&&(!null(authored)||points.size()>1))state.object["terminal_crossing"]=record({{"point",point(points[index])},{"at",number(now)}});break;
        }
        ++index;state.object["index"]=integer(index);goal=point(points[index]);
    }
    if(intField(state,"blocked_skip_to",-1)!=index||s(state,"parking_phase")=="waiting")state.object.erase("blocked_skip_to");
    const bool join=intField(state,"join_index",-1)==index&&dict(state.get("join_anchor"));const auto anchor=join?point(state.get("join_anchor")):point(points[std::max(0,index-1)]);
    state.object["progress_receipt"]=record({{"at",number(now)},{"route_id",text(name)},{"entry",state.get("entry_receipt")},{"requested_limit",integer(limit)},{"effective_limit",integer(limit)},{"previous_index",integer(previous_index)},{"selected_index",integer(index)},{"point",goal},{"anchor",anchor},{"route_join",boolean(join)},{"outcome",text("normal_progress")}});
    history.object[name]=state;return {name,index,goal,anchor,join};
}
void Planner::authoredOrder(Value& order,const Value& bot,const Value& route_point,double now) {
    const auto actor=id(bot);if(emptyActive(bot)||route_recoveries_.count(actor)||travel_tasks_.count(actor)||b(order,"defense_base_id")||s(order,"combat_mode")=="base_capture")return;
    const auto& authored=routeConfig(bot,route(bot),true);if(null(authored))return;
    bool scripted=s(bot.get("profile"),"class_tag")=="SPG"&&(!b(authored,"default")||s(authored,"class_tag")=="SPG");for(const auto& p:rows(authored.get("points")))scripted=scripted||rows(p).size()>3;if(!scripted)return;
    auto& state=entry(route_states_,actor);const bool holding=b(state,"holding");const auto phase=s(state,"parking_phase");
    if(!holding&&!oneOf(phase,{"approach","waiting","queue"})&&s(bot.get("profile"),"class_tag")!="SPG")return;
    order.object["move_position"]=route_point;order.object["parking_phase"]=state.get("parking_phase");order.object["parking_slot"]=state.get("parking_slot");
    if(phase=="waiting") {
        const int index=int(intField(state,"index")),slot=int(intField(state,"parking_slot",-1));const auto& points=rows(authored.get("points"));const auto places=waits(points.at(index));
        if(slot>=0&&std::size_t(slot)<rows(places).size()&&!indexPresent(state,"parking_heading_done",index)) {
            const auto& place=rows(rows(places)[slot]);std::optional<double> heading;
            if(place.size()>3)heading=place[3].numeric();else if(std::size_t(index+1)<points.size()) {
                const auto& next=rows(points[index+1]);const double dx=next[0].numeric()-place[0].numeric(),dz=next[1].numeric()-place[1].numeric();if(dx*dx+dz*dz>.0001)heading=std::atan2(dx,dz)*180./3.14159265358979323846;
            }
            const double elapsed=now-state.get("parking_arrived").get(std::to_string(index)).numeric(now);
            if(!heading||!null(order.get("target_id"))||elapsed>=5.)addIndex(state,"parking_heading_done",index);
            else {
                const auto& pose=bot.get("state");const double angle=*heading*3.14159265358979323846/180.,delta=std::atan2(std::sin(angle-n(pose,"yaw")),std::cos(angle-n(pose,"yaw")));
                if(std::abs(delta)<.05)addIndex(state,"parking_heading_done",index);
                else {order.object["parking_heading"]=number(*heading);order.object["face_position"]=record({{"x",number(n(pose,"x")+std::sin(angle)*20.)},{"y",number(n(pose,"y"))},{"z",number(n(pose,"z")+std::cos(angle)*20.)}});}
            }
        }
    }
    order.object["arrival_radius"]=phase=="approach"?number(6.):Value();order.object["combat_mode"]=text(holding?"hold":phase=="approach"?"parking_approach":"route");order.object["throttle_override"]=holding?number(0.):Value();
}
void Planner::recordAssignment(const Value& bot,double now,const std::string& previous,const std::string& current,const std::string& reason,const Value& evidence) {
    const ID actor=id(bot);const auto old=lookup(assignment_receipts_,actor);const auto& lease=lookup(route_assignments_,actor);auto receipt=record({{"sequence",integer(intField(old,"sequence")+1)},{"at",number(now)},{"clock_basis",text("server_monotonic_seconds")},{"round",integer(state_.round_id)},{"bot",integer(actor)},{"previous_route",text(previous)},{"route",text(current)},{"reason",text(reason)},{"position",point(bot.get("state"))}});
    if(n(lease,"until")>0.)receipt.object["previous_lease"]=record({{"until",lease.get("until")},{"check",lease.get("_lease_check")},{"observation",lease.get("_lease_observation")}});
    const auto& points=rows(route(bot).get("waypoints"));const auto index=intField(lookup(route_states_,actor),"index",-1);const auto p=index>=0&&static_cast<std::uint64_t>(index)<points.size()?points[static_cast<std::size_t>(index)]:Value();
    receipt.object["previous_progress"]=record({{"index",integer(index)},{"point",p},{"distance",null(p)?Value():number(distance(p,receipt.get("position")))}});
    if(!null(evidence))receipt.object["selection"]=evidence;
    auto history=array();const auto& prior=rows(old.get("history"));for(std::size_t i=prior.size()>7?prior.size()-7:0;i<prior.size();++i)history.array.push_back(prior[i]);history.array.push_back(receipt);
    receipt.object["history_dropped"]=integer(std::max<ID>(0,intField(receipt,"sequence")-ID(history.array.size())));receipt.object["history"]=std::move(history);assignment_receipts_[actor]=std::move(receipt);
}
}
