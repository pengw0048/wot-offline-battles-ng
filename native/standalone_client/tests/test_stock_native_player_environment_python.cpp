// Production critical payloads must reach every simulation consumer without Python mirrors.
#include "../bigworld/stock_native_battle_subsystems.hpp"
#include "../bigworld/stock_native_battle_critical_state.hpp"
#include "../bigworld/stock_native_player_environment_python.hpp"
#include "../bigworld/stock_native_projectile_damage_python.hpp"
#include "../bigworld/stock_native_wire_value.hpp"
#include "../bigworld/stock_native_bot_descriptor.hpp"
#include "../bigworld/stock_native_bot_resource_lifecycle.hpp"
#include <Python.h>
#include <cassert>
#include <cmath>
using namespace stock_native_player_environment;
namespace native_battle {
struct VisibilityCore {};struct LocalCriticalOwner {};struct RemoteVehicle {};struct ReplicaBinding {};
struct FallenFoliage {};struct DestructibleCatalog {};class ReloadSound {};
Visibility::~Visibility()=default;Critical::Critical()=default;Critical::~Critical()=default;
Remote::~Remote()=default;World::~World()=default;LocalGun::~LocalGun()=default;Projectiles::~Projectiles()=default;
Battle::Battle():vehicles(std::make_shared<cs::State>()){} Battle::~Battle()=default;
Battle* active=nullptr;PyObject* presented_entity=nullptr;
Battle* current(){return active;}
Actor* Battle::vehicleByEngine(std::int64_t id)const {
 for(const auto& row:vehicles->actors)if(row.second&&row.second->engine_id==id)return row.second.get();return nullptr;
}
p::Ref Battle::entity(std::int64_t id)const {
 return p::retain(presented_entity&&p::wide(py::attr(presented_entity,"id").get())==id?presented_entity:Py_None);
}
namespace remote {
p::Ref get(const Battle& b,std::int64_t id){return b.entity(id);}
Overlay* overlay(PyObject*,bool){return nullptr;}
}
namespace critical {
@PRODUCER_HELPERS@
@PRODUCER@
using V=Value;
bool has(const V& v,const char* k){return v.resolved().object.count(k)!=0;}
bool track(const std::string& n){return n=="leftTrackHealth"||n=="rightTrackHealth";}
V& writable(V& v){return v;}
bool nativeOwner(const Battle&,const Actor&){return false;}
bool reconcileNative(Battle&,Actor&,const V&,V*){throw p::Error("Unexpected local critical path");}
bool acknowledgeLocalDamageReport(Battle&,std::int64_t,std::int64_t,std::int64_t){return false;}
@RECONCILE@
}
}
namespace stock_native_projectile_damage {
namespace p=stock_native_bot_python;namespace w=stock_native_wire_value;
@PROJECTILE_READER@
}
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();
 const char* script=R"PY(
