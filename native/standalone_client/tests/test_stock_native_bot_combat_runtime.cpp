#include "../bigworld/stock_native_bot_combat_runtime.hpp"
#include "../bigworld/stock_native_bot_environment.hpp"
#include <cassert>
using namespace wot::offline_bot_step;
namespace w=offline_simulation::weapons;
struct World:WeaponWorld {bool turret_clear(const AimPose&,const AimPose&) override {return true;}};
struct Scene:DirectCombatWorld {
    bool barrel_under_water(const Point&) override{return false;}
    bool vehicle_segment(const LaneBody&,const Point&,const Point&) override{return false;}
    bool debris_segment(const Point&,const Point&) override{return false;}
    bool static_segment_blocked(const Point&,const Point&) override{return false;}
    bool wreck_segment_blocked(const Point&,const Point&) override{return false;}
    std::optional<double> nominal_damage(const Point&,const Point&) override{return 10.;}
};
struct ArcScene:ArcWorld {ArcQuery segment(const Point&,const Point&,Point&) override{return ArcQuery::clear;}};
struct EnvironmentWorld:CombatWorld {double water_depth(const CombatPoint&) override{return -1.;}};
int main() {
    w::Store store;w::GunConfig gun;gun.fully_aimed_dispersion=.2;gun.aiming_time=1.;gun.reload_full=1.;
    gun.clip_size=gun.shell_count=gun.burst_count=1;w::AmmoState ammo;ammo.categories={0};ammo.remaining={3};
    offline_simulation::ActorKey key;key.id=1;store.install(key,gun,ammo,0,1.);
    auto& weapon=store.at(key);weapon.has_aim=true;weapon.aim_config.fixed_pitch=true;
    weapon.aim_config.fixed_limits={-.5,.5};weapon.aim_config.turret_speed=weapon.aim_config.gun_speed=1.;
    CombatState combat;combat.health=combat.maximum_health=100;SiegeState siege;
    std::vector<EquipmentState> equipment;CombatLedger ledger;CombatActorRuntime memory;
    CombatActorRefs actor{key,combat,siege,equipment,ledger,store,memory};CombatSlice slice;slice.elapsed=.1;
    assert(prepare_combat_weapon(actor,slice,0)==1.);
    DirectAimInput input;input.round_id=22;input.refresh_control=true;input.mount=GunMount{};
    DirectShot shell;shell.speed=500.;shell.gravity=9.81;shell.maximum_distance=1000.;input.shell=shell;
    GunnerTarget enemy;enemy.kind="human";enemy.id=5;enemy.position={0.,0.,100.};input.target=enemy;
    input.target_pose.position=enemy.position;input.signature.target_kind="human";input.signature.target_id=5;
    AimSample sample;sample.point={0.,1.,0.};input.selected_sample=sample;
    input.selected_token=DirectAimToken{1,2,0};
    WeaponFrame frame;frame.valid_pitch=true;World world;
    const auto first=advance_direct_aim(actor,slice,input,frame,{},world);
    assert(first.ballistic.fresh && first.ballistic.solution && first.aim.origin_available);
    assert(direct_aim_matches(memory,input.selected_token));
    weapon.aim.aligned=true;weapon.gun.elapsed=weapon.gun.reload_duration+1.;
    AimSelection selection;selection.samples={sample};selection.selected=0;
    DirectFireInput fire;fire.aim=input;fire.selection=&selection;fire.target_record_revision=1;fire.target_descriptor_revision=2;
    fire.publish=fire.fire_allowed=fire.source_available=fire.target_available=true;fire.end_us=100000;
    CombatSharedRuntime shared;shared.final_lane_budget=1;Scene scene;
    const auto fired=advance_direct_fire(actor,shared,slice,fire,first,frame,scene);
    assert(fired.fire.started && fired.fire.committed==1 && weapon.ammo.remaining[0]==2);
    slice.now=.01;const auto cached=advance_direct_aim(actor,slice,input,frame,{},world);
    assert(!cached.ballistic.fresh && cached.ballistic.solution);
    input.target.reset();input.signature.target_id.reset();input.signature.target_kind.clear();
    const auto no_target=advance_direct_aim(actor,slice,input,frame,{},world);
    assert(no_target.ballistic.fresh && !no_target.ballistic.solution);
    offline_simulation::ActorKey spg_key;spg_key.id=2;store.install(spg_key,gun,ammo,0,1.);
    auto& spg=store.at(spg_key);spg.has_aim=true;spg.aim_config.fixed_pitch=true;
    spg.aim_config.fixed_limits={-1.57,1.57};spg.aim_config.turret_speed=spg.aim_config.gun_speed=1.;
    spg.gun.elapsed=spg.gun.reload_duration+1.;CombatActorRuntime spg_memory;
    CombatActorRefs spg_actor{spg_key,combat,siege,equipment,ledger,store,spg_memory};
    ArtilleryAimInput artillery;artillery.round_id=22;artillery.target=enemy;artillery.mount=GunMount{};
    artillery.signature.target_kind="human";artillery.signature.target_id=5;artillery.refresh_control=true;artillery.fire_allowed=true;
    artillery.physical.identity.actor=2;artillery.physical.identity.target=5;artillery.physical.identity.target_kind="human";
    artillery.physical.source_available=artillery.physical.valid_pitch=true;artillery.physical.target=enemy.position;
    artillery.physical.speed=500.;artillery.physical.gravity=9.81;artillery.physical.maximum=1000.;
    artillery.physical.pitch_config=spg.aim_config;WeaponFrame spg_frame;spg_frame.valid_pitch=true;ArcScene arc;
    slice.now=0.;auto planning=advance_artillery_aim(spg_actor,shared,slice,artillery,spg_frame,{},world);
    assert(!planning.solution && shared.artillery.advance(0.,100,arc)>0);
    slice.now=.2;planning=advance_artillery_aim(spg_actor,shared,slice,artillery,spg_frame,{},world);
    assert(planning.solution && planning.aim.aligned);
    ArtilleryFireInput artillery_fire;artillery_fire.aim=artillery;artillery_fire.publish=true;artillery_fire.team=1;
    artillery_fire.end_us=200000;shared.final_lane_budget=1;
    auto pending=advance_artillery_fire(spg_actor,shared,slice,artillery_fire,planning,spg_frame,scene);
    assert(pending.proof.status==ArtilleryProofStatus::pending && spg_memory.artillery.intent);
    assert(shared.artillery.advance(.21,100,arc)>0);
    slice.now=.21;planning=advance_artillery_aim(spg_actor,shared,slice,artillery,spg_frame,{},world);
    auto launched=advance_artillery_fire(spg_actor,shared,slice,artillery_fire,planning,spg_frame,scene);
    assert(launched.fire.started && launched.fire.committed==1 && spg.ammo.remaining[0]==2);
    CombatState burning;burning.health=burning.maximum_health=100;burning.has_critical_payload=true;burning.critical.fire=true;
    burning.critical.devices["engineHealth"]={55.,100.,50.,false,false,100.};CombatActorRuntime fire_memory;
    EnvironmentTotals totals;totals.fire_provenance.insert(2);CombatActorRefs fire_actor{spg_key,burning,siege,equipment,ledger,store,fire_memory,&totals};
    CombatDescriptor descriptor;descriptor.water_sensor=Point{};CombatSlice fire_tick;fire_tick.elapsed=fire_tick.now=1.;EnvironmentWorld env_world;
    const auto critical=prepare_combat_actor(fire_actor,descriptor,fire_tick,env_world);
    assert(critical.alive && burning.critical.devices["engineHealth"].critical);
    assert(totals.critical[std::make_tuple(std::int64_t(2),std::string("fire"),std::uint32_t(1))]==1);
}
