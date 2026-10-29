#pragma once
#include <Python.h>
#include <cstdint>
namespace stock_native_client_landing {
// The existing client pending/queue/ACK dictionaries remain the sole ledger.
bool sendPending(PyObject*client,bool rebind=false);
std::int64_t sendObservation(PyObject*client,double impact,PyObject*contacts);
bool startQueued(PyObject*client);
PyObject*configure(PyObject*,PyObject*module);
PyMethodDef*methods();
}
