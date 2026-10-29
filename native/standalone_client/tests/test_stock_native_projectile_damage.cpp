#include "../bigworld/stock_native_projectile_damage.hpp"
#include <cassert>
#include <cmath>
using namespace stock_native_projectile_damage;
struct World:DamageWorld {
 stock_native_bot_armor::Material hull;
 World(){hull.armor=10.;hull.kind=1;}
 std::vector<BlastComponent> components(const Point&,double)override{return {{1,{0.,0.,-2.},{-1.,-1.,-1.},{1.,1.,1.}}};}
 Point toWorld(unsigned,const Point& p)override{return p;}
 std::vector<ImpactLayer> vehicleRay(const Point&,const Point&)override{return {{1.,1.,1,&hull}};}
 bool visible(const Point&,const Point&,const Point&)override{return true;}
 std::vector<CriticalLayer> internal(const Target&,const Point&,const Point&,bool,const std::vector<ImpactLayer>&)override{return {};}
 std::optional<std::uint64_t> sticker(const Target&,const Point&,const Point&,const std::vector<ImpactLayer>&,const std::optional<ArmorContact>&,int)override{return {};}
};
struct Rng:Random {int count=0;double gauss(double mean,double sigma)override{++count;return count==1?mean+4.*sigma:mean;}double uniform()override{return .5;}};
int main(){Rng rng;FrozenShot shot;shot.shell.kind="ARMOR_PIERCING";shot.shell.caliber=100;shot.shell.piercing_near=180;shot.shell.piercing_far=140;shot.shell.maximum_distance=800;shot.shell.damage=200;
 stock_native_bot_armor::Material screen;screen.armor=20;screen.vehicle_damage_factor=0.;screen.kind=1;screen.once=true;stock_native_bot_armor::Material hull;hull.armor=100;hull.kind=2;
 std::vector<ImpactLayer> layers={{1.,1.,0,&screen},{1.1,1.,0,&screen},{2.,1.,1,&hull}};auto result=resolveArmor(shot,100,10,layers,rng);assert(result&&result->result==2&&result->structural&&result->layer==2&&result->accumulated_loss==30.);assert(rng.count==2);auto again=resolveArmor(shot,100,10,layers,rng);assert(again&&rng.count==2);assert(freezeDamage(shot,rng)==200.&&rng.count==3);assert(directDamage(shot,2,0,1)==200);
 FrozenShot criticalShot;criticalShot.shell.kind="ARMOR_PIERCING";criticalShot.shell.damage=200.;criticalShot.device_damage_mean=80.;CriticalConfig config;config.health=500;config.maxima["engineHealth"]=100.;config.engine_fire_chance=0.;wot::offline_bot_step::CriticalState before;before.devices["engineHealth"].hp=100.;before.devices["engineHealth"].maximum=100.;CriticalLayer hit;hit.distance=1.;hit.name="engineHealth";hit.projectile_chance=1.;auto crit=resolveCritical(criticalShot,config,before,{hit},50,rng);assert(crit.state.devices.at("engineHealth").hp==20.&&crit.device_loss.at("engineHealth")==80.&&crit.events.size()==1);
 Target target;target.identity={false,1};target.config.module_damage=false;World world;DirectEvidence ev;ev.query_start={0.,0.,0.};ev.query_end={0.,0.,4.};ev.impact={0.,0.,1.};ev.collisions={{1.,1.,1,&world.hull}};ev.range_distance=100.;FrozenShot directShot;directShot.shell=shot.shell;std::optional<RicochetContact> ricochet;auto effect=direct(directShot,target,ev,world,rng,ricochet);assert(effect&&effect->damage==200&&effect->result==2&&!ricochet);
 auto sticker=encodeSticker(1,7,{-1.,-1.,-1.},{1.,1.,1.},{-2.,0.,0.},{2.,0.,0.});assert(sticker&&sticker->encoded==std::uint64_t(0x8080ff8080000107ULL)&&sticker->decoded_start[0]<-1.&&sticker->decoded_end[0]>1.);
 FrozenShot he;he.shell.kind="HIGH_EXPLOSIVE";he.shell.explosion_damage_factor=.5;he.shell.explosion_absorption_factor=1.3;he.edge_damage_factor=.15;he.explosion_radius=10;he.damage_roll=200.;hull.vehicle_damage_factor=1.;auto blast=heBlast(he,{0.,0.,0.},{0.,0.,0.},{0.,0.,10.},{{2.,1.,0,&screen},{5.,1.,1,&hull}},200.,1.);assert(blast&&blast->damage==0&&blast->through.size()==2);
 hull.vehicle_damage_factor.reset();assert(!heBlast(he,{0.,0.,0.},{0.,0.,0.},{0.,0.,10.},{{5.,1.,1,&hull}},200.,1.));hull.vehicle_damage_factor=1.;hull.armor=10;blast=heBlast(he,{0.,0.,0.},{0.,0.,0.},{0.,0.,10.},{{5.,1.,1,&hull}},200.,2.);assert(blast&&blast->damage==39);assert(!firstRicochetMultiplier("HIGH_EXPLOSIVE"));assert(*firstRicochetMultiplier("ARMOR_PIERCING")==.75);
}
