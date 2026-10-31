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
#include "stock_native_world_events.hpp"
#include "stock_native_client_self_presentation.hpp"
#include "stock_native_client_critical.hpp"
#include "stock_native_battle_local.hpp"
#include "stock_native_battle_world.hpp"
namespace native_battle {
namespace vehicles {
// Provided by the vehicles subsystem: the accepted motion revision time (us) and the local clock it arrived at.
std::optional<std::pair<std::int64_t,double>> poseMotionAnchor(const Battle&);
}
namespace projectiles {
// Provided by the projectiles subsystem: the latest server time (ms) and the local clock it was received at.
std::optional<std::pair<double,double>> serverClockAnchor(const Battle&);
}}
namespace stock_native_client_query {class ProjectileTurrets;std::shared_ptr<ProjectileTurrets> projectileTurrets(native_battle::Battle&);}
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
struct NativeOwner {
    // The battle space's shared native world (world::sceneAccess); this owner keeps its own actor pipeline.
    SceneAccess world_access;world::Registry& registry;stock_native_destructible::NativeBackend& backend;
    world::NativeScene& scene;world::Pipeline pipeline;SceneGround ground;LocalGeometry local_geometry;
    native_battle::Battle*battle=nullptr;
    std::unique_ptr<Owner> drive;std::optional<std::uint64_t> params_revision;std::uint64_t generation=0;bool retired=false;
    StatusOwner statuses;
    std::unique_ptr<PlayerDriver> player;
    PlayerContacts player_contacts;
    PublicationLedger publication;
    std::unique_ptr<NativePeers> peers;
    PeerInstaller peer_installer;
    std::unique_ptr<PlayerImpact> impact;
    std::unique_ptr<PlayerRam> ram;
    std::unique_ptr<NativePlayerWorld> actual;
    std::unique_ptr<p::Ref> player_entity,player_resource;std::optional<std::uint64_t> player_physics;
    std::uint64_t player_descriptor_revision=0;
    std::shared_ptr<const void>player_geometry;
    std::uint64_t full_frames=0,cold_projections=0;
    std::uint64_t mirrored_journal=0;
    world::WorldEvents* events=nullptr;
    NativeOwner(SceneAccess access,std::uint16_t flags,std::uint64_t gen):world_access(std::move(access)),registry(*world_access.registry),
        backend(*world_access.backend),scene(*world_access.scene),pipeline(registry,scene,flags),ground(pipeline),generation(gen){events=world_access.events;}
    bool current(const native_battle::Battle&b)const{return !retired&&scene.current()&&b.generation==generation;}
    // The shared scene/backend belong to the world space; this owner only stops using them.
    void retire(){retired=true;actual.reset();if(Py_IsInitialized()&&battle)retireSelfPresentation(*battle);}
    void mirrorJournal(){if(registry.journal.empty()||registry.journal.back().sequence<=mirrored_journal)return;const auto first=std::upper_bound(registry.journal.begin(),registry.journal.end(),mirrored_journal,[](std::uint64_t sequence,const world::Event&event){return sequence<event.sequence;});bool changed=false;for(auto at=first;at!=registry.journal.end();++at){require(events&&events->mirrorOnly(at->destruction));mirrored_journal=at->sequence;changed=true;}(void)changed;}
    void installParams(const step::SuspensionDescriptor&value,std::uint64_t revision){if(params_revision==revision)return;
        drive.reset(new Owner(value));params_revision=revision;}
    void installActual(native_battle::Battle&battle,PyObject*entity,const std::shared_ptr<stock_native_client_state::State>&state){
        const auto key="player:"+std::to_string(state->local_player_id);const auto found=state->actors.find(key);if(found==state->actors.end()||!found->second->local)throw p::Error("Native local actor installation is unavailable");
        const auto&physics=battle.local->physics;if(!physics)throw p::Error("Native player effective physics are unavailable");auto resource=p::value(entity,"typeDescriptor");const bool changed=!player_entity||player_entity->get()!=entity||player_physics!=battle.local->physics_revision||!player_resource||player_resource->get()!=resource.get()||player_descriptor_revision!=found->second->descriptor_revision||player_geometry!=found->second->geometry;
        if(actual&&!changed)return;
        actual.reset();auto metadata=readPlayerDescriptor(battle,resource.get(),*physics);if(player)player->descriptor(std::move(metadata));else{player=std::make_unique<PlayerDriver>(std::move(metadata),readPlayerInitialState(battle,*found->second));player->controls()=readPlayerInitialControls(battle);}
        // The fixed descriptor projection is a cold producer. Its original
        // exclusion/error lifecycle remains intact; no physical slice calls it.
        ++cold_projections;if(const auto*suspension=native_battle::local::ensureSuspensionParams(battle,resource.get()))installParams(*suspension,battle.local->suspension_revision);else{drive.reset();params_revision.reset();}
        player->state().suspension_disabled=battle.local->suspension_disabled;
        if(!peers)peers=std::make_unique<NativePeers>(player_contacts,native_battle::local::law::CONTACT_BROADPHASE_PADDING,native_battle::local::law::SERVER_PHYSICS_STEP);
        {step::InternalLayout layout;if(!step::load_internal_layout(resource.get(),layout))throw p::Error("Native player internal geometry installation failed");auto impact_config=readPlayerImpactConfig(entity);if(impact)impact->metadata(std::move(impact_config),std::move(layout));else impact=std::make_unique<PlayerImpact>(publication,std::move(impact_config),std::move(layout));}
        if(!ram)ram=std::make_unique<PlayerRam>(publication,state,found->second,nativeRamGeometry());
        auto config=readNativeWorldConfig(battle,entity,player->descriptor());auto turrets=stock_native_client_query::projectileTurrets(battle);
        actual=std::make_unique<NativePlayerWorld>(battle,entity,state,found->second,*player,scene,pipeline,ground,drive.get(),*peers,player_contacts,publication,*impact,*ram,std::move(turrets),std::move(config),local_geometry);
        player_entity=std::make_unique<p::Ref>(p::retain(entity));player_physics=battle.local->physics_revision;player_resource=std::make_unique<p::Ref>(p::retain(resource.get()));player_descriptor_revision=found->second->descriptor_revision;player_geometry=found->second->geometry;
    }
};
std::shared_ptr<NativeOwner> existing(const native_battle::Battle&b){auto owner=b.local?std::static_pointer_cast<NativeOwner>(b.local->drive):std::shared_ptr<NativeOwner>{};return owner&&owner->current(b)?owner:std::shared_ptr<NativeOwner>{};}
std::shared_ptr<NativeOwner> owned(const native_battle::Battle&b){auto owner=existing(b);if(!owner)throw p::Error("Native client scene owner is unavailable");return owner;}
// Every drive slice publishes the player's pose for the other subsystems.
void publishPose(native_battle::LocalVehicle&local,const PlayerState&s){local.position=s.position;local.yaw=s.yaw;local.pitch=s.pitch;local.roll=s.roll;local.speed=s.speed;local.turn_speed=s.turn_speed;local.airborne=s.airborne;}
// The latest accepted motion revision time (us) and the local clock it was received at, from the replica.
std::optional<std::pair<ID,double>> motionAnchor(const native_battle::Battle&b){return native_battle::vehicles::poseMotionAnchor(b);}
}
SceneAccess sceneAccess(const native_battle::Battle&b){auto access=native_battle::world::sceneAccess(b);if(!access.registry)throw p::Error("Native client scene owner is unavailable");return access;}
void synchronizeStatus(const native_battle::Battle&b,double now){
    auto owner=owned(b);const auto anchor=native_battle::projectiles::serverClockAnchor(b);std::optional<double>estimated;if(anchor)estimated=anchor->first+std::max(0.,now-anchor->second)*1000.;owner->statuses.synchronize(*b.vehicles,now,estimated);
}
DriverAccess driverAccess(native_battle::Battle&b){
    auto owner=owned(b);if(!owner->player){auto*actor=b.vehicles->find("player:"+std::to_string(b.vehicles->local_player_id));if(!actor)throw p::Error("Native local actor is not installed");auto entity=b.entity(actor->engine_id);if(entity.get()==Py_None)throw p::Error("Native local entity "+std::to_string(actor->engine_id)+" of "+actor->key+" is unavailable");if(!b.local->physics)throw p::Error("Native player effective physics are unavailable");installPlayer(b,entity.get(),*b.local->physics);}
    return {owner,owner->player.get(),&owner->player_contacts,&owner->publication};
}
void installPlayer(native_battle::Battle&b,PyObject*entity,const native_battle::local::DriveParams&params){
    auto owner=owned(b);auto&state=*b.vehicles;
    auto*actor=state.find("player:"+std::to_string(state.local_player_id));if(!actor||!actor->local)throw p::Error("Native local player actor is unavailable");
    auto resource=p::value(entity,"typeDescriptor");auto metadata=readPlayerDescriptor(b,resource.get(),params);
    if(owner->player)owner->player->descriptor(std::move(metadata));else{owner->player=std::make_unique<PlayerDriver>(std::move(metadata),readPlayerInitialState(b,*actor));owner->player->controls()=readPlayerInitialControls(b);const auto&s=owner->player->state();const auto&c=owner->player->controls();require(state.updateLocal({s.position[0],s.position[1],s.position[2],s.yaw,s.pitch,s.roll,c.aim_yaw,c.gun_pitch},{},s.speed));}
}
void mirrorDestruction(const native_battle::Battle&b,const world::Destruction&event){auto owner=owned(b);if(!owner->events||!owner->events->mirrorOnly(event))throw p::Error("Native accepted destruction mirror failed");}
}

