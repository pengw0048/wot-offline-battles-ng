#include "stock_native_bot_planning_runtime.hpp"
#include <cassert>
#include <iostream>
namespace p=stock_native_bot_planning;
namespace w=stock_native_world;
namespace n=stock_native_bot_navigation;
struct Scene:w::Scene {
    unsigned queries=0;
    w::QueryStatus segment(const w::Point&a,const w::Point&b,std::uint16_t,const w::Filter&,w::Hit&hit)override{
        ++queries;if(a[0]==b[0]&&a[2]==b[2]&&a[1]>=0.&&b[1]<=0.){hit.point={a[0],0.,a[2]};hit.normal={0.,1.,0.};return w::QueryStatus::Found;}return w::QueryStatus::Miss;}
    w::QueryStatus material(const w::Point&,const w::Point&,const w::Point&,w::MaterialHit&)override{return w::QueryStatus::Miss;}
    w::QueryStatus water(const w::Point&,const w::Point&,double&)override{return w::QueryStatus::Miss;}
    w::Admission refresh(const w::Key&,w::Registry&)override{return w::Admission::Pending;}
    bool unresolved(const w::Key&,std::vector<w::Box>&)override{return false;}
    bool destroy(const w::Destruction&)override{return true;}
    bool treeChunks(const std::vector<w::Box>&,std::vector<std::uint32_t>&)override{return true;}
    bool turretSweep(const w::TurretSweep&)override{return true;}
};
struct Host:p::PlanningHost {int rotations=0,cancellations=0;
    double planningRotationFraction(p::ID,const n::Point&,double,double,bool)override{++rotations;return 1.;}
    void cancelArtillery(p::ID)override{++cancellations;}
};
int main(){Scene scene;Host host;w::Registry registry;w::Pipeline pipeline(registry,scene,0);offline_simulation::control::Store control;
stock_native_bot_perception::Perception perception(control,registry,scene,{});p::PlanningSnapshot snapshot;
snapshot.map.width=snapshot.map.height=21;snapshot.map.cell_size=10.;snapshot.map.origin_x=snapshot.map.origin_z=-100.;snapshot.map.has_bounds=true;snapshot.map.bounds={-100.,-100.,100.,100.};snapshot.map.heights.assign(441,0.);snapshot.map.hazards.assign(441,0);snapshot.map.links.assign(441,255);snapshot.grade_up=snapshot.grade_down=1.;snapshot.bake={2.15,3.,true};snapshot.directions={{{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}}};
n::NavigationActor actor;actor.id=1;actor.team=1;actor.half_width=actor.collision_half_width=1.7;actor.half_length=actor.collision_half_length=3.5;snapshot.actors[1]=actor;
n::ExactDescriptor descriptor;descriptor.candidate={true,1.7};descriptor.hull_minimum={-1.7,0.,-3.5};descriptor.hull_maximum={1.7,2.,3.5};descriptor.mass=30000.;descriptor.speed_forward=15.;descriptor.speed_reverse=5.;descriptor.valid=true;snapshot.descriptors[1]=descriptor;
p::StrategicOrder order;order.drive.mode=order.navigation.mode="route";order.drive.move.set({0.,0.,40.});order.drive.fire_range=450.;snapshot.orders[1]=order;snapshot.order_tokens[1]=1;
stock_native_bot_strategy::Agent agent;agent.id=1;agent.team=1;agent.profile.class_tag="mediumTank";snapshot.director.agents[1]=agent;
p::PlanningRuntime runtime(snapshot,pipeline,perception,host);p::LiveActor live;live.decision.planning.body.id=1;live.decision.planning.body.team=1;live.decision.planning.body.alive=true;live.decision.planning.body.half_width=1.7;live.decision.planning.body.half_length=3.5;live.decision.planning.body.lower_y=0.;live.decision.planning.body.upper_y=2.;live.decision.step=.05;live.decision.health=live.decision.maximum_health=1000.;runtime.update(live);
stock_native_bot_perception::Actor vision;vision.config.key={offline_simulation::ActorKind::Bot,1};vision.config.team=1;vision.sample.key=vision.config.key;vision.identity=1;vision.target_available=true;
auto far=vision;far.config.key={offline_simulation::ActorKind::Bot,2};far.config.team=2;far.sample.key=far.config.key;far.sample.pose.position={1000.,0.,0.};far.identity=2;
auto human=vision;human.config.key={offline_simulation::ActorKind::Human,7};human.config.team=2;human.sample.key=human.config.key;human.sample.pose.position={25.,0.,0.};human.sample.pose.mask=1u<<9;human.sample.pose.values[9]=3.;human.identity=7;
p::TargetMetadata metadata;stock_native_bot_decision::Target target;target.health=target.max_health=600.;target.team=2;target.position={999.,0.,999.};target.speed=99.;metadata[far.config.key]=target;metadata[human.config.key]=target;
runtime.beginFrame(.05,{1});runtime.beginPerception({1,1,1,0},{vision,far,human},0.,16,false,0);
auto first=runtime.prepare(1,metadata,{});assert(first.due&&!first.reused);assert(first.contacts.size()==2);assert(first.targets.size()==1);assert(first.targets.at(1000007).position[0]==25.);assert(first.targets.at(1000007).speed==3.);
for(const auto&contact:first.contacts)if(contact.id==2){assert(!contact.visible);assert(contact.position==n::Point{});assert(contact.speed==0.);}const auto planner_clock=runtime.planner().actors().at(1).driver.clock;
live.decision.refresh_control=false;live.decision.planning.now=.05;runtime.update(live);auto reused=runtime.prepare(1,{},{});assert(reused.reused&&!reused.due);assert(runtime.planner().actors().at(1).driver.clock==planner_clock);
runtime.replaceOrders({{1,order}},{{1,2}});auto hold=runtime.prepare(1,{},{});assert(hold.physical_hold&&hold.command.throttle==0.);
n::MotionPlanInput motion;motion.id=1;motion.position={};motion.speed=3.;motion.throttle=1.;motion.step=.05;motion.now=.05;motion.half_length=3.5;motion.grounded=true;motion.decision_due=true;motion.move_position=n::Point{0.,0.,40.};auto selected=runtime.selectedMotion(motion);assert(selected.path_clear&&selected.sample&&selected.sample->receipt);assert(runtime.motionPlan().worldReceiptReusable(1,{0.,0.,.1},0.,3.,.06,.05));
auto before=scene.queries;motion.now=.06;motion.position={0.,0.,.1};motion.refresh_control=false;auto cached=runtime.selectedMotion(motion);assert(cached.cache_reused&&cached.path_clear);assert(scene.queries==before);
const auto rotations=host.rotations;assert(runtime.pose(1,.1,false));assert(host.rotations==rotations+1);runtime.finishPerception(false);runtime.endFrame();runtime.endFrame();
std::cout<<"Native planning runtime integration passed\n";
}
