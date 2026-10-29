#include "stock_native_bot_weapon_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <set>
namespace wot::offline_bot_step {
namespace {
namespace d=stock_native_bot_decision;
constexpr double pi=3.14159265358979323846;
double delta(double target,double current){double a=target-current;while(a>pi)a-=2.*pi;while(a< -pi)a+=2.*pi;return a;}
bool destroyed(const CombatState& c,const char* name){const auto i=c.critical.devices.find(name);return i!=c.critical.devices.end() && i->second.destroyed;}
AimPose pose(const WeaponActorState& a,const stock_native_bot_motion::State& motion,const offline_simulation::weapons::ActorState& weapon){
 AimPose out;out.position=motion.position;out.yaw=motion.yaw;out.pitch=motion.pitch;out.roll=motion.roll;
 out.turret_yaw=weapon.aim.turret_yaw;out.gun_pitch=weapon.aim.gun_pitch;out.aim_yaw=a.aim_yaw;return out;}
BallisticSignature signature(const WeaponActorState& a,const CombatActorRefs& actor,const std::optional<WeaponTarget>& t){
 BallisticSignature s;if(t){s.target_kind=t->target.kind;s.target_id=t->target.id;s.target_alive=t->alive;}
 const auto& weapon=actor.weapons.at(actor.key);s.shell=weapon.ammo.loaded;s.fire_sequence=weapon.fire_seq;
 s.siege_state=actor.siege.mode;s.descriptor_revision=static_cast<std::int64_t>(a.descriptor_revision);return s;}
WeaponFrame frame(const WeaponActorState& a,const CombatActorRefs& actor,const CombatModeSnapshot& mode,const stock_native_bot_motion::State& motion,
 const std::optional<WeaponTarget>& target,const d::Decisions& decisions){
 const auto& weapon=actor.weapons.at(actor.key);WeaponFrame f;f.pose=pose(a,motion,weapon);f.terrain_pitch=motion.terrain_pitch;
 f.hydraulic_pitch=motion.hydraulic_pitch;f.speed=motion.speed;f.turn_speed=motion.turn_speed;f.crew_factor=mode.crew_factor;f.gun_factor=mode.gun_rotation_factor;
 f.valid_pitch=mode.valid_pitch;f.siege_enabled=actor.siege.mode==2;f.limited_yaw=mode.limited_yaw;f.minimum_yaw=mode.minimum_yaw;f.maximum_yaw=mode.maximum_yaw;
 const bool engine=destroyed(actor.combat,"engineHealth"),track=destroyed(actor.combat,"leftTrackHealth")||destroyed(actor.combat,"rightTrackHealth");
 const bool switching=actor.siege.mode==1||actor.siege.mode==3;
 if(mode.static_yaw && (engine||track||actor.combat.overturned||motion.movement_direction!=0||switching)) {
  f.minimum_yaw=f.maximum_yaw=*mode.static_yaw;f.limited_yaw=true;}
 f.pitch_locked=mode.aim_config.has_static_pitch && (engine||actor.combat.overturned||switching);f.static_pitch=mode.aim_config.static_pitch;
 auto found=decisions.actors.find(actor.key.id);if(found!=decisions.actors.end()&&found->second.rear_hull_aim&&target &&
  found->second.rear_hull_aim->first.kind==target->target.kind&&found->second.rear_hull_aim->first.id==target->target.id)
  f.rear_turn=found->second.rear_hull_aim->second;
 return f;
}
const std::optional<DirectShot>* shell(const CombatModeSnapshot& mode,int i){return i>=0 && static_cast<std::size_t>(i)<mode.shells.size()?&mode.shells[i]:nullptr;}
ArtilleryAimInput artilleryInput(const WeaponActorState& a,const CombatActorRefs& actor,const CombatModeSnapshot& mode,
 const stock_native_bot_motion::State& motion,const std::optional<WeaponTarget>& target,const stock_native_bot_planner::Command& command){
 ArtilleryAimInput in;in.round_id=a.round_id;in.gunner=a.gunner;in.mount=mode.gun_mount;in.signature=signature(a,actor,target);in.fire_allowed=command.fire_allowed;
 in.command_aim_position=command.aim;const auto& weapon=actor.weapons.at(actor.key);auto& p=in.physical;p.identity.actor=actor.key.id;
 p.identity.fire_sequence=weapon.fire_seq;p.identity.shell=weapon.ammo.loaded;p.identity.descriptor_revision=a.descriptor_revision;
 p.pose=pose(a,motion,weapon);p.pitch_config=weapon.aim_config;p.valid_pitch=mode.valid_pitch;
 if(mode.gun_mount)p.origin=shot_origin(p.pose,*mode.gun_mount);
 if(target){in.target=target->target;p.identity.target_kind=target->target.kind;p.identity.target=target->target.id;p.target=target->target.position;p.velocity=target->target.velocity;}
 const auto physical=shell(mode,weapon.ammo.loaded);if(physical && *physical){p.speed=(**physical).speed;p.gravity=(**physical).gravity;p.maximum=(**physical).maximum_distance;}
 return in;
}
std::array<double,3> hull_aim(double hull,double target,double lo,double hi,double turn,double throttle,double rear,
 const stock_native_bot_planner::Command& cmd,bool has_target,bool allow_rear){
 if(cmd.movement_intent&&(cmd.mode=="artillery_deploy"||cmd.mode=="artillery_relocate"))return {turn,throttle,0.};
 static const std::set<std::string> held={"avoid","blocked","reverse_turn","pivot_recovery","forward_escape","short_forward_escape","short_reverse_escape","contact_escape","wreck_push","friendly_yield","nav_wait","physical_hold"};
 if(!has_target||held.count(cmd.recovery))return {turn,throttle,0.};
 const bool limited=!(lo<=-pi+.1&&hi>=pi-.1);const double relative=delta(target,hull),margin=std::min(.04,std::max(0.,hi-lo)*.25);
 const auto clamp=[](double v,double l,double h){return std::max(l,std::min(h,v));};
 if(limited&&(rear!=0.||(allow_rear&&std::abs(relative)>pi*.5))&&!(lo+margin<=relative&&relative<=hi-margin)){
  double a=delta(target-(lo+hi)*.5,hull);if(rear!=0.&&std::abs(relative)>pi*.5&&a*rear<0.)a+=std::copysign(2.*pi,rear);return {clamp(a/.58,-1.,1.),0.,1.};}
 if(cmd.recovery=="reverse_withdraw"){const double center=(lo+hi)*.5;if(cmd.withdrawal_aim&&throttle<0.&&std::abs(relative-center)<=.35&&limited){
  if(lo+.02<=relative&&relative<=hi-.02)return {0.,throttle,0.};return {-clamp((relative-center)/.58,-.5,.5),throttle,1.};}return {turn,throttle,0.};}
 if((cmd.movement_intent&&cmd.mode!="engage")||!limited)return {turn,throttle,0.};
 if(lo+margin<=relative&&relative<=hi-margin)return {std::abs(throttle)<=.01?0.:turn,throttle,0.};
 return {clamp(delta(target-(lo+hi)*.5,hull)/.58,-1.,1.),0.,1.};
}
}
WeaponActorState::SelectionRecord& WeaponRuntime::selection(WeaponActorState& actor,const WeaponTarget& target){
 const auto key=std::make_pair(target.target.kind,target.target.id);auto& record=actor.targets[key];
 if(record.record_revision!=target.record_revision||record.descriptor_revision!=target.descriptor_revision ||
    (record.selection.samples.empty()&&!target.samples.empty())){record={};record.record_revision=target.record_revision;
  record.descriptor_revision=target.descriptor_revision;record.selection.samples=target.samples;}
 return record;
}
std::optional<bool> WeaponRuntime::shotClear(WeaponActorState& state,CombatActorRefs actor,const CombatModeSnapshot& mode,
 const stock_native_bot_motion::State& motion,const WeaponTarget& target,double now,int& budget,bool force,WeaponRuntimeWorld& world){
 auto key=std::make_tuple(actor.key.id,target.target.kind,target.target.id);double d2=0.;
 for(int i=0;i<3;++i){const double d=target.target.position[i]-motion.position[i];d2+=d*d;}
 const bool spg=state.class_tag=="SPG";if(std::sqrt(d2)>(spg?2500.:585.)){shared_.lanes[key]={now,false};return false;}
 const auto found=shared_.lanes.find(key);if(!force && found!=shared_.lanes.end() && now-found->second.at<=.2+1.e-9)return found->second.clear;
 if(budget<=0)return {};--budget;const auto& weapon=actor.weapons.at(actor.key);const int index=weapon.ammo.loaded;
 const auto armor=index>=0 && static_cast<std::size_t>(index)<mode.armor_shells.size()?&mode.armor_shells[index]:nullptr;
 world.bind(actor.key,target,armor);const auto source_pose=pose(state,motion,weapon);const auto barrel=world.barrel_point(actor.key,source_pose);
 bool clear=false;try {
  const bool source=world.source_available(actor.key)&&barrel&&!world.barrel_under_water(*barrel);
  if(spg) {stock_native_bot_planner::Command command;auto input=artilleryInput(state,actor,mode,motion,target,command);
   input.physical.source_available=source;if(barrel)input.physical.barrel_point=*barrel;
   const auto plan=shared_.artillery.plan(input.physical,now);clear=plan.ready&&plan.candidate.has_value();
  }else {auto& record=selection(state,target);if(source && target.available && target.alive && mode.gun_mount)
    clear=select_exposed_aim(record.selection,target.pose,shot_origin(source_pose,*mode.gun_mount),state.rating,state.round_id,actor.key.id,
        target.target,actor.memory.gunner,world);
   else record.selection.selected.reset();}
 }catch(...){clear=false;}
 shared_.lanes[key]={now,clear};
 if(shared_.lanes.size()>1024){std::vector<std::pair<double,decltype(key)>> order;for(const auto& row:shared_.lanes)order.push_back({row.second.at,row.first});
  std::stable_sort(order.begin(),order.end(),[](const auto&a,const auto&b){return a.first<b.first;});
  for(std::size_t i=0;i<256;++i)shared_.lanes.erase(order[i].second);}
 return clear;
}
void WeaponRuntime::preMotion(WeaponActorState& state,CombatActorRefs actor,const CombatModeSnapshot& mode,
 stock_native_bot_motion::State& motion,stock_native_bot_planner::Command& command,const std::optional<WeaponTarget>& target,
 const CombatSlice& slice,d::Decisions& decisions){
 prepare_combat_weapon(actor,slice,command.shell_index);
 if(state.class_tag=="SPG") {auto input=artilleryInput(state,actor,mode,motion,target,command);
  const auto hold=prepare_artillery_hold(actor,shared_,slice,input);if(hold.aim_position)command.aim=command.face=*hold.aim_position;
  if(hold.brake){command.brake=true;command.throttle=command.turn=0.;command.movement_intent=false;}}
 const auto aim=command.aim;const double dx=aim[0]-motion.position[0],dz=aim[2]-motion.position[2],distance=std::hypot(dx,dz);
 double yaw=distance>.1?std::atan2(dx,dz):motion.yaw;double laying_yaw=yaw;
 const auto sig=signature(state,actor,target);
 if(mode.limited_yaw && target){double pitch=-std::atan2(aim[1]+1.-motion.position[1],std::max(.5,distance));
  std::optional<AimTarget> solution;if(actor.memory.ballistic.initialized&&actor.memory.ballistic.signature==sig)solution=actor.memory.ballistic.solution;
  if(state.class_tag=="SPG") {const auto& m=actor.memory;std::optional<ArcCandidate> arc;
   if(m.artillery.intent)arc=m.artillery.intent->solution;else if(m.artillery.reproof)arc=m.artillery.reproof->hold_solution;
   else if(m.artillery_continuity&&m.artillery_continuity_signature==sig)arc=m.artillery_continuity;
   if(arc){AimTarget x;x.solution_yaw=arc->yaw;x.solution_pitch=arc->pitch;solution=x;}}
  if(solution){yaw=solution->solution_yaw;pitch=solution->solution_pitch;}
  AimIntent intent;intent.world_yaw=yaw;intent.world_pitch=pitch;laying_yaw=motion.yaw+local_aim_intent(pose(state,motion,actor.weapons.at(actor.key)),intent).raw_yaw;}
 auto& policy=decisions.actors[actor.key.id];d::Identity id;if(target){id.kind=target->target.kind;id.id=target->target.id;}
 double rear=policy.rear_hull_aim&&policy.rear_hull_aim->first==id?policy.rear_hull_aim->second:0.;
 const bool allow=!(policy.rear_retry&&policy.rear_retry->first==id&&slice.now<policy.rear_retry->second)&&!policy.rear_motion_blocked;
 const auto aimed=hull_aim(motion.yaw,laying_yaw,mode.minimum_yaw,mode.maximum_yaw,std::max(-1.,std::min(1.,command.turn)),
  std::max(-1.,std::min(1.,command.throttle)),rear,command,target&&command.mode!="base_defense"&&command.parking_phase.empty(),allow);
 command.turn=aimed[0];command.throttle=aimed[1];const bool active=aimed[2]!=0.;state.hull_aiming=active;
 if(active&&mode.limited_yaw&&(rear!=0.||std::abs(delta(laying_yaw,motion.yaw))>pi*.5)){
  policy.rear_hull_aim=std::make_pair(id,command.turn);
  if(!policy.rear_lease&&command.movement_intent&&command.mode!="engage")policy.rear_lease=d::RearLease{motion.position,slice.now,{}};
  if(!policy.rear_episode)policy.rear_episode=d::RearEpisode{target?target->target.id:0,actor.weapons.at(actor.key).fire_seq,id,slice.now};
 }else policy.rear_hull_aim.reset();
 if(active&&std::abs(command.throttle)<=.01)command.brake=true;
}
WeaponRuntimeResult WeaponRuntime::postMotion(WeaponActorState& state,CombatActorRefs actor,const CombatModeSnapshot& mode,
 stock_native_bot_motion::State& motion,const d::Input& decision_input,const stock_native_bot_planner::Command& command,
 const std::optional<WeaponTarget>& target,const CombatSlice& slice,bool publish,bool refresh_control,
 std::int64_t start_us,std::int64_t end_us,d::Decisions& decisions,WeaponRuntimeWorld& world){
 WeaponRuntimeResult result;auto& weapon=actor.weapons.at(actor.key);const int index=weapon.ammo.loaded;
 const auto physical=shell(mode,index);const auto armor=index>=0&&static_cast<std::size_t>(index)<mode.armor_shells.size()?&mode.armor_shells[index]:nullptr;
 world.bind(actor.key,target,armor);auto f=frame(state,actor,mode,motion,target,decisions);const auto barrel=world.barrel_point(actor.key,f.pose);
 bool source=world.source_available(actor.key) && barrel.has_value();
 if(source)try {source=!world.barrel_under_water(*barrel);}catch(...){source=false;}
 const auto roster=world.lane_bodies(actor.key);
 d::Target decision_target;if(target){decision_target.identity={target->target.kind,target->target.id};decision_target.id=target->target.id;
  decision_target.position=target->target.position;decision_target.alive=target->alive;}
 const d::Target* decision_target_ptr=target?&decision_target:nullptr;
 if(state.class_tag=="SPG"){
  auto aim=artilleryInput(state,actor,mode,motion,target,command);aim.refresh_control=refresh_control;aim.physical.source_available=source;
  if(barrel)aim.physical.barrel_point=*barrel;
  const auto aimed=advance_artillery_aim(actor,shared_,slice,aim,f,mode.hydraulic,world);
  const auto launch_barrel=world.barrel_point(actor.key,f.pose);
  aim.physical.source_available=world.source_available(actor.key)&&launch_barrel.has_value();
  if(launch_barrel)aim.physical.barrel_point=*launch_barrel;
  ArtilleryFireInput input;input.aim=aim;input.publish=publish;input.team=state.team;input.fire_range=command.fire_range;
  input.start_us=start_us;input.end_us=end_us;input.roster=roster;if(physical && *physical)input.splash_radius=(**physical).splash_radius;
  const auto fired=advance_artillery_fire(actor,shared_,slice,input,aimed,f,world);
  result.fire=fired.fire;result.artillery_proof=fired.proof;result.shot_clear=fired.shot_clear;
  if(fired.fire.started)decisions.clearReposition(actor.key.id);
  if(fired.proof.status==ArtilleryProofStatus::friendly_blocked&&fired.proof.has_blocker)
   decisions.markReposition(decision_input,command,decision_target_ptr,fired.proof.shot_yaw.value_or(f.pose.aim_yaw),
      weapon.fire_seq+1,fired.proof.blocker_kind,fired.proof.blocker_id,fired.proof.blocker_team,
      fired.proof.blocker_position,fired.proof.blocker_radius);
  d::FirePositionEvidence evidence;evidence.ready=aimed.planning.ready;evidence.planning_clear=aimed.planning.ready&&aimed.planning.candidate.has_value();
  evidence.planning_failed=aimed.planning.ready&&!aimed.planning.candidate;evidence.reason=aimed.planning.reason;evidence.local_blockage=aimed.planning.local_blockage;
  evidence.launch_clear=fired.proof.status==ArtilleryProofStatus::ready;
  evidence.launch_failed=fired.proof.status==ArtilleryProofStatus::failed;
  if(evidence.launch_failed){evidence.launch_reason=fired.proof.reason;evidence.launch_completed=fired.proof.completed;if(target)evidence.launch_target=decision_target.identity;}
  evidence.completed=aimed.planning.completed;
  if(publish)decisions.observeFirePosition(decision_input,command,decision_target_ptr,evidence);
 }else {
  DirectAimInput aim;aim.round_id=state.round_id;aim.gunner=state.gunner;aim.signature=signature(state,actor,target);aim.refresh_control=refresh_control;
  aim.mount=mode.gun_mount;aim.command_aim_position=command.aim;if(physical&&*physical){aim.shell=**physical;aim.shell->round_id=state.round_id;aim.shell->team=state.team;
   aim.shell->class_tag=state.class_tag;if(barrel)aim.shell->barrel_point=*barrel;}
  WeaponActorState::SelectionRecord* record=nullptr;
  if(target){aim.target=target->target;aim.target_pose=target->pose;record=&selection(state,*target);
   if(target->available && target->alive && record->selection.selected&&*record->selection.selected<record->selection.samples.size()){
    const auto i=*record->selection.selected;aim.selected_sample=record->selection.samples[i];aim.selected_token=DirectAimToken{target->record_revision,target->descriptor_revision,i};}}
  if(!source){aim.mount.reset();aim.shell.reset();}
  const auto aimed=advance_direct_aim(actor,slice,aim,f,mode.hydraulic,world);
  const auto launch_barrel=world.barrel_point(actor.key,f.pose);
  bool launch_source=world.source_available(actor.key)&&launch_barrel.has_value();
  if(launch_source)try {launch_source=!world.barrel_under_water(*launch_barrel);}catch(...){launch_source=false;}
  if(launch_source && physical && *physical && !aim.shell){aim.mount=mode.gun_mount;aim.shell=**physical;
   aim.shell->round_id=state.round_id;aim.shell->team=state.team;aim.shell->class_tag=state.class_tag;}
  if(aim.shell && launch_barrel)aim.shell->barrel_point=*launch_barrel;
  if(!launch_source){aim.mount.reset();aim.shell.reset();}
  DirectFireInput input;input.aim=aim;input.publish=publish;input.fire_allowed=command.fire_allowed;input.rating=state.rating;
  input.fire_range=command.fire_range;input.source_available=launch_source;input.target_available=target&&target->available;
  input.start_us=start_us;input.end_us=end_us;input.roster=roster;
  if(record){input.selection=&record->selection;input.target_record_revision=record->record_revision;input.target_descriptor_revision=record->descriptor_revision;}
  const auto fired=advance_direct_fire(actor,shared_,slice,input,aimed,f,world);
  result.fire=fired.fire;result.shot_clear=fired.shot_clear;
  if(fired.fire.started)decisions.clearReposition(actor.key.id);
  if(fired.fire.blocker){offline_simulation::weapons::BurstEdge edge;edge.shot_seq=edge.group_seq=weapon.fire_seq+1;edge.count=1;edge.final_round=true;
   const double dispersion=std::max(weapon.gun.dispersion,weapon.gun.config.fully_aimed_dispersion*critical_factor(actor.combat.critical,CriticalStat::dispersion));
   const auto angles=dispersed_shot_angles(state.round_id,actor.key.id,edge,f.pose,dispersion);const auto& blocker=*fired.fire.blocker;
   decisions.markReposition(decision_input,command,decision_target_ptr,angles[0],weapon.fire_seq+1,blocker.kind,blocker.id,blocker.team,blocker.position,blocker.radius);}
 }
 motion.hydraulic_pitch=f.hydraulic_pitch;motion.pitch=f.pose.pitch;state.aim_yaw=f.pose.aim_yaw;return result;
}
}
