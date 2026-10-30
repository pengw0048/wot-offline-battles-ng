#pragma once
// Python C-API helpers shared by the native ports of pinned BattleRuntime methods.
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <initializer_list>
#include <map>
#include <vector>
#include <new>
#include <string>
#include <utility>

namespace stock_native_client_lifecycle {
PyMethodDef* lifecycle_methods();
namespace py {
namespace p = stock_native_bot_python;

inline PyObject* fail(const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
inline bool truthy(const p::Ref& value) { return p::flag(value.get()); }
// A dict built like the pinned n-entry literal (BUILD_MAP presizes it), so iteration order matches.
inline p::Ref literal(Py_ssize_t entries) {
#if PY_MAJOR_VERSION < 3
    return p::Ref(_PyDict_NewPresized(entries));
#else
    return p::dict();
#endif
}
// Python 2 int (not long) where the pinned code produced one through int().
inline p::Ref integer(std::int64_t v) {
#if PY_MAJOR_VERSION < 3
    if (v >= LONG_MIN && v <= LONG_MAX) return p::Ref(PyInt_FromLong(long(v)));
#endif
    return p::integer(v);
}
inline p::Ref attr(PyObject* o, const char* name) { return p::Ref(PyObject_GetAttrString(o, name)); }
inline p::Ref optionalAttr(PyObject* o, const char* name) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (!v && PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}
// record.get(name, fallback) on a dict
inline p::Ref get(PyObject* d, const char* name, PyObject* fallback = Py_None) {
    PyObject* v = PyDict_Check(d) ? PyDict_GetItemString(d, name) : nullptr;
    return p::retain(v ? v : fallback);
}
inline void put(PyObject* d, const char* name, const p::Ref& v) { p::set(d, name, v); }
template<class... A> p::Ref method(PyObject* o, const char* name, A... args) {
    p::Ref fn = attr(o, name);
    return p::Ref(PyObject_CallFunctionObjArgs(fn.get(), args..., nullptr));
}
// Python int(value), or nothing on TypeError/ValueError (OverflowError too when asked).
inline bool pyInt(PyObject* v, std::int64_t& out, bool overflow = false) {
    PyObject* n = PyNumber_Int(v);
    if (!n) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError) ||
            (overflow && PyErr_ExceptionMatches(PyExc_OverflowError))) { PyErr_Clear(); return false; }
        throw p::Error("Native lifecycle integer conversion failed");
    }
    p::Ref held(n);
    out = PyLong_Check(n) ? PyLong_AsLongLong(n) : PyInt_AsLong(n);
    if (PyErr_Occurred()) { if (overflow && PyErr_ExceptionMatches(PyExc_OverflowError)) { PyErr_Clear(); return false; } throw p::Error("Native lifecycle integer overflow"); }
    return true;
}
// Module helper _number: float(value) with a finite fallback.
inline double number(PyObject* v, double fallback = 0.) {
    PyObject* f = PyNumber_Float(v);
    if (!f) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError)) { PyErr_Clear(); return fallback; }
        throw p::Error("Native lifecycle float conversion failed");
    }
    p::Ref held(f);
    const double out = PyFloat_AS_DOUBLE(f);
    return std::isfinite(out) ? out : fallback;
}
inline bool exactInteger(PyObject* v) { return !PyBool_Check(v) && (PyInt_Check(v) || PyLong_Check(v)); }
inline bool equal(PyObject* a, PyObject* b) {
    const int same = PyObject_RichCompareBool(a, b, Py_EQ);
    if (same < 0) throw p::Error("Native lifecycle comparison failed");
    return same != 0;
}
inline void replace(p::Ref& target, p::Ref value) { target.~Ref(); new (&target) p::Ref(std::move(value)); }
inline void setAttr(PyObject* o, const char* name, PyObject* v) { if (PyObject_SetAttrString(o, name, v) < 0) throw p::Error("Native lifecycle attribute write failed"); }
inline p::Ref copyDict(PyObject* v) { return v == Py_None || PyObject_IsTrue(v) <= 0 ? p::dict() : p::Ref(PyDict_Copy(v)); }
// Python int(value) where the pinned code lets the conversion error propagate unchanged.
inline std::int64_t strictInt(PyObject* v) {
    p::Ref n(PyNumber_Int(v));
    const std::int64_t out = PyLong_Check(n.get()) ? PyLong_AsLongLong(n.get()) : PyInt_AsLong(n.get());
    if (PyErr_Occurred()) throw p::Error("Native lifecycle integer overflow");
    return out;
}
inline p::Ref callKw(PyObject* o, const char* name, std::initializer_list<PyObject*> positional,
              std::initializer_list<std::pair<const char*, PyObject*>> keywords) {
    auto fn = attr(o, name);
    p::Ref args(PyTuple_New(Py_ssize_t(positional.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : positional) { Py_INCREF(v); PyTuple_SET_ITEM(args.get(), i++, v); }
    auto kw = p::dict();
    for (const auto& item : keywords) put(kw.get(), item.first, p::retain(item.second));
    return p::Ref(PyObject_Call(fn.get(), args.get(), kw.get()));
}
inline bool callableAttr(PyObject* o, const char* name, p::Ref& out) {
    replace(out, optionalAttr(o, name));
    return out.get() != Py_None && PyCallable_Check(out.get());
}
inline p::Ref key(const char* kind, PyObject* id) {
    p::Ref text(PyObject_Str(id));
    return p::text(std::string(kind) + ":" + p::string(text.get()));
}

// BattleRuntime._event_entity_key
inline p::Ref eventEntityKey(PyObject* event, bool attacker) {
    const char* bot = attacker ? "attacker_bot" : "target_bot";
    const char* player = attacker ? "attacker" : "target";
    auto b = get(event, bot);
    if (b.get() != Py_None) return key("bot", b.get());
    auto h = get(event, player);
    if (h.get() != Py_None) return key("player", h.get());
    return p::retain(Py_None);
}



// The native port registered under `port` in this module.
inline PyCFunction nativePort(const char* port) {
    for (PyMethodDef* m = lifecycle_methods(); m->ml_name; ++m)
        if (std::string(m->ml_name) == port) return m->ml_meth;
    throw p::Error(std::string("native lifecycle port is missing: ") + port);
}
// battle.<method>(*args, **keywords) with its Python signature, or its native port called directly with
// `native` (the port's complete argument list after battle) unless this instance overrides the method.
inline p::Ref dispatch(PyObject* battle, const char* name, const char* port, std::initializer_list<PyObject*> args,
                       std::initializer_list<std::pair<const char*, PyObject*>> keywords,
                       std::initializer_list<PyObject*> native) {
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, name)) return callKw(battle, name, args, keywords);
    p::Ref tuple(PyTuple_New(Py_ssize_t(native.size()) + 1));
    Py_INCREF(battle); PyTuple_SET_ITEM(tuple.get(), 0, battle);
    Py_ssize_t i = 1;
    for (PyObject* v : native) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    static std::map<std::string, PyCFunction> cache;
    auto& fn = cache[port];
    if (!fn) fn = nativePort(port);
    return p::Ref(fn(nullptr, tuple.get()));
}
// The common case: the port takes the Python arguments, plus `extra` (such as globals()).
inline p::Ref dispatch(PyObject* battle, const char* name, const char* port, std::initializer_list<PyObject*> args,
                       std::initializer_list<PyObject*> extra = {}) {
    std::vector<PyObject*> all(args);
    all.insert(all.end(), extra.begin(), extra.end());
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, name)) return callKw(battle, name, args, {});
    p::Ref tuple(PyTuple_New(Py_ssize_t(all.size()) + 1));
    Py_INCREF(battle); PyTuple_SET_ITEM(tuple.get(), 0, battle);
    Py_ssize_t i = 1;
    for (PyObject* v : all) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    static std::map<std::string, PyCFunction> cache;
    auto& fn = cache[port];
    if (!fn) fn = nativePort(port);
    return p::Ref(fn(nullptr, tuple.get()));
}
}
}
