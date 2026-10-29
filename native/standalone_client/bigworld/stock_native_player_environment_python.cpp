#include "stock_native_player_environment_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
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
bool readPlayer(PyObject* record,PyObject* entity,const Metadata& metadata,Player& output,PyObject* battle){try{
 const auto entityValue=[&](const char* name){return p::Ref(stock_native_bot::readNativeEntityField(battle,entity,name));};
 if(!record||!PyDict_Check(record)||!entity||entity==Py_None)return false;auto kind=p::value(record,"kind"),local=p::value(record,"local"),state=p::value(record,"state");
 if(p::string(kind.get())!="player"||p::flag(local.get())||!PyDict_Check(state.get())||!p::truth(state.get(),"alive",true)||p::numeric(state.get(),"health")<=0.)return false;
 Player row;row.native_remote=p::truth(record,"native_remote");auto siege=entityValue("siegeState");row.siege_state=siege.get()==Py_None?0:int(p::wide(siege.get()));row.id=p::integral(record,"network_id");if(row.id<=0)return false;row.input_sequence=std::max<std::int64_t>(0,p::integral(state.get(),"input_seq"));row.descriptor=metadata;
 auto snapshot=p::value(record,"projectile_collision_pose");auto source=PyDict_Check(snapshot.get())?snapshot.get():state.get();
 bool xyz=true;for(const char*k:{"x","y","z"})if(!p::field(source,k))xyz=false;
 if(xyz){row.pose.position={p::numeric(source,"x"),p::numeric(source,"y"),p::numeric(source,"z")};}
 else if(p::field(state.get(),"x")&&p::field(state.get(),"y")&&p::field(state.get(),"z")){row.pose.position={p::numeric(state.get(),"x"),p::numeric(state.get(),"y"),p::numeric(state.get(),"z")};}
 else {auto pos=entityValue("position");row.pose.position=point(pos.get());}
 row.pose.yaw=p::numeric(source,"yaw");row.pose.pitch=p::numeric(source,"pitch");row.pose.roll=p::numeric(source,"roll");
 auto hp=entityValue("devices_hp"),destroyed=entityValue("_destroyed_devices"),critical=entityValue("_critical_devices"),crew=entityValue("_crew_ko");
 auto dead=names(destroyed.get()),damaged=names(critical.get());row.critical.crew_ko=names(crew.get());row.critical.fire=p::flag(entityValue("is_on_fire").get());row.critical.ammo_rack_death=p::flag(entityValue("_ammo_rack_death").get());
 if(hp.get()!=Py_None){if(!PyDict_Check(hp.get()))throw p::Error("Player device HP is invalid");Py_ssize_t i=0;PyObject *key=nullptr,*v=nullptr;while(PyDict_Next(hp.get(),&i,&key,&v)){auto name=p::string(key);auto& d=row.critical.devices[name];d.hp=p::scalar(v);const auto maximum=metadata.maxima.find(name);if(maximum!=metadata.maxima.end())d.maximum=maximum->second;}}
 for(const auto& n:dead)row.critical.devices[n].destroyed=true;for(const auto& n:damaged)if(!dead.count(n))row.critical.devices[n].critical=true;
 output=std::move(row);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readAndAdvance(PyObject* battle,double dt,bool authority,Owner& owner,World& world,
 MetadataCache& cache,std::optional<Batch>& output){try{
 if(!owner.willSample(dt,authority)){output=owner.advance(dt,authority,{},world);return true;}
 auto records=p::value(battle,"_records");if(!PyDict_Check(records.get()))throw p::Error("Player environment records invalid");
 std::vector<Player> players;std::set<std::int64_t> present;Py_ssize_t cursor=0;PyObject* key=nullptr,*record=nullptr;
 while(PyDict_Next(records.get(),&cursor,&key,&record)){
  if(!PyDict_Check(record))continue;
  try {auto kind=p::value(record,"kind");if(p::string(kind.get())!="player"||p::truth(record,"local"))continue;
   const auto id=p::integral(record,"network_id");if(id<=0)continue;present.insert(id);
   p::Ref entity(stock_native_bot::readNativeEntity(battle,p::integral(record,"engine_id")));if(entity.get()==Py_None)continue;
   p::Ref descriptor(stock_native_bot::readNativeEntityField(battle,entity.get(),"typeDescriptor"));if(descriptor.get()==Py_None)continue;
   auto entry=cache.find(id);if(entry==cache.end()||entry->second.descriptor.get()!=descriptor.get()){
    Metadata metadata;if(!readMetadata(descriptor.get(),metadata)){PyErr_Clear();continue;}
    Py_INCREF(descriptor.get());
    cache[id]={std::shared_ptr<PyObject>(descriptor.get(),[](PyObject* value){if(Py_IsInitialized())Py_DECREF(value);}),std::move(metadata)};entry=cache.find(id);
   }
   Player player;if(readPlayer(record,entity.get(),entry->second.metadata,player,battle))players.push_back(std::move(player));else PyErr_Clear();
  }catch(const std::exception&){PyErr_Clear();}
 }
 for(auto i=cache.begin();i!=cache.end();)if(!present.count(i->first))i=cache.erase(i);else ++i;
 output=owner.advance(dt,true,players,world);return true;
 }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
PyObject* observationsValue(const Batch& batch){try{auto out=p::list();for(const auto& item:batch.observations){auto row=p::dict();p::set(row.get(),"player_id",p::integer(item.id));p::set(row.get(),"input_seq",p::integer(item.input_sequence));p::set(row.get(),"level",p::integer(item.level));
 if(item.drowning){const auto& proposal=*item.drowning;auto data=p::dict(),list=p::list(),destroyed=p::list(),crew=p::list(),events=p::list();
  for(const auto& entry:proposal.state.devices){const auto& d=entry.second;if(!d.hp&&!d.destroyed&&!d.critical)continue;const double hp=d.hp.value_or(0.);const double maximum=std::max(1.,d.maximum.value_or(std::max(1.,std::floor(hp+.5))));
   auto device=p::dict();p::set(device.get(),"name",p::text(entry.first));p::set(device.get(),"hp",p::number(std::max(0.,hp)));p::set(device.get(),"max_hp",p::number(maximum));const char* state=d.destroyed?"destroyed":d.critical?"critical":hp<=0.?"destroyed":hp<=maximum*.5?"critical":"normal";p::set(device.get(),"state",p::text(state));p::append(list.get(),device);if(d.destroyed)p::append(destroyed.get(),p::text(entry.first));}
  for(const auto& name:proposal.state.crew_ko)p::append(crew.get(),p::text(name));
  for(const auto& event:proposal.events){auto e=p::dict();const auto kind=event.kind==wot::offline_bot_step::CriticalEventKind::device?"device":event.kind==wot::offline_bot_step::CriticalEventKind::crew?"crew":event.kind==wot::offline_bot_step::CriticalEventKind::fire?"fire":"ammo_rack";p::set(e.get(),"kind",p::text(kind));if(event.kind==wot::offline_bot_step::CriticalEventKind::device||event.kind==wot::offline_bot_step::CriticalEventKind::crew)p::set(e.get(),"name",p::text(event.name));if(event.kind==wot::offline_bot_step::CriticalEventKind::device)p::set(e.get(),"old_state",p::text(status(event.old_status)));if(event.kind==wot::offline_bot_step::CriticalEventKind::fire)p::set(e.get(),"state",p::boolean(event.status==wot::offline_bot_step::CriticalStatus::fire_on));else p::set(e.get(),"state",p::text(status(event.status)));p::set(e.get(),"cause",p::text("drowning"));p::append(events.get(),e);}
  p::set(data.get(),"devices",list);p::set(data.get(),"destroyed",destroyed);p::set(data.get(),"crew_ko",crew);p::set(data.get(),"fire",p::boolean(proposal.state.fire));p::set(data.get(),"ammo_rack_death",p::boolean(proposal.state.ammo_rack_death));p::set(data.get(),"events",events);p::set(row.get(),"drowning_critical",data);}
 p::append(out.get(),row);}return out.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
}