class O(object):pass
entity=O();entity.id=101;entity.health=500;entity.siegeState=0
entity.typeDescriptor=O();entity.typeDescriptor.type=O();entity.typeDescriptor.type.crewRoles=[('commander',),('driver',)]
entity.typeDescriptor._offline_critical_device_pools={'engineHealth':(100.,50.),'leftTrackHealth':(100.,50.)}
# These retired mirrors deliberately contradict every native state below.
entity.devices_hp={'engineHealth':99.};entity._destroyed_devices=set(['gunHealth'])
entity._critical_devices=set(['radioHealth']);entity._crew_ko=set(['driver'])
entity.is_on_fire=False;entity._ammo_rack_death=False
accepted={'equipment_states':[{'equipment':{'kind':'extinguisher','fireStartingChanceFactor':.8},'usesLeft':0},{'equipment':{'kind':'medkit','bonusValue':.3,'stunResistanceDuration':.1},'usesLeft':1}],'effective_params':{'crew':{'dynamic_spotting':{'crew':['commander'],'states':{'1:1':{'battle_factors':{'engine_fire_factor':.5}}}}}}}
payloads=[dict(devices=[dict(name='engineHealth',hp=37.,max_hp=100.,state='critical'),dict(name='leftTrackHealth',hp=0.,max_hp=100.,state='destroyed'),dict(name='unknownHealth',hp=8.,max_hp=10.,state='normal')],destroyed=['leftTrackHealth'],crew_ko=['commander'],fire=True,ammo_rack_death=True),
 dict(devices=[dict(name='engineHealth',hp=17.,max_hp=100.,state='critical'),dict(name='leftTrackHealth',hp=0.,max_hp=100.,state='destroyed')],destroyed=['leftTrackHealth'],crew_ko=['commander'],fire=True,ammo_rack_death=False),
 dict(devices=[dict(name='engineHealth',hp=100.,max_hp=100.,state='normal'),dict(name='leftTrackHealth',hp=50.,max_hp=100.,state='critical')],destroyed=[],crew_ko=[],fire=False,ammo_rack_death=False),
 dict(devices=[],destroyed=[],crew_ko=[],fire=False,ammo_rack_death=False)]
)PY";
 if(PyRun_SimpleString(script)){PyErr_Print();return 1;}
 auto* globals=PyModule_GetDict(PyImport_AddModule("__main__"));auto* entity=PyDict_GetItemString(globals,"entity");
 namespace b=native_battle;namespace d=stock_native_projectile_damage;
 {
 b::Battle battle;b::active=&battle;b::presented_entity=entity;battle.phase=b::Phase::Running;
 battle.critical=std::make_unique<b::Critical>();auto record=std::make_shared<b::Actor>();
 record->kind="player";record->key="player:3";record->id=3;record->engine_id=101;record->ready=true;
 battle.vehicles->actors[record->key]=record;
 auto& state=record->state;state.kind=b::Value::Kind::Object;
 for(const auto& item:std::map<std::string,double>{{"x",382.},{"y",56.31},{"z",38.},{"yaw",1.25},{"pitch",0.},{"roll",0.},{"health",500.},{"input_seq",64.}}){state.object[item.first].kind=b::Value::Kind::Number;state.object[item.first].number=item.second;}
 auto* accepted=PyDict_GetItemString(globals,"accepted");
 for(const char* name:{"equipment_states","effective_params"})state.object[name]=b::cs::read(PyDict_GetItemString(accepted,name));
 state.object["alive"].kind=b::Value::Kind::Boolean;state.object["alive"].boolean=true;
 state.object["critical_base_revision"].kind=b::Value::Kind::Integer;state.object["critical_base_revision"].integer=4;
 state.object["critical_ack_seq"].kind=b::Value::Kind::Integer;state.object["critical_ack_seq"].integer=5;
 state.object["combat_base_revision"]=state.object["critical_base_revision"];state.object["combat_ack_seq"]=state.object["critical_ack_seq"];
 Metadata metadata;metadata.maxima={{"engineHealth",100.},{"leftTrackHealth",100.}};metadata.crew={"commander","driver"};
 d::TargetDescriptor descriptor;descriptor.critical.maxima=metadata.maxima;
 auto* payloads=PyDict_GetItemString(globals,"payloads");
 // Real server startup omits empty player critical and sends the three revision tokens at zero.
 for(const char* name:{"critical_revision","critical_base_revision","critical_ack_seq"}){state.object[name].kind=b::Value::Kind::Integer;state.object[name].integer=0;}
 assert(battle.critical->vehicles.empty());
 assert(b::critical::reconcileAuthority(battle,*record,state,nullptr));
 assert(battle.critical->vehicles.count(101));
 Player initial;assert(readPlayer(*record,entity,metadata,initial)&&initial.critical.devices.empty()&&!initial.critical.fire&&initial.critical.crew_ko.empty());
 d::Target initial_target;assert(d::readProjectileTargetState(nullptr,true,3,state,entity,descriptor,initial_target));
 auto initial_drowning=drowningProposal(initial.critical,metadata);assert(initial_drowning&&initial_drowning->state.devices.at("engineHealth").destroyed);
 // A nonzero first contract, or a lost already-initialized owner, cannot claim an undamaged baseline.
 battle.critical->vehicles.clear();record->presented.critical_revision=-1;state.object["critical_revision"].integer=1;
 assert(b::critical::reconcileAuthority(battle,*record,state,nullptr));assert(battle.critical->vehicles.empty());
 for(const char* name:{"critical_revision","critical_base_revision","critical_ack_seq"}){
  for(const char* zero:{"critical_revision","critical_base_revision","critical_ack_seq"})state.object[zero].integer=0;
  state.object[name].integer=-1;record->presented.critical_revision=-1;
  b::critical::reconcileAuthority(battle,*record,state,nullptr);assert(battle.critical->vehicles.empty());
 }
 for(const char* zero:{"critical_revision","critical_base_revision","critical_ack_seq"})state.object[zero].integer=0;
 record->presented.critical_revision=-1;state.object["critical_revision"].integer=0;
 assert(b::critical::reconcileAuthority(battle,*record,state,nullptr));battle.critical->vehicles.clear();
 assert(!b::critical::reconcileAuthority(battle,*record,state,nullptr));assert(battle.critical->vehicles.empty());
 state.object["critical_base_revision"].integer=4;state.object["critical_ack_seq"].integer=5;
 for(int i=0;i<4;++i){
  // Keep stale mirrors opposite to both the native start and end edges.
  assert(PyObject_SetAttrString(entity,"is_on_fire",i<2?Py_False:Py_True)==0);
  b::critical::applyPayload(battle,entity,b::cs::read(PyList_GetItem(payloads,i)));
  Player player;if(!readPlayer(*record,entity,metadata,player)){PyErr_Print();return 2;}
  assert(player.id==3&&player.input_sequence==64&&player.pose.position[0]==382.&&player.pose.position[1]==56.31&&player.pose.position[2]==38.&&player.pose.yaw==1.25);
  assert(player.critical.fire==(i<2)&&player.critical.ammo_rack_death==(i==0));
  assert(player.critical.crew_ko==(i<2?std::set<std::string>{"commander"}:std::set<std::string>{}));
  if(i<3){assert(player.critical.devices.at("engineHealth").hp==std::optional<double>(i==0?37.:i==1?17.:100.));assert(player.critical.devices.at("engineHealth").maximum==std::optional<double>(100.));assert(player.critical.devices.at("leftTrackHealth").destroyed==(i<2));}
  else assert(player.critical.devices.empty());
  if(i==0){assert(!player.critical.devices.at("unknownHealth").maximum);auto drowned=drowningProposal(player.critical,metadata);assert(drowned&&drowned->state.fire&&drowned->state.ammo_rack_death&&drowned->state.devices.at("unknownHealth").hp==std::optional<double>(8.));assert(drowned->state.crew_ko.count("commander")&&drowned->state.crew_ko.count("driver"));}
  // The same reader serves human and Bot projectile targets, including sequential damage and repairs.
  for(bool human:{true,false}){record->kind=human?"player":"bot";d::Target target;
   if(!d::readProjectileTargetState(nullptr,human,3,state,entity,descriptor,target)){PyErr_Print();return 3;}
   assert(std::abs(target.config.fire_chance_factor-(i<2?.4:.8))<1.e-12&&std::abs(target.config.medkit_bonus-.3)<1.e-12&&std::abs(target.stun_config.resist_duration-.1)<1.e-12);
   assert(target.identity.human==human&&target.identity.id==3&&target.config.health==500&&target.critical_target_base_revision==4&&target.critical_target_ack_sequence==5);
   assert(target.critical.fire==player.critical.fire&&target.critical.ammo_rack_death==player.critical.ammo_rack_death&&target.critical.crew_ko==player.critical.crew_ko);
   assert(target.critical.devices.size()==player.critical.devices.size());for(const auto& entry:player.critical.devices){const auto& t=target.critical.devices.at(entry.first);assert(t.hp==entry.second.hp&&t.maximum==entry.second.maximum&&t.destroyed==entry.second.destroyed&&t.critical==entry.second.critical);}
  }record->kind="player";
 }
 // The exact stock entity need not expose any retired damage properties.
 for(const char* name:{"devices_hp","_destroyed_devices","_critical_devices","_crew_ko","is_on_fire","_ammo_rack_death"})assert(PyObject_DelAttrString(entity,name)==0);
 Player absent;assert(readPlayer(*record,entity,metadata,absent));d::Target absent_target;assert(d::readProjectileTargetState(nullptr,true,3,state,entity,descriptor,absent_target));
 // Failed sampling must leave the caller's previous output intact, never replace damage with defaults.
 Player saved;saved.id=999;d::Target saved_target;saved_target.config.health=999;
 const auto rejected=[&](){assert(!readPlayer(*record,entity,metadata,saved));PyErr_Clear();assert(saved.id==999);assert(!d::readProjectileTargetState(nullptr,true,3,state,entity,descriptor,saved_target));PyErr_Clear();assert(saved_target.config.health==999);};
 battle.critical->vehicles.clear();rejected();b::critical::applyPayload(battle,entity,b::cs::read(PyList_GetItem(payloads,0)));
 battle.phase=b::Phase::Stopped;rejected();battle.phase=b::Phase::Running;
 battle.vehicles->retired=true;rejected();battle.vehicles->retired=false;
 record->tombstone=true;rejected();record->tombstone=false;
 record->ready=false;rejected();record->ready=true;
 b::presented_entity=nullptr;rejected();b::presented_entity=entity;
 b::active=nullptr;rejected();b::active=&battle;
 auto replacement=std::make_shared<b::Actor>(*record);battle.vehicles->actors[record->key]=replacement;
 assert(!readPlayer(*record,entity,metadata,saved));PyErr_Clear();assert(saved.id==999);
 battle.vehicles->actors[record->key]=record;record->id=4;assert(!d::readProjectileTargetState(nullptr,true,3,state,entity,descriptor,saved_target));PyErr_Clear();assert(saved_target.config.health==999);
 b::active=nullptr;b::presented_entity=nullptr;
 }
 Py_Finalize();
}
