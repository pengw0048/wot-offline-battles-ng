#include "stock_native_bot_decision.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_decision {
namespace {
constexpr double pi=3.14159265358979323846;
double distance(const Point&a,const Point&b){return std::hypot(a[0]-b[0],a[2]-b[2]);}
double delta(double a,double b){double x=a-b;while(x>pi)x-=2*pi;while(x<-pi)x+=2*pi;return x;}
bool mode(const std::string&value,std::initializer_list<const char*>choices){for(auto*c:choices)if(value==c)return true;return false;}
const Target* target(const plan::Optional<std::int64_t>&id,const Targets&values){if(!id.present)return nullptr;auto i=values.find(id.value);return i==values.end()?nullptr:&i->second;}
void move(StrategicOrder&out,const Point&p,const std::string&mode_name){out.drive.move.set(p);out.drive.mode=mode_name;out.navigation.mode=mode_name;}
void throttle(StrategicOrder&out,std::optional<double>value){if(value){out.drive.throttle.set(*value);out.navigation.throttle=value;}else{out.drive.throttle.clear();out.navigation.throttle.reset();}}
void clearTarget(StrategicOrder&out,const Point&p){out.drive.target.clear();out.drive.target_kind.clear();out.navigation.target.reset();out.drive.fire_allowed=false;out.drive.aim.set(p);out.drive.face.set(p);}
void routeAnchor(StrategicOrder&out,const Point&p){out.navigation.route_anchor=p;out.navigation.route_join=false;}
std::uint64_t positive(std::int64_t x){return x<0?std::uint64_t(-(x+1))+1:std::uint64_t(x);}
}
Decisions::Decisions(plan::Planner&p,plan::Traffic&t,strategy::Director&d,parking::Parking&parking):planner_(p),traffic_(t),director_(d),parking_(parking){}
void Decisions::install(const stock_native_bot_planning::PlanningSnapshot&snapshot)
{
    planner_.restore(snapshot.planner);traffic_.restore(snapshot.traffic);director_=snapshot.director;parking_.catalogs=snapshot.parking;orders=snapshot.orders;tokens=snapshot.order_tokens;routes=snapshot.routes;cache.clear();actors.clear();angle_candidate_tick_.reset();
    for(const auto&pair:snapshot.actors){Actor actor;auto local=director_.agents.find(pair.first);if(local!=director_.agents.end()){actor.class_tag=local->second.profile.class_tag;actor.route=local->second.waypoints;actor.route_id=local->second.route_id;}
        auto initial=snapshot.initial_parking.find(pair.first);if(initial!=snapshot.initial_parking.end())actor.initial=initial->second;auto origin=snapshot.parking_origins.find(pair.first);if(origin!=snapshot.parking_origins.end())actor.selection_origin=origin->second;actors[pair.first]=std::move(actor);}
}
bool Decisions::valid(std::int64_t id,const Cache&c)const
{auto order=orders.find(id);if(c.server!=(order!=orders.end()))return false;auto token=tokens.find(id);return !c.server||c.token==(token==tokens.end()?0:token->second);}
bool Decisions::due(const Input&in)const
{auto found=cache.find(in.planning.body.id);return in.refresh_control&&(found==cache.end()||!valid(in.planning.body.id,found->second)||in.evaluation_time.value_or(in.planning.now)>=found->second.deadline);}
void Decisions::invalidate(std::int64_t id){cache.erase(id);}
bool Decisions::clearReposition(std::int64_t id){auto&actor=actors.at(id);if(!actor.reposition)return false;actor.reposition.reset();invalidate(id);return true;}
StrategicOrder Decisions::overlay(StrategicOrder out,const Target*t,const Point&origin)
{
    auto&c=out.drive;if(!c.target.present)return out;if(!t||!t->alive){c.fire_allowed=false;return out;}c.aim.set(t->position);
    if(!c.stable_hull_face){if(!c.hull_angle_degrees.present)c.face=c.aim;else{const double a=c.hull_angle_degrees.value;if(!std::isfinite(a)||std::abs(a)>45.)throw std::invalid_argument("Native hull angle origin is invalid");const double angle=a*pi/180.,dx=t->position[0]-origin[0],dz=t->position[2]-origin[2];c.face.set({origin[0]+dx*std::cos(angle)-dz*std::sin(angle),origin[1],origin[2]+dx*std::sin(angle)+dz*std::cos(angle)});}}
    if(c.mode=="advance_contact")c.move=c.aim;return out;
}
void Decisions::overlay(plan::Command&c,const Target*t,const Point&p,bool stable)
{StrategicOrder o;o.drive.target=c.target;o.drive.aim.set(c.aim);o.drive.face.set(c.face);o.drive.move.set(c.move);o.drive.fire_allowed=c.fire_allowed;o.drive.mode=c.mode;o.drive.hull_angle_degrees=c.hull_angle_degrees;o.drive.stable_hull_face=stable||c.stable_hull_face;
    o=overlay(o,t,p);c.aim=o.drive.aim.value;c.face=o.drive.face.value;c.move=o.drive.move.value;c.fire_allowed=o.drive.fire_allowed;}
