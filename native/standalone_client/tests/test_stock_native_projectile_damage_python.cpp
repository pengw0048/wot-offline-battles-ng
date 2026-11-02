#include "../bigworld/stock_native_projectile_damage.hpp"
#include "../bigworld/stock_native_projectile_damage_python.hpp"
#include <Python.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <string>
using namespace stock_native_projectile_damage;
struct Rng:Random{double gauss(double mean,double)override{return mean;}double uniform()override{return .5;}};
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();auto source=std::getenv("WOT_GAMEPLAY_SOURCE");assert(source);auto path=PySys_GetObject(const_cast<char*>("path"));auto value=PyString_FromString((std::string(source)+"/src/res/scripts/client").c_str());PyList_Insert(path,0,value);Py_DECREF(value);
 const char* script=R"PY(
import sys,types
gui=types.ModuleType('gui');gui.__path__=[sys.path[0]+'/gui'];sys.modules['gui']=gui
mods=types.ModuleType('gui.mods');mods.__path__=[sys.path[0]+'/gui/mods'];sys.modules['gui.mods']=mods
from gui.mods.offline_lan_0922 import combat_rules as r
class M(object): pass
screen=M();screen.armor=20.;screen.vehicleDamageFactor=0.;screen.kind=1;screen.collideOnceOnly=True
hull=M();hull.armor=100.;hull.vehicleDamageFactor=1.;hull.kind=2
rows=[]
for kind in ('ARMOR_PIERCING','ARMOR_PIERCING_CR','HOLLOW_CHARGE','HIGH_EXPLOSIVE','ARMOR_PIERCING_HE'):
 for cosine in (.3,.6,1.):
  shot={'piercingPower':(180.,140.),'maxDistance':800.,'shell':{'kind':kind,'caliber':100.,'damage':(200.,75.)}}
  hits=[(1.,1.,screen,'a'),(1.1,1.,screen,'a'),(2.,cosine,hull,'b')]
  c=r._offh_resolve_armor_contact(shot,200.,hits,10.,1.,1.)
  rows.append(None if c is None else (c['result'],c['layer']=='structural',c['distance'],c['effective_armor'],c['piercing'],c['accumulated_loss']))
)PY";if(PyRun_SimpleString(script)){PyErr_Print();return 1;}auto globals=PyModule_GetDict(PyImport_AddModule("__main__"));auto rows=PyDict_GetItemString(globals,"rows");Rng rng;int i=0;for(const char* kind:{"ARMOR_PIERCING","ARMOR_PIERCING_CR","HOLLOW_CHARGE","HIGH_EXPLOSIVE","ARMOR_PIERCING_HE"})for(double cosine:{.3,.6,1.}){
 FrozenShot shot;shot.shell.kind=kind;shot.shell.caliber=100;shot.shell.piercing_near=180;shot.shell.piercing_far=140;shot.shell.maximum_distance=800;shot.penetration_factor=1.;stock_native_bot_armor::Material a;a.armor=20.;a.vehicle_damage_factor=0.;a.kind=1;a.once=true;stock_native_bot_armor::Material b;b.armor=100.;b.kind=2;
 auto out=resolveArmor(shot,200.,10.,{{1.,1.,0,&a},{1.1,1.,0,&a},{2.,cosine,1,&b}},rng);auto row=PyList_GetItem(rows,i++);assert(out&&row!=Py_None);assert(out->result==PyInt_AsLong(PyTuple_GetItem(row,0)));assert(out->structural==(PyTuple_GetItem(row,1)==Py_True));double expected[]={out->distance,out->effective_armor,out->piercing,out->accumulated_loss};for(int j=0;j<4;++j)assert(std::abs(expected[j]-PyFloat_AsDouble(PyTuple_GetItem(row,j+2)))<1.e-8);
 }
 const char* critical=R"PY(
