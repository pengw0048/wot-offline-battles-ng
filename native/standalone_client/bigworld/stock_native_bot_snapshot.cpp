#include "stock_native_bot_snapshot.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_combat_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include <algorithm>

namespace stock_native_bot {
namespace {
namespace p=stock_native_bot_python;
Point point(PyObject*value){Point out;for(int i=0;i<3;++i){auto v=p::item(value,i);out[i]=p::scalar(v.get());}return out;}
stock_native_bot_turret::MovingDescriptor envelope(PyObject*d){
    stock_native_bot_turret::MovingDescriptor result;const char*names[]={"chassis","hull"};
    stock_native_bot_turret::Component*targets[]={&result.chassis,&result.hull};
    for(int i=0;i<2;++i){auto part=p::value(d,names[i]),tester=p::value(part.get(),"hitTester"),bbox=p::value(tester.get(),"bbox");auto lo=p::item(bbox.get(),0),hi=p::item(bbox.get(),1);targets[i]->bounds={point(lo.get()),point(hi.get())};}
    auto chassis=p::value(d,"chassis"),offset=p::value(chassis.get(),"hullPosition");result.hull.offset=point(offset.get());return result;
}
void mode(const BotSeed&bot,PyObject*descriptor,Mode&value){
    if(!readMotionMode(bot,descriptor,value.motion)||
       !step::load_combat_mode(bot,descriptor,value.combat)||
       !physics::readPhysicsSnapshot(bot.state,descriptor,bot.siege?bot.travel.get():nullptr,envelope(descriptor),value.motion.detailed_suspension,value.physics)||
       !planning::readNavigationMode(bot.state,descriptor,value.navigation)||
       !planning::readPerceptionMode(bot,descriptor,value.perception))throw p::Error("Native Bot mode installation failed");
    value.spawn_drive_cap=stock_native_bot_descriptor::driveCap(descriptor,value.motion.descriptor.drive.forward,value.motion.descriptor.drive.reverse,0.);

}
}
bool readNativeSnapshot(const RoundSeed&seed,const ServerOrders&orders,double refresh_bank,Snapshot&output){try{
    Snapshot next;if(!planning::readPlanningSnapshot(seed,orders,next.planning))return false;
    // The round start's fixed control clock: nothing has advanced before the cold installation.
    next.identity.round=seed.round;next.identity.generation=seed.generation;
    next.clock.period=seed.control_seconds;next.clock.bank=0.;next.clock.refresh_bank=refresh_bank;next.clock.sample_us=0;
    next.clock.next_publication=0.;next.equipment_time=0.;next.slope_cursor=0;
    if(next.clock.period<=0.)throw p::Error("Native Bot owner requires the installed fixed control clock");
    for(const auto&row:seed.bots){
        const auto actor_id=row.first;const auto&bot=row.second;const auto&state=bot.state;PyObject*d=activeDescriptor(bot);
        ActorSnapshot actor;if(!readMotionSnapshot(bot,d,actor.motion)||!step::load_combat_snapshot(bot,d,actor.combat))return false;
        mode(bot,bot.travel.get(),actor.modes[0]);
        if(bot.siege){actor.has_siege=true;mode(bot,bot.siege.get(),actor.modes[1]);}else actor.modes[1]=actor.modes[0];
        const auto&aim=state.get("aim_yaw");actor.weapon.aim_yaw=aim.kind==stock_native_client_state::Value::Kind::Null?actor.motion.state.yaw:aim.numeric();
        actor.weapon.round_id=next.identity.round;actor.weapon.team=actor.motion.team;actor.weapon.gunner=actor.combat.gunner;
        actor.weapon.hull_aiming=state.get("hull_aiming").truth();actor.weapon.descriptor_revision=1;
        actor.weapon.rating=bot.rating;
        const auto strategy=next.planning.director.agents.find(actor_id);if(strategy==next.planning.director.agents.end())throw p::Error("Native Bot actor lacks strategy metadata");
        actor.weapon.class_tag=strategy->second.profile.class_tag;
        auto&t=actor.metadata;t.id=actor_id;t.identity={"bot",actor_id};t.team=actor.motion.team;t.position=actor.motion.state.position;t.health=actor.combat.state.health;t.max_health=actor.combat.state.maximum_health;t.armor=strategy->second.profile.armor;t.class_tag=actor.weapon.class_tag;t.alive=actor.motion.state.alive;t.speed=actor.motion.state.speed;
        const auto&yaw=state.get("shot_yaw"),&pitch=state.get("shot_pitch");if(yaw.kind!=stock_native_client_state::Value::Kind::Null&&pitch.kind!=stock_native_client_state::Value::Kind::Null)actor.shot_angles={{yaw.numeric(),pitch.numeric()}};
        actor.world_pose=state.object.count("world_pose")?state.get("world_pose").truth():true;next.actors.push_back(std::move(actor));
        next.actors.back().spawn_anchor=bot.spawn;
    }
    output=std::move(next);return true;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readNativeServerSnapshot(PyObject*raw,PyObject*tick,PyObject*time,const Actor&actor,const offline_simulation::weapons::ActorState&weapon,ServerSnapshot&output){try{
    // Preserve the source's missing-field defaults without copying the full
    // server row, and parse the complete transaction before changing an actor.
    p::Ref combat(PyDict_Copy(raw));
    if(!p::field(raw,"health"))p::set(combat.get(),"health",p::integer(actor.combat.health));
    if(!p::field(raw,"alive"))p::set(combat.get(),"alive",p::boolean(actor.combat.alive));
    if(!p::field(raw,"critical")){p::Ref current(step::combat_snapshot_value(actor.combat,actor.siege,weapon,{}));auto critical=p::value(current.get(),"critical");p::set(combat.get(),"critical",critical);}
    ServerSnapshot next;if(!step::readServerCombat(combat.get(),tick,actor.combat.critical,next.combat))return false;
    next.fire_sequence=p::integral(raw,"fire_seq");next.server_time_ms=time&&time!=Py_None?p::wide(time):0;
    auto reload=p::value(raw,"reload_time"),duration=p::value(raw,"reload_duration");next.has_reload=reload.get()!=Py_None&&duration.get()!=Py_None;
    if(next.has_reload){next.reload_time=p::scalar(reload.get());next.reload_duration=p::scalar(duration.get());}
    auto clip=p::value(raw,"clip"),clip_size=p::value(raw,"clip_size");if(clip.get()!=Py_None&&clip_size.get()!=Py_None)next.clip={{int(p::wide(clip.get())),int(p::wide(clip_size.get()))}};
    if(p::field(raw,"ammo_remaining")&&p::field(raw,"shell_index")&&p::field(raw,"next_shell_index")&&p::field(raw,"ammo_reload_pending")){
        auto ammo=weapon.ammo;auto counts=p::value(raw,"ammo_remaining");ammo.remaining.clear();for(Py_ssize_t i=0;i<p::size(counts.get());++i){auto count=p::item(counts.get(),i);const auto n=p::wide(count.get());if(n<0||n>2147483647)throw p::Error("Native ammunition count invalid");ammo.remaining.push_back(int(n));}
        ammo.loaded=int(p::integral(raw,"shell_index"));ammo.next=int(p::integral(raw,"next_shell_index"));ammo.reload_pending=p::truth(raw,"ammo_reload_pending");ammo.plan_pending=false;ammo.validate(weapon.gun.config.shell_count);next.ammunition=std::move(ammo);
    }
    output=std::move(next);return true;
}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
}
