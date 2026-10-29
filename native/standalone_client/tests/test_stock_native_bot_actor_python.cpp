#include "../bigworld/stock_native_bot_publication_python.hpp"
#include <Python.h>
#include <cassert>
using namespace stock_native_bot;
int main(){Py_DontWriteBytecodeFlag=1;Py_Initialize();Actor actor;actor.motion.id=3;actor.motion.position={1.,2.,3.};actor.motion.alive=true;actor.team=1;actor.combat.health=90;actor.combat.maximum_health=100;actor.combat.alive=true;actor.weapon.aim_yaw=.2;actor.shot_angles=std::array<double,2>{.4,.5};
 step::EquipmentState equipment;equipment.uses_left=2;equipment.ready_at=17.;equipment.active=true;equipment.auto_pending_since=8.;equipment.ai_pending_since=9.;actor.equipment.push_back(equipment);
 offline_simulation::weapons::GunConfig config;config.fully_aimed_dispersion=.1;config.aiming_time=1.;config.reload_full=1.;config.clip_size=1;config.shell_count=1;config.burst_count=1;offline_simulation::weapons::ActorState weapon(config);
 auto previous=PyDict_New();auto list=PyList_New(0);auto row=PyDict_New();auto identity=PyString_FromString("medkit");PyDict_SetItemString(row,"equipment",identity);Py_DECREF(identity);PyList_Append(list,row);Py_DECREF(row);PyDict_SetItemString(previous,"equipment_states",list);Py_DECREF(list);PyDict_SetItemString(previous,"ground_height",Py_True);
 auto result=packNativeActor(actor,weapon,nullptr,previous,10.);if(!result)PyErr_Print();assert(result);assert(!PyDict_GetItemString(previous,"ground_height"));assert(PyFloat_AsDouble(PyDict_GetItemString(result,"shot_yaw"))==.4);assert(PyInt_AsLong(PyDict_GetItemString(result,"health"))==90);
 auto item=PyList_GetItem(PyDict_GetItemString(result,"equipment_states"),0);assert(PyFloat_AsDouble(PyDict_GetItemString(item,"cooldownTimeLeft"))==7.);assert(PyFloat_AsDouble(PyDict_GetItemString(item,"autoPendingElapsed"))==2.);assert(PyFloat_AsDouble(PyDict_GetItemString(item,"aiPendingElapsed"))==1.);assert(PyDict_GetItemString(item,"active")==Py_True);assert(PyString_Check(PyDict_GetItemString(item,"equipment")));
 Py_DECREF(result);Py_DECREF(previous);Py_Finalize();}