import random
from gui.mods.offline_lan_0922 import critical_damage as cd, device_damage as dd
bw=types.ModuleType('BigWorld');bw.player=lambda: type('Player',(object,),{'playerVehicleID':-1})();sys.modules['BigWorld']=bw
sys.modules['Math']=types.ModuleType('Math')
conf=types.ModuleType('_constants');conf.CONFIG_OPTIONS={'internal_module_damage':False};sys.modules['_constants']=conf
random.random=lambda:.1;random.uniform=lambda lo,hi:(lo+hi)*.5
dd.device_max_hp=lambda td,name:100.
class O(object):pass
td=O();td.engine={'fireStartingChance':0.};td.type=O();td.type.crewRoles=[('commander',),('driver',)]
target=O();target.typeDescriptor=td;target.id=2;target.health=500;target.devices_hp={'engineHealth':100.};target._destroyed_devices=set();target._critical_devices=set();target._crew_ko=set();target.is_on_fire=False;target._ammo_rack_death=False;target._is_killed=False;target.getComponents=lambda:()
material=M();material.extra=O();material.extra.name='engineHealth';material.chanceToHitByProjectile=1.;material.vehicleDamageFactor=1.;material.armor=100.
result=cd.apply_direct(target,[(1.,1.,material,'vehicleHull')],None,None,50,{'kind':'ARMOR_PIERCING','damage':(200.,80.)},1,True)
)PY";if(PyRun_SimpleString(critical)){PyErr_Print();return 2;}FrozenShot shot;shot.shell.kind="ARMOR_PIERCING";shot.shell.damage=200.;shot.device_damage_mean=80.;CriticalConfig config;config.health=500;config.maxima["engineHealth"]=100.;config.engine_fire_chance=0.;wot::offline_bot_step::CriticalState initial;initial.devices["engineHealth"].hp=100.;initial.devices["engineHealth"].maximum=100.;CriticalLayer layer;layer.distance=1.;layer.name="engineHealth";layer.projectile_chance=1.;auto result=resolveCritical(shot,config,initial,{layer},50,rng);auto target=PyDict_GetItemString(globals,"target");auto hp=PyObject_GetAttrString(target,"devices_hp");assert(std::abs(*result.state.devices.at("engineHealth").hp-PyFloat_AsDouble(PyDict_GetItemString(hp,"engineHealth")))<1.e-8);Py_DECREF(hp);assert(result.damage==50&&result.events.size()==1);
 const char* cold=R"PY(
from gui.mods.offline_lan_0922 import track_damage
track_damage.bulk_health_factor=lambda desc:3.
cold_shot={'speed':500.,'gravity':9.81,'piercingPower':(180.,140.),'maxDistance':800.,'shell':{'kind':'HIGH_EXPLOSIVE','caliber':100.,'damage':(200.,80.),'explosionRadius':5.,'explosionEdgeDamageFactor':.2,'stun':{'stunRadius':5.,'stunDuration':10.,'stunFactor':.5,'guaranteedStunDuration':.3,'damageDurationCoeff':.7,'guaranteedStunEffect':.2,'damageEffectCoeff':.8}}}
td._offline_critical_device_pools=dict((name,(100.,50.)) for name in ('engineHealth','ammoBayHealth','fuelTankHealth','radioHealth','gunHealth','turretRotatorHealth','surveyingDeviceHealth','leftTrackHealth','rightTrackHealth'))
from gui.mods.offline_lan_0922 import descriptor_donation, stun_mechanics
bound_inputs=[{'topRightCarryingPoint':(1.,3.),'drivingWheelsSizes':(1.,1.)},
 {'topRightCarryingPoint':{'y':1.},'drivingWheelsSizes':{'x':3.,'y':2.}},
 {'topRightCarryingPoint':(1.,0.),'drivingWheelsSizes':(1.,1.)},
 {'topRightCarryingPoint':(1.,3.),'drivingWheelsSizes':(0.,1.)}]
