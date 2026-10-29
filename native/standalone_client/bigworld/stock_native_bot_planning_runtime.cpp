#include "stock_native_bot_planning_runtime.hpp"
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_planning {
namespace {
template<class T> std::optional<T> optional(const plan::Optional<T>&v){return v.present?std::optional<T>(v.value):std::nullopt;}
ID plannerID(Key key){return key.kind==offline_simulation::ActorKind::Human?1000000+key.id:key.id;}
}
PlanningRuntime::PlanningRuntime(const PlanningSnapshot&s,stock_native_world::Pipeline&pipeline,
    perception::Perception&perception,PlanningHost&host):host_(host),perception_(perception),bake_(s.bake),
    scene_(pipeline,*this),grid_(s.map,scene_,s.grade_up,s.grade_down),navigator_(grid_),navigation_(navigator_),
    motion_queries_(scene_,pipeline,recast_budget_),motion_plan_(motion_queries_),parking_(grid_.map,s.directions),
    decisions_(planner_,traffic_,director_,parking_),descriptors_(s.descriptors)
{
    grid_.failed_edges=s.failed_edges;grid_.static_hull_edges=s.static_hull_edges;grid_.review_cells=s.review_cells;
    grid_.hull_revision=s.hull_revision;grid_.proof_revision=s.proof_revision;
    navigation_.actors=s.actors;decisions_.install(s);
    for(const auto&order:s.orders)navigation_.orders[order.first]=order.second.navigation;
}
PlanningRuntime::~PlanningRuntime(){endFrame();navigator_.close();}
void PlanningRuntime::replaceOrders(std::map<ID,StrategicOrder> orders,std::map<ID,std::uint64_t> tokens)
{
    decisions_.orders=std::move(orders);decisions_.tokens=std::move(tokens);navigation_.orders.clear();
    for(const auto&row:decisions_.orders)navigation_.orders[row.first]=row.second.navigation;
}
void PlanningRuntime::descriptor(ID id,const nav::ExactDescriptor&value)
{descriptors_.at(id)=value;candidate_probes_.erase(id);motion_plan_.cache.erase(id);decisions_.invalidate(id);}
void PlanningRuntime::syncParking(ID id)
{
    const auto&a=decisions_.actors.at(id);auto&n=navigation_.actors.at(id);
    if(a.initial){const auto&p=*a.initial;n.spg=nav::InitialSPG{p.catalog,p.map,p.zone,p.cell,p.point,p.clearance};}
    else n.spg.reset();
}
void PlanningRuntime::update(LiveActor value)
{
    const auto&in=value.decision.planning;const ID id=in.body.id;auto&n=navigation_.actors.at(id);
    n.position=in.body.position;n.yaw=in.body.yaw;n.speed=in.speed;n.alive=in.body.alive;
    n.team=in.body.team;n.slot=in.slot;n.half_width=in.body.half_width;n.half_length=in.body.half_length;
    n.collision_half_width=in.body.half_width;n.collision_half_length=in.body.half_length;
    n.artillery_intent=value.artillery_intent;n.artillery_reproof=value.artillery_reproof;
    auto driver=planner_.actors().find(id);n.traffic_waiting=driver!=planner_.actors().end()&&driver->second.driver.traffic_waiting;
    auto&state=candidate_states_[id];state.id=id;state.position=in.body.position;state.yaw=in.body.yaw;state.speed=in.speed;state.now=in.now;
    state.water_depth=value.water_depth;state.half_width=in.body.half_width;state.half_length=in.body.half_length;state.rejected_rotations=value.rejected_rotations;
    decisions_.actors.at(id).blocked_rotations=value.rejected_rotations;
    actors_[id]=std::move(value);candidate_probes_.erase(id);syncParking(id);
}
void PlanningRuntime::humans(std::vector<nav::NavigationHuman> values){navigation_.humans=std::move(values);}
void PlanningRuntime::beginFrame(double elapsed,const std::set<ID>&live)
{
    if(open_)throw std::logic_error("Native planning frame is already open");
    navigator_.beginFrame(elapsed);motion_plan_.beginFrame(live);recast_budget_=24;open_=true;
}
void PlanningRuntime::endFrame()
{if(!open_)return;motion_plan_.endFrame();navigator_.endFrame();candidate_probes_.clear();open_=false;}
void PlanningRuntime::beginPerception(const offline_simulation::TickToken&token,const std::vector<perception::Actor>&rows,
    double now,int budget,bool include_humans,std::uint64_t phase)
{perception_.begin(token,rows,now,budget,include_humans,phase);}
void PlanningRuntime::updatePerception(perception::Actor row){perception_.update(std::move(row));}
void PlanningRuntime::finishPerception(bool humans){if(humans)perception_.observeHumans();perception_.finish();}
nav::CandidateProbe& PlanningRuntime::candidate(ID id)
{
    auto&probe=candidate_probes_[id];if(!probe)probe=std::make_unique<nav::CandidateProbe>(grid_,navigator_,scene_,candidate_states_.at(id),descriptors_.at(id).candidate,bake_);return *probe;
}
std::vector<stock_native_bot_parking::Occupied> PlanningRuntime::occupancy(ID id)const
{
    std::vector<stock_native_bot_parking::Occupied> out;
    for(const auto&row:navigation_.actors){if(row.first==id)continue;const auto&a=row.second;
        out.push_back({{},a.position,std::hypot(a.collision_half_width,a.collision_half_length)});
        if(a.alive&&a.spg)out.push_back({a.team,a.spg->point,a.spg->clearance});}
    for(const auto&human:navigation_.humans)if(human.world_pose&&human.participating)
        out.push_back({{},human.position,std::hypot(human.collision_half_width,human.collision_half_length)});
    return out;
}
decision::Output PlanningRuntime::prepare(ID id,const TargetMetadata&metadata,const std::vector<plan::Body>&neighbours)
{
    auto&live=actors_.at(id);auto&in=live.decision;in.planning.navigation_recovery_allowed=navigation_.recoveryAllowed(id,in.planning.body.position,live.hard_grinds);
    std::vector<decision::Target> contacts;decision::Targets targets;
    if(decisions_.due(in))for(const auto&row:perception_.contacts({offline_simulation::ActorKind::Bot,id})){
        const auto found=metadata.find(row.target);if(found==metadata.end())throw std::invalid_argument("Native perception contact has no installed target metadata");
        auto target=found->second;target.id=plannerID(row.target);target.identity={row.target.kind==offline_simulation::ActorKind::Human?"human":"bot",row.target.id};
        target.visible=row.visible;target.direct_visible=row.direct;target.fresh_visible=row.fresh;target.position={};target.speed=0.;target.observed_pose.reset();
        if(row.has_pose){target.observed_pose=row.pose;target.position=row.pose.position;if(row.pose.mask&(1u<<9))target.speed=row.pose.values[9];}
        contacts.push_back(target);if(target.visible)targets[target.id]=target;
    }
    candidate_probes_.erase(id);auto output=decisions_.prepare(in,contacts,targets,neighbours,occupancy(id),*this);syncParking(id);
    return output;
}
plan::Command PlanningRuntime::safeControls(ID id,const plan::Command&command,const std::vector<plan::Body>&neighbours,double stopping_distance)
{const auto&actor=actors_.at(id);auto input=actor.decision.planning;input.dt=actor.decision.step;return traffic_.safe_controls(input,command,neighbours,stopping_distance);}
nav::ProbePlan PlanningRuntime::selectedMotion(nav::MotionPlanInput input)
{
    input.descriptor=descriptors_.at(input.id);input.cell_size=grid_.map.cell_size;input.baked=true;
    input.clearance_distance=optional(traffic_.clearance_distance(input.id,input.position));auto out=motion_plan_.prepare(input);
    if(out.remember_failure)planner_.remember_failure(input.id,out.travel_yaw);
    if(out.report_blocked_corridor&&input.move_position)navigator_.reportHardContact(input.id,input.position,*input.move_position,out.travel_yaw,input.now);
    return out;
}
bool PlanningRuntime::direction(ID id,double yaw,const plan::Optional<double>&maximum,double drive_direction,const plan::Optional<double>&implicit)
{(void)drive_direction;return candidate(id).direction(yaw,optional(maximum),optional(implicit));}
bool PlanningRuntime::pose(ID id,double yaw,bool wreck_push){return candidate(id).pose(yaw,wreck_push);}
plan::Navigation PlanningRuntime::navigate(const plan::Input&in,const plan::Order&,const nav::Point&destination)
{
    const ID id=in.body.id;auto agent=director_.agents.find(id);if(agent!=director_.agents.end()){auto&actor=navigation_.actors.at(id);actor.local_route_index=agent->second.waypoint_index;actor.local_route_join=agent->second.route_join_index==agent->second.waypoint_index;}
    const auto result=navigation_.target(id,in.body.position,destination,selected_orders_.at(id),in.now);plan::Navigation out;
    out.target=result.target;out.status=result.status;out.stop_at_target.set(result.stop_at_target);if(result.progress)out.progress_target.set(*result.progress);return out;
}
void PlanningRuntime::useOrder(ID id,const nav::Strategy&order){syncParking(id);selected_orders_[id]=order;}
void PlanningRuntime::replan(ID id,const nav::Point&p,double now){navigator_.requestReplan(id,p,now);}
void PlanningRuntime::blockedPlanner(const nav::Point&p,const plan::Command&command)
{
    if(command.recovery!="blocked"||!command.movement_intent)return;
    const auto found=candidate_probes_.find(command.bot_id);if(found==candidate_probes_.end())return;
    for(const auto&row:found->second->samples())if(row.second.collision&&!row.second.deferred&&!row.second.probe_failed){navigator_.reportBlockedPlan(p,command.move);return;}
}
void PlanningRuntime::cancelArtillery(ID id){host_.cancelArtillery(id);}
double PlanningRuntime::fraction(ID id,const nav::Point&p,double from,double to,bool wreck_push)
{return host_.planningRotationFraction(id,p,from,to,wreck_push);}
}
