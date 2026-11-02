#include "pch.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_client_weapon_equipment.hpp"
#include "stock_native_client_weapon_python.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_drive_status.hpp"
#include "stock_native_battle_projectiles.hpp"
#include "stock_native_battle_params.hpp"
#include "stock_native_battle_startup.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "pyscript/script.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <set>

namespace stock_native_client_weapon_runtime {
namespace p=stock_native_bot_python;namespace shared=stock_native_client_state;namespace drive=stock_native_client_drive;namespace step=wot::offline_bot_step;
using native_battle::Battle;using native_battle::HudEdge;

// The player's gun runtime for one round: exact gun identity, descriptor metadata and the HUD edges it still
// owes the Avatar. Presentation failures leave an edge queued for its next boundary.
struct RuntimeOwner {
    std::shared_ptr<shared::State> state;
    std::shared_ptr<shared::Actor> actor;
    std::uint64_t epoch=0,generation=0,descriptor_revision=0,accepted_revision=0;
    std::int64_t engine_id=0;
    p::Ref entity,gun_object,descriptor;
    std::unique_ptr<Runtime> runtime;
    std::optional<double> next_audio;
    bool audio_stopped=false;
    std::vector<std::string> order;
    std::map<std::string,HudEdge> pending;
    bool closed=false,advancing=false,have_equipment=false;
    std::set<std::string> reported_impaired;
    bool reported=false;
    EquipmentGate equipment_gate;
    shared::Value equipment_wire;
    std::uint64_t equipment_transaction=0;
    std::string publication_error;
    RuntimeOwner(std::shared_ptr<shared::State>s,std::shared_ptr<shared::Actor>a,std::uint64_t g,PyObject*e,PyObject*gun,PyObject*d)
        :state(std::move(s)),actor(std::move(a)),epoch(state->epoch),generation(g),engine_id(actor->engine_id),entity(p::retain(e)),gun_object(p::retain(gun)),descriptor(p::retain(d)){}
};

namespace {
p::Ref field(PyObject*o,const char*n){if(!o||o==Py_None)return p::retain(Py_None);if(PyDict_Check(o))return p::value(o,n);auto key=p::text(n);auto*v=p::directAttribute(o,key.get());if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);}
void replace(p::Ref&old,p::Ref value){old.~Ref();new(&old)p::Ref(std::move(value));}
bool active(const Battle&b,const RuntimeOwner&o){
    if(o.closed||o.generation!=b.generation||o.state->retired||o.epoch!=o.state->epoch||!o.state->actors.count(o.actor->key)||o.state->actors.at(o.actor->key)!=o.actor||o.actor->tombstone||o.actor->missing||o.actor->engine_id!=o.engine_id)return false;
    auto*control=BW::ConnectionControl::pInstance();auto*entity=control?control->findEntity(static_cast<BW::EntityID>(o.engine_id)):nullptr;
    if(!entity||entity->isDestroyed()||!entity->isInWorld())return false;p::Ref python(entity->pPyEntity().newRef());return python.get()==o.entity.get();
}
Metadata metadata(Battle&b,PyObject*d,PyObject*gun_object){
    Metadata out;out.maximum_health=p::numeric(d,"maxHealth");auto chassis=field(d,"chassis"),gun=field(d,"gun"),turret=field(d,"turret"),chassis_factors=field(chassis.get(),"shotDispersionFactors"),gun_factors=field(gun.get(),"shotDispersionFactors"),loadout=p::value(gun_object,"loadout");
    const auto factors=p::numbers<2>(chassis_factors.get());out.descriptor_factors={factors[0],factors[1],p::numeric(gun_factors.get(),"turretRotation")};out.raw_dispersion=p::numeric(gun.get(),"shotDispersionAngle");if(out.raw_dispersion<=0.)throw p::Error("#1513 installed gun dispersion is unavailable");
    out.turret_speed=p::numeric(turret.get(),"rotationSpeed");out.gun_speed=p::numeric(gun.get(),"rotationSpeed");out.crew_factor=std::max(0.,p::numeric(loadout.get(),"crew_factor",1.));out.gun_rotation_factor=std::max(0.,p::numeric(loadout.get(),"gun_rotation_factor",out.crew_factor));
    auto shots=p::value(gun_object,"shots");for(Py_ssize_t i=0;i<p::size(shots.get());++i){auto shot=p::item(shots.get(),i),shell=field(shot.get(),"shell");out.shells.push_back(p::integral(shell.get(),"compactDescr"));}
    auto settings=field(b.engine.constants.get(),"VEHICLE_SETTING"),stages=field(b.engine.constants.get(),"EQUIPMENT_STAGES");out.current_shell_setting=int(p::integral(settings.get(),"CURRENT_SHELLS"));out.ready_stage=int(p::integral(stages.get(),"READY"));out.cooldown_stage=int(p::integral(stages.get(),"COOLDOWN"));out.exhausted_stage=int(p::integral(stages.get(),"EXHAUSTED"));return out;
}
// One accepted wire equipment row (contract, uses, activation) at its absolute ready time.
Equipment equipmentRow(const shared::Value&raw,double ready_at){
    Equipment e;const auto&contract=raw.get("equipment");const auto&kind=contract.get("kind").resolved().text;
    e.compact=std::int64_t(contract.get("compactDescr").numeric());e.uses_left=int(raw.get("usesLeft").numeric());e.active=raw.get("active").truth();
    e.rpm_limiter=kind=="rpm_limiter";e.fuel=kind=="fuel";e.repairkit=kind=="repairkit";e.repair_bonus=contract.get("bonusValue").numeric();e.engine_power_factor=contract.get("enginePowerFactor").numeric(1.);e.ready_at=ready_at;return e;
}
std::shared_ptr<RuntimeOwner> install(Battle&b,PyObject*entity){
    auto state=b.vehicles;if(!state||state->retired)throw p::Error("Native player weapon state is unavailable");
    std::shared_ptr<shared::Actor> actor;for(const auto&row:state->actors)if(row.second->local){actor=row.second;break;}if(!actor)throw p::Error("Native local weapon actor is unavailable");
    if(!b.gun->gun)throw p::Error("Native player gun must be installed before its runtime");auto*gun=stock_native_client_weapon::gun(b.gun->gun.get());
    auto*shell=b.engine.shell.get();p::Ref actual(entity&&entity!=Py_None?p::retain(entity):p::Ref(stock_native_bot::readNativeEntity(shell,actor->engine_id)));
    if(actual.get()==Py_None)throw p::Error("Native player weapon entity is unavailable");auto descriptor=p::Ref(stock_native_bot::readNativeEntityField(shell,actual.get(),"typeDescriptor"));
    auto o=std::make_shared<RuntimeOwner>(state,actor,b.generation,actual.get(),b.gun->gun.get(),descriptor.get());
    o->descriptor_revision=actor->descriptor_revision;o->runtime=std::make_unique<Runtime>(*gun,metadata(b,descriptor.get(),b.gun->gun.get()));
    o->runtime->lastTick(b.gun->last_tick);o->runtime->seedReload(b.gun->reload_event);o->runtime->seedTargeting(b.gun->targeting_signature);
    b.gun->runtime=o;return o;
}
std::shared_ptr<RuntimeOwner> current(Battle&b){auto&o=b.gun->runtime;if(!o||!active(b,*o))throw p::Error("Native weapon runtime is retired");return o;}
std::shared_ptr<RuntimeOwner> ensure(Battle&b,PyObject*entity=nullptr){
    auto&o=b.gun->runtime;if(!o||!active(b,*o)||o->gun_object.get()!=b.gun->gun.get()){if(o)o->closed=true;return install(b,entity);}return o;
}
void refreshOwner(Battle&b,RuntimeOwner&o,double now){
    drive::synchronizeStatus(b,now);if(!active(b,o))throw p::Error("Native player weapon retired during status synchronization");
    auto*d=stock_native_client_query::descriptor(*o.actor);if(!d)throw p::Error("Native player weapon descriptor resources are not installed");
    if(o.descriptor.get()!=d||o.descriptor_revision!=o.actor->descriptor_revision){auto value=metadata(b,d,o.gun_object.get());o.runtime->metadata(std::move(value));replace(o.descriptor,p::retain(d));o.descriptor_revision=o.actor->descriptor_revision;}
    if(o.accepted_revision==o.state->revision&&o.have_equipment)return;
    const auto&rows=o.actor->state.get("equipment_states").resolved();
    if(o.have_equipment&&o.equipment_gate.valid&&rows==o.equipment_wire){o.accepted_revision=o.state->revision;return;}
    if(rows.kind!=shared::Value::Kind::Null&&rows.kind!=shared::Value::Kind::Array)throw p::Error("Native player equipment ledger is invalid");
    std::vector<Equipment> equipment;for(const auto&raw:rows.array)equipment.push_back(equipmentRow(raw,o.state->received_at+raw.get("cooldownTimeLeft").numeric()));
    o.runtime->equipment(std::move(equipment));o.have_equipment=true;o.accepted_revision=o.state->revision;
}
Factors factors(const RuntimeOwner&o){
    const auto status=drive::status(*o.actor);Factors f;f.reload=status->factor(step::CriticalStat::reload);f.dispersion=status->factor(step::CriticalStat::dispersion);f.aim_time=status->factor(step::CriticalStat::aim_time);f.turret_speed=status->factor(step::CriticalStat::turret_speed);f.gun_stun=status->stunFactor("turret_speed");f.bloom_move=status->stunFactor("bloom_move");f.bloom_rotation=status->stunFactor("bloom_rotation");f.bloom_turret=status->stunFactor("bloom_turret");
    const auto modifiers=conditional(o.actor->state.get("effective_params"),status->casualties(),status->critical().fire,
        o.actor->state.get("health").numeric(),o.runtime->metadata().maximum_health,status->critical());
    f.reload*=modifiers.reload;f.dispersion*=modifiers.dispersion;
    return f;
}
void reportCrew(RuntimeOwner&o,const Factors&f){
    const auto status=drive::status(*o.actor);const auto&impaired=status->critical().crew_ko;
    if(o.reported&&o.reported_impaired==impaired)return;o.reported=true;o.reported_impaired=impaired;
    std::string names;for(const auto&role:impaired){if(!names.empty())names+=",";names+=role;}if(names.empty())names="-";
    char line[256];snprintf(line,sizeof(line),"CREW out=%s reload=%.3f aim=%.3f disp=%.3f turret=%.3f mobility=%.3f vision=%.3f",names.c_str(),f.reload,f.aim_time,f.dispersion,f.turret_speed,status->factor(step::CriticalStat::mobility),status->factor(step::CriticalStat::vision));
    native_battle::log(line);
}
void queue(RuntimeOwner&o,HudEdge edge){if(!o.pending.count(edge.id))o.order.push_back(edge.id);auto key=edge.id;o.pending[key]=std::move(edge);}
void queue(Battle&b,RuntimeOwner&o,const Edges&output,double now){
    const auto&g=o.runtime->gun();
    for(const auto&ammo:output.ammunition){HudEdge e{HudEdge::Kind::Ammo};e.id="ammo:"+std::to_string(ammo.compact);e.compact=ammo.compact;e.quantity=ammo.quantity;e.clip=ammo.clip;queue(o,std::move(e));}
    if(output.current_shell){HudEdge e{HudEdge::Kind::Shell};e.id="shell";e.setting=o.runtime->metadata().current_shell_setting;e.compact=*output.current_shell;queue(o,std::move(e));}
    for(const auto&eq:output.equipment){HudEdge e{HudEdge::Kind::Equipment};e.id="equipment:"+std::to_string(eq.compact);e.compact=eq.compact;e.quantity=eq.quantity;e.stage=eq.stage;e.remaining_seconds=eq.remaining;queue(o,std::move(e));}
    if(output.reload){o.audio_stopped=false;HudEdge e{HudEdge::Kind::Reload};e.id="reload";e.remaining=output.reload->remaining;e.duration=output.reload->duration;e.reason=output.reload->reason;e.now=now;e.live=b.live;
        e.gun_reload_time=output.reload->remaining;e.gun_reload_duration=output.reload->duration;e.gun_clip=g.clip;e.gun_shot=g.shot;e.gun_ammo.assign(g.ammunition.begin(),g.ammunition.end());queue(o,std::move(e));}
    if(output.targeting){HudEdge e{HudEdge::Kind::Targeting};e.id="targeting";e.targeting=*output.targeting;queue(o,std::move(e));}
}
void cacheAudio(Battle&b,RuntimeOwner&o){
    o.next_audio.reset();if(!b.gun->reload_sound)return;o.next_audio=native_battle::nextReloadCue(*b.gun->reload_sound);
}
void emit(Battle&b,RuntimeOwner&o){
    if(o.pending.empty()||!active(b,o))return;std::vector<HudEdge> batch;
    for(const auto&key:o.order){const auto found=o.pending.find(key);if(found!=o.pending.end())batch.push_back(found->second);}
    const auto done=native_battle::gun::publish(b,o.entity.get(),batch);if(!active(b,o))return;
    const std::set<std::string> accepted(done.begin(),done.end());
    for(const auto&row:batch)if(accepted.count(row.id))o.pending.erase(row.id);
    o.order.erase(std::remove_if(o.order.begin(),o.order.end(),[&](const std::string&key){return !o.pending.count(key);}),o.order.end());
    if(std::any_of(batch.begin(),batch.end(),[](const HudEdge&e){return e.kind==HudEdge::Kind::Reload;}))cacheAudio(b,o);
}
void emitSafe(Battle&b,RuntimeOwner&o){try{emit(b,o);o.publication_error.clear();}catch(const std::exception&error){
    // Presentation failure never unwinds an already advanced gun or admitted input; the edge stays queued.
    const auto detail=native_battle::describe(error);
    if(detail!=o.publication_error){o.publication_error=detail;native_battle::log("Native player HUD publication pending: "+detail);}
}}
struct Guard {RuntimeOwner&o;explicit Guard(RuntimeOwner&value):o(value){o.advancing=true;}~Guard(){o.advancing=false;}};
Gun*advanceWithMotion(Battle&b,double now,std::optional<std::array<double,2>>frozen){
    auto o=ensure(b);if(o->advancing)return &o->runtime->gun();Guard guard(*o);refreshOwner(b,*o,now);const auto f=factors(*o);
    // Startup owns readiness after the stock own-vehicle visual clears the consumables controllers.
    if(!b.replay){const auto driver=drive::driverAccess(b);if(!driver.driver)throw p::Error("Native local gun has no current player driver");const auto&motion=driver.driver->state();
        const auto output=o->runtime->advance(now,b.live,frozen?(*frozen)[0]:motion.speed,frozen?(*frozen)[1]:motion.turn_speed,f,(b.startup && b.startup->client_ready_received));
        b.gun->last_tick=now;reportCrew(*o,f);if(!active(b,*o))throw p::Error("Native gun retired during crew diagnostics");queue(b,*o,output,now);emitSafe(b,*o);}
    if(!active(b,*o))throw p::Error("Native player gun retired during presentation");return &o->runtime->gun();
}
}

