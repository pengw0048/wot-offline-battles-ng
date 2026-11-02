// Run the production replica's immutable-static reader without a game scene.
#include "stock_native_client_state.hpp"
#include "stock_native_bot_python.hpp"
#include <map>
#include <memory>

namespace {
namespace cs = stock_native_client_state;
namespace p = stock_native_bot_python;
using Value = cs::Value;
struct Remote {
    std::map<PyObject*, std::pair<cs::Handle, std::shared_ptr<const Value>>> static_versions;
};
cs::Handle borrow(PyObject* raw) { Py_INCREF(raw); return cs::Handle(raw); }
Value object() { Value result; result.kind = Value::Kind::Object; return result; }

// PRODUCTION_STATIC_READERS

struct Owner { Remote remote; Value rows; };
constexpr const char* capsule_name = "snapshot.static.owner";
Owner* owner(PyObject* capsule) {
    return static_cast<Owner*>(PyCapsule_GetPointer(capsule, capsule_name));
}
void destroy(PyObject* capsule) { delete owner(capsule); }
PyObject* capture(PyObject*, PyObject* raw) {
    try {
        auto value = std::make_unique<Owner>();
        value->rows = readRows(value->remote, raw);
        PyObject* capsule = PyCapsule_New(value.get(), capsule_name, destroy);
        if (capsule) value.release();
        return capsule;
    } catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyObject* refresh(PyObject*, PyObject* args) {
    PyObject *capsule, *raw;
    if (!PyArg_ParseTuple(args, "OO", &capsule, &raw)) return nullptr;
    auto* value = owner(capsule);
    if (!value) return nullptr;
    try { value->rows = readRows(value->remote, raw); Py_RETURN_NONE; }
    catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyObject* read(PyObject*, PyObject* capsule) {
    auto* value = owner(capsule);
    return value ? cs::write(value->rows) : nullptr;
}
PyObject* versions(PyObject*, PyObject* capsule) {
    auto* value = owner(capsule);
    return value ? PyLong_FromSize_t(value->remote.static_versions.size()) : nullptr;
}
PyMethodDef methods[] = {
    {"capture", capture, METH_O, nullptr}, {"refresh", refresh, METH_VARARGS, nullptr},
    {"read", read, METH_O, nullptr}, {"versions", versions, METH_O, nullptr}, {nullptr, nullptr, 0, nullptr}
};
}
PyMODINIT_FUNC initstatic_snapshot_native() { Py_InitModule("static_snapshot_native", methods); }