namespace native_battle {namespace local {
namespace dr=stock_native_client_drive;
namespace cs=stock_native_client_state;
// The visible player's native world owner over the battle's space, created before the first frame.
void prepare(Battle&b){
    if(b.worker||!b.local||dr::existing(b))return;
    auto access=world::sceneAccess(b);if(!access.registry)throw p::Error("Native player space is unavailable");
    // TRIANGLE_NOCOLLIDE | TRIANGLE_WATER (collision_flags.VEHICLE_SKIP_FLAGS).
    auto owner=std::make_shared<dr::NativeOwner>(std::move(access),std::uint16_t(0x10|0x40),b.generation);owner->battle=&b;
    b.local->drive=owner;
}
void retire(Battle&b){
    if(!b.local)return;
    stock_native_client_survival::retire(b);stock_native_client_critical::close(b);try{closeSiegeHints(b);}catch(const std::exception&){PyErr_Clear();}
    auto owner=std::static_pointer_cast<dr::NativeOwner>(b.local->drive);if(owner){try{owner->retire();}catch(const std::exception&){PyErr_Clear();}}b.local->drive.reset();
}
bool ingress(Battle&b,const Value&event,double now){auto owner=dr::owned(b);if(!owner->events)throw p::Error("Native visible world events are not installed");p::Ref raw(cs::write(event));const bool accepted=owner->events->ingress(raw.get(),now);if(!accepted&&PyErr_Occurred())throw p::Error("Native canonical destruction was not applied");return accepted;}
void died(Battle&b){if(b.replay)return;auto owner=dr::existing(b);if(owner&&owner->player)owner->player->state().speed=0.;if(b.local)b.local->speed=0.;}
// One visible player slice: world ingress, status, the native driver and its reliable input publications.
void drive(Battle&b,double dt){
    auto owner=dr::owned(b);auto state=b.vehicles;const auto key="player:"+std::to_string(state->local_player_id);const auto found=state->actors.find(key);
    if(found==state->actors.end()||!found->second||!found->second->has_current)return;auto entity=b.entity(found->second->engine_id);if(entity.get()==Py_None)return;
    const double now=b.clock();owner->installActual(b,entity.get(),state);owner->peer_installer.synchronize(b,*state,*owner->peers);dr::synchronizeStatus(b,now);
    const auto anchor=dr::motionAnchor(b);owner->actual->frame(now,anchor?std::optional<dr::ID>{anchor->first}:std::nullopt,anchor?anchor->second:now);
    const auto levels=stock_native_client_survival::levels(b);owner->actual->environment(levels[0],levels[1],stock_native_client_critical::report(b).resolved().kind!=Value::Kind::Null);owner->actual->battleOver(b.result.resolved().kind!=Value::Kind::Null);
    owner->player->controls().autorotation=stock_native_client_input::autorotationEnabled(b);const auto publication=owner->player->advance(*owner->actual,dt);++owner->full_frames;owner->mirrorJournal();
    if(!owner->current(b)||state->retired)return;dr::publishPose(*b.local,owner->player->state());
    struct Sink final:dr::PlayerPublicationSink {Battle&b;double now;Sink(Battle&battle,double n):b(battle),now(n){}bool barriers()override{return stock_native_client_input::flushBarrierInputsAt(b,now);}bool current(std::optional<bool>mode={})override{return stock_native_client_input::sendCurrentAt(b,now,mode);}bool landing()override{return stock_native_client_input::flushLandingAt(b,now);}std::optional<dr::ID>inputSequence()const override{return native_battle::local::inputSequence(b);}}sink(b,now);
    dr::publishPlayer(*owner->player,owner->publication,publication,sink);
}
bool acknowledgeContacts(Battle&b,const Value&snapshot,bool destructible){try{auto owner=dr::existing(b);if(!owner)return false;const auto&players=snapshot.get("players").resolved();if(players.kind!=Value::Kind::Array)return false;
    for(const auto&raw:players.array){const auto&row=raw.resolved();if(row.kind!=Value::Kind::Object)continue;if(std::int64_t(row.get("id").numeric(-1.))!=b.vehicles->local_player_id)continue;
        if(!destructible)return owner->publication.acknowledgeRam(dr::ID(row.get("ram_contact_admitted_seq").numeric()),dr::ID(row.get("ram_contact_resolved_seq").numeric()));
        const auto vector=[&](const char*name){std::vector<dr::ID>out;for(const auto&value:row.get(name).resolved().array)out.push_back(dr::ID(value.numeric()));return out;};
        const auto rejected=owner->publication.acknowledgeDestructible(dr::ID(row.get("destructible_contact_admitted_seq").numeric()),dr::ID(row.get("destructible_contact_resolved_seq").numeric()),vector("destructible_contact_resolved_seqs"),vector("destructible_contact_rejected_seqs"));
        for(const auto&receipt:rejected)for(const auto&token:receipt.get("token").resolved().array){const auto&fields=token.resolved().array;if(fields.size()!=3)continue;const auto chunk=std::uint32_t(fields[0].numeric()),item=std::uint32_t(fields[1].numeric());const auto&material=fields[2].resolved();const dr::ID kind=material.kind==Value::Kind::Null?-1:dr::ID(material.numeric());owner->registry.skin.speculative.erase({chunk,item,kind});}
        if(!rejected.empty())++owner->registry.generation;return true;
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
