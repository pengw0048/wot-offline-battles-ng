#pragma once
// The native battle: one per round. It owns the state the pinned BattleRuntime kept in Python attributes;
// stock #1513 objects (Avatar, entities, GUI, BigWorld) stay Python objects and are held as handles.
#include "stock_native_bot_python.hpp"
#include "stock_native_client_state.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <new>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace native_battle {
namespace p = stock_native_bot_python;
namespace cs = stock_native_client_state;
using cs::Actor;
using cs::Point;
using cs::Pose;
using cs::Value;
// A nullable owned Python reference that battle state can hold and replace.
using Object = cs::Handle;
inline Object own(p::Ref value) { return Object(value.release()); }
inline Object borrow(PyObject* value) { Py_XINCREF(value); return Object(value); }
// Rebinds a p::Ref (it has no assignment).
inline void replace(p::Ref& target, p::Ref value) { target.~Ref(); new (&target) p::Ref(std::move(value)); }

// Calls into stock Python objects. Each throws p::Error with the Python exception set on failure.
namespace py {
inline p::Ref attr(PyObject* o, const char* name) { return p::Ref(PyObject_GetAttrString(o, name)); }
// getattr(o, name, None): only AttributeError means "absent".
inline p::Ref optional(PyObject* o, const char* name) {
    PyObject* v = o ? PyObject_GetAttrString(o, name) : nullptr;
    if (v) return p::Ref(v);
    if (o && !PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(Py_None);
}
inline bool callable(const p::Ref& v) { return v.get() != Py_None && PyCallable_Check(v.get()); }
template<class... A> p::Ref call(PyObject* o, const char* name, A... args) {
    auto fn = attr(o, name);
    return p::Ref(PyObject_CallFunctionObjArgs(fn.get(), static_cast<PyObject*>(args)..., nullptr));
}
template<class... A> p::Ref invoke(PyObject* fn, A... args) {
    return p::Ref(PyObject_CallFunctionObjArgs(fn, static_cast<PyObject*>(args)..., nullptr));
}
inline void set(PyObject* o, const char* name, PyObject* v) {
    if (PyObject_SetAttrString(o, name, v) < 0) throw p::Error(name);
}
inline bool truth(PyObject* v) { return p::flag(v); }
// The low 64 bits of a Python int or long (2.7's PyLong_AsUnsignedLongLongMask rejects a plain int).
inline std::uint64_t bits(PyObject* v) {
    p::Ref n(PyNumber_Long(v));
    const auto out = PyLong_AsUnsignedLongLongMask(n.get());
    if (PyErr_Occurred()) throw p::Error("integer mask is invalid");
    return out;
}
}

enum class Phase { Idle, Loading, Running, Finished, Failed, Stopped, Leaving };

// Stock objects the battle drives. #1513 owns them, so they remain Python objects.
struct Engine {
    Object shell;           // BattleRuntime: the glue object stock hooks and BigWorld callbacks reach
    Object modules;         // runtime bundle: bigworld, constants, math, vehicles, compatibility, ...
    Object bigworld, constants, math, vehicles, compatibility;
    Object client;          // LAN client and its native transport
    Object avatar, binding, server, remote_factory;
};

struct Vehicles;
struct Events;
struct Projectiles;
struct LocalVehicle;
struct LocalGun;
struct Visibility;
struct World;
struct BotAuthority;
struct Diagnostics;
struct Startup;
struct Critical;
struct Remote;
struct Feedback;
struct Params;
struct Compat;

struct Battle {
    Battle();
    ~Battle();
    std::uint64_t generation = 0;
    Phase phase = Phase::Idle;
    bool worker = false, replay = false, live = false;
    Value start, config, result;
    std::string error;
    std::int64_t local_player = 0, round_id = 0;
    double last_frame = -1., prebattle_deadline = -1.;
    Engine engine;
    std::shared_ptr<cs::State> vehicles;
    std::unique_ptr<Vehicles> roster;
    std::set<std::string> disabled_features, reported_failures;
    std::unique_ptr<Events> events;
    std::unique_ptr<Projectiles> projectiles;
    std::unique_ptr<LocalVehicle> local;
    std::unique_ptr<LocalGun> gun;
    std::unique_ptr<Visibility> visibility;
    std::unique_ptr<World> world;
    std::unique_ptr<BotAuthority> bots;
    std::unique_ptr<Diagnostics> diagnostics;
    std::unique_ptr<Startup> startup;
    std::unique_ptr<Critical> critical;
    std::unique_ptr<Remote> remote;
    std::unique_ptr<Feedback> feedback;
    std::unique_ptr<Params> params;
    std::unique_ptr<Compat> compat;

    bool running() const { return phase == Phase::Running; }
    double clock() const;
    Actor* vehicle(const std::string& key) const;
    Actor* vehicleByEngine(std::int64_t engine_id) const;
    Actor* localVehicle() const;
    // A stock entity by engine id: the remote factory's, else BigWorld's.
    p::Ref entity(std::int64_t engine_id) const;
    // An optional presentation feature: false once it failed this round; a failure disables it and is logged once.
    bool featureEnabled(const std::string& name) const { return !disabled_features.count(name); }
    template<class Body> bool feature(const std::string& name, Body body, bool disable = true);
    void warn(const std::string& feature, const std::string& error, bool disable = true);
    // Ends the round as a failure: logs, cancels callbacks and starts teardown through the shell.
    void fail(const std::string& error);
};

// The battle of the current round, or null between rounds.
Battle* current();
Battle& require();
void install(std::unique_ptr<Battle> battle);
std::unique_ptr<Battle> retire();

// BigWorld.callback(delay, body), fenced by the scheduling battle's generation; returns the callback id.
p::Ref schedule(Battle& battle, double delay, std::function<void(Battle&)> body);
void cancel(Battle& battle, Object& callback_id);
// Teardown after Battle::fail; owned by the startup subsystem.
void teardownAfterFailure(Battle& battle);

// Python's current exception (or the C++ message) as text, clearing the Python error.
std::string describe(const std::exception& e);
// Writes one "[Offline LAN 0.9.22] ..." line to python.log through sys.stdout.
void log(const std::string& line);

template<class Body> bool Battle::feature(const std::string& name, Body body, bool disable) {
    if (!featureEnabled(name)) return false;
    try { body(); return true; }
    catch (const std::exception& e) { warn(name, describe(e), disable); return false; }
}
}
