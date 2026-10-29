#include "../bigworld/stock_native_bot_resource_lifecycle.hpp"
#include <Python.h>
#include <cassert>
using namespace stock_native_bot;
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();const char* setup=R"PY(
class O(object): pass
class Vehicle(object):
 def __getattribute__(self, name): raise AssertionError('Python Vehicle hook called: '+name)
 @property
 def isTurretDetached(self): raise AssertionError('Python turret property called')
class Ephemeral(object):
 @property
 def turretMatrix(self): raise AssertionError("hot matrix getter called")
 @property
 def gunMatrix(self): raise AssertionError("hot matrix getter called")
battle=O(); battle._remote_factory=O(); entity=Vehicle(); entity.isStarted=False; entity.health=-5; entity._Vehicle__turretDetachmentConfirmed=True; entity.appearance=Ephemeral(); vehicle_filter=O(); entity.filter=vehicle_filter; vehicle_filter.bodyMatrix=None; vehicle_filter.groundPlacingMatrix=None
battle._runtime=O(); battle._runtime.compatibility=O(); battle._runtime.compatibility._vehicle_property_overlays={id(entity):{'health':-13}}
battle._remote_factory._vehicles={7:entity}
def descriptor():
 d={}
 for k in ('chassis','hull','turret','gun'): d[k]={'hitTester':{'_ModelHitTester__bspModel':None}}
 return d
d=descriptor(); other=descriptor()
)PY";assert(PyRun_SimpleString(setup)==0);auto globals=PyModule_GetDict(PyImport_AddModule("__main__"));auto battle=PyDict_GetItemString(globals,"battle");auto entity=PyDict_GetItemString(globals,"entity"),descriptor=PyDict_GetItemString(globals,"d"),other=PyDict_GetItemString(globals,"other");
 auto got=readNativeEntity(battle,7);assert(got==entity);Py_DECREF(got);got=readNativeEntity(battle,8);assert(got==Py_None);Py_DECREF(got);
 ResourceStamp first,again,changed;assert(readResourceStamp(entity,{descriptor,other},first));assert(readResourceStamp(entity,{descriptor,other},again));assert(first==again);
 assert(PyObject_SetAttrString(entity,"isStarted",Py_True)==0);assert(readResourceStamp(entity,{descriptor,other},changed));assert(changed!=first);
 first=changed;assert(PyRun_SimpleString("other['hull']['hitTester']['_ModelHitTester__bspModel']=object()") == 0);assert(readResourceStamp(entity,{descriptor,other},changed));assert(changed!=first);
 first=changed;assert(PyRun_SimpleString("vehicle_filter.bodyMatrix=object()") == 0);assert(readResourceStamp(entity,{descriptor,other},changed));assert(changed==first);
 first=changed;assert(PyRun_SimpleString("entity._offline_critical_descriptor=object()") == 0);assert(readResourceStamp(entity,{descriptor,other},changed));assert(changed!=first);
 first=changed;assert(readResourceStamp(entity,{descriptor,other},changed));assert(changed==first);
 assert(readResourceStamp(entity,{descriptor,other},changed,battle));assert(changed.flags[1]);
 auto field=readNativeEntityField(battle,entity,"health");assert(field&&PyLong_AsLong(field)==-13);Py_DECREF(field);
 field=readNativeEntityField(nullptr,entity,"isTurretDetached");assert(field==Py_False);Py_DECREF(field);
 assert(readResourceStamp(Py_None,{nullptr,Py_None},changed));Py_Finalize();}
