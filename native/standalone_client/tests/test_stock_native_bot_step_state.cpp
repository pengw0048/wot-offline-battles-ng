#include "../bigworld/stock_native_bot_step_state.hpp"

#include <cassert>
#include <cmath>
#include <stdexcept>

using namespace wot::offline_bot_step;

struct FlatWorld : VerticalWorld {
    bool allow_turret=true;
    bool fail_ground=false;
    std::optional<double> basic=0.;
    std::optional<double> initial_ground(double,double,double) override { return 0.; }
    std::optional<double> basic_ground(double,double,double,std::optional<double> ceiling) override {
        return basic && (!ceiling || *basic<=*ceiling) ? basic : std::nullopt;
    }
    std::optional<double> ground(double,double,double low,double high,std::optional<double>) override {
        if (fail_ground) throw std::runtime_error("ground unavailable");
        return low<=0. && high>=0. ? std::optional<double>(0.) : std::nullopt;
    }
    bool turret_clear(const VerticalState&,const VerticalState&) override { return allow_turret; }
    std::array<double,2> passive_move(VerticalState& s,double x,double z,double) override {
        s.position[0]+=x; s.position[2]+=z; return {x,z};
    }
};

int main() {
    const double pi = std::acos(-1.0);
    AimPose pose;
    pose.position = {7, 3, -2};
    pose.yaw = 0.8; pose.pitch = -0.31; pose.roll = 0.27;
    pose.turret_yaw = -0.4; pose.gun_pitch = -0.2;
    GunMount mount{{0.2, 1.0, 0.3}, {0, 0.4, 0.6}, {0, 0.3, 1.2}};
    auto direction = shot_direction(pose);
    AimIntent world;
    world.world_yaw = std::atan2(direction[0], direction[2]);
    world.world_pitch = -std::atan2(direction[1], std::hypot(direction[0], direction[2]));
    auto local = local_aim_intent(pose, world);
    assert(std::abs(local.raw_yaw-pose.turret_yaw) < 1.e-12);
    assert(std::abs(local.raw_pitch-pose.gun_pitch) < 1.e-12);
    AimTarget target;
    pose.aim_yaw = 1.7;
    auto rest = world_aim_intent(pose, target, std::nullopt);
    assert(rest && rest->world_yaw == 1.7 && rest->world_pitch == 0);
    target.target_present = true;
    target.aim_position = {8, 3, 5};
    assert(!world_aim_intent(pose, target, std::nullopt));
    auto direct = world_aim_intent(pose, target, mount);
    const auto origin = shot_origin(pose, mount);
    assert(direct && std::abs(direct->world_yaw -
        std::atan2(8-origin[0], 5-origin[2])) < 1.e-12);
    target.solution_present = target.solution_origin_present = true;
    target.solution_origin = {0, 0, 0};
    target.solution_yaw = -0.3; target.solution_pitch = -0.5;
    auto ballistic = world_aim_intent(pose, target, std::nullopt);
    assert(ballistic && ballistic->world_yaw == -0.3 && ballistic->world_pitch == -0.5);
    assert(limited_traverse_target(-3, -1, 1, 1) == 1);
    assert(limited_traverse_target(3, -1, 1, -1) == -1);
    assert(limited_traverse_target(pi / 4, -1, 1, -1) == pi / 4);
    const auto braked=partition_contact_velocity({4.,0.,0.,0.,false},0.,true,0.,-2.);
    assert(braked.speed==2. && braked.forward==0. && braked.push_z==0.);
    const auto shoved=partition_contact_velocity({4.,0.,0.,0.,false},0.,true,1.,-6.);
    assert(shoved.speed==-2. && shoved.forward==-2. && shoved.push_x==1.);
    const auto wreck=partition_contact_velocity({0.,0.,0.,0.,false},0.,false,1.,-2.);
    assert(wreck.speed==0. && wreck.push_x==1. && wreck.push_z==-2.);

    SuspensionContact rigid;
    rigid.x=1.; rigid.y=-.5; rigid.penetration=.01; rigid.rigid=true;
    const auto level = suspension_pose_contacts({}, {rigid}, Point{0., 1., 0.},
        {{1., 0., 0.}}, 0., 0., 0., 0., .1, pi/2);
    assert(!level.expanded && level.contacts.empty());
    const auto tipped = suspension_pose_contacts({}, {rigid}, Point{0., 1., 0.},
        {{1., 0., 0.}}, 0., 0., 0., 2., .3, pi/2);
    assert(tipped.expanded && tipped.contacts.size() == 2);
    assert(std::abs(tipped.contacts[1].z+1.) < 1.e-12 && tipped.contacts[1].rigid);

    const std::array<double,5> params{10000.,10000.,10000.,.02,20.};
    const std::array<double,6> state{.5,-1.,0.,0.,.2,0.};
    const std::array<double,9> tuning{12.2625,.1,4.,.6,.4,.15,.35,.06,12.};
    rigid.ground=0.;
    const auto landing=suspension_solve(params,state,{rigid},.1,0.,tuning,12);
    // Fixed gameplay Python _reference_damper_suspension_step: a single edge
    // contact must not put a tipping hull to sleep.
    assert(landing.contacts==1 && landing.rigid==1 && landing.impact_speed);
    assert(std::abs(landing.state[0]-.326425)<1.e-12);
    assert(std::abs(landing.state[1]+2.22625)<1.e-12);
    assert(std::abs(*landing.impact_speed+1.981)<1.e-12);
    rigid.ground.reset();
    const auto airborne=suspension_solve(params,state,{rigid},.1,0.,tuning,12);
    assert(airborne.contacts==0 && !airborne.impact_speed);
    assert(std::abs(airborne.state[0]-.326425)<1.e-12);
    VerticalState body;
    body.position={0.,.5,0.}; body.terrain_pitch=body.pitch=0.;
    body.roll=.2; body.vertical_speed=-1.;
    VerticalState spawn=body;
    assert(!prepare_vertical_spawn(spawn,std::nullopt) && spawn.position[1]==.5);
    assert(prepare_vertical_spawn(spawn,0.) && spawn.position[1]==0. && spawn.roll==0.);
    auto pending=vertical_trial(body,body.position,std::nullopt,std::nullopt,
        {rigid},params,tuning,12,.1);
    auto pending_result=commit_vertical(body,pending,false);
    assert(pending_result.pending_spawn && !body.airborne && body.vertical_speed==0.);
    body.grounded_once=true; body.airborne=true; body.vertical_speed=-1.;
    rigid.ground=0.;
    auto trial=vertical_trial(body,body.position,std::nullopt,std::nullopt,
        {rigid},params,tuning,12,.1);
    auto settled=commit_vertical(body,trial,false);
    assert(!settled.blocked && settled.landing_impact &&
           std::abs(*settled.landing_impact-1.981)<1.e-12);
    assert(settled.normal_impact && !settled.ground_observed);
    assert(!body.airborne && std::abs(body.position[1]-.326425)<1.e-12);
    VerticalTrial raised=trial;
    raised.original=body; raised.tick_position={-1.,.4,0.};
    raised.invalid=false; raised.raised=true;
    raised.solved.state[0]=1.5;
    auto rejected=commit_vertical(body,raised,false);
    assert(rejected.blocked && rejected.clear_contact_memory && body.position[0]==-1.);
    raised.original=body;
    auto accepted=commit_vertical(body,raised,true);
    assert(!accepted.blocked && body.position[1]==1.5);
    SupportPlane plane;
    plane.gradient_x=.1; plane.normal={-.1,1.,0.};
    const auto checkpoints=support_checkpoints(plane,plane,{0.,0.,0.},{3.,.3,0.},.9);
    assert(checkpoints && checkpoints->size()==2);
    assert(support_checkpoints_proved(*checkpoints,{.1,.2}));
    assert(!support_checkpoints_proved(*checkpoints,{.1,std::nullopt}));
    assert(!support_checkpoints_proved(*checkpoints,{.1,.7}));
    auto upper=plane; upper.center_y=1.;
    assert(!support_checkpoints(plane,upper,{0.,0.,0.},{3.,.3,0.},.9));
    auto memory=retain_ground(0.,0.,2.,std::nullopt,1.,std::nullopt);
    assert(memory.height && *memory.height==2. && memory.memory);
    auto carry=retain_ground(.2,0.,std::nullopt,memory.memory,1.,std::array<double,2>{.5,0.});
    assert(carry.height && std::abs(*carry.height-2.1)<1.e-12);
    auto stationary=retain_ground(.2,0.,std::nullopt,carry.memory,1.,std::array<double,2>{.5,0.});
    assert(!stationary.height && !stationary.memory);
    auto unsupported=retain_ground(.2,0.,std::nullopt,memory.memory,1.,std::nullopt);
    assert(!unsupported.height);
    SuspensionDescriptor desc;
    desc.parameters=params; desc.tuning=tuning; desc.maximum_steps=12;
    desc.width=2.; desc.clearance=.5; desc.rest_length=.5;
    for (int x : {-1,1}) for (int z : {-1,1}) {
        SuspensionContact c; c.spring=true; c.x=x; c.z=z; c.side=x;
        c.compression=.1; c.maximum=.3; c.rest_length=.5;
        c.stiffness=40000.; c.damping=5000.; c.force=100000.;
        desc.springs.push_back(c);
    }
    FlatWorld flat_world;
    VerticalState flat_body; flat_body.position={0.,.1,0.}; flat_body.grounded_once=true;
    VerticalMemory frame_memory;
    auto result=advance_vertical(flat_body,frame_memory,desc,flat_world,0.,0.,flat_body.position,
        flat_body.position,.1);
    assert(!result.blocked && flat_body.ground_plane && frame_memory.springs.size()==4);
    assert(std::abs(flat_body.position[1]-.03127475599375855)<1.e-12);
    assert(std::abs(flat_body.vertical_speed+1.1026449740556878)<1.e-12);
    auto original=flat_body; auto old_memory=frame_memory;
    flat_world.allow_turret=false;
    result=advance_vertical(flat_body,frame_memory,desc,flat_world,0.,0.,flat_body.position,
        flat_body.position,.1);
    assert(result.blocked && flat_body.position==original.position &&
        frame_memory.springs.size()==old_memory.springs.size());
    flat_world.allow_turret=true; flat_world.fail_ground=true;
    result=advance_vertical(flat_body,frame_memory,desc,flat_world,0.,0.,flat_body.position,
        flat_body.position,.1);
    assert(result.blocked && result.suspension_retired && flat_body.suspension_disabled &&
        frame_memory.springs.empty());
    flat_world.fail_ground=false;
    VerticalState basic; basic.position={0.,0.,0.}; basic.grounded_once=true;
    int budget=1;
    auto basic_result=advance_basic_vertical(basic,desc,flat_world,basic.position,.1,budget);
    assert(!basic_result.blocked && !basic.airborne && basic.position[1]==0.);
    flat_world.basic.reset(); basic.position[1]=1.;
    basic_result=advance_basic_vertical(basic,desc,flat_world,basic.position,.1,budget);
    assert(basic.airborne && std::abs(basic.position[1]-.877375)<1.e-12 &&
        std::abs(basic.vertical_speed+1.22625)<1.e-12);
    flat_world.basic=0.;basic.position[1]=-.01;basic.vertical_speed=-1.;
    basic_result=advance_basic_vertical(basic,desc,flat_world,basic.position,.1,budget);
    assert(basic_result.landing_impact && !basic_result.normal_impact && basic_result.ground_observed);
    VerticalState raised_body; raised_body.grounded_once=true;
    flat_world.basic=2.;
    basic_result=advance_basic_vertical(raised_body,desc,flat_world,raised_body.position,.1,budget);
    assert(basic_result.blocked && basic_result.invalidate_motion && raised_body.position[1]==0.);
}
