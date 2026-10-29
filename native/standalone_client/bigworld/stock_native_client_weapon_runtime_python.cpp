#include "pch.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_client_weapon_equipment.hpp"
#include "stock_native_client_weapon_python.hpp"
#include "stock_native_client_state.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_drive_status.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "pyscript/script.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include <algorithm>
#include <memory>
#include <new>
#include <set>

namespace stock_native_client_weapon_runtime {
namespace p=stock_native_bot_python;namespace shared=stock_native_client_state;namespace drive=stock_native_client_drive;namespace step=wot::offline_bot_step;
namespace {
constexpr const char*capsule_name="offline.client.weapon.runtime";
p::Ref field(PyObject*o,const char*n){if(!o||o==Py_None)return p::retain(Py_None);if(PyDict_Check(o))return p::value(o,n);auto key=p::text(n);auto*v=p::directAttribute(o,key.get());if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(v);}
void attribute(PyObject*o,const char*n,const p::Ref&v){auto key=p::text(n);if(PyObject_GenericSetAttr(o,key.get(),v.get())<0)throw p::Error("Native weapon runtime mirror assignment failed");}
std::optional<double> optional(PyObject*o,const char*n){auto v=field(o,n);return v.get()==Py_None?std::nullopt:std::optional<double>(p::scalar(v.get()));}
void replace(p::Ref&old,p::Ref value){old.~Ref();new(&old)p::Ref(std::move(value));}
struct Owner {
    std::shared_ptr<shared::State> state;
    std::shared_ptr<shared::Actor> actor;
    std::uint64_t epoch=0,descriptor_revision=0,accepted_revision=0;
    std::int64_t engine_id=0;
    p::Ref battle,entity,gun_object,descriptor,publisher;
    std::unique_ptr<Runtime> runtime;
    std::unique_ptr<p::Ref> audio;
    std::optional<double> next_audio;
    bool audio_stopped=false;
    std::vector<std::string> order;
    std::map<std::string,std::unique_ptr<p::Ref>> pending;
    bool closed=false,advancing=false,have_equipment=false;
    std::set<std::string> reported_impaired;
    bool reported=false;
    EquipmentGate equipment_gate;
    shared::Value equipment_wire;
    std::uint64_t equipment_transaction=0;
    std::string publication_error;
    Owner(std::shared_ptr<shared::State>s,std::shared_ptr<shared::Actor>a,PyObject*b,PyObject*e,PyObject*g,PyObject*d,PyObject*pub):state(std::move(s)),actor(std::move(a)),epoch(state->epoch),engine_id(actor->engine_id),battle(p::retain(b)),entity(p::retain(e)),gun_object(p::retain(g)),descriptor(p::retain(d)),publisher(p::retain(pub)){}
};
struct Holder {std::shared_ptr<Owner> owner;};
bool active(const Owner&o){
    if(o.closed||o.state->retired||o.epoch!=o.state->epoch||!o.state->actors.count(o.actor->key)||o.state->actors.at(o.actor->key)!=o.actor||o.actor->tombstone||o.actor->missing||o.actor->engine_id!=o.engine_id)return false;
    auto*control=BW::ConnectionControl::pInstance();auto*entity=control?control->findEntity(static_cast<BW::EntityID>(o.engine_id)):nullptr;
    if(!entity||entity->isDestroyed()||!entity->isInWorld())return false;p::Ref python(entity->pPyEntity().newRef());return python.get()==o.entity.get();
}
void destroy(PyObject*c){auto*h=static_cast<Holder*>(PyCapsule_GetPointer(c,capsule_name));if(h)delete h;else PyErr_Clear();}

Metadata metadata(PyObject*battle,PyObject*d,PyObject*gun_object){
    Metadata out;out.maximum_health=p::numeric(d,"maxHealth");auto chassis=field(d,"chassis"),gun=field(d,"gun"),turret=field(d,"turret"),chassis_factors=field(chassis.get(),"shotDispersionFactors"),gun_factors=field(gun.get(),"shotDispersionFactors"),loadout=p::value(gun_object,"loadout");
    const auto factors=p::numbers<2>(chassis_factors.get());out.descriptor_factors={factors[0],factors[1],p::numeric(gun_factors.get(),"turretRotation")};out.raw_dispersion=p::numeric(gun.get(),"shotDispersionAngle");if(out.raw_dispersion<=0.)throw p::Error("#1513 installed gun dispersion is unavailable");
    out.turret_speed=p::numeric(turret.get(),"rotationSpeed");out.gun_speed=p::numeric(gun.get(),"rotationSpeed");out.crew_factor=std::max(0.,p::numeric(loadout.get(),"crew_factor",1.));out.gun_rotation_factor=std::max(0.,p::numeric(loadout.get(),"gun_rotation_factor",out.crew_factor));
    auto shots=p::value(gun_object,"shots");for(Py_ssize_t i=0;i<p::size(shots.get());++i){auto shot=p::item(shots.get(),i),shell=field(shot.get(),"shell");out.shells.push_back(p::integral(shell.get(),"compactDescr"));}
    auto runtime=field(battle,"_runtime"),constants=field(runtime.get(),"constants"),settings=field(constants.get(),"VEHICLE_SETTING"),stages=field(constants.get(),"EQUIPMENT_STAGES");out.current_shell_setting=int(p::integral(settings.get(),"CURRENT_SHELLS"));out.ready_stage=int(p::integral(stages.get(),"READY"));out.cooldown_stage=int(p::integral(stages.get(),"COOLDOWN"));out.exhausted_stage=int(p::integral(stages.get(),"EXHAUSTED"));return out;
}
std::vector<int> integers(PyObject*raw){std::vector<int> result;for(Py_ssize_t i=0;i<p::size(raw);++i)result.push_back(int(p::wide(p::item(raw,i).get())));return result;}
std::vector<Equipment> equipmentObjects(PyObject*states){std::vector<Equipment> out;if(states==Py_None)return out;
    for(Py_ssize_t i=0;i<p::size(states);++i){auto raw=p::item(states,i),contract=field(raw.get(),"contract");Equipment e;e.compact=p::integral(contract.get(),"compactDescr");e.uses_left=int(p::integral(raw.get(),"uses_left"));e.active=p::truth(raw.get(),"active");const auto kind=p::string(field(contract.get(),"kind").get());e.rpm_limiter=kind=="rpm_limiter";e.fuel=kind=="fuel";e.repairkit=kind=="repairkit";e.repair_bonus=p::numeric(contract.get(),"bonusValue");e.engine_power_factor=p::numeric(contract.get(),"enginePowerFactor",1.);e.ready_at=p::numeric(raw.get(),"ready_at");out.push_back(e);}return out;
}
void seed(Owner&o){
    o.runtime->lastTick(optional(o.battle.get(),"_gun_last_tick"));auto reload=field(o.battle.get(),"_reload_event");if(reload.get()!=Py_None){const auto values=p::numbers<2>(reload.get());o.runtime->seedReload(std::make_pair(values[0],values[1]));}
    auto targeting=field(o.battle.get(),"_targeting_signature");if(targeting.get()!=Py_None)o.runtime->seedTargeting(p::numbers<7>(targeting.get()));
    auto ammo=field(o.battle.get(),"_ammo_signature");if(ammo.get()!=Py_None&&p::size(ammo.get())==3){auto shot=p::item(ammo.get(),0),counts=p::item(ammo.get(),1),clip=p::item(ammo.get(),2);o.runtime->seedAmmo(int(p::wide(shot.get())),int(p::wide(clip.get())),integers(counts.get()));}
    auto raw=field(o.battle.get(),"_equipment_signature");std::map<std::int64_t,std::array<int,3>> equipment;if(raw.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(raw.get());++i){auto row=p::item(raw.get(),i),compact=p::item(row.get(),0),values=p::item(row.get(),1);auto quantity=p::item(values.get(),0),stage=p::item(values.get(),1),indefinite=p::item(values.get(),2);equipment[p::wide(compact.get())]={int(p::wide(quantity.get())),int(p::wide(stage.get())),p::flag(indefinite.get())};}o.runtime->seedEquipment(std::move(equipment));
    auto states=field(o.battle.get(),"_equipment_state");if(states.get()!=Py_None){o.runtime->equipment(equipmentObjects(states.get()));o.accepted_revision=o.state->revision;o.have_equipment=true;}
}
std::shared_ptr<Owner> current(PyObject*battle){
    auto capsule=field(battle,"_native_client_weapon_runtime");if(capsule.get()==Py_None)throw p::Error("Native weapon runtime is not bound");auto*h=static_cast<Holder*>(PyCapsule_GetPointer(capsule.get(),capsule_name));if(!h||!h->owner||!active(*h->owner))throw p::Error("Native weapon runtime is retired");return h->owner;
}
std::shared_ptr<Owner> install(PyObject*battle,PyObject*entity){
    auto state=shared::fromBattle(battle);if(!state||state->retired)throw p::Error("Native player weapon state is unavailable");
    std::shared_ptr<shared::Actor> actor;for(const auto&row:state->actors)if(row.second->local){actor=row.second;break;}if(!actor)throw p::Error("Native local weapon actor is unavailable");
    auto gun_object=field(battle,"_gun_state");if(gun_object.get()==Py_None)throw p::Error("Native player gun must be installed before its runtime");auto*gun=stock_native_client_weapon::gun(gun_object.get());
    p::Ref actual(entity&&entity!=Py_None?p::retain(entity):p::Ref(stock_native_bot::readNativeEntity(battle,actor->engine_id)));
    if(actual.get()==Py_None)throw p::Error("Native player weapon entity is unavailable");auto descriptor=p::Ref(stock_native_bot::readNativeEntityField(battle,actual.get(),"typeDescriptor"));
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.native_client_weapon_runtime"));auto publisher=field(module.get(),"_publish");if(!PyCallable_Check(publisher.get()))throw p::Error("Native weapon presentation boundary is unavailable");auto o=std::make_shared<Owner>(state,actor,battle,actual.get(),gun_object.get(),descriptor.get(),publisher.get());
    o->descriptor_revision=actor->descriptor_revision;o->runtime=std::make_unique<Runtime>(*gun,metadata(battle,descriptor.get(),gun_object.get()));seed(*o);
    auto h=std::make_unique<Holder>();h->owner=o;p::Ref capsule(PyCapsule_New(h.get(),capsule_name,destroy));h.release();attribute(battle,"_native_client_weapon_runtime",capsule);return o;
}
std::shared_ptr<Owner> ensure(PyObject*battle,PyObject*entity=nullptr){
    auto capsule=field(battle,"_native_client_weapon_runtime"),gun=field(battle,"_gun_state");if(capsule.get()==Py_None)return install(battle,entity);
    auto*h=static_cast<Holder*>(PyCapsule_GetPointer(capsule.get(),capsule_name));if(!h)throw p::Error("Native player weapon owner is invalid");
    if(!h->owner||!active(*h->owner)||h->owner->gun_object.get()!=gun.get()){if(h->owner)h->owner->closed=true;return install(battle,entity);}return h->owner;
}
void refreshOwner(Owner&o,double now){
    drive::synchronizeStatus(o.battle.get(),now);if(!active(o))throw p::Error("Native player weapon retired during status synchronization");
    auto*d=stock_native_client_query::descriptor(*o.actor);if(!d)throw p::Error("Native player weapon descriptor resources are not installed");
    if(o.descriptor.get()!=d||o.descriptor_revision!=o.actor->descriptor_revision){auto value=metadata(o.battle.get(),d,o.gun_object.get());o.runtime->metadata(std::move(value));replace(o.descriptor,p::retain(d));o.descriptor_revision=o.actor->descriptor_revision;}
    if(o.accepted_revision==o.state->revision&&o.have_equipment)return;
    const auto&rows=o.actor->state.get("equipment_states").resolved();
    if(o.have_equipment&&o.equipment_gate.valid&&rows==o.equipment_wire){o.accepted_revision=o.state->revision;return;}
    if(rows.kind!=shared::Value::Kind::Null&&rows.kind!=shared::Value::Kind::Array)throw p::Error("Native player equipment ledger is invalid");
    std::vector<Equipment> equipment;for(const auto&raw:rows.array){Equipment e;const auto&contract=raw.get("equipment");e.compact=std::int64_t(contract.get("compactDescr").numeric());e.uses_left=int(raw.get("usesLeft").numeric());e.active=raw.get("active").truth();e.rpm_limiter=contract.get("kind").resolved().text=="rpm_limiter";e.fuel=contract.get("kind").resolved().text=="fuel";e.repairkit=contract.get("kind").resolved().text=="repairkit";e.repair_bonus=contract.get("bonusValue").numeric();e.engine_power_factor=contract.get("enginePowerFactor").numeric(1.);e.ready_at=o.state->received_at+raw.get("cooldownTimeLeft").numeric();equipment.push_back(e);}
    o.runtime->equipment(std::move(equipment));o.have_equipment=true;o.accepted_revision=o.state->revision;
}
Factors factors(const Owner&o){
    const auto status=drive::status(*o.actor);Factors f;f.reload=status->factor(step::CriticalStat::reload);f.dispersion=status->factor(step::CriticalStat::dispersion);f.aim_time=status->factor(step::CriticalStat::aim_time);f.turret_speed=status->factor(step::CriticalStat::turret_speed);f.gun_stun=status->stunFactor("turret_speed");f.bloom_move=status->stunFactor("bloom_move");f.bloom_rotation=status->stunFactor("bloom_rotation");f.bloom_turret=status->stunFactor("bloom_turret");
    const auto modifiers=conditional(o.actor->state.get("effective_params"),status->casualties(),status->critical().fire,
        o.actor->state.get("health").numeric(),o.runtime->metadata().maximum_health,status->critical());
    f.reload*=modifiers.reload;f.dispersion*=modifiers.dispersion;
    return f;
}
void reportCrew(Owner&o,const Factors&f){
    const auto status=drive::status(*o.actor);const auto&impaired=status->critical().crew_ko;
    if(o.reported&&o.reported_impaired==impaired)return;o.reported=true;o.reported_impaired=impaired;
    std::string names;for(const auto&role:impaired){if(!names.empty())names+=",";names+=role;}if(names.empty())names="-";
    PySys_WriteStdout("[Offline LAN 0.9.22] CREW out=%s reload=%.3f aim=%.3f disp=%.3f turret=%.3f mobility=%.3f vision=%.3f\n",names.c_str(),f.reload,f.aim_time,f.dispersion,f.turret_speed,status->factor(step::CriticalStat::mobility),status->factor(step::CriticalStat::vision));
    if(PyErr_Occurred())PyErr_Clear();
}
void queue(Owner&o,const std::string&key,p::Ref value){p::set(value.get(),"id",p::text(key));if(!o.pending.count(key))o.order.push_back(key);o.pending[key]=std::make_unique<p::Ref>(std::move(value));}
p::Ref edge(const char*kind){auto row=p::dict();p::set(row.get(),"kind",p::text(kind));return row;}
void queue(Owner&o,const Edges&output,double now){
    const auto&g=o.runtime->gun();
    for(const auto&ammo:output.ammunition){auto row=edge("ammo");p::set(row.get(),"compact",p::integer(ammo.compact));p::set(row.get(),"quantity",p::integer(ammo.quantity));p::set(row.get(),"clip",p::integer(ammo.clip));queue(o,"ammo:"+std::to_string(ammo.compact),std::move(row));}
    if(output.current_shell){auto row=edge("shell");p::set(row.get(),"setting",p::integer(o.runtime->metadata().current_shell_setting));p::set(row.get(),"compact",p::integer(*output.current_shell));queue(o,"shell",std::move(row));}
    for(const auto&e:output.equipment){auto row=edge("equipment");p::set(row.get(),"compact",p::integer(e.compact));p::set(row.get(),"quantity",p::integer(e.quantity));p::set(row.get(),"stage",p::integer(e.stage));p::set(row.get(),"remaining",p::integer(e.remaining));queue(o,"equipment:"+std::to_string(e.compact),std::move(row));}
    if(output.reload){o.audio_stopped=false;auto row=edge("reload"),audio=p::dict();p::set(row.get(),"remaining",p::number(output.reload->remaining));p::set(row.get(),"duration",p::number(output.reload->duration));p::set(row.get(),"reason",p::text(output.reload->reason));p::set(row.get(),"now",p::number(now));p::set(row.get(),"live",p::boolean(p::truth(o.battle.get(),"_battle_live")));p::set(audio.get(),"reload_time",p::number(output.reload->remaining));p::set(audio.get(),"reload_duration",p::number(output.reload->duration));p::set(audio.get(),"clip",p::integer(g.clip));p::set(audio.get(),"shot_index",p::integer(g.shot));p::set(audio.get(),"ammo",p::integers(g.ammunition));p::set(row.get(),"gun",audio);queue(o,"reload",std::move(row));}
    if(output.targeting){auto row=edge("targeting");p::set(row.get(),"signature",p::tupleNumbers(*output.targeting));queue(o,"targeting",std::move(row));}
}
void cacheAudio(Owner&o){
    auto current=field(o.battle.get(),"_live_reload_sound");if(current.get()==Py_None){o.audio.reset();o.next_audio.reset();return;}
    if(!o.audio||o.audio->get()!=current.get()){o.audio=std::make_unique<p::Ref>(p::retain(current.get()));o.audio_stopped=false;}
    auto rows=field(current.get(),"queue");o.next_audio.reset();
    if(rows.get()!=Py_None&&p::size(rows.get())>0){auto row=p::item(rows.get(),0),time=p::item(row.get(),0);o.next_audio=p::scalar(time.get());}
}
void emit(Owner&o){
    if(o.pending.empty()||!active(o))return;auto batch=p::list();std::vector<std::pair<std::string,PyObject*>> sent;
    for(const auto&key:o.order){const auto found=o.pending.find(key);if(found!=o.pending.end()){p::append(batch.get(),*found->second);sent.emplace_back(key,found->second->get());}}
    p::Ref args(PyTuple_Pack(3,o.battle.get(),o.entity.get(),batch.get()));p::Ref accepted(PyObject_CallObject(o.publisher.get(),args.get()));if(!active(o))return;
    std::set<std::string> done;for(Py_ssize_t i=0;i<p::size(accepted.get());++i)done.insert(p::string(p::item(accepted.get(),i).get()));
    for(const auto&row:sent){const auto found=o.pending.find(row.first);if(found!=o.pending.end()&&found->second->get()==row.second&&done.count(row.first))o.pending.erase(found);}
    o.order.erase(std::remove_if(o.order.begin(),o.order.end(),[&](const std::string&key){return !o.pending.count(key);}),o.order.end());
    if(std::any_of(sent.begin(),sent.end(),[](const std::pair<std::string,PyObject*>&item){return item.first=="reload";}))cacheAudio(o);
}
void emitSafe(Owner&o){try{emit(o);o.publication_error.clear();}catch(const std::exception&error){
    // Presentation failure never unwinds an already advanced gun or admitted
    // input. The unacknowledged edge remains queued for its next boundary.
    std::string detail=error.what();PyObject*type=nullptr,*value=nullptr,*trace=nullptr;PyErr_Fetch(&type,&value,&trace);
    if(value){auto*text=PyObject_Str(value);if(text){try{detail+=": "+p::string(text);}catch(const std::exception&){PyErr_Clear();}Py_DECREF(text);}else PyErr_Clear();}
    Py_XDECREF(type);Py_XDECREF(value);Py_XDECREF(trace);PyErr_Clear();
    if(detail!=o.publication_error){o.publication_error=detail;PySys_WriteStdout("[Offline LAN 0.9.22] Native player HUD publication pending: %s\n",detail.c_str());if(PyErr_Occurred())PyErr_Clear();}
}}
struct Guard {Owner&o;explicit Guard(Owner&value):o(value){o.advancing=true;}~Guard(){o.advancing=false;}};
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
namespace {
Gun*advanceWithMotion(PyObject*battle,double now,std::optional<std::array<double,2>>frozen){
    auto o=ensure(battle);if(o->advancing)return &o->runtime->gun();Guard guard(*o);refreshOwner(*o,now);const auto f=factors(*o);
    const bool replay=p::truth(battle,"_replay_mode");if(!replay){const auto driver=drive::driverAccess(battle);if(!driver.driver)throw p::Error("Native local gun has no current player driver");const auto&motion=driver.driver->state();const auto output=o->runtime->advance(now,p::truth(battle,"_battle_live"),frozen?(*frozen)[0]:motion.speed,frozen?(*frozen)[1]:motion.turn_speed,f,p::truth(battle,"_client_ready_received"));attribute(battle,"_gun_last_tick",p::number(now));reportCrew(*o,f);if(!active(*o))throw p::Error("Native gun retired during crew diagnostics");queue(*o,output,now);emitSafe(*o);}
    if(!active(*o))throw p::Error("Native player gun retired during presentation");return &o->runtime->gun();
}
}
Gun*advanceWeaponMotion(PyObject*battle,double now,double speed,double turn){return advanceWithMotion(battle,now,std::array<double,2>{speed,turn});}
Gun*advanceWeapon(PyObject*battle,double now){return advanceWithMotion(battle,now,{});}
double activeRepairBonus(PyObject*battle){return ensure(battle)->runtime->repairBonus();}
double activeEnginePowerFactor(PyObject*battle){return ensure(battle)->runtime->enginePowerFactor();}
double reloadFactor(PyObject*battle){auto o=ensure(battle);return factors(*o).reload;}
Checkpoint checkpoint(PyObject*battle,double now){advanceWeapon(battle,now);return current(battle)->runtime->checkpoint();}
Checkpoint currentCheckpoint(PyObject*battle){return current(battle)->runtime->checkpoint();}
int livingSkillCount(PyObject*battle,const std::string&skill){auto o=ensure(battle);return livingSkillCount(o->actor->state.get("effective_params"),skill,drive::status(*o->actor)->casualties());}
PyObject*bind(PyObject*,PyObject*args){PyObject*battle,*entity=Py_None;if(!PyArg_ParseTuple(args,"O|O",&battle,&entity))return nullptr;try{auto o=ensure(battle,entity);return p::retain(o->gun_object.get()).release();}catch(const std::exception&e){return fail(e);}}
PyObject*refresh(PyObject*,PyObject*args){PyObject*battle;double now;if(!PyArg_ParseTuple(args,"Od",&battle,&now))return nullptr;try{auto o=ensure(battle);refreshOwner(*o,now);Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject*advance(PyObject*,PyObject*args){PyObject*battle,*entity;double now;if(!PyArg_ParseTuple(args,"OOd",&battle,&entity,&now))return nullptr;try{ensure(battle,entity);advanceWeapon(battle,now);return p::retain(current(battle)->gun_object.get()).release();}catch(const std::exception&e){return fail(e);}}
PyObject*audioFrame(PyObject*,PyObject*args){PyObject*battle;double now;if(!PyArg_ParseTuple(args,"Od",&battle,&now))return nullptr;try{
    auto capsule=field(battle,"_native_client_weapon_runtime");if(capsule.get()==Py_None)return p::boolean(false).release();auto o=current(battle);if(!o->audio)return p::boolean(false).release();
    const bool stop=!p::truth(battle,"_battle_live")||o->actor->state.get("health").numeric()<=0.;
    if(stop){if(!o->audio_stopped){auto method=field(o->audio->get(),"stop");p::Ref result(PyObject_CallObject(method.get(),nullptr));if(!active(*o))return p::boolean(false).release();o->audio_stopped=true;o->next_audio.reset();}return p::boolean(false).release();}
    if(!audioDue(o->next_audio,now))return p::boolean(false).release();
    auto time=p::number(now),method=field(o->audio->get(),"pump");p::Ref call_args(PyTuple_Pack(1,time.get()));p::Ref result(PyObject_CallObject(method.get(),call_args.get()));if(active(*o))cacheAudio(*o);return p::boolean(true).release();
}catch(const std::exception&e){return fail(e);}}
PyObject*equipmentIngress(PyObject*,PyObject*args){PyObject*battle,*states;if(!PyArg_ParseTuple(args,"OO",&battle,&states))return nullptr;try{auto o=ensure(battle);o->runtime->equipment(equipmentObjects(states));o->accepted_revision=o->state->revision;o->have_equipment=true;Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject*equipmentSnapshot(PyObject*,PyObject*args){PyObject*battle,*snapshot;int present;if(!PyArg_ParseTuple(args,"OOi",&battle,&snapshot,&present))return nullptr;try{
    auto o=ensure(battle);const auto transaction=++o->equipment_transaction;auto client=field(battle,"client"),rows=field(snapshot,"players");if(rows.get()==Py_None)Py_RETURN_FALSE;
    p::Ref local=p::retain(Py_None);const auto player=p::integral(client.get(),"player_id");
    for(Py_ssize_t i=0;i<p::size(rows.get());++i){auto row=p::item(rows.get(),i);if(PyDict_Check(row.get())&&p::integral(row.get(),"id")==player){replace(local,std::move(row));break;}}
    if(local.get()==Py_None||!PyDict_GetItemString(local.get(),"equipment_states"))Py_RETURN_FALSE;
    auto raw=field(local.get(),"equipment_states");
    auto revision_value=field(local.get(),"equipment_revision");p::Ref revision_number(PyNumber_Long(revision_value.get()));if(!revision_number.get())throw p::Error("canonical player equipment snapshot is invalid");const auto revision=p::wide(revision_number.get());
    if(revision<0||revision<p::integral(battle,"_equipment_revision",-1))throw p::Error("canonical player equipment snapshot is invalid");
    auto runtime=field(battle,"_runtime"),bigworld=field(runtime.get(),"bigworld"),time=field(bigworld.get(),"time");if(!PyCFunction_Check(time.get()))throw p::Error("Native equipment clock requires the reviewed builtin");p::Ref clock(PyObject_CallObject(time.get(),nullptr));const double now=p::scalar(clock.get());auto wire=shared::read(raw.get());const auto frame=equipmentFrame(wire,now);auto result=shared::read(field(local.get(),"equipment_intent_result").get());
    auto states=field(battle,"_equipment_state");
    if(!o->equipment_gate.matches(frame,revision,result)||states.get()==Py_None||p::size(states.get())!=Py_ssize_t(frame.clocks.size())){
        // This is a real contract/quantity/activation/result boundary. Retain
        // the reviewed cold stock parser and its repair-kit acknowledgement.
        auto flag=p::boolean(present!=0);p::Ref call_args(PyTuple_Pack(2,snapshot,flag.get()));auto accepted=p::call(battle,"_restore_local_equipment_snapshot_cold",call_args.get());
        if(active(*o)&&transaction==o->equipment_transaction&&p::flag(accepted.get())){o->equipment_gate.commit(frame,revision,std::move(result));o->equipment_wire=std::move(wire);}return accepted.release();
    }
    // No Python restore/ready/passive policy runs for a ticking wire clock.
    // Raw slots remain coherent for discrete stock activation/settlement.
    for(std::size_t i=0;i<frame.clocks.size();++i){auto state=p::item(states.get(),i);const auto&c=frame.clocks[i];attribute(state.get(),"ready_at",p::number(c.ready_at));attribute(state.get(),"_auto_pending_since",c.automatic?p::number(*c.automatic):p::retain(Py_None));attribute(state.get(),"_ai_pending_since",c.ai?p::number(*c.ai):p::retain(Py_None));}
    o->runtime->equipment(equipmentObjects(states.get()));o->accepted_revision=o->state->revision;o->have_equipment=true;o->equipment_wire=std::move(wire);
    if(present&&p::truth(battle,"_client_ready_received")&&field(battle,"_avatar").get()!=Py_None&&field(battle,"_server").get()!=Py_None){Edges output;o->runtime->equipment(output,now,true);queue(*o,output,now);if(!o->advancing){Guard guard(*o);emitSafe(*o);}}
    Py_RETURN_TRUE;
}catch(const std::exception&e){return fail(e);}}
PyObject*publish(PyObject*,PyObject*args){PyObject*battle;const char*kind;double now,remaining=0.,duration=0.;int force=0;if(!PyArg_ParseTuple(args,"Osd|idd",&battle,&kind,&now,&force,&remaining,&duration))return nullptr;try{
    auto o=ensure(battle);refreshOwner(*o,now);Edges output;const std::string action=kind;const bool ready=p::truth(battle,"_client_ready_received");
    if(action=="ammo")o->runtime->ammo(output,now,ready,force!=0);else if(action=="equipment")o->runtime->equipment(output,now,ready);else if(action=="reload")o->runtime->reload(output,remaining,duration,force!=0);else if(action=="targeting")o->runtime->targeting(output,factors(*o));else throw p::Error("Unknown native player publication edge");
    const bool changed=!output.ammunition.empty()||output.current_shell||!output.equipment.empty()||output.reload||output.targeting;queue(*o,output,now);if(!o->advancing){Guard guard(*o);emitSafe(*o);}return p::boolean(changed).release();
}catch(const std::exception&e){return fail(e);}}
PyObject*close(PyObject*,PyObject*args){PyObject*battle;if(!PyArg_ParseTuple(args,"O",&battle))return nullptr;try{auto capsule=field(battle,"_native_client_weapon_runtime");if(capsule.get()!=Py_None){auto*h=static_cast<Holder*>(PyCapsule_GetPointer(capsule.get(),capsule_name));if(h&&h->owner)h->owner->closed=true;}Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
namespace {
PyObject*factor(PyObject*,PyObject*args){PyObject*battle;if(!PyArg_ParseTuple(args,"O",&battle))return nullptr;try{return p::number(reloadFactor(battle)).release();}catch(const std::exception&e){return fail(e);}}
PyMethodDef methods[]={{"equipment_snapshot",equipmentSnapshot,METH_VARARGS,"Restore relative equipment clocks without repeated stock reconstruction."},{"bind",bind,METH_VARARGS,"Bind exact local gun and cold HUD descriptor metadata."},{"refresh",refresh,METH_VARARGS,"Refresh accepted equipment/status and descriptor metadata."},{"advance",advance,METH_VARARGS,"Advance the complete visible gun timeline natively."},{"audio_frame",audioFrame,METH_VARARGS,"Dispatch a stock audio cue only when its cached native deadline is due."},{"equipment_ingress",equipmentIngress,METH_VARARGS,"Install the exact ready-at clocks once at accepted equipment restoration."},{"publish",publish,METH_VARARGS,"Decide one native HUD publication transaction."},{"reload_factor",factor,METH_VARARGS,"Read the complete native current reload modifier."},{"close",close,METH_VARARGS,"Retire one player weapon runtime."},{nullptr,nullptr,0,nullptr}};
class Module final:public BW::Script::InitTimeJob {public:Module():BW::Script::InitTimeJob(1){}void init()override{if(!Py_InitModule("offline_client_weapon_runtime_native",methods)){PyErr_Print();CRITICAL_MSG("Native player weapon runtime initialization failed\n");}}};Module module;
}
}
