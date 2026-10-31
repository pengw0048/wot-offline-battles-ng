#if !defined(WOT_CLIENT_CRITICAL_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_critical.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_local.hpp"
#if !defined(WOT_CLIENT_CRITICAL_HOST_BUILD)
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include "pyscript/script.hpp"
#endif
#include <algorithm>
namespace stock_native_client_weapon_runtime {
// Provided by the projectiles/gun subsystem: the active repair-kit bonus and one equipment HUD publication.
double activeRepairBonus(native_battle::Battle&);
void publishEquipment(native_battle::Battle&,double now);
}
namespace native_battle {namespace events {
// Provided by the events subsystem: the stock damage-info panel lifetime check and one damage-info row.
bool damageInfoServiceable(Battle&);
bool showDamageInfo(Battle&,std::int64_t vehicle,int code,int extra,std::int64_t attacker);
}}
namespace stock_native_client_critical {
namespace {
using native_battle::Battle;
namespace p=stock_native_bot_python;namespace drive=stock_native_client_drive;
using Value=shared::Value;
p::Ref field(PyObject*o,const char*n){if(!o||o==Py_None)return p::retain(Py_None);if(PyDict_Check(o))return p::value(o,n);auto key=p::text(n);auto*raw=p::directAttribute(o,key.get());if(!raw&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}return p::Ref(raw);}
void put(PyObject*o,const char*n,const p::Ref&v){auto key=p::text(n);if(PyObject_GenericSetAttr(o,key.get(),v.get())<0)throw p::Error("Native critical compatibility write failed");}
#if defined(WOT_CLIENT_CRITICAL_HOST_BUILD)
extern PyObject* testEntity(const shared::Actor&);
p::Ref entity(const shared::Actor&a){return p::Ref(testEntity(a));}
#else
p::Ref entity(const shared::Actor&a){auto*c=BW::ConnectionControl::pInstance();auto*value=c?c->findEntity(static_cast<BW::EntityID>(a.engine_id)):nullptr;if(!value||value->isDestroyed()||!value->isInWorld())throw p::Error("Native local critical entity retired");return p::Ref(value->pPyEntity().newRef());}
#endif
int extraIndex(PyObject*descriptor,const std::string&name){auto extras=field(descriptor,"extras"),mapping=field(descriptor,"extrasDict"),selected=field(mapping.get(),name.c_str());const auto matches=[&](PyObject*extra){auto named=field(extra,"name");return extra==selected.get()||p::string(named.get())==name;};if(PyDict_Check(extras.get())){Py_ssize_t cursor=0;PyObject*key,*value;while(PyDict_Next(extras.get(),&cursor,&key,&value))if(matches(value))return int(p::wide(key));}else if(extras.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(extras.get());++i){auto value=p::item(extras.get(),i);if(matches(value.get()))return int(i);}const int index=int(p::integral(selected.get(),"index"));if(index<=0)throw p::Error("Native critical descriptor extra is unavailable: "+name);return index;}
struct Native {
 Battle*battle;p::Ref entity;std::shared_ptr<shared::State>state;std::shared_ptr<shared::Actor>actor;std::shared_ptr<drive::Status>status;std::shared_ptr<Owner>core;
 std::unique_ptr<p::Ref>descriptor;std::uint64_t epoch=0,descriptor_revision=0;bool closed=false,busy=false;int progress_status=0,repair_info=0;std::int64_t visual_base=-1;std::uint64_t visual_sequence=0;
 std::map<std::string,int>extra_indexes;std::map<std::string,double>maxima;std::map<std::string,std::shared_ptr<p::Ref>>pending;std::vector<std::string>order;
 Native(Battle&b,PyObject*e):battle(&b),entity(p::retain(e)){}
};
bool active(const Native&o){if(o.closed||o.state->retired||o.state->epoch!=o.epoch||o.actor->tombstone||o.actor->missing||!o.actor->ready)return false;const auto at=o.state->actors.find(o.actor->key);if(at==o.state->actors.end()||at->second!=o.actor)return false;auto current=entity(*o.actor);return current.get()==o.entity.get();}
double now(Native&o){return o.battle->clock();}
void metadata(Native&o){auto*raw=stock_native_client_query::descriptor(*o.actor);if(!raw||raw==Py_None)throw p::Error("Native critical descriptor cache is unavailable");if(o.descriptor&&o.descriptor->get()==raw&&o.descriptor_revision==o.actor->descriptor_revision)return;auto held=p::retain(raw);Metadata m;auto misc=field(raw,"miscAttrs");m.repair_speed_factor=p::numeric(misc.get(),"repairSpeedFactor",1.);
 for(const char*name:{"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","leftTrackHealth","rightTrackHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth"}){const auto pool=stock_native_bot_descriptor::devicePool(raw,name);if(pool.maximum)m.maxima[name]=*pool.maximum;if(pool.regeneration)m.regeneration[name]=*pool.regeneration;if(pool.maximum||pool.regeneration)m.extra_indexes[name]=extraIndex(raw,name);}
 PyObject*constants=o.battle->engine.constants.get();auto statuses=field(constants,"VEHICLE_MISC_STATUS"),indices=field(constants,"DAMAGE_INFO_INDICES");auto progress=field(statuses.get(),"DESTROYED_DEVICE_IS_REPAIRING");if(progress.get()==Py_None)throw p::Error("Native repair-progress status is unavailable");o.progress_status=int(p::wide(progress.get()));o.repair_info=int(p::integral(indices.get(),"DEVICE_REPAIRED_TO_CRITICAL",-1));if(o.repair_info<0)throw p::Error("Native repaired-track damage-info code is unavailable");
 o.extra_indexes=m.extra_indexes;o.maxima=m.maxima;if(o.core)o.core->metadata(std::move(m));else{State seed;auto cache=field(o.entity.get(),"_offline_lan_repair_progress");const auto progress=shared::read(cache.get());for(const auto&entry:progress.resolved().object)seed.progress[entry.first]=int(entry.second.numeric());o.core=std::make_shared<Owner>(std::move(m),o.status->critical(),std::move(seed),o.status->casualties());o.status->trackOverlay(o.core);auto current=o.status->critical();o.core->reconcile(*o.actor,current);o.status->localTracks(current);}
 o.descriptor=std::make_unique<p::Ref>(std::move(held));o.descriptor_revision=o.actor->descriptor_revision;
}
std::shared_ptr<Native>existing(const Battle&b){return b.local?std::static_pointer_cast<Native>(b.local->critical):std::shared_ptr<Native>{};}
void retire(Battle&b){auto saved=existing(b);if(saved){saved->closed=true;if(saved->status)saved->status->trackOverlay({});}if(b.local)b.local->critical.reset();}
std::shared_ptr<Native>ensure(Battle&b){auto saved=existing(b);if(saved){if(active(*saved))return saved;retire(b);}
 if(!b.local)throw p::Error("Native local critical has no local vehicle");
 auto state=b.vehicles;auto at=state->actors.find("player:"+std::to_string(state->local_player_id));if(state->retired||at==state->actors.end()||!at->second->local||!at->second->ready)throw p::Error("Native local critical actor is unavailable");auto value=entity(*at->second);auto owner=std::make_shared<Native>(b,value.get());owner->state=state;owner->actor=at->second;owner->epoch=state->epoch;
 drive::synchronizeStatus(b,now(*owner));owner->status=drive::localStatus(*owner->actor);b.local->critical=owner;metadata(*owner);return owner;
}
void mirror(Native&o){const auto&s=o.core->state();if(o.visual_base!=s.base_revision){o.pending.clear();o.order.clear();o.visual_base=s.base_revision;}}
void queue(Native&o,std::string key,p::Ref row){if(!o.pending.count(key))o.order.push_back(key);p::set(row.get(),"key",p::text(key));o.pending[key]=std::make_shared<p::Ref>(std::move(row));}
// One discrete stock presentation edge; false leaves it pending for the next tick.
bool present(Native&o,PyObject*row){
 const auto kind=p::string(field(row,"kind").get());
 if(kind=="crash"){auto appearance=field(o.entity.get(),"appearance"),callback=field(appearance.get(),"delCrashedTrack");if(PyCallable_Check(callback.get())){auto left=field(row,"left");p::Ref done(PyObject_CallFunctionObjArgs(callback.get(),left.get(),nullptr));}return true;}
 auto id=field(o.entity.get(),"id");
 if(kind=="progress"){auto status=field(row,"status"),code=field(row,"code"),seconds=field(row,"seconds");p::Ref period(PyTuple_Pack(1,seconds.get()));auto callback=field(o.battle->engine.avatar.get(),"updateVehicleMiscStatus");p::Ref done(PyObject_CallFunctionObjArgs(callback.get(),id.get(),status.get(),code.get(),period.get(),nullptr));return true;}
 if(kind=="repaired"){if(native_battle::events::damageInfoServiceable(*o.battle))native_battle::events::showDamageInfo(*o.battle,p::wide(id.get()),int(p::integral(row,"index")),int(p::integral(row,"extra")),p::wide(id.get()));return true;}
 return true;
}
void emit(Native&o){if(o.order.empty()||!active(o))return;std::set<std::string>done;for(const auto&key:o.order){if(!active(o))break;const auto at=o.pending.find(key);if(at==o.pending.end())continue;try{if(present(o,at->second->get()))done.insert(key);}catch(const std::exception&error){o.battle->warn("native track repair presentation",native_battle::describe(error),false);}}
 for(const auto&key:done)o.pending.erase(key);o.order.erase(std::remove_if(o.order.begin(),o.order.end(),[&](const std::string&key){return !o.pending.count(key);}),o.order.end());}
void mirrorTracks(Native&o){auto hp=field(o.entity.get(),"devices_hp"),destroyed=field(o.entity.get(),"_destroyed_devices"),critical=field(o.entity.get(),"_critical_devices");if(!PyDict_Check(hp.get()))throw p::Error("Native local critical HP mapping is unavailable");p::Ref red(PySet_New(destroyed.get())),yellow(PySet_New(critical.get()));bool tracked=false;for(const char*name:{"leftTrackHealth","rightTrackHealth"}){const auto at=o.core->tracks().devices.find(name);if(at==o.core->tracks().devices.end())continue;const auto&d=at->second;auto key=p::text(name);if(d.hp)p::set(hp.get(),name,p::number(*d.hp));if(d.destroyed){if(PySet_Add(red.get(),key.get())<0)throw p::Error("Native track state mirror failed");tracked=true;}else if(PySet_Discard(red.get(),key.get())<0)throw p::Error("Native track state mirror failed");if(d.critical){if(PySet_Add(yellow.get(),key.get())<0)throw p::Error("Native track state mirror failed");}else if(PySet_Discard(yellow.get(),key.get())<0)throw p::Error("Native track state mirror failed");}put(o.entity.get(),"_destroyed_devices",red);put(o.entity.get(),"_critical_devices",yellow);put(o.entity.get(),"is_tracked",p::boolean(tracked));}
// The presented critical state of the local vehicle, and its presented state's "critical" field.
void present(shared::Actor&a,const Value&canonical){a.presented.critical=canonical;Value state=a.presented.state.resolved();if(state.kind!=Value::Kind::Object)state.kind=Value::Kind::Object;state.object["critical"]=canonical;a.presented.state=std::move(state);}
void payload(Native&o,const Effects&effects){if(!effects.payload)return;mirrorTracks(o);auto raw=*effects.payload;raw.object["events"].array.clear();present(*o.actor,raw);}
void visuals(Native&o,const Effects&effects){if(effects.payload)for(const auto&event:effects.payload->get("events").resolved().array){const auto&name=event.get("name").resolved().text;if(event.get("kind").resolved().text!="device"||event.get("old_state").resolved().text!="destroyed"||(name!="leftTrackHealth"&&name!="rightTrackHealth"))continue;auto row=p::dict();p::set(row.get(),"kind",p::text("crash"));p::set(row.get(),"left",p::boolean(name=="leftTrackHealth"));queue(o,"crash:"+name,std::move(row));}
 for(const auto&edge:effects.progress){auto row=p::dict();p::set(row.get(),"kind",p::text("progress"));p::set(row.get(),"status",p::integer(o.progress_status));p::set(row.get(),"code",p::integer(edge.extra_index|(edge.percentage<<8)));p::set(row.get(),"seconds",p::number(edge.seconds_left));queue(o,"progress:"+edge.name,std::move(row));}
 if(effects.payload)for(const auto&event:effects.payload->get("events").resolved().array){const auto&name=event.get("name").resolved().text;if(event.get("kind").resolved().text!="device")continue;auto row=p::dict();p::set(row.get(),"kind",p::text("repaired"));p::set(row.get(),"index",p::integer(o.repair_info));const auto found=o.core->tracks().devices.find(name);if(found==o.core->tracks().devices.end())continue;const auto extra=o.extra_indexes.find(name);if(extra==o.extra_indexes.end()||extra->second<=0)continue;p::set(row.get(),"extra",p::integer(extra->second));queue(o,"repaired:"+std::to_string(++o.visual_sequence)+":"+name,std::move(row));}
}
PyObject*fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
struct Guard{Native&owner;explicit Guard(Native&o):owner(o){owner.busy=true;}~Guard(){owner.busy=false;}};
}
void tick(Battle&b,double dt){if(dt<=0.)return;auto owner=ensure(b);auto&o=*owner;if(o.busy)return;Guard guard(o);const double time=now(o);drive::synchronizeStatus(b,time);if(!active(o))throw p::Error("Native critical owner retired during status ingress");metadata(o);const auto&state=o.actor->state;const auto&fields=state.resolved().object;const bool eligible=fields.count("alive")?state.get("alive").truth()&&std::int64_t(fields.count("health")?state.get("health").numeric():1.)>0:o.actor->alive;if(!eligible)return;const auto&repair=state.get("effective_params").get("loadout").get("repair_factor").resolved();if(repair.kind==Value::Kind::Null)throw p::Error("Native local repair factor is unavailable");const double factor=std::max(0.,repair.numeric(1.))*(1.+stock_native_client_weapon_runtime::activeRepairBonus(b));const auto effects=o.core->advance(o.status->critical(),dt,time,true,factor,o.status->casualties());o.status->localTracks(o.core->tracks());mirror(o);payload(o,effects);visuals(o,effects);emit(o);if(!active(o))return;
 stock_native_client_weapon_runtime::publishEquipment(b,time);
}
bool reconcile(Battle&b,shared::Actor&local,const Value&source,Value*canonical){auto owner=ensure(b);auto&o=*owner;for(const char*key:{"critical_revision","critical_base_revision","critical_ack_seq"})if(source.resolved().kind!=Value::Kind::Object||!source.resolved().object.count(key))throw p::Error("Native player critical state has no revision contract");if(&local!=o.actor.get())return false;drive::synchronizeStatus(b,now(o));metadata(o);const auto revision=std::int64_t(source.get("critical_revision").numeric()),base=std::int64_t(source.get("critical_base_revision").numeric()),ack=std::int64_t(source.get("critical_ack_seq").numeric()),previous=local.presented.critical_revision;if(revision<previous)return false;auto decoded=!canonical?o.status->critical():drive::decodeCritical(*canonical,o.status->metadata());o.core->accept(revision,base,ack,decoded);o.status->localTracks(decoded);mirror(o);local.presented.critical_revision=revision;local.presented.critical_base_revision=base;local.presented.critical_ack_seq=ack;
 if(canonical&&o.core->state().owned){const Value raw=canonical->resolved();Value rows;rows.kind=Value::Kind::Array;for(const auto&row:raw.get("devices").resolved().array)if(row.get("name").resolved().text!="leftTrackHealth"&&row.get("name").resolved().text!="rightTrackHealth")rows.array.push_back(row);std::set<std::string>destroyed;for(const auto&name:raw.get("destroyed").resolved().array)if(name.resolved().text!="leftTrackHealth"&&name.resolved().text!="rightTrackHealth")destroyed.insert(name.resolved().text);
  for(const char*name:{"leftTrackHealth","rightTrackHealth"}){const auto at=decoded.devices.find(name);if(at==decoded.devices.end())continue;Value row;row.kind=Value::Kind::Object;Value n;n.kind=Value::Kind::Text;n.text=name;row.object["name"]=n;auto hp=at->second.hp.value_or(0.);Value value;value.kind=Value::Kind::Number;value.number=hp;row.object["hp"]=value;const auto maximum=o.maxima.find(name);value.number=maximum==o.maxima.end()?at->second.wire_maximum.value_or(at->second.maximum.value_or(std::max(1.,hp))):maximum->second;row.object["max_hp"]=value;n.text=at->second.destroyed?"destroyed":at->second.critical?"critical":"normal";row.object["state"]=n;rows.array.push_back(std::move(row));if(at->second.destroyed)destroyed.insert(name);}
  Value names;names.kind=Value::Kind::Array;for(const auto&name:destroyed){Value text;text.kind=Value::Kind::Text;text.text=name;names.array.push_back(std::move(text));}
  Value updated=raw;updated.object["devices"]=std::move(rows);updated.object["destroyed"]=std::move(names);*canonical=std::move(updated);}
 return revision>previous;}
void close(Battle&b){retire(b);}
const Value&report(const Battle&b){static const Value none;auto owner=existing(b);return owner&&owner->core?owner->core->state().report:none;}
}