bound_expected=[track_damage.wheel_zone_bounds(v) for v in bound_inputs]
shell_type=O();shell_type.name='HIGH_EXPLOSIVE';shell_type.explosionRadius=4.;shell_type.explosionDamageFactor=.75
native_shell=O();native_shell.type=shell_type;native_shell.caliber=90.;native_shell.damage=(190.,70.)
native_shot=O();native_shot.shell=native_shell;native_shot.piercingPower=(110.,80.);native_shot.maxDistance=600.;native_shot.speed=400.;native_shot.gravity=9.81
native_expected=r.legacy_shot(native_shot);native_launch_expected=descriptor_donation.project_shot(native_shot)
stun_round_values=[0.,.0000005,.0000015,.1234565,.5000005,.9999995,1.]
stun_expected=[]
stun_inputs=[]
for value in stun_round_values:
    item=dict(cold_shot['shell']['stun']);item['stunFactor']=value
    shot=dict(cold_shot);shot['shell']=dict(cold_shot['shell']);shot['shell']['stun']=item
    stun_inputs.append(shot);stun_expected.append(stun_mechanics.shell_component(item)['stunFactor'])
forbidden_callbacks=[]
def bomb(*args,**kwargs):
 forbidden_callbacks.append(True)
 raise AssertionError('offline descriptor callback')
saved_maxhp=dd.device_max_hp
dd.device_max_hp=bomb
r.legacy_shot=bomb
r._offh_he_factors=bomb
r._offh_he_radius=bomb
descriptor_donation.project_shot=bomb
stun_mechanics.shell_component=bomb
track_damage.wheel_zone_bounds=bomb
td.chassis={'topRightCarryingPoint':(1.,3.,0.),'drivingWheelsSizes':(1.,1.)};td.miscAttrs={'stunResistanceDuration':.1,'stunResistanceEffect':.2}
)PY";if(PyRun_SimpleString(cold)){PyErr_Print();return 3;}ShotDescriptor frozen;if(!readShotDescriptor(PyDict_GetItemString(globals,"cold_shot"),frozen)){PyErr_Print();return 4;}assert(frozen.shot.device_damage_mean&&*frozen.shot.device_damage_mean==80.&&frozen.shot.explosion_radius==5.&&frozen.shot.stun);LaunchDescriptor launch;if(!readLaunchDescriptor(PyDict_GetItemString(globals,"cold_shot"),launch)){PyErr_Print();return 11;}assert(launch.speed==500.&&launch.gravity==9.81&&launch.raw_radius==5.&&launch.explosion_factors&&launch.shot.device_damage_mean);TargetDescriptor descriptor;if(!readTargetDescriptor(PyDict_GetItemString(globals,"td"),nullptr,descriptor)){PyErr_Print();return 5;}assert(descriptor.front_bound&&*descriptor.front_bound==2.&&descriptor.rear_bound&&*descriptor.rear_bound==-2.&&descriptor.bulk_factor&&*descriptor.bulk_factor==3.);
 ShotDescriptor objectShot;LaunchDescriptor objectLaunch;auto objectSource=PyDict_GetItemString(globals,"native_shot");if(!readShotDescriptor(objectSource,objectShot)||!readLaunchDescriptor(objectSource,objectLaunch)){PyErr_Print();return 16;}
 assert(objectShot.shot.shell.kind=="HIGH_EXPLOSIVE"&&objectShot.shot.explosion_radius==4.&&objectShot.shot.shell.explosion_damage_factor==.75&&objectShot.shot.device_damage_mean==70.);
 assert(objectLaunch.shot.shell.kind=="HIGH_EXPLOSIVE"&&objectLaunch.raw_radius==4.&&objectLaunch.shot.shell.explosion_damage_factor==.75);
 auto stunInputs=PyDict_GetItemString(globals,"stun_inputs"),stunExpected=PyDict_GetItemString(globals,"stun_expected");
 for(Py_ssize_t j=0;j<PyList_Size(stunInputs);++j){ShotDescriptor test;if(!readShotDescriptor(PyList_GetItem(stunInputs,j),test)){PyErr_Print();return 15;}assert(test.stun&&test.stun->factor==PyFloat_AsDouble(PyList_GetItem(stunExpected,j)));}
 auto inputs=PyDict_GetItemString(globals,"bound_inputs"),expected_bounds=PyDict_GetItemString(globals,"bound_expected"),td=PyDict_GetItemString(globals,"td");
 for(Py_ssize_t j=0;j<PyList_Size(inputs);++j){assert(PyObject_SetAttrString(td,"chassis",PyList_GetItem(inputs,j))==0);TargetDescriptor test;if(!readTargetDescriptor(td,nullptr,test)){PyErr_Print();return 14;}auto expected=PyList_GetItem(expected_bounds,j);if(expected==Py_None)assert(!test.front_bound&&!test.rear_bound);else{assert(test.front_bound&&test.rear_bound);assert(std::abs(*test.front_bound-PyFloat_AsDouble(PyTuple_GetItem(expected,0)))<1.e-12);assert(std::abs(*test.rear_bound-PyFloat_AsDouble(PyTuple_GetItem(expected,1)))<1.e-12);}}
 const char* stun_script=R"PY(
