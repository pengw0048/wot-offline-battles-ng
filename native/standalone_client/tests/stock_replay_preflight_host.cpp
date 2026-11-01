// Expose the production receive-thread recorder boundary to the replay fixture.
#define PY_SSIZE_T_CLEAN
#include "stock_wire_codec.hpp"
#include "stock_wire_replay.hpp"
#include "stock_wire_replica_store.hpp"
#include <vector>

namespace {
PyObject* recordFrame(PyObject*, PyObject* args) {
    PyObject* capsule;
    const char* data;
    Py_ssize_t size;
    double stamp;
    if (!PyArg_ParseTuple(args, "Os#d", &capsule, &data, &size, &stamp)) return nullptr;
    auto writer = stock_wire_replay::writer(capsule);
    if (!writer) return nullptr;
    return PyBool_FromLong(stock_wire_replay::record_frame(*writer, std::string(data, size), stamp));
}
PyMethodDef fixture[] = {{"record_frame", recordFrame, METH_VARARGS, nullptr}, {nullptr, nullptr, 0, nullptr}};
}

PyMODINIT_FUNC initoffline_client_wire_native() {
    static std::vector<PyMethodDef> methods;
    for (PyMethodDef* table : {stock_wire::methods(), stock_wire_replay::methods(), stock_wire::replica_methods(), fixture})
        for (PyMethodDef* method = table; method->ml_name; ++method) methods.push_back(*method);
    methods.push_back({nullptr, nullptr, 0, nullptr});
    Py_InitModule("offline_client_wire_native", methods.data());
}
