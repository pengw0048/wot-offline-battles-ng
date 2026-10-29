#include "../bigworld/stock_native_bot_weapon.hpp"
#include <cassert>
#include <cmath>
using namespace wot::offline_bot_step;
namespace weapons=offline_simulation::weapons;
struct World : WeaponWorld {bool clear=true;
    bool turret_clear(const AimPose&,const AimPose&) override {return clear;}
};
struct Scene : DirectWorld {
    bool water=false,collision=false,debris=false;
    bool barrel_under_water(const Point&) override {return water;}
    bool vehicle_segment(const LaneBody&,const Point&,const Point&) override {return collision;}
    bool debris_segment(const Point&,const Point&) override {return debris;}
};
struct SelectionScene : AimSelectionWorld {
    int rays=0;bool block_first=false;
    bool static_segment_blocked(const Point&,const Point&) override {return block_first && rays++==0;}
    bool wreck_segment_blocked(const Point&,const Point&) override {return false;}
    std::optional<double> nominal_damage(const Point&,const Point& end) override {return end[1];}
};
int main() {
    AimBox box;box.lower={-2.,0.,-1.};box.upper={2.,2.,1.};
    AimSelection selected;selected.samples=vehicle_aim_samples({box});assert(selected.samples.size()==4);
    AimBox hull=box;hull.mount={1.,2.,3.};AimBox turret;
    turret.lower={-1.,-1.,-1.};turret.upper={1.,1.,1.};turret.mount={1.,3.,3.};turret.turret=true;
    const auto samples=vehicle_aim_samples({hull,turret});assert(samples.size()==8 && samples[1].turret);
    AimPose pose;pose.position={4.,5.,6.};pose.yaw=.3;pose.pitch=.2;pose.roll=-.1;pose.turret_yaw=.6;
    const auto centre=vehicle_aim_point(samples[1],pose);
    assert(std::abs(centre[0]-6.274966912838334)<1.e-12 && std::abs(centre[1]-7.231659594213008)<1.e-12 && std::abs(centre[2]-8.973923502891063)<1.e-12);
    const auto sample_edge=vehicle_aim_point(samples[5],pose);
    assert(std::abs(sample_edge[0]-5.966887161552977)<1.e-12 && std::abs(sample_edge[1]-7.215947842340661)<1.e-12 && std::abs(sample_edge[2]-9.36742065370801)<1.e-12);
    SelectionScene selection_world;selection_world.block_first=true;GunnerTarget selection_target;selection_target.kind="human";selection_target.id=5;
    assert(select_exposed_aim(selected,{}, {},1.,22,3,selection_target,{},selection_world));
    assert(selected.selected && *selected.selected==1 && selected.cursor==2);
    weapons::Store store;weapons::GunConfig gun;
    gun.fully_aimed_dispersion=.2;gun.aiming_time=1.;gun.reload_full=1.;
    gun.clip_size=gun.shell_count=gun.burst_count=1;
    weapons::AmmoState ammo;ammo.categories={0};ammo.remaining={10};
    offline_simulation::ActorKey key;key.id=1;store.install(key,gun,ammo,0,1.);
    auto& actor=store.at(key);actor.has_aim=true;actor.aim_config.fixed_pitch=true;
    actor.aim_config.fixed_limits={-.5,.5};actor.aim_config.turret_speed=1.;actor.aim_config.gun_speed=1.;
    WeaponFrame frame;frame.elapsed=.1;frame.valid_pitch=true;
    AimTarget target;target.target_present=true;target.aim_position={0.,0.,100.};
    frame.target=true;World world;HydraulicConfig hydraulic;
    const auto missing=advance_weapon_aim(store,key,frame,target,std::nullopt,hydraulic,world);
    assert(!missing.origin_available && !missing.aligned);
    GunMount mount;
    const auto aimed=advance_weapon_aim(store,key,frame,target,mount,hydraulic,world);
    assert(aimed.origin_available && std::abs(frame.pose.gun_pitch+.009999666686665238)<1.e-12);
    hydraulic={true,true,true,-.3,.3,1.};frame.siege_enabled=true;
    target.solution_present=true;target.solution_origin_present=true;
    target.solution_pitch=-.7;target.solution_yaw=0.;
    world.clear=false;const double previous=frame.pose.pitch;
    const auto blocked=advance_weapon_aim(store,key,frame,target,mount,hydraulic,world);
    assert(frame.pose.pitch==previous && blocked.hydraulic_pitch==0.);
    world.clear=true;
    const auto hydraulic_result=advance_weapon_aim(store,key,frame,target,mount,hydraulic,world);
    assert(hydraulic_result.hydraulic_reachable && std::abs(frame.hydraulic_pitch+.1)<1.e-12);
    actor.aim.aligned=true;actor.gun.elapsed=actor.gun.reload_duration+.01;
    FireConditions fire;fire.publish=fire.action_fresh=fire.fire_allowed=fire.has_target=true;
    fire.has_ballistic=fire.shot_clear=fire.gunner_ready=true;fire.target_distance=100.;
    Scene shot_scene;DirectShot direct_fire;direct_fire.speed=500.;direct_fire.gravity=9.81;
    direct_fire.maximum_distance=1000.;direct_fire.flight_time=.2;direct_fire.dispersion=.1;
    const auto fired=advance_weapon_fire(store,key,frame,fire,{},0,100000,mount,direct_fire,{},shot_scene,std::nullopt);
    assert(fired.started && fired.committed==1 && actor.fire_seq==1 && actor.ammo.remaining[0]==9);
    assert(store.pending().size()==1 && store.pending().front().launch_time_us==100000);
    GunnerState memory;GunnerConfig skill{.75,1.1,1.8,.85,.22};
    GunnerTarget enemy;enemy.kind="human";enemy.id=5;enemy.position={0.,0.,100.};
    auto gunner=advance_gunner(memory,skill,enemy,{0.,0.,0.},.2,1.,22,3,0,0.);
    assert(gunner && !gunner->ready);
    assert(std::abs(memory.radius-.6591861817958783)<1.e-15);
    assert(std::abs(memory.azimuth-5.2640553440229425)<1.e-15);
    assert(std::abs(memory.lead_scale-1.025569467430187)<1.e-15);
    gunner=advance_gunner(memory,skill,enemy,{0.,0.,0.},.2,1.,22,3,0,.8);
    assert(gunner && gunner->ready);
    BallisticInput physical;physical.origin={0.,0.,0.};physical.target={0.,0.,100.};
    physical.speed=500.;physical.gravity=9.81;physical.maximum_distance=1000.;
    WeaponFrame ballistic_frame;ballistic_frame.valid_pitch=true;
    BallisticCache cache;BallisticSignature signature;signature.target_id=5;
    auto ball=advance_direct_ballistic(cache,signature,physical,ballistic_frame,{},actor.aim_config,1,0.,true,false);
    assert(ball.fresh && ball.solution);
    ball=advance_direct_ballistic(cache,signature,physical,ballistic_frame,{},actor.aim_config,1,1.,false,false);
    assert(!ball.fresh && ball.solution);
    ++signature.fire_sequence;
    ball=advance_direct_ballistic(cache,signature,physical,ballistic_frame,{},actor.aim_config,1,1.,false,false);
    assert(ball.fresh && ball.solution);
    Scene scene;DirectShot direct;direct.round_id=22;direct.team=1;direct.speed=500.;direct.gravity=9.81;
    direct.maximum_distance=1000.;direct.flight_time=.2;direct.dispersion=.1;
    weapons::BurstEdge edge;edge.shot_seq=edge.group_seq=1;edge.count=1;edge.final_round=true;
    auto proof=prove_direct_launch(key,AimPose{},mount,direct,edge,100,{},scene);
    assert(proof.clear && proof.launch);
    assert(std::abs(proof.launch->yaw+0.0027530548912467682)<1.e-14);
    assert(std::abs(proof.launch->pitch-0.004268068853337199)<1.e-14);
    LaneBody ally;ally.id=2;ally.team=1;ally.position={0.,0.,10.};scene.collision=true;
    proof=prove_direct_launch(key,AimPose{},mount,direct,edge,100,{ally},scene);
    assert(!proof.clear && proof.has_blocker && proof.blocker.id==2);
    ally.alive=false;
    proof=prove_direct_launch(key,AimPose{},mount,direct,edge,100,{ally},scene);
    assert(!proof.clear && !proof.has_blocker);
}
