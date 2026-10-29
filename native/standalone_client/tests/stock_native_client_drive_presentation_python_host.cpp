#include "stock_native_client_drive_presentation_python.cpp"
namespace {namespace d=stock_native_client_drive;PyObject*captures=nullptr;
PyObject*mode(PyObject*,PyObject*args){if(PyList_Append(captures,args)<0)return nullptr;Py_RETURN_NONE;}
PyObject*apply(PyObject*,PyObject*args){PyObject*entity,*appearance;int power,flags;double left,right;if(!PyArg_ParseTuple(args,"OOiidd",&entity,&appearance,&power,&flags,&left,&right))return nullptr;try{d::applyTrackMode(entity,{power,flags});d::applyTrackScroll(appearance,left,right);Py_RETURN_NONE;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"capture_mode",mode,METH_VARARGS,""},{"capture_scroll",mode,METH_VARARGS,""},{"apply",apply,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
extern "C" void initclient_drive_visual_test(){auto*module=Py_InitModule("client_drive_visual_test",methods);captures=PyList_New(0);PyModule_AddObject(module,"captures",captures);}
