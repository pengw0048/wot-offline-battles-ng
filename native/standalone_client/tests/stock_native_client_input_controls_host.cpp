#include <Python.h>
#include "stock_native_client_input_controls.hpp"
namespace input=stock_native_client_input;
namespace {
PyObject* movement(PyObject*,PyObject*args){int n;if(!PyArg_ParseTuple(args,"i",&n))return nullptr;return PyFloat_FromDouble(input::movementThrottle(n));}
PyObject* cruise(PyObject*,PyObject*args){int n;if(!PyArg_ParseTuple(args,"i",&n))return nullptr;return PyFloat_FromDouble(input::cruiseThrottle(n));}
PyObject*pack(const input::Aim&v){PyObject*point=v.point?Py_BuildValue("(ddd)",(*v.point)[0],(*v.point)[1],(*v.point)[2]):Py_None;if(!v.point)Py_INCREF(point);return Py_BuildValue("dddN",v.yaw,v.pitch,v.world_pitch,point);}
PyObject* track(PyObject*,PyObject*args){input::Point target{},position{},stabilised{};int relative;if(!PyArg_ParseTuple(args,"(ddd)i(ddd)(ddd)",&target[0],&target[1],&target[2],&relative,&position[0],&position[1],&position[2],&stabilised[0],&stabilised[1],&stabilised[2]))return nullptr;return pack(input::trackAim(target,relative!=0,position,stabilised));}
PyObject*align(PyObject*,PyObject*args){double hull,terrain,hydraulic,turret,gun;if(!PyArg_ParseTuple(args,"ddddd",&hull,&terrain,&hydraulic,&turret,&gun))return nullptr;return pack(input::alignedAim(hull,terrain,hydraulic,turret,gun));}
PyObject*packed(PyObject*,PyObject*args){double yaw,pitch;std::array<double,2>limits{};if(!PyArg_ParseTuple(args,"dd(dd)",&yaw,&pitch,&limits[0],&limits[1]))return nullptr;return PyLong_FromUnsignedLong(input::packedAim(yaw,pitch,limits));}
PyMethodDef methods[]={{"movement",movement,METH_VARARGS,""},{"cruise",cruise,METH_VARARGS,""},{"track",track,METH_VARARGS,""},{"align",align,METH_VARARGS,""},{"packed",packed,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
extern "C" void initoffline_client_controls_test(){Py_InitModule("offline_client_controls_test",methods);}
