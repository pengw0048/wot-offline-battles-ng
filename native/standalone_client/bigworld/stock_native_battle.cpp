// The native battle core: round lifetime, engine handles, vehicle lookup, optional features and callbacks.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle.hpp"
#include "stock_native_battle_subsystems.hpp"
#include <chrono>

namespace native_battle {
namespace {
std::unique_ptr<Battle> active;
std::uint64_t generations = 0;

// One BigWorld.callback target: runs its body only for the battle generation that scheduled it.
struct Scheduled {
    PyObject_HEAD
    std::uint64_t generation;
    std::function<void(Battle&)>* body;
};
void scheduledDealloc(PyObject* self) {
    delete reinterpret_cast<Scheduled*>(self)->body;
    PyObject_Del(self);
}
PyObject* scheduledCall(PyObject* self, PyObject*, PyObject*) {
    auto* s = reinterpret_cast<Scheduled*>(self);
    Battle* b = current();
    if (!b || b->generation != s->generation) Py_RETURN_NONE;
    try {
        (*s->body)(*b);
    } catch (const std::exception& e) {
        if (current() == b) b->fail(describe(e));
        else PyErr_Clear();
    }
    Py_RETURN_NONE;
}
PyTypeObject ScheduledType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.Scheduled"};
}

Battle::Battle() : generation(++generations), vehicles(std::make_shared<cs::State>()) {}
Battle::~Battle() = default;

Battle* current() { return active.get(); }
Battle& require() {
    if (!active) throw p::Error("no native battle is active");
    return *active;
}
void install(std::unique_ptr<Battle> battle) { active = std::move(battle); }
std::unique_ptr<Battle> retire() { return std::move(active); }

std::string describe(const std::exception& e) {
    if (!PyErr_Occurred()) return e.what();
    PyObject *type, *value, *trace;
    PyErr_Fetch(&type, &value, &trace);
    PyErr_NormalizeException(&type, &value, &trace);
    std::string text = e.what();
    if (value) {
        PyObject* raw = PyObject_Str(value);
        if (raw) { text = PyString_AsString(raw); Py_DECREF(raw); }
        else PyErr_Clear();
        if (type) {
            const char* name = reinterpret_cast<PyTypeObject*>(type)->tp_name;
            text = std::string(name ? name : "Error") + ": " + text;
        }
    }
    Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
    return text;
}

void log(const std::string& line) {
    PyObject* stream = PySys_GetObject(const_cast<char*>("stdout"));
    if (!stream) return;
    const std::string text = "[Offline LAN 0.9.22] " + line + "\n";
    PyObject* done = PyObject_CallMethod(stream, const_cast<char*>("write"), const_cast<char*>("s"), text.c_str());
    if (done) Py_DECREF(done);
    else PyErr_Clear();
}

double Battle::clock() const {
    if (engine.bigworld) {
        PyObject* raw = PyObject_CallMethod(engine.bigworld.get(), const_cast<char*>("time"), nullptr);
        if (raw) {
            const double value = PyFloat_AsDouble(raw);
            Py_DECREF(raw);
            if (!PyErr_Occurred()) return value;
        }
        PyErr_Clear();
    }
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

Actor* Battle::vehicle(const std::string& key) const { return vehicles->find(key); }
Actor* Battle::vehicleByEngine(std::int64_t engine_id) const {
    for (auto& entry : vehicles->actors)
        if (entry.second && entry.second->engine_id == engine_id) return entry.second.get();
    return nullptr;
}
Actor* Battle::localVehicle() const { return vehicle("player:" + std::to_string(local_player)); }

p::Ref Battle::entity(std::int64_t engine_id) const {
    // Native remotes the AOI facade hides (wrecks) come from their owner; BigWorld owns the rest.
    return remote::get(*this, engine_id);
}

void Battle::warn(const std::string& name, const std::string& error, bool disable) {
    if (disable) disabled_features.insert(name);
    if (!reported_failures.insert(name).second) return;
    log("optional " + name + (disable ? " disabled for this round: " : " degraded for this round: ") + error);
}

void Battle::fail(const std::string& message) {
    if (phase == Phase::Failed || phase == Phase::Stopped || phase == Phase::Leaving) return;
    error = message;
    log("battle failed: " + message);
    teardownAfterFailure(*this);
}

// BigWorld.callback(delay, body) fenced by this battle's generation.
p::Ref schedule(Battle& b, double delay, std::function<void(Battle&)> body) {
    if (!ScheduledType.tp_call) {
        ScheduledType.tp_basicsize = sizeof(Scheduled);
        ScheduledType.tp_dealloc = scheduledDealloc;
        ScheduledType.tp_call = scheduledCall;
        ScheduledType.tp_flags = Py_TPFLAGS_DEFAULT;
        if (PyType_Ready(&ScheduledType) < 0) throw p::Error("native scheduled callback type failed");
    }
    auto* invoke = PyObject_New(Scheduled, &ScheduledType);
    if (!invoke) throw p::Error("native scheduled callback allocation failed");
    invoke->generation = b.generation;
    invoke->body = new std::function<void(Battle&)>(std::move(body));
    p::Ref target(reinterpret_cast<PyObject*>(invoke));
    return p::Ref(PyObject_CallMethod(b.engine.bigworld.get(), const_cast<char*>("callback"), const_cast<char*>("dO"), delay, target.get()));
}
void cancel(Battle& b, Object& id) {
    if (!id || id.get() == Py_None) return;
    PyObject* done = PyObject_CallMethod(b.engine.bigworld.get(), const_cast<char*>("cancelCallback"), const_cast<char*>("O"), id.get());
    if (done) Py_DECREF(done);
    else PyErr_Clear();
    id = Object();
}
}
