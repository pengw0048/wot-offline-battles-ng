// Remote vehicle lifecycle and presentation edges, ported from the pinned battle runtime.
// Records are still the runtime's shared Python dicts until every writer is native.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script.hpp"
#endif
#include "stock_native_bot_python.hpp"
#include <climits>
#include <new>
#include <utility>
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
bool exactInteger(PyObject* v) { return !PyBool_Check(v) && (PyInt_Check(v) || PyLong_Check(v)); }
bool equal(PyObject* a, PyObject* b) {
    const int same = PyObject_RichCompareBool(a, b, Py_EQ);
    if (same < 0) throw p::Error("Native lifecycle comparison failed");
    return same != 0;
}
void replace(p::Ref& target, p::Ref value) { target.~Ref(); new (&target) p::Ref(std::move(value)); }
void setAttr(PyObject* o, const char* name, PyObject* v) { if (PyObject_SetAttrString(o, name, v) < 0) throw p::Error("Native lifecycle attribute write failed"); }
p::Ref copyDict(PyObject* v) { return v == Py_None || PyObject_IsTrue(v) <= 0 ? p::dict() : p::Ref(PyDict_Copy(v)); }
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


// BattleRuntime._apply_critical_state(record, payload, authority=None)
PyObject* criticalState(PyObject*, PyObject* args) {
    PyObject *battle, *record, *payload, *authority = Py_None, *critical_damage;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &record, &payload, &authority, &critical_damage)) return nullptr;
    try {
        auto canonical = method(battle, "_critical_state", payload);
        if (canonical.get() == Py_None) Py_RETURN_FALSE;
        if (authority != Py_None) {
            auto apply = method(battle, "_reconcile_critical_authority", record, authority, canonical.get());
            if (!truthy(apply)) {
                if (truthy(get(record, "local")) && truthy(get(record, "critical_state"))) {
                    auto state = copyDict(get(record, "state").get());
                    put(state.get(), "critical", get(record, "critical_state"));
                    put(record, "state", state);
                }
                Py_RETURN_FALSE;
            }
        }
        if (equal(canonical.get(), get(record, "critical_state").get())) Py_RETURN_FALSE;
        auto engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) throw p::Error("record has no engine_id");
        auto entity = method(battle, "_server_entity", engine);
        if (entity.get() == Py_None) Py_RETURN_FALSE;
        auto gun_edge = p::retain(Py_None);
        auto gun = attr(battle, "_gun_state");
        if (truthy(get(record, "local")) && gun.get() != Py_None) replace(gun_edge, method(battle, "_advance_local_gun_edge", gun.get()));
        auto events = method(critical_damage, "apply_payload", entity.get(), canonical.get());
        if (gun_edge.get() != Py_None) method(battle, "_advance_local_gun_to", entity.get(), p::item(gun_edge.get(), 1).get());
        put(record, "critical_state", canonical);
        auto state = copyDict(get(record, "state").get());
        put(state.get(), "critical", canonical);
        put(record, "state", state);
        if (!truthy(attr(battle, "_worker_mode"))) method(battle, "_present_critical", record, events.get(), integer(0).get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._apply_stun_state(record, state)
PyObject* stunState(PyObject*, PyObject* args) {
    PyObject *battle, *record, *state;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &record, &state)) return nullptr;
    try {
        auto end = get(state, "stun_end_server_time_ms", integer(0).get());
        if (!exactInteger(end.get()) || PyObject_RichCompareBool(end.get(), integer(0).get(), Py_LT) == 1)
            throw p::Error("LAN snapshot has an invalid stun end time");
        auto kind = get(state, "stun_attacker_kind", p::text("").get());
        auto attacker = get(state, "stun_attacker_id", integer(0).get());
        const bool stunned = PyObject_IsTrue(end.get()) > 0;
        if (stunned) {
            const bool valid_kind = equal(kind.get(), p::text("player").get()) || equal(kind.get(), p::text("bot").get());
            if (!valid_kind || !exactInteger(attacker.get()) || PyObject_RichCompareBool(attacker.get(), integer(0).get(), Py_LE) == 1)
                throw p::Error("LAN snapshot has an invalid stun attacker");
        } else if (PyObject_IsTrue(kind.get()) > 0 || PyObject_IsTrue(attacker.get()) > 0) {
            throw p::Error("cleared LAN stun retains an attacker");
        }
        auto factors_raw = get(state, "stun_factors");
        auto factors = PyObject_IsTrue(factors_raw.get()) > 0 ? p::retain(factors_raw.get()) : p::dict();
        auto items = method(factors.get(), "items");
        if (PyList_Sort(items.get()) < 0) throw p::Error("Native stun factor sort failed");
        p::Ref sorted(PyList_AsTuple(items.get()));
        std::int64_t end_value = 0, attacker_value = 0;
        if (!pyInt(end.get(), end_value) || !pyInt(attacker.get(), attacker_value)) throw p::Error("Native stun integers are invalid");
        p::Ref kind_text(PyObject_Str(kind.get()));
        p::Ref signature(PyTuple_Pack(4, integer(end_value).get(), kind_text.get(), integer(attacker_value).get(), sorted.get()));
        auto presented = get(record, "presented_stun_state");
        if (presented.get() == Py_None && !stunned) { put(record, "presented_stun_state", p::retain(signature.get())); Py_RETURN_FALSE; }
        if (equal(presented.get(), signature.get())) Py_RETURN_FALSE;
        auto now = method(battle, "_clock");
        auto estimated = method(battle, "_projectile_estimated_server_time", now.get());
        if (stunned && estimated.get() == Py_None) throw p::Error("LAN stun has no server-time anchor");
        const double remaining = stunned ? std::max(0., (number(end.get()) - p::scalar(estimated.get())) / 1000.) : 0.;
        auto engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) throw p::Error("record has no engine_id");
        auto entity = method(battle, "_server_entity", engine);
        if (entity.get() == Py_None) throw p::Error("stunned LAN vehicle is unavailable");
        const bool worker = truthy(attr(battle, "_worker_mode"));
        auto local_gun = p::retain(Py_None);
        if (truthy(get(record, "local")) && !worker) {
            replace(local_gun, attr(battle, "_gun_state"));
            if (local_gun.get() != Py_None) method(battle, "_advance_local_gun_edge", local_gun.get());
        }
        PyObject* raw_previous = PyObject_GetAttrString(entity.get(), "stunInfo");
        if (!raw_previous) { if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("Native stun read failed"); PyErr_Clear(); }
        p::Ref previous(raw_previous ? raw_previous : p::number(0.).release());
        auto stun_info = remaining > 0. ? p::number(p::scalar(method(battle, "_server_clock").get()) + remaining) : p::number(0.);
        setAttr(entity.get(), "stunInfo", stun_info.get());
        auto copied = remaining > 0. ? p::Ref(PyDict_Copy(factors.get())) : p::dict();
        setAttr(entity.get(), "_offlineStunFactors", copied.get());
        if (local_gun.get() != Py_None) {
            auto rescaled = method(battle, "_apply_current_reload_factor", local_gun.get(), entity.get());
            if (truthy(rescaled)) {
                auto publish = attr(battle, "_publish_reload_event");
                p::Ref positional(PyTuple_Pack(2, attr(local_gun.get(), "reload_time").get(), attr(local_gun.get(), "reload_duration").get()));
                auto keywords = p::dict(); put(keywords.get(), "force", p::boolean(true));
                p::Ref unused(PyObject_Call(publish.get(), positional.get(), keywords.get()));
            }
            method(battle, "_publish_targeting_info", entity.get(), local_gun.get());
        }
        if (!worker) {
            auto native_callback = optionalAttr(entity.get(), "set_stunInfo");
            auto left = p::number(remaining);
            if (native_callback.get() != Py_None && PyCallable_Check(native_callback.get())) {
                p::Ref unused(PyObject_CallFunctionObjArgs(native_callback.get(), previous.get(), nullptr));
            } else if (truthy(get(record, "local"))) {
                auto provider = attr(attr(battle, "_avatar").get(), "guiSessionProvider");
                auto view = attr(attr(battle, "_runtime").get(), "vehicle_view_state");
                method(provider.get(), "invalidateVehicleState", attr(view.get(), "STUN").get(), left.get());
            } else {
                auto feedback = attr(attr(attr(attr(battle, "_avatar").get(), "guiSessionProvider").get(), "shared").get(), "feedback");
                auto callback = optionalAttr(feedback.get(), "invalidateStun");
                if (callback.get() == Py_None || !PyCallable_Check(callback.get()))
                    throw p::Error("#1513 remote stun feedback boundary is unavailable");
                p::Ref unused(PyObject_CallFunctionObjArgs(callback.get(), engine, left.get(), nullptr));
            }
        }
        put(record, "presented_stun_state", p::retain(signature.get()));
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
    {"critical_state", criticalState, METH_VARARGS, "Apply a canonical critical payload to a vehicle."},
    {"stun_state", stunState, METH_VARARGS, "Present the #1513 stunInfo absolute-time contract."},
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
