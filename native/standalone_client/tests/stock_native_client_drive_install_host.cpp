#include "stock_native_client_drive_install.cpp"
namespace stock_native_bot_descriptor {
Bounds bodyBounds(PyObject*){throw std::runtime_error("Unexpected descriptor access in initial pose consumer");}
Bounds hullBounds(PyObject*){throw std::runtime_error("Unexpected descriptor access in initial pose consumer");}
std::array<double,4>chassisShape(PyObject*){throw std::runtime_error("Unexpected descriptor access in initial pose consumer");}
}
namespace stock_native_bot_motion {void validate(const Descriptor&){throw std::runtime_error("Unexpected physical validation in initial pose consumer");}}
namespace {
PyObject*hazards(PyObject*,PyObject*value){try{const auto result=stock_native_client_drive::readPlayerHazards(value);PyObject*out=PyList_New(Py_ssize_t(result.size()));if(!out)return nullptr;for(std::size_t i=0;i<result.size();++i)PyList_SET_ITEM(out,Py_ssize_t(i),PyLong_FromUnsignedLong(result[i]));return out;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*initial(PyObject*,PyObject*args){try{PyObject*b;int admitted;if(!PyArg_ParseTuple(args,"Oi",&b,&admitted))return nullptr;stock_native_client_state::Actor actor;actor.local=true;actor.has_current=admitted!=0;actor.current={10.,20.,30.,.5,.6,.7,.8,.9};const auto value=stock_native_client_drive::readPlayerInitialState(b,actor);return Py_BuildValue("(ddddddd)",value.position[0],value.position[1],value.position[2],value.yaw,value.pitch,value.roll,value.speed);}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"initial",initial,METH_VARARGS,""},{"hazards",hazards,METH_O,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_drive_install_test(){Py_InitModule("client_drive_install_test",methods);}
