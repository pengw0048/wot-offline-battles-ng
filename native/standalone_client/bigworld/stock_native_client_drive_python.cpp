#include "pch.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive.hpp"
#include "stock_native_client_drive_geometry.hpp"
#include "stock_native_client_drive_status.hpp"
#include "stock_native_client_drive_publication.hpp"
#include "stock_native_client_drive_install.hpp"
#include "stock_native_client_drive_world.hpp"
#include "stock_native_client_drive_dispatch.hpp"
#include "stock_native_client_drive_peers_python.hpp"
#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_input_controls_python.hpp"
#include "stock_native_client_survival_python.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_destructible_backend.hpp"
#include "stock_native_world_snapshot.hpp"
#include "stock_native_world_events.hpp"
#include "stock_native_client_self_presentation.hpp"
#include "stock_native_client_critical.hpp"
#include "stock_native_battle_local.hpp"
namespace native_battle {
namespace world {
// Provided by the world subsystem: the destructibles sensor whose accepted ledger the visible world mirrors.
PyObject* sensor(const Battle&);
}
namespace vehicles {
// Provided by the vehicles subsystem: the accepted motion revision time (us) and the local clock it arrived at.
std::optional<std::pair<std::int64_t,double>> poseMotionAnchor(const Battle&);
}
namespace projectiles {
// Provided by the projectiles subsystem: the latest server time (ms) and the local clock it was received at.
std::optional<std::pair<double,double>> serverClockAnchor(const Battle&);
}}
namespace stock_native_client_query {struct ProjectileTurrets;std::shared_ptr<ProjectileTurrets> projectileTurrets(native_battle::Battle&);}
#include "space/space_manager.hpp"
#include "pyscript/script.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include <memory>
#include <algorithm>

