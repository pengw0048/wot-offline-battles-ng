#include "stock_native_bot_physics_snapshot.hpp"
#include <Python.h>
using namespace stock_native_bot_physics;
static PyObject* inspect(PyObject*,PyObject*args){
    PyObject*battle,*state,*descriptor;int detailed;
    if(!PyArg_ParseTuple(args,"OOOi",&battle,&state,&descriptor,&detailed))return nullptr;
    stock_native_bot_turret::MovingDescriptor geometry;
    geometry.chassis.bounds={{{-1.,0.,-2.}},{{1.,1.,2.}}};geometry.hull.bounds=geometry.chassis.bounds;
    PhysicsSnapshot out;if(!readPhysicsSnapshot(battle,state,descriptor,geometry,detailed,out))return nullptr;
    return Py_BuildValue("(iiiddddidd)",int(out.geometry.pitch_hull_aiming),int(out.geometry.turret.pivot_available),int(out.geometry.turret.rotation_around_center),
        out.geometry.turret.track_center,out.config.travel_forward.value_or(-1.),out.config.travel_reverse.value_or(-1.),out.memory.last_world_impact_time,
        int(out.memory.pose_sample.has_value()),out.memory.pose_sample?(*out.memory.pose_sample)[2]:-1.,out.config.turret_yaw);
}
static PyObject* row(PyObject*,PyObject*raw){
    stock_native_bot_turret::Record out;if(!readDetachedTurretRow(raw,out))return nullptr;
    return Py_BuildValue("(iLdLddiiii)",int(out.owner.human),static_cast<PY_LONG_LONG>(out.owner.id),out.created_time_ms,
        static_cast<PY_LONG_LONG>(out.motion_sequence),out.duration,out.rest[1],int(out.landed),int(out.has_body),
        int(out.rest_attitude.has_value()),out.support?int(out.support->id):-1);
}
static PyMethodDef methods[]={{"inspect",inspect,METH_VARARGS,nullptr},{"row",row,METH_O,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" void initphysics_snapshot_test(){Py_InitModule("physics_snapshot_test",methods);}
