#include "../bigworld/stock_native_player_environment_python.hpp"
#include <Python.h>
#include <cassert>
#include <cstdlib>
#include <string>
using namespace stock_native_player_environment;
struct Water:World {std::optional<Pose> bodyPose(const Player&)override{return {};}double waterDepth(const Point&)override{return 10.;}};
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();const char* source=std::getenv("WOT_GAMEPLAY_SOURCE");assert(source);auto path=PySys_GetObject(const_cast<char*>("path"));const auto scripts=std::string(source)+"/src/res/scripts/client";auto p=PyString_FromString(scripts.c_str());PyList_Insert(path,0,p);Py_DECREF(p);
 const char* code=R"PY(
import sys,types
gui=types.ModuleType('gui');gui.__path__=[sys.path[0]+'/gui'];sys.modules['gui']=gui
mods=types.ModuleType('gui.mods');mods.__path__=[sys.path[0]+'/gui/mods'];sys.modules['gui.mods']=mods
from gui.mods.offline_lan_0922 import critical_damage as cd,device_damage as dd
class O(object):pass
descriptor=O();descriptor.hull={'hitTester':{'bbox':((-1.,0.,-2.),(1.,2.,2.))}};descriptor.chassis={'hullPosition':(0.,1.,0.)};descriptor.type=O();descriptor.type.crewRoles=[('commander',),('driver',)]
dd.device_max_hp=lambda desc,name:100.
descriptor._offline_critical_device_pools=dict((name,(100.,50.)) for name in ('engineHealth','ammoBayHealth','fuelTankHealth','radioHealth','gunHealth','turretRotatorHealth','surveyingDeviceHealth','leftTrackHealth','rightTrackHealth'))
entity=O();entity.typeDescriptor=descriptor;entity.position=(0.,0.,0.);entity.devices_hp={'engineHealth':25.};entity._destroyed_devices=set();entity._critical_devices=set();entity._crew_ko=set(['driver']);entity.is_on_fire=True;entity._ammo_rack_death=False
entity.id=3;entity.health=100;entity.matrix=None;entity._crew_impaired=set();entity._fire_started=None;entity._fire_timer=0.;entity._is_killed=False;entity.getComponents=lambda:()
record={'kind':'player','network_id':3,'engine_id':33,'state':{'alive':True,'health':100,'input_seq':7,'x':0.,'y':0.,'z':0.}}
battle=O();battle._records={'player:3':record};battle._remote_factory=O();battle._remote_factory._vehicles={33:entity}
expected=cd.propose_drowning(entity)
def bomb(*args,**kwargs): raise AssertionError('offline descriptor callback')
dd.device_max_hp=bomb
cd._crew_roster=bomb
)PY";if(PyRun_SimpleString(code)){PyErr_Print();return 1;}auto globals=PyModule_GetDict(PyImport_AddModule("__main__"));auto battle=PyDict_GetItemString(globals,"battle");MetadataCache cache;Owner owner;Water water;std::optional<Batch> batch;
 if(!readAndAdvance(battle,.3,true,owner,water,cache,batch)){PyErr_Print();return 2;}assert(batch&&batch->observations.size()==1);auto out=observationsValue(*batch);if(!out){PyErr_Print();return 3;}PyDict_SetItemString(globals,"observations",out);Py_DECREF(out);if(PyRun_SimpleString("assert observations[0]['drowning_critical']==expected, (observations[0]['drowning_critical'],expected)\nassert entity.devices_hp=={'engineHealth':25.}\n")){PyErr_Print();return 4;}Py_Finalize();}