Gun*advanceWeaponMotion(Battle&b,double now,double speed,double turn){return advanceWithMotion(b,now,std::array<double,2>{speed,turn});}
Gun*advanceWeapon(Battle&b,double now){return advanceWithMotion(b,now,{});}
Gun*bindWeapon(Battle&b,PyObject*entity){return &ensure(b,entity)->runtime->gun();}
double activeRepairBonus(Battle&b){return ensure(b)->runtime->repairBonus();}
double activeEnginePowerFactor(Battle&b){return ensure(b)->runtime->enginePowerFactor();}
double reloadFactor(Battle&b){auto o=ensure(b);return factors(*o).reload;}
Checkpoint checkpoint(Battle&b,double now){advanceWeapon(b,now);return current(b)->runtime->checkpoint();}
Checkpoint currentCheckpoint(Battle&b){return current(b)->runtime->checkpoint();}
int livingSkillCount(Battle&b,const std::string&skill){auto o=ensure(b);return livingSkillCount(o->actor->state.get("effective_params"),skill,drive::status(*o->actor)->casualties());}
void refreshWeapon(Battle&b,double now){auto o=ensure(b);refreshOwner(b,*o,now);}
void closeWeapon(Battle&b){if(b.gun&&b.gun->runtime)b.gun->runtime->closed=true;}

