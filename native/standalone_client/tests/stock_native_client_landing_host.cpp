#include "stock_native_client_landing.hpp"
#include "stock_native_bot_python.hpp"
namespace p=stock_native_bot_python;
namespace {
p::Ref clone(PyObject*v){if(PyDict_Check(v)){auto out=p::dict();Py_ssize_t i=0;PyObject*k,*value;while(PyDict_Next(v,&i,&k,&value)){auto copy=clone(value);if(PyDict_SetItem(out.get(),k,copy.get())<0)throw p::Error("Fixture clone failed");}return out;}if(PyList_Check(v)||PyTuple_Check(v)){auto out=p::list();for(Py_ssize_t i=0;i<PySequence_Size(v);++i){auto value=p::item(v,i);p::append(out.get(),clone(value.get()));}return out;}return p::retain(v);}
}
namespace stock_native_client_input {
bool enqueue(PyObject*client,PyObject*message){auto attempts=p::value(client,"_attempts");p::append(attempts.get(),clone(message));return p::truth(client,"_send_ok");}
}
PyMODINIT_FUNC initclient_landing_test(){Py_InitModule("client_landing_test",stock_native_client_landing::methods());}
