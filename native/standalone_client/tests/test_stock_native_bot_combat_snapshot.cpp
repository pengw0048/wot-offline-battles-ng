#include "../bigworld/stock_native_bot_combat_snapshot.hpp"
#include "../bigworld/stock_native_bot_descriptor.hpp"
#include <Python.h>
#include <cassert>
#include <cstdlib>
#include <iostream>
using namespace wot::offline_bot_step;
int main(){
 Py_DontWriteBytecodeFlag=1;Py_Initialize();
 const char* source=std::getenv("WOT_GAMEPLAY_SOURCE");assert(source);
 PyObject* path=PySys_GetObject(const_cast<char*>("path"));
 std::string scripts=std::string(source)+"/src/res/scripts/client";
 PyObject* python_path=PyString_FromString(scripts.c_str());assert(PyList_Insert(path,0,python_path)==0);Py_DECREF(python_path);
 const char* code=R"PY(
import sys, types
gui=types.ModuleType('gui');gui.__path__=[sys.path[0]+'/gui'];sys.modules['gui']=gui
mods=types.ModuleType('gui.mods');mods.__path__=[sys.path[0]+'/gui/mods'];sys.modules['gui.mods']=mods
class Obj(object): pass
runtime=Obj(); runtime._gun_states={}; runtime._ammo_states={}; runtime._burst_states={}; runtime._equipment_states={}; runtime._bot_behavior={};runtime._combat_sync={}
runtime._bot_ratings={1:.34};runtime._repair_factors={1:.57}
state={'id':1,'health':100,'max_health':100,'alive':True,'fire_seq':6,'critical':{'devices':[{'name':'engineHealth','hp':12.,'max_hp':100.,'state':'critical'}], 'destroyed':[], 'crew_ko':[], 'fire':False}}
descriptor={'gun':{'pitchLimits':(-.5,.5),'rotationSpeed':.4,'shots':[{'speed':500.,'gravity':9.81,'maxDistance':1000.,'piercingPower':(150.,100.),'shell':{'kind':'HIGH_EXPLOSIVE','damage':(300.,0.),'caliber':100.,'explosionDamageFactor':.6,'explosionDamageAbsorptionFactor':1.5}}]}, 'turret':{'rotationSpeed':.5,'gunPosition':(0.,.2,1.),'hitTester':{'bbox':((-1.,-1.,-1.),(1.,1.,1.))}}, 'chassis':{'hullPosition':(0.,0.,0.),'topRightCarryingPoint':(1.,.5,1.)}, 'hull':{'turretPositions':[(0.,1.,0.)],'hitTester':{'bbox':((-2.,0.,-1.),(2.,2.,1.))}}, 'miscAttrs':{}}
descriptor['gun'].update(shotDispersionAngle=.2,shotDispersionFactors={'afterShot':1.,'turretRotation':.1},aimingTime=1.,reloadTime=2.,burst=(1,0.))
descriptor['type']={'crewRoles':[('commander',),('gunner',),('loader',)],'name':'fixture'};descriptor['optionalDevices']=[]
class NativeVector(object):
    def __init__(self,*values):self.values=values
    def __len__(self):return len(self.values)
    def __getitem__(self,n):return self.values[n]
descriptor['chassis']['hullPosition']=NativeVector(0.,0.,0.);descriptor['chassis']['topRightCarryingPoint']=NativeVector(1.,1.)
descriptor['hull']['turretPositions']=[NativeVector(0.,1.,0.)]
for name in ('items','items.utils','items.tankmen','items.qualifiers','items.VehicleDescrCrew','VehicleQualifiersApplier','constants'):
    sys.modules[name]=types.ModuleType(name)
sys.modules['items'].__path__=[]
utils=sys.modules['items.utils'];utils.generateDefaultCrew=lambda descriptor,level:['crew-%d'%level]
utils.makeDefaultVehicleAttributeFactors=lambda: {'invisibility':[0.,1.]}
sys.modules['items.tankmen'].MAX_SKILL_LEVEL=100
aspects=Obj();aspects.DEFAULT=0;aspects.WHEN_STILL=1;sys.modules['constants'].VEHICLE_TTC_ASPECTS=aspects
qualifier=Obj();qualifier.MAIN_SKILL=0;sys.modules['items.qualifiers'].QUALIFIER_TYPE=qualifier
sys.modules['VehicleQualifiersApplier'].VehicleQualifiersApplier=lambda *args:{0:{}}
class Crew(object):
    camouflageFactor=0.
    def __init__(self,descriptor,crew,bonuses,activityFlags,isFire):assert activityFlags==[True] and isFire is False
    def onCollectFactors(self,factors):factors.update({'turret/rotationSpeed':1.043,'gun/rotationSpeed':1.043,'gun/reloadTime':1./1.043,'gun/aimingTime':1./1.043,'repairSpeed':.42})
    def onCollectShotDispersionFactors(self,factors):factors[0]=1./1.043
sys.modules['items.VehicleDescrCrew'].VehicleDescrCrew=Crew
profile={}
g=Obj();g.crew_level=90
g.fully_aimed_dispersion=.2;g.after_shot=1.;g.after_shot_in_burst=1.;g.burst_count=1;g.burst_interval=0.
g.turret_dispersion_factor=.1;g.aiming_time=1.;g.movement_dispersion_factor=.1;g.rotation_dispersion_factor=.1
g.reload_full=2.;g.reload_intra=0.;g.clip_size=1;g.shell_count=1;g.clip=1;g.elapsed=.5;g.reload_duration=2.;g.reload_kind='full';g.reload_factor=1.;g._burst_remaining=0
g.current_dispersion_factor=1.;g.aiming_start_factor=1.;g.aiming_elapsed=0.;g.dispersion=.2;g.motion_dispersion_squared=0.;g.loadout={}
a=Obj();a.categories={0:'standard'};a.remaining=[10];a.loaded=0;a.next=0;a.reload_pending=False;a.plan_pending=True
from gui.mods.offline_lan_0922.burst_mechanics import BurstClock
runtime._gun_states[1]=g;runtime._ammo_states[1]=a;runtime._burst_states[1]=BurstClock()
# Freeze fixed-source pure ammo functions as an independent differential oracle.
import ast,math
source_tree=ast.parse(open(sys.path[0]+'/gui/mods/offline_lan_0922/bot_runtime.py').read())
wanted=set(('_number','_value','_bot_ammo_capacity','_bot_ammo_categories','_bot_ammo_distribution'))
oracle={'math':math};exec(compile(ast.Module([node for node in source_tree.body if isinstance(node,ast.FunctionDef) and node.name in wanted]),'<fixed ammo oracle>','exec'),oracle)
record={'health':100,'alive':True,'critical':state['critical'], 'combat_fire_elapsed':0.,'combat_fire_timer':0.,'stun_end_server_time_ms':0}
runtime._combat_sync[1]={'server_combat':record,'next_seq':1,'acked_seq':0,'combat_revision':0,'base_revision':0,'server_tick':4,
 'authority_handoff_pending':True,'unpublished_steps':[(.1,2.,False,())],
 'pending':[{'seq':1,'combat':record,'steps':[(.2,1.,False,())]}]}
)PY";
 if(PyRun_SimpleString(code)!=0){PyErr_Print();return 1;}
 auto main=PyImport_AddModule("__main__");auto dict=PyModule_GetDict(main);
 CombatSnapshot snapshot;
 if(!load_combat_snapshot(PyDict_GetItemString(dict,"runtime"),PyDict_GetItemString(dict,"state"),
     PyDict_GetItemString(dict,"descriptor"),PyDict_GetItemString(dict,"profile"),snapshot)){PyErr_Print();return 2;}
 assert(snapshot.state.health==100 && snapshot.gun_mount && snapshot.valid_pitch);
 assert(snapshot.fire_sequence==6);
 assert(snapshot.target_samples.size()==8 && snapshot.armor_shells.size()==1);
 assert(snapshot.armor_shells[0].kind=="HIGH_EXPLOSIVE" && snapshot.armor_shells[0].damage==300. && snapshot.armor_shells[0].piercing_near==150.);
 assert(snapshot.ammo.remaining.size()==1 && snapshot.ammo.remaining[0]==10);
 assert(snapshot.gun && snapshot.gun->elapsed==.5 && snapshot.gunner.reaction_seconds==.75);
 assert(snapshot.ledger.pending.size()==1 && snapshot.ledger.unpublished_steps.size()==1 && snapshot.ledger.authority_handoff_pending);
 offline_simulation::weapons::Store store;offline_simulation::ActorKey key;key.id=1;
 store.install(key,snapshot.gun_config,snapshot.ammo,snapshot.fire_sequence,1.);
 auto& actor=store.at(key);actor.gun=*snapshot.gun;actor.burst=snapshot.burst;actor.aim=snapshot.aim;
 assert(PyRun_SimpleString("def forbidden(*args,**kwargs):raise AssertionError('Offline Python business callback invoked')\nfor module_name,module in list(sys.modules.items()):\n if module_name.startswith('gui.mods.offline_lan_0922') and hasattr(module,'__dict__'):\n  for name,value in list(vars(module).items()):\n   if callable(value):setattr(module,name,forbidden)\nruntime.bot_rating=forbidden;runtime.bot_crew_level=forbidden;runtime._bot_repair_factor=forbidden\n")==0);
 CombatModeSnapshot mode;
 if(!load_combat_mode(PyDict_GetItemString(dict,"runtime"),PyDict_GetItemString(dict,"state"),
     PyDict_GetItemString(dict,"descriptor"),PyDict_GetItemString(dict,"profile"),mode)){PyErr_Print();return 4;}
 const double old_elapsed=actor.gun.elapsed;adopt_combat_mode(store,key,mode);
 assert(mode.base_repair_factor==.42 && mode.gun_config.fully_aimed_dispersion==.2*(1./1.043));
 assert(mode.target_samples.size()==8 && mode.water_sensor && (*mode.water_sensor)[1]==1.);
 assert(actor.gun.elapsed==old_elapsed && actor.ammo.remaining[0]==10);
 // The complete cold constructor also owns missing gun/ammo/burst state.
 assert(PyRun_SimpleString("runtime._gun_states.clear();runtime._ammo_states.clear();runtime._burst_states.clear()\nstate.pop('ammo_remaining',None);state.pop('next_shell_index',None);state.pop('ammo_reload_pending',None)\n")==0);
 for(int count=1;count<=5;++count)for(int capacity:{0,1,2,17,45})for(int spg=0;spg<2;++spg){
    std::string setup="descriptor['gun']['shots']=[descriptor['gun']['shots'][0]]*"+std::to_string(count)+"\ndescriptor['maxAmmo']="+std::to_string(capacity)+"\nprofile={'class_tag':'"+(spg?"SPG":"mediumTank")+"','shells':[{'index':i,'kind':('HIGH_EXPLOSIVE' if i%3==2 else 'ARMOR_PIERCING'),'penetration':100.+i*5} for i in range("+std::to_string(count)+")]}\nexpected=oracle['_bot_ammo_distribution'](descriptor,profile,"+std::to_string(count)+")\n";
    assert(PyRun_SimpleString(setup.c_str())==0);CombatSnapshot fresh;
    if(!load_combat_snapshot(PyDict_GetItemString(dict,"runtime"),PyDict_GetItemString(dict,"state"),PyDict_GetItemString(dict,"descriptor"),PyDict_GetItemString(dict,"profile"),fresh)){PyErr_Print();return 5;}
    auto expected=PyDict_GetItemString(dict,"expected");assert(fresh.ammo.remaining.size()==std::size_t(count));for(int i=0;i<count;++i)assert(fresh.ammo.remaining[i]==PyInt_AsLong(PyList_GetItem(expected,i)));
    assert(fresh.gun && fresh.burst.count==0 && fresh.gun->config.shell_count==count);
 }
 assert(PyRun_SimpleString("pool_descriptor=Obj();pool_descriptor.engine={'maxHealth':101.,'maxRegenHealth':0};pool_descriptor.miscAttrs={'engineHealthFactor':1.5}\n")==0);
 auto pool=stock_native_bot_descriptor::devicePool(PyDict_GetItemString(dict,"pool_descriptor"),"engineHealth");assert(pool.maximum==152. && pool.regeneration==75.);
 assert(PyRun_SimpleString("pool_descriptor._offline_critical_device_pools={'engineHealth':(91.,31.)}\n")==0);
 pool=stock_native_bot_descriptor::devicePool(PyDict_GetItemString(dict,"pool_descriptor"),"engineHealth");assert(pool.maximum==91. && pool.regeneration==31.);
 assert(!stock_native_bot_descriptor::devicePool(PyDict_GetItemString(dict,"pool_descriptor"),"gunHealth").maximum);
 assert(PyRun_SimpleString("descriptor['type']['crewRoles']=[('commander',),('gunner',),('gunner','commander'),('loader',)]\n")==0);
 auto roster=stock_native_bot_descriptor::crewRoster(PyDict_GetItemString(dict,"descriptor"));assert((roster==std::vector<std::string>{"commander","gunner1","gunner2","loader1"}));
 assert(PyRun_SimpleString("descriptor['type']['crewRoles']=[('commander',),('commander',)]\n")==0);assert(stock_native_bot_descriptor::crewRoster(PyDict_GetItemString(dict,"descriptor")).empty());
 PyObject* result=combat_snapshot_value(snapshot.state,snapshot.siege,actor,{});
 if(!result){PyErr_Print();return 3;}
 assert(PyInt_AsLong(PyDict_GetItemString(result,"health"))==100);
 Py_DECREF(result);
 std::cout<<"combat snapshot bootstrap passed\n";
 return 0;
}