// The live reload sound pump: dispatches a stock audio cue only when its cached deadline is due.
bool audioFrame(Battle&b,double now){
    auto&o=b.gun->runtime;if(!o||!active(b,*o)||!b.gun->reload_sound)return false;
    const bool stop=!b.live||o->actor->state.get("health").numeric()<=0.;
    if(stop){if(!o->audio_stopped){native_battle::stopReloadSound(*b.gun->reload_sound);o->audio_stopped=true;o->next_audio.reset();}return false;}
    if(!audioDue(o->next_audio,now))return false;
    native_battle::pumpReloadSound(*b.gun->reload_sound,now);if(active(b,*o))cacheAudio(b,*o);return true;
}
// Accepted equipment: identical contracts only move their relative clocks; a contract, quantity or activation
// result change goes through the cold equipment restore.
bool equipmentSnapshot(Battle&b,const shared::Value&snapshot,bool present){
    auto o=ensure(b);const auto transaction=++o->equipment_transaction;const auto player=b.local_player;
    const shared::Value*local=nullptr;for(const auto&row:snapshot.get("players").resolved().array){const auto&r=row.resolved();if(r.kind==shared::Value::Kind::Object&&std::int64_t(r.get("id").numeric(-1.))==player){local=&r;break;}}
    if(!local||!local->object.count("equipment_states"))return false;
    const auto&revision_value=local->get("equipment_revision").resolved();if(revision_value.kind!=shared::Value::Kind::Integer&&revision_value.kind!=shared::Value::Kind::Number)throw p::Error("canonical player equipment snapshot is invalid");
    const auto revision=std::int64_t(revision_value.numeric());if(revision<0||revision<b.gun->equipment_revision)throw p::Error("canonical player equipment snapshot is invalid");
    const double now=b.clock();const auto&wire=local->get("equipment_states");const auto frame=equipmentFrame(wire,now);const auto&result=local->get("equipment_intent_result");
    if(!o->equipment_gate.matches(frame,revision,result)){
        const bool accepted=native_battle::params::restoreEquipmentCold(b,snapshot,present);
        if(active(b,*o)&&transaction==o->equipment_transaction&&accepted){o->equipment_gate.commit(frame,revision,result);o->equipment_wire=wire;b.gun->equipment_revision=int(revision);}
        return accepted;
    }
    std::vector<Equipment> equipment;const auto&rows=wire.resolved().array;for(std::size_t i=0;i<rows.size();++i)equipment.push_back(equipmentRow(rows[i],frame.clocks[i].ready_at));
    o->runtime->equipment(std::move(equipment));o->accepted_revision=o->state->revision;o->have_equipment=true;o->equipment_wire=wire;b.gun->equipment_revision=int(revision);
    if(present&&(b.startup && b.startup->client_ready_received)&&b.engine.avatar&&b.engine.server){Edges output;o->runtime->equipment(output,now,true);queue(b,*o,output,now);if(!o->advancing){Guard guard(*o);emitSafe(b,*o);}}
    return true;
}
// The rows stay current until the accepted wire changes again; actor state may still hold the previous ledger.
void equipmentIngress(Battle&b,std::vector<Equipment> rows){auto o=ensure(b);o->runtime->equipment(std::move(rows));o->have_equipment=true;o->accepted_revision=o->state->revision;}
// One HUD publication request; true when it produced an edge.
bool publish(Battle&b,const std::string&action,double now,bool force,double remaining,double duration){
    auto o=ensure(b);refreshOwner(b,*o,now);Edges output;const bool ready=(b.startup && b.startup->client_ready_received);
    if(action=="ammo")o->runtime->ammo(output,now,ready,force);else if(action=="equipment")o->runtime->equipment(output,now,ready);else if(action=="reload")o->runtime->reload(output,remaining,duration,force);else if(action=="targeting")o->runtime->targeting(output,factors(*o));else throw p::Error("Unknown native player publication edge");
    const bool changed=!output.ammunition.empty()||output.current_shell||!output.equipment.empty()||output.reload||output.targeting;queue(b,*o,output,now);if(!o->advancing){Guard guard(*o);emitSafe(b,*o);}return changed;
}

// Native modules of other subsystems still pass the battle shell; they reach the current round's gun.
Gun*advanceWeaponMotion(PyObject*,double now,double speed,double turn){return advanceWeaponMotion(native_battle::require(),now,speed,turn);}
Gun*advanceWeapon(PyObject*,double now){return advanceWeapon(native_battle::require(),now);}
double activeRepairBonus(PyObject*){return activeRepairBonus(native_battle::require());}
double activeEnginePowerFactor(PyObject*){return activeEnginePowerFactor(native_battle::require());}
double reloadFactor(PyObject*){return reloadFactor(native_battle::require());}
Checkpoint checkpoint(PyObject*,double now){return checkpoint(native_battle::require(),now);}
Checkpoint currentCheckpoint(PyObject*){return currentCheckpoint(native_battle::require());}
int livingSkillCount(PyObject*,const std::string&skill){return livingSkillCount(native_battle::require(),skill);}
}
