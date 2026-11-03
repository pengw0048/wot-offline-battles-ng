#include "stock_native_bot_decision.hpp"
#include <cassert>
#include <iostream>
using namespace stock_native_bot_decision;
struct FixtureWorld:stock_native_bot_decision::World {
    int cancelled=0;
    bool direction(std::int64_t,double,const plan::Optional<double>&,double,const plan::Optional<double>&)override{return true;}
    bool pose(std::int64_t,double,bool)override{return true;}
    plan::Navigation navigate(const plan::Input&,const plan::Order&,const Point&p)override{plan::Navigation out;out.target=p;out.status="ready";return out;}
    void useOrder(std::int64_t,const nav::Strategy&)override{}
    void replan(std::int64_t,const Point&,double)override{}
    void blockedPlanner(const Point&,const plan::Command&)override{}
    void cancelArtillery(std::int64_t)override{++cancelled;}
};
int main(){
    offline_navigation::MapInput map;map.width=map.height=10;map.cell_size=10.;map.heights.assign(100,0.);map.links.assign(100,255);map.hazards.assign(100,0);
    plan::Planner planner;plan::Traffic traffic;strategy::Director director;parking::Parking parking(map,{});Decisions owner(planner,traffic,director,parking);FixtureWorld world;
    stock_native_bot_planning::PlanningSnapshot snapshot;snapshot.objective_bases[1]={{0.,0.,0.},10.};snapshot.objective_bases[2]={{90.,0.,90.},10.};
    for(int id=1;id<=5;++id){snapshot.actors[id].team=1;snapshot.actors[id].id=id;snapshot.director.agents[id].profile.class_tag=id==5?"mediumTank":"SPG";snapshot.director.agents[id].id=id;snapshot.director.agents[id].team=1;}
    parking::Plan initial;initial.zone="one";initial.point={20.,0.,20.};initial.clearance=6.;initial.radius=2.;snapshot.initial_parking[1]=initial;
    owner.install(snapshot);std::map<int,Input> live;
    for(int id=1;id<=5;++id){Input in;in.planning.body.id=id;in.planning.body.team=1;in.planning.body.alive=true;in.planning.body.position={30.,0.,30.};in.planning.body.half_length=3.5;in.planning.body.half_width=1.7;in.refresh_control=true;in.step=in.planning.dt=.1;live[id]=in;owner.updateActor(in);StrategicOrder order;order.drive.mode="artillery_hold";order.drive.move.set({20.,0.,20.});owner.orders[id]=order;}
    auto output=owner.prepare(live[2],{},{},{},{},world);assert(output.command.mode=="base_capture");assert(owner.actors[1].spg_deployment=="parking");
    live[1].planning.body.alive=false;live[2].planning.now=1.;for(auto&row:live)owner.updateActor(row.second);owner.prepare(live[2],{},{},{},{},world);int replacements=0;for(int id=2;id<=4;++id)replacements+=owner.actors[id].initial&&owner.actors[id].replacement_parking;assert(replacements==1);
    live[5].planning.body.alive=false;live[2].planning.now=2.;for(auto&row:live)owner.updateActor(row.second);owner.prepare(live[2],{},{},{},{},world);int sorties=0;for(int id=2;id<=4;++id)sorties+=owner.actors[id].spg_mode=="sortie";assert(sorties==3);
    live[2].empty_ammo=true;live[2].planning.now=3.;owner.updateActor(live[2]);output=owner.prepare(live[2],{},{},{},{},world);assert(owner.actors[2].spg_mode=="empty"&&owner.actors[2].empty_active&&!owner.actors[2].initial);assert(output.command.mode=="spg_route_travel"&&!output.command.fire_allowed);
    live[2].planning.body.position={0.,0.,0.};live[2].planning.now=4.;owner.updateActor(live[2]);output=owner.prepare(live[2],{},{},{},{},world);assert(output.command.mode=="base_capture");
    Target target;target.id=100;target.identity={"bot",100};const Target*selected=&target;plan::Command tracking;tracking.target.set(100);tracking.fire_allowed=true;tracking.aim={10.,2.,30.};tracking.face=tracking.aim;LaneReceipt blocked{0.,false};
    assert(!Decisions::blockedDirectAim(tracking,selected,&blocked,{},.1,.1,false));assert(selected==&target&&tracking.target.present&&!tracking.fire_allowed&&tracking.aim==Point({10.,2.,30.}));
    Input magazine;magazine.planning.body.id=5;magazine.planning.body.team=1;magazine.planning.body.alive=true;magazine.planning.body.half_length=3.5;magazine.planning.body.half_width=1.7;magazine.health=100.;magazine.maximum_health=100.;magazine.clip_size=3;magazine.clip_remaining=2;magazine.loaded_ammo=10;magazine.reload_time=2.;magazine.planning.now=10.;
    target.visible=true;target.health=100.;target.position={50.,0.,50.};Targets targets{{100,target}};plan::Command shot;shot.mode="engage";shot.target.set(100);shot.throttle=0.;owner.noteCommittedFire(magazine,shot,&target);assert(owner.actors[5].magazine);
    StrategicOrder approach;approach.drive.target.set(100);approach.drive.aim.set(target.position);approach.drive.move.set(target.position);approach.drive.mode="advance_contact";approach.drive.fire_allowed=true;
    auto retained=owner.adaptOrder(magazine,approach,targets,{},world);assert(retained.drive.mode=="engage"&&retained.drive.move.value==magazine.planning.body.position&&retained.drive.throttle.value==0.);
    magazine.magazine_lane_blocked=true;owner.adaptOrder(magazine,approach,targets,{},world);assert(!owner.actors[5].magazine);
    magazine.blocked_lane_target=target.identity;magazine.planning.now=15.;owner.actors[5].rear_episode=RearEpisode{100,0,target.identity,15.};
    const auto wall_approach=owner.adaptOrder(magazine,approach,targets,{},world);assert(!owner.actors[5].rear_episode&&owner.actors[5].rear_motion_blocked&&wall_approach.drive.move.value==target.position);
    magazine.blocked_lane_target.reset();
    magazine.planning.now=20.;magazine.planning.speed=5.;magazine.mechanical_speed=0.;magazine.blocked_player_contact=true;owner.actors[5].gun_refusal=target.identity;
    const auto blocked_contact=owner.adaptOrder(magazine,approach,targets,{},world);assert(owner.actors[5].gun_adjustment&&blocked_contact.drive.move.value==magazine.planning.body.position&&!blocked_contact.drive.fire_allowed);
    magazine.planning.now=20.;assert(owner.markReposition(magazine,shot,&target,0.,9,"bot",7,1,{5.,0.,0.},4.));const auto original=*owner.actors[5].reposition;
    magazine.planning.body.position={1.,0.,1.};magazine.planning.now=21.;assert(owner.markReposition(magazine,shot,&target,0.,9,"bot",7,1,{5.,0.,0.},4.));assert(owner.actors[5].reposition->destination==original.destination&&owner.actors[5].reposition->deadline==original.deadline);
    std::cout<<"native artillery modes and blocked tracking passed\n";
}
