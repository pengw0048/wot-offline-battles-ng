#include "../bigworld/stock_native_bot_human_snapshot.hpp"
#include <Python.h>
#include <cassert>
#include <cstdlib>
#include <string>
using namespace stock_native_bot;
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();const char* source=std::getenv("WOT_GAMEPLAY_SOURCE");assert(source);
 auto path=PySys_GetObject(const_cast<char*>("path"));const std::string scripts=std::string(source)+"/src/res/scripts/client";
 PyObject* p=PyString_FromString(scripts.c_str());assert(PyList_Insert(path,0,p)==0);Py_DECREF(p);
 const char* code=R"PY(
import sys,types
gui=types.ModuleType('gui');gui.__path__=[sys.path[0]+'/gui'];sys.modules['gui']=gui
mods=types.ModuleType('gui.mods');mods.__path__=[sys.path[0]+'/gui/mods'];sys.modules['gui.mods']=mods
from gui.mods.offline_lan_0922 import bot_runtime as br, native_control_core as nc, device_damage as dd, spotting, shot_geometry, destructibles_sensor
destructibles_sensor._vehicle_body_bbox=lambda descriptor: ((-1.,-2.,-3.),(1.,2.,3.),None)
real_effective=br._player_effective_params;real_projection=nc.NativeControl._project_actor_config
class NativeVectorLike(object):
 def __init__(self,*values): self.values=values
 def __getitem__(self,index): return self.values[index]
class Runtime(object):
 def _player_collision_profile(self,raw): return {'mass':20000.,'shape':(1.7,3.5,-.5,1.5),'physics':{'specificFriction':.2,'terrainResist':(1.,1.,1.)},'ram_profile':{'spall_coefficient':1.,'ramming_bonus':.1}}
 def _human_observation_target(self,raw): return {'class_tag':'mediumTank','armor':45.}
runtime=Runtime();raw={'id':3,'team':1,'slot':2,'vehicle':'vehicle','alive':True,'x':1.,'y':2.,'z':3.,'yaw':0.,'speed':2.,'world_pose':True,'participating':True,'fire_seq':7,
 'critical':{'crew_ko':['driver'],'fire':True,'destroyed':['radioHealth'],'devices':[]},'tank_pushes':[[9,2,1.,2.,3.,4.,5.]]}
descriptor={'physics':{'weight':23000.},'chassis':{'hullPosition':NativeVectorLike(0.,0.,0.)},'hull':{'turretPositions':[NativeVectorLike(0.,1.,0.)]},'gun':{},'turret':{'circularVisionRadius':400.},'radio':{'distance':400.}}
projection_snapshot={'physics':{'rotSpd':.4},'crew':{'members':[{'instance':name,'roles':[name],'skills':[]} for name in ('driver','commander')],'dynamic_spotting':{'crew':['driver','commander'],'states':dict(('%d:%d'%(mask,fire),dict(vision=1.-.1*bin(mask).count('1'),signal=1.,base_moving=.2,base_still=.1,invisibility_moving=(.3,.5),invisibility_still=(.7,.6))) for mask in range(4) for fire in (0,1))}},'spotting':dict(vision_factor=1.,binocular_factor=1.,binocular_delay=3.,camouflage_net_delay=3.,has_binoculars=False,has_camouflage_net=False),'camouflage':{'shot_factor':1.},'loadout':{'radio_factor':1.}}
projection_snapshot['physics'].update(mass=20000.,specificFriction=.2,terrainResist=(1.,1.,1.),speedFwd=18.,speedBwd=8.)
projection_snapshot['ramming']={'spall_coefficient':1.,'ramming_bonus':.1};projection_snapshot['version']=1
raw['effective_params']=projection_snapshot
descriptor['physics']['speedLimits']=(18.,8.)
descriptor['chassis']['hitTester']=type('Tester',(),{'bbox':((-1.,-2.,-3.),(1.,2.,3.))})()
descriptor['hull'].update(hitTester=type('Tester',(),{'bbox':((-.5,-1.,-2.),(.5,1.,2.))})(),primaryArmor=[45.])
descriptor['turret'].update(hitTester=type('Tester',(),{'bbox':((-.5,-.2,-.5),(.5,.5,.5))})(),gunPosition=NativeVectorLike(0.,0.,1.),surveyingDeviceHealth={'maxHealth':100.,'maxRegenHealth':50.})
descriptor['radio']['maxHealth']=100.
br._player_effective_params=lambda raw:projection_snapshot
dd.device_max_hp=lambda descriptor,name:100.
spotting.descriptor_check_points=lambda descriptor:[(0.,0.,0.)]*6
shot_geometry.vehicle_aim_samples=lambda descriptor:()
calls=[0]
def projection(self,source,tick):
 calls[0]+=1; c=source['critical']; loss=len(c['crew_ko']);status=len(c['destroyed'])+len(c['devices'])
 values=(source['team'],source['slot'],3.5,1.7,400.-loss*10.-status,500.-loss*10.-status,-1.,.1,.2,.3,.4,.5,.6,1.,-1.,100.,0.,0.)
 return values,False,False
