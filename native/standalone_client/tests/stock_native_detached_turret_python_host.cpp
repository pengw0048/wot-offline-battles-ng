#include "stock_native_detached_turret_python_stub.hpp"
#include "stock_native_detached_turret_python.hpp"
#include <Python.h>
namespace t=stock_native_detached_turret;
struct Scene:stock_native_world::Scene {
    stock_native_world::QueryStatus segment(const t::Point&,const t::Point&,std::uint16_t,const stock_native_world::Filter&,stock_native_world::Hit&)override{return stock_native_world::QueryStatus::Miss;}
    stock_native_world::QueryStatus material(const t::Point&,const t::Point&,const t::Point&,stock_native_world::MaterialHit&)override{return stock_native_world::QueryStatus::Miss;}
    stock_native_world::QueryStatus water(const t::Point&,const t::Point&,double&)override{return stock_native_world::QueryStatus::Miss;}
    stock_native_world::Admission refresh(const stock_native_world::Key&,stock_native_world::Registry&)override{return stock_native_world::Admission::Pending;}
    bool unresolved(const stock_native_world::Key&,std::vector<t::Box>&)override{return false;}
    bool destroy(const stock_native_world::Destruction&)override{return false;}
    bool treeChunks(const std::vector<t::Box>&,std::vector<std::uint32_t>&)override{return false;}
    bool turretSweep(const stock_native_world::TurretSweep&)override{return true;}
};
static stock_native_bot::Runtime bots;
static stock_native_world::Registry registry;
static Scene scene;
static stock_native_bot_geometry::Vehicles vehicles;
static stock_native_bot::NativeServices services;
static std::unique_ptr<t::PythonOwner> owner;
PyObject* create(PyObject*,PyObject*battle){owner=std::make_unique<t::PythonOwner>(bots,registry,scene,vehicles,services,0);if(!owner->install(battle))return nullptr;Py_RETURN_TRUE;}
PyObject* queue(PyObject*,PyObject*args){PyObject*record,*entity,*state;double now;if(!PyArg_ParseTuple(args,"OOOd",&record,&entity,&state,&now))return nullptr;const auto value=owner->queue(record,entity,state,now);if(PyErr_Occurred())return nullptr;return PyBool_FromLong(value);}
PyObject* ingress(PyObject*,PyObject*args){PyObject*message;double now;if(!PyArg_ParseTuple(args,"Od",&message,&now))return nullptr;const auto value=owner->ingress(message,now);if(PyErr_Occurred())return nullptr;return PyBool_FromLong(value);}
PyObject* advance(PyObject*,PyObject*args){PyObject*battle;double now;if(!PyArg_ParseTuple(args,"Od",&battle,&now))return nullptr;return owner->advance(battle,now);}
PyObject* rows(PyObject*,PyObject*){return owner->rows();}
PyObject* errors(PyObject*,PyObject*){return owner->takeErrors();}
PyObject* counts(PyObject*,PyObject*){return Py_BuildValue("(II)",t::NativeWorld::queries,t::NativeWorld::transactions);}
PyObject* fail(PyObject*,PyObject*value){const auto truth=PyObject_IsTrue(value);if(truth<0)return nullptr;t::NativeWorld::fail=truth!=0;Py_RETURN_NONE;}
PyObject* clear(PyObject*,PyObject*){owner.reset();Py_RETURN_NONE;}
PyObject* stale(PyObject*,PyObject*){++registry.epoch;Py_RETURN_NONE;}
PyMethodDef methods[]={{"create",create,METH_O,nullptr},{"queue",queue,METH_VARARGS,nullptr},{"ingress",ingress,METH_VARARGS,nullptr},{"advance",advance,METH_VARARGS,nullptr},{"rows",rows,METH_NOARGS,nullptr},{"errors",errors,METH_NOARGS,nullptr},{"counts",counts,METH_NOARGS,nullptr},{"fail",fail,METH_O,nullptr},{"clear",clear,METH_NOARGS,nullptr},{"stale",stale,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initdetached_python_test(){Py_InitModule("detached_python_test",methods);}
