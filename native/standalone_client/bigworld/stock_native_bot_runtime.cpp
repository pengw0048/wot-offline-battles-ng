#include "stock_native_bot_runtime.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot {
namespace {
Key key(ID id){return {offline_simulation::ActorKind::Bot,id};}
double distance(const Point&a,const Point&b){return std::sqrt(std::pow(a[0]-b[0],2)+std::pow(a[2]-b[2],2));}
bool destroyed(const step::CriticalState&c,const char* name)
{const auto i=c.devices.find(name);return i!=c.devices.end()&&i->second.destroyed;}
template<class T> std::optional<T> optional(const plan::Optional<T>& value)
{return value.present?std::optional<T>(value.value):std::nullopt;}
}
Runtime::Runtime(Snapshot snapshot,stock_native_world::Registry&registry,stock_native_world::Scene&scene,Services&services):
    services_(services),pipeline_(registry,scene,128),perception_(control_,registry,scene,std::move(snapshot.foliage)),
    planning_(snapshot.planning,pipeline_,perception_,*this),
    physics_context_{contact_actors_,contact_humans_,insertion_order_,geometry_,host_memory_,vertical_memory_,slots_,
        planning_.grid(),planning_.navigator(),planning_.candidates(),planning_.motionPlan(),planning_.planner(),services.turrets(),*this,snapshot.planning.bake,0.,0.,0.,{},{}},
    physics_(physics_context_),observations_(weapons_runtime_.shared(),perception_,planning_.decisions(),planning_.grid(),services.tacticalWorld()),
    identity_(snapshot.identity),clock_(snapshot.clock),equipment_time_(snapshot.equipment_time),camera_(snapshot.camera),slope_cursor_(snapshot.slope_cursor),order_revision_(snapshot.planning.order_revision)
{
    if(identity_.round<=0||identity_.generation<=0)throw std::invalid_argument("Invalid native Bot round identity");
    for(auto& input:snapshot.actors) {
        const ID id=input.motion.state.id;if(id<=0||actors_.count(id))throw std::invalid_argument("Duplicate native Bot installation identity");
        Actor a;a.motion=std::move(input.motion.state);a.descriptor=input.motion.descriptor;a.suspension=input.motion.suspension;
        a.combat=std::move(input.combat.state);a.ledger=std::move(input.combat.ledger);a.siege=input.combat.siege;a.siege_config=input.combat.siege_config;
        a.equipment=std::move(input.combat.equipment);a.modes=std::move(input.modes);a.weapon=std::move(input.weapon);
        a.metadata=std::move(input.metadata);a.shot_angles=input.shot_angles;a.has_siege=input.has_siege;a.world_pose=input.world_pose;
        a.spawn_anchor=input.spawn_anchor;
        a.team=input.motion.team;a.slot=input.motion.slot;a.mode=(a.siege.mode==2||a.siege.mode==3)?1:0;
        a.modes[a.mode].motion.descriptor=a.descriptor;a.modes[a.mode].motion.suspension=a.suspension;
        a.modes[a.mode].motion.ram=input.motion.ram;a.modes[a.mode].motion.detailed_suspension=input.motion.detailed_suspension;
        // Every mode is frozen at installation. The current snapshot carries
        // persistent clocks; descriptor mode rows contain no live clocks.
        motion::validate(a.descriptor);input.combat.gun_config.validate();
        weapons_.install(key(id),input.combat.gun_config,input.combat.ammo,
            input.combat.fire_sequence,1.);
        auto&w=weapons_.at(key(id));
        if(input.combat.gun)w.gun=*input.combat.gun;
        w.ammo=input.combat.ammo;w.burst=input.combat.burst;w.aim_config=input.combat.aim_config;w.aim=input.combat.aim;w.has_aim=true;
        // fire_seq is captured independently of the room/actor identity.
        w.fire_seq=input.combat.fire_sequence;
        actors_.emplace(id,std::move(a));auto& actor=actors_.at(id);
        insertion_order_.push_back(id);slots_[id]=actor.slot;vertical_memory_[id]=std::move(input.motion.memory);
        geometry_[id]=actor.modes[actor.mode].physics.geometry;host_memory_[id]=actor.modes[actor.mode].physics.memory;
        contacts::Actor contact;contact.motion=&actor.motion;contact.descriptor=&actor.descriptor;contact.team=actor.team;contact.profile=input.motion.ram;
        contact.push_acks=std::move(input.motion.push_acknowledgements);contact.ram_motion=std::move(input.motion.ram_motion);contact.ram_motion_sequence=input.motion.ram_sequence;
        contact_actors_[id]=std::move(contact);
    }
    dictionary_order_=contacts::python27_integer_order(insertion_order_);
    publication_order_=dictionary_order_;
    std::stable_sort(publication_order_.begin(),publication_order_.end(),[&](ID a,ID b){
        const auto&left=actors_.at(a);const auto&right=actors_.at(b);
        return std::tie(left.slot,left.team)<std::tie(right.slot,right.team);});
    for(auto&pair:actors_) {
        auto&a=pair.second;const auto&m=a.modes[a.mode];observation::Descriptor d;d.valid=true;d.valid_pitch=m.combat.valid_pitch;
        d.mount=m.combat.gun_mount;d.samples=m.combat.target_samples;d.hull_minimum=a.descriptor.hull_minimum;d.hull_maximum=a.descriptor.hull_maximum;
        d.aim=m.combat.aim_config;d.minimum_yaw=m.combat.minimum_yaw;d.maximum_yaw=m.combat.maximum_yaw;observations_.descriptor(key(pair.first),std::move(d));
    }
}
Runtime::~Runtime(){planning_.endFrame();perception_.finish();}
std::vector<BurstDeadline> Runtime::bursts()const
{
    std::vector<BurstDeadline> result;result.reserve(actors_.size());
    for(const auto&p:actors_){const auto&b=weapons_.at(key(p.first)).burst;result.push_back({b.active,b.time_left});}return result;
}
step::AimPose Runtime::aimPose(const Actor&a)const
{
    const auto&aim=weapons_.at(key(a.motion.id)).aim;step::AimPose p;
    p.position=a.motion.position;p.yaw=a.motion.yaw;p.pitch=a.motion.pitch;p.roll=a.motion.roll;
    p.turret_yaw=aim.turret_yaw;p.gun_pitch=aim.gun_pitch;p.aim_yaw=a.weapon.aim_yaw;return p;
}
void Runtime::updateCombatPose(Actor&a)
{
    a.combat.position=a.motion.position;a.combat.pitch=a.motion.pitch;a.combat.roll=a.motion.roll;
    a.combat.speed=a.motion.speed;a.combat.turn_speed=a.motion.turn_speed;
    a.combat.movement_direction=a.motion.movement_direction;a.combat.rotation_direction=a.motion.rotation_direction;a.combat.alive=a.motion.alive;
}
void Runtime::applyCombatPose(Actor&a)
{
    a.motion.alive=a.combat.alive;a.motion.speed=a.combat.speed;a.motion.turn_speed=a.combat.turn_speed;
    a.motion.movement_direction=a.combat.movement_direction;a.motion.rotation_direction=a.combat.rotation_direction;
    if(!a.combat.alive)a.selected_target.reset();
}
step::CombatActorRefs Runtime::combatRefs(Actor&a)
{return {key(a.motion.id),a.combat,a.siege,a.equipment,a.ledger,weapons_,a.weapon_memory,&environment_};}
step::CombatDescriptor Runtime::combatDescriptor(const Actor&a)const
{
    const auto&mode=a.modes[a.mode].combat;
    return {mode.water_sensor,a.siege_config,mode.base_repair_factor,mode.repair_speed_factor};
}
void Runtime::adoptMode(Actor&a)
{
    const int mode=(a.siege.mode==2||a.siege.mode==3)?1:0;if(mode==a.mode)return;
    a.mode=mode;const auto&m=a.modes[mode];a.descriptor=m.motion.descriptor;a.suspension=m.motion.suspension;
    a.motion.pitch_speed=a.motion.roll_speed=a.motion.support_vertical_speed=0.;a.motion.support_gradient.reset();a.motion.ground_plane.reset();
    a.motion.suspension_disabled=false;vertical_memory_[a.motion.id]={};
    contact_actors_.at(a.motion.id).profile=m.motion.ram;geometry_[a.motion.id]=m.physics.geometry;
    planning_.descriptor(a.motion.id,m.navigation);planning_.traffic().forget(a.motion.id);cancelArtillery(a.motion.id);
    step::adopt_combat_mode(weapons_,key(a.motion.id),m.combat);++a.weapon.descriptor_revision;
    observation::Descriptor d;d.valid=true;d.valid_pitch=m.combat.valid_pitch;d.mount=m.combat.gun_mount;d.samples=m.combat.target_samples;
    d.hull_minimum=a.descriptor.hull_minimum;d.hull_maximum=a.descriptor.hull_maximum;d.aim=m.combat.aim_config;
    d.minimum_yaw=m.combat.minimum_yaw;d.maximum_yaw=m.combat.maximum_yaw;observations_.descriptor(key(a.motion.id),std::move(d));
    services_.modeChanged(a.motion.id,m);
}
plan::Body Runtime::body(const Actor&a)const
{
    plan::Body b;b.id=a.motion.id;b.team=a.team;b.alive=a.motion.alive;b.position=a.motion.position;
    b.yaw=a.motion.yaw;b.pitch=a.motion.pitch;b.roll=a.motion.roll;b.velocity={{std::sin(b.yaw)*a.motion.speed,0.,std::cos(b.yaw)*a.motion.speed}};
    b.half_width=a.descriptor.collision_shape[0];b.half_length=a.descriptor.collision_shape[1];b.lower_y=a.descriptor.collision_shape[2];b.upper_y=a.descriptor.collision_shape[3];return b;
}
std::vector<plan::Body> Runtime::neighbours(ID id)const
{
    std::vector<plan::Body> result;result.reserve(actors_.size()+humans_.size());
    for(const auto&h:humans_)result.push_back(h.neighbour);
    for(ID peer:dictionary_order_)if(peer!=id){const auto&a=actors_.at(peer);auto row=body(a);
        const double speed=a.motion.alive?a.motion.speed:0.;
        row.velocity={{std::sin(row.yaw)*speed+a.motion.push_x,0.,std::cos(row.yaw)*speed+a.motion.push_z}};result.push_back(row);}
    return result;
}
int Runtime::detail(const Actor&a)const
{
    if(!camera_)return 0;const double d=std::pow(a.motion.position[0]-(*camera_)[0],2)+std::pow(a.motion.position[2]-(*camera_)[2],2);
    return d<=150.*150.?0:d<=350.*350.?1:2;
}
decision::Input Runtime::decisionInput(const Actor&a,const Slice&s)const
{
    decision::Input out;auto&in=out.planning;in.body=body(a);in.slot=a.slot;in.speed=a.motion.speed;in.dt=s.elapsed;in.now=s.now;
    in.rotation_refused=a.motion.rotation_refused_until>s.now;out.health=a.combat.health;out.maximum_health=a.combat.maximum_health;
    out.step=s.elapsed;out.fire_sequence=weapons_.at(key(a.motion.id)).fire_seq;out.shell_index=weapons_.at(key(a.motion.id)).ammo.loaded;
    out.reload_time=weapons_.at(key(a.motion.id)).gun.remaining(step::critical_factor(a.combat.critical,step::CriticalStat::reload));
    out.detail_tier=detail(a);out.refresh_control=s.refresh;out.fixed_control=true;out.airborne=a.motion.airborne;out.overturned=a.combat.overturned;
    out.evaluation_time=s.evaluation_time;in.turn_speed_limit.set(a.descriptor.drive.rotation);
    const auto order=planning_.decisions().orders.find(a.motion.id);const auto cache=planning_.decisions().cache.find(a.motion.id);
    if(std::abs(a.motion.speed)>.35&&(order==planning_.decisions().orders.end()||(order->second.drive.mode!="route"&&order->second.drive.mode!="advance")))
        in.stopping_distance.set(motion::stopping_distance(a.descriptor,a.motion.speed,a.motion.last_drive_pitch,
            cache!=planning_.decisions().cache.end()&&std::abs(cache->second.command.turn)>.01,.02,1./30.));
    return out;
}
void Runtime::planningActor(const Actor&a,const Slice&s)
{
    planning::LiveActor live;live.decision=decisionInput(a,s);live.water_depth=a.combat.water_depth;live.hard_grinds=a.motion.hard_grinds;
    live.artillery_intent=bool(a.weapon_memory.artillery.intent);live.artillery_reproof=bool(a.weapon_memory.artillery.reproof);
    for(const auto&r:a.motion.rejected_rotations)live.rejected_rotations[r.first]={r.second.origin,r.second.yaw,r.second.delta,r.second.until};
    planning_.update(std::move(live));
}
perception::Actor Runtime::perceptionActor(const Actor&a,const Slice&s)
{
    auto row=a.modes[a.mode].perception;const auto p=aimPose(a);const auto&w=weapons_.at(key(a.motion.id));
    const double vision=std::max(.5,std::min(1.,step::critical_factor(a.combat.critical,step::CriticalStat::vision)));
    row.config.view_moving*=vision;row.config.view_still*=vision;row.config.radio*=step::critical_factor(a.combat.critical,step::CriticalStat::signal);
    row.sample.pose.position=p.position;row.sample.pose.values={{p.position[0],p.position[1],p.position[2],p.yaw,p.pitch,p.roll,p.aim_yaw,p.turret_yaw,p.gun_pitch,a.motion.speed,
        std::sin(p.yaw)*a.motion.speed,0.,std::cos(p.yaw)*a.motion.speed}};row.sample.pose.mask=(1u<<13)-1;
    row.sample.alive=a.motion.alive;row.sample.fire_sequence=w.fire_seq;row.sample.decision_due=planning_.decisions().due(decisionInput(a,s));
    const auto cache=planning_.decisions().cache.find(a.motion.id);row.sample.has_selected=false;
    if(cache!=planning_.decisions().cache.end()&&cache->second.command.target.present) {
        const auto selected=cache->second.targets.find(cache->second.command.target.value);
        if(selected!=cache->second.targets.end()){row.sample.has_selected=true;row.sample.selected={selected->second.identity.kind=="human"?offline_simulation::ActorKind::Human:offline_simulation::ActorKind::Bot,selected->second.identity.id};}
    }
    row.observer_pose={p.position,p.yaw,p.pitch,p.roll,p.turret_yaw};row.target_pose=row.observer_pose;return row;
}
observation::Actor Runtime::observationActor(const Actor&a)const
{
    observation::Actor row;row.key=key(a.motion.id);row.pose=aimPose(a);row.speed=a.motion.speed;row.target=a.metadata;
    row.target.id=a.motion.id;row.target.identity={"bot",a.motion.id};row.target.team=a.team;row.target.alive=a.motion.alive;
    row.target.health=a.combat.health;row.target.max_health=a.combat.maximum_health;row.target.position=a.motion.position;row.target.speed=a.motion.speed;
    const auto&c=a.modes[a.mode].combat;row.pitch_locked=c.aim_config.has_static_pitch;row.static_pitch=c.aim_config.static_pitch;return row;
}
void Runtime::publishPose(Actor&a,const Slice&s,bool perception)
{
    updateCombatPose(a);services_.actorChanged(a,aimPose(a));observations_.update(observationActor(a));
    if(perception){planningActor(a,s);planning_.updatePerception(perceptionActor(a,s));}
}
std::optional<step::WeaponTarget> Runtime::target(const decision::Output&decision,const plan::Command&command)
{
    if(!command.target.present)return {};
    const auto i=decision.targets.find(command.target.value);if(i==decision.targets.end())return {};
    auto result=services_.weaponWorld().target(i->second.identity.kind,i->second.identity.id);if(!result)return {};
    // The selected target retains this observer's canonical remembered pose.
    const auto& t=i->second;if(t.fresh_visible)return result;
    result->target.position=t.position;result->pose.position=t.position;
    if(t.observed_pose) {
        const auto&p=*t.observed_pose;const auto&v=p.values;
        if(p.mask&(1u<<3))result->pose.yaw=v[3];if(p.mask&(1u<<4))result->pose.pitch=v[4];if(p.mask&(1u<<5))result->pose.roll=v[5];
        if(p.mask&(1u<<6))result->pose.aim_yaw=v[6];if(p.mask&(1u<<7))result->pose.turret_yaw=v[7];if(p.mask&(1u<<8))result->pose.gun_pitch=v[8];
        if(p.mask&(1u<<10))result->target.velocity={{v[10],v[11],v[12]}};
        else{const double speed=p.mask&(1u<<9)?v[9]:t.speed;result->target.velocity={{std::sin(result->pose.yaw)*speed,0.,std::cos(result->pose.yaw)*speed}};}
    }
    return result;
}
void Runtime::humans(std::vector<Human> rows)
{
    if(callback_open_)throw std::logic_error("Native human roster changed inside a physical callback");
    std::set<ID> ids;for(const auto&h:rows)if(h.contacts.network_id<=0||!ids.insert(h.contacts.network_id).second)
        throw std::invalid_argument("Invalid native human roster identity");
    humans_=std::move(rows);contact_humans_.clear();std::vector<nav::NavigationHuman> navigation;
    for(const auto&h:humans_){contact_humans_.push_back(h.contacts);navigation.push_back(h.navigation);}
    planning_.humans(std::move(navigation));
}
void Runtime::orders(std::map<ID,planning::StrategicOrder> orders,std::map<ID,std::uint64_t> tokens)
{if(callback_open_)throw std::logic_error("Native order replacement inside a physical callback");planning_.replaceOrders(std::move(orders),std::move(tokens));}
bool Runtime::acceptOrders(std::map<ID,planning::StrategicOrder> accepted,ID revision)
{
    if(callback_open_)throw std::logic_error("Native orders changed inside a physical callback");
    if(revision<order_revision_)return false;
    const auto same=[](const planning::StrategicOrder&a,const planning::StrategicOrder&b) {
        if(!a.signature.empty()||!b.signature.empty())return a.signature==b.signature;
        const auto&x=a.drive;const auto&y=b.drive;const auto&u=a.navigation;const auto&v=b.navigation;
        if(std::tie(x.target_kind,x.mode,x.parking_phase,x.fire_allowed,x.stable_hull_face,x.shell_index,x.fire_range)!=
            std::tie(y.target_kind,y.mode,y.parking_phase,y.fire_allowed,y.stable_hull_face,y.shell_index,y.fire_range))return false;
        if(optional(x.target)!=optional(y.target)||optional(x.throttle)!=optional(y.throttle)||optional(x.arrival_radius)!=optional(y.arrival_radius)||
            optional(x.parking_heading)!=optional(y.parking_heading)||optional(x.hull_angle_degrees)!=optional(y.hull_angle_degrees))return false;
        if((!x.target.present&&optional(x.aim)!=optional(y.aim))||((!x.target.present||x.stable_hull_face)&&optional(x.face)!=optional(y.face))||
            ((!x.target.present||x.mode!="advance_contact")&&optional(x.move)!=optional(y.move)))return false;
        return std::tie(u.mode,u.route_id,u.defense_base,u.parking_phase,u.route_index,u.route_join,u.route_anchor,u.throttle,u.target,u.move_area,u.team_command)==
            std::tie(v.mode,v.route_id,v.defense_base,v.parking_phase,v.route_index,v.route_join,v.route_anchor,v.throttle,v.target,v.move_area,v.team_command);
    };
    const auto&previous=planning_.decisions().orders;std::set<ID> changed;
    for(const auto&entry:previous){auto next=accepted.find(entry.first);if(next==accepted.end()||!same(entry.second,next->second))changed.insert(entry.first);}
    for(const auto&entry:accepted)if(!previous.count(entry.first))changed.insert(entry.first);
    auto tokens=planning_.decisions().tokens;
    for(ID id:changed){auto before=previous.find(id);auto after=accepted.find(id);
        const std::string old_route=before==previous.end()?std::string():before->second.navigation.route_id;
        const std::string new_route=after==accepted.end()?std::string():after->second.navigation.route_id;
        auto actor=planning_.navigation().actors.find(id);
        if(old_route!=new_route&&actor!=planning_.navigation().actors.end()){auto&a=actor->second;
            a.lane_group.reset();a.lane_gate.reset();a.lane_origin.reset();a.lane_goal.reset();a.lane_forward.reset();a.lane_segment.reset();
            a.lane_desired=a.row_desired=a.lane_offset=a.lane_row=0.;}
        ++tokens[id];planning_.invalidate(id);planning_.motionPlan().cache.erase(id);
    }
    planning_.replaceOrders(std::move(accepted),std::move(tokens));order_revision_=revision;
    if(!changed.empty()&&clock_.refresh_bank+1.e-9<clock_.period)clock_.refresh_bank+=clock_.period;
    return true;
}
bool Runtime::reconcile(ID id,const step::ServerCombat& incoming)
{
    auto i=actors_.find(id);if(i==actors_.end())return false;auto&a=i->second;const auto&mode=a.modes[a.mode].combat;
    const double repair=mode.base_repair_factor*(1.+std::max(0.,step::equipment_passives(a.equipment).repair_bonus));
    const bool changed=a.ledger.reconcile(a.combat,incoming,repair,mode.repair_speed_factor);if(changed)applyCombatPose(a);return changed;
}
bool Runtime::serverSnapshot(ID id,const ServerSnapshot&incoming)
{
    if(callback_open_)throw std::logic_error("Native combat ingress inside a physical callback");
    auto found=actors_.find(id);if(found==actors_.end())return false;auto&a=found->second;auto&w=weapons_.at(key(id));
    if(incoming.server_time_ms<0||incoming.fire_sequence<0)throw std::invalid_argument("Invalid native combat snapshot clock");
    if(incoming.combat.server_tick&&*incoming.combat.server_tick<a.ledger.server_tick)return false;
    const bool new_fire=incoming.fire_sequence>w.fire_seq;
    // Validate the complete ingress before mutating canonical combat state.
    if(new_fire&&!incoming.has_reload)throw std::invalid_argument("Native combat snapshot has no reload progress");
    if(incoming.ammunition)incoming.ammunition->validate(w.gun.config.shell_count);
    auto replacement=w.gun;auto combat=a.combat;auto ledger=a.ledger;
    const auto&mode=a.modes[a.mode].combat;
    const double repair=mode.base_repair_factor*(1.+std::max(0.,step::equipment_passives(a.equipment).repair_bonus));
    const bool changed=ledger.reconcile(combat,incoming.combat,repair,mode.repair_speed_factor);
    if(new_fire)replacement.restore(incoming.fire_sequence,step::critical_factor(combat.critical,step::CriticalStat::dispersion),
        true,incoming.reload_time,incoming.reload_duration,step::critical_factor(combat.critical,step::CriticalStat::reload),
        bool(incoming.clip),incoming.clip?(*incoming.clip)[0]:0,incoming.clip?(*incoming.clip)[1]:0);
    a.combat=std::move(combat);a.ledger=std::move(ledger);w.gun=std::move(replacement);
    a.combat.stun_until_equipment_time=equipment_time_+std::max(0.,double(a.combat.stun_end_server_time_ms-incoming.server_time_ms)/1000.);
    if(new_fire){w.fire_seq=incoming.fire_sequence;if(incoming.ammunition)w.ammo=*incoming.ammunition;}
    if(new_fire||!a.combat.alive)cancelArtillery(id);
    if(!a.combat.alive){planning_.decisions().clearReposition(id);a.combat.speed=a.combat.turn_speed=0.;
        a.combat.movement_direction=a.combat.rotation_direction=0;a.combat.has_target=false;a.selected_target.reset();}
    applyCombatPose(a);return changed||new_fire;
}
bool Runtime::acknowledgeLaunch(ID id,ID sequence){return weapons_.ack(key(id),sequence);}
void Runtime::acknowledgeRam(ID id,ID sequence){contacts_.acknowledge(id,sequence);}
double Runtime::planningRotationFraction(ID id,const Point&p,double from,double to,bool wreck)
{return physics_.planning_rotation_fraction(id,p,from,to,wreck);}
void Runtime::cancelArtillery(ID id)
{
    const auto a=actors_.find(id);if(a!=actors_.end()){a->second.weapon_memory.artillery={};a->second.weapon_memory.artillery_solution.reset();}
    weapons_runtime_.shared().artillery.cancel_launch(id);
}
int Runtime::fall_damage(motion::State&state,double speed,const std::vector<Point>&witnesses,const Point&normal,bool ground)
{
    auto&a=actors_.at(state.id);updateCombatPose(a);
    const auto result=step::apply_environment_fall_damage(state.id,a.combat,state,speed,ground,witnesses,normal,services_.impactWorld(state.id),environment_);
    applyCombatPose(a);return result.damage;
}
motion::World& Runtime::motion_world(ID id){return *worlds_.at(id);}
std::optional<contacts::Armor> Runtime::armor(const contacts::Body&a,const contacts::Body&b,const std::array<double,3>&n)
{return services_.armor(a,b,n);}
double Runtime::water_depth(const Point&p)
{
    const Point start{{p[0],p[1]+20.,p[2]}},end{{p[0],p[1]-5.,p[2]}};double hit=-1.;
    const auto result=pipeline_.water(start,end,hit);
    if(result==stock_native_world::QueryStatus::Failed)return -1.;
    return hit<0.?-1.:20.-hit;
}
publication::Input Runtime::publicationActor(const Actor&a)const
{
    const auto&w=weapons_.at(key(a.motion.id));const auto&b=w.burst;const auto&c=a.combat;const auto&m=a.motion;
    const double reload=step::critical_factor(c.critical,step::CriticalStat::reload);
    publication::Input out;out.scalars={{double(m.id),0.,m.position[0],m.position[1],m.position[2],m.yaw,m.pitch,m.roll,
        a.weapon.aim_yaw,w.aim.gun_pitch,m.speed,double(w.fire_seq),double(w.ammo.loaded),double(w.ammo.next),
        double(w.gun.clip),double(w.gun.config.clip_size),w.gun.remaining(reload),w.gun.duration(reload),
        double(b.group_seq),double(b.count),double(b.next_index),double(b.shell),b.interval,b.time_left,double(a.siege.mode),
        double(publication::fixed(a.siege.remaining,1000.)),double(publication::fixed(a.siege.total,1000.)),
        double(c.health),double(c.display_health),double(a.ledger.base_revision),double(a.ledger.next_sequence),
        c.critical.fire_elapsed,c.critical.fire_timer,double(c.death_reason),double(c.stun_end_server_time_ms),
        a.shot_angles?(*a.shot_angles)[0]:0.,a.shot_angles?(*a.shot_angles)[1]:0.}};
    const auto flag=[&](bool value,publication::Flag f){if(value)out.flags|=f;};
    flag(m.alive,publication::Alive);flag(a.world_pose,publication::WorldPose);
    flag(w.ammo.reload_pending,publication::AmmoReloadPending);flag(b.active,publication::BurstActive);
    flag(c.critical.fire,publication::Fire);flag(c.critical.ammo_rack_death,publication::AmmoRackDeath);
    flag(bool(a.shot_angles),publication::ShotAngles);flag(true,publication::Critical);
    flag(true,publication::Equipment);flag(true,publication::Ammo);flag(true,publication::Burst);flag(true,publication::Clip);
    flag(true,publication::Siege);flag(m.movement_direction>0,publication::Forward);flag(m.movement_direction<0,publication::Backward);
    flag(m.rotation_direction>0,publication::Left);flag(m.rotation_direction<0,publication::Right);
    flag(m.airborne,publication::Airborne);flag(m.service_brake,publication::ServiceBrake);flag(a.route_wreck_blocked,publication::RouteWreckBlocked);
    const auto&contact=contact_actors_.at(m.id);flag(true,publication::ContactMomentum);flag(true,publication::RamMotion);
    out.ammunition=w.ammo.remaining;out.critical=c.critical;out.crew_roster_present=!c.critical.crew_roster.empty();
    for(auto i=out.critical.devices.begin();i!=out.critical.devices.end();) {
        if(!i->second.hp)i=out.critical.devices.erase(i);
        else{i->second.maximum=i->second.wire_maximum.value_or(1.);++i;}
    }
    out.push={{m.push_x,m.push_z,m.contact_forward}};out.acknowledgements=contact.push_acks;
    for(const auto&row:contact.ram_motion)out.motions.push_back(row.second);
    for(const auto&e:a.equipment)out.equipment.push_back({ID(e.contract_revision),e.uses_left,e.active,e.ready_at,e.auto_pending_since,e.ai_pending_since});
    return out;
}
bool Runtime::outgoingLane(ID id,const observation::Actor&destination,double now)
{
    auto i=actors_.find(id);if(i==actors_.end())return false;auto&a=i->second;
    auto t=services_.weaponWorld().target(destination.target.identity.kind,destination.key.id);if(!t)return false;
    t->target.position=destination.pose.position;t->pose=destination.pose;t->alive=destination.target.alive;
    int budget=1;return weapons_runtime_.shotClear(a.weapon,combatRefs(a),a.modes[a.mode].combat,a.motion,*t,now,budget,true,services_.weaponWorld()).value_or(false);
}
ExternalContactsSnapshot Runtime::snapshotExternalContacts()const
{
    ExternalContactsSnapshot out;
    for(const auto&entry:actors_){const auto&m=entry.second.motion;out.actors[entry.first]={m.position[0],m.position[2],m.speed,m.push_x,m.push_z};}
    return out;
}
void Runtime::restoreExternalContacts(const ExternalContactsSnapshot& snapshot,double now)
{
    if(!std::isfinite(now))throw std::invalid_argument("External contact restore time is invalid");
    for(const auto&entry:snapshot.actors)for(double value:entry.second)if(!std::isfinite(value))throw std::invalid_argument("External contact rollback pose is invalid");
    Slice slice;slice.now=now;slice.evaluation_time=now;slice.start_us=slice.end_us=clock_.sample_us;
    for(const auto&entry:snapshot.actors){const auto found=actors_.find(entry.first);if(found==actors_.end())continue;
        auto&a=found->second;auto&m=a.motion;const auto&v=entry.second;
        m.position[0]=v[0];m.position[2]=v[1];m.speed=v[2];m.push_x=v[3];m.push_z=v[4];publishPose(a,slice,true);
    }
}
bool Runtime::externalContact(ID id,const motion::ContactResponse& response,double dt,double now)
{
    const auto found=actors_.find(id);if(found==actors_.end())return false;
    if(callback_open_||!std::isfinite(dt)||dt<0||!std::isfinite(now))throw std::invalid_argument("External contact callback boundary is invalid");
    for(double value:{response.correction[0],response.correction[1],response.delta_velocity[0],response.delta_velocity[1],response.delta_yaw})
        if(!std::isfinite(value))throw std::invalid_argument("External contact response is invalid");
    auto&a=found->second;physics_context_.now=now;physics_context_.elapsed=dt;
    physics::Config config;config.turret_yaw=weapons_.at(key(id)).aim.turret_yaw;
    config.detailed_suspension=a.modes[a.mode].motion.detailed_suspension;
    auto&world=worlds_[id];
    if(world)world->configure(config);
    else{world=std::make_unique<physics::PhysicsWorld>(pipeline_,physics_,a.motion,a.descriptor,vertical_memory_.at(id),a.suspension,repair_budget_,config,now);physics_.attach(id,*world);}
    motion::ContactOptions options;options.advance_push=false;
    motion::contact(a.motion,a.descriptor,response,dt,now,*world,options);
    for(ID invalid:physics_context_.invalidated_decisions)planning_.invalidate(invalid);
    physics_context_.invalidated_decisions.clear();
    Slice slice;slice.elapsed=dt;slice.now=now;slice.evaluation_time=now;slice.start_us=slice.end_us=clock_.sample_us;publishPose(a,slice,true);
    return true;
}
std::optional<Output> Runtime::advance(const Slice&s)
{
    ++statistics_.slices;statistics_.refreshes+=s.refresh;equipment_time_+=s.elapsed;
    physics_context_.now=s.now;physics_context_.elapsed=s.elapsed;
    physics_context_.invalidated_decisions.clear();physics_context_.observations.clear();
    const auto makeWorld=[&](Actor&a,std::optional<Point> support={}) {
        physics::Config config;config.turret_yaw=weapons_.at(key(a.motion.id)).aim.turret_yaw;
        config.detailed_suspension=a.modes[a.mode].motion.detailed_suspension;config.suspension_motion_position=support;
        auto&world=worlds_[a.motion.id];
        if(world)world->configure(config);
        else{world=std::make_unique<physics::PhysicsWorld>(pipeline_,physics_,a.motion,a.descriptor,
            vertical_memory_.at(a.motion.id),a.suspension,repair_budget_,config,s.now);physics_.attach(a.motion.id,*world);}
    };
    if(s.refresh)for(auto&entry:planning_.decisions().cache) {
        const double factors[]={1.,2.,4.};const double interval=.15*factors[detail(actors_.at(entry.first))];
        if(interval<entry.second.interval){entry.second.deadline=std::min(entry.second.deadline,entry.second.at+interval);entry.second.interval=interval;}
    }
    std::vector<observation::Actor> observed;std::vector<perception::Actor> sight;planning::TargetMetadata metadata;
    for(ID id:dictionary_order_) {auto&a=actors_.at(id);planningActor(a,s);makeWorld(a);
        auto row=observationActor(a);metadata[row.key]=row.target;observed.push_back(row);sight.push_back(perceptionActor(a,s));}
    for(const auto&h:humans_){observed.push_back(h.observation);sight.push_back(h.perception);metadata[h.observation.key]=h.observation.target;}
    observations_.replaceActors(std::move(observed));const auto schedule=observations_.begin(s.now,s.refresh,s.publish,true,clock_.period);
    if(s.refresh) {
        auto hulls=services_.turrets().navigation(physics_context_.server_time_ms);double clearance=0.;
        for(const auto&entry:actors_){const auto&a=entry.second;if(a.motion.alive)clearance=std::max(clearance,a.descriptor.half_width);
            else hulls.push_back({entry.first,a.motion.position[0],a.motion.position[2],a.motion.yaw,a.descriptor.half_length,a.descriptor.half_width});}
        for(const auto&h:humans_)if(!h.neighbour.alive)hulls.push_back({h.neighbour.id,h.neighbour.position[0],h.neighbour.position[2],h.neighbour.yaw,h.neighbour.half_length,h.neighbour.half_width});
        planning_.grid().setStaticHulls(hulls,clearance);
    }
    auto token=identity_;token.sequence=++identity_.sequence;token.sample_time_us=s.start_us;
    planning_.beginPerception(token,sight,s.now,48,schedule.collect_observation||schedule.refresh_shot_lanes,statistics_.callbacks);
    if(schedule.collect_observation||schedule.refresh_shot_lanes){perception_.observeHumans();observations_.observeHumans();}
    weapons_runtime_.beginSlice(64);contacts_.consume(contact_actors_,contact_humans_,*this,s.now);
    struct Tick {Point start{},settled{};physics::SuspensionSnapshot suspension;double yaw=0.,passive=0.;
        bool safe=false,blocked=false,ballistic=false,rollback=false;std::optional<Point> goal;};
    std::map<ID,Tick> ticks;std::map<ID,std::array<double,3>> locked;
    std::optional<std::map<std::array<int,2>,std::vector<plan::Body>>> traffic_snapshot;
    const auto plannerNeighbours=[&](const Actor&a) {
        std::vector<plan::Body> result;if(!planning_.decisions().due(decisionInput(a,s)))return result;
        if(!traffic_snapshot){traffic_snapshot.emplace();std::map<ID,plan::Body> by_id;std::vector<ID> insertions;
            for(const auto&h:humans_){insertions.push_back(h.neighbour.id);by_id[h.neighbour.id]=h.neighbour;}
            for(ID id:dictionary_order_){const auto&peer=actors_.at(id);auto row=body(peer);const double speed=peer.motion.alive?peer.motion.speed:0.;
                row.velocity={{std::sin(row.yaw)*speed+peer.motion.push_x,0.,std::cos(row.yaw)*speed+peer.motion.push_z}};
                insertions.push_back(id);by_id[id]=row;}
            for(ID id:contacts::python27_integer_order(insertions)){const auto&row=by_id.at(id);
                (*traffic_snapshot)[{{int(std::floor(row.position[0]/24.)),int(std::floor(row.position[2]/24.))}}].push_back(row);}}
        const int x=int(std::floor(a.motion.position[0]/24.)),z=int(std::floor(a.motion.position[2]/24.));
        for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){const auto found=traffic_snapshot->find({{x+dx,z+dz}});
            if(found!=traffic_snapshot->end())for(const auto&row:found->second)if(row.id!=a.motion.id)result.push_back(row);}
        return result;
    };
    for(ID id:dictionary_order_) {
        auto&a=actors_.at(id);auto&m=a.motion;
        if(!m.alive){a.selected_target.reset();weapons_.cancel(key(id));publishPose(a,s,true);metadata[key(id)]=observationActor(a).target;continue;}
        ++statistics_.actors;control_.note_source_stillness(key(id),s.now,m.speed);updateCombatPose(a);
        step::CombatSlice combat{s.elapsed,s.now,equipment_time_,a.combat.stun_until_equipment_time>equipment_time_+1.e-9};
        const auto prep=step::prepare_combat_actor(combatRefs(a),combatDescriptor(a),combat,*this);applyCombatPose(a);
        if(prep.siege_descriptor_changed)adoptMode(a);
        if(!prep.alive){a.selected_target.reset();publishPose(a,s,true);metadata[key(id)]=observationActor(a).target;continue;}
        Tick tick;tick.start=m.position;tick.suspension={m,vertical_memory_.at(id),host_memory_.at(id).pose_sample};
        tick.safe=bool(planning_.grid().index(planning_.grid().cellFor(m.position),false))&&!planning_.grid().pointHazard(m.position,9);
        const double siege_yaw=m.yaw;planningActor(a,s);
        auto decision=planning_.prepare(id,metadata,plannerNeighbours(a));auto command=decision.command;
        a.route_wreck_blocked=planning_.navigation().actors.at(id).route_wreck_blocked;
        observations_.observeActor(id,decision.contacts,command);
        auto chosen=target(decision,command);const decision::Target* selected=nullptr;
        if(command.target.present){auto found=decision.targets.find(command.target.value);if(found!=decision.targets.end())selected=&found->second;}
        decision::Target current;if(selected&&chosen){current=*selected;current.position=chosen->target.position;selected=&current;}
        if(selected&&chosen&&a.weapon.class_tag!="SPG") {
            auto&shared=weapons_runtime_.shared();const auto lane=shared.lanes.find({id,selected->identity.kind,selected->identity.id});
            if(lane!=shared.lanes.end()) {
                decision::LaneReceipt receipt{lane->second.at,lane->second.clear};bool refreshed=false;
                if(!receipt.clear&&s.now-receipt.at>.2+1.e-9&&s.now-receipt.at<=1.+clock_.period+1.e-9)
                    refreshed=weapons_runtime_.shotClear(a.weapon,combatRefs(a),a.modes[a.mode].combat,m,*chosen,s.now,shared.final_lane_budget,false,services_.weaponWorld()).value_or(false);
                if(decision::Decisions::blockedDirectAim(command,selected,&receipt,m.position,s.now,clock_.period,false,refreshed))chosen.reset();
            }
        }
        decision::Decisions::overlay(command,selected,m.position);
        a.combat.has_target=bool(chosen);
        a.selected_target=chosen&&selected?std::optional<decision::Identity>(selected->identity):std::nullopt;
        step::SiegeCommand siege; siege.throttle=command.throttle;siege.turn=command.turn;siege.brake=command.brake;
        siege.fire_allowed=command.fire_allowed;siege.movement_intent=command.movement_intent;siege.position=m.position;
        siege.destination=command.move;siege.target_alive=chosen&&chosen->alive;
        const auto change=step::prepare_combat_siege(combatRefs(a),combatDescriptor(a),combat,siege);
        if(change.descriptor_changed)adoptMode(a);
        command.throttle=siege.throttle;command.turn=siege.turn;command.brake=siege.brake;
        command.fire_allowed=siege.fire_allowed;command.movement_intent=siege.movement_intent;
        weapons_runtime_.preMotion(a.weapon,combatRefs(a),a.modes[a.mode].combat,m,command,chosen,combat,planning_.decisions());
        const double stopping=motion::stopping_distance(a.descriptor,m.speed,m.last_drive_pitch,std::abs(command.turn)>.01,.02,1./30.);
        auto safe=planning_.safeControls(id,command,neighbours(id),stopping);command.throttle=safe.throttle;command.turn=safe.turn;
        const bool siege_locked=prep.siege_motion_locked||change.locked;
        if(siege_locked) {
            command.fire_allowed=false;command.throttle=command.turn=0.;command.movement_intent=false;
            m.speed=m.turn_speed=m.push_x=m.push_z=0.;m.movement_direction=m.rotation_direction=0;
            locked[id]={{m.position[0],m.position[2],siege_yaw}};
        }
        if(a.combat.overturned||destroyed(a.combat.critical,"engineHealth")||destroyed(a.combat.critical,"leftTrackHealth")||destroyed(a.combat.critical,"rightTrackHealth"))command.throttle=command.turn=0.;
        else if(std::abs(command.throttle)>.01)command.throttle*=step::critical_factor(a.combat.critical,step::CriticalStat::mobility);
        nav::MotionPlanInput input;input.id=id;input.position=m.position;input.yaw=m.yaw;input.speed=m.speed;input.turn_speed=m.turn_speed;
        input.throttle=command.throttle;input.turn=command.turn;input.step=s.elapsed;input.now=s.now;input.half_length=a.descriptor.half_length;
        input.terrain_pitch=m.terrain_pitch;input.grounded=m.grounded_once;input.airborne=m.airborne;input.decision_due=decision.due;
        input.refresh_control=s.refresh;input.movement_intent=command.movement_intent;input.recovery=command.recovery;
        input.move_position=command.move;input.recovery_probe_distance=optional(command.probe_distance);
        const auto probe=planning_.selectedMotion(input);
        motion::Input drive;drive.dt=s.elapsed;drive.now=s.now;drive.throttle=probe.throttle;drive.turn=probe.turn;
        drive.slope_pitch=probe.slope_pitch;drive.travel_sign=probe.travel_sign;drive.path_clear=probe.path_clear;drive.pose_frozen=probe.pose_frozen;
        drive.probe_collision=probe.generic_collision;drive.exact_collision=probe.exact_collision;drive.brake=command.brake;drive.siege_locked=siege_locked;
        drive.wet_escape=planning_.grid().pointHazard(m.position,4)||a.combat.water_depth>.90;
        drive.shallow_admitted=planning_.navigator().controlledShallowStep(id,m.position,probe.travel_yaw)||planning_.navigator().controlledShallowCommitted(id,m.position,probe.travel_yaw);
        drive.maximum_probe_distance=probe.maximum_distance;drive.move_target=command.move;
        makeWorld(a);const auto trace=motion::advance(m,a.descriptor,drive,*worlds_.at(id));tick.yaw=trace.attempted_yaw;tick.passive=trace.passive_forward;
        if(command.movement_intent)tick.goal=command.move;
        for(ID invalid:physics_context_.invalidated_decisions)planning_.invalidate(invalid);
        physics_context_.invalidated_decisions.clear();
        updateCombatPose(a);weapons_runtime_.postMotion(a.weapon,combatRefs(a),a.modes[a.mode].combat,m,decisionInput(a,s),command,chosen,
            combat,s.publish,s.refresh,s.start_us,s.end_us,planning_.decisions(),services_.weaponWorld());
        for(auto launch=weapons_.pending().rbegin();launch!=weapons_.pending().rend();++launch)if(launch->actor==key(id)&&launch->edge.shot_seq==weapons_.at(key(id)).fire_seq){a.shot_angles=std::array<double,2>{{launch->yaw,launch->pitch}};break;}
        observations_.update(observationActor(a));observations_.queueCover(id,selected,command);publishPose(a,s,true);metadata[key(id)]=observationActor(a).target;ticks[id]=tick;
    }
    Output result;result.observation=observations_.finish();if(s.refresh)repair_budget_=1;
    std::vector<ID> slope;std::map<ID,Point> starts;
    for(ID id:publication_order_) {
        auto&a=actors_.at(id);auto found=ticks.find(id);if(found==ticks.end())continue;
        auto&t=found->second;starts[id]=t.start;if(!a.motion.alive)continue;auto&m=a.motion;
        const Point driven=m.position;const bool airborne=m.airborne;makeWorld(a);
        t.blocked=worlds_.at(id)->vertical(m,a.descriptor,t.start,t.yaw,s.elapsed);t.ballistic=airborne||m.airborne;
        if(!t.blocked&&!t.ballistic)t.rollback=physics_.guard_realised_pose(m,t.start,t.safe,t.yaw,
            a.modes[a.mode].motion.detailed_suspension?std::optional<physics::SuspensionSnapshot>(t.suspension):std::nullopt,true,driven);
        t.settled=m.position;slope.push_back(id);
    }
    contacts_.translations(contact_actors_,contact_humans_,starts,s.end_us,publication_order_);
    for(ID id:dictionary_order_)if(ticks.count(id)&&ticks.at(id).passive!=0.&&!locked.count(id)) {
        auto&a=actors_.at(id);if(!a.motion.alive)continue;motion::ContactResponse response;
        response.delta_velocity={{std::sin(a.motion.yaw)*ticks.at(id).passive,std::cos(a.motion.yaw)*ticks.at(id).passive}};
        motion::ContactOptions options;options.advance_push=false;options.advance_forward=true;
        motion::contact(a.motion,a.descriptor,response,s.elapsed,s.now,*worlds_.at(id),options);
    }
    auto contact_order=publication_order_;for(const auto&human:contact_humans_)contact_order.push_back(1000000+human.network_id);
    contact_order=contacts::python27_integer_order(contact_order);
    const auto previous=reports_.size();auto reports=contacts_.settle(contact_actors_,contact_humans_,contact_order,s.elapsed,s.now,*this,publication_order_);
    reports_.insert(reports_.end(),reports.begin(),reports.end());bool publish=s.publish||reports_.size()>previous;
    for(const auto&entry:locked){auto&m=actors_.at(entry.first).motion;const auto&p=entry.second;
        m.position[0]=p[0];m.position[2]=p[1];m.yaw=p[2];m.speed=m.turn_speed=m.push_x=m.push_z=0.;m.movement_direction=m.rotation_direction=0;}
    for(ID id:slope) {
        auto&a=actors_.at(id);auto&m=a.motion;auto&t=ticks.at(id);
        if(std::abs(m.position[0]-t.settled[0])>1.e-6||std::abs(m.position[2]-t.settled[2])>1.e-6) {
            Point rollback{{m.position[0],t.settled[1],m.position[2]}};makeWorld(a,t.settled);
            // Always evaluate the endpoint even if the earlier support failed.
            const bool blocked=worlds_.at(id)->vertical(m,a.descriptor,rollback,t.yaw,0.);t.blocked=t.blocked||blocked;
        }
        if(!t.blocked&&!t.ballistic&&!m.airborne)t.rollback=physics_.guard_realised_pose(m,t.settled,false,t.yaw,{},false)||t.rollback;
        if(t.blocked&&t.goal){planning_.navigator().reportHardContact(id,t.start,*t.goal,t.yaw,s.now);physics_.invalidate(id,t.yaw,true);}
        else if(!t.rollback&&!t.ballistic&&distance(t.start,t.settled)>1.e-6)planning_.navigator().clearBlockedContact(id);
    }
    if(s.refresh&&!slope.empty()) {
        const auto start=slope_cursor_%slope.size();std::size_t visited=0,sampled=0;
        while(visited<slope.size()&&sampled<5) {
            auto&a=actors_.at(slope[(start+visited)%slope.size()]);
            const auto attempt=host_memory_.at(a.motion.id).slope_attempt;
            const bool changed=physics_.update_slope_pose(a.motion,detail(a));
            if(changed||attempt!=host_memory_.at(a.motion.id).slope_attempt)++sampled;++visited;
        }
        slope_cursor_=(start+std::max<std::size_t>(1,visited))%slope.size();
    }
    for(ID id:physics_context_.invalidated_decisions)planning_.invalidate(id);
    for(auto&entry:actors_)publishPose(entry.second,s,false);
    if(!publish)return {};
    std::vector<publication::Input> rows;for(ID id:publication_order_){auto&a=actors_.at(id);a.ledger.mark_publication(a.combat);rows.push_back(publicationActor(a));}
    result.state=publisher_.publish(rows,equipment_time_,s.end_us,weapons_.pending(),reports_);
    for(const auto&p:contacts_.push_proofs())result.pushes.push_back(p.second);
    for(const auto&p:environment_.fall_damage)result.falls.push_back({{p.first,p.second}});
    for(const auto&p:environment_.critical)result.critical.push_back({std::get<0>(p.first),std::get<1>(p.first),std::get<2>(p.first),p.second});
    for(ID id:publication_order_){const auto&a=actors_.at(id);if(a.motion.airborne||a.combat.drowning)result.hazards.push_back(id);
        if(environment_.fire_provenance.count(id))result.environment_fires.push_back(id);}return result;
}
std::vector<Output> Runtime::update(double elapsed,double now,double server_time_ms)
{
    if(callback_open_)throw std::logic_error("Native Bot callback reentered");
    ++statistics_.callbacks;services_.beginCallback(now,server_time_ms);physics_context_.server_time_ms=server_time_ms;
    std::set<ID> live;for(const auto&entry:actors_)if(entry.second.motion.alive)live.insert(entry.first);
    // Navigation debt accrues even when the physical clock has a banked tail.
    if(!clock_.begin(elapsed,now,bursts())) {planning_.navigator().beginFrame(std::max(0.,elapsed));planning_.navigator().endFrame();return {};}
    callback_open_=true;std::vector<Output> output;
    try {
        planning_.beginFrame(std::max(0.,elapsed),live);planning_.navigator().tick(now);
        while(auto slice=clock_.next(bursts())) {
            const auto started=std::chrono::steady_clock::now();const auto actors_before=statistics_.actors;
            auto result=advance(*slice);
            const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count());
            if(slice->refresh){++statistics_.refresh_slices;statistics_.refresh_ns+=ns;statistics_.refresh_actors+=statistics_.actors-actors_before;}
            else{++statistics_.reuse_slices;statistics_.reuse_ns+=ns;statistics_.reuse_actors+=statistics_.actors-actors_before;}
            clock_.complete(*slice);if(result)output.push_back(std::move(*result));}
        for(const auto&wait:contacts_.take_waits())planning_.planner().wait_for_traffic(wait.first,wait.second);
        for(ID id:publication_order_){const auto&a=actors_.at(id);if(a.motion.alive&&a.combat.health>0&&std::abs(a.motion.speed)>=1.)services_.scanDestructibles(a.motion);}
        planning_.finishPerception(false);planning_.endFrame();clock_.finish();callback_open_=false;
    }catch(...){planning_.finishPerception(false);planning_.endFrame();clock_.finish();callback_open_=false;throw;}
    return output;
}
}