dd.device_max_hp=saved_maxhp
stun_mechanics=reload(stun_mechanics)
r=reload(r)
track_damage=reload(track_damage)
from gui.mods.offline_lan_0922 import stun_mechanics as sm
stun_cfg=dict((v, .8 if k in ('mobility','traverse','turret_speed','vision','speed') else 1.2) for k,v in sm.STAT_CONFIG.items());stun_cfg['minStunDuration']=1.
expected_stun=sm.impact(cold_shot['shell'],100,2.,stun_cfg,{'stunResistanceDuration':.1,'stunResistanceEffect':.2})
)PY";if(PyRun_SimpleString(stun_script)){PyErr_Print();return 6;}StunConfig stunConfig;if(!readStunConfig(PyDict_GetItemString(globals,"stun_cfg"),stunConfig)){PyErr_Print();return 7;}stunConfig.resist_duration=.1;stunConfig.resist_effect=.2;auto stunned=stun(frozen.shot,*frozen.shot.stun,stunConfig,100,2.,1000,100000);assert(stunned);auto expected=PyDict_GetItemString(globals,"expected_stun");assert(stunned->duration_ms==long(std::round(PyFloat_AsDouble(PyTuple_GetItem(expected,0))*1000.)));auto factors=PyTuple_GetItem(expected,1);const char* names[]={"mobility","traverse","turret_speed","vision","speed","reload","aim_time","bloom_move","bloom_rotation","bloom_turret","dispersion"};for(int j=0;j<11;++j)assert(std::abs(stunned->factors[j]-PyFloat_AsDouble(PyDict_GetItemString(factors,names[j])))<1.e-9);
 assert(PyRun_SimpleString("accepted_profile={'devices':[{'name':'engineHealth','max_hp':250.,'regen_hp':125.}]}")==0);TargetDescriptor adjusted;if(!readTargetDescriptor(PyDict_GetItemString(globals,"td"),nullptr,adjusted,PyDict_GetItemString(globals,"accepted_profile"))){PyErr_Print();return 12;}assert(adjusted.critical.maxima.size()==1&&adjusted.critical.maxima.at("engineHealth")==250.);
 const char* he_script=R"PY(
he_shell={'shell':{'kind':'HIGH_EXPLOSIVE','damage':(200.,80.),'caliber':100.,'explosionRadius':10.,'explosionDamageFactor':.5,'explosionDamageAbsorptionFactor':1.3,'explosionEdgeDamageFactor':.15}}
screen.vehicleDamageFactor=0.;hull.vehicleDamageFactor=1.;hull.armor=10.
he_expected=r.he_blast_contact(he_shell,(0.,0.,0.),(0.,0.,0.),(0.,0.,10.),[(2.,1.,screen,'a'),(5.,1.,hull,'b')],200.,spall_coefficient=2.)
)PY";if(PyRun_SimpleString(he_script)){PyErr_Print();return 13;}FrozenShot he;he.shell.kind="HIGH_EXPLOSIVE";he.shell.explosion_damage_factor=.5;he.shell.explosion_absorption_factor=1.3;he.explosion_radius=10.;he.edge_damage_factor=.15;stock_native_bot_armor::Material screen;screen.vehicle_damage_factor=0.;screen.armor=20.;stock_native_bot_armor::Material hull;hull.vehicle_damage_factor=1.;hull.armor=10.;auto he_result=heBlast(he,{0.,0.,0.},{0.,0.,0.},{0.,0.,10.},{{2.,1.,0,&screen},{5.,1.,1,&hull}},200.,2.);assert(he_result);auto he_expected=PyDict_GetItemString(globals,"he_expected");assert(he_result->damage==PyInt_AsLong(PyDict_GetItemString(he_expected,"damage"))&&he_result->distance==PyFloat_AsDouble(PyDict_GetItemString(he_expected,"distance")));
 const char* target_script=R"PY(
