#include "stock_native_player_environment_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_battle.hpp"
#include "stock_native_bot_value.hpp"
#include <algorithm>
#include <cmath>
namespace stock_native_player_environment {
namespace p=stock_native_bot_python;
namespace {
const char* devices[]={"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth","leftTrackHealth","rightTrackHealth"};
Point point(PyObject* o){Point out;for(int i=0;i<3;++i){auto v=p::item(o,i);out[i]=p::scalar(v.get());}return out;}
std::set<std::string> names(PyObject* object){std::set<std::string> out;if(!object||object==Py_None)return out;
 if(!(PyList_CheckExact(object)||PyTuple_CheckExact(object)||Py_TYPE(object)==&PySet_Type||Py_TYPE(object)==&PyFrozenSet_Type))throw p::Error("Player critical set is invalid");
 p::Ref iter(PyObject_GetIter(object));while(auto* item=PyIter_Next(iter.get())){p::Ref held(item);out.insert(p::string(item));}if(PyErr_Occurred())throw p::Error("Player critical set iteration failed");return out;}
std::set<std::string> drowningRoster(PyObject* descriptor){
 std::set<std::string> out;std::map<std::string,int> counts{{"gunner",1},{"loader",1},{"radioman",1}};
 try{auto type=p::value(descriptor,"type"),roles=p::value(type.get(),"crewRoles");if(roles.get()!=Py_None){p::Ref iterator(PyObject_GetIter(roles.get()));while(auto* raw=PyIter_Next(iterator.get())){p::Ref row(raw);auto first=p::item(row.get(),0);auto role=p::string(first.get());auto count=counts.find(role);if(count!=counts.end())role+=std::to_string(count->second++);out.insert(role);}if(PyErr_Occurred())throw p::Error("Player crew roster iteration failed");}}
 catch(const std::exception&){PyErr_Clear();}
 if(out.empty())out={"commander","driver","gunner1","loader1","radioman1"};return out;
}
const char* status(wot::offline_bot_step::CriticalStatus s){using S=wot::offline_bot_step::CriticalStatus;return s==S::destroyed?"destroyed":s==S::critical?"critical":"normal";}
}
bool readMetadata(PyObject* descriptor,Metadata& output){try{
 Metadata m;auto hull=p::value(descriptor,"hull"),tester=p::value(hull.get(),"hitTester"),bbox=p::value(tester.get(),"bbox");auto lo=p::item(bbox.get(),0),hi=p::item(bbox.get(),1);m.minimum=point(lo.get());m.maximum=point(hi.get());auto chassis=p::value(descriptor,"chassis"),mount=p::value(chassis.get(),"hullPosition");m.mount=point(mount.get());
 for(const char* name:devices){const auto pool=stock_native_bot_descriptor::devicePool(descriptor,name);if(pool.maximum)m.maxima[name]=*pool.maximum;}
 m.crew=drowningRoster(descriptor);output=std::move(m);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readPlayer(const native_battle::Actor& record,PyObject* entity,const Metadata& metadata,Player& output){try{
 using native_battle::Value;
 const auto entityValue=[&](const char* name){return p::Ref(stock_native_bot::readNativeEntityField(nullptr,entity,name));};
 if(!entity||entity==Py_None)return false;const Value& state=record.state.resolved();
 if(record.kind!="player"||record.local||state.kind!=Value::Kind::Object||!state.get("alive").truth(true)||state.get("health").numeric()<=0.)return false;
 Player row;row.native_remote=record.native_remote;auto siege=entityValue("siegeState");row.siege_state=siege.get()==Py_None?0:int(p::wide(siege.get()));row.id=record.id;if(row.id<=0)return false;row.input_sequence=std::max<std::int64_t>(0,std::int64_t(state.get("input_seq").numeric()));row.descriptor=metadata;
 // The latest authority row, as the projectile owner reads it; the worker sets a human's collision pose only at spawn.
 const auto has=[](const Value& v,const char* k){return v.object.count(k)!=0;};
 if(has(state,"x")&&has(state,"y")&&has(state,"z")){row.pose.position={state.get("x").numeric(),state.get("y").numeric(),state.get("z").numeric()};}
 else {auto pos=entityValue("position");row.pose.position=point(pos.get());}
 row.pose.yaw=state.get("yaw").numeric();row.pose.pitch=state.get("pitch").numeric();row.pose.roll=state.get("roll").numeric();
 auto hp=entityValue("devices_hp"),destroyed=entityValue("_destroyed_devices"),critical=entityValue("_critical_devices"),crew=entityValue("_crew_ko");
 auto dead=names(destroyed.get()),damaged=names(critical.get());row.critical.crew_ko=names(crew.get());row.critical.fire=p::flag(entityValue("is_on_fire").get());row.critical.ammo_rack_death=p::flag(entityValue("_ammo_rack_death").get());
 if(hp.get()!=Py_None){if(!PyDict_Check(hp.get()))throw p::Error("Player device HP is invalid");Py_ssize_t i=0;PyObject *key=nullptr,*v=nullptr;while(PyDict_Next(hp.get(),&i,&key,&v)){auto name=p::string(key);auto& d=row.critical.devices[name];d.hp=p::scalar(v);const auto maximum=metadata.maxima.find(name);if(maximum!=metadata.maxima.end())d.maximum=maximum->second;}}
 for(const auto& n:dead)row.critical.devices[n].destroyed=true;for(const auto& n:damaged)if(!dead.count(n))row.critical.devices[n].critical=true;
 output=std::move(row);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readAndAdvance(native_battle::Battle& battle,double dt,bool authority,Owner& owner,World& world,
 MetadataCache& cache,std::optional<Batch>& output){try{
 if(!owner.willSample(dt,authority)){output=owner.advance(dt,authority,{},world);return true;}
 std::vector<Player> players;std::set<std::int64_t> present;
 for(const auto& entry:battle.vehicles->actors){const auto& record=*entry.second;
  try {if(record.kind!="player"||record.local)continue;
   const auto id=record.id;if(id<=0)continue;present.insert(id);
   p::Ref entity=p::Ref(stock_native_bot::readNativeEntity(nullptr,record.engine_id));if(entity.get()==Py_None)continue;
   p::Ref descriptor(stock_native_bot::readNativeEntityField(nullptr,entity.get(),"typeDescriptor"));if(descriptor.get()==Py_None)continue;
   auto entry=cache.find(id);if(entry==cache.end()||entry->second.descriptor.get()!=descriptor.get()){
    Metadata metadata;if(!readMetadata(descriptor.get(),metadata)){PyErr_Clear();continue;}
    Py_INCREF(descriptor.get());
    cache[id]={std::shared_ptr<PyObject>(descriptor.get(),[](PyObject* value){if(Py_IsInitialized())Py_DECREF(value);}),std::move(metadata)};entry=cache.find(id);
   }
   Player player;if(readPlayer(record,entity.get(),entry->second.metadata,player))players.push_back(std::move(player));else PyErr_Clear();
  }catch(const std::exception&){PyErr_Clear();}
 }
 for(auto i=cache.begin();i!=cache.end();)if(!present.count(i->first))i=cache.erase(i);else ++i;
 output=owner.advance(dt,true,players,world);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
native_battle::Value observationsValue(const Batch& batch){namespace v=stock_native_bot_value;using wot::offline_bot_step::CriticalEventKind;
 auto out=v::array();for(const auto& item:batch.observations){auto row=v::object();row.object["player_id"]=v::integer(item.id);row.object["input_seq"]=v::integer(item.input_sequence);row.object["level"]=v::integer(item.level);
 if(item.drowning){const auto& proposal=*item.drowning;auto data=v::object(),list=v::array(),destroyed=v::array(),crew=v::array(),events=v::array();
  for(const auto& entry:proposal.state.devices){const auto& d=entry.second;if(!d.hp&&!d.destroyed&&!d.critical)continue;const double hp=d.hp.value_or(0.);const double maximum=std::max(1.,d.maximum.value_or(std::max(1.,std::floor(hp+.5))));
   auto device=v::object();device.object["name"]=v::text(entry.first);device.object["hp"]=v::number(std::max(0.,hp));device.object["max_hp"]=v::number(maximum);const char* state=d.destroyed?"destroyed":d.critical?"critical":hp<=0.?"destroyed":hp<=maximum*.5?"critical":"normal";device.object["state"]=v::text(state);list.array.push_back(std::move(device));if(d.destroyed)destroyed.array.push_back(v::text(entry.first));}
  for(const auto& name:proposal.state.crew_ko)crew.array.push_back(v::text(name));
  for(const auto& event:proposal.events){auto e=v::object();const auto kind=event.kind==CriticalEventKind::device?"device":event.kind==CriticalEventKind::crew?"crew":event.kind==CriticalEventKind::fire?"fire":"ammo_rack";e.object["kind"]=v::text(kind);if(event.kind==CriticalEventKind::device||event.kind==CriticalEventKind::crew)e.object["name"]=v::text(event.name);if(event.kind==CriticalEventKind::device)e.object["old_state"]=v::text(status(event.old_status));if(event.kind==CriticalEventKind::fire)e.object["state"]=v::boolean(event.status==wot::offline_bot_step::CriticalStatus::fire_on);else e.object["state"]=v::text(status(event.status));e.object["cause"]=v::text("drowning");events.array.push_back(std::move(e));}
  data.object["devices"]=std::move(list);data.object["destroyed"]=std::move(destroyed);data.object["crew_ko"]=std::move(crew);data.object["fire"]=v::boolean(proposal.state.fire);data.object["ammo_rack_death"]=v::boolean(proposal.state.ammo_rack_death);data.object["events"]=std::move(events);row.object["drowning_critical"]=std::move(data);}
 out.array.push_back(std::move(row));}return out;}
}
