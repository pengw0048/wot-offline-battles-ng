#include "stock_native_bot_decision.hpp"
#include <iostream>
#include <iomanip>
using namespace stock_native_bot_decision;
struct Scene:World {bool clear=true;int cancellations=0;
    bool direction(std::int64_t,double,const plan::Optional<double>&,double,const plan::Optional<double>&)override{return clear;}
    bool pose(std::int64_t,double,bool)override{return true;}
    plan::Navigation navigate(const plan::Input&,const plan::Order&,const Point&p)override{plan::Navigation n;n.target=p;return n;}
    void useOrder(std::int64_t,const nav::Strategy&)override{}
    void replan(std::int64_t,const Point&,double)override{}
    void blockedPlanner(const Point&,const plan::Command&)override{}
    void cancelArtillery(std::int64_t)override{++cancellations;}
};
void point(const plan::Optional<Point>&p){if(!p.present){std::cout<<"null";return;}std::cout<<"["<<p.value[0]<<","<<p.value[1]<<","<<p.value[2]<<"]";}
int main(){std::cout<<std::setprecision(17);offline_navigation::MapInput map;map.width=map.height=1;map.heights={0.};map.links={0};map.hazards={0};parking::Parking parking(map,{});plan::Planner planner;plan::Traffic traffic;strategy::Director director;Decisions owner(planner,traffic,director,parking);Scene scene;
for(int id=1;id<=3;++id){Actor a;a.class_tag=id==2?"SPG":"mediumTank";a.route_id="r";a.route={{0.,0.},{0.,20.},{0.,60.},{0.,200.}};owner.actors[id]=a;strategy::Agent s;s.id=id;s.team=1;s.seed=id;s.has_route=true;s.route_id="r";s.waypoints=a.route;s.profile.class_tag=a.class_tag;s.profile.dominant_role="support";s.profile.desired_range=200.;s.profile.fire_range=450.;s.profile.armor=100.;s.personality={.5,.5,.5,.5};director.agents[id]=s;}
std::string op;while(std::cin>>op){if(op=="mark"){Input in;std::string kind;int blocker,team,has_radius,has_target;double bx,by,bz,yaw,radius,fire_range;std::int64_t sequence;std::cin>>in.planning.body.id>>in.planning.now>>in.planning.body.position[0]>>in.planning.body.position[1]>>in.planning.body.position[2]>>yaw>>sequence>>in.shell_index>>fire_range>>kind>>blocker>>team>>bx>>by>>bz>>has_radius>>radius>>has_target;
in.planning.body.team=1;in.planning.body.half_width=1.7;in.planning.body.half_length=3.5;plan::Command command;command.fire_range=fire_range;if(has_target)command.target.set(100);Target target;target.id=100;target.identity={"bot",100};const bool accepted=owner.markReposition(in,command,&target,yaw,sequence,kind,blocker,team,{bx,by,bz},has_radius?std::optional<double>(radius):std::nullopt);const auto&marker=owner.actors.at(in.planning.body.id).reposition;
std::cout<<'['<<accepted<<',';if(marker){std::cout<<'[';point(plan::Optional<Point>{true,marker->destination});std::cout<<','<<marker->target<<','<<marker->shell<<','<<marker->fire_range<<','<<marker->deadline<<']';}else std::cout<<"null";std::cout<<"]\n";continue;}
if(op=="rear"){int id;double now;std::cin>>id>>now;auto&a=owner.actors[id];a.rear_episode=RearEpisode{100,0,{"bot",100},now};a.rear_lease=RearLease{{},now,{}};continue;}
if(op=="obstruction"){int id;double since,last;std::cin>>id>>since>>last;owner.actors[id].obstruction=Obstruction{{},0.,since,last,{"bot",100}};continue;}
Input in;int alive,refused,fire,refresh,fixed,tier,has_server,token;double tx,tz,mx,mz;std::string mode_name,phase;
std::cin>>in.planning.body.id>>in.planning.now>>in.planning.body.position[0]>>in.planning.body.position[2]>>in.planning.body.yaw>>in.planning.speed>>in.reload_time>>in.fire_sequence>>mode_name>>phase>>alive>>tx>>tz>>mx>>mz>>fire>>refused>>scene.clear>>refresh>>fixed>>tier>>has_server>>token;
in.planning.body.team=1;in.planning.body.alive=true;in.planning.body.half_length=3.5;in.planning.body.half_width=1.7;in.planning.body.lower_y=-1.;in.planning.body.upper_y=2.;in.step=.05;in.health=in.maximum_health=1000.;in.refresh_control=refresh;in.fixed_control=fixed;in.detail_tier=tier;
Target t;t.id=100;t.identity={"bot",100};t.team=2;t.position={tx,0.,tz};t.health=t.max_health=1000.;t.visible=true;t.alive=alive;t.class_tag="mediumTank";Targets targets{{100,t}};
StrategicOrder raw;raw.drive.target.set(100);raw.drive.aim.set(t.position);raw.drive.face.set(t.position);raw.drive.move.set({mx,0.,mz});raw.drive.mode=mode_name;raw.drive.parking_phase=phase=="none"?"":phase;raw.drive.fire_allowed=fire;raw.drive.fire_range=450.;raw.navigation.mode=mode_name;raw.navigation.route_id="r";raw.navigation.route_index=2;raw.navigation.target=100;
auto&a=owner.actors.at(in.planning.body.id);a.gun_refusal=refused?std::optional<Identity>(t.identity):std::nullopt;
if(op=="policy"){auto out=owner.adaptOrder(in,raw,targets,{},scene);std::cout<<"[\""<<out.drive.mode<<"\",";point(out.drive.move);std::cout<<",";point(out.drive.aim);std::cout<<",";point(out.drive.face);std::cout<<","<<out.drive.fire_allowed<<",";if(out.drive.throttle.present)std::cout<<out.drive.throttle.value;else std::cout<<"null";std::cout<<",";if(out.drive.target.present)std::cout<<out.drive.target.value;else std::cout<<"null";
std::cout<<","<<bool(a.rear_episode)<<","<<a.rear_motion_blocked<<","<<bool(a.gun_adjustment)<<","<<a.gun_rejected.size()<<","<<bool(a.rear_position)<<","<<scene.cancellations<<"]\n";}
else if(op=="frame"){if(has_server)owner.orders[in.planning.body.id]=raw;else owner.orders.erase(in.planning.body.id);owner.tokens[in.planning.body.id]=token;auto out=owner.prepare(in,{t},targets,{}, {},scene);const auto&c=out.command;std::cout<<"["<<out.due<<","<<out.reused<<","<<out.physical_hold<<","<<c.throttle<<","<<c.turn<<",\""<<c.mode<<"\",\""<<c.recovery<<"\","<<c.movement_intent<<","<<c.fire_allowed<<",";
if(out.deadline)std::cout<<*out.deadline;else std::cout<<"null";std::cout<<"]\n";}}
}