entity=target
entity._critical_devices=set();entity._crew_ko=set(['driver']);entity.is_on_fire=False
record={'kind':'player','network_id':2,'state':{'health':500,'critical_base_revision':4,'critical_ack_seq':5,'equipment_states':[{'equipment':{'kind':'extinguisher','fireStartingChanceFactor':.8},'usesLeft':0},{'equipment':{'kind':'medkit','bonusValue':.3,'stunResistanceDuration':.1},'usesLeft':1}], 'effective_params':{'crew':{'dynamic_spotting':{'crew':['driver'],'states':{'1:0':{'battle_factors':{'engine_fire_factor':.5}}}}}}}}
)PY";if(PyRun_SimpleString(target_script)){PyErr_Print();return 8;}Target parsed;parsed.config.health=999;
 // A bare entity fixture has no current native battle/critical owner and must not fabricate target state.
 assert(!readProjectileTargetState(nullptr,true,2,stock_native_client_state::read(PyDict_GetItemString(PyDict_GetItemString(globals,"record"),"state")),PyDict_GetItemString(globals,"entity"),descriptor,parsed));PyErr_Clear();assert(parsed.config.health==999);

 const char* extra=R"PY(
critical_cases=[]
for name,hp in [('engineHealth',100.),('fuelTankHealth',50.),('ammoBayHealth',50.),('leftTrackHealth',70.),('commanderHealth',0.)]:
 v=O();v.typeDescriptor=td;v.id=2;v.health=500;v.devices_hp={} if name=='commanderHealth' else {name:hp};v._destroyed_devices=set();v._critical_devices=set();v._crew_ko=set();v.is_on_fire=False;v._ammo_rack_death=False;v._is_killed=False;v.getComponents=lambda:()
 m=M();m.extra=O();m.extra.name=name;m.chanceToHitByProjectile=1.;m.vehicleDamageFactor=1.;m.armor=100.
 damage,payload=cd.apply_direct(v,[(1.,1.,m,'vehicleHull')],None,None,50,{'kind':'ARMOR_PIERCING','damage':(200.,80.)},1,True)
 critical_cases.append((damage, v.devices_hp.get(name), v.is_on_fire,v._ammo_rack_death, sorted(v._crew_ko)))
)PY";if(PyRun_SimpleString(extra)){PyErr_Print();return 10;}auto cases=PyDict_GetItemString(globals,"critical_cases");const char* devices[]={"engineHealth","fuelTankHealth","ammoBayHealth","leftTrackHealth","commanderHealth"};double initial_hp[]={100.,50.,50.,70.,0.};for(int j=0;j<5;++j){CriticalConfig cfg;cfg.health=500;cfg.engine_fire_chance=0.;wot::offline_bot_step::CriticalState before;cfg.maxima[devices[j]]=100.;if(j<4){before.devices[devices[j]].hp=initial_hp[j];before.devices[devices[j]].maximum=100.;}CriticalLayer cl;cl.distance=1.;cl.name=devices[j];cl.projectile_chance=1.;auto got=resolveCritical(shot,cfg,before,{cl},50,rng);auto expected=PyList_GetItem(cases,j);assert(got.damage==PyInt_AsLong(PyTuple_GetItem(expected,0)));if(j<4)assert(std::abs(*got.state.devices.at(devices[j]).hp-PyFloat_AsDouble(PyTuple_GetItem(expected,1)))<1.e-9);assert(got.state.fire==(PyTuple_GetItem(expected,2)==Py_True));assert(got.state.ammo_rack_death==(PyTuple_GetItem(expected,3)==Py_True));assert(got.state.crew_ko.size()==std::size_t(PyList_Size(PyTuple_GetItem(expected,4))));}
 Py_Finalize();}