namespace stock_native_client_drive {
namespace {
namespace p=stock_native_bot_python;
namespace world=stock_native_world;
const char*const capsule_name="offline_client_drive_native.owner";
void require(bool value){if(!value)throw p::Error("Native player drive installation failed");}
p::Ref engineEntity(std::int64_t id){auto*c=BW::ConnectionControl::pInstance();auto*e=c?c->findEntity(static_cast<BW::EntityID>(id)):nullptr;if(!e||e->isDestroyed()||!e->isInWorld())return p::retain(Py_None);return p::Ref(e->pPyEntity().newRef());}
void attribute(PyObject*object,const char*name,const p::Ref&value){if(PyObject_SetAttrString(object,name,value.get())<0)throw p::Error("Native player drive mirror assignment failed");}
std::optional<Point> optionalPoint(PyObject*object,const char*name){auto v=p::value(object,name);return v.get()==Py_None?std::nullopt:std::optional<Point>(p::numbers<3>(v.get()));}
step::SuspensionDescriptor descriptor(PyObject*params){step::SuspensionDescriptor out;
    const char*names[]={"mass","pitch_inertia","roll_inertia","fixed_step","constraint_iterations"};
    for(unsigned i=0;i<5;++i)out.parameters[i]=p::numeric(params,names[i]);
    p::Ref physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));
    const char*tuning[]={"GRAVITY","CONTACT_PENETRATION","AIRBORNE_ANGULAR_DAMPING","AIRBORNE_ANGULAR_SPEED_LIMIT","FREEZE_ACCEL_EPSILON","FREEZE_VEL_EPSILON","FREEZE_ANG_ACCEL_EPSILON","FREEZE_ANG_VEL_EPSILON","SERVER_PHYSICS_MAX_SUBSTEPS"};
    for(unsigned i=0;i<9;++i)out.tuning[i]=p::numeric(physics.get(),tuning[i]);out.maximum_steps=int(out.tuning[8]);
    out.width=p::numeric(params,"width");out.clearance=p::numeric(params,"clearance");out.rest_length=p::numeric(params,"rest_length");
    out.contact_memory_distance=p::numeric(params,"contact_memory_distance");out.footprint_half_width=p::numeric(params,"footprint_half_width");
    auto read=[&](const char*name,std::vector<step::SuspensionContact>&rows,bool spring){auto raw=p::value(params,name);if(raw.get()==Py_None)return;
        for(Py_ssize_t i=0;i<p::size(raw.get());++i){auto v=p::item(raw.get(),i);step::SuspensionContact c;
            c.x=p::numeric(v.get(),"x");c.y=p::numeric(v.get(),"y");c.z=p::numeric(v.get(),"z");auto side=p::value(v.get(),"side"),kind=p::value(v.get(),"kind");
            const auto text=p::string(side.get());c.side=text=="left"?-1:text=="right"?1:0;c.spring=spring;c.rigid=p::string(kind.get())=="rigid";c.track=p::string(kind.get())=="track";
            if(spring){c.stiffness=p::numeric(v.get(),"stiffness");c.damping=p::numeric(v.get(),"damping");c.compression=p::numeric(v.get(),"static_compression");
                c.maximum=p::numeric(v.get(),"max_compression");c.force=p::numeric(v.get(),"max_force");c.rest_length=p::numeric(v.get(),"rest_length");
                const double footprint=p::numeric(params,"footprint_half_length",.3);c.footprint_front=p::numeric(v.get(),"footprint_front",footprint);c.footprint_rear=p::numeric(v.get(),"footprint_rear",footprint);
            }else c.penetration=p::numeric(v.get(),"penetration",.1);rows.push_back(c);}};
    read("springs",out.springs,true);read("pseudo_contacts",out.pseudo,false);read("rigid_contacts",out.rigid,false);
    out.turret_origin=optionalPoint(params,"turret_origin");auto turrets=p::value(params,"turret_contacts");if(turrets.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(turrets.get());++i){auto v=p::item(turrets.get(),i);out.turret_contacts.push_back(p::numbers<3>(v.get()));}
    return out;
}
struct NativeOwner {
    BW::ClientSpacePtr space;world::Registry registry;stock_native_destructible::NativeBackend backend;
    world::NativeScene scene;world::Pipeline pipeline;SceneGround ground;LocalGeometry local_geometry;
    std::unique_ptr<p::Ref> sensor,authority;native_battle::Battle*battle=nullptr;
    std::unique_ptr<Owner> drive;std::unique_ptr<p::Ref> params;std::uint64_t generation=0;bool retired=false;
    StatusOwner statuses;
    std::unique_ptr<PlayerDriver> player;
    PlayerContacts player_contacts;
    PublicationLedger publication;
    std::unique_ptr<NativePeers> peers;
    PeerInstaller peer_installer;
    std::unique_ptr<PlayerImpact> impact;
    std::unique_ptr<PlayerRam> ram;
    std::unique_ptr<NativePlayerWorld> actual;
    std::unique_ptr<p::Ref> player_entity,player_physics,player_resource;
    std::uint64_t player_descriptor_revision=0;
    std::shared_ptr<const void>player_geometry;
    std::uint64_t full_frames=0,cold_projections=0;
    std::uint64_t mirrored_journal=0;
    std::unique_ptr<world::WorldEvents> events;
    std::optional<double> last_frame;
    bool ledger_installed=false;std::array<std::int64_t,2> ledger_revision{};std::uint64_t ledger_refreshes=0,ledger_reuses=0;
    std::uint64_t native_accepted_revision=0;
    std::set<world::MaterialKey> native_destroyed;
    NativeOwner(BW::ClientSpacePtr s,std::uint16_t flags,std::uint64_t gen):space(s),backend(s,registry,[](const world::TurretSweep&){return true;}),
        scene(s,registry,backend),pipeline(registry,scene,flags),ground(pipeline),generation(gen){}
    bool current(const native_battle::Battle&b)const{return !retired&&scene.current()&&b.generation==generation;}
    void retire(){retired=true;actual.reset();scene.retire();backend.retire();if(Py_IsInitialized()&&battle)retireSelfPresentation(*battle);}
    void syncLedger(){
        // The existing visible adapter owns destruction and its presentation.
        // Copy only its accepted raw ledger at a slice boundary. No per-ray
        // producer, predicate, authority method or animator runs in Python.
        const std::array<std::int64_t,2> revision{{p::integral(authority->get(),"_native_client_drive_ledger_revision",-1),p::integral(sensor->get(),"_native_client_drive_ledger_revision",-1)}};
        if(revision[0]<0||revision[1]<0)throw p::Error("Native player ledger mutation counters were not installed");
        const auto native_revision=backend.runtime().counters().accepted;
        if(ledger_installed&&revision==ledger_revision&&native_revision==native_accepted_revision){++ledger_reuses;return;}
        if(native_revision!=native_accepted_revision){for(const auto&event:backend.runtime().acceptedOrders())native_destroyed.insert({event.key.first,event.key.second,event.kind==world::Kind::Structure?event.material:-1});native_accepted_revision=native_revision;}
        std::set<world::MaterialKey> broken,speculative;std::set<world::Key> trees;
        auto state=p::value(authority->get(),"_state"),chunks=p::value(state.get(),"chunks");
        if(!PyDict_Check(chunks.get()))throw p::Error("Native player accepted world ledger is unavailable");
        Py_ssize_t cursor=0;PyObject*chunk=nullptr,*row=nullptr;
        auto token=[&](PyObject*raw,std::int64_t chunk_id,bool with_chunk){
            const auto size=p::size(raw);if(size!=(with_chunk?3:2))throw p::Error("Native player world ledger width is invalid");
            auto first=p::item(raw,0),item=p::item(raw,with_chunk?1:0),material=p::item(raw,with_chunk?2:1);
            const auto c=with_chunk?p::wide(first.get()):chunk_id,i=p::wide(item.get());
            if(c<0||std::uint64_t(c)>0xffffffffULL||i<0||i>0xffff)throw p::Error("Native player world ledger identity is invalid");
            return world::MaterialKey{std::uint32_t(c),std::uint32_t(i),material.get()==Py_None?-1:p::wide(material.get())};};
        auto visit=[&](PyObject*raw,const auto&callback){if(!PySet_Check(raw)&&!PyFrozenSet_Check(raw))throw p::Error("Native player world ledger requires a raw set");
            p::Ref iterator(PyObject_GetIter(raw));while(PyObject*item=PyIter_Next(iterator.get())){p::Ref value(item);callback(value.get());}if(PyErr_Occurred())throw p::Error("Native player world ledger iteration failed");};
        while(PyDict_Next(chunks.get(),&cursor,&chunk,&row)){const auto id=p::wide(chunk);auto keys=p::value(row,"keys");if(keys.get()!=Py_None)visit(keys.get(),[&](PyObject*v){broken.insert(token(v,id,false));});}
        auto pending=p::value(sensor->get(),"g_offh_destr_speculative");if(pending.get()!=Py_None)visit(pending.get(),[&](PyObject*v){speculative.insert(token(v,0,true));});
        auto tree_state=p::value(sensor->get(),"g_offh_tree_state");
        auto committed=[&](){auto value=p::value(tree_state.get(),"native_committed");return value.get()==Py_None?p::value(tree_state.get(),"felled"):p::retain(value.get());}();
        if(committed.get()!=Py_None)visit(committed.get(),[&](PyObject*v){const auto key=visibleTreeKey(v);if(key&&!registry.isolated(*key)){trees.insert(*key);broken.insert({key->first,key->second,-1});}});
        broken.insert(native_destroyed.begin(),native_destroyed.end());
        if(registry.skin.destroyed!=broken||registry.skin.speculative!=speculative){registry.skin.destroyed=std::move(broken);registry.skin.speculative=std::move(speculative);++registry.generation;}
        registry.skin.tree_instances.insert(trees.begin(),trees.end());
        ledger_revision=revision;ledger_installed=true;++ledger_refreshes;
    }
    void bumpLedger(){for(auto*object:{authority->get(),sensor->get()})attribute(object,"_native_client_drive_ledger_revision",p::integer(p::integral(object,"_native_client_drive_ledger_revision",0)+1));}
    void mirrorJournal(){if(registry.journal.empty()||registry.journal.back().sequence<=mirrored_journal)return;const auto first=std::upper_bound(registry.journal.begin(),registry.journal.end(),mirrored_journal,[](std::uint64_t sequence,const world::Event&event){return sequence<event.sequence;});bool changed=false;for(auto at=first;at!=registry.journal.end();++at){require(events&&events->mirrorOnly(at->destruction));mirrored_journal=at->sequence;changed=true;}if(changed){bumpLedger();syncLedger();}}
    void frame(double now){if(!std::isfinite(now))throw p::Error("Native player world clock is invalid");syncLedger();
        if(events){require(events->mappingChanged(now));backend.runtime().update(last_frame?std::max(0.,now-*last_frame):0.,now);require(events->synchronizeGeometry());}last_frame=now;}
    void installParams(PyObject*value){if(params&&params->get()==value)return;
        drive.reset(new Owner(descriptor(value)));params.reset(new p::Ref(p::retain(value)));}
    void installActual(native_battle::Battle&battle,PyObject*entity,const std::shared_ptr<stock_native_client_state::State>&state){
        const auto key="player:"+std::to_string(state->local_player_id);const auto found=state->actors.find(key);if(found==state->actors.end()||!found->second->local)throw p::Error("Native local actor installation is unavailable");
        auto physics=p::retain(battle.local->physics?battle.local->physics.get():Py_None),resource=p::value(entity,"typeDescriptor");const bool changed=!player_entity||player_entity->get()!=entity||!player_physics||player_physics->get()!=physics.get()||!player_resource||player_resource->get()!=resource.get()||player_descriptor_revision!=found->second->descriptor_revision||player_geometry!=found->second->geometry;
        if(actual&&!changed)return;
        actual.reset();auto metadata=readPlayerDescriptor(battle,entity,physics.get());if(player)player->descriptor(std::move(metadata));else{player=std::make_unique<PlayerDriver>(std::move(metadata),readPlayerInitialState(battle,*found->second));player->controls()=readPlayerInitialControls(battle);}
        // The fixed descriptor projection is a cold producer. Its original
        // exclusion/error lifecycle remains intact; no physical slice calls it.
        ++cold_projections;p::Ref suspension(native_battle::local::ensureSuspensionParams(battle,resource.get()));
        if(suspension.get()!=Py_None)installParams(suspension.get());else{drive.reset();params.reset();}
        player->state().suspension_disabled=battle.local->suspension_disabled;
        if(!peers){p::Ref collision(PyImport_ImportModule("gui.mods.offline_lan_0922.tank_collision")),laws(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));peers=std::make_unique<NativePeers>(player_contacts,p::numeric(collision.get(),"CONTACT_BROADPHASE_PADDING"),p::numeric(laws.get(),"SERVER_PHYSICS_STEP"));}
        {step::InternalLayout layout;if(!step::load_internal_layout(resource.get(),layout))throw p::Error("Native player internal geometry installation failed");auto impact_config=readPlayerImpactConfig(entity);if(impact)impact->metadata(std::move(impact_config),std::move(layout));else impact=std::make_unique<PlayerImpact>(publication,std::move(impact_config),std::move(layout));}
        if(!ram)ram=std::make_unique<PlayerRam>(publication,state,found->second,nativeRamGeometry());
        auto config=readNativeWorldConfig(battle,entity,player->descriptor());auto turrets=stock_native_client_query::projectileTurrets(battle);
        actual=std::make_unique<NativePlayerWorld>(battle,entity,state,found->second,*player,scene,pipeline,ground,drive.get(),*peers,player_contacts,publication,*impact,*ram,std::move(turrets),std::move(config),local_geometry,sensor->get());
        player_entity=std::make_unique<p::Ref>(p::retain(entity));player_physics=std::make_unique<p::Ref>(p::retain(physics.get()));player_resource=std::make_unique<p::Ref>(p::retain(resource.get()));player_descriptor_revision=found->second->descriptor_revision;player_geometry=found->second->geometry;
    }
};
std::shared_ptr<NativeOwner> existing(const native_battle::Battle&b){auto owner=b.local?std::static_pointer_cast<NativeOwner>(b.local->drive):std::shared_ptr<NativeOwner>{};return owner&&owner->current(b)?owner:std::shared_ptr<NativeOwner>{};}
std::shared_ptr<NativeOwner> owned(const native_battle::Battle&b){auto owner=existing(b);if(!owner)throw p::Error("Native client scene owner is unavailable");return owner;}
// Every drive slice publishes the player's pose for the other subsystems.
void publishPose(native_battle::LocalVehicle&local,const PlayerState&s){local.position=s.position;local.yaw=s.yaw;local.pitch=s.pitch;local.roll=s.roll;local.speed=s.speed;local.turn_speed=s.turn_speed;local.airborne=s.airborne;}
// The latest accepted motion revision time (us) and the local clock it was received at, from the replica.
std::optional<std::pair<ID,double>> motionAnchor(const native_battle::Battle&b){return native_battle::vehicles::poseMotionAnchor(b);}
}
SceneAccess sceneAccess(const native_battle::Battle&b){auto owner=owned(b);return {owner,&owner->registry,&owner->scene,&owner->pipeline,&owner->backend,owner->events.get()};}
void synchronizeStatus(const native_battle::Battle&b,double now){
    auto owner=owned(b);const auto anchor=native_battle::projectiles::serverClockAnchor(b);std::optional<double>estimated;if(anchor)estimated=anchor->first+std::max(0.,now-anchor->second)*1000.;owner->statuses.synchronize(*b.vehicles,now,estimated);
}
DriverAccess driverAccess(native_battle::Battle&b){
    auto owner=owned(b);if(!owner->player){auto*actor=b.vehicles->find("player:"+std::to_string(b.vehicles->local_player_id));if(!actor)throw p::Error("Native local actor is not installed");auto entity=engineEntity(actor->engine_id);if(entity.get()==Py_None)throw p::Error("Native local entity is unavailable");installPlayer(b,entity.get(),b.local->physics?b.local->physics.get():Py_None);}
    return {owner,owner->player.get(),&owner->player_contacts,&owner->publication};
}
void installPlayer(native_battle::Battle&b,PyObject*entity,PyObject*params){
    auto owner=owned(b);auto&state=*b.vehicles;
    auto*actor=state.find("player:"+std::to_string(state.local_player_id));if(!actor||!actor->local)throw p::Error("Native local player actor is unavailable");
    auto metadata=readPlayerDescriptor(b,entity,params);
    if(owner->player)owner->player->descriptor(std::move(metadata));else{owner->player=std::make_unique<PlayerDriver>(std::move(metadata),readPlayerInitialState(b,*actor));owner->player->controls()=readPlayerInitialControls(b);const auto&s=owner->player->state();const auto&c=owner->player->controls();require(state.updateLocal({s.position[0],s.position[1],s.position[2],s.yaw,s.pitch,s.roll,c.aim_yaw,c.gun_pitch},{},s.speed));}
}
void mirrorDestruction(const native_battle::Battle&b,const world::Destruction&event){auto owner=owned(b);if(!owner->events||!owner->events->mirrorOnly(event))throw p::Error("Native accepted destruction mirror failed");owner->bumpLedger();owner->syncLedger();}
}

