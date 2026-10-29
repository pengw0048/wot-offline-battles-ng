#include "../bigworld/stock_native_bot_weapon_runtime.hpp"
#include <cassert>
using namespace wot::offline_bot_step;
namespace p=stock_native_bot_planner;namespace d=stock_native_bot_decision;
struct World:WeaponRuntimeWorld {
 void bind(offline_simulation::ActorKey,const std::optional<WeaponTarget>&,const stock_native_bot_armor::Shell*)override{}
 std::optional<WeaponTarget> target(const std::string&,std::int64_t)override{return {};}
 std::optional<Point> barrel_point(offline_simulation::ActorKey,const AimPose&)override{return Point{};}
 bool source_available(offline_simulation::ActorKey)override{return true;}
 std::vector<LaneBody> lane_bodies(offline_simulation::ActorKey)override{return {};}
 bool turret_clear(const AimPose&,const AimPose&)override{return true;}
 bool barrel_under_water(const Point&)override{return false;}
 bool vehicle_segment(const LaneBody&,const Point&,const Point&)override{return false;}
 bool debris_segment(const Point&,const Point&)override{return false;}
 bool static_segment_blocked(const Point&,const Point&)override{return false;}
 bool wreck_segment_blocked(const Point&,const Point&)override{return false;}
 std::optional<double> nominal_damage(const Point&,const Point&)override{return 10.;}
 ArcQuery segment(const Point&,const Point&,Point&)override{return ArcQuery::clear;}
};
int main(){
 p::Planner planner;p::Traffic traffic;stock_native_bot_strategy::Director director;
 offline_navigation::MapInput map;stock_native_bot_parking::Parking parking(map,{});d::Decisions decisions(planner,traffic,director,parking);
 offline_simulation::weapons::Store store;offline_simulation::weapons::GunConfig gun;
 gun.fully_aimed_dispersion=.2;gun.aiming_time=1.;gun.reload_full=1.;gun.clip_size=gun.shell_count=gun.burst_count=1;
 offline_simulation::weapons::AmmoState ammo;ammo.categories={0};ammo.remaining={3};offline_simulation::ActorKey key;key.id=1;
 store.install(key,gun,ammo,0,1.);auto& weapon=store.at(key);weapon.has_aim=true;weapon.aim_config.fixed_pitch=true;
 weapon.aim_config.fixed_limits={-.5,.5};weapon.aim_config.turret_speed=weapon.aim_config.gun_speed=1.;
 weapon.gun.elapsed=weapon.gun.reload_duration+1.;CombatState combat;combat.health=combat.maximum_health=100;SiegeState siege;
 std::vector<EquipmentState> equipment;CombatLedger ledger;CombatActorRuntime memory;CombatActorRefs refs{key,combat,siege,equipment,ledger,store,memory};
 CombatModeSnapshot mode;mode.gun_mount=GunMount{};mode.valid_pitch=true;mode.aim_config=weapon.aim_config;
 DirectShot shot;shot.speed=500.;shot.gravity=9.81;shot.maximum_distance=1000.;mode.shells={shot};
 WeaponActorState actor;actor.round_id=1;actor.team=1;actor.rating=1.;actor.class_tag="mediumTank";
 stock_native_bot_motion::State motion;motion.id=1;WeaponTarget target;target.target.kind="human";target.target.id=3;
 target.target.position={0.,0.,100.};target.pose.position=target.target.position;target.available=true;target.record_revision=target.descriptor_revision=1;
 AimSample sample;sample.point={0.,1.,0.};target.samples={sample};WeaponRuntime runtime;World world;int budget=1;
 assert(runtime.shotClear(actor,refs,mode,motion,target,0.,budget,true,world).value_or(false));
 p::Command command;command.fire_allowed=true;command.aim=target.target.position;command.mode="engage";
 CombatSlice slice;slice.elapsed=.1;runtime.preMotion(actor,refs,mode,motion,command,target,slice,decisions);
 d::Input input;input.planning.body.id=1;runtime.beginSlice(1);
 const auto result=runtime.postMotion(actor,refs,mode,motion,input,command,target,slice,true,true,0,100000,decisions,world);
 assert(result.fire.started && result.fire.committed==1 && weapon.ammo.remaining[0]==2);
}
