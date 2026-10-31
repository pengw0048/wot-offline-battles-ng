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



// Python semantics shared by the event and projectile ports.
inline p::Ref global(PyObject* g, const char* name) {
    PyObject* v = PyDict_GetItemString(g, name);
    if (!v) { PyErr_Format(PyExc_NameError, "global name '%s' is not defined", name); throw p::Error(name); }
    return p::retain(v);
}
inline bool member(PyObject* sequence, PyObject* v) {
    const int found = PySequence_Contains(sequence, v);
    if (found < 0) throw p::Error("Native membership test failed");
    return found != 0;
}
inline bool textIn(PyObject* v, std::initializer_list<const char*> options) {
    for (const char* option : options) if (equal(v, p::text(option).get())) return true;
    return false;
}
inline p::Ref str(PyObject* v) { return p::Ref(PyObject_Str(v)); }
[[noreturn]] inline void raise(const std::string& message) { throw p::Error(message); }
inline p::Ref module(const char* name) { return p::Ref(PyImport_ImportModule(name)); }
// The exception a Python `except Exception as error` would bind for the current failure.
inline p::Ref caught(const std::exception& e) {
    PyObject *type, *value, *trace;
    PyErr_Fetch(&type, &value, &trace);
    if (!type) return p::Ref(PyObject_CallFunction(PyExc_RuntimeError, const_cast<char*>("s"), e.what()));
    PyErr_NormalizeException(&type, &value, &trace);
    Py_XDECREF(type); Py_XDECREF(trace);
    return p::Ref(value);
}
inline std::string keyText(PyObject* k) { return p::string(str(k).get()); }
// '%s:%s' % (first, second)
inline p::Ref formatKey(PyObject* first, PyObject* second) {
    p::Ref values(PyTuple_Pack(2, first, second));
    return p::Ref(PyString_Format(p::text("%s:%s").get(), values.get()));
}
// int(value) with Python's int/long result.
inline p::Ref toInt(PyObject* value) { return p::Ref(PyNumber_Int(value)); }
// int(value) with `except (TypeError, ValueError[, OverflowError, IndexError])` falling back.
inline p::Ref intOr(PyObject* value, std::int64_t fallback) {
    PyObject* converted = PyNumber_Int(value);
    if (converted) return p::Ref(converted);
    if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError) &&
        !PyErr_ExceptionMatches(PyExc_OverflowError)) throw p::Error("int conversion failed");
    PyErr_Clear();
    return integer(fallback);
}
// `a, b, ... = value` with Python 2's iteration and error messages.
inline std::vector<p::Ref> unpack(PyObject* value, std::size_t count) {
    p::Ref iterator(PyObject_GetIter(value));
    std::vector<p::Ref> items;
    while (items.size() <= count) {
        PyObject* item = PyIter_Next(iterator.get());
        if (!item) { if (PyErr_Occurred()) throw p::Error("unpack"); break; }
        items.emplace_back(item);
    }
    if (items.size() != count) {
        if (items.size() > count) PyErr_SetString(PyExc_ValueError, "too many values to unpack");
        else PyErr_Format(PyExc_ValueError, "need more than %d value%s to unpack", int(items.size()), items.size() == 1 ? "" : "s");
        throw p::Error("unpack");
    }
    return items;
}
// max(0, value): the value only when it is greater.
inline p::Ref positive(p::Ref value) {
    const int greater = PyObject_RichCompareBool(value.get(), integer(0).get(), Py_GT);
    if (greater < 0) throw p::Error("Native comparison failed");
    return greater ? std::move(value) : integer(0);
}
inline bool greaterThanZero(PyObject* value) {
    const int greater = PyObject_RichCompareBool(value, integer(0).get(), Py_GT);
    if (greater < 0) throw p::Error("Native comparison failed");
    return greater != 0;
}
// int(event.get(name, 0) or 0), clamped below at zero.
inline p::Ref nonNegative(PyObject* event, const char* name) {
    auto raw = method(event, "get", p::text(name).get(), integer(0).get());
    return positive(toInt(truthy(raw) ? raw.get() : integer(0).get()));
}
// sys.stdout.write(text) through the live stream object.
inline void writeStdout(PyObject* text) {
    PyObject* stream = PySys_GetObject(const_cast<char*>("stdout"));
    if (!stream) throw p::Error("sys.stdout is unavailable");
    method(stream, "write", text);
}

// fn(*positional, k=v..., **mapping): the mapping is copied, then explicit keywords join it (CPython 2.7 ext_do_call).
inline p::Ref callWithMapping(PyObject* fn, PyObject* positional, std::initializer_list<std::pair<const char*, PyObject*>> named,
                              PyObject* mapping) {
    p::Ref keywords(PyDict_New());
    if (PyDict_Update(keywords.get(), mapping) < 0) throw p::Error("keyword mapping is not a mapping");
    for (const auto& entry : named) {
        if (PyDict_GetItemString(keywords.get(), entry.first)) {
            PyErr_Format(PyExc_TypeError, "%.200s%s got multiple values for keyword argument '%.200s'",
                         PyEval_GetFuncName(fn), PyEval_GetFuncDesc(fn), entry.first);
            throw p::Error("duplicate keyword");
        }
        put(keywords.get(), entry.first, p::retain(entry.second));
    }
    return p::Ref(PyObject_Call(fn, positional, keywords.get()));
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
// BattleRuntime._combat_event_source (staticmethod)
inline p::Ref combatEventSource(PyObject* event) {
    if (!PyDict_GetItemString(event, "source")) raise("ordered combat event has no source");
    auto source = get(event, "source");
    if (!textIn(source.get(), {"shot", "fire", "ram", "client_simulation", "player_left", "environment"}))
        raise("ordered combat event has invalid source: " + p::string(str(source.get()).get()));
    return source;
}
inline p::Ref recordsGet(PyObject* battle, PyObject* k) { return method(attr(battle, "_records").get(), "get", k); }
// self._run_optional_feature(name, fn, args)
inline p::Ref optionalFeature(PyObject* battle, const char* name, PyObject* fn, PyObject* call_args) {
    auto label = p::text(name);
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, "_run_optional_feature"))
        return callKw(battle, "_run_optional_feature", {label.get(), fn, call_args}, {});
    return dispatch(battle, "_run_optional_feature", "run_optional_feature", {}, {},
                    {label.get(), fn, call_args, Py_None, Py_True, Py_None});
}
// self._run_optional_feature(name, fn, args=call_args)
inline void optionalFeatureArgs(PyObject* battle, const char* name, PyObject* fn, PyObject* call_args) {
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, "_run_optional_feature")) {
        callKw(battle, "_run_optional_feature", {p::text(name).get(), fn}, {{"args", call_args}});
        return;
    }
    optionalFeature(battle, name, fn, call_args);
}
}
}
