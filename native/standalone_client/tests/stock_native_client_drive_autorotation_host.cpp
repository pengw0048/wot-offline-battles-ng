#define WOT_CLIENT_INPUT_CONTROLS_HOST_BUILD
#include "stock_native_client_input_controls_python.cpp"
#include "stock_native_battle_test_lifetime.hpp"
namespace stock_native_client_drive {DriverAccess driverAccess(native_battle::Battle&){throw std::runtime_error("Unexpected driver access in autorotation producer");}}
namespace stock_native_client_input {bool sendCurrent(native_battle::Battle&,std::optional<bool>){throw std::runtime_error("Unexpected input publication in autorotation producer");}}
namespace {
PyObject*read(PyObject*,PyObject*b){try{native_battle::Battle battle;battle.engine.avatar=native_battle::borrow(stock_native_bot_python::value(b,"_avatar").get());return stock_native_bot_python::boolean(stock_native_client_input::autorotationEnabled(battle)).release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"read",read,METH_O,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_autorotation_test(){Py_InitModule("client_autorotation_test",methods);}

namespace native_battle {p::Ref Battle::entity(std::int64_t)const{throw p::Error("Unexpected native entity read in autorotation producer");}namespace projectiles {bool shoot(Battle&,double,double){throw p::Error("Unexpected shot in autorotation producer");}bool cancelNativeShotWait(Battle&){throw p::Error("Unexpected shot cancellation in autorotation producer");}}}
