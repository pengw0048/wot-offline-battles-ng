#include "stock_native_client_turret_presentation.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
#include "stock_native_bot_python.hpp"
namespace p=stock_native_bot_python;namespace t=stock_native_client_turret_presentation;
const char*name="turret.presentation.test";
void cleanup(PyObject*c){delete static_cast<t::Buffer*>(PyCapsule_GetPointer(c,name));}
t::Tuning tuning(){return {9.81,.04,.06,.09,.005};}
t::Buffer&buffer(PyObject*c){return *static_cast<t::Buffer*>(PyCapsule_GetPointer(c,name));}
// The decoded wire row; a malformed row raises ValueError.
bool readWireRow(PyObject*raw,t::Row&row){try{std::string error;if(stock_native_detached_turret::readRow(stock_native_client_state::read(raw),row,&error))return true;PyErr_SetString(PyExc_ValueError,error.c_str());}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());}return false;}
PyObject*create(PyObject*,PyObject*){return PyCapsule_New(new t::Buffer(tuning()),name,cleanup);}
p::Ref pack(const t::Pose&pose){auto out=p::dict();p::set(out.get(),"position",p::numbers(pose.position));p::set(out.get(),"attitude",p::numbers(pose.attitude));p::set(out.get(),"body",p::boolean(bool(pose.body)));if(pose.body){p::set(out.get(),"impact_serial",p::integer(pose.body->impact_serial));p::set(out.get(),"sleeping",p::boolean(pose.body->sleeping));}return out;}
PyObject*push(PyObject*,PyObject*args){PyObject*c,*raw;double now,age;if(!PyArg_ParseTuple(args,"OOdd",&c,&raw,&now,&age))return nullptr;t::Row row;if(!readWireRow(raw,row))return nullptr;try{return p::boolean(buffer(c).push(row,now,age)).release();}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject*pose(PyObject*,PyObject*args){PyObject*c;double now;if(!PyArg_ParseTuple(args,"Od",&c,&now))return nullptr;try{auto&b=buffer(c);const auto value=b.pose(now);if(!value)Py_RETURN_NONE;auto out=pack(*value);p::set(out.get(),"cursor",p::number(*b.cursor()));p::set(out.get(),"delay",p::number(b.delay()));p::set(out.get(),"samples",p::integer(b.samples().size()));return out.release();}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject*arc(PyObject*,PyObject*args){PyObject*raw;double elapsed;if(!PyArg_ParseTuple(args,"Od",&raw,&elapsed))return nullptr;t::Row row;if(!readWireRow(raw,row))return nullptr;try{return pack(t::poseAt(row,elapsed,tuning())).release();}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyMethodDef methods[]={{"create",create,METH_NOARGS,nullptr},{"push",push,METH_VARARGS,nullptr},{"pose",pose,METH_VARARGS,nullptr},{"arc",arc,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initturret_presentation_test(){Py_InitModule("turret_presentation_test",methods);}
