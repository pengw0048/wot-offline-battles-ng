// The worker's native Bot simulation owner and the C++ entry points the native battle drives.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_bot_owner.hpp"
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_bots_needs.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_snapshot.hpp"
#include "stock_native_battle_startup.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_services.hpp"
#include "stock_native_bot_human_snapshot.hpp"
#include "stock_native_bot_human_history.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_bot_foliage_snapshot.hpp"
#include "stock_native_bot_publication_python.hpp"
#include "stock_native_bot_presentation.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_destructible_backend.hpp"
#include "stock_native_battle_world.hpp"
#include "stock_native_detached_turret_runtime.hpp"
#include "stock_native_world_events.hpp"
#include "stock_native_player_contacts_python.hpp"
#include "stock_native_player_environment_python.hpp"
#include "stock_native_human_ram.hpp"
#include "stock_native_detached_turret_python.hpp"
#include "stock_native_projectile_python.hpp"
#include "stock_native_bot_value.hpp"
#include <algorithm>
#include <chrono>
#include <memory>

namespace stock_native_bot {
namespace {
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
namespace v=stock_native_bot_value;
using native_battle::Value;
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
    // The battle space's shared native world (world::sceneAccess).
    stock_native_client_drive::SceneAccess world_access;
    world::Registry& registry;
    stock_native_destructible::NativeBackend& backend;
    world::NativeScene& scene;
    world::WorldEvents& events;
    PlayerTrees player_trees;
    player_contacts::PipelineWorld player_world;
    player_contacts::Owner player_contacts;
    geometry::Vehicles vehicles;
    PlayerEnvironmentWorld environment_world;
    player_environment::Owner environment;
    player_environment::MetadataCache environment_metadata;
    NativeServices services;
    std::unique_ptr<Runtime> runtime;
    std::unique_ptr<stock_native_detached_turret::NativeOwner> detached_runtime;
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
    bool retired=false;
    // The shared scene/backend belong to the world space; this owner only stops using them.
    void retire(){retired=true;}
    explicit Owner(stock_native_client_drive::SceneAccess access):world_access(std::move(access)),registry(*world_access.registry),
        backend(*world_access.backend),scene(*world_access.scene),events(*world_access.events),player_trees(events),
        player_world(registry,scene,player_trees),environment_world(scene,vehicles),services(registry,scene,vehicles){}
    // The detached-turret veto the world backend applies to native destruction sweeps.
    bool turretSweepAllowed(const world::TurretSweep&q){
        if(retired||!runtime)return true;
        const auto a=runtime->actors().find(ID(q.actor));if(a==runtime->actors().end())return true;
        return !services.turrets().blocks(q,a->second.modes[a->second.mode].physics.geometry.turret,server_time_ms);
    }
    // The native round start's manifest seed (descriptor pairs) and each Bot's static ram profile.
    std::shared_ptr<const RoundSeed> seed;
    std::map<ID,Value> profiles;
    const BotSeed* bot(ID id)const{const auto found=seed->bots.find(id);return found==seed->bots.end()?nullptr:&found->second;}
    // The Bot authority closes the owner when the round or native generation changes.
    bool current()const{return !retired&&runtime&&scene.current();}
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
    void installBot(Battle&battle,Key key,const VehicleRecord&record,const BotSeed&seed_row){
        const auto a=runtime->actors().find(key.id);if(a==runtime->actors().end())return;
        auto e=p::Ref(readNativeEntity(nullptr,record.engine_id));if(e.get()==Py_None||!p::truth(e.get(),"isStarted"))return;
        auto d0=p::retain(seed_row.travel.get());
        auto d1=p::retain(seed_row.siege?seed_row.siege.get():Py_None);
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
    void records(Battle&battle){
        std::set<Key> present;
        for(const auto&entry:battle.vehicles->actors){const auto&record=*entry.second;
            try{
                const auto&tag=record.kind;if(tag!="bot"&&tag!="player")continue;
                const ID id=record.id;if(id<=0)continue;const Key key=tag=="bot"?botKey(id):humanKey(id);present.insert(key);
                const BotSeed*seed_row=tag=="bot"?bot(id):nullptr;PyObject*descriptor=tag=="bot"?(seed_row?activeDescriptor(*seed_row):nullptr):
                    (human_descriptors.count(id)?human_descriptors.at(id)->get():nullptr);
                RecordVersion version;version.engine=record.engine_id;version.ready=record.ready;
                version.tombstone=record.tombstone;version.descriptor=descriptor;
                // Accepted crew/equipment profiles are immutable within this round.
                // Network dictionary identity is not a resource lifecycle change.
                std::vector<PyObject*> modes{descriptor};
                if(seed_row){modes.clear();modes.push_back(seed_row->travel.get());modes.push_back(seed_row->siege?seed_row->siege.get():Py_None);}
                auto e=version.ready&&!version.tombstone?p::Ref(readNativeEntity(nullptr,record.engine_id)):p::retain(Py_None);
                require(readResourceStamp(e.get(),modes,version.resources,nullptr));
                const auto old=installed.find(key);if(old!=installed.end()&&old->second==version)continue;
                installed[key]=version;services.remove(key);services.recordReady(key,version.ready&&!version.tombstone);
                if(key.kind==offline_simulation::ActorKind::Human){
                    if(humans.count(id)&&descriptor)installHumanGeometry(battle,id,descriptor,&record);
                    continue;
                }
                if(version.ready&&!version.tombstone&&descriptor)installBot(battle,key,record,*seed_row);
            }catch(const std::exception&e){local(e);}
        }
        for(auto i=installed.begin();i!=installed.end();){if(!present.count(i->first)){services.remove(i->first);if(i->first.kind==offline_simulation::ActorKind::Human){humans.erase(i->first.id);human_descriptors.erase(i->first.id);}i=installed.erase(i);}else ++i;}
    }
    void installHuman(Battle&battle,const Value&input,ID id){
        auto descriptor=native_battle::startup::playerDescriptor(battle,input);
        if(descriptor.get()==Py_None)throw p::Error("Native human descriptor is unavailable");
        HumanDescriptor frozen;require(readHumanDescriptor(input,descriptor.get(),frozen));
        // The formation spawn of this (team, slot) (the pinned BattleRuntime._formation_pose and its cache).
        const auto spawn=native_battle::startup::formationPose(battle,v::integral(input,"team",1),v::integral(input,"slot"));
        frozen.spawn_anchor=Point{spawn.first[0],spawn.first[1],spawn.first[2]};
        const Key key=humanKey(id);
        const auto*record=battle.vehicle(recordName(key));
        human_descriptors[id].reset(new p::Ref(p::retain(descriptor.get())));humans[id]=std::move(frozen);
        installHumanGeometry(battle,id,descriptor.get(),record);++cold_installs;
    }
    void installHumanGeometry(Battle&battle,ID id,PyObject*descriptor,const VehicleRecord*record){
        auto&frozen=humans.at(id);ServiceEntity row;row.key=humanKey(id);row.team=frozen.template_actor.config.team;
        row.target.record_revision=++record_serial;row.target.descriptor_revision=record_serial;
        if(record&&record->ready&&!record->tombstone){
            row.record_ready=true;
            try{auto e=p::Ref(readNativeEntity(nullptr,record->engine_id));if(e.get()!=Py_None&&p::truth(e.get(),"isStarted")){
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
    // `players` is either the snapshot's player rows or rows already prepared by prepareHumanInput.
    void humanInput(Battle&battle,const Value&players,bool is_prepared=false){
        std::vector<Human> accepted;std::set<ID> present;
        const Value prepared_rows=is_prepared?Value():prepareHumanInput(players,battle,profiles);
        const auto&prepared=is_prepared?players.resolved():prepared_rows;
        static const std::vector<Value> none;
        for(const auto&entry:prepared.kind==Value::Kind::Null?none:v::rows(prepared)){const auto&row=entry.resolved();if(row.kind!=Value::Kind::Object)continue;
            try{
                const ID id=v::integral(row,"id");if(id<=0||!v::truth(row,"world_pose"))continue;
                if(!present.insert(id).second)continue;
                if(!humans.count(id))installHuman(battle,row,id);
                HumanIngress input;require(readHumanInput(row,humans.at(id),input));
                services.updateHuman(humanKey(id),input.target,input.human.perception.config.team,input.target.alive,input.target.available,input.radius);
                accepted.push_back(std::move(input.human));
            }catch(const std::exception&e){local(e);}
        }
        for(auto i=humans.begin();i!=humans.end();){if(!present.count(i->first)){services.remove(humanKey(i->first));human_descriptors.erase(i->first);i=humans.erase(i);}else ++i;}
        runtime->humans(std::move(accepted));
    }
    void detached(Battle&battle){
        if(!battle.world)return;
        for(const auto&row:battle.world->turrets.rows)try{
            const auto record=stock_native_detached_turret::obstacleRecord(row.second);
            // Visual-only debris never enters collision or navigation in the
            // pinned runtime, including after the visual arc has landed.
            if(!record.has_body){services.turrets().remove(record.owner);continue;}
            const auto*vehicle=vehicles.find(record.owner.human?humanKey(record.owner.id):botKey(record.owner.id));
            if(vehicle)services.turrets().put(record,physics::detachedDescriptor(*vehicle));
        }catch(const std::exception&e){local(e);}
    }
    void frame(double now,bool force=false){
        // Stream remapping and body updates run once per frame in world::foliage (spaceFrame).
        if(!std::isfinite(now))throw p::Error("Native world frame time invalid");
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
bool live(const Owner&o){return o.current();}
const Value& lastSnapshot(const Battle&b){static const Value none;return b.bots?b.bots->last_snapshot.resolved():none;}
const Value& snapshotPlayers(const Battle&b){return v::value(lastSnapshot(b),"players");}
}

namespace owner {
std::shared_ptr<Owner> create(Battle&b){
    if(!b.bots||!b.bots->seed)throw p::Error("Native Bot round seed is unavailable");
    auto access=native_battle::world::sceneAccess(b);
    if(!access.registry)throw p::Error("Native Bot space is unavailable");
    auto handle=std::make_shared<Owner>(std::move(access));auto&o=*handle;
    std::weak_ptr<Owner> weak=handle;
    native_battle::world::setTurretSweep(b,[weak](const world::TurretSweep&q){auto owner=weak.lock();return !owner||owner->turretSweepAllowed(q);});
    o.environment=player_environment::Owner(b.bots->player_environment_check,b.bots->player_environment_seq);
    o.seed=b.bots->seed;
    Snapshot snapshot;require(readNativeSnapshot(*o.seed,b.bots->orders,b.bots->refresh_bank,snapshot));
    // The world space owns this map's foliage; the worker Bot world refreshes its own copy (world::refreshFallenFoliage is visible-only).
    // No map foliage or a disabled "foliage camouflage" feature leaves both copies disabled.
    const auto*trees=native_battle::world::trees(b);
    foliage::TreeState tree_state=trees?*trees:foliage::TreeState{};
    if(!trees)tree_state.foliage.enabled=false;
    snapshot.foliage=tree_state.foliage;
    o.trees.reset(new foliage::Trees(std::move(tree_state)));
    o.runtime.reset(new Runtime(std::move(snapshot),o.registry,o.scene,o.services));
    // The round start has accepted no server snapshot tick yet.
    o.services.bind(*o.runtime);o.server_tick=-1;
    // A player's ram receipt pins the Bot's static collision profile.
    o.profiles=b.bots->profiles;
    o.records(b);
    o.detached_runtime.reset(new stock_native_detached_turret::NativeOwner(*o.runtime,o.registry,
        o.scene,o.vehicles,VEHICLE_SKIP_FLAGS));
    o.detached_runtime->install(b);
    o.projectiles.reset(new stock_native_projectile::NativeProjectiles(o.registry,o.scene,
        o.vehicles,o.events,*o.runtime,o.services));
    require(o.projectiles->install(b));
    return handle;
}
void close(Owner&o){o.retire();}
bool current(const Owner&o){return o.current();}
const Clock& clock(const Owner&o){return o.runtime->clock();}

Value players(Owner&o,Battle&b){return prepareHumanInput(snapshotPlayers(b),b,o.profiles);}

Value update(Owner&o,Battle&b,double elapsed,double now,const Value&players){
    if(!live(o))return v::array();
    auto start=Time::now();o.records(b);o.humanInput(b,players,true);o.detached(b);
    // The hidden worker has no viewpoint: every Bot keeps full presentation detail.
    o.runtime->camera(std::nullopt);
    o.server_time_ms=v::numeric(lastSnapshot(b),"server_time_ms");
    o.ingress_ns+=nanos(start);start=Time::now();
    // Original artillery jobs advance once per frame, before the Bot slices.
    o.services.beginCallback(now,o.server_time_ms);
    o.runtime->weaponRuntime().advanceArtillery(now,4,o.services.weaponWorld());
    auto result=o.runtime->update(elapsed,now,o.server_time_ms);
    o.step_ns+=nanos(start);if(!live(o))return v::array();start=Time::now();
    auto packed=packNativeOutputs(result,o.runtime->clock().sample_us);
    o.publication_ns+=nanos(start);return packed;
}
bool frame(Owner&o,double now,bool force){if(!live(o))return false;o.frame(now,force);return true;}
std::size_t prewarm(Owner&o,Battle&b,double now){
    if(!live(o))return 0;o.records(b);return o.runtime->prewarm(now);
}
std::size_t prewarmPlayers(Owner&o,Battle&b,double now){
    if(!live(o))return 0;o.records(b);
    o.humanInput(b,snapshotPlayers(b));return o.runtime->prewarmPlayers(now,b.live);
}
std::size_t presentPoses(Owner&o,Battle&b,double now){
    if(!live(o))return 0;std::vector<std::string> errors;
    const auto count=presentNativeBotPoses(b,*o.runtime,now,o.services,errors);
    for(const auto&error:errors)o.local(p::Error(error));
    return count;
}
void snapshot(Owner&o,const Value&source){
    const auto&message=source.resolved();
    if(!live(o)||message.kind!=Value::Kind::Object)return;
    auto started=Time::now();
    const auto&orders=v::value(message,"bot_orders"),&revision=v::value(message,"bot_order_revision");
    if(orders.kind!=Value::Kind::Null&&revision.kind!=Value::Kind::Null)try{
        std::map<ID,Value> mapping;const auto&rows=v::rows(orders);const auto count=std::min<std::size_t>(30,rows.size());
        for(std::size_t i=0;i<count;++i){const auto&row=rows[i].resolved();const auto key=v::wide(v::value(row,"id"));
            if(!mapping.emplace(key,row).second)throw p::Error("Native Bot order has a duplicate actor");}
        std::map<ID,planning::StrategicOrder> values;require(planning::readPlanningOrders(mapping,values));
        o.runtime->acceptOrders(std::move(values),v::wide(revision));
    }catch(const std::exception&e){o.local(e);}
    const auto&tick=v::value(message,"server_tick"),&time=v::value(message,"server_time_ms");
    if(tick.kind!=Value::Kind::Null){const auto incoming=v::wide(tick);if(incoming<o.server_tick)return;o.server_tick=incoming;}
    const auto&rows=v::value(message,"bots");if(rows.kind!=Value::Kind::Null)for(const auto&entry:v::rows(rows)){const auto&row=entry.resolved();if(row.kind!=Value::Kind::Object)continue;
        try{
            const ID id=v::integral(row,"id");const auto actor=o.runtime->actors().find(id);if(actor==o.runtime->actors().end())continue;
            ServerSnapshot value;require(readNativeServerSnapshot(row,tick,time,actor->second,o.runtime->weapons().at(botKey(id)),value));
            if(o.runtime->serverSnapshot(id,value))++o.snapshot_full_mirrors;else ++o.snapshot_receipt_mirrors;
        }catch(const std::exception&e){o.local(e);}
    }
    o.snapshot_read_ns+=nanos(started);
}

std::vector<std::pair<std::uint64_t,Value>> worldEvents(Owner&o,Battle&b){if(!live(o))return {};return o.events.pack(b.engine.shell.get());}
bool acknowledgeWorld(Owner&o,std::uint64_t sequence){const bool acknowledged=o.events.ack(sequence);if(PyErr_Occurred())throw p::Error("World acknowledgement failed");return acknowledged;}
bool worldIngress(Owner&o,const Value&event,double now){
    if(!live(o))return false;const bool applied=o.events.ingress(event,now);
    if(PyErr_Occurred())o.local(p::Error("Canonical native world event was rejected"));return applied;
}
Value playerContacts(Owner&o,Battle&b,const Value&players,double now){
    if(!live(o))return v::array();
    o.records(b);o.humanInput(b,players,true);
    std::vector<player_contacts::Request> requests;
    require(player_contacts::readPlayerContacts(players,o.humans,requests,o.contact_rejected));
    return player_contacts::packPlayerVerdicts(o.player_contacts.update(requests,now,o.player_world));
}
bool acknowledgePlayerContact(Owner&o,std::uint64_t serial,double now){return o.player_contacts.acknowledge(serial,now);}
std::optional<std::pair<std::int64_t,Value>> playerEnvironment(Owner&o,Battle&b,double elapsed,bool authority){
    if(!live(o))return {};
    if(o.environment.willSample(elapsed,authority)){o.humanInput(b,snapshotPlayers(b));o.records(b);}
    std::optional<player_environment::Batch> batch;
    require(player_environment::readAndAdvance(b,elapsed,authority,o.environment,o.environment_world,o.environment_metadata,batch));
    b.bots->player_environment_check=o.environment.elapsed();
    if(!batch)return {};
    return std::make_pair(std::int64_t(batch->sequence),player_environment::observationsValue(*batch));
}
void acknowledgePlayerEnvironment(Owner&o,Battle&b,std::int64_t sequence,bool sent){
    o.environment.acknowledge(sequence,sent);b.bots->player_environment_seq=o.environment.sequence();
}
Value humanRam(Owner&o,Battle&b){
    if(!live(o))return v::array();o.records(b);
    stock_native_human_ram::NativeWorld<NativeServices,geometry::Vehicles> world(o.services,o.vehicles);
    std::vector<std::string> errors;auto result=stock_native_human_ram::results(b,world,&errors);
    for(const auto&error:errors)o.local(p::Error(error));return result;
}

namespace {
void detachedErrors(Owner&o){for(const auto&error:o.detached_runtime->takeErrors())o.local(p::Error(error));}
void detachedFailure(Owner&o,const char*context,const std::exception&e){o.local(p::Error(std::string(context)+": "+e.what()));}
}
bool detachedQueue(Owner&o,Battle&b,const native_battle::Actor&vehicle,PyObject*entity,const cs::Value*state,double now){
    if(!live(o))return false;bool queued=false;
    try{queued=o.detached_runtime->queue(b,vehicle,entity,state,now);}
    catch(const std::exception&e){detachedFailure(o,"Native detached turret launch failed",e);}
    detachedErrors(o);return queued;
}
bool detachedIngress(Owner&o,Battle&b,const cs::Value&message){
    if(!live(o))return false;bool changed=false;
    try{changed=o.detached_runtime->ingress(b,message);}
    catch(const std::exception&e){detachedFailure(o,"Native detached turret snapshot failed",e);}
    detachedErrors(o);return changed;
}
std::optional<cs::Value> detachedAdvance(Owner&o,Battle&b,double now){
    if(!live(o))return std::nullopt;
    try{auto tail=o.detached_runtime->advance(b,now);detachedErrors(o);o.detached(b);return tail;}
    catch(const std::exception&e){detachedFailure(o,"Native detached turret advance failed",e);return std::nullopt;}
}
cs::Value detachedRows(Owner&o){return o.detached_runtime->rows();}

namespace {
// A malformed projectile message is local to that message; a Python glue error keeps its own detail.
template<class Call>bool projectileInput(Owner&o,const char*context,Call&&call){
    if(!live(o))return false;bool changed=false;
    try{changed=call();if(PyErr_Occurred())o.local(p::Error(context));}
    catch(const std::exception&e){o.local(p::Error(PyErr_Occurred()?std::string(context):std::string(context)+"; "+e.what()));}
    return changed;
}
}
bool projectileIngress(Owner&o,const cs::Value&message,double now){return projectileInput(o,"Native projectile snapshot failed",[&]{return o.projectiles->ingress(message,now);});}
bool projectileObserve(Owner&o,const cs::Value&message,double now){return projectileInput(o,"Native projectile clock message failed",[&]{return o.projectiles->observe(message,now);});}
bool projectileEvent(Owner&o,const cs::Value&event,double now){return projectileInput(o,"Native projectile event failed",[&]{return o.projectiles->event(event,now);});}
std::vector<stock_native_projectile::WireDispatch> projectileAdvance(Owner&o,Battle&b,double now,bool force){
    if(!live(o))return {};
    auto result=o.projectiles->advance(b,now,force);
    if(!live(o))result.clear();return result;
}
bool projectileAcknowledge(Owner&o,std::uint64_t serial,bool sent){return o.projectiles->acknowledge(serial,sent);}
std::vector<stock_native_projectile::LaunchMessage> projectileLaunches(Owner&o,const cs::Value*message){
    if(!live(o))return {};return o.projectiles->launches(message);
}
void projectileLaunchResult(Owner&o,std::int64_t bot,std::int64_t sequence,bool accepted){o.projectiles->launchResult(bot,sequence,accepted);}
void transportError(Owner&o,const std::string&message){o.local(p::Error("Native projectile transport failed: "+message));}

bool acknowledgeLaunch(Owner&o,std::int64_t bot,std::int64_t sequence){return o.runtime->acknowledgeLaunch(bot,sequence);}
void acknowledgeRam(Owner&o,std::int64_t player,std::int64_t sequence){o.runtime->acknowledgeRam(player,sequence);}
void stageTiming(Owner&o,bool enabled){o.runtime->stageTiming(enabled);}
Value statistics(Owner&o){
    auto out=v::object();auto&r=out.object;
    const auto&s=o.runtime->statistics();const auto&d=o.backend.presentation_counters();
    r["callbacks"]=v::integer(std::int64_t(s.callbacks));r["slices"]=v::integer(std::int64_t(s.slices));
    r["refreshes"]=v::integer(std::int64_t(s.refreshes));r["actors"]=v::integer(std::int64_t(s.actors));
    r["refresh_slices"]=v::integer(std::int64_t(s.refresh_slices));r["reuse_slices"]=v::integer(std::int64_t(s.reuse_slices));
    r["refresh_ns"]=v::integer(std::int64_t(s.refresh_ns));r["reuse_ns"]=v::integer(std::int64_t(s.reuse_ns));
    r["refresh_actors"]=v::integer(std::int64_t(s.refresh_actors));r["reuse_actors"]=v::integer(std::int64_t(s.reuse_actors));
    r["python_business_callbacks"]=v::integer(std::int64_t(s.python_business_callbacks));r["stock_presentation_callbacks"]=v::integer(std::int64_t(d.python_boundary_calls));
    r["cold_installs"]=v::integer(std::int64_t(o.cold_installs));r["ingress_ns"]=v::integer(std::int64_t(o.ingress_ns));
    r["step_ns"]=v::integer(std::int64_t(o.step_ns));r["publication_ns"]=v::integer(std::int64_t(o.publication_ns));
    r["snapshot_read_ns"]=v::integer(std::int64_t(o.snapshot_read_ns));
    r["snapshot_full_mirrors"]=v::integer(std::int64_t(o.snapshot_full_mirrors));r["snapshot_receipt_mirrors"]=v::integer(std::int64_t(o.snapshot_receipt_mirrors));
    r["ingress_errors"]=v::integer(std::int64_t(o.ingress_errors));r["last_ingress_error"]=v::text(o.last_ingress_error);
    r["stage_timing"]=v::boolean(s.stage_timing);r["detailed_slices"]=v::integer(std::int64_t(s.detailed_slices));
    const char*stage_names[]={"prepare","combat","decision","weapon_prepare","motion_plan","motion","weapon_finish",
        "observation","suspension","contacts","support_recheck","slope","publication"};
    static_assert(sizeof(stage_names)/sizeof(*stage_names)==std::size_t(Stage::count),"Native stage name count differs");
    for(std::size_t index=0;index<std::size_t(Stage::count);++index)
        r[std::string("stage_")+stage_names[index]+"_ns"]=v::integer(std::int64_t(s.stage_ns[index]));
    r["rejected_player_contacts"]=v::integer(std::int64_t(o.contact_rejected));
    std::size_t ready_bots=0,live_bots=0;
    for(const auto&entry:o.runtime->actors())if(entry.second.motion.alive){
        ++live_bots;const auto*vehicle=o.vehicles.find(botKey(entry.first));
        if(vehicle&&vehicle->ready)++ready_bots;
    }
    r["live_bots"]=v::integer(std::int64_t(live_bots));
    r["ready_bot_geometry"]=v::integer(std::int64_t(ready_bots));
    // The projectile owner still reports Python statistics.
    p::Ref projectile(o.projectiles->statistics());for(auto&row:cs::read(projectile.get()).object)r["projectile_"+row.first]=std::move(row.second);
    return out;
}
}
}
