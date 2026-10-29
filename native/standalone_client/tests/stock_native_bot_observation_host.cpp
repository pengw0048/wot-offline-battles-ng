#include "stock_native_bot_observation_runtime.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
namespace o=stock_native_bot_observation;
namespace n=stock_native_bot_navigation;
namespace w=stock_native_world;
struct Terrain:n::World {
    std::optional<double> ground(double,double,double)override{return 0.;}
    n::Proof corridor(const n::Point&,const n::Point&,double)override{return n::Proof::Clear;}
    n::Proof navigation(const n::Point&,const n::Point&,double,double,double,std::optional<double>)override{return n::Proof::Clear;}
};
struct Scene:w::Scene {
    w::QueryStatus segment(const w::Point&,const w::Point&,std::uint16_t,const w::Filter&,w::Hit&)override{return w::QueryStatus::Miss;}
    w::QueryStatus material(const w::Point&,const w::Point&,const w::Point&,w::MaterialHit&)override{return w::QueryStatus::Miss;}
    w::QueryStatus water(const w::Point&,const w::Point&,double&)override{return w::QueryStatus::Miss;}
    w::Admission refresh(const w::Key&,w::Registry&)override{return w::Admission::Pending;}
    bool unresolved(const w::Key&,std::vector<w::Box>&)override{return false;}
    bool destroy(const w::Destruction&)override{return true;}
    bool treeChunks(const std::vector<w::Box>&,std::vector<std::uint32_t>&)override{return true;}
    bool turretSweep(const w::TurretSweep&)override{return true;}
};
struct World:o::World {
    std::vector<o::LaneKey> queries;bool sheltered=false;unsigned rays=0;
    bool outgoingLane(const o::Actor&a,const o::Actor&b,double now)override{queries.push_back({a.key.id,b.key.kind==offline_simulation::ActorKind::Human?"human":"bot",b.key.id});return (a.key.id+b.key.id+int(std::floor(now*10)))%3!=0;}
    bool staticClear(const o::Point&a,const o::Point&b)override{++rays;return sheltered&&(a[0]<-3.||b[0]<-3.); }
    std::optional<double> ground(double,double,double)override{return sheltered?std::optional<double>(0.):std::nullopt;}
    double waterDepth(const o::Point&)override{return -1.;}
};
int coverCase();
int main(int argc,char**){if(argc>1)return coverCase();std::cout<<std::setprecision(17);Terrain terrain;Scene scene;World world;w::Registry registry;offline_navigation::MapInput map;map.width=map.height=1;map.cell_size=10.;map.heights={0.};map.links={0};map.hazards={0};n::Grid grid(map,terrain,.4,.6);stock_native_bot_parking::Parking parking(map,{});stock_native_bot_planner::Planner planner;stock_native_bot_planner::Traffic traffic;stock_native_bot_strategy::Director director;stock_native_bot_decision::Decisions decisions(planner,traffic,director,parking);offline_simulation::control::Store control;stock_native_bot_perception::Perception perception(control,registry,scene,{});wot::offline_bot_step::CombatSharedRuntime shared;o::Runtime runtime(shared,perception,decisions,grid,world);
double now,distance;int count,fresh,alive,refresh,publish,selected,cover,inject;while(std::cin>>now>>count>>fresh>>alive>>refresh>>publish>>selected>>cover>>distance>>inject){std::vector<o::Actor>actors;
for(int i=1;i<=count;++i){o::Actor a;a.key={offline_simulation::ActorKind::Bot,i};a.target.id=i;a.target.team=1;a.target.alive=i!=alive;a.target.health=a.target.max_health=1000.;a.pose.position={double(i),0.,0.};a.target.position=a.pose.position;a.target.class_tag=i%9?"mediumTank":"SPG";actors.push_back(a);decisions.actors[i]={};}
for(int i=0;i<2;++i){o::Actor a;a.key={i?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,201+i};a.target.id=i?1000202:201;a.target.identity={i?"human":"bot",201+i};a.target.team=2;a.target.alive=true;a.target.health=1000.;a.target.class_tag="mediumTank";a.pose.position={0.,0.,distance};a.target.position=a.pose.position;actors.push_back(a);}
runtime.replaceActors(actors);if(inject)shared.lanes[{inject,"bot",201}]={now,true};const auto schedule=runtime.begin(now,refresh,publish,true,.1);std::vector<stock_native_bot_decision::Target>contacts;
for(int i=0;i<2;++i){auto t=actors[count+i].target;t.visible=true;t.direct_visible=fresh;t.fresh_visible=fresh;contacts.push_back(t);}
for(int i=1;i<=count;++i){if(i==alive)continue;stock_native_bot_planner::Command command;command.bot_id=i;command.mode="engage";command.fire_allowed=true;command.move={0.,0.,60.};if(selected)command.target.set(selected==2?1000202:201);runtime.observeActor(i,contacts,command);if(cover)runtime.queueCover(i,&contacts.front(),command);}
auto result=runtime.finish();std::cout<<'['<<schedule.collect_observation<<','<<schedule.refresh_shot_lanes<<','<<schedule.collect_cover_jobs<<','<<schedule.cover_reserved<<','<<schedule.cycle_time<<','<<runtime.pendingLanes()<<','<<runtime.pendingCover()<<",[";
for(std::size_t i=0;i<world.queries.size();++i){if(i)std::cout<<',';const auto&k=world.queries[i];std::cout<<'['<<std::get<0>(k)<<",\""<<std::get<1>(k)<<"\","<<std::get<2>(k)<<']';}std::cout<<"],[";bool first=true;for(const auto&row:shared.lanes){if(!first)std::cout<<',';first=false;const auto&k=row.first;std::cout<<'['<<std::get<0>(k)<<",\""<<std::get<1>(k)<<"\","<<std::get<2>(k)<<','<<row.second.at<<','<<row.second.clear<<']';}std::cout<<"]]\n";}
}

