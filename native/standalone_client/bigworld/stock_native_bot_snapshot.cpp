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
void mode(PyObject*runtime,PyObject*battle,PyObject*state,PyObject*descriptor,PyObject*profile,Mode&value){
    if(!readMotionMode(runtime,state,descriptor,value.motion)||
       !step::load_combat_mode(runtime,state,descriptor,profile,value.combat)||
       !physics::readPhysicsSnapshot(battle,state,descriptor,envelope(descriptor),value.motion.detailed_suspension,value.physics)||
       !planning::readNavigationMode(runtime,state,descriptor,value.navigation)||
       !planning::readPerceptionMode(runtime,state,descriptor,value.perception))throw p::Error("Native Bot mode installation failed");
    value.spawn_drive_cap=stock_native_bot_descriptor::driveCap(descriptor,value.motion.descriptor.drive.forward,value.motion.descriptor.drive.reverse,0.);

}
}
bool readNativeSnapshot(PyObject*runtime,PyObject*battle,Snapshot&output){try{
    Snapshot next;if(!planning::readPlanningSnapshot(runtime,next.planning))return false;
    next.identity.round=p::integral(runtime,"round_id");next.identity.generation=p::integral(runtime,"_native_generation");
    next.clock.period=p::numeric(runtime,"_control_seconds");next.clock.bank=p::numeric(runtime,"_accumulator");
    next.clock.refresh_bank=p::numeric(runtime,"_control_refresh_accumulator");next.clock.sample_us=p::integral(runtime,"_sample_time_us");
    next.clock.next_publication=p::numeric(runtime,"_next_publication");next.equipment_time=p::numeric(runtime,"_equipment_now");next.slope_cursor=std::size_t(p::integral(runtime,"_slope_pose_cursor"));
    if(!p::truth(runtime,"_fixed_control")||next.clock.period<=0.)throw p::Error("Native Bot owner requires the installed fixed control clock");
    auto camera=p::value(runtime,"_camera_position");if(camera.get()!=Py_None)next.camera=point(camera.get());
    auto states=p::value(runtime,"states"),descriptors=p::value(runtime,"_descriptors"),pairs=p::value(runtime,"_descriptor_pairs");
    if(!PyDict_Check(states.get())||!PyDict_Check(descriptors.get())||!PyDict_Check(pairs.get()))throw p::Error("Native Bot actor installation maps are invalid");
    Py_ssize_t cursor=0;PyObject*id=nullptr,*state=nullptr;
    while(PyDict_Next(states.get(),&cursor,&id,&state)){
        const auto actor_id=p::wide(id);auto*d=PyDict_GetItem(descriptors.get(),id);if(!d)throw p::Error("Native Bot actor lacks its descriptor");auto profile=p::value(state,"profile");
        ActorSnapshot actor;if(!readMotionSnapshot(runtime,state,d,actor.motion)||!step::load_combat_snapshot(runtime,state,d,profile.get(),actor.combat))return false;
        auto*pair=PyDict_GetItem(pairs.get(),id);PyObject*mode0=d,*mode1=d;
        if(pair&&pair!=Py_None){if(p::size(pair)!=2)throw p::Error("Native Bot descriptor mode width mismatch");auto a=p::item(pair,0),b=p::item(pair,1);mode0=a.get();if(b.get()!=Py_None){mode1=b.get();actor.has_siege=true;}else mode1=mode0;
            mode(runtime,battle,state,mode0,profile.get(),actor.modes[0]);if(actor.has_siege)mode(runtime,battle,state,mode1,profile.get(),actor.modes[1]);else actor.modes[1]=actor.modes[0];
        }else{mode(runtime,battle,state,d,profile.get(),actor.modes[0]);actor.modes[1]=actor.modes[0];}
        actor.weapon.aim_yaw=p::numeric(state,"aim_yaw",actor.motion.state.yaw);actor.weapon.round_id=next.identity.round;actor.weapon.team=actor.motion.team;actor.weapon.gunner=actor.combat.gunner;
        actor.weapon.hull_aiming=p::truth(state,"hull_aiming");actor.weapon.descriptor_revision=1;
        actor.weapon.rating=stock_native_bot_descriptor::botRating(runtime,id);
        const auto strategy=next.planning.director.agents.find(actor_id);if(strategy==next.planning.director.agents.end())throw p::Error("Native Bot actor lacks strategy metadata");
        actor.weapon.class_tag=strategy->second.profile.class_tag;
        auto&t=actor.metadata;t.id=actor_id;t.identity={"bot",actor_id};t.team=actor.motion.team;t.position=actor.motion.state.position;t.health=actor.combat.state.health;t.max_health=actor.combat.state.maximum_health;t.armor=strategy->second.profile.armor;t.class_tag=actor.weapon.class_tag;t.alive=actor.motion.state.alive;t.speed=actor.motion.state.speed;
        auto yaw=p::value(state,"shot_yaw"),pitch=p::value(state,"shot_pitch");if(yaw.get()!=Py_None&&pitch.get()!=Py_None)actor.shot_angles={{p::scalar(yaw.get()),p::scalar(pitch.get())}};
        actor.world_pose=p::truth(state,"world_pose",true);next.actors.push_back(std::move(actor));
        next.actors.back().spawn_anchor=stock_native_bot_descriptor::spawnAnchor(battle,next.actors.back().motion.team,next.actors.back().motion.slot);
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
