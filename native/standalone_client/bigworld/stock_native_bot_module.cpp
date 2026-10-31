// The worker's native Bot simulation owner and the C++ entry points the native battle drives.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_bot_owner.hpp"
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_bots_needs.hpp"
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
#include <algorithm>
#include <chrono>
#include <memory>

namespace stock_native_bot {
namespace {
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
namespace world=stock_native_world;
namespace foliage=stock_native_bot_foliage;
namespace player_contacts=stock_native_player_contacts;
namespace player_environment=stock_native_player_environment;
using native_battle::Battle;
using VehicleRecord=native_battle::Actor;
// collision_flags.VEHICLE_SKIP_FLAGS: TRIANGLE_NOCOLLIDE | TRIANGLE_WATER.
constexpr std::uint16_t VEHICLE_SKIP_FLAGS=0x10|0x40;
void require(bool ok){if(!ok)throw p::Error("Native Bot installation failed");}
Key botKey(ID id){return {offline_simulation::ActorKind::Bot,id};}
Key humanKey(ID id){return {offline_simulation::ActorKind::Human,id};}
std::string recordName(Key key){return std::string(key.kind==offline_simulation::ActorKind::Human?"player:":"bot:")+std::to_string(key.id);}
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
}
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
    std::uint64_t record_serial=0,cold_installs=0,ingress_ns=0,step_ns=0,publication_ns=0,contact_rejected=0,ingress_errors=0;
    std::string last_ingress_error;
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
    // A malformed ingress or unavailable resource is local to that row: count it without failing the round.
    void local(const std::exception&error){
        std::string detail(error.what());
        if(PyErr_Occurred()){
            PyObject*type=nullptr,*value=nullptr,*traceback=nullptr;PyErr_Fetch(&type,&value,&traceback);
            if(value){PyObject*message=PyObject_Str(value);
                if(message){if(PyString_Check(message)){detail+="; ";detail.append(PyString_AS_STRING(message),PyString_GET_SIZE(message));}Py_DECREF(message);}}
            Py_XDECREF(type);Py_XDECREF(value);Py_XDECREF(traceback);
        }
        PyErr_Clear();++ingress_errors;last_ingress_error=std::move(detail);
    }
    void projectileDescriptor(Battle&battle,PyObject*descriptor,ServiceEntity&row,std::size_t mode){
        row.pitch_hull_aiming[mode]=p::truth(descriptor,"isPitchHullAimingAvailable");
        auto gun=p::value(descriptor,"gun"),shots=p::value(gun.get(),"shots");
        auto cache=p::value(battle.engine.vehicles.get(),"g_cache"),effects=p::value(cache.get(),"shotEffects");
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
    void installBot(PyObject*raw,Battle&battle,Key key,const VehicleRecord&record,PyObject*descriptor){
        const auto a=runtime->actors().find(key.id);if(a==runtime->actors().end())return;
        auto e=battle.entity(record.engine_id);if(e.get()==Py_None||!p::truth(e.get(),"isStarted"))return;
        auto pairs=p::value(raw,"_descriptor_pairs"),id=p::integer(key.id);
        auto*pair=PyDict_Check(pairs.get())?PyDict_GetItem(pairs.get(),id.get()):nullptr;
        auto d0=pair&&pair!=Py_None?p::item(pair,0):p::retain(descriptor);
        auto d1=pair&&pair!=Py_None?p::item(pair,1):p::retain(Py_None);
        ServiceEntity row;row.key=key;row.team=a->second.team;row.mode=a->second.mode;row.record_ready=record.ready;
        row.radius=std::hypot(a->second.descriptor.half_length,a->second.descriptor.half_width);row.alive=a->second.motion.alive;
        row.target.record_revision=++record_serial;row.target.descriptor_revision=a->second.weapon.descriptor_revision;
        require(geometry::readVehicle(d0.get(),e.get(),true,record_serial,row.geometry[0]));
        require(step::load_internal_layout(d0.get(),row.interior[0]));row.aim_samples[0]=a->second.modes[0].combat.target_samples;
        p::Ref critical(readNativeEntityField(nullptr,e.get(),"_offline_critical_descriptor"));
        p::Ref profile(cs::write(record.state.get("effective_params").get("critical")));
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
    void records(PyObject*raw,Battle&battle){
        auto descriptors=p::value(raw,"_descriptors"),pairs=p::value(raw,"_descriptor_pairs");
        if(!PyDict_Check(descriptors.get())||!PyDict_Check(pairs.get()))throw p::Error("Native Bot lifecycle descriptors invalid");
        std::set<Key> present;
        for(const auto&entry:battle.vehicles->actors){const auto&record=*entry.second;
            try{
                const auto&tag=record.kind;if(tag!="bot"&&tag!="player")continue;
                const ID id=record.id;if(id<=0)continue;const Key key=tag=="bot"?botKey(id):humanKey(id);present.insert(key);
                auto index=p::integer(id);PyObject*descriptor=tag=="bot"?PyDict_GetItem(descriptors.get(),index.get()):
                    (human_descriptors.count(id)?human_descriptors.at(id)->get():nullptr);
                RecordVersion version;version.engine=record.engine_id;version.ready=record.ready;
                version.tombstone=record.tombstone;version.descriptor=descriptor;
                // Accepted crew/equipment profiles are immutable within this round.
                // Network dictionary identity is not a resource lifecycle change.
                std::vector<PyObject*> modes{descriptor};
                auto*pair=key.kind==offline_simulation::ActorKind::Bot?PyDict_GetItem(pairs.get(),index.get()):nullptr;
                if(pair&&pair!=Py_None&&(PyTuple_Check(pair)||PyList_Check(pair))){modes.clear();for(Py_ssize_t i=0;i<PySequence_Fast_GET_SIZE(pair);++i)modes.push_back(PySequence_Fast_GET_ITEM(pair,i));}
                auto e=version.ready&&!version.tombstone?battle.entity(record.engine_id):p::retain(Py_None);
                require(readResourceStamp(e.get(),modes,version.resources,nullptr));
                const auto old=installed.find(key);if(old!=installed.end()&&old->second==version)continue;
                installed[key]=version;services.remove(key);services.recordReady(key,version.ready&&!version.tombstone);
                if(key.kind==offline_simulation::ActorKind::Human){
                    if(humans.count(id)&&descriptor)installHumanGeometry(raw,battle,id,descriptor,&record);
                    continue;
                }
                if(version.ready&&!version.tombstone&&descriptor)installBot(raw,battle,key,record,descriptor);
            }catch(const std::exception&e){local(e);}
        }
        for(auto i=installed.begin();i!=installed.end();){if(!present.count(i->first)){services.remove(i->first);if(i->first.kind==offline_simulation::ActorKind::Human){humans.erase(i->first.id);human_descriptors.erase(i->first.id);}i=installed.erase(i);}else ++i;}
    }
    void installHuman(PyObject*raw,Battle&battle,PyObject*input,ID id){
        p::Ref args(PyTuple_Pack(1,input));auto descriptor=p::call(raw,"player_descriptor_resolver",args.get());
        if(descriptor.get()==Py_None)throw p::Error("Native human descriptor is unavailable");
        HumanDescriptor frozen;require(readHumanDescriptor(raw,input,descriptor.get(),frozen));
        frozen.spawn_anchor=stock_native_bot_descriptor::spawnAnchor(
            battle.engine.shell.get(),int(p::integral(input,"team",1)),int(p::integral(input,"slot")));
        const Key key=humanKey(id);
        const auto*record=battle.vehicle(recordName(key));
        human_descriptors[id].reset(new p::Ref(p::retain(descriptor.get())));humans[id]=std::move(frozen);
        installHumanGeometry(raw,battle,id,descriptor.get(),record);++cold_installs;
    }
    void installHumanGeometry(PyObject*,Battle&battle,ID id,PyObject*descriptor,const VehicleRecord*record){
        auto&frozen=humans.at(id);ServiceEntity row;row.key=humanKey(id);row.team=frozen.template_actor.config.team;
        row.target.record_revision=++record_serial;row.target.descriptor_revision=record_serial;
        if(record&&record->ready&&!record->tombstone){
            row.record_ready=true;
            try{auto e=battle.entity(record->engine_id);if(e.get()!=Py_None&&p::truth(e.get(),"isStarted")){
                    require(geometry::readVehicle(descriptor,e.get(),record->native_remote,record_serial,row.geometry[0]));
                    require(step::load_internal_layout(descriptor,row.interior[0]));
                    p::Ref critical(readNativeEntityField(nullptr,e.get(),"_offline_critical_descriptor"));
                    p::Ref profile(cs::write(record->state.get("effective_params").get("critical")));
                    stock_native_projectile_damage::TargetDescriptor target_descriptor;
                    require(stock_native_projectile_damage::readTargetDescriptor(descriptor,critical.get(),target_descriptor,profile.get()));
                    row.projectile[0]=std::move(target_descriptor);row.projectile[1]=row.projectile[0];
                    projectileDescriptor(battle,descriptor,row,0);row.pitch_hull_aiming[1]=row.pitch_hull_aiming[0];row.projectile_stickers[1]=row.projectile_stickers[0];row.launch_shots[1]=row.launch_shots[0];
                    row.geometry[1]=row.geometry[0];row.interior[1]=row.interior[0];row.available=row.geometry[0].ready;
                }
            }catch(const std::exception&e){local(e);row.available=false;}
        }
        row.aim_samples[0]=frozen.aim_samples;row.aim_samples[1]=frozen.aim_samples;
        frozen.target_available=row.available;frozen.record_revision=row.target.record_revision;frozen.descriptor_revision=row.target.descriptor_revision;
        services.install(std::move(row));
    }
    void humanInput(PyObject*raw,Battle&battle,PyObject*players,bool is_prepared=false){
        std::vector<Human> accepted;std::set<ID> present;
        p::Ref prepared=is_prepared?(players&&players!=Py_None?p::retain(players):p::list()):p::Ref(prepareHumanInput(players,battle.engine.shell.get(),raw));
        for(Py_ssize_t i=0;i<p::size(prepared.get());++i){auto row=p::item(prepared.get(),i);if(!PyDict_Check(row.get()))continue;
            try{
                const ID id=p::integral(row.get(),"id");if(id<=0||!p::truth(row.get(),"world_pose"))continue;
                if(!present.insert(id).second)continue;
                if(!humans.count(id))installHuman(raw,battle,row.get(),id);
                HumanIngress input;require(readHumanInput(row.get(),humans.at(id),input));
                services.updateHuman(humanKey(id),input.target,input.human.perception.config.team,input.target.alive,input.target.available,input.radius);
                accepted.push_back(std::move(input.human));
            }catch(const std::exception&e){local(e);}
        }
        for(auto i=humans.begin();i!=humans.end();){if(!present.count(i->first)){services.remove(humanKey(i->first));human_descriptors.erase(i->first);i=humans.erase(i);}else ++i;}
        runtime->humans(std::move(accepted));
    }
    void detached(Battle&battle){
        auto rows=native_battle::world::detachedTurretRows(battle);if(rows.get()==Py_None)return;
        if(!PyDict_Check(rows.get()))throw p::Error("Native detached turret mapping invalid");
        Py_ssize_t cursor=0;PyObject*key=nullptr,*value=nullptr;
        while(PyDict_Next(rows.get(),&cursor,&key,&value))try{
            stock_native_bot_turret::Record record;require(physics::readDetachedTurretRow(value,record));
            // Visual-only debris never enters collision or navigation in the
            // pinned runtime, including after the visual arc has landed.
            if(!record.has_body){services.turrets().remove(record.owner);continue;}
            const auto*vehicle=vehicles.find(record.owner.human?humanKey(record.owner.id):botKey(record.owner.id));
            if(vehicle)services.turrets().put(record,physics::detachedDescriptor(*vehicle));
        }catch(const std::exception&e){local(e);}
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
};
namespace {
using Time=std::chrono::steady_clock;
std::uint64_t nanos(Time::time_point start){return std::chrono::duration_cast<std::chrono::nanoseconds>(Time::now()-start).count();}
bool live(const Owner&o,PyObject*runtime){return owner::current(o,runtime);}
PyObject* lastSnapshot(const Battle&b){return b.bots&&b.bots->last_snapshot?b.bots->last_snapshot.get():Py_None;}
p::Ref snapshotPlayers(const Battle&b){return p::value(lastSnapshot(b),"players");}
}

namespace owner {
std::shared_ptr<Owner> create(Battle&b,PyObject*raw){
    const auto id=p::integral(b.engine.avatar.get(),"spaceID");
    if(id<=0||id>2147483647)throw p::Error("Native Bot space identity invalid");
    auto space=BW::SpaceManager::instance().space(BW::SpaceID(id));
    if(!space||space->cleared())throw p::Error("Native Bot space is unavailable");
    PyObject*shell=b.engine.shell.get();
    auto handle=std::make_shared<Owner>(space);auto&o=*handle;
    require(o.events.install(shell));
    o.environment=player_environment::Owner(b.bots->player_environment_check,b.bots->player_environment_seq);
    Snapshot snapshot;require(readNativeSnapshot(raw,shell,snapshot));
    require(foliage::readFoliageSnapshot(shell,snapshot.foliage));
    foliage::TreeState trees;require(foliage::readFoliageTreeState(shell,trees));o.trees.reset(new foliage::Trees(std::move(trees)));
    auto dto=native_battle::world::destructibleSnapshot(b);
    require(o.backend.installPresenter());require(world::installWorldSnapshot(dto.get(),o.registry,o.scene,o.backend));
    o.runtime.reset(new Runtime(std::move(snapshot),o.registry,o.scene,o.services));
    o.services.bind(*o.runtime);o.server_tick=p::integral(raw,"_server_tick",-1);
    o.records(raw,b);
    o.detached_runtime.reset(new stock_native_detached_turret::PythonOwner(*o.runtime,o.registry,
        o.scene,o.vehicles,o.services,VEHICLE_SKIP_FLAGS));
    require(o.detached_runtime->install(shell));
    o.projectiles.reset(new stock_native_projectile::NativeProjectiles(o.registry,o.scene,
        o.vehicles,o.events,*o.runtime,o.services));
    require(o.projectiles->install(shell));
    return handle;
}
void close(Owner&o){o.retire();}
bool current(const Owner&o,PyObject*raw){return o.current(raw);}
const Clock& clock(const Owner&o){return o.runtime->clock();}

p::Ref players(Battle&b,PyObject*raw){auto rows=snapshotPlayers(b);return p::Ref(prepareHumanInput(rows.get(),b.engine.shell.get(),raw));}

p::Ref update(Owner&o,Battle&b,PyObject*raw,double elapsed,double now,PyObject*players){
    if(!live(o,raw))return p::list();
    auto start=Time::now();o.records(raw,b);o.humanInput(raw,b,players,true);o.detached(b);
    auto camera=p::value(raw,"_camera_position");o.runtime->camera(camera.get()==Py_None?std::optional<Point>{}:p::numbers<3>(camera.get()));
    o.server_time_ms=p::numeric(lastSnapshot(b),"server_time_ms");
    o.ingress_ns+=nanos(start);start=Time::now();
    // Original artillery jobs advance once per frame, before the Bot slices.
    o.services.beginCallback(now,o.server_time_ms);
    o.runtime->weaponRuntime().advanceArtillery(now,4,o.services.weaponWorld());
    auto result=o.runtime->update(elapsed,now,o.server_time_ms);
    o.step_ns+=nanos(start);if(!live(o,raw))return p::list();start=Time::now();
    p::Ref packed(packNativeOutputs(result,o.runtime->clock().sample_us));
    o.publication_ns+=nanos(start);return packed;
}
bool frame(Owner&o,PyObject*raw,double now,bool force){if(!live(o,raw))return false;o.frame(now,force);return true;}
std::size_t prewarm(Owner&o,Battle&b,PyObject*raw,double now){
    if(!live(o,raw))return 0;o.records(raw,b);return o.runtime->prewarm(now);
}
std::size_t prewarmPlayers(Owner&o,Battle&b,PyObject*raw,double now){
    if(!live(o,raw))return 0;o.records(raw,b);auto rows=snapshotPlayers(b);
    o.humanInput(raw,b,rows.get());return o.runtime->prewarmPlayers(now,b.live);
}
std::size_t presentPoses(Owner&o,Battle&b,PyObject*raw,double now){
    if(!live(o,raw))return 0;std::vector<std::string> errors;
    const auto count=presentNativeBotPoses(b,*o.runtime,now,o.services,errors);
    for(const auto&error:errors)o.local(p::Error(error));
    return count;
}
void snapshot(Owner&o,PyObject*raw,PyObject*message){
    if(!live(o,raw)||!PyDict_Check(message))return;
    auto started=Time::now();
    auto orders=p::value(message,"bot_orders"),revision=p::value(message,"bot_order_revision");
    if(orders.get()!=Py_None&&revision.get()!=Py_None)try{
        auto mapping=p::dict();const auto count=std::min<Py_ssize_t>(30,p::size(orders.get()));
        for(Py_ssize_t i=0;i<count;++i){auto row=p::item(orders.get(),i),id=p::value(row.get(),"id");auto key=p::integer(p::wide(id.get()));
            if(PyDict_GetItem(mapping.get(),key.get()))throw p::Error("Native Bot order has a duplicate actor");
            if(PyDict_SetItem(mapping.get(),key.get(),row.get())<0)throw p::Error("Native Bot order allocation failed");}
        std::map<ID,planning::StrategicOrder> values;require(planning::readPlanningOrders(mapping.get(),values));
        o.runtime->acceptOrders(std::move(values),p::wide(revision.get()));
    }catch(const std::exception&e){o.local(e);}
    auto tick=p::value(message,"server_tick"),time=p::value(message,"server_time_ms");
    if(tick.get()!=Py_None){const auto incoming=p::wide(tick.get());if(incoming<o.server_tick)return;o.server_tick=incoming;}
    auto rows=p::value(message,"bots");if(rows.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(rows.get());++i){auto row=p::item(rows.get(),i);if(!PyDict_Check(row.get()))continue;
        try{
            const ID id=p::integral(row.get(),"id");const auto actor=o.runtime->actors().find(id);if(actor==o.runtime->actors().end())continue;
            ServerSnapshot value;require(readNativeServerSnapshot(row.get(),tick.get(),time.get(),actor->second,o.runtime->weapons().at(botKey(id)),value));
            if(o.runtime->serverSnapshot(id,value))++o.snapshot_full_mirrors;else ++o.snapshot_receipt_mirrors;
        }catch(const std::exception&e){o.local(e);}
    }
    o.snapshot_read_ns+=nanos(started);
}

p::Ref worldEvents(Owner&o,Battle&b,PyObject*raw){if(!live(o,raw))return p::list();return p::Ref(o.events.pack(b.engine.shell.get()));}
bool acknowledgeWorld(Owner&o,std::uint64_t sequence){const bool acknowledged=o.events.ack(sequence);if(PyErr_Occurred())throw p::Error("World acknowledgement failed");return acknowledged;}
bool worldIngress(Owner&o,PyObject*raw,PyObject*event,double now){
    if(!live(o,raw))return false;const bool applied=o.events.ingress(event,now);
    if(PyErr_Occurred())o.local(p::Error("Canonical native world event was rejected"));return applied;
}
p::Ref playerContacts(Owner&o,Battle&b,PyObject*raw,PyObject*players,double now){
    if(!live(o,raw))return p::list();
    o.records(raw,b);o.humanInput(raw,b,players,true);
    std::vector<player_contacts::Request> requests;
    require(player_contacts::readPlayerContacts(players,o.humans,requests,o.contact_rejected));
    return p::Ref(player_contacts::packPlayerVerdicts(o.player_contacts.update(requests,now,o.player_world)));
}
bool acknowledgePlayerContact(Owner&o,std::uint64_t serial,double now){return o.player_contacts.acknowledge(serial,now);}
std::optional<std::pair<std::int64_t,p::Ref>> playerEnvironment(Owner&o,Battle&b,PyObject*raw,double elapsed,bool authority){
    if(!live(o,raw))return {};
    if(o.environment.willSample(elapsed,authority)){auto rows=snapshotPlayers(b);o.humanInput(raw,b,rows.get());o.records(raw,b);}
    std::optional<player_environment::Batch> batch;
    require(player_environment::readAndAdvance(b,elapsed,authority,o.environment,o.environment_world,o.environment_metadata,batch));
    b.bots->player_environment_check=o.environment.elapsed();
    if(!batch)return {};
    return std::make_pair(std::int64_t(batch->sequence),p::Ref(player_environment::observationsValue(*batch)));
}
void acknowledgePlayerEnvironment(Owner&o,Battle&b,std::int64_t sequence,bool sent){
    o.environment.acknowledge(sequence,sent);b.bots->player_environment_seq=o.environment.sequence();
}
p::Ref humanRam(Owner&o,Battle&b,PyObject*raw){
    if(!live(o,raw))return p::list();o.records(raw,b);
    stock_native_human_ram::NativeWorld<NativeServices,geometry::Vehicles> world(o.services,o.vehicles);
    std::vector<std::string> errors;p::Ref result(stock_native_human_ram::results(b,world,&errors));
    for(const auto&error:errors)o.local(p::Error(error));return result;
}

namespace {
void detachedErrors(Owner&o){
    p::Ref errors(o.detached_runtime->takeErrors());
    for(Py_ssize_t i=0;i<p::size(errors.get());++i){auto message=p::item(errors.get(),i);o.local(p::Error(p::string(message.get())));}
}
}
bool detachedQueue(Owner&o,PyObject*raw,const native_battle::Actor&vehicle,PyObject*entity,PyObject*state,double now){
    if(!live(o,raw))return false;
    // The detached owner keys its queue by the vehicle identity: kind, engine id and network id.
    auto record=p::dict();
    p::set(record.get(),"kind",p::text(vehicle.kind));
    p::set(record.get(),"engine_id",p::integer(vehicle.engine_id));
    p::set(record.get(),"network_id",p::integer(vehicle.id));
    const bool queued=o.detached_runtime->queue(record.get(),entity,state?state:Py_None,now);
    if(PyErr_Occurred())o.local(p::Error("Native detached turret launch failed"));
    detachedErrors(o);return queued;
}
bool detachedIngress(Owner&o,PyObject*raw,PyObject*message,double now){
    if(!live(o,raw))return false;const bool changed=o.detached_runtime->ingress(message,now);
    if(PyErr_Occurred())o.local(p::Error("Native detached turret snapshot failed"));
    detachedErrors(o);return changed;
}
p::Ref detachedAdvance(Owner&o,Battle&b,PyObject*raw,double now){
    if(!live(o,raw))return p::retain(Py_None);
    PyObject*result=o.detached_runtime->advance(b.engine.shell.get(),now);
    if(!result){o.local(p::Error("Native detached turret advance failed"));return p::retain(Py_None);}
    p::Ref tail(result);detachedErrors(o);o.detached(b);return tail;
}
p::Ref detachedRows(Owner&o){return p::Ref(o.detached_runtime->rows());}

bool projectileIngress(Owner&o,PyObject*raw,PyObject*message,double now){
    if(!live(o,raw))return false;const bool changed=o.projectiles->ingress(message,now);
    if(PyErr_Occurred())o.local(p::Error("Native projectile snapshot failed"));return changed;
}
bool projectileObserve(Owner&o,PyObject*raw,PyObject*message,double now){
    if(!live(o,raw))return false;const bool changed=o.projectiles->observe(message,now);
    if(PyErr_Occurred())o.local(p::Error("Native projectile clock message failed"));return changed;
}
bool projectileEvent(Owner&o,PyObject*raw,PyObject*event,double now){
    if(!live(o,raw))return false;const bool changed=o.projectiles->event(event,now);
    if(PyErr_Occurred())o.local(p::Error("Native projectile event failed"));return changed;
}
p::Ref projectileAdvance(Owner&o,Battle&b,PyObject*raw,double now,bool force){
    if(!live(o,raw))return p::list();
    p::Ref result(o.projectiles->advance(b.engine.shell.get(),now,force));
    return live(o,raw)?std::move(result):p::list();
}
bool projectileAcknowledge(Owner&o,std::uint64_t serial,bool sent){return o.projectiles->acknowledge(serial,sent);}
p::Ref projectileLaunches(Owner&o,PyObject*raw,PyObject*message){
    if(!live(o,raw))return p::list();return p::Ref(o.projectiles->launches(message));
}
void projectileLaunchResult(Owner&o,std::int64_t bot,std::int64_t sequence,bool accepted){o.projectiles->launchResult(bot,sequence,accepted);}
void transportError(Owner&o,const std::string&message){o.local(p::Error("Native projectile transport failed: "+message));}

bool acknowledgeLaunch(Owner&o,std::int64_t bot,std::int64_t sequence){return o.runtime->acknowledgeLaunch(bot,sequence);}
void acknowledgeRam(Owner&o,std::int64_t player,std::int64_t sequence){o.runtime->acknowledgeRam(player,sequence);}
void stageTiming(Owner&o,bool enabled){o.runtime->stageTiming(enabled);}
p::Ref statistics(Owner&o){
    auto out=p::dict();
    const auto&s=o.runtime->statistics();const auto&d=o.backend.presentation_counters();
    p::set(out.get(),"callbacks",p::integer(s.callbacks));p::set(out.get(),"slices",p::integer(s.slices));
    p::set(out.get(),"refreshes",p::integer(s.refreshes));p::set(out.get(),"actors",p::integer(s.actors));
    p::set(out.get(),"refresh_slices",p::integer(s.refresh_slices));p::set(out.get(),"reuse_slices",p::integer(s.reuse_slices));
    p::set(out.get(),"refresh_ns",p::integer(s.refresh_ns));p::set(out.get(),"reuse_ns",p::integer(s.reuse_ns));
    p::set(out.get(),"refresh_actors",p::integer(s.refresh_actors));p::set(out.get(),"reuse_actors",p::integer(s.reuse_actors));
    p::set(out.get(),"python_business_callbacks",p::integer(s.python_business_callbacks));p::set(out.get(),"stock_presentation_callbacks",p::integer(d.python_boundary_calls));
    p::set(out.get(),"cold_installs",p::integer(o.cold_installs));p::set(out.get(),"ingress_ns",p::integer(o.ingress_ns));
    p::set(out.get(),"step_ns",p::integer(o.step_ns));p::set(out.get(),"publication_ns",p::integer(o.publication_ns));
    p::set(out.get(),"snapshot_read_ns",p::integer(o.snapshot_read_ns));
    p::set(out.get(),"snapshot_full_mirrors",p::integer(o.snapshot_full_mirrors));p::set(out.get(),"snapshot_receipt_mirrors",p::integer(o.snapshot_receipt_mirrors));
    p::set(out.get(),"ingress_errors",p::integer(o.ingress_errors));p::set(out.get(),"last_ingress_error",p::text(o.last_ingress_error));
    p::set(out.get(),"stage_timing",p::boolean(s.stage_timing));p::set(out.get(),"detailed_slices",p::integer(s.detailed_slices));
    const char*stage_names[]={"prepare","combat","decision","weapon_prepare","motion_plan","motion","weapon_finish",
        "observation","suspension","contacts","support_recheck","slope","publication"};
    static_assert(sizeof(stage_names)/sizeof(*stage_names)==std::size_t(Stage::count),"Native stage name count differs");
    for(std::size_t index=0;index<std::size_t(Stage::count);++index)
        p::set(out.get(),(std::string("stage_")+stage_names[index]+"_ns").c_str(),p::integer(s.stage_ns[index]));
    p::set(out.get(),"rejected_player_contacts",p::integer(o.contact_rejected));
    std::size_t ready_bots=0,live_bots=0;
    for(const auto&entry:o.runtime->actors())if(entry.second.motion.alive){
        ++live_bots;const auto*vehicle=o.vehicles.find(botKey(entry.first));
        if(vehicle&&vehicle->ready)++ready_bots;
    }
    p::set(out.get(),"live_bots",p::integer(live_bots));
    p::set(out.get(),"ready_bot_geometry",p::integer(ready_bots));
    p::Ref projectile(o.projectiles->statistics());Py_ssize_t cursor=0;PyObject*key=nullptr,*value=nullptr;
    while(PyDict_Next(projectile.get(),&cursor,&key,&value))p::set(out.get(),("projectile_"+p::string(key)).c_str(),p::retain(value));
    return out;
}
}
}
