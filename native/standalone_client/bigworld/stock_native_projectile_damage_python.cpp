#include "stock_native_projectile_damage_python.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_battle_critical_state.hpp"
#include "stock_native_wire_value.hpp"
#include <algorithm>
namespace stock_native_projectile_damage {
namespace p=stock_native_bot_python;
namespace w=stock_native_wire_value;
namespace {
const HeTuning* installed_he_tuning=nullptr;
const char* devices[]={"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth","leftTrackHealth","rightTrackHealth"};
const char* stun_factors[]={"stunFactorEnginePower","stunFactorVehicleRotationSpeed","stunFactorTurretTraverse","stunFactorViewDistance","stunFactorMaxSpeed","stunFactorReloadTime","stunFactorAimingTime","stunFactorVehicleMovementShotDispersion","stunFactorVehicleRotationShotDispersion","stunFactorTurretRotationShotDispersion","stunFactorMinShotDispersion"};
p::Ref call1(PyObject* callable,PyObject* arg){return p::Ref(PyObject_CallFunctionObjArgs(callable,arg,nullptr));}
const char* stun_fields[]={"stunRadius","stunDuration","stunFactor","guaranteedStunDuration","damageDurationCoeff","guaranteedStunEffect","damageEffectCoeff"};
p::Ref fallbackValue(PyObject* source,PyObject* fallback,const char* name){auto value=p::value(source,name);return value.get()==Py_None?p::value(fallback,name):std::move(value);}
void copyField(PyObject* source,PyObject* target,const char* name){auto v=p::value(source,name);if(v.get()!=Py_None)p::set(target,name,v);}
p::Ref legacyShot(PyObject* source){
 if(PyDict_Check(source))return p::retain(source);
 auto shot=p::dict(),source_shell=p::value(source,"shell");
 auto shell=PyDict_Check(source_shell.get())?p::retain(source_shell.get()):p::dict();
 if(!PyDict_Check(source_shell.get())) {
  auto type=p::value(source_shell.get(),"type");
  const char* fields[]={"kind","caliber","damage","explosionRadius","explosionDamageFactor","explosionDamageAbsorptionFactor","explosionEdgeDamageFactor","compactDescr","name"};
  for(const char* name:fields){auto v=fallbackValue(source_shell.get(),type.get(),name);if(v.get()!=Py_None)p::set(shell.get(),name,v);}
  if(!p::field(shell.get(),"kind")){auto kind=p::value(type.get(),"name");if(kind.get()!=Py_None)p::set(shell.get(),"kind",kind);}
  copyField(source_shell.get(),shell.get(),"stun");
 }
 p::set(shot.get(),"shell",shell);
 PyObject* raw_power=PyObject_GetAttrString(source,"piercingPower");
 if(!raw_power){if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw p::Error("Projectile power read failed");PyErr_Clear();p::Ref pair(Py_BuildValue("(dd)",100.,100.));p::set(shot.get(),"piercingPower",pair);}else{p::Ref power(raw_power);p::set(shot.get(),"piercingPower",power);}
 PyObject* raw_maximum=PyObject_GetAttrString(source,"maxDistance");
 if(!raw_maximum){if(!PyErr_ExceptionMatches(PyExc_AttributeError))throw p::Error("Projectile range read failed");PyErr_Clear();p::set(shot.get(),"maxDistance",p::number(0.));}else{p::Ref maximum(raw_maximum);p::set(shot.get(),"maxDistance",maximum);}
 return shot;
}
double relaxedNumber(PyObject* value){p::Ref numeric(PyNumber_Float(value));return p::scalar(numeric.get());}
double heFactor(PyObject* shell,const char* name,double fallback,double maximum=0.){
 try{auto value=p::value(shell,name);if(value.get()==Py_None)return fallback;double n=relaxedNumber(value.get());return n>0.&&(!maximum||n<=maximum)?n:fallback;}catch(const std::exception&){PyErr_Clear();return fallback;}
}
double roundSix(double value){
 if(value==0.)return value;
 int exponent=0;const double fraction=std::frexp(value,&exponent);
 const std::uint64_t significand=std::uint64_t(std::ldexp(fraction,53));
 const std::uint64_t lower=(significand&0xffffffffu)*15625u;
 const std::uint64_t upper=(significand>>32)*15625u;
 const std::uint64_t low=lower+(upper<<32),high=(upper>>32)+(low<lower?1u:0u);
 const int shift=47-exponent;std::uint64_t integer=0;
 if(shift>0&&shift<64){integer=(low>>shift)|(high<<(64-shift));if((low&((std::uint64_t(1)<<shift)-1))>=(std::uint64_t(1)<<(shift-1)))++integer;}
 else if(shift==64)integer=high+(low>=(std::uint64_t(1)<<63));
 else if(shift>64&&shift<128){const int bits=shift-64;integer=high>>bits;if((high&((std::uint64_t(1)<<bits)-1))>=(std::uint64_t(1)<<(bits-1)))++integer;}
 return double(integer)/1.e6;
}
StunSpec readStun(PyObject* value){
 if(PyDict_Check(value)&&PyDict_Size(value)!=7)throw p::Error("invalid stun shell fields");
 double v[7];for(int i=0;i<7;++i){auto item=p::value(value,stun_fields[i]);if(item.get()==Py_None||PyBool_Check(item.get())||!(PyFloat_Check(item.get())
#if PY_MAJOR_VERSION < 3
 ||PyInt_Check(item.get())
#endif
 ||PyLong_Check(item.get())))throw p::Error("invalid stun number");
  double n=p::scalar(item.get()),high=i==0?100.:i==1?900.:1.;if(n<0.||n>high)throw p::Error("invalid stun number");v[i]=roundSix(n);}
 StunSpec out;out.radius=v[0];out.duration=v[1];out.factor=v[2];out.guaranteed_duration=v[3];out.damage_duration=v[4];out.guaranteed_effect=v[5];out.damage_effect=v[6];return out;
}
double optionalNumber(PyObject* o,const char* name,double fallback){return p::numeric(o,name,fallback);}
std::optional<double> positiveComponent(PyObject* value,Py_ssize_t index){
 if(!value||value==Py_None)return {};
 const char* keys[]={"x","y","z"};
 PyObject* raw=nullptr;
 if(PyDict_Check(value)){raw=PyDict_GetItemString(value,keys[index]);Py_XINCREF(raw);}
 else {raw=PySequence_GetItem(value,index);if(!raw){PyErr_Clear();raw=PyObject_GetAttrString(value,keys[index]);}}
 if(!raw){PyErr_Clear();return {};}
 p::Ref part(raw);
 try {if(PyBool_Check(part.get()))return {};const double n=relaxedNumber(part.get());return n>0.?std::optional<double>(n):std::nullopt;}
 catch(const std::exception&){PyErr_Clear();return {};}
}
void readTrackBounds(PyObject* chassis,TargetDescriptor& out){
 auto carrying=p::value(chassis,"topRightCarryingPoint"),sizes=p::value(chassis,"drivingWheelsSizes");
 try {
  auto half=positiveComponent(carrying.get(),1),front=positiveComponent(sizes.get(),0),rear=positiveComponent(sizes.get(),1);
  if(!half||!front||!rear)return;
  const double largest=std::max({*half,*front,*rear});
  const double a=*half/largest,b=*front/largest,c=*rear/largest;
  if(b+c>=2.*a){const double split=*half*((c-b)/(b+c));out.front_bound=split;out.rear_bound=split;}
  else {out.front_bound=*half-*front;out.rear_bound=*rear-*half;}
 } catch(const std::exception&){PyErr_Clear();}
}
}
bool readShotDescriptor(PyObject* source,ShotDescriptor& output){try{
 auto shot=legacyShot(source),shell=p::value(shot.get(),"shell");if(!PyDict_Check(shell.get()))throw p::Error("Projectile shell is invalid");
 ShotDescriptor out;out.shot.deadeye=p::truth(source,"deadeye");auto kind=p::value(shell.get(),"kind");out.shot.shell.kind=kind.get()==Py_None?"ARMOR_PIERCING":p::string(kind.get());out.shot.shell.caliber=optionalNumber(shell.get(),"caliber",100.);
 auto piercing=p::value(shot.get(),"piercingPower");if(piercing.get()==Py_None)throw p::Error("Projectile piercing descriptor unavailable");auto near=p::item(piercing.get(),0),far=p::item(piercing.get(),1);
 out.shot.shell.piercing_near=p::scalar(near.get());out.shot.shell.piercing_far=p::scalar(far.get());out.shot.shell.maximum_distance=optionalNumber(shot.get(),"maxDistance",0.);
 auto damage=p::value(shell.get(),"damage");if(damage.get()!=Py_None){if(PySequence_Check(damage.get())&&!PyString_Check(damage.get())){auto a=p::item(damage.get(),0);out.shot.shell.damage=p::scalar(a.get());const auto count=PySequence_Size(damage.get());if(count<0)throw p::Error("Projectile damage sequence invalid");if(count>1){auto b=p::item(damage.get(),1);out.shot.device_damage_mean=p::scalar(b.get());}}else out.shot.shell.damage=p::scalar(damage.get());}
 if(!out.shot.device_damage_mean){auto d=p::value(shell.get(),"deviceDamage");if(d.get()!=Py_None){if(PyTuple_Check(d.get())||PyList_Check(d.get())){if(p::size(d.get())>0){auto first=p::item(d.get(),0);out.shot.device_damage_mean=p::scalar(first.get());}}else out.shot.device_damage_mean=p::scalar(d.get());}}
 out.shot.shell.explosion_damage_factor=heFactor(shell.get(),"explosionDamageFactor",.5);
 out.shot.shell.explosion_absorption_factor=heFactor(shell.get(),"explosionDamageAbsorptionFactor",1.3);
 out.shot.edge_damage_factor=heFactor(shell.get(),"explosionEdgeDamageFactor",.15,1.);
 if(installed_he_tuning){if(installed_he_tuning->splash_fraction)out.shot.shell.explosion_damage_factor=*installed_he_tuning->splash_fraction;if(installed_he_tuning->armor_factor)out.shot.shell.explosion_absorption_factor=*installed_he_tuning->armor_factor;}
 else{p::Ref rules(PyImport_ImportModule("gui.mods.offline_lan_0922.combat_rules"));auto overrides=p::value(rules.get(),"_OFFH_HE_TUNING_OVERRIDES");
 if(PyDict_Check(overrides.get())){auto f=p::value(overrides.get(),"splash_fraction"),a=p::value(overrides.get(),"armor_factor");if(f.get()!=Py_None)out.shot.shell.explosion_damage_factor=p::scalar(f.get());if(a.get()!=Py_None)out.shot.shell.explosion_absorption_factor=p::scalar(a.get());}}
 double radius=0.;try{auto v=p::value(shell.get(),"explosionRadius");if(v.get()!=Py_None)radius=relaxedNumber(v.get());}catch(const std::exception&){PyErr_Clear();}
 if(radius<=0.){double caliber=0.;try{auto v=p::value(shell.get(),"caliber");if(v.get()!=Py_None)caliber=relaxedNumber(v.get());}catch(const std::exception&){PyErr_Clear();}radius=caliber>0.?caliber*caliber/5555.:0.;}out.shot.explosion_radius=radius;
 auto stun=p::value(shell.get(),"stun");if(stun.get()!=Py_None){auto v=readStun(stun.get());out.stun=v;out.shot.stun=v;}
 output=std::move(out);return true;
 }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readLaunchDescriptor(PyObject* source,LaunchDescriptor& output){try{
 auto raw=p::dict(),shell=p::dict(),source_shell=p::value(source,"shell"),shell_type=p::value(source_shell.get(),"type");
 const char* shot_fields[]={"speed","gravity","maxDistance","piercingPower"};for(const char* name:shot_fields)copyField(source,raw.get(),name);
 const char* shell_fields[]={"kind","caliber","damage","explosionRadius","explosionDamageFactor","explosionDamageAbsorptionFactor","explosionEdgeDamageFactor"};for(const char* name:shell_fields)copyField(source_shell.get(),shell.get(),name);
 if(!p::field(shell.get(),"kind")){auto kind=p::value(shell_type.get(),"name");if(kind.get()!=Py_None&&p::flag(kind.get()))p::set(shell.get(),"kind",kind);}
 if(!p::field(shell.get(),"explosionRadius")){auto radius=p::value(shell_type.get(),"explosionRadius");if(radius.get()!=Py_None)p::set(shell.get(),"explosionRadius",radius);else p::set(shell.get(),"explosionRadius",p::number(0.));}
 auto kind=p::value(shell.get(),"kind");if(kind.get()!=Py_None&&p::string(kind.get())=="HIGH_EXPLOSIVE"){
  const char* names[]={"explosionDamageFactor","explosionDamageAbsorptionFactor","explosionEdgeDamageFactor"};const double defaults[]={.5,1.3,.15};
  for(int i=0;i<3;++i){auto value=fallbackValue(source_shell.get(),shell_type.get(),names[i]);auto selected=p::dict();if(value.get()!=Py_None)p::set(selected.get(),names[i],value);p::set(shell.get(),names[i],p::number(heFactor(selected.get(),names[i],defaults[i],i==2?1.:0.)));}
  copyField(source_shell.get(),shell.get(),"stun");
 }
 p::set(raw.get(),"shell",shell);
 if(!PyDict_Check(raw.get()))throw p::Error("Projectile launch projection invalid");if(!PyDict_Check(shell.get()))throw p::Error("Projectile launch shell invalid");
 LaunchDescriptor next;next.speed=p::numeric(raw.get(),"speed");next.gravity=p::numeric(raw.get(),"gravity");next.maximum=p::numeric(raw.get(),"maxDistance");next.raw_radius=p::numeric(shell.get(),"explosionRadius");
 next.explosion_factors=p::field(shell.get(),"explosionDamageFactor")&&p::field(shell.get(),"explosionDamageAbsorptionFactor")&&p::field(shell.get(),"explosionEdgeDamageFactor");
 ShotDescriptor shot;if(!readShotDescriptor(raw.get(),shot))return false;next.shot=std::move(shot.shot);output=std::move(next);return true;
 }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readTargetDescriptor(PyObject* descriptor,PyObject* critical_descriptor,TargetDescriptor& output,PyObject* accepted_critical_profile){try{
 if(!critical_descriptor||critical_descriptor==Py_None)critical_descriptor=descriptor;
 TargetDescriptor out;PyObject* config_module=PyImport_ImportModule("_constants");if(config_module){p::Ref module(config_module);auto options=p::value(module.get(),"CONFIG_OPTIONS");if(PyDict_Check(options.get())){out.critical.module_damage=p::truth(options.get(),"module_damage",true);out.critical.crew_damage=p::truth(options.get(),"crew_damage",true);out.critical.internal_module_damage=p::truth(options.get(),"internal_module_damage",true);}}else PyErr_Clear();
 p::Ref track(PyImport_ImportModule("gui.mods.offline_lan_0922.track_damage"));
 for(const char* name:devices){const auto pool=stock_native_bot_descriptor::devicePool(critical_descriptor,name);if(pool.maximum)out.critical.maxima[name]=*pool.maximum;}
 if(accepted_critical_profile&&accepted_critical_profile!=Py_None){if(!PyDict_Check(accepted_critical_profile))throw p::Error("Projectile critical profile invalid");auto rows=p::value(accepted_critical_profile,"devices");if(!PyList_Check(rows.get())&&!PyTuple_Check(rows.get()))throw p::Error("Projectile critical pools invalid");out.critical.maxima.clear();for(Py_ssize_t i=0;i<p::size(rows.get());++i){auto row=p::item(rows.get(),i),name=p::value(row.get(),"name");out.critical.maxima[p::string(name.get())]=p::numeric(row.get(),"max_hp");}}
 auto engine=p::value(descriptor,"engine");out.critical.engine_fire_chance=p::numeric(engine.get(),"fireStartingChance",.15);out.critical.min_engine_fire_damage=p::numeric(engine.get(),"minFireStartingDamage",21.);
 auto chassis=p::value(descriptor,"chassis");readTrackBounds(chassis.get(),out);
 auto bulk_fn=p::value(track.get(),"bulk_health_factor"),bulk=call1(bulk_fn.get(),descriptor);if(bulk.get()!=Py_None)out.bulk_factor=p::scalar(bulk.get());
 out.crew_roster=stock_native_bot_descriptor::crewRoster(descriptor);
 auto misc=p::value(descriptor,"miscAttrs");try{out.spall=std::max(1.,p::numeric(misc.get(),"antifragmentationLiningFactor",1.));}catch(const std::exception&){PyErr_Clear();out.spall=1.;}out.stun_resistance_duration=p::numeric(misc.get(),"stunResistanceDuration");out.stun_resistance_effect=p::numeric(misc.get(),"stunResistanceEffect");
 output=std::move(out);return true;
 }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readProjectileTargetState(PyObject* battle,bool human,std::int64_t network_id,const stock_native_client_state::Value& accepted,PyObject* entity,const TargetDescriptor& descriptor,Target& output){try{
 using Kind=w::Kind;const auto& state=accepted.resolved();if(!entity||entity==Py_None)throw p::Error("Projectile target record invalid");if(state.kind!=Kind::Object)throw p::Error("Projectile target state invalid");
 Target out;out.identity={human,network_id};out.config=descriptor.critical;out.spall=descriptor.spall;out.crew_roster=descriptor.crew_roster;
 out.track_front=descriptor.front_bound;out.track_rear=descriptor.rear_bound;out.track_bulk=descriptor.bulk_factor;out.stun_config.resist_duration=descriptor.stun_resistance_duration;out.stun_config.resist_effect=descriptor.stun_resistance_effect;
 out.critical_target_base_revision=w::integral(state,human?"critical_base_revision":"combat_base_revision",-1);out.critical_target_ack_sequence=w::integral(state,human?"critical_ack_seq":"combat_ack_seq",-1);
 auto* current=native_battle::current();
 auto entity_id=p::value(entity,"id");const auto engine_id=p::wide(entity_id.get());
 const auto* actor=current?current->vehicleByEngine(engine_id):nullptr;
 if(!actor||actor->id!=network_id||actor->kind!=(human?"player":"bot"))throw p::Error("Projectile target identity is stale");
 out.critical=native_battle::critical::simulationState(*actor,entity,descriptor.critical.maxima);
 auto native_health=p::Ref(stock_native_bot::readNativeEntityField(battle,entity,"health"));out.config.health=int(p::wide(native_health.get()));
 const auto& equipment=state.get("equipment_states").resolved();if(equipment.kind!=Kind::Null){w::size(equipment);for(const auto& value:equipment.array){const auto& row=value.resolved();if(row.kind!=Kind::Object)continue;const auto& contract=row.get("equipment").resolved();if(contract.kind!=Kind::Object)continue;const auto equipment_kind=w::string(contract.get("kind"));const auto uses=w::integral(row,"usesLeft",-1);if(equipment_kind=="extinguisher")out.config.fire_chance_factor*=std::max(0.,w::numeric(contract,"fireStartingChanceFactor",1.));else if(equipment_kind=="medkit"&&uses!=0)out.config.medkit_bonus+=w::numeric(contract,"bonusValue");if(uses!=0){out.stun_config.resist_duration+=w::numeric(contract,"stunResistanceDuration");out.stun_config.resist_effect+=w::numeric(contract,"stunResistanceEffect");}}}
 out.stun_config.resist_duration=std::max(0.,std::min(1.,out.stun_config.resist_duration));out.stun_config.resist_effect=std::max(0.,std::min(1.,out.stun_config.resist_effect));
 const auto& dynamic=state.get("effective_params").resolved().get("crew").resolved().get("dynamic_spotting").resolved();const auto& roster=dynamic.get("crew").resolved();const auto& states=dynamic.get("states").resolved();std::uint64_t mask=0;if(roster.kind!=Kind::Null&&w::size(roster)<=63)for(std::size_t i=0;i<roster.array.size();++i){if(out.critical.crew_ko.count(w::string(roster.array[i])))mask|=std::uint64_t(1)<<i;}
 if(states.kind==Kind::Object){const auto key=std::to_string(mask)+":"+(out.critical.fire?"1":"0");const auto row=states.object.find(key);if(row!=states.object.end()){const auto& factors=row->second.resolved().get("battle_factors");out.config.fire_chance_factor*=w::numeric(factors,"engine_fire_factor",1.);}}
 output=std::move(out);return true;
 }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readStunConfig(PyObject* config,StunConfig& output){try{StunConfig out;out.minimum_duration=p::numeric(config,"minStunDuration");for(std::size_t i=0;i<out.factors.size();++i)out.factors[i]=p::numeric(config,stun_factors[i]);output=out;return true;
 }catch(const std::exception& e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
void installHeTuning(const HeTuning* tuning){installed_he_tuning=tuning;}
void releaseHeTuning(const HeTuning* tuning){if(installed_he_tuning==tuning)installed_he_tuning=nullptr;}
}
