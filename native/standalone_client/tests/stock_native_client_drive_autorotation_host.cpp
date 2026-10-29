#define WOT_CLIENT_INPUT_CONTROLS_HOST_BUILD
#include "stock_native_client_input_controls_python.cpp"
namespace stock_native_client_drive {DriverAccess driverAccess(PyObject*){throw std::runtime_error("Unexpected driver access in autorotation producer");}}
namespace stock_native_client_input {bool sendCurrent(PyObject*,std::optional<bool>){throw std::runtime_error("Unexpected input publication in autorotation producer");}}
namespace {
PyObject*read(PyObject*,PyObject*b){try{return stock_native_bot_python::boolean(stock_native_client_input::autorotationEnabled(b)).release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"read",read,METH_O,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_autorotation_test(){Py_InitModule("client_autorotation_test",methods);}
