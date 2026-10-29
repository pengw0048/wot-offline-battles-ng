#include "stock_native_bot_combat_runtime.hpp"
#include "stock_native_bot_environment.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wot::offline_bot_step {
CombatPreparation prepare_combat_actor(CombatActorRefs actor,const CombatDescriptor& descriptor,
    const CombatSlice& slice,CombatWorld& world) {
    CombatPreparation result;
    auto& c=actor.combat;
    const bool has_critical=c.has_critical_payload || c.critical.fire || c.critical.ammo_rack_death || !c.critical.crew_ko.empty() ||
        std::any_of(c.critical.devices.begin(),c.critical.devices.end(),[](const auto& p){return p.second.hp || p.second.destroyed;});
    if (has_critical || (c.alive && c.stun_until_equipment_time>slice.equipment_now+1.e-9)) {
        const bool was_fire=c.critical.fire;
        for (auto& equipment:actor.equipment) {
            const bool stunned=c.alive && c.stun_until_equipment_time>slice.equipment_now+1.e-9;
            auto effect=poll_bot_equipment(equipment,c,slice.equipment_now,stunned);
            if (effect) result.equipment_effects.push_back(*effect);
        }
        const double repair=descriptor.base_repair_factor*(1.+std::max(0.,equipment_passives(actor.equipment).repair_bonus));
        const auto changed=advance_critical(c,slice.elapsed,slice.now,repair,descriptor.repair_speed_factor);
        if (actor.environment && actor.environment->fire_provenance.count(actor.key.id)) {
            if (changed.fire_before && changed.fire_after)accumulate_environment_critical(*actor.environment,
                actor.key.id,"fire",*changed.fire_before,*changed.fire_after);
            if (!c.critical.fire && changed.fire_damage<=0)actor.environment->fire_provenance.erase(actor.key.id);
        }
        result.events=changed.events;
        if (changed.changed || was_fire || c.critical.fire || !result.equipment_effects.empty()) {
            CombatReplayStep replay;replay.elapsed=slice.elapsed;replay.now=slice.now;
            replay.advance_fire=was_fire;replay.equipment=result.equipment_effects;
            actor.ledger.unpublished_steps.push_back(std::move(replay));
        }
    }
    if (!c.alive) {actor.weapons.cancel(actor.key);return result;}
    if (!descriptor.water_sensor && c.drown_check+slice.elapsed>=.3)
        throw std::invalid_argument("Water sensor descriptor is unavailable");
    if (descriptor.water_sensor) {
        const auto before=c.critical;
        const auto drowning=advance_drowning(c,*descriptor.water_sensor,slice.elapsed,world);
        if (drowning.terminal!=TerminalCause::none) {result.events=drowning.events;
            if(actor.environment)accumulate_environment_critical(*actor.environment,actor.key.id,"drowning",before,c.critical,true);}
    }
    if (!c.alive) {actor.weapons.cancel(actor.key);return result;}
    const auto overturn=advance_overturn(c,slice.elapsed);
    if (overturn.terminal!=TerminalCause::none) result.events=overturn.events;
    if (!c.alive) {actor.weapons.cancel(actor.key);return result;}
    const bool initially_switching=actor.siege.mode==1 || actor.siege.mode==3;
    const auto siege=advance_siege(actor.siege,descriptor.siege,slice.elapsed);
    result.siege_motion_locked=initially_switching || siege.locked;
    result.siege_descriptor_changed=siege.descriptor_changed;
    result.alive=true;return result;
}
SiegeResult prepare_combat_siege(CombatActorRefs actor,const CombatDescriptor& descriptor,
    const CombatSlice& slice,SiegeCommand& command) {
    const auto& weapon=actor.weapons.at(actor.key);
    const auto engine=actor.combat.critical.devices.find("engineHealth");
    const bool destroyed=engine!=actor.combat.critical.devices.end() && engine->second.destroyed;
    const bool damaged=engine!=actor.combat.critical.devices.end() && !destroyed && engine->second.critical;
    return update_siege_intent(actor.siege,descriptor.siege,command,slice.elapsed,
        actor.combat.speed,weapon.burst.active,destroyed,damaged);
}
double prepare_combat_weapon(CombatActorRefs actor,const CombatSlice& slice,int requested_shell) {
    const double reload=critical_factor(actor.combat.critical,CriticalStat::reload);
    actor.weapons.prepare(actor.key,slice.elapsed,reload,requested_shell);
    return reload;
}
DirectAimResult advance_direct_aim(CombatActorRefs actor,const CombatSlice& slice,
    const DirectAimInput& input,WeaponFrame& frame,const HydraulicConfig& hydraulic,WeaponWorld& world) {
    auto& weapon=actor.weapons.at(actor.key);auto& cache=actor.memory.ballistic;
    frame.elapsed=slice.elapsed;frame.target=input.target.has_value();
    frame.turret_factor=critical_factor(actor.combat.critical,CriticalStat::turret_speed);
    frame.dispersion_factor=critical_factor(actor.combat.critical,CriticalStat::dispersion);
    frame.aim_factor=critical_factor(actor.combat.critical,CriticalStat::aim_time);
    const bool fresh=input.force || !cache.initialized || !(cache.signature==input.signature) ||
        (input.refresh_control && slice.now+1.e-9>=cache.deadline);
    BallisticInput physical;
    bool can_solve=false;
    if (fresh && input.target) {
        const auto gunner=advance_gunner(actor.memory.gunner,input.gunner,input.target,frame.pose.position,
            weapon.gun.config.fully_aimed_dispersion,weapon.gun.current_dispersion_factor,
            input.round_id,actor.key.id,weapon.fire_seq,slice.now);
        if (gunner && input.mount && input.shell && input.selected_sample) {
            auto target_pose=input.target_pose;target_pose.position=gunner->aimed.position;
            physical.origin=shot_origin(frame.pose,*input.mount);
            physical.target=vehicle_aim_point(*input.selected_sample,target_pose);
            physical.velocity=gunner->aimed.velocity;physical.speed=input.shell->speed;
            physical.gravity=input.shell->gravity;physical.maximum_distance=input.shell->maximum_distance;
            can_solve=true;
        }
    }
    DirectAimResult result;
    if (fresh && !can_solve) {
        actor.memory.direct_aim_token.reset();
        const bool stagger=!cache.initialized;cache.initialized=true;cache.signature=input.signature;
        cache.solution.reset();cache.deadline=slice.now+.1;
        if (stagger) {const auto id=actor.key.id<0?std::uint64_t(-(actor.key.id+1))+1:std::uint64_t(actor.key.id);
            cache.deadline+=double(((id%29)*17+13*11)%29)/29.*.1;}
        result.ballistic={true,std::nullopt};
    } else if (fresh) {result.ballistic=advance_direct_ballistic(cache,input.signature,physical,frame,hydraulic,
        weapon.aim_config,actor.key.id,slice.now,input.refresh_control,input.force);
        actor.memory.direct_aim_token=result.ballistic.solution?input.selected_token:std::nullopt;
    }
    else result.ballistic={false,cache.solution};
    AimTarget aim;
    if (result.ballistic.solution) aim=*result.ballistic.solution;
    else if (input.target) {aim.target_present=true;aim.aim_position=input.command_aim_position.value_or(input.target->position);}
    result.aim=advance_weapon_aim(actor.weapons,actor.key,frame,aim,input.mount,hydraulic,world);
    return result;
}
bool direct_aim_matches(CombatActorRuntime& memory,const std::optional<DirectAimToken>& current) {
    const bool same=memory.ballistic.solution && current && memory.direct_aim_token && *current==*memory.direct_aim_token;
    if (!same) {memory.ballistic.initialized=false;memory.ballistic.solution.reset();memory.direct_aim_token.reset();}
    return same;
}
DirectFireResult advance_direct_fire(CombatActorRefs actor,CombatSharedRuntime& shared,const CombatSlice& slice,
    const DirectFireInput& input,const DirectAimResult& aimed,const WeaponFrame& frame,DirectCombatWorld& world) {
    DirectFireResult result;auto& weapon=actor.weapons.at(actor.key);
    FireConditions conditions;conditions.alive=actor.combat.alive;conditions.drowning=actor.combat.drowning;
    conditions.overturned=actor.combat.overturned;
    const auto gun=actor.combat.critical.devices.find("gunHealth");
    conditions.gun_destroyed=gun!=actor.combat.critical.devices.end() && gun->second.destroyed;
    conditions.siege_switching=actor.siege.mode==1 || actor.siege.mode==3;
    conditions.reload_factor=critical_factor(actor.combat.critical,CriticalStat::reload);
    conditions.dispersion_factor=critical_factor(actor.combat.critical,CriticalStat::dispersion);
    if (!input.aim.mount || !input.aim.shell) {
        if (!aimed.aim.edges.empty()) {actor.weapons.cancel(actor.key);result.fire.cancelled=true;}
        return result;
    }
    auto shot=*input.aim.shell;
    shot.flight_time=aimed.ballistic.solution?aimed.ballistic.solution->flight_time:
        (shot.speed>0. && shot.maximum_distance>0.?std::min(20.,shot.maximum_distance/shot.speed):0.);
    result.fire=advance_weapon_fire(actor.weapons,actor.key,frame,conditions,aimed.aim.edges,input.start_us,
        input.end_us,*input.aim.mount,shot,input.roster,world,std::nullopt);
    if (!input.aim.target)return result;
    const auto& target=*input.aim.target;double distance2=0.;
    for (int i=0;i<3;++i) {const double d=target.position[i]-frame.pose.position[i];distance2+=d*d;}
    const double distance=std::sqrt(distance2);
    const bool range=distance>1. && aimed.ballistic.solution && (input.fire_range<=0. || distance<input.fire_range);
    if (!(conditions.alive && input.publish && aimed.ballistic.fresh && input.fire_allowed && range && !conditions.drowning &&
          !conditions.overturned && !conditions.gun_destroyed && weapon.aim.aligned && !weapon.burst.active &&
          weapon.gun.ready(conditions.reload_factor) && weapon.ammo.can_fire())) return result;
    const auto key=std::make_tuple(actor.key.id,target.kind,target.id);
    auto cached=shared.lanes.find(key);
    if (distance>585.) {shared.lanes[key]={slice.now,false};result.shot_clear=false;}
    else if (cached!=shared.lanes.end() && slice.now-cached->second.at<=.20+1.e-9)
        result.shot_clear=cached->second.clear;
    else if (shared.final_lane_budget>0) {
        --shared.final_lane_budget;bool clear=false;
        try {
            if (input.selection && input.source_available && input.target_available && !world.barrel_under_water(input.aim.shell->barrel_point))
                clear=select_exposed_aim(*input.selection,input.aim.target_pose,shot_origin(frame.pose,*input.aim.mount),
                    input.rating,input.aim.round_id,actor.key.id,target,actor.memory.gunner,world);
            else if (input.selection)input.selection->selected.reset();
        } catch (...) {if (input.selection)input.selection->selected.reset();}
        shared.lanes[key]={slice.now,clear};result.shot_clear=clear;
    }
    if (!result.shot_clear || !*result.shot_clear)return result;
    auto gunner=advance_gunner(actor.memory.gunner,input.aim.gunner,input.aim.target,frame.pose.position,
        weapon.gun.config.fully_aimed_dispersion,weapon.gun.current_dispersion_factor,
        input.aim.round_id,actor.key.id,weapon.fire_seq,slice.now);
    if (!gunner || !gunner->ready)return result;
    std::optional<DirectAimToken> current;
    if (input.source_available && input.target_available && input.selection && input.selection->selected)
        current=DirectAimToken{input.target_record_revision,input.target_descriptor_revision,*input.selection->selected};
    if (!direct_aim_matches(actor.memory,current))return result;
    conditions.publish=input.publish;conditions.action_fresh=aimed.ballistic.fresh;conditions.fire_allowed=input.fire_allowed;
    conditions.has_target=true;conditions.has_ballistic=true;conditions.target_distance=distance;conditions.fire_range=input.fire_range;
    conditions.shot_clear=true;conditions.gunner_ready=true;
    auto fired=advance_weapon_fire(actor.weapons,actor.key,frame,conditions,{},input.start_us,input.end_us,
        *input.aim.mount,shot,input.roster,world,std::nullopt);
    result.fire.committed+=fired.committed;result.fire.started=fired.started;
    result.fire.cancelled=result.fire.cancelled || fired.cancelled;if (fired.blocker)result.fire.blocker=fired.blocker;
    return result;
}
ArtilleryHold prepare_artillery_hold(CombatActorRefs actor,CombatSharedRuntime& shared,const CombatSlice& slice,
    const ArtilleryAimInput& input) {
    auto& state=actor.memory.artillery;ArtilleryHold hold;
    if (!input.fire_allowed) {state={};shared.artillery.cancel_launch(actor.key.id);return hold;}
    const bool alive=input.target && input.signature.target_alive;
    if (!artillery_reproof_current(state,shared.artillery,input.physical,
        actor.weapons.at(actor.key).fire_seq+1,alive,slice.now))return hold;
    if (state.intent) hold.aim_position=state.intent->solution.aim;
    else if (state.reproof->hold_solution)hold.aim_position=state.reproof->hold_solution->aim;
    hold.brake=true;return hold;
}
ArtilleryAimResult advance_artillery_aim(CombatActorRefs actor,CombatSharedRuntime& shared,const CombatSlice& slice,
    const ArtilleryAimInput& input,WeaponFrame& frame,const HydraulicConfig& hydraulic,WeaponWorld& world) {
    auto& memory=actor.memory;auto& cache=memory.ballistic;auto& weapon=actor.weapons.at(actor.key);
    frame.elapsed=slice.elapsed;frame.target=input.target.has_value();
    frame.turret_factor=critical_factor(actor.combat.critical,CriticalStat::turret_speed);
    frame.dispersion_factor=critical_factor(actor.combat.critical,CriticalStat::dispersion);
    frame.aim_factor=critical_factor(actor.combat.critical,CriticalStat::aim_time);
    const bool fresh=input.force || memory.artillery.intent || memory.artillery.reproof || !cache.initialized ||
        !(cache.signature==input.signature) || (input.refresh_control && slice.now+1.e-9>=cache.deadline);
    if (fresh) {
        auto physical=input.physical;physical.pose=frame.pose;
        if (input.mount)physical.origin=shot_origin(frame.pose,*input.mount);
        if (input.target) {
            const auto gunner=advance_gunner(memory.gunner,input.gunner,input.target,frame.pose.position,
                weapon.gun.config.fully_aimed_dispersion,weapon.gun.current_dispersion_factor,
                input.round_id,actor.key.id,weapon.fire_seq,slice.now);
            if (gunner) {physical.target=gunner->aimed.position;physical.velocity=gunner->aimed.velocity;}
        }
        const bool current=artillery_reproof_current(memory.artillery,shared.artillery,physical,
            weapon.fire_seq+1,input.target && input.signature.target_alive,slice.now);
        memory.artillery_solution.reset();
        if (current && memory.artillery.intent)memory.artillery_solution=memory.artillery.intent->solution;
        else if (current && memory.artillery.reproof->attempts)
            memory.artillery_solution=artillery_reproof_solution(physical,*memory.artillery.reproof,frame,hydraulic);
        else if (input.target) {
            const auto plan=shared.artillery.plan(physical,slice.now);memory.artillery_planning=plan;
            if (plan.ready && plan.candidate && world_solution_reachable(frame,hydraulic,weapon.aim_config,
                plan.candidate->yaw,plan.candidate->pitch))memory.artillery_solution=plan.candidate;
        }
        const bool stagger=!cache.initialized;cache.initialized=true;cache.signature=input.signature;cache.deadline=slice.now+.1;
        if (stagger) {const auto id=actor.key.id<0?std::uint64_t(-(actor.key.id+1))+1:std::uint64_t(actor.key.id);
            cache.deadline+=double(((id%29)*17+13*11)%29)/29.*.1;}
    }
    ArtilleryAimResult result;result.fresh=fresh;result.solution=memory.artillery_solution;result.planning=memory.artillery_planning;
    std::optional<ArcCandidate> laying=result.solution;
    if (input.target && laying) {memory.artillery_continuity=laying;memory.artillery_continuity_signature=input.signature;}
    else if (input.target && memory.artillery_continuity && memory.artillery_continuity_signature==input.signature) {
        laying=memory.artillery_continuity;result.planning_pending=true;
    } else memory.artillery_continuity.reset();
    AimTarget target;
    if (laying) {target.target_present=target.solution_present=target.solution_origin_present=true;
        target.aim_position=laying->aim;target.solution_origin=laying->origin;target.solution_yaw=laying->yaw;
        target.solution_pitch=laying->pitch;target.flight_time=laying->flight_time;}
    else if (input.target) {target.target_present=true;target.aim_position=input.command_aim_position.value_or(input.target->position);}
    frame.planning_pending=result.planning_pending;
    result.aim=advance_weapon_aim(actor.weapons,actor.key,frame,target,input.mount,hydraulic,world);
    return result;
}
ArtilleryFireResult advance_artillery_fire(CombatActorRefs actor,CombatSharedRuntime& shared,const CombatSlice& slice,
    const ArtilleryFireInput& input,const ArtilleryAimResult& aimed,const WeaponFrame& frame,DirectWorld& world) {
    ArtilleryFireResult result;auto& weapon=actor.weapons.at(actor.key);
    FireConditions c;c.alive=actor.combat.alive;c.drowning=actor.combat.drowning;c.overturned=actor.combat.overturned;
    const auto gun=actor.combat.critical.devices.find("gunHealth");
    c.gun_destroyed=gun!=actor.combat.critical.devices.end() && gun->second.destroyed;
    c.siege_switching=actor.siege.mode==1 || actor.siege.mode==3;
    c.reload_factor=critical_factor(actor.combat.critical,CriticalStat::reload);
    c.dispersion_factor=critical_factor(actor.combat.critical,CriticalStat::dispersion);
    if (!input.aim.mount) {if (!aimed.aim.edges.empty()) {actor.weapons.cancel(actor.key);result.fire.cancelled=true;}return result;}
    auto physical=input.aim.physical;physical.pose=frame.pose;physical.identity.fire_sequence=weapon.fire_seq;
    physical.origin=shot_origin(frame.pose,*input.aim.mount);
    DirectShot shot;shot.round_id=input.aim.round_id;shot.team=input.team;shot.speed=physical.speed;
    shot.gravity=physical.gravity;shot.maximum_distance=physical.maximum;shot.barrel_point=physical.barrel_point;
    shot.splash_radius=input.splash_radius;shot.class_tag="SPG";
    shot.flight_time=aimed.solution?aimed.solution->flight_time:
        (shot.speed>0. && shot.maximum_distance>0.?std::min(20.,shot.maximum_distance/shot.speed):0.);
    result.fire=advance_weapon_fire(actor.weapons,actor.key,frame,c,aimed.aim.edges,input.start_us,input.end_us,
        *input.aim.mount,shot,input.roster,world,std::nullopt);
    if (!input.aim.target)return result;
    const auto& target=*input.aim.target;double d2=0.;
    for (int i=0;i<3;++i) {const double d=target.position[i]-frame.pose.position[i];d2+=d*d;}
    const double distance=std::sqrt(d2);
    if (!(c.alive && input.publish && aimed.fresh && input.aim.fire_allowed && distance>1. && aimed.solution &&
          (input.fire_range<=0. || distance<input.fire_range) && !c.drowning && !c.overturned && !c.gun_destroyed &&
          weapon.aim.aligned && !weapon.burst.active && weapon.gun.ready(c.reload_factor) && weapon.ammo.can_fire()))return result;
    const auto key=std::make_tuple(actor.key.id,target.kind,target.id);
    if (actor.memory.artillery.intent || actor.memory.artillery.reproof)result.shot_clear=true;
    else if (distance>2500.) {shared.lanes[key]={slice.now,false};result.shot_clear=false;}
    else {
        auto cached=shared.lanes.find(key);
        if (cached!=shared.lanes.end() && slice.now-cached->second.at<=.20+1.e-9)result.shot_clear=cached->second.clear;
        else if (shared.final_lane_budget>0) {--shared.final_lane_budget;
            const auto plan=shared.artillery.plan(physical,slice.now);const bool clear=plan.ready && plan.candidate.has_value();
            shared.lanes[key]={slice.now,clear};result.shot_clear=clear;}
    }
    if (!result.shot_clear || !*result.shot_clear)return result;
    const auto gunner=advance_gunner(actor.memory.gunner,input.aim.gunner,input.aim.target,frame.pose.position,
        weapon.gun.config.fully_aimed_dispersion,weapon.gun.current_dispersion_factor,
        input.aim.round_id,actor.key.id,weapon.fire_seq,slice.now);
    if (!gunner || !gunner->ready)return result;
    const double dispersion=std::max(weapon.gun.dispersion,weapon.gun.config.fully_aimed_dispersion*c.dispersion_factor);
    result.proof=prove_artillery_launch(actor.memory.artillery,shared.artillery,physical,*aimed.solution,
        weapon.fire_seq+1,input.aim.round_id,frame.speed,dispersion,weapon.aim.aligned,
        input.aim.signature.target_alive,slice.now,input.splash_radius,input.team,input.roster,world);
    if (!result.proof.launch)return result;
    c.publish=input.publish;c.action_fresh=aimed.fresh;c.fire_allowed=input.aim.fire_allowed;c.has_target=c.has_ballistic=true;
    c.target_distance=distance;c.fire_range=input.fire_range;c.shot_clear=c.gunner_ready=c.artillery=true;
    auto fired=advance_weapon_fire(actor.weapons,actor.key,frame,c,{},input.start_us,input.end_us,
        *input.aim.mount,shot,input.roster,world,result.proof.launch);
    result.fire.committed+=fired.committed;result.fire.started=fired.started;
    result.fire.cancelled=result.fire.cancelled || fired.cancelled;
    if (fired.started) {actor.memory.artillery={};shared.artillery.cancel_launch(actor.key.id);}
    return result;
}
} // namespace wot::offline_bot_step
