#include <Python.h>
#include "stock_native_bot_planning_snapshot.hpp"
using namespace stock_native_bot_planning;
namespace {
PyObject* inspect(PyObject*,PyObject*runtime){PlanningSnapshot snapshot;const auto refs=Py_REFCNT(runtime);if(!readPlanningSnapshot(runtime,snapshot))return nullptr;
    const auto&a=snapshot.director.agents.at(7);const auto&n=snapshot.actors.at(7);const auto&d=snapshot.descriptors.at(7);const auto&p=snapshot.perception.at(0);const auto&o=snapshot.orders.at(7);
    return Py_BuildValue("(iiidddddddiiidddLii)",snapshot.map.width,snapshot.map.height,int(snapshot.map.heights.size()),snapshot.map.heights[0],snapshot.bake.vehicle_half_width,snapshot.bake.maximum_clearance_radius,a.waypoints[1][0],a.waypoints[1][1],d.mass,d.speed_forward,int(p.checkpoints.available),int(p.checkpoints.turret_mount_available),int(o.drive.target.present),p.config.view_moving,p.config.camo_still,n.position[2],static_cast<PY_LONG_LONG>(snapshot.order_tokens.at(7)),int(snapshot.planner.size()),int(Py_REFCNT(runtime)==refs));
}
PyObject* orders(PyObject*,PyObject*source){std::map<std::int64_t,StrategicOrder> value;if(!readPlanningOrders(source,value))return nullptr;return PyInt_FromLong(long(value.size()));}
PyObject* routes(PyObject*,PyObject*runtime){PlanningSnapshot snapshot;if(!readPlanningSnapshot(runtime,snapshot))return nullptr;
    PyObject* result=PyDict_New();if(!result)return nullptr;
    for(const auto&team:snapshot.routes)for(const auto&route:team.second){
        PyObject* key=Py_BuildValue("(is)",team.first,route.first.c_str());PyObject* points=PyList_New(Py_ssize_t(route.second.size()));
        if(!key||!points){Py_XDECREF(key);Py_XDECREF(points);Py_DECREF(result);return nullptr;}
        for(std::size_t i=0;i<route.second.size();++i)PyList_SET_ITEM(points,Py_ssize_t(i),Py_BuildValue("(dd)",route.second[i][0],route.second[i][1]));
        if(PyDict_SetItem(result,key,points)<0){Py_DECREF(key);Py_DECREF(points);Py_DECREF(result);return nullptr;}Py_DECREF(key);Py_DECREF(points);
    }
    return result;
}
PyObject* perception(PyObject*,PyObject*args){PyObject *runtime,*state,*descriptor;if(!PyArg_ParseTuple(args,"OOO",&runtime,&state,&descriptor))return nullptr;stock_native_bot_perception::Actor actor;if(!readPerceptionMode(runtime,state,descriptor,actor))return nullptr;const auto&c=actor.config;return Py_BuildValue("(iiddddddddddddddd)",c.team,c.slot,c.half_length,c.half_width,c.view_moving,c.view_still,c.view_delay,c.camo_still,c.camo_moving,c.camo_add,c.camo_net_add,c.camo_multiplier,c.camo_net_multiplier,c.camo_shot,c.camo_net_delay,c.radio,c.relay);}
PyMethodDef methods[]={{"perception",perception,METH_VARARGS,nullptr},{"routes",routes,METH_O,nullptr},{"inspect",inspect,METH_O,nullptr},{"orders",orders,METH_O,nullptr},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initplanning_snapshot_test(){Py_InitModule("planning_snapshot_test",methods);}
