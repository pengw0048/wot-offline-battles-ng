// Host module: the native replica decode and merge path, for differential tests.
#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "../bigworld/stock_native_client_state.cpp"
#include "../bigworld/stock_wire_codec.cpp"
#include "../bigworld/stock_wire_replica_store.cpp"

namespace {
PyObject* merge(PyObject*, PyObject* frames) {
    stock_wire::ReplicaStore store;
    PyObject* out = PyList_New(0);
    for (Py_ssize_t i = 0; i < PyList_Size(frames); ++i) {
        char* data; Py_ssize_t size;
        if (PyString_AsStringAndSize(PyList_GetItem(frames, i), &data, &size) < 0) return nullptr;
        stock_native_client_state::Value message; std::string error;
        if (!stock_wire::decode_value(reinterpret_cast<const unsigned char*>(data), std::size_t(size), message, error)) {
            PyErr_SetString(PyExc_ValueError, error.c_str()); return nullptr;
        }
        try {
            auto snapshot = store.apply(std::move(message));
            PyObject* row = stock_native_client_state::write(*snapshot);
            PyList_Append(out, row); Py_DECREF(row);
        } catch (const std::exception& failure) {
            PyErr_SetString(PyExc_ValueError, failure.what()); return nullptr;
        }
    }
    return out;
}
PyMethodDef methods[] = {{"merge", merge, METH_O, ""}, {nullptr, nullptr, 0, nullptr}};
}
PyMODINIT_FUNC initwire_replica_host() { Py_InitModule("wire_replica_host", methods); }
