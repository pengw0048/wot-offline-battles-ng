#include <Python.h>
#include "stock_native_projectile_random.hpp"
namespace {
std::unique_ptr<stock_native_projectile::Random> random_owner(new stock_native_projectile::Random);
PyObject* install(PyObject*,PyObject* owner){if(!random_owner->install(owner))return nullptr;Py_RETURN_NONE;}
PyObject* uniform(PyObject*,PyObject*){try{return PyFloat_FromDouble(random_owner->uniform());}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject* gauss(PyObject*,PyObject*args){double mean,sigma;if(!PyArg_ParseTuple(args,"dd",&mean,&sigma))return nullptr;try{return PyFloat_FromDouble(random_owner->gauss(mean,sigma));}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyObject* close(PyObject*,PyObject*){random_owner.reset();Py_RETURN_NONE;}
PyMethodDef methods[]={{"close",close,METH_NOARGS,nullptr},{"install",install,METH_O,nullptr},{"uniform",uniform,METH_NOARGS,nullptr},{"gauss",gauss,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initprojectile_random_test(){Py_InitModule("projectile_random_test",methods);}
