#include "stock_native_bot_motion_snapshot.hpp"
#include <Python.h>
static PyObject * inspect(PyObject *,PyObject * args)
{
    PyObject * runtime=nullptr,*state=nullptr,*descriptor=nullptr;
    if(!PyArg_ParseTuple(args,"OOO",&runtime,&state,&descriptor))return nullptr;
    stock_native_bot::MotionSnapshot value;
    if(!stock_native_bot::readMotionSnapshot(runtime,state,descriptor,value))return nullptr;
    const auto& s=value.state;const auto& d=value.descriptor;
    return Py_BuildValue("(ddddddddddddiiii)",s.position[0],s.yaw,s.turn_speed,s.hydraulic_pitch,
        s.straddle_reference.value_or(-99.),s.slope_basis_yaw.value_or(-99.),
        d.drive.mass,d.drive.power,d.body_minimum[1],d.body_maximum[1],value.ram.spall,value.ram.bonus,
        int(value.push_acknowledgements.size()),int(value.ram_motion.size()),
        int(value.memory.springs.size()),int(value.detailed_suspension));
}
static PyObject * inspectMode(PyObject *,PyObject * args){
 PyObject *runtime=nullptr,*state=nullptr,*descriptor=nullptr;if(!PyArg_ParseTuple(args,"OOO",&runtime,&state,&descriptor))return nullptr;
 stock_native_bot::MotionModeSnapshot value;if(!stock_native_bot::readMotionMode(runtime,state,descriptor,value))return nullptr;
 const auto& d=value.descriptor;return Py_BuildValue("(dddddddddddd)",d.drive.mass,d.drive.power,d.drive.ratio,d.drive.forward,d.drive.reverse,d.drive.friction,d.drive.brake,d.drive.rotation,d.drive.terrain[0],d.drive.terrain[1],d.drive.terrain[2],d.track_center);
}
static PyObject * inspectRaw(PyObject *,PyObject * descriptor){return stock_native_bot::readUnmodifiedDrive(descriptor);}
static PyMethodDef methods[]={{"inspect",inspect,METH_VARARGS,nullptr},{"inspect_mode",inspectMode,METH_VARARGS,nullptr},{"inspect_raw",inspectRaw,METH_O,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" void initmotion_snapshot_test(){Py_InitModule("motion_snapshot_test",methods);}