int coverCase(){Terrain terrain;Scene scene;World world;world.sheltered=true;w::Registry registry;offline_navigation::MapInput map;map.width=map.height=21;map.cell_size=10.;map.origin_x=map.origin_z=-100.;map.has_bounds=true;map.bounds={-100.,-100.,100.,100.};map.heights.assign(441,0.);map.links.assign(441,255);map.hazards.assign(441,0);n::Grid grid(map,terrain,.4,.6);stock_native_bot_parking::Parking parking(map,{});stock_native_bot_planner::Planner planner;stock_native_bot_planner::Traffic traffic;stock_native_bot_strategy::Director director;stock_native_bot_decision::Decisions decisions(planner,traffic,director,parking);decisions.actors[1]={};offline_simulation::control::Store control;stock_native_bot_perception::Perception perception(control,registry,scene,{});wot::offline_bot_step::CombatSharedRuntime shared;o::Runtime runtime(shared,perception,decisions,grid,world);
o::Actor source,target;source.key={offline_simulation::ActorKind::Bot,1};source.target.team=1;source.target.health=source.target.max_health=1000.;source.target.alive=true;target=source;target.key.id=2;target.target.id=2;target.target.team=2;target.target.identity={"bot",2};target.pose.position={0.,0.,40.};target.target.position=target.pose.position;target.target.visible=target.target.fresh_visible=target.target.direct_visible=true;runtime.replaceActors({source,target});
o::Descriptor d;d.valid=d.valid_pitch=true;d.mount=wot::offline_bot_step::GunMount{};d.hull_minimum={-2.,0.,-3.};d.hull_maximum={2.,2.,3.};d.minimum_yaw=-3.141592653589793;d.maximum_yaw=3.141592653589793;d.aim.fixed_pitch=true;d.aim.fixed_limits={-1.,1.};for(auto p:{o::Point{0.,1.,0.},o::Point{0.,2.,0.},o::Point{2.,1.,0.},o::Point{-2.,1.,0.}}){wot::offline_bot_step::AimSample sample;sample.point=p;sample.turret=d.samples.size()==1;d.samples.push_back(sample);}runtime.descriptor(source.key,d);runtime.descriptor(target.key,d);
stock_native_bot_perception::Actor a,b;a.config.key=source.key;a.config.team=1;a.sample.key=source.key;a.sample.alive=true;a.identity=1;a.target_available=true;b=a;b.config.key=target.key;b.config.team=2;b.sample.key=target.key;b.sample.pose.position=target.pose.position;b.identity=2;
perception.begin({1,1,1,0},{a,b},0.,48,false,0);const auto contacts=perception.contacts(source.key);if(contacts.empty()||!contacts[0].visible)return 2;
runtime.begin(0.,true,true,true,.1);stock_native_bot_planner::Command command;command.mode="engage";command.fire_allowed=true;command.target.set(2);command.move={0.,0.,60.};runtime.observeActor(1,{target.target},command);runtime.queueCover(1,&target.target,command);runtime.finish();runtime.begin(.4,true,true,true,.1);runtime.observeActor(1,{target.target},command);auto published=runtime.finish();perception.finish();
if(!published||published->affordances.empty()||published->affordances[0].candidates.empty())return 3;const auto&value=published->affordances[0].candidates[0];std::cout<<std::setprecision(17)<<"[\""<<value.id<<"\",["<<value.position[0]<<','<<value.position[1]<<','<<value.position[2]<<"],["<<value.peek_position[0]<<','<<value.peek_position[1]<<','<<value.peek_position[2]<<"],"<<value.travel_distance<<','<<value.route_alignment<<','<<value.enemy_occlusion<<','<<value.exposure<<','<<value.slope<<','<<value.water<<','<<value.ally_congestion<<"]\n";return 0;}
