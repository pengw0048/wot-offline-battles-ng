#include "pch.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_services.hpp"
#include "stock_native_bot_human_snapshot.hpp"
#include "stock_native_bot_human_history.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_bot_foliage_snapshot.hpp"
#include "stock_native_bot_publication_python.hpp"
#include "stock_native_bot_presentation.hpp"
#include "stock_native_world_snapshot.hpp"
#include "stock_native_world_events.hpp"
#include "stock_native_player_contacts_python.hpp"
#include "stock_native_player_environment_python.hpp"
#include "stock_native_human_ram.hpp"
#include "stock_native_detached_turret_python.hpp"
#include "stock_native_projectile_python.hpp"
#include "space/space_manager.hpp"
#include "pyscript/script.hpp"
#include <algorithm>
#include <chrono>
#include <memory>

namespace stock_native_bot {
namespace {
namespace p=stock_native_bot_python;
namespace world=stock_native_world;
namespace foliage=stock_native_bot_foliage;
namespace player_contacts=stock_native_player_contacts;
namespace player_environment=stock_native_player_environment;
const char* const capsuleName="offline_bot_runtime_native.owner";
void attribute(PyObject*o,const char*k,const p::Ref&v){if(PyObject_SetAttrString(o,k,v.get())<0)throw p::Error("Native Bot mirror assignment failed");}
void require(bool ok){if(!ok)throw p::Error("Native Bot installation failed");}
Key botKey(ID id){return {offline_simulation::ActorKind::Bot,id};}
Key humanKey(ID id){return {offline_simulation::ActorKind::Human,id};}
std::string recordName(Key key){return std::string(key.kind==offline_simulation::ActorKind::Human?"player:":"bot:")+std::to_string(key.id);}
void localError(PyObject*runtime,const std::exception&error){
    // A malformed ingress or unavailable resource is local to that row.
    // Keep an inspectable counter without formatting a log on every slice.
    std::string detail(error.what());
    if(PyErr_Occurred()){
        PyObject*type=nullptr,*value=nullptr,*traceback=nullptr;
        PyErr_Fetch(&type,&value,&traceback);
        if(value){PyObject*message=PyObject_Str(value);
            if(message){if(PyString_Check(message)){detail+="; ";detail.append(PyString_AS_STRING(message),PyString_GET_SIZE(message));}Py_DECREF(message);}}
        Py_XDECREF(type);Py_XDECREF(value);Py_XDECREF(traceback);
    }
    PyErr_Clear();attribute(runtime,"_native_bot_ingress_errors",p::integer(p::integral(runtime,"_native_bot_ingress_errors")+1));
    attribute(runtime,"_native_bot_last_ingress_error",p::text(detail));
}
struct RecordVersion {
    ID engine=0;bool ready=false,tombstone=false;PyObject* descriptor=nullptr;
    ResourceStamp resources;
    bool operator==(const RecordVersion&o)const{return engine==o.engine&&ready==o.ready&&tombstone==o.tombstone&&descriptor==o.descriptor&&resources==o.resources;}
};
struct PlayerTrees final:player_contacts::TreeAuthority {
    world::WorldEvents& events;
    explicit PlayerTrees(world::WorldEvents&e):events(e){}
    player_contacts::TreeStatus classify(const world::Key&key)override{
        switch(events.trustedTreeStatus(key)){
        case world::TreeIdentity::Tree:return player_contacts::TreeStatus::Tree;
        case world::TreeIdentity::Pending:return player_contacts::TreeStatus::Pending;
        case world::TreeIdentity::Other:return player_contacts::TreeStatus::Other;
        default:return player_contacts::TreeStatus::Hard;
        }
    }
    world::ContactResult commit(const player_contacts::Token&token,const Point&start,
        const Point&end,double yaw,double speed,double now)override{
        return events.commitTrustedTrees(token,start,end,yaw,speed,now);
    }
};
struct PlayerEnvironmentWorld final:player_environment::World {
    world::NativeScene& scene;geometry::Vehicles& vehicles;
    PlayerEnvironmentWorld(world::NativeScene&s,geometry::Vehicles&v):scene(s),vehicles(v){}
    std::optional<player_environment::Pose> bodyPose(const player_environment::Player&player)override{
        if(!player.native_remote)return {};
        const auto*vehicle=vehicles.find(humanKey(player.id));if(!vehicle||!vehicle->ready)return {};
        try{
            geometry::Pose pose;pose.position=player.pose.position;pose.yaw=player.pose.yaw;
            pose.pitch=player.pose.pitch;pose.roll=player.pose.roll;pose.siege=player.siege_state;
            const auto frame=geometry::frames(*vehicle,pose);const auto origin=frame.body.applyToOrigin();
            player_environment::Pose result;result.position={origin.x,origin.y,origin.z};
            result.yaw=frame.body.yaw();result.pitch=frame.body.pitch();result.roll=frame.body.roll();
            for(double value:{result.position[0],result.position[1],result.position[2],result.yaw,result.pitch,result.roll})
                if(!std::isfinite(value))return {};
            return result;
        }catch(const std::exception&){return {};}
    }
    double waterDepth(const Point&point)override{
        auto start=point,end=point;start[1]+=20.;end[1]-=5.;double distance=0.;
        return scene.water(start,end,distance)==world::QueryStatus::Found&&distance>=0.?20.-distance:-1.;
    }
};
struct Owner {
    BW::ClientSpacePtr space;
    world::Registry registry;
    stock_native_destructible::NativeBackend backend;
    world::NativeScene scene;
    world::WorldEvents events;
    PlayerTrees player_trees;
    player_contacts::PipelineWorld player_world;
    player_contacts::Owner player_contacts;
    geometry::Vehicles vehicles;
    PlayerEnvironmentWorld environment_world;
    player_environment::Owner environment;
    player_environment::MetadataCache environment_metadata;
    NativeServices services;
    std::unique_ptr<Runtime> runtime;
    std::unique_ptr<stock_native_detached_turret::PythonOwner> detached_runtime;
    std::unique_ptr<stock_native_projectile::NativeProjectiles> projectiles;
    std::unique_ptr<foliage::Trees> trees;
    std::map<Key,RecordVersion> installed;
    std::map<ID,HumanDescriptor> humans;
    std::map<ID,std::unique_ptr<p::Ref>> human_descriptors;
    std::uint64_t record_serial=0,cold_installs=0,ingress_ns=0,step_ns=0,publication_ns=0,contact_rejected=0;
    std::uint64_t snapshot_read_ns=0,snapshot_mirror_ns=0,snapshot_full_mirrors=0,snapshot_receipt_mirrors=0;
    ID server_tick=-1;
    double server_time_ms=0.;
    std::optional<double> last_tree_time;
    bool retired=false;
    void retire(){retired=true;scene.retire();backend.retire();}
    explicit Owner(BW::ClientSpacePtr s):space(s),backend(s,registry,[this](const world::TurretSweep&q){
        if(!runtime)return true;
        const auto a=runtime->actors().find(ID(q.actor));if(a==runtime->actors().end())return true;
        return !services.turrets().blocks(q,a->second.modes[a->second.mode].physics.geometry.turret,server_time_ms);
    }),scene(s,registry,backend),events(registry,backend,scene,s->id()),player_trees(events),
    player_world(registry,scene,player_trees),environment_world(scene,vehicles),services(registry,scene,vehicles){}
    bool current(PyObject*raw)const{return !retired&&runtime&&scene.current()&&
        p::integral(raw,"round_id")==runtime->identity().round&&
        p::integral(raw,"_native_generation")==runtime->identity().generation;}
    p::Ref entity(PyObject*battle,PyObject*record){
        return p::Ref(readNativeEntity(battle,p::integral(record,"engine_id")));
    }
    void projectileDescriptor(PyObject*battle,PyObject*descriptor,ServiceEntity&row,std::size_t mode){
        row.pitch_hull_aiming[mode]=p::truth(descriptor,"isPitchHullAimingAvailable");
        auto gun=p::value(descriptor,"gun"),shots=p::value(gun.get(),"shots");
        auto host=p::value(battle,"_runtime"),types=p::value(host.get(),"vehicles"),cache=p::value(types.get(),"g_cache"),effects=p::value(cache.get(),"shotEffects");
        for(Py_ssize_t index=0;index<p::size(shots.get());++index){
            auto shot=p::item(shots.get(),index),shell=p::value(shot.get(),"shell"),effect_index=p::value(shell.get(),"effectsIndex");
            stock_native_projectile_damage::LaunchDescriptor launch;
            require(stock_native_projectile_damage::readLaunchDescriptor(shot.get(),launch));
            row.launch_shots[mode].push_back(std::move(launch));
            PyObject*raw=PyObject_GetItem(effects.get(),effect_index.get());
            if(!raw){PyErr_Clear();continue;}
            p::Ref effect(raw);auto stickers=p::value(effect.get(),"targetStickers");
            for(std::size_t hit=0;hit<2;++hit){
                auto sticker=p::value(stickers.get(),hit?"armorPierced":"armorResisted");
                if(PyBool_Check(sticker.get())||(!PyInt_Check(sticker.get())&&!PyLong_Check(sticker.get())))continue;
                const auto value=PyLong_AsLongLong(sticker.get());
                if(PyErr_Occurred()){PyErr_Clear();continue;}
                if(value>=0&&value<=255)row.projectile_stickers[mode][int(index)][hit]=unsigned(value);
            }
        }
    }
    void installBot(PyObject*raw,PyObject*battle,Key key,PyObject*record,PyObject*descriptor){
        const auto a=runtime->actors().find(key.id);if(a==runtime->actors().end())return;
        auto e=entity(battle,record);if(e.get()==Py_None||!p::truth(e.get(),"isStarted"))return;
        auto pairs=p::value(raw,"_descriptor_pairs"),id=p::integer(key.id);
        auto*pair=PyDict_Check(pairs.get())?PyDict_GetItem(pairs.get(),id.get()):nullptr;
        auto d0=pair&&pair!=Py_None?p::item(pair,0):p::retain(descriptor);
        auto d1=pair&&pair!=Py_None?p::item(pair,1):p::retain(Py_None);
        ServiceEntity row;row.key=key;row.team=a->second.team;row.mode=a->second.mode;row.record_ready=p::truth(record,"ready");
        row.radius=std::hypot(a->second.descriptor.half_length,a->second.descriptor.half_width);row.alive=a->second.motion.alive;
        row.target.record_revision=++record_serial;row.target.descriptor_revision=a->second.weapon.descriptor_revision;
        require(geometry::readVehicle(d0.get(),e.get(),true,record_serial,row.geometry[0]));
        require(step::load_internal_layout(d0.get(),row.interior[0]));row.aim_samples[0]=a->second.modes[0].combat.target_samples;
        p::Ref critical(readNativeEntityField(battle,e.get(),"_offline_critical_descriptor"));
        auto accepted=p::value(record,"state"),effective=p::value(accepted.get(),"effective_params"),profile=p::value(effective.get(),"critical");
        stock_native_projectile_damage::TargetDescriptor target_descriptor;
        require(stock_native_projectile_damage::readTargetDescriptor(d0.get(),critical.get(),target_descriptor,profile.get()));
        row.projectile[0]=std::move(target_descriptor);
        projectileDescriptor(battle,d0.get(),row,0);
        if(d1.get()!=Py_None){require(geometry::readVehicle(d1.get(),e.get(),true,record_serial,row.geometry[1]));
            require(step::load_internal_layout(d1.get(),row.interior[1]));row.aim_samples[1]=a->second.modes[1].combat.target_samples;
            require(stock_native_projectile_damage::readTargetDescriptor(d1.get(),critical.get(),target_descriptor,profile.get()));
            row.projectile[1]=std::move(target_descriptor);
            projectileDescriptor(battle,d1.get(),row,1);
        }else{row.geometry[1]=row.geometry[0];row.interior[1]=row.interior[0];row.aim_samples[1]=row.aim_samples[0];row.projectile[1]=row.projectile[0];row.pitch_hull_aiming[1]=row.pitch_hull_aiming[0];row.projectile_stickers[1]=row.projectile_stickers[0];row.launch_shots[1]=row.launch_shots[0];}
        row.available=row.geometry[row.mode].ready;row.target.available=row.available;
        services.install(std::move(row));++cold_installs;
    }
    void records(PyObject*raw,PyObject*battle){
        auto records=p::value(battle,"_records"),descriptors=p::value(raw,"_descriptors"),pairs=p::value(raw,"_descriptor_pairs");
        if(!PyDict_Check(records.get())||!PyDict_Check(descriptors.get())||!PyDict_Check(pairs.get()))throw p::Error("Native Bot lifecycle records invalid");
        std::set<Key> present;Py_ssize_t cursor=0;PyObject*name=nullptr,*record=nullptr;
        while(PyDict_Next(records.get(),&cursor,&name,&record)){
            if(!PyDict_Check(record))continue;
            try{
                auto kind=p::value(record,"kind");const auto tag=p::string(kind.get());if(tag!="bot"&&tag!="player")continue;
                const ID id=p::integral(record,"network_id");if(id<=0)continue;const Key key=tag=="bot"?botKey(id):humanKey(id);present.insert(key);
                auto index=p::integer(id);PyObject*descriptor=tag=="bot"?PyDict_GetItem(descriptors.get(),index.get()):
                    (human_descriptors.count(id)?human_descriptors.at(id)->get():nullptr);
                RecordVersion version;version.engine=p::integral(record,"engine_id");version.ready=p::truth(record,"ready");
                version.tombstone=p::truth(record,"tombstone");version.descriptor=descriptor;
                // Accepted crew/equipment profiles are immutable within this round.
                // Network dictionary identity is not a resource lifecycle change.
                std::vector<PyObject*> modes{descriptor};
                auto*pair=key.kind==offline_simulation::ActorKind::Bot?PyDict_GetItem(pairs.get(),index.get()):nullptr;
                if(pair&&pair!=Py_None&&(PyTuple_Check(pair)||PyList_Check(pair))){modes.clear();for(Py_ssize_t i=0;i<PySequence_Fast_GET_SIZE(pair);++i)modes.push_back(PySequence_Fast_GET_ITEM(pair,i));}
                auto e=version.ready&&!version.tombstone?entity(battle,record):p::retain(Py_None);
                require(readResourceStamp(e.get(),modes,version.resources,battle));
                const auto old=installed.find(key);if(old!=installed.end()&&old->second==version)continue;
                installed[key]=version;services.remove(key);services.recordReady(key,version.ready&&!version.tombstone);
                if(key.kind==offline_simulation::ActorKind::Human){
                    if(humans.count(id)&&descriptor)installHumanGeometry(raw,battle,id,descriptor,record);
                    continue;
                }
                if(version.ready&&!version.tombstone&&descriptor)installBot(raw,battle,key,record,descriptor);
            }catch(const std::exception&e){localError(raw,e);}
        }
        for(auto i=installed.begin();i!=installed.end();){if(!present.count(i->first)){services.remove(i->first);if(i->first.kind==offline_simulation::ActorKind::Human){humans.erase(i->first.id);human_descriptors.erase(i->first.id);}i=installed.erase(i);}else ++i;}
    }
    void installHuman(PyObject*raw,PyObject*battle,PyObject*input,ID id){
        p::Ref args(PyTuple_Pack(1,input));auto descriptor=p::call(raw,"player_descriptor_resolver",args.get());
        if(descriptor.get()==Py_None)throw p::Error("Native human descriptor is unavailable");
        HumanDescriptor frozen;require(readHumanDescriptor(raw,input,descriptor.get(),frozen));
        frozen.spawn_anchor=stock_native_bot_descriptor::spawnAnchor(
            battle,int(p::integral(input,"team",1)),int(p::integral(input,"slot")));
        const Key key=humanKey(id);
        auto records=p::value(battle,"_records");auto*record=PyDict_GetItemString(records.get(),recordName(key).c_str());
        human_descriptors[id].reset(new p::Ref(p::retain(descriptor.get())));humans[id]=std::move(frozen);
        installHumanGeometry(raw,battle,id,descriptor.get(),record);++cold_installs;
    }
    void installHumanGeometry(PyObject*raw,PyObject*battle,ID id,PyObject*descriptor,PyObject*record){
        auto&frozen=humans.at(id);ServiceEntity row;row.key=humanKey(id);row.team=frozen.template_actor.config.team;
        row.target.record_revision=++record_serial;row.target.descriptor_revision=record_serial;
        if(record&&p::truth(record,"ready")&&!p::truth(record,"tombstone")){
            row.record_ready=true;
            try{auto e=entity(battle,record);if(e.get()!=Py_None&&p::truth(e.get(),"isStarted")){
                    require(geometry::readVehicle(descriptor,e.get(),p::truth(record,"native_remote"),record_serial,row.geometry[0]));
                    require(step::load_internal_layout(descriptor,row.interior[0]));
                    p::Ref critical(readNativeEntityField(battle,e.get(),"_offline_critical_descriptor"));
                    auto accepted=p::value(record,"state"),effective=p::value(accepted.get(),"effective_params"),profile=p::value(effective.get(),"critical");
                    stock_native_projectile_damage::TargetDescriptor target_descriptor;
                    require(stock_native_projectile_damage::readTargetDescriptor(descriptor,critical.get(),target_descriptor,profile.get()));
                    row.projectile[0]=std::move(target_descriptor);row.projectile[1]=row.projectile[0];
                    projectileDescriptor(battle,descriptor,row,0);row.pitch_hull_aiming[1]=row.pitch_hull_aiming[0];row.projectile_stickers[1]=row.projectile_stickers[0];row.launch_shots[1]=row.launch_shots[0];
                    row.geometry[1]=row.geometry[0];row.interior[1]=row.interior[0];row.available=row.geometry[0].ready;
                }
            }catch(const std::exception&e){localError(raw,e);row.available=false;}
        }
        row.aim_samples[0]=frozen.aim_samples;row.aim_samples[1]=frozen.aim_samples;
        frozen.target_available=row.available;frozen.record_revision=row.target.record_revision;frozen.descriptor_revision=row.target.descriptor_revision;
        services.install(std::move(row));
    }
    void humanInput(PyObject*raw,PyObject*battle,PyObject*players,bool is_prepared=false){
        std::vector<Human> accepted;std::set<ID> present;
        p::Ref prepared=is_prepared?(players&&players!=Py_None?p::retain(players):p::list()):p::Ref(prepareHumanInput(players,battle,raw));
        for(Py_ssize_t i=0;i<p::size(prepared.get());++i){auto row=p::item(prepared.get(),i);if(!PyDict_Check(row.get()))continue;
            try{
                const ID id=p::integral(row.get(),"id");if(id<=0||!p::truth(row.get(),"world_pose"))continue;
                if(!present.insert(id).second)continue;
                if(!humans.count(id))installHuman(raw,battle,row.get(),id);
                HumanIngress input;require(readHumanInput(row.get(),humans.at(id),input));
                services.updateHuman(humanKey(id),input.target,input.human.perception.config.team,input.target.alive,input.target.available,input.radius);
                accepted.push_back(std::move(input.human));
            }catch(const std::exception&e){localError(raw,e);}
        }
        for(auto i=humans.begin();i!=humans.end();){if(!present.count(i->first)){services.remove(humanKey(i->first));human_descriptors.erase(i->first);i=humans.erase(i);}else ++i;}
        runtime->humans(std::move(accepted));
    }
    void detached(PyObject*raw,PyObject*battle){
        auto rows=p::value(battle,"_detached_turret_rows");if(rows.get()==Py_None)return;
        if(!PyDict_Check(rows.get()))throw p::Error("Native detached turret mapping invalid");
        Py_ssize_t cursor=0;PyObject*key=nullptr,*value=nullptr;
        while(PyDict_Next(rows.get(),&cursor,&key,&value))try{
            stock_native_bot_turret::Record record;require(physics::readDetachedTurretRow(value,record));
            // Visual-only debris never enters collision or navigation in the
            // pinned runtime, including after the visual arc has landed.
            if(!record.has_body){services.turrets().remove(record.owner);continue;}
            const auto*vehicle=vehicles.find(record.owner.human?humanKey(record.owner.id):botKey(record.owner.id));
            if(vehicle)services.turrets().put(record,physics::detachedDescriptor(*vehicle));
        }catch(const std::exception&e){localError(raw,e);}
    }
    void frame(double now,bool force=false){
        if(!std::isfinite(now))throw p::Error("Native world frame time invalid");
        const double elapsed=last_tree_time?std::max(0.,now-*last_tree_time):0.;last_tree_time=now;
        require(events.mappingChanged(now));
        backend.runtime().update(elapsed,now);
        if(!trees)return;
        for(const auto&event:registry.journal)if(event.destruction.kind==world::Kind::Tree)trees->activate(event.destruction.key);
        for(const auto&key:events.takeActivatedTrees())trees->activate(key);
        std::set<foliage::Key> active;for(const auto&body:backend.runtime().bodies())if(body.order.kind==world::Kind::Tree)active.insert(body.order.key);
        offline_visibility::Update changes;if(trees->refresh(now,active,backend,changes,force))runtime->foliage(std::move(changes));
    }
    void sync(PyObject*raw){
        auto states=p::value(raw,"states"),descriptors=p::value(raw,"_descriptors"),pairs=p::value(raw,"_descriptor_pairs");
        for(const auto&entry:runtime->actors()){
            auto key=p::integer(entry.first);auto*state=PyDict_GetItem(states.get(),key.get());if(!state||!PyDict_Check(state))continue;
            p::Ref value(packNativeActor(entry.second,runtime->weapons().at(botKey(entry.first)),runtime->contactActor(entry.first),state,runtime->equipmentTime()));
            if(PyDict_Update(state,value.get())<0)throw p::Error("Native Bot mirror update failed");
            auto*pair=PyDict_GetItem(pairs.get(),key.get());if(pair&&pair!=Py_None){auto descriptor=p::item(pair,entry.second.mode);
                if(descriptor.get()!=Py_None&&PyDict_SetItem(descriptors.get(),key.get(),descriptor.get())<0)throw p::Error("Native descriptor mirror update failed");}
        }
        const auto&clock=runtime->clock();attribute(raw,"_sample_time_us",p::integer(clock.sample_us));
        attribute(raw,"_accumulator",p::number(clock.bank));attribute(raw,"_control_refresh_accumulator",p::number(clock.refresh_bank));
        attribute(raw,"_next_publication",p::number(clock.next_publication));attribute(raw,"_last_update_control_steps",p::integer(clock.callback_steps));
        attribute(raw,"_last_update_max_control_step",p::number(clock.callback_max_step));
    }
};
// Approved stock effects can synchronously close the Python handle. Every
// entry pins the owner until its native stack has unwound.
struct Holder {std::shared_ptr<Owner> owner;};
Holder* holder(PyObject*object){auto*value=static_cast<Holder*>(PyCapsule_GetPointer(object,capsuleName));if(!value)throw p::Error("Invalid native Bot owner handle");return value;}
void retire(PyObject*object){auto*value=static_cast<Holder*>(PyCapsule_GetPointer(object,capsuleName));if(value)delete value;else PyErr_Clear();}
PyObject* fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
using Time=std::chrono::steady_clock;
std::uint64_t nanos(Time::time_point start){return std::chrono::duration_cast<std::chrono::nanoseconds>(Time::now()-start).count();}
PyObject* create(PyObject*,PyObject*args){try{
    PyObject*raw=nullptr,*battle=nullptr;if(!PyArg_ParseTuple(args,"OO",&raw,&battle))return nullptr;
    auto avatar=p::value(battle,"_avatar");const auto id=p::integral(avatar.get(),"spaceID");
    if(id<=0||id>2147483647)throw p::Error("Native Bot space identity invalid");
    auto space=BW::SpaceManager::instance().space(BW::SpaceID(id));
    if(!space||space->cleared())throw p::Error("Native Bot space is unavailable");
    std::unique_ptr<Holder> handle(new Holder);handle->owner.reset(new Owner(space));auto&owner=*handle->owner;
    require(owner.events.install(battle));
    owner.environment=player_environment::Owner(p::numeric(battle,"_player_environment_check"),p::integral(battle,"_player_environment_seq"));
    Snapshot snapshot;require(readNativeSnapshot(raw,battle,snapshot));
    require(foliage::readFoliageSnapshot(battle,snapshot.foliage));
    foliage::TreeState trees;require(foliage::readFoliageTreeState(battle,trees));owner.trees.reset(new foliage::Trees(std::move(trees)));
    p::Ref builder(PyImport_ImportModule("gui.mods.offline_lan_0922.stock_native_world_snapshot"));
    auto sensor=p::value(battle,"_destructibles");p::Ref build_args(PyTuple_Pack(1,sensor.get()));auto dto=p::call(builder.get(),"build",build_args.get());
    require(owner.backend.installPresenter());require(world::installWorldSnapshot(dto.get(),owner.registry,owner.scene,owner.backend));
    owner.runtime.reset(new Runtime(std::move(snapshot),owner.registry,owner.scene,owner.services));
    owner.services.bind(*owner.runtime);owner.server_tick=p::integral(raw,"_server_tick",-1);
    owner.records(raw,battle);
    p::Ref battle_module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime"));
    const auto flags=p::integral(battle_module.get(),"VEHICLE_SKIP_FLAGS");
    if(flags<0||flags>65535)throw p::Error("Native vehicle collision flags invalid");
    owner.detached_runtime.reset(new stock_native_detached_turret::PythonOwner(*owner.runtime,owner.registry,
        owner.scene,owner.vehicles,owner.services,std::uint16_t(flags)));
    require(owner.detached_runtime->install(battle));
    owner.projectiles.reset(new stock_native_projectile::NativeProjectiles(owner.registry,owner.scene,
        owner.vehicles,owner.events,*owner.runtime,owner.services));
    require(owner.projectiles->install(battle));
    auto capsule=p::Ref(PyCapsule_New(handle.get(),capsuleName,retire));handle.release();return capsule.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* close(PyObject*,PyObject*args){try{PyObject*object=nullptr;if(!PyArg_ParseTuple(args,"O",&object))return nullptr;
    auto*handle=holder(object);if(handle->owner)handle->owner->retire();
    handle->owner.reset();Py_RETURN_NONE;
}catch(const std::exception&e){return fail(e);}}
PyObject* frame(PyObject*,PyObject*args){try{PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;double now=0.;int force=0;
    if(!PyArg_ParseTuple(args,"OOOdi",&object,&raw,&battle,&now,&force))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    owner->frame(now,force!=0);Py_RETURN_TRUE;
}catch(const std::exception&e){return fail(e);}}
PyObject* prewarm(PyObject*,PyObject*args){try{PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&battle,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyInt_FromLong(0);
    owner->records(raw,battle);return PyLong_FromSize_t(owner->runtime->prewarm(now));
}catch(const std::exception&e){return fail(e);}}
PyObject* prewarmPlayers(PyObject*,PyObject*args){try{PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&battle,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyInt_FromLong(0);
    owner->records(raw,battle);auto snapshot=p::value(battle,"_last_snapshot"),players=p::value(snapshot.get(),"players");
    owner->humanInput(raw,battle,players.get());return PyLong_FromSize_t(owner->runtime->prewarmPlayers(now,p::truth(battle,"_battle_live")));
}catch(const std::exception&e){return fail(e);}}
PyObject* players(PyObject*,PyObject*args){try{PyObject*raw=nullptr,*battle=nullptr;if(!PyArg_ParseTuple(args,"OO",&raw,&battle))return nullptr;
    auto snapshot=p::value(battle,"_last_snapshot"),rows=p::value(snapshot.get(),"players");return prepareHumanInput(rows.get(),battle,raw);
}catch(const std::exception&e){return fail(e);}}
PyObject* worldEvents(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;
    if(!PyArg_ParseTuple(args,"OOO",&object,&raw,&battle))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyList_New(0);
    return owner->events.pack(battle);
}catch(const std::exception&e){return fail(e);}}
PyObject* ackWorld(PyObject*,PyObject*args){try{
    PyObject*object=nullptr;unsigned PY_LONG_LONG sequence=0;
    if(!PyArg_ParseTuple(args,"OK",&object,&sequence))return nullptr;
    auto owner=holder(object)->owner;const bool acknowledged=owner&&owner->events.ack(sequence);
    if(PyErr_Occurred())return nullptr;return PyBool_FromLong(acknowledged);
}catch(const std::exception&e){return fail(e);}}
PyObject* worldIngress(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*event=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&event,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    const bool applied=owner->events.ingress(event,now);
    if(PyErr_Occurred())localError(raw,p::Error("Canonical native world event was rejected"));
    return PyBool_FromLong(applied);
}catch(const std::exception&e){return fail(e);}}
PyObject* playerContacts(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr,*players=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOOd",&object,&raw,&battle,&players,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyList_New(0);
    owner->records(raw,battle);owner->humanInput(raw,battle,players,true);
    std::vector<player_contacts::Request> requests;
    require(player_contacts::readPlayerContacts(players,owner->humans,requests,owner->contact_rejected));
    return player_contacts::packPlayerVerdicts(owner->player_contacts.update(requests,now,owner->player_world));
}catch(const std::exception&e){return fail(e);}}
PyObject* ackPlayerContact(PyObject*,PyObject*args){try{
    PyObject*object=nullptr;unsigned PY_LONG_LONG serial=0;double now=0.;
    if(!PyArg_ParseTuple(args,"OKd",&object,&serial,&now))return nullptr;
    auto owner=holder(object)->owner;return PyBool_FromLong(owner&&owner->player_contacts.acknowledge(serial,now));
}catch(const std::exception&e){return fail(e);}}
PyObject* playerEnvironment(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;double elapsed=0.;int authority=0;
    if(!PyArg_ParseTuple(args,"OOOdi",&object,&raw,&battle,&elapsed,&authority))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_NONE;
    if(owner->environment.willSample(elapsed,authority!=0)){
        auto snapshot=p::value(battle,"_last_snapshot"),players=p::value(snapshot.get(),"players");
        owner->humanInput(raw,battle,players.get());owner->records(raw,battle);
    }
    std::optional<player_environment::Batch> batch;
    require(player_environment::readAndAdvance(battle,elapsed,authority!=0,owner->environment,owner->environment_world,owner->environment_metadata,batch));
    attribute(battle,"_player_environment_check",p::number(owner->environment.elapsed()));
    if(!batch)Py_RETURN_NONE;
    auto sequence=p::integer(batch->sequence);p::Ref observations(player_environment::observationsValue(*batch));
    return PyTuple_Pack(2,sequence.get(),observations.get());
}catch(const std::exception&e){return fail(e);}}
PyObject* ackPlayerEnvironment(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*battle=nullptr;PY_LONG_LONG sequence=0;int sent=0;
    if(!PyArg_ParseTuple(args,"OOLi",&object,&battle,&sequence,&sent))return nullptr;
    auto owner=holder(object)->owner;if(!owner)Py_RETURN_FALSE;
    owner->environment.acknowledge(sequence,sent!=0);
    attribute(battle,"_player_environment_seq",p::integer(owner->environment.sequence()));Py_RETURN_TRUE;
}catch(const std::exception&e){return fail(e);}}
PyObject* humanRam(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;
    if(!PyArg_ParseTuple(args,"OOO",&object,&raw,&battle))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyList_New(0);
    owner->records(raw,battle);
    stock_native_human_ram::NativeWorld<NativeServices,geometry::Vehicles> world(owner->services,owner->vehicles);
    std::vector<std::string> errors;p::Ref result(stock_native_human_ram::results(battle,world,&errors));
    for(const auto&error:errors)localError(raw,p::Error(error));return result.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* update(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr,*players=nullptr;double elapsed=0.,now=0.;
    if(!PyArg_ParseTuple(args,"OOOddO",&object,&raw,&battle,&elapsed,&now,&players))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyList_New(0);
    auto start=Time::now();owner->records(raw,battle);owner->humanInput(raw,battle,players,true);owner->detached(raw,battle);
    auto camera=p::value(raw,"_camera_position");owner->runtime->camera(camera.get()==Py_None?std::optional<Point>{}:p::numbers<3>(camera.get()));
    auto last=p::value(battle,"_last_snapshot");owner->server_time_ms=p::numeric(last.get(),"server_time_ms");
    owner->ingress_ns+=nanos(start);start=Time::now();
    // Original artillery jobs advance once per frame, before the Bot slices.
    owner->services.beginCallback(now,owner->server_time_ms);
    owner->runtime->weaponRuntime().advanceArtillery(now,4,owner->services.weaponWorld());
    auto result=owner->runtime->update(elapsed,now,owner->server_time_ms);
    owner->step_ns+=nanos(start);if(!owner->current(raw))return PyList_New(0);start=Time::now();
    owner->sync(raw);p::Ref packed(packNativeOutputs(result,owner->runtime->clock().sample_us));
    owner->publication_ns+=nanos(start);return packed.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* snapshot(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*message=nullptr;if(!PyArg_ParseTuple(args,"OOO",&object,&raw,&message))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_NONE;
    if(!PyDict_Check(message))Py_RETURN_NONE;
    auto started=Time::now();std::vector<std::pair<ID,bool>> mirrors;
    auto orders=p::value(message,"bot_orders"),revision=p::value(message,"bot_order_revision");
    if(orders.get()!=Py_None&&revision.get()!=Py_None)try{
        auto mapping=p::dict();const auto count=std::min<Py_ssize_t>(30,p::size(orders.get()));
        for(Py_ssize_t i=0;i<count;++i){auto row=p::item(orders.get(),i),id=p::value(row.get(),"id");auto key=p::integer(p::wide(id.get()));
            if(PyDict_GetItem(mapping.get(),key.get()))throw p::Error("Native Bot order has a duplicate actor");
            if(PyDict_SetItem(mapping.get(),key.get(),row.get())<0)throw p::Error("Native Bot order allocation failed");}
        std::map<ID,planning::StrategicOrder> values;require(planning::readPlanningOrders(mapping.get(),values));
        owner->runtime->acceptOrders(std::move(values),p::wide(revision.get()));
    }catch(const std::exception&e){localError(raw,e);}
    auto tick=p::value(message,"server_tick"),time=p::value(message,"server_time_ms");
    if(tick.get()!=Py_None){const auto incoming=p::wide(tick.get());if(incoming<owner->server_tick)Py_RETURN_NONE;owner->server_tick=incoming;attribute(raw,"_server_tick",p::integer(incoming));}
    auto rows=p::value(message,"bots");if(rows.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(rows.get());++i){auto row=p::item(rows.get(),i);if(!PyDict_Check(row.get()))continue;
        try{
            const ID id=p::integral(row.get(),"id");const auto actor=owner->runtime->actors().find(id);if(actor==owner->runtime->actors().end())continue;
            ServerSnapshot value;require(readNativeServerSnapshot(row.get(),tick.get(),time.get(),actor->second,owner->runtime->weapons().at(botKey(id)),value));
            mirrors.emplace_back(id,owner->runtime->serverSnapshot(id,value));
        }catch(const std::exception&e){localError(raw,e);}
    }
    owner->snapshot_read_ns+=nanos(started);started=Time::now();
    auto states=p::value(raw,"states");
    for(const auto&mirror:mirrors){
        auto key=p::integer(mirror.first);auto*state=PyDict_GetItem(states.get(),key.get());
        if(!state||!PyDict_Check(state))continue;
        const auto&actor=owner->runtime->actors().at(mirror.first);
        p::Ref value(packNativeSnapshotActor(actor,owner->runtime->weapons().at(botKey(mirror.first)),
            owner->runtime->contactActor(mirror.first),state,owner->runtime->equipmentTime(),mirror.second));
        if(PyDict_Update(state,value.get())<0)throw p::Error("Native snapshot mirror update failed");
        if(mirror.second)++owner->snapshot_full_mirrors;else ++owner->snapshot_receipt_mirrors;
    }
    attribute(raw,"_control_refresh_accumulator",p::number(owner->runtime->clock().refresh_bank));
    owner->snapshot_mirror_ns+=nanos(started);Py_RETURN_NONE;
}catch(const std::exception&e){return fail(e);}}
void detachedErrors(Owner&owner,PyObject*raw){
    p::Ref errors(owner.detached_runtime->takeErrors());
    for(Py_ssize_t i=0;i<p::size(errors.get());++i){auto message=p::item(errors.get(),i);localError(raw,p::Error(p::string(message.get())));}
}
PyObject* detachedQueue(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*record=nullptr,*entity=nullptr,*state=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOOOd",&object,&raw,&record,&entity,&state,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    const bool queued=owner->detached_runtime->queue(record,entity,state,now);
    if(PyErr_Occurred())localError(raw,p::Error("Native detached turret launch failed"));
    detachedErrors(*owner,raw);return PyBool_FromLong(queued);
}catch(const std::exception&e){return fail(e);}}
PyObject* detachedIngress(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*message=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&message,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    const bool changed=owner->detached_runtime->ingress(message,now);
    if(PyErr_Occurred())localError(raw,p::Error("Native detached turret snapshot failed"));
    detachedErrors(*owner,raw);return PyBool_FromLong(changed);
}catch(const std::exception&e){return fail(e);}}
PyObject* detachedAdvance(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&battle,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_NONE;
    PyObject*result=owner->detached_runtime->advance(battle,now);
    if(!result){localError(raw,p::Error("Native detached turret advance failed"));Py_RETURN_NONE;}
    p::Ref tail(result);detachedErrors(*owner,raw);owner->detached(raw,battle);return tail.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* detachedRows(PyObject*,PyObject*args){try{
    PyObject*object=nullptr;if(!PyArg_ParseTuple(args,"O",&object))return nullptr;
    auto owner=holder(object)->owner;return owner?owner->detached_runtime->rows():PyList_New(0);
}catch(const std::exception&e){return fail(e);}}
PyObject* projectileIngress(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*message=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&message,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    const bool changed=owner->projectiles->ingress(message,now);
    if(PyErr_Occurred())localError(raw,p::Error("Native projectile snapshot failed"));
    return PyBool_FromLong(changed);
}catch(const std::exception&e){return fail(e);}}
PyObject* projectileObserve(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*message=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&message,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    const bool changed=owner->projectiles->observe(message,now);
    if(PyErr_Occurred())localError(raw,p::Error("Native projectile clock message failed"));
    return PyBool_FromLong(changed);
}catch(const std::exception&e){return fail(e);}}
PyObject* projectileEvent(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*event=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOd",&object,&raw,&event,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    const bool changed=owner->projectiles->event(event,now);
    if(PyErr_Occurred())localError(raw,p::Error("Native projectile event failed"));
    return PyBool_FromLong(changed);
}catch(const std::exception&e){return fail(e);}}
PyObject* projectileAdvance(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr;double now=0.;int force=0;
    if(!PyArg_ParseTuple(args,"OOOdi",&object,&raw,&battle,&now,&force))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyList_New(0);
    p::Ref result(owner->projectiles->advance(battle,now,force!=0));
    return owner->current(raw)?result.release():PyList_New(0);
}catch(const std::exception&e){return fail(e);}}
PyObject* projectileAck(PyObject*,PyObject*args){try{
    PyObject*object=nullptr;unsigned PY_LONG_LONG serial=0;int sent=0;
    if(!PyArg_ParseTuple(args,"OKi",&object,&serial,&sent))return nullptr;
    auto owner=holder(object)->owner;return PyBool_FromLong(owner&&owner->projectiles->acknowledge(serial,sent!=0));
}catch(const std::exception&e){return fail(e);}}
PyObject* projectileLaunches(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*message=nullptr;
    if(!PyArg_ParseTuple(args,"OOO",&object,&raw,&message))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))return PyList_New(0);
    return owner->projectiles->launches(message);
}catch(const std::exception&e){return fail(e);}}
PyObject* projectileLaunchResult(PyObject*,PyObject*args){try{
    PyObject*object=nullptr;PY_LONG_LONG bot=0,sequence=0;int accepted=0;
    if(!PyArg_ParseTuple(args,"OLLi",&object,&bot,&sequence,&accepted))return nullptr;
    auto owner=holder(object)->owner;if(!owner)Py_RETURN_FALSE;
    owner->projectiles->launchResult(bot,sequence,accepted!=0);Py_RETURN_TRUE;
}catch(const std::exception&e){return fail(e);}}
PyObject* transportError(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*message=nullptr;
    if(!PyArg_ParseTuple(args,"OOO",&object,&raw,&message))return nullptr;
    auto owner=holder(object)->owner;if(owner&&owner->current(raw))localError(raw,p::Error("Native projectile transport failed: "+p::string(message)));
    Py_RETURN_NONE;
}catch(const std::exception&e){return fail(e);}}
PyObject* presentation(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr;if(!PyArg_ParseTuple(args,"OO",&object,&raw))return nullptr;
    if(object!=Py_None){auto owner=holder(object)->owner;if(owner&&owner->current(raw))owner->sync(raw);}
    auto states=p::value(raw,"states");if(!PyDict_Check(states.get()))throw p::Error("Native Bot presentation map invalid");
    struct Row {ID slot,team;PyObject*value;};std::vector<Row> rows;Py_ssize_t cursor=0;PyObject*id=nullptr,*row=nullptr;
    while(PyDict_Next(states.get(),&cursor,&id,&row))rows.push_back({p::integral(row,"slot"),p::integral(row,"team"),row});
    std::stable_sort(rows.begin(),rows.end(),[](const Row&a,const Row&b){return std::tie(a.slot,a.team)<std::tie(b.slot,b.team);});
    p::Ref result(PyTuple_New(Py_ssize_t(rows.size())));Py_ssize_t index=0;
    for(const auto&r:rows){p::Ref copy(PyDict_Copy(r.value));
        const char*private_keys[]={"_critical_parts_tick_cache","_gun_pitch_limit_cache"};
        for(const auto*k:private_keys)if(PyDict_GetItemString(copy.get(),k)&&PyDict_DelItemString(copy.get(),k)<0)throw p::Error("Native presentation cache removal failed");
        PyTuple_SET_ITEM(result.get(),index++,copy.release());}
    return result.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* applyPresentation(PyObject*,PyObject*args){try{
    PyObject*object=nullptr,*raw=nullptr,*battle=nullptr,*states=nullptr;double now=0.;
    if(!PyArg_ParseTuple(args,"OOOOd",&object,&raw,&battle,&states,&now))return nullptr;
    auto owner=holder(object)->owner;if(!owner||!owner->current(raw))Py_RETURN_FALSE;
    std::vector<std::string> errors;const bool applied=presentNativeBotPoses(battle,states,now,owner->services,errors);
    for(const auto&error:errors)localError(raw,p::Error(error));
    return PyBool_FromLong(applied);
}catch(const std::exception&e){return fail(e);}}
PyObject* ackLaunch(PyObject*,PyObject*args){try{PyObject*object=nullptr;PY_LONG_LONG id=0,sequence=0;
    if(!PyArg_ParseTuple(args,"OLL",&object,&id,&sequence))return nullptr;auto owner=holder(object)->owner;
    return PyBool_FromLong(owner&&owner->runtime->acknowledgeLaunch(id,sequence));
}catch(const std::exception&e){return fail(e);}}
PyObject* ackRam(PyObject*,PyObject*args){try{PyObject*object=nullptr;PY_LONG_LONG id=0,sequence=0;
    if(!PyArg_ParseTuple(args,"OLL",&object,&id,&sequence))return nullptr;auto owner=holder(object)->owner;
    if(owner)owner->runtime->acknowledgeRam(id,sequence);return PyBool_FromLong(owner!=nullptr);
}catch(const std::exception&e){return fail(e);}}
PyObject* stageTiming(PyObject*,PyObject*args){try{PyObject*object=nullptr;int enabled=0;
    if(!PyArg_ParseTuple(args,"Oi",&object,&enabled))return nullptr;
    auto owner=holder(object)->owner;if(owner)owner->runtime->stageTiming(enabled!=0);
    return PyBool_FromLong(owner!=nullptr);
}catch(const std::exception&e){return fail(e);}}
PyObject* statistics(PyObject*,PyObject*args){try{PyObject*object=nullptr;if(!PyArg_ParseTuple(args,"O",&object))return nullptr;
    auto out=p::dict();auto owner=holder(object)->owner;if(!owner)return out.release();
    const auto&s=owner->runtime->statistics();const auto&d=owner->backend.presentation_counters();
    p::set(out.get(),"callbacks",p::integer(s.callbacks));p::set(out.get(),"slices",p::integer(s.slices));
    p::set(out.get(),"refreshes",p::integer(s.refreshes));p::set(out.get(),"actors",p::integer(s.actors));
    p::set(out.get(),"refresh_slices",p::integer(s.refresh_slices));p::set(out.get(),"reuse_slices",p::integer(s.reuse_slices));
    p::set(out.get(),"refresh_ns",p::integer(s.refresh_ns));p::set(out.get(),"reuse_ns",p::integer(s.reuse_ns));
    p::set(out.get(),"refresh_actors",p::integer(s.refresh_actors));p::set(out.get(),"reuse_actors",p::integer(s.reuse_actors));
    p::set(out.get(),"python_business_callbacks",p::integer(s.python_business_callbacks));p::set(out.get(),"stock_presentation_callbacks",p::integer(d.python_boundary_calls));
    p::set(out.get(),"cold_installs",p::integer(owner->cold_installs));p::set(out.get(),"ingress_ns",p::integer(owner->ingress_ns));
    p::set(out.get(),"step_ns",p::integer(owner->step_ns));p::set(out.get(),"publication_ns",p::integer(owner->publication_ns));
    p::set(out.get(),"snapshot_read_ns",p::integer(owner->snapshot_read_ns));p::set(out.get(),"snapshot_mirror_ns",p::integer(owner->snapshot_mirror_ns));
    p::set(out.get(),"snapshot_full_mirrors",p::integer(owner->snapshot_full_mirrors));p::set(out.get(),"snapshot_receipt_mirrors",p::integer(owner->snapshot_receipt_mirrors));
    p::set(out.get(),"stage_timing",p::boolean(s.stage_timing));p::set(out.get(),"detailed_slices",p::integer(s.detailed_slices));
    const char*stage_names[]={"prepare","combat","decision","weapon_prepare","motion_plan","motion","weapon_finish",
        "observation","suspension","contacts","support_recheck","slope","publication"};
    static_assert(sizeof(stage_names)/sizeof(*stage_names)==std::size_t(Stage::count),"Native stage name count differs");
    for(std::size_t index=0;index<std::size_t(Stage::count);++index)
        p::set(out.get(),(std::string("stage_")+stage_names[index]+"_ns").c_str(),p::integer(s.stage_ns[index]));
    p::set(out.get(),"rejected_player_contacts",p::integer(owner->contact_rejected));
    std::size_t ready_bots=0,live_bots=0;
    for(const auto&entry:owner->runtime->actors())if(entry.second.motion.alive){
        ++live_bots;const auto*vehicle=owner->vehicles.find(botKey(entry.first));
        if(vehicle&&vehicle->ready)++ready_bots;
    }
    p::set(out.get(),"live_bots",p::integer(live_bots));
    p::set(out.get(),"ready_bot_geometry",p::integer(ready_bots));
    p::Ref projectile(owner->projectiles->statistics());Py_ssize_t cursor=0;PyObject*key=nullptr,*value=nullptr;
    while(PyDict_Next(projectile.get(),&cursor,&key,&value))p::set(out.get(),("projectile_"+p::string(key)).c_str(),p::retain(value));
    return out.release();
}catch(const std::exception&e){return fail(e);}}
PyMethodDef methods[]={
    {"create",create,METH_VARARGS,"Install a persistent native Bot owner."},
    {"close",close,METH_VARARGS,"Retire the native Bot owner idempotently."},
    {"update",update,METH_VARARGS,"Advance accepted native simulation time."},
    {"frame",frame,METH_VARARGS,"Advance native resource bodies and foliage."},
    {"prewarm",prewarm,METH_VARARGS,"Warm native Bot spawn geometry fairly."},
    {"prewarm_players",prewarmPlayers,METH_VARARGS,"Warm authoritative player spawn geometry."},
    {"players",players,METH_VARARGS,"Read accepted authoritative human poses."},
    {"world_events",worldEvents,METH_VARARGS,"Pack ordered native world events."},
    {"ack_world",ackWorld,METH_VARARGS,"Acknowledge world transport admission."},
    {"world_ingress",worldIngress,METH_VARARGS,"Apply canonical world destruction."},
    {"player_contacts",playerContacts,METH_VARARGS,"Resolve player contact receipts."},
    {"ack_player_contact",ackPlayerContact,METH_VARARGS,"Acknowledge player contact transport admission."},
    {"player_environment",playerEnvironment,METH_VARARGS,"Sample player native hull and water state."},
    {"ack_player_environment",ackPlayerEnvironment,METH_VARARGS,"Acknowledge player environment transport admission."},
    {"human_ram",humanRam,METH_VARARGS,"Resolve exact human contact armor requests."},
    {"snapshot",snapshot,METH_VARARGS,"Accept server-owned combat events."},
    {"detached_queue",detachedQueue,METH_VARARGS,"Freeze a native detached turret launch."},
    {"detached_ingress",detachedIngress,METH_VARARGS,"Accept canonical detached turret rows."},
    {"detached_advance",detachedAdvance,METH_VARARGS,"Advance native detached turret state."},
    {"detached_rows",detachedRows,METH_VARARGS,"Pack sorted detached turret transport rows."},
    {"projectile_ingress",projectileIngress,METH_VARARGS,"Accept canonical projectile cursors."},
    {"projectile_observe",projectileObserve,METH_VARARGS,"Anchor the native projectile clock."},
    {"projectile_event",projectileEvent,METH_VARARGS,"Accept an ordered projectile event."},
    {"projectile_advance",projectileAdvance,METH_VARARGS,"Advance native projectile cursors."},
    {"projectile_ack",projectileAck,METH_VARARGS,"Acknowledge immutable projectile transport."},
    {"projectile_launches",projectileLaunches,METH_VARARGS,"Read the next frozen Bot launch dispatch."},
    {"projectile_launch_result",projectileLaunchResult,METH_VARARGS,"Record ordered Bot launch transport admission."},
    {"transport_error",transportError,METH_VARARGS,"Record one local projectile transport failure."},
    {"presentation",presentation,METH_VARARGS,"Copy current native presentation values."},
    {"apply_presentation",applyPresentation,METH_VARARGS,"Apply one native worker pose and aim batch."},
    {"ack_launch",ackLaunch,METH_VARARGS,"Acknowledge a frozen launch."},
    {"ack_ram",ackRam,METH_VARARGS,"Acknowledge a human ram receipt."},
    {"statistics",statistics,METH_VARARGS,"Read cumulative native boundary timings."},
    {"stage_timing",stageTiming,METH_VARARGS,"Enable exclusive native slice stage timing for a bounded diagnostic."},
    {nullptr,nullptr,0,nullptr}
};
class Module final:public BW::Script::InitTimeJob {
public:
    Module():BW::Script::InitTimeJob(1){}
    void init()override{if(!Py_InitModule("offline_bot_runtime_native",methods)){PyErr_Print();CRITICAL_MSG("Native Bot runtime module initialization failed\n");}}
};
Module module;
}
}