StrategicOrder Decisions::routeOrder(const Input&in,const StrategicOrder&order)
{
    const auto&a=actors.at(in.planning.body.id);const auto*points=&a.route;if(a.route_id!=order.navigation.route_id){auto team=routes.find(in.planning.body.team);if(team==routes.end())return order;auto route=team->second.find(order.navigation.route_id);if(route==team->second.end())return order;points=&route->second;}
    const int i=order.navigation.route_index.value_or(0);if(i<0||i>=int(points->size()))return order;StrategicOrder out=order;out.drive.target.clear();out.drive.target_kind.clear();out.navigation.target.reset();out.drive.aim.clear();out.drive.face.clear();out.drive.fire_allowed=false;
    move(out,{(*points)[i][0],in.planning.body.position[1],(*points)[i][1]},"route");throttle(out,{});return out;
}
StrategicOrder Decisions::rearOrder(const Input&in,const StrategicOrder&order,const Targets&targets)
{
    auto&a=actors.at(in.planning.body.id);const auto&p=in.planning.body.position;const double now=in.planning.now;bool blocked=false;
    if(a.rear_lease){auto&lease=*a.rear_lease;const Point goal=order.drive.move.present?order.drive.move.value:p;const bool complete=distance(p,lease.origin)>=2.*in.planning.body.half_length+1.5||distance(p,goal)<=1.5||!order.drive.parking_phase.empty()||mode(order.drive.mode,{"base_defense","support_hold","cover_hold","hold","artillery_deploy","artillery_relocate"});
        if(complete||(lease.retry_until&&now>=*lease.retry_until))a.rear_lease.reset();else if(lease.retry_until||now-lease.start>=8.){if(!lease.retry_until)lease.retry_until=now+std::max(8.,in.reload_time);blocked=true;}}
    a.rear_motion_blocked=blocked;if(!a.rear_episode)return order;const auto episode=*a.rear_episode;auto t=targets.find(episode.target);
    const bool changed=!order.drive.target.present||order.drive.target.value!=episode.target||t==targets.end()||!t->second.alive||!order.drive.parking_phase.empty()||mode(order.drive.mode,{"base_defense","artillery_deploy","artillery_relocate"});
    const bool fired=in.fire_sequence!=episode.fire_sequence,expired=blocked||now-episode.start>=8.;bool rotation=false;
    for(const auto&pair:a.blocked_rotations){const auto&f=pair.second;if(now<f.until&&distance(p,f.origin)<=.08&&std::abs(delta(in.planning.body.yaw,f.yaw))<=.02){rotation=true;break;}}
    if(changed||fired||expired||rotation){a.rear_episode.reset();a.rear_hull_aim.reset();if(fired||expired||rotation)a.rear_retry=std::make_pair(episode.identity,now+std::max(8.,in.reload_time));return order;}
    auto out=order;move(out,p,"engage");out.drive.face.set(t->second.position);throttle(out,{});return out;
}
StrategicOrder Decisions::angleOrder(const Input&in,const StrategicOrder&order,const Targets&targets,World&w)
{
    auto&a=actors.at(in.planning.body.id);const auto&p=in.planning.body.position;const double now=in.planning.now;
    if(a.class_tag=="SPG"||!order.drive.parking_phase.empty()||!mode(order.drive.mode,{"engage","support_hold","cover_hold","hold"})){a.gun_adjustment.reset();if(!order.drive.parking_phase.empty())a.gun_rejected.clear();return order;}
    const auto*t=target(order.drive.target,targets);for(auto i=a.gun_rejected.begin();i!=a.gun_rejected.end();)if(now>=i->second)i=a.gun_rejected.erase(i);else ++i;
    if(t&&a.gun_rejected.count(t->identity))return routeOrder(in,order);if(!t||!a.gun_refusal||*a.gun_refusal!=t->identity){a.gun_adjustment.reset();return order;}
    const Point goal=order.drive.move.present?order.drive.move.value:p;if(!a.gun_adjustment&&(std::abs(in.planning.speed)>1.||distance(p,goal)>1.5))return order;
    if(!a.gun_adjustment)a.gun_adjustment=AngleAdjustment{now,{},false,false};auto&marker=*a.gun_adjustment;const double elapsed=now-marker.start;
    auto out=order;out.drive.move.set(p);out.drive.face.set(t->position);out.drive.fire_allowed=false;out.drive.arrival_radius.set(1.);throttle(out,{});if(elapsed<2.)return out;
    if(elapsed<6.&&!marker.selected&&(!angle_candidate_tick_||*angle_candidate_tick_!=now)){angle_candidate_tick_=now;marker.selected=true;const double yaw=in.planning.body.yaw;
        for(const double sample:{yaw,yaw+pi}){plan::Optional<double> maximum;maximum.set(8.);if(w.direction(in.planning.body.id,sample,maximum,1.,{})){marker.destination=Point{p[0]+std::sin(sample)*8.,p[1],p[2]+std::cos(sample)*8.};marker.reverse=sample!=yaw;break;}}}
    if(elapsed<6.&&marker.destination){move(out,*marker.destination,marker.reverse?"withdraw":"gun_angle_adjust");throttle(out,1.);return out;}
    if(elapsed<6.&&!marker.selected)return out;if(a.gun_rejected.size()>=8){auto old=std::min_element(a.gun_rejected.begin(),a.gun_rejected.end(),[](const auto&x,const auto&y){return x.second<y.second;});a.gun_rejected.erase(old);}a.gun_rejected[t->identity]=now+8.;a.gun_adjustment.reset();return routeOrder(in,order);
}
std::pair<std::optional<StrategicOrder>,bool> Decisions::repositionOrder(const Input&in,const Targets&targets)
{
    const auto id=in.planning.body.id;auto server=orders.find(id);if(server!=orders.end()&&mode(server->second.drive.parking_phase,{"waiting","queue"})){clearReposition(id);return {{},false};}
    auto&a=actors.at(id);if(!a.reposition)return {{},false};const auto marker=*a.reposition;if(in.planning.now>=marker.deadline){clearReposition(id);return {{},true};}
    auto target=targets.find(marker.target);if(target==targets.end()||!target->second.alive||target->second.health<=0.||distance(in.planning.body.position,marker.destination)<=1.5){clearReposition(id);return {{},false};}
    StrategicOrder out;out.drive.target.set(marker.target);out.navigation.target=marker.target;out.drive.aim.set(target->second.position);out.drive.face=out.drive.aim;move(out,marker.destination,"friendly_lane_reposition");throttle(out,1.);out.drive.fire_range=marker.fire_range;out.drive.shell_index=marker.shell;return {out,false};
}
parking::Plan Decisions::retryDeployment(const Input&in,parking::Plan initial,double remaining,const std::vector<parking::Occupied>&occupied,bool obstructed,World&w)
{
    auto&a=actors.at(in.planning.body.id);const double now=in.planning.now;if(!obstructed&&(!a.deployment||a.deployment->plan!=initial)){a.deployment=DeploymentProgress{initial,remaining,now};return initial;}
    if(!obstructed){auto&progress=*a.deployment;if(remaining<progress.best-.25){progress.best=remaining;progress.since=now;}if(now-progress.since<20.)return initial;progress.since=now;if(a.parking_retries>=3){a.position_event="parking_retry_exhausted";return initial;}++a.parking_retries;
        if(std::find(a.failed_parking.begin(),a.failed_parking.end(),initial.point)==a.failed_parking.end())a.failed_parking.push_back(initial.point);}
    auto excluded=a.failed_parking;for(const auto&p:a.fire_failed_parking)if(p.second>now)excluded.push_back(p.first);Point origin=in.planning.body.position;
    if(a.selection_origin&&!parking_.closest(origin)&&parking_.closest(*a.selection_origin))origin=*a.selection_origin;
    auto replacement=parking_.select(in.planning.body.id,in.planning.body.team,origin,excluded,occupied,initial.source=="launcher_manual_v1"?std::optional<std::string>(initial.zone):std::nullopt);
    if(!replacement){a.position_event="no_safe_alternate_parking";return initial;}a.initial=replacement;a.arrived.reset();a.deployment.reset();a.position_event="blocked_deployment_alternate_parking";invalidate(in.planning.body.id);w.cancelArtillery(in.planning.body.id);return *replacement;
}
parking::Plan Decisions::retryFire(const Input&in,parking::Plan initial,const std::vector<parking::Occupied>&occupied,World&w)
{
    auto&a=actors.at(in.planning.body.id);const double now=in.planning.now;const auto&p=in.planning.body.position;
    if(!a.fire_failure)return initial;const auto&e=*a.fire_failure;if(e.plan!=initial||e.fire_sequence!=in.fire_sequence||in.airborne||in.overturned||distance(e.origin,p)>2.||e.elapsed<30.||e.proofs.size()<2||now-e.last_failure>2.||now<a.fire_retry_at)return initial;
    a.fire_retry_at=now+45.;a.fire_failed_parking.erase(std::remove_if(a.fire_failed_parking.begin(),a.fire_failed_parking.end(),[&](const auto&v){return v.second<=now;}),a.fire_failed_parking.end());a.fire_failed_parking.push_back({p,now+120.});if(a.fire_failed_parking.size()>8)a.fire_failed_parking.erase(a.fire_failed_parking.begin(),a.fire_failed_parking.end()-8);
    std::vector<Point> excluded;for(const auto&v:a.fire_failed_parking)excluded.push_back(v.first);excluded.insert(excluded.end(),a.failed_parking.begin(),a.failed_parking.end());auto replacement=parking_.select(in.planning.body.id,in.planning.body.team,p,excluded,occupied,initial.zone);
    if(!replacement){a.position_event="fire_position_no_safe_alternate";return initial;}a.initial=replacement;a.fire_relocating=true;a.arrived.reset();a.displaced_safe=false;a.deployment.reset();a.fire_failure.reset();a.position_event=replacement->zone==initial.zone?"fire_position_same_zone":"fire_position_other_zone";invalidate(in.planning.body.id);w.cancelArtillery(in.planning.body.id);return *replacement;
}
StrategicOrder Decisions::artilleryOrder(const Input&in,const StrategicOrder&order,const Targets&targets,const std::vector<parking::Occupied>&occupied,World&w)
{
    auto&a=actors.at(in.planning.body.id);if(a.class_tag!="SPG")return order;const auto&p=in.planning.body.position;const double now=in.planning.now;const bool deployment=mode(order.drive.mode,{"artillery_hold","artillery_deploy"});if(!deployment)a.deployment.reset();
    if(a.initial&&deployment){auto initial=*a.initial;if(order.drive.fire_allowed&&target(order.drive.target,targets))initial=retryFire(in,initial,occupied,w);a.rear_position.reset();Point destination=initial.point;
        if(now>=a.parking_check_at){a.parking_check_at=now+1.;if(a.arrived&&*a.arrived==initial){a.displaced_safe=parking_.displaced(in.planning.body.id,initial,p,in.airborne,in.overturned);if(!a.displaced_safe&&distance(p,destination)>initial.radius)a.arrived.reset();}
            if(!a.arrived||*a.arrived!=initial){a.parking_occupied=!parking::available(destination,initial.clearance,in.planning.body.team,{},occupied);if(a.parking_occupied&&now>=a.occupied_retry_at){a.occupied_retry_at=now+5.;initial=retryDeployment(in,initial,distance(p,destination),occupied,true,w);destination=initial.point;a.parking_occupied=!parking::available(destination,initial.clearance,in.planning.body.team,{},occupied);}}else a.parking_occupied=false;}
        bool arrived=distance(p,destination)<=initial.radius;if(a.arrived&&*a.arrived==initial&&a.displaced_safe&&!in.airborne&&!in.overturned){destination=p;arrived=true;}
        if(arrived&&!a.parking_occupied){a.arrived=initial;a.parking_occupied=false;}auto out=order;
        if(a.parking_occupied){move(out,p,"artillery_hold");routeAnchor(out,p);throttle(out,0.);a.position_event="occupied_parking_queue";return out;}
        if(arrived)a.deployment.reset();else{initial=retryDeployment(in,initial,distance(p,destination),occupied,false,w);destination=initial.point;arrived=distance(p,destination)<=initial.radius;}
        move(out,destination,arrived?"artillery_hold":"artillery_deploy");routeAnchor(out,destination);throttle(out,arrived?std::optional<double>(0.):std::nullopt);
        if(a.obstruction&&arrived)a.position_event="library_position_fire_obstructed";if(a.fire_relocating){if(arrived)a.fire_relocating=false;else clearTarget(out,destination);}return out;}
    if(!deployment){a.rear_position.reset();return order;}const Point anchor=order.drive.move.present?order.drive.move.value:p;if(a.rear_position&&a.rear_position->anchor!=anchor)a.rear_position.reset();const auto*t=target(order.drive.target,targets);
    if(order.drive.mode=="artillery_hold"&&t&&a.obstruction&&now-a.obstruction->since>=3.&&now-a.obstruction->last<=2.&&std::abs(in.planning.speed)<.2&&distance(a.obstruction->origin,p)<=.25&&std::abs(delta(a.obstruction->yaw,in.planning.body.yaw))<=.05){
        auto visited=a.rear_position?a.rear_position->visited:std::vector<Point>{anchor};std::optional<std::tuple<double,int,Point>> choice;
        if(visited.size()<=3&&a.route.size()>1){const auto first=a.route.front(),last=a.route.back();const double dx=last[0]-first[0],dz=last[1]-first[1],length=std::max(1.,dx*dx+dz*dz);
            for(std::size_t i=0;i<a.route.size();++i){const Point candidate{a.route[i][0],p[1],a.route[i][1]};const double d=distance(p,candidate),progress=((candidate[0]-first[0])*dx+(candidate[2]-first[1])*dz)/length;bool seen=false;for(const auto&old:visited)if((candidate[0]-old[0])*(candidate[0]-old[0])+(candidate[2]-old[2])*(candidate[2]-old[2])<225.){seen=true;break;}
                if(d<16.||d>80.||progress<-.12||progress>.30||seen)continue;const auto value=std::make_tuple(d,int(i),candidate);if(!choice||value<*choice)choice=value;}}
        if(choice){const auto candidate=std::get<2>(*choice);visited.push_back(candidate);a.rear_position=RearPosition{anchor,candidate,std::get<1>(*choice),visited,now};a.position_event="adopted_rear_waypoint";w.cancelArtillery(in.planning.body.id);}else a.position_event="no_safe_rear_waypoint";a.obstruction.reset();}
    if(!a.rear_position)return order;auto out=order;const auto&selected=*a.rear_position;const bool arrived=distance(p,selected.point)<=15.;move(out,selected.point,arrived?"artillery_hold":"artillery_relocate");routeAnchor(out,selected.point);out.navigation.route_index=selected.index;throttle(out,arrived?std::optional<double>(0.):std::nullopt);if(!arrived)clearTarget(out,selected.point);return out;
}
StrategicOrder Decisions::adaptOrder(const Input&in,StrategicOrder selected,const Targets&targets,const std::vector<parking::Occupied>&occupied,World&w)
{const auto*t=target(selected.drive.target,targets);selected=overlay(std::move(selected),t,in.planning.body.position);selected=rearOrder(in,selected,targets);selected=artilleryOrder(in,selected,targets,occupied,w);return angleOrder(in,selected,targets,w);}
Output Decisions::prepare(const Input&in,const std::vector<Target>&contacts,const Targets&targets,const std::vector<plan::Body>&neighbours,const std::vector<parking::Occupied>&occupied,World&w)
{
    const auto id=in.planning.body.id;auto found=cache.find(id);std::optional<Cache> previous;if(found!=cache.end())previous=found->second;Output out;out.due=due(in);
    if(previous&&valid(id,*previous)&&!out.due){out.command=previous->command;out.contacts=previous->contacts;out.targets=previous->targets;out.reused=true;}
    else if(!in.refresh_control){auto&c=out.command;c.bot_id=id;c.aim=c.face=c.move=in.planning.body.position;c.mode=c.recovery="physical_hold";c.shell_index=in.shell_index;c.target_yaw=in.planning.body.yaw;c.brake=true;out.physical_hold=true;}
    else{
        out.contacts=contacts;out.targets=targets;auto server=orders.find(id);plan::Input planning=in.planning;planning.dt=previous?std::max(in.step,in.planning.now-previous->at):in.step;
        const double factors[]={1.,2.,4.};const double interval=.15*(in.fixed_control&&!previous?1.:factors[std::max(0,std::min(2,in.detail_tier))]);planning.decision_horizon=interval;
        auto reposition=repositionOrder(in,targets);StrategicOrder selected;
        if(reposition.first)selected=*reposition.first;
        else if(server!=orders.end()){selected=server->second;if(selected.drive.target_kind=="human"&&selected.drive.target.present){selected.drive.target.value+=1000000;selected.navigation.target=selected.drive.target.value;}
            selected=adaptOrder(in,std::move(selected),targets,occupied,w);
        }else{for(const auto&contact:contacts)director_.observe(planning.body.team,contact);auto local=director_.order(id,planning.body.position,planning.body.yaw,planning.speed,in.health,in.maximum_health,in.planning.now);selected.drive=local;selected.navigation.mode=local.mode;selected.navigation.route_id=local.route_id;selected.navigation.route_index=local.route_index;selected.navigation.route_join=local.route_join;selected.navigation.route_anchor=local.route_anchor;if(local.throttle.present)selected.navigation.throttle=local.throttle.value;if(local.target.present)selected.navigation.target=local.target.value;}
        w.useOrder(id,selected.navigation);out.command=planner_.decide(planning,selected.drive,neighbours,w);if(out.command.navigation_replan){w.replan(id,planning.body.position,planning.now);out.command.navigation_replan=false;}w.blockedPlanner(planning.body.position,out.command);out.command=traffic_.adjust(planning,out.command,neighbours,w);if(reposition.second)out.command.fire_allowed=false;
        const double phase=double((positive(id)%29*17+33)%29)/29.*interval;out.deadline=in.planning.now+interval+(!previous?phase:0.);
        if(out.due){Cache value;value.server=server!=orders.end();auto token=tokens.find(id);value.token=token==tokens.end()?0:token->second;value.deadline=*out.deadline;value.at=in.planning.now;value.interval=interval;value.command=out.command;value.contacts=contacts;value.targets=targets;cache[id]=std::move(value);}}
    out.command.throttle=std::max(-1.,std::min(1.,out.command.throttle));return out;
}
bool Decisions::markReposition(const Input&in,const plan::Command&command,const Target*t,double yaw,std::int64_t sequence,const std::string&kind,std::int64_t blocker,int team,const Point&position,std::optional<double>radius)
{
    if((kind!="bot"&&kind!="player")||blocker<=0||team!=in.planning.body.team||(kind=="bot"&&blocker==in.planning.body.id)||!command.target.present||!std::all_of(position.begin(),position.end(),[](double x){return std::isfinite(x);}))return false;
    const auto&body=in.planning.body;const double own=std::hypot(body.half_width,body.half_length),other=radius&&std::isfinite(*radius)&&*radius>0.?*radius:own,clearance=own+other+.01,side=((body.id+sequence)&1)?1.:-1.;
    Reposition next;next.target=command.target.value;if(t)next.identity=t->identity;next.destination={body.position[0]+std::cos(yaw)*clearance*side,body.position[1],body.position[2]-std::sin(yaw)*clearance*side};next.shell=in.shell_index;next.fire_range=std::max(0.,command.fire_range);next.deadline=in.planning.now+4.;actors.at(body.id).reposition=next;invalidate(body.id);return true;
}
void Decisions::observeFirePosition(const Input&in,const plan::Command&command,const Target*t,const FirePositionEvidence&e)
{
    auto&a=actors.at(in.planning.body.id);const auto&p=in.planning.body.position;const double now=in.planning.now;
    if(!a.initial||command.mode!="artillery_hold"||in.airborne||in.overturned||distance(p,a.initial->point)>a.initial->radius+2.)a.fire_failure.reset();
    else{if(a.fire_failure&&(a.fire_failure->plan!=*a.initial||a.fire_failure->fire_sequence!=in.fire_sequence||distance(a.fire_failure->origin,p)>2.||now-a.fire_failure->last_failure>30.))a.fire_failure.reset();
        if(e.launch_clear)a.fire_failure.reset();else{const bool ready=t&&command.fire_allowed&&e.ready;const bool planning_failed=ready&&e.planning_failed&&mode(e.reason,{"world_blocked","no_candidate"});const bool exact_failed=ready&&!planning_failed&&e.launch_failed&&e.launch_reason=="world_blocked"&&e.launch_target&&*e.launch_target==t->identity&&e.launch_completed&&now-*e.launch_completed>=0.&&now-*e.launch_completed<=2.;const bool failed=planning_failed||exact_failed;
            if(a.fire_failure){auto&f=*a.fire_failure;if(ready&&f.counting)f.elapsed+=std::max(0.,std::min(2.,now-f.last));f.last=now;f.counting=ready;}
            if(failed){if(!a.fire_failure){FireFailure f;f.plan=*a.initial;f.origin=p;f.fire_sequence=in.fire_sequence;f.last=f.last_failure=now;a.fire_failure=f;}auto&f=*a.fire_failure;const auto proof=std::make_tuple(t->identity,exact_failed?e.launch_completed:e.completed,exact_failed?e.launch_reason:e.reason);if(std::find(f.proofs.begin(),f.proofs.end(),proof)==f.proofs.end()){f.proofs.push_back(proof);if(f.proofs.size()>8)f.proofs.erase(f.proofs.begin(),f.proofs.end()-8);}f.last_failure=now;}}}
    const Identity identity=t?t->identity:Identity{};if(a.obstruction&&(a.obstruction->target!=identity||distance(a.obstruction->origin,p)>.25||std::abs(delta(a.obstruction->yaw,in.planning.body.yaw))>.05))a.obstruction.reset();
    if(e.planning_failed&&e.reason=="world_blocked"&&e.local_blockage&&t){if(!a.obstruction)a.obstruction=Obstruction{p,in.planning.body.yaw,now,now,identity};a.obstruction->last=now;}else if(!t||e.planning_clear)a.obstruction.reset();
}
bool Decisions::blockedDirectAim(plan::Command&command,const Target*&target,const LaneReceipt*receipt,const Point&source,double now,double control,bool spg,bool refreshed,bool stable)
{
    if(!target||spg||!receipt||receipt->clear||now-receipt->at<0.||now-receipt->at>1.+control+1.e-9||refreshed)return false;
    command.target.clear();command.fire_allowed=false;command.aim=source;if(!stable&&!command.stable_hull_face)command.face=source;target=nullptr;return true;
}
}
