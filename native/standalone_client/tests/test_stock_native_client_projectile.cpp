#include "stock_native_client_projectile.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
using namespace stock_native_client_projectile;
struct World:projectile::OwnerWorld {
 std::vector<projectile::Record>records()override{return {};}
 projectile::CollisionQuery collide(const projectile::Record&,const Point&,const Point&,const std::optional<projectile::Pose>&)override{return {};}
 std::optional<double>turretBlock(const projectile::Chord&)override{return {};}
 projectile::SceneResult scenery(const projectile::Cursor&,projectile::FlightMeta&,const Point&,const Point&,const Point&)override{return {};}
 damage::Resolution resolve(projectile::Flight&,const projectile::Cursor&,const projectile::TerminalData&)override{return {};}
 std::vector<stock_native_world::Destruction>takeDestructions()override{return {};}
 std::string shooterKey(const projectile::Launch&l)const override{return "player:"+std::to_string(l.shooter);}
 bool pipelined(const projectile::Launch&)const override{return true;}
};
struct Random:damage::Random {int draws=0;double uniform()override{++draws;return .25;}double gauss(double mean,double)override{++draws;return mean;}};
stock_native_client_weapon::Gun gun(){stock_native_client_weapon::Gun g;g.config.base_dispersion=.1;g.config.after_shot=2.;g.config.after_shot_in_burst=.5;g.config.reload=5.;g.config.clip_reload=.2;g.config.clip_size=3;g.config.burst_count=3;g.config.burst_interval=.1;g.ammunition={7,8};g.clip=3;g.dispersion=.1;return g;}
Trigger trigger(){Trigger t;t.round=9;t.player=4;t.input_sequence=23;t.vehicle="ussr:T-34";t.direction={0,0,1};t.now=4.321;t.server_ms=1234;t.reload_factor=.75;t.mounted.speed=100;t.mounted.gravity=10;t.mounted.maximum=500;t.mounted.shot.shell.kind="ARMOR_PIERCING";t.mounted.shot.shell.damage=200;t.mounted.shot.shell.caliber=100;t.mounted.shot.shell.piercing_near=100;t.mounted.shot.shell.piercing_far=80;t.stickers={11u,12u};return t;}
int main(int argc,char**argv){if(argc>1&&std::string(argv[1])=="barrel"){std::cout<<std::setprecision(17);Point p{},a{};Barrel mount;double turret,pitch;while(std::cin>>p[0]){for(unsigned i=1;i<3;++i)std::cin>>p[i];for(auto&v:a)std::cin>>v;std::cin>>turret>>pitch;for(auto*part:{&mount.hull,&mount.turret,&mount.gun})for(auto&v:*part)std::cin>>v;std::cin>>mount.length;const auto point=barrelPoint(mount,p,a,turret,pitch);std::cout<<point[0]<<' '<<point[1]<<' '<<point[2]<<'\n';}return 0;}if(argc>1){double value;std::cout<<std::setprecision(17);while(std::cin>>value)std::cout<<wireRound(value)<<'\n';return 0;}
 World world;projectile::Owner owner(world);assert(owner.observe(2,1234,4.,4.));Lifecycle lifecycle(owner);Random random;auto g=gun();auto t=trigger();
 const auto*pending=lifecycle.begin(t,g,random);assert(pending);const auto first=*pending;assert(first.flight.id=="9:p:4:1"&&first.flight.burst_count==3&&first.flight.burst_index==0&&first.input_sequence==23);assert(g.ammunition[0]==7&&g.reload_time==0.);assert(owner.flights().at(first.flight.id).anchor==t.now);assert(owner.flights().at(first.flight.id).meta.shooter_key=="player:4");assert(!lifecycle.complete(false,g));assert(owner.flights().empty()&&g.ammunition[0]==7&&!lifecycle.burst());
 pending=lifecycle.begin(t,g,random);assert(pending);const auto accepted=*pending;g.pending=1;assert(lifecycle.complete(true,g));assert(!lifecycle.complete(true,g));assert(g.ammunition[0]==6&&g.clip==2&&g.burst_remaining==2&&g.pending==1&&g.reload_time==0.);assert(!lifecycle.burstDue(t.now+.099));assert(lifecycle.burstDue(t.now+.1));assert(lifecycle.launches().at(accepted.flight.id).source.speed==100.&&lifecycle.launches().at(accepted.flight.id).stickers[1]==12u);
 t.now+=.1;t.input_sequence=24;t.mounted.speed=110;pending=lifecycle.begin(t,g,random);assert(pending&&pending->flight.burst_index==1&&pending->flight.group_sequence==2&&pending->flight.sequence==3);assert(lifecycle.complete(true,g));assert(g.ammunition[0]==5&&g.burst_remaining==1);
 t.now+=.1;t.input_sequence=25;pending=lifecycle.begin(t,g,random);assert(pending&&pending->flight.burst_index==2);assert(lifecycle.complete(true,g));assert(g.ammunition[0]==4&&g.clip==0&&g.burst_remaining==0&&g.shot==1&&g.reload_time==3.75&&!lifecycle.burst());
 // Accepted immutable source survives new mounted descriptor and shell choice.
 auto echo=accepted;assert(lifecycle.canonical(echo,t.now));echo.source.speed=115.;assert(!lifecycle.canonical(echo,t.now));assert(lifecycle.launches().at(accepted.flight.id).source.speed==100.);
 lifecycle.reset(true);g=gun();t=trigger();pending=lifecycle.begin(t,g,random);assert(pending&&pending->flight.sequence==5);assert(lifecycle.complete(true,g));assert(lifecycle.cancelBurst(g,.6));assert(g.ammunition[0]==6&&g.burst_remaining==0&&g.reload_time==.2);
 // Unknown clock/identity and empty trajectory cannot create a sound-only shot.
 lifecycle.reset();g=gun();t=trigger();t.direction={};assert(!lifecycle.begin(t,g,random));assert(g.ammunition[0]==7);t=trigger();t.input_sequence=0;assert(!lifecycle.begin(t,g,random));
 std::cout<<"Native visible projectile freeze, FIFO admission, burst and ammo passed\n";
}
