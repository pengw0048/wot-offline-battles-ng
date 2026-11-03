#include "../bigworld/stock_native_worker_probe.hpp"
#include "../bigworld/stock_wire_codec.hpp"
#include <Python.h>
namespace {
PyObject* reply(PyObject*,PyObject* input) {
    try {auto request=stock_native_client_state::read(input);auto pong=native_battle::link::workerProbeReply(request,7,42,{});
        std::string frame,error;if(!stock_wire::encode_value(pong,frame,error))throw std::runtime_error(error);
        auto decoded=stock_native_client_state::Value();if(!stock_wire::decode_value(reinterpret_cast<const unsigned char*>(frame.data()),frame.size(),decoded,error))throw std::runtime_error(error);
        return stock_native_client_state::write(decoded);}
    catch(const std::exception& e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
PyMethodDef methods[]={{"reply",reply,METH_O,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initworker_probe_test(){Py_InitModule("worker_probe_test",methods);}