namespace native_battle {namespace local {
namespace dr=stock_native_client_drive;
namespace cs=stock_native_client_state;
// The visible player's native world owner over the battle's space, created before the first frame.
void prepare(Battle&b){
    if(b.worker||!b.local||dr::existing(b))return;
    const auto id=p::integral(b.engine.avatar.get(),"spaceID");if(id<=0||id>2147483647)throw p::Error("Native player space identity is invalid");
    auto space=BW::SpaceManager::instance().space(BW::SpaceID(id));if(!space||space->cleared())throw p::Error("Native player space is unavailable");
    // TRIANGLE_NOCOLLIDE | TRIANGLE_WATER (collision_flags.VEHICLE_SKIP_FLAGS).
    auto owner=std::make_shared<dr::NativeOwner>(space,std::uint16_t(0x10|0x40),b.generation);owner->battle=&b;
    PyObject*sensor=native_battle::world::sensor(b);owner->sensor.reset(new p::Ref(p::retain(sensor)));
    owner->authority.reset(new p::Ref(p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.destructibles_authority"))));
    p::Ref builder(PyImport_ImportModule("gui.mods.offline_lan_0922.stock_native_world_snapshot"));p::Ref build_args(PyTuple_Pack(1,sensor));auto dto=p::call(builder.get(),"build",build_args.get());
    owner->local_geometry.seedIsolation(sensor);
    dr::require(stock_native_world::installWorldSnapshot(dto.get(),owner->registry,owner->scene,owner->backend));owner->syncLedger();
    dr::require(owner->backend.installPresenter());owner->events=std::make_unique<stock_native_world::WorldEvents>(owner->registry,owner->backend,owner->scene,std::uint32_t(id));dr::require(owner->events->install(b.engine.shell.get()));
    b.local->drive=owner;
}
void retire(Battle&b){
    if(!b.local)return;
    stock_native_client_survival::retire(b);stock_native_client_critical::close(b);
    auto owner=std::static_pointer_cast<dr::NativeOwner>(b.local->drive);if(owner){try{owner->retire();}catch(const std::exception&){PyErr_Clear();}}b.local->drive.reset();
}
void worldFrame(Battle&b,double now){auto owner=dr::existing(b);if(owner)owner->frame(now);}
bool ingress(Battle&b,const Value&event,double now){auto owner=dr::owned(b);if(!owner->events)throw p::Error("Native visible world events are not installed");p::Ref raw(cs::write(event));const bool accepted=owner->events->ingress(raw.get(),now);if(!accepted&&PyErr_Occurred())throw p::Error("Native canonical destruction was not applied");owner->bumpLedger();owner->syncLedger();return accepted;}
void died(Battle&b){if(b.replay)return;auto owner=dr::existing(b);if(owner&&owner->player)owner->player->state().speed=0.;if(b.local)b.local->speed=0.;}
// One visible player slice: world ingress, status, the native driver and its reliable input publications.
void drive(Battle&b,double dt){
    auto owner=dr::owned(b);auto state=b.vehicles;const auto key="player:"+std::to_string(state->local_player_id);const auto found=state->actors.find(key);
    if(found==state->actors.end()||!found->second||!found->second->has_current)return;auto entity=dr::engineEntity(found->second->engine_id);if(entity.get()==Py_None)return;
    const double now=b.clock();owner->frame(now);owner->installActual(b,entity.get(),state);owner->peer_installer.synchronize(b,*state,*owner->peers);dr::synchronizeStatus(b,now);
    const auto anchor=dr::motionAnchor(b);owner->actual->frame(now,anchor?std::optional<dr::ID>{anchor->first}:std::nullopt,anchor?anchor->second:now);
    const auto levels=stock_native_client_survival::levels(b);owner->actual->environment(levels[0],levels[1],stock_native_client_critical::report(b).resolved().kind!=Value::Kind::Null);owner->actual->battleOver(b.result.resolved().kind!=Value::Kind::Null);
    owner->player->controls().autorotation=stock_native_client_input::autorotationEnabled(b);const auto publication=owner->player->advance(*owner->actual,dt);++owner->full_frames;owner->mirrorJournal();
    if(!owner->current(b)||state->retired)return;dr::publishPose(*b.local,owner->player->state());
    struct Sink final:dr::PlayerPublicationSink {Battle&b;double now;Sink(Battle&battle,double n):b(battle),now(n){}bool barriers()override{return stock_native_client_input::flushBarrierInputsAt(b,now);}bool current(std::optional<bool>mode={})override{return stock_native_client_input::sendCurrentAt(b,now,mode);}bool landing()override{return stock_native_client_input::flushLandingAt(b,now);}std::optional<dr::ID>inputSequence()const override{auto sequence=p::value(b.engine.client.get(),"_input_seq");if(sequence.get()==Py_None||PyBool_Check(sequence.get())||(!PyInt_Check(sequence.get())&&!PyLong_Check(sequence.get())))return {};const auto n=p::wide(sequence.get());return n>0?std::optional<dr::ID>{n}:std::nullopt;}}sink(b,now);
    dr::publishPlayer(*owner->player,owner->publication,publication,sink);
}
bool acknowledgeContacts(Battle&b,const Value&snapshot,bool destructible){try{auto owner=dr::existing(b);if(!owner)return false;const auto&players=snapshot.get("players").resolved();if(players.kind!=Value::Kind::Array)return false;
    for(const auto&raw:players.array){const auto&row=raw.resolved();if(row.kind!=Value::Kind::Object)continue;if(std::int64_t(row.get("id").numeric(-1.))!=b.vehicles->local_player_id)continue;
        if(!destructible)return owner->publication.acknowledgeRam(dr::ID(row.get("ram_contact_admitted_seq").numeric()),dr::ID(row.get("ram_contact_resolved_seq").numeric()));
        const auto vector=[&](const char*name){std::vector<dr::ID>out;for(const auto&value:row.get(name).resolved().array)out.push_back(dr::ID(value.numeric()));return out;};
        const auto rejected=owner->publication.acknowledgeDestructible(dr::ID(row.get("destructible_contact_admitted_seq").numeric()),dr::ID(row.get("destructible_contact_resolved_seq").numeric()),vector("destructible_contact_resolved_seqs"),vector("destructible_contact_rejected_seqs"));
        auto predicted=p::value(owner->sensor->get(),"g_offh_destr_speculative");for(const auto&receipt:rejected)for(const auto&token:receipt.get("token").resolved().array){const auto&fields=token.resolved().array;if(fields.size()!=3)continue;const auto chunk=std::uint32_t(fields[0].numeric()),item=std::uint32_t(fields[1].numeric());const auto&material=fields[2].resolved();const dr::ID kind=material.kind==Value::Kind::Null?-1:dr::ID(material.numeric());owner->registry.skin.speculative.erase({chunk,item,kind});if(predicted.get()!=Py_None){auto mat=kind==-1?p::retain(Py_None):p::integer(kind);p::Ref k(Py_BuildValue("(IIO)",chunk,item,mat.get()));if(PySet_Discard(predicted.get(),k.get())<0)throw p::Error("Native rejected local prediction could not retire");}}
        if(!rejected.empty()){owner->bumpLedger();++owner->registry.generation;owner->syncLedger();}return true;
    }return false;
}catch(const std::exception&){PyErr_Clear();return false;}}
bool observeFeedback(Battle&b,PyObject*first,PyObject*second,double now){try{auto owner=dr::existing(b);if(!owner||!owner->actual)return false;auto a=p::value(first,"filter"),c=p::value(second,"filter"),av=p::value(a.get(),"velocity"),bv=p::value(c.get(),"velocity");BW::Vector3 va,vb;if(BW::Script::setData(av.get(),va)!=0||BW::Script::setData(bv.get(),vb)!=0)throw p::Error("Native observed contact velocity unavailable");owner->actual->observeFeedback(p::integral(first,"id"),p::integral(second,"id"),{va.x,va.y,va.z},{vb.x,vb.y,vb.z},now);return true;}catch(const std::exception&){PyErr_Clear();return false;}}
bool observeContact(Battle&battle,PyObject*first,PyObject*second,PyObject*hit,double time){try{if(!std::isfinite(time))return false;auto owner=dr::existing(battle);if(!owner)return false;if(!owner->actual||!owner->ram||!owner->peers||!owner->player)return false;auto state=battle.vehicles;auto*local=state->find("player:"+std::to_string(state->local_player_id));if(!local)return false;PyObject*own=nullptr,*other=nullptr;const auto first_id=p::integral(first,"id"),second_id=p::integral(second,"id");if(first_id==local->engine_id){own=first;other=second;}else if(second_id==local->engine_id){own=second;other=first;}else return false;
    std::shared_ptr<cs::Actor>peer_actor;for(const auto&row:state->actors)if(row.second&&row.second->kind=="bot"&&row.second->ready&&row.second->engine_id==p::integral(other,"id")){peer_actor=row.second;break;}if(!peer_actor)return false;const auto*parameters=owner->peers->descriptor(peer_actor->key);if(!parameters)return false;
    const auto point=[](PyObject*value){BW::Vector3 out;if(BW::Script::setData(value,out)!=0)throw p::Error("Native ram callback point is unavailable");Point p{out.x,out.y,out.z};for(double v:p)if(!std::isfinite(v))throw p::Error("Native ram callback point is nonfinite");return p;};auto own_filter=p::value(own,"filter"),other_filter=p::value(other,"filter"),own_velocity=p::value(own_filter.get(),"velocity"),other_velocity=p::value(other_filter.get(),"velocity"),other_position=p::value(other,"position");const auto a=point(own_velocity.get()),b=point(other_velocity.get()),position=point(other_position.get());
    auto matrix=p::value(other,"matrix");BW::MatrixProviderPtr provider;if(matrix.get()!=Py_None&&BW::Script::setData(matrix.get(),provider,"native ram callback body")!=0)throw p::Error("Native ram callback matrix is unavailable");double yaw=0.;if(provider){BW::Matrix body;provider->matrix(body);yaw=body.yaw();}
    dr::ContactPeer peer;peer.bot=true;peer.actor=peer_actor;peer.network_id=peer_actor->id;peer.engine_id=peer_actor->engine_id;peer.body.id=1000000+peer.engine_id;peer.body.team=dr::ID(peer_actor->state.get("team").numeric());peer.body.x=position[0];peer.body.y=position[1];peer.body.z=position[2];peer.body.yaw=yaw;peer.body.shape=parameters->motion.collision_shape;peer.body.mass=parameters->motion.drive.mass;
    offline_contact::Body body;const auto&s=owner->player->state();body.x=s.position[0];body.y=s.position[1];body.z=s.position[2];body.yaw=s.yaw;body.pitch=s.pitch;body.roll=s.roll;body.shape=owner->player->descriptor().motion.collision_shape;body.mass=owner->player->descriptor().motion.drive.mass;body.team=owner->player->descriptor().team;
    const auto anchor=dr::motionAnchor(battle);if(!anchor)return false;const auto stamp=dr::ID(double(anchor->first)+std::max(0.,battle.clock()-anchor->second)*1000000.);return owner->ram->observe(peer,point(hit),a,b,stamp,body,dr::actualRamProfile(local->state.get("effective_params"),dr::status(*local)->casualties()));
}catch(const std::exception&){PyErr_Clear();return false;}}
}}
