// Host-only Python 2.7 module wrapper for stock_wire_codec tests.
#include "stock_wire_codec.hpp"
#include "stock_native_client_state.hpp"
#include <string>
#include <vector>

namespace {
// The owned-value codec path: Python dict -> Value -> frame, and frame -> Value -> Python dict.
PyObject* encodeNative(PyObject*, PyObject* message) {
    std::string out, error;
    if (!stock_wire::encode_value(stock_native_client_state::read(message), out, error)) {
        PyErr_SetString(PyExc_ValueError, error.c_str());
        return nullptr;
    }
    return PyString_FromStringAndSize(out.data(), static_cast<Py_ssize_t>(out.size()));
}
PyObject* decodeNative(PyObject*, PyObject* args) {
    const char* data; int size;
    if (!PyArg_ParseTuple(args, "s#", &data, &size)) return nullptr;
    stock_native_client_state::Value value;
    std::string error;
    if (!stock_wire::decode_value(reinterpret_cast<const unsigned char*>(data), static_cast<std::size_t>(size), value, error)) {
        PyErr_SetString(PyExc_ValueError, error.c_str());
        return nullptr;
    }
    return stock_native_client_state::write(value);
}
}

PyMODINIT_FUNC initwire_test(void) {
    static std::vector<PyMethodDef> table;
    for (PyMethodDef* m = stock_wire::methods(); m->ml_name; ++m) table.push_back(*m);
    table.push_back({"encode_native", encodeNative, METH_O, nullptr});
    table.push_back({"decode_native", decodeNative, METH_VARARGS, nullptr});
    table.push_back({nullptr, nullptr, 0, nullptr});
    Py_InitModule("wire_test", table.data());
}
