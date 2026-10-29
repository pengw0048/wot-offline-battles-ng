#include "stock_native_bot_decision.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
using namespace stock_native_bot_decision;
struct Scene:World {
    int cancelled=0;
    bool direction(std::int64_t,double,const plan::Optional<double>&,double,const plan::Optional<double>&)override{return true;}
    bool pose(std::int64_t,double,bool)override{return true;}
    plan::Navigation navigate(const plan::Input&,const plan::Order&,const Point&p)override{plan::Navigation n;n.target=p;return n;}
    void useOrder(std::int64_t,const nav::Strategy&)override{}
    void replan(std::int64_t,const Point&,double)override{}
    void blockedPlanner(const Point&,const plan::Command&)override{}
    void cancelArtillery(std::int64_t)override{++cancelled;}
};
void point(const Point&p){std::cout<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';}
int main(){std::cout<<std::setprecision(17);offline_navigation::MapInput map;map.width=map.height=21;map.cell_size=10.;map.origin_x=map.origin_z=-100.;map.has_bounds=true;map.bounds={-100.,-100.,100.,100.};map.heights.assign(441,0.);map.links.assign(441,255);map.hazards.assign(441,0);
std::array<std::array<int,2>,8> dirs{{{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}}};parking::Parking parking(map,dirs);parking::Plan initial;initial.catalog="test";initial.map="test";initial.zone="a";initial.cell="A1";initial.source="fixture";initial.radius=2.;initial.clearance=6.;parking::Catalog catalog;
for(int i=0;i<4;++i){parking::Plan p=initial;p.point={20.*i,0.,0.};p.zone=i==3?"b":"a";p.cell="A"+std::to_string(i+1);catalog.candidates.push_back({p,220+2*i,i==3?1:0,0.});catalog.areas[p.cell]={false,{-90.,-90.,90.,90.},0.};}parking.catalogs[1]=catalog;
plan::Planner planner;plan::Traffic traffic;strategy::Director director;Decisions owner(planner,traffic,director,parking);owner.actors[1].class_tag="SPG";owner.actors[1].initial=initial;Scene scene;
std::string op;while(std::cin>>op){
if(op=="grid"){int index,hazard;double height,x,y,z,radius;std::cin>>index>>hazard>>height>>x>>y>>z>>radius;if(index>=0){map.hazards[index]=hazard;map.heights[index]=height;}auto closest=parking.closest({x,y,z});std::cout<<'[';if(closest)std::cout<<*closest;else std::cout<<"null";std::cout<<','<<(closest&&parking.clear(*closest,radius))<<",[";auto distances=parking.distances({x,y,z});for(std::size_t i=0;i<distances.size();++i){if(i)std::cout<<',';if(std::isfinite(distances[i]))std::cout<<distances[i];else std::cout<<"null";}std::cout<<"]]\n";continue;}
Input in;in.planning.body.id=1;in.planning.body.team=1;in.planning.body.half_width=1.7;in.planning.body.half_length=3.5;int ready,allow,has_target,airborne,overturned,planning_failed,planning_clear,launch_clear,launch_failed,launch_target,has_completed,has_launch_completed,local;double completed,launch_completed,occupied_x;std::string reason,launch_reason,mode;
std::cin>>in.planning.now>>in.planning.body.position[0]>>in.planning.body.position[1]>>in.planning.body.position[2]>>in.fire_sequence>>ready>>allow>>has_target>>airborne>>overturned>>mode>>planning_failed>>planning_clear>>reason>>has_completed>>completed>>launch_clear>>launch_failed>>launch_reason>>has_launch_completed>>launch_completed>>launch_target>>local>>occupied_x;
in.airborne=airborne;in.overturned=overturned;Target t;t.id=100;t.identity={"bot",100};t.alive=true;t.health=100.;t.position={0.,0.,180.};plan::Command command;command.mode=mode;command.fire_allowed=allow;FirePositionEvidence evidence;evidence.ready=ready;evidence.planning_failed=planning_failed;evidence.planning_clear=planning_clear;evidence.reason=reason;evidence.local_blockage=local;if(has_completed)evidence.completed=completed;evidence.launch_clear=launch_clear;evidence.launch_failed=launch_failed;evidence.launch_reason=launch_reason;if(has_launch_completed)evidence.launch_completed=launch_completed;if(launch_target)evidence.launch_target=Identity{"bot",launch_target};owner.observeFirePosition(in,command,has_target?&t:nullptr,evidence);
std::vector<parking::Occupied> occupied;if(occupied_x>-999.)occupied.push_back({{},Point{occupied_x,0.,0.},4.});StrategicOrder order;order.drive.mode=mode;order.drive.fire_allowed=allow;order.drive.move.set({});order.drive.aim.set(t.position);order.drive.face.set(t.position);if(has_target)order.drive.target.set(100);Targets targets;if(has_target)targets[100]=t;
auto out=owner.adaptOrder(in,order,targets,occupied,scene);const auto&a=owner.actors.at(1);std::cout<<"[\""<<out.drive.mode<<"\",";point(out.drive.move.value);std::cout<<','<<out.drive.fire_allowed<<',';if(out.drive.target.present)std::cout<<out.drive.target.value;else std::cout<<"null";std::cout<<',';point(a.initial->point);std::cout<<','<<a.parking_retries<<','<<a.failed_parking.size()<<','<<a.fire_failed_parking.size()<<','<<a.fire_relocating<<",\""<<a.position_event<<"\","<<scene.cancelled<<',';
if(a.fire_failure){const auto&f=*a.fire_failure;std::cout<<'['<<f.elapsed<<','<<f.last<<','<<f.last_failure<<','<<f.counting<<','<<f.proofs.size()<<']';}else std::cout<<"null";std::cout<<','<<bool(a.obstruction)<<"]\n";
}}
