#pragma once
// Startup subsystem internals shared by its translation units.
#include "stock_native_battle_startup.hpp"
#include "stock_native_battle_startup_needs.hpp"
#include <cmath>
#include <string>

namespace native_battle {
namespace startup {
inline const Value& field(const Value& v, const char* name) { return v.resolved().get(name); }
inline bool has(const Value& v, const char* name) {
    const auto& r = v.resolved();
    return r.kind == Value::Kind::Object && r.object.count(name);
}
inline std::int64_t integer(const Value& v, std::int64_t fallback = 0) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Integer) return r.integer;
    if (r.kind == Value::Kind::Null) return fallback;
    return std::int64_t(r.numeric(double(fallback)));
}
inline std::string text(const Value& v, const std::string& fallback = std::string()) {
    const auto& r = v.resolved();
    return r.kind == Value::Kind::Text ? r.text : fallback;
}
inline Value fromPython(PyObject* o) { return cs::read(o); }
inline p::Ref toPython(const Value& v) { return p::Ref(cs::write(v)); }
inline p::Ref module(const char* name) {
    PyObject* loaded = PyDict_GetItemString(PyImport_GetModuleDict(), name);
    return loaded ? p::retain(loaded) : p::Ref(PyImport_ImportModule(name));
}
// getattr(o, name) or the #1513 capability error the pinned binding raised.
inline p::Ref need(PyObject* o, const char* name) {
    PyObject* v = o ? PyObject_GetAttrString(o, name) : nullptr;
    if (v) return p::Ref(v);
    PyErr_Clear();
    throw p::Error(std::string("required #1513 capability missing: ") + name);
}
inline p::Ref needCallable(PyObject* o, const char* name) {
    auto v = need(o, name);
    if (!PyCallable_Check(v.get())) throw p::Error(std::string("required #1513 capability is not callable: ") + name);
    return v;
}
constexpr double kPi = 3.14159265358979323846;
inline std::int64_t asInt(PyObject* v) { return p::wide(p::Ref(PyNumber_Int(v)).get()); }
inline double asFloat(PyObject* v) { return PyFloat_AsDouble(p::Ref(PyNumber_Float(v)).get()); }
inline p::Ref str(PyObject* v) { return p::Ref(PyObject_Str(v)); }
// Runs a cleanup step, keeping the first error for the caller.
template<class Body> void step(std::string& first, Body body) {
    try { body(); }
    catch (const std::exception& e) {
        const std::string error = describe(e);
        if (first.empty()) first = error;
    }
}
// cPickle.dumps(value) and zlib.compress(data), as #1513's arena updates expect.
inline p::Ref pickle(PyObject* value) { return py::call(module("cPickle").get(), "dumps", value); }
inline p::Ref compress(PyObject* data) { return py::call(module("zlib").get(), "compress", data); }

// Internal steps.
void createNativeBattleMap(Battle& b, const std::string& map_name);
void installGuiGuard(Battle& b);
void restoreGuiGuard(Battle& b);
std::pair<std::int64_t, std::int64_t> visibilityContract(Battle& b);
void configureSpaceVisibility(Battle& b, std::optional<std::int64_t> space_id = {});
void createEntities(Battle& b);
void waitForClientReady(Battle& b);
void finishEntityStartup(Battle& b);
void onClientReady(Battle& b);
void scheduleLobbyRestore(Battle& b, bool report_failure, bool lobby_report_after);
void cleanup(Battle& b);
void freezeGarage(Battle& b);
void prepareSpawn(Battle& b);
void prepareSpawnAssignments(Battle& b);
Value acceptedEffectiveParams(Battle& b, const Value& local_state);
// Server bridge: created once the binding exists; destroyed at cleanup.
p::Ref createServer(Battle& b);
void destroyServer(Battle& b);
bool flushClientReady(Battle& b);
std::int64_t addVehicleToArena(Battle& b, PyObject* properties, PyObject* position, PyObject* rotation, PyObject* mission_ids, std::int64_t crew_group);
void reportFailure(Battle& b, bool lobby_restored);
void ignoreLivePayload(Battle& b, const char* kind, const std::string& error, PyObject* message);
// fn(*positional, **keywords)
p::Ref callKw(PyObject* fn, std::initializer_list<PyObject*> positional, std::initializer_list<std::pair<const char*, PyObject*>> keywords);
namespace server { PyObject* radio(Battle& b); }
void closeTeamChat(Battle& b);
}
}