nc.NativeControl._project_actor_config=projection
)PY";
 if(PyRun_SimpleString(code)){PyErr_Print();return 1;}auto main=PyModule_GetDict(PyImport_AddModule("__main__"));
 HumanDescriptor descriptor;if(!readHumanDescriptor(PyDict_GetItemString(main,"runtime"),PyDict_GetItemString(main,"raw"),PyDict_GetItemString(main,"descriptor"),descriptor)){PyErr_Print();return 2;}
 assert(descriptor.projections.size()==24 && descriptor.crew.size()==2);descriptor.target_available=true;descriptor.record_revision=42;descriptor.descriptor_revision=43;
 HumanIngress ingress;if(!readHumanInput(PyDict_GetItemString(main,"raw"),descriptor,ingress)){PyErr_Print();return 3;}
 assert(ingress.human.perception.config.view_moving==360. && ingress.human.perception.config.radio==100.);
 assert(ingress.human.perception.sample.fire_sequence==7 && ingress.human.perception.identity==42);
 assert(ingress.human.contacts.checkpoints_valid && ingress.human.contacts.checkpoints.size()==1);
 assert(ingress.human.contacts.checkpoints[0].actor==9 && ingress.human.contacts.checkpoints[0].sequence==2);
 assert(ingress.human.contacts.body.has(offline_contact::PositionFixed) && !ingress.human.contacts.body.has(offline_contact::Impulse));
 assert(ingress.human.contacts.body.vz==2. && ingress.target.target.id==3 && ingress.target.available);
 assert(ingress.human.contact_mass==23000. && ingress.human.body_minimum[1]==-2. && ingress.human.body_maximum[2]==3.);
 auto calls=PyDict_GetItemString(main,"calls");assert(PyInt_AsLong(PyList_GetItem(calls,0))==0);
 assert(PyRun_SimpleString("bad_descriptor=dict(descriptor, chassis={'hullPosition':(0.,)})")==0);
 HumanDescriptor bad;if(readHumanDescriptor(PyDict_GetItemString(main,"runtime"),PyDict_GetItemString(main,"raw"),PyDict_GetItemString(main,"bad_descriptor"),bad)){assert(false);}PyErr_Clear();assert(PyInt_AsLong(PyList_GetItem(calls,0))==0);
 const char* real=R"PY(
import os
sys.path.insert(0,os.getcwd()+'/tests')
from effective_params_fixture import effective_params
br._player_effective_params=lambda raw,tick=None:raw['effective_params'];nc.NativeControl._project_actor_config=real_projection
real_runtime=br.BotRuntime.__new__(br.BotRuntime);real_runtime._spotting_profiles={}
real_runtime._player_vehicle_profile=lambda raw,tick=None:{'descriptor':real_descriptor,'class_tag':'mediumTank','armor':45.}
real_runtime._player_collision_profile=runtime._player_collision_profile
real_runtime._human_observation_target=runtime._human_observation_target
real_descriptor=dict(descriptor,turret=dict(descriptor['turret'],circularVisionRadius=400.),miscAttrs={'circularVisionRadiusFactor':1.},radio={'distance':700.,'maxHealth':100.})
real_raw=dict(raw,effective_params=effective_params(),critical={'crew_ko':['commander'],'fire':True,'destroyed':['radioHealth'],
 'devices':[{'name':'surveyingDeviceHealth','hp':25.,'state':'critical'}]})
control=nc.NativeControl.__new__(nc.NativeControl);control.runtime=real_runtime
source=dict(real_raw,kind='human',network_id=3)
reference=control._project_actor_config(source,None)
)PY";
 if(PyRun_SimpleString(real)){PyErr_Print();return 4;}
 HumanDescriptor real_descriptor;
 if(!readHumanDescriptor(PyDict_GetItemString(main,"real_runtime"),PyDict_GetItemString(main,"real_raw"),PyDict_GetItemString(main,"real_descriptor"),real_descriptor)){PyErr_Print();return 5;}
 HumanIngress real_ingress;if(!readHumanInput(PyDict_GetItemString(main,"real_raw"),real_descriptor,real_ingress)){PyErr_Print();return 6;}
 auto reference=PyTuple_GetItem(PyDict_GetItemString(main,"reference"),0);
 assert(std::abs(real_ingress.human.perception.config.view_moving-PyFloat_AsDouble(PyTuple_GetItem(reference,4)))<1.e-10);
 assert(std::abs(real_ingress.human.perception.config.radio-PyFloat_AsDouble(PyTuple_GetItem(reference,15)))<1.e-10);
}
