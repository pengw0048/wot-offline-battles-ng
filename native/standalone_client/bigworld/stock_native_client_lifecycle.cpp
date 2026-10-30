// Remote vehicle lifecycle and presentation edges, ported from the pinned battle runtime.
// Records are still the runtime's shared Python dicts until every writer is native.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script.hpp"
#endif
#include "stock_native_bot_python.hpp"
#include <climits>
#include <cmath>
#include <string>

namespace stock_native_client_lifecycle {
namespace {
namespace p = stock_native_bot_python;

PyObject* fail(const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
bool truthy(const p::Ref& value) { return p::flag(value.get()); }
// Python 2 int (not long) where the pinned code produced one through int().
p::Ref integer(std::int64_t v) {
#if PY_MAJOR_VERSION < 3
    if (v >= LONG_MIN && v <= LONG_MAX) return p::Ref(PyInt_FromLong(long(v)));
#endif
    return p::integer(v);
}
p::Ref attr(PyObject* o, const char* name) { return p::Ref(PyObject_GetAttrString(o, name)); }
p::Ref optionalAttr(PyObject* o, const char* name) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (!v && PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}
// record.get(name, fallback) on a dict
p::Ref get(PyObject* d, const char* name, PyObject* fallback = Py_None) {
    PyObject* v = PyDict_Check(d) ? PyDict_GetItemString(d, name) : nullptr;
    return p::retain(v ? v : fallback);
}
void put(PyObject* d, const char* name, const p::Ref& v) { p::set(d, name, v); }
template<class... A> p::Ref method(PyObject* o, const char* name, A... args) {
    p::Ref fn = attr(o, name);
    return p::Ref(PyObject_CallFunctionObjArgs(fn.get(), args..., nullptr));
}
// Python int(value), or nothing on TypeError/ValueError (OverflowError too when asked).
bool pyInt(PyObject* v, std::int64_t& out, bool overflow = false) {
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
double number(PyObject* v, double fallback = 0.) {
    PyObject* f = PyNumber_Float(v);
    if (!f) {
        if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError)) { PyErr_Clear(); return fallback; }
        throw p::Error("Native lifecycle float conversion failed");
    }
    p::Ref held(f);
    const double out = PyFloat_AS_DOUBLE(f);
    return std::isfinite(out) ? out : fallback;
}
p::Ref key(const char* kind, PyObject* id) {
    p::Ref text(PyObject_Str(id));
    return p::text(std::string(kind) + ":" + p::string(text.get()));
}

// BattleRuntime._event_entity_key
p::Ref eventEntityKey(PyObject* event, bool attacker) {
    const char* bot = attacker ? "attacker_bot" : "target_bot";
    const char* player = attacker ? "attacker" : "target";
    auto b = get(event, bot);
    if (b.get() != Py_None) return key("bot", b.get());
    auto h = get(event, player);
    if (h.get() != Py_None) return key("player", h.get());
    return p::retain(Py_None);
}

// BattleRuntime._remote_visual_components
std::pair<bool, bool> remoteVisualComponents(PyObject* record) {
    if (!PyDict_GetItemString(record, "world_marker_started") && !PyDict_GetItemString(record, "minimap_started")) {
        const bool legacy = truthy(get(record, "visual_started", Py_False));
        return {legacy, legacy};
    }
    return {truthy(get(record, "world_marker_started", Py_False)), truthy(get(record, "minimap_started", Py_False))};
}
// BattleRuntime._store_remote_visual_components
void storeRemoteVisualComponents(PyObject* record, bool world, bool minimap) {
    put(record, "world_marker_started", p::boolean(world));
    put(record, "minimap_started", p::boolean(minimap));
    put(record, "visual_started", p::boolean(world || minimap));
}

PyObject* pendingCombat(PyObject*, PyObject* args) {
    PyObject *battle, *record, *kinds;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &record, &kinds)) return nullptr;
    try {
        auto journal = attr(battle, "_event_journal"), records = attr(battle, "_records");
        p::Ref iterator(PyObject_GetIter(journal.get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref event(raw);
            auto kind = get(event.get(), "kind");
            const int member = PySequence_Contains(kinds, kind.get());
            if (member < 0) throw p::Error("Native combat kind lookup failed");
            if (!member || truthy(get(event.get(), "cosmetic_only", Py_False))) continue;
            auto target = eventEntityKey(event.get(), false);
            PyObject* found = target.get() == Py_None ? nullptr : PyDict_GetItem(records.get(), target.get());
            if (found == record) Py_RETURN_TRUE;
        }
        if (PyErr_Occurred()) return nullptr;
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* vehicleStatistics(PyObject*, PyObject* args) {
    PyObject *battle, *record, *state;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &record, &state)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        auto raw = get(state, "frags", integer(0).get());
        std::int64_t frags = 0;
        if (!pyInt(raw.get(), frags)) frags = 0;
        auto binding = attr(battle, "_binding");
        auto engine = get(record, "engine_id");
        if (engine.get() == Py_None && !PyDict_GetItemString(record, "engine_id")) throw p::Error("record has no engine_id");
        bool changed = false;
        auto value = integer(frags);
        const int same = PyObject_RichCompareBool(get(record, "presented_frags").get(), value.get(), Py_EQ);
        if (same < 0) throw p::Error("Native frags comparison failed");
        if (!same) {
            method(binding.get(), "arena_vehicle_statistics", engine.get(), value.get());
            put(record, "presented_frags", value);
            changed = true;
        }
        if (truthy(get(state, "team_killer", Py_False)) && !truthy(get(record, "presented_team_killer"))) {
            method(binding.get(), "arena_team_killer", engine.get());
            put(record, "presented_team_killer", p::boolean(true));
            changed = true;
        }
        return p::boolean(changed).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* deathAttackerEngineId(PyObject*, PyObject* args) {
    PyObject *battle, *state;
    if (!PyArg_ParseTuple(args, "OO", &battle, &state)) return nullptr;
    try {
        auto kind = get(state, "death_attacker_kind");
        std::int64_t network = 0;
        if (!pyInt(get(state, "death_attacker_id", integer(0).get()).get(), network)) return integer(0).release();
        p::Ref kindText(PyObject_Str(kind.get()));
        auto lookup = key(p::string(kindText.get()).c_str(), integer(network).get());
        auto records = attr(battle, "_records");
        PyObject* record = PyDict_GetItem(records.get(), lookup.get());
        if (!record) return integer(0).release();
        std::int64_t engine = 0;
        if (!pyInt(get(record, "engine_id", integer(0).get()).get(), engine)) throw p::Error("record engine_id is not an integer");
        return integer(engine).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* suppressPostmortemKiller(PyObject*, PyObject* args) {
    PyObject *battle, *victim, *state, *attacker;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &victim, &state, &attacker)) return nullptr;
    try {
        if (!truthy(get(victim, "local")) || !PyDict_GetItemString(state, "health")) Py_RETURN_FALSE;
        const bool dead = number(get(state, "health", p::number(0.).get()).get()) <= 0. ||
                          !truthy(get(state, "alive", Py_True));
        std::int64_t attacker_id = 0;
        auto raw = PyObject_IsTrue(attacker) > 0 ? p::retain(attacker) : integer(0);
        if (!pyInt(raw.get(), attacker_id, true)) Py_RETURN_FALSE;
        if (!dead || attacker_id <= 0) Py_RETURN_FALSE;
        auto records = attr(battle, "_records");
        PyObject* found = nullptr;
        Py_ssize_t position = 0; PyObject *k, *candidate;
        while (PyDict_Next(records.get(), &position, &k, &candidate)) {
            std::int64_t engine = 0;
            auto raw_engine = get(candidate, "engine_id", integer(0).get());
            if (!pyInt(PyObject_IsTrue(raw_engine.get()) > 0 ? raw_engine.get() : integer(0).get(), engine))
                throw p::Error("record engine_id is not an integer");
            if (engine == attacker_id) { found = candidate; break; }
        }
        if (!found) Py_RETURN_FALSE;
        std::int64_t victim_team = 0, attacker_team = 0;
        auto team = get(state, "team", integer(0).get());
        auto attacker_state = get(found, "state");
        auto other = attacker_state.get() != Py_None && PyObject_IsTrue(attacker_state.get()) > 0
            ? get(attacker_state.get(), "team", integer(0).get()) : integer(0);
        if (!pyInt(PyObject_IsTrue(team.get()) > 0 ? team.get() : integer(0).get(), victim_team, true) ||
            !pyInt(PyObject_IsTrue(other.get()) > 0 ? other.get() : integer(0).get(), attacker_team, true))
            Py_RETURN_FALSE;
        if (victim_team <= 0 || attacker_team <= 0 || victim_team == attacker_team) Py_RETURN_FALSE;
        return p::boolean(!truthy(get(found, "spot_visible", Py_True))).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* syncFireEffect(PyObject*, PyObject* args) {
    PyObject *battle, *entity, *burning = Py_None;
    if (!PyArg_ParseTuple(args, "OO|O", &battle, &entity, &burning)) return nullptr;
    try {
        auto descriptor = optionalAttr(entity, "typeDescriptor");
        auto extras = optionalAttr(descriptor.get(), "extrasDict");
        auto extra = extras.get() != Py_None ? method(extras.get(), "get", p::text("fire").get()) : p::retain(Py_None);
        if (extra.get() == Py_None) Py_RETURN_FALSE;
        const bool on = burning == Py_None ? truthy(optionalAttr(entity, "is_on_fire")) : p::flag(burning);
        if (on == truthy(method(extra.get(), "isRunningFor", entity))) Py_RETURN_FALSE;
        if (!on) { method(extra.get(), "stopFor", entity); Py_RETURN_TRUE; }
        auto appearance = optionalAttr(entity, "appearance");
        if (optionalAttr(appearance.get(), "compoundModel").get() == Py_None) Py_RETURN_FALSE;
        method(extra.get(), "startFor", entity);
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* stopRemoteVisual(PyObject*, PyObject* args) {
    PyObject *battle, *record;
    if (!PyArg_ParseTuple(args, "OO", &battle, &record)) return nullptr;
    try {
        const int outlined = PyObject_RichCompareBool(attr(battle, "_outlined_engine_id").get(), get(record, "engine_id").get(), Py_EQ);
        if (outlined < 0) throw p::Error("Native outline comparison failed");
        if (outlined) method(battle, "_clear_target_outline");
        const auto started = remoteVisualComponents(record);
        if (!started.first && !started.second) Py_RETURN_FALSE;
        std::int64_t id = 0;
        if (!pyInt(get(record, "engine_id").get(), id)) throw p::Error("record engine_id is not an integer");
        auto binding = attr(battle, "_binding");
        auto entity = integer(id);
        if (started.first) method(binding.get(), "stop_vehicle_visual", entity.get(), Py_False);
        else method(binding.get(), "stop_vehicle_minimap", entity.get());
        storeRemoteVisualComponents(record, false, false);
        if (truthy(get(record, "native_remote"))) {
            auto vehicle = method(attr(battle, "_remote_factory").get(), "get", entity.get());
            if (vehicle.get() != Py_None && PyObject_SetAttrString(vehicle.get(), "_offlineNativeMarkerVisible", Py_False) < 0)
                throw p::Error("Native marker visibility write failed");
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"pending_combat", pendingCombat, METH_VARARGS, "Whether a journaled combat event still targets the record."},
    {"vehicle_statistics", vehicleStatistics, METH_VARARGS, "Feed frags and team-killer state to the stock arena."},
    {"death_attacker_engine_id", deathAttackerEngineId, METH_VARARGS, "Resolve the durable killer's engine id."},
    {"suppress_postmortem_killer", suppressPostmortemKiller, METH_VARARGS, "Whether a local death hides its killer."},
    {"sync_fire_effect", syncFireEffect, METH_VARARGS, "Match the stock fire extra to the burning state."},
    {"stop_remote_visual", stopRemoteVisual, METH_VARARGS, "Stop a remote vehicle's marker and minimap entry."},
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* lifecycle_methods() { return methods; }
}

#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
namespace {
class Module final : public BW::Script::InitTimeJob {
public:
    Module() : BW::Script::InitTimeJob(1) {}
    void init() override {
        if (!Py_InitModule("offline_client_lifecycle_native", stock_native_client_lifecycle::lifecycle_methods())) {
            PyErr_Print();
            CRITICAL_MSG("Native remote vehicle lifecycle initialization failed\n");
        }
    }
};
Module module;
}
#endif
