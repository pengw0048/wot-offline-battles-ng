// Remote vehicle lifecycle and presentation edges, ported from the pinned battle runtime.
// Records are still the runtime's shared Python dicts until every writer is native.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"
#include <vector>
#include <climits>
#include <initializer_list>
#include <new>
#include <utility>
#include <cmath>
#include <string>

namespace stock_native_client_lifecycle {
namespace {
using namespace py;

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


// BattleRuntime._apply_siege_state(record, state)
PyObject* siegeState(PyObject*, PyObject* args) {
    PyObject *battle, *record, *state, *vehicle_physics;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &record, &state, &vehicle_physics)) return nullptr;
    try {
        auto states = attr(attr(attr(battle, "_runtime").get(), "constants").get(), "VEHICLE_SIEGE_STATE");
        auto disabled = attr(states.get(), "DISABLED"), on = attr(states.get(), "SWITCHING_ON");
        auto enabled = attr(states.get(), "ENABLED"), off = attr(states.get(), "SWITCHING_OFF");
        auto siege = get(state, "siege_state", disabled.get());
        auto left = get(state, "siege_time_left_ms", integer(0).get());
        if (!exactInteger(siege.get())) throw p::Error("LAN snapshot has an invalid siege state");
        if (!equal(siege.get(), disabled.get()) && !equal(siege.get(), on.get()) && !equal(siege.get(), enabled.get()) &&
            !equal(siege.get(), off.get()))
            throw p::Error("LAN snapshot has an unsupported siege state");
        if (!exactInteger(left.get()) || PyObject_RichCompareBool(left.get(), integer(0).get(), Py_LT) == 1)
            throw p::Error("LAN snapshot has an invalid siege transition time");
        const bool switching = equal(siege.get(), on.get()) || equal(siege.get(), off.get());
        const bool positive = PyObject_RichCompareBool(left.get(), integer(0).get(), Py_GT) == 1;
        if ((switching && !positive) || (!switching && !equal(left.get(), integer(0).get())))
            throw p::Error("LAN snapshot has an inconsistent siege transition");
        const bool local = truthy(get(record, "local"));
        auto pending = attr(battle, "_local_siege_pending");
        if (local && pending.get() != Py_None) {
            auto request_seq = p::item(pending.get(), 1);
            bool acknowledged = request_seq.get() == Py_None;
            auto snapshot_seq = get(state, "input_seq");
            if (exactInteger(snapshot_seq.get()))
                acknowledged = request_seq.get() == Py_None ||
                               PyObject_RichCompareBool(snapshot_seq.get(), request_seq.get(), Py_GE) == 1;
            if (acknowledged && !switching) {
                setAttr(battle, "_local_siege_pending", Py_None);
                method(battle, "_report_local_siege_edge", p::text("ack").get(), siege.get(), left.get(), snapshot_seq.get());
            }
        }
        auto presented = get(record, "presented_siege_state");
        if (equal(presented.get(), siege.get())) Py_RETURN_FALSE;
        if (presented.get() == Py_None && equal(siege.get(), disabled.get())) {
            put(record, "presented_siege_state", p::retain(disabled.get()));
            Py_RETURN_FALSE;
        }
        auto engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) throw p::Error("record has no engine_id");
        auto entity = method(battle, "_server_entity", engine);
        auto descriptor = optionalAttr(entity.get(), "typeDescriptor");
        if (descriptor.get() == Py_None) throw p::Error("Siege vehicle descriptor is unavailable");
        const bool has_siege = truthy(optionalAttr(descriptor.get(), "hasSiegeMode"));
        if (!has_siege && !equal(siege.get(), disabled.get())) throw p::Error("Vehicle without Siege mode received an active state");
        if (!has_siege) { put(record, "presented_siege_state", p::retain(disabled.get())); Py_RETURN_FALSE; }
        auto gun_edge = p::retain(Py_None);
        if (local && attr(battle, "_gun_state").get() != Py_None)
            replace(gun_edge, method(battle, "_advance_local_gun_edge", attr(battle, "_gun_state").get()));
        auto seconds = p::number(number(left.get()) / 1000.);
        method(attr(battle, "_binding").get(), "update_vehicle_siege_state", engine, siege.get(), seconds.get());
        put(record, "presented_siege_state", p::retain(siege.get()));
        auto factory = attr(battle, "_remote_factory");
        if (truthy(attr(battle, "_worker_mode")) && truthy(get(record, "native_remote")) && factory.get() != Py_None) {
            auto updater = optionalAttr(factory.get(), "update_entity_siege_pose");
            if (updater.get() != Py_None && PyCallable_Check(updater.get())) p::Ref unused(PyObject_CallFunctionObjArgs(updater.get(), engine, nullptr));
        }
        if (local && attr(battle, "_local_matrix").get() != Py_None) {
            const bool raised = equal(siege.get(), enabled.get()) || equal(siege.get(), off.get());
            method(battle, "_select_local_siege_pose", entity.get(), p::boolean(raised).get());
        }
        if (local && attr(battle, "_gun_state").get() != Py_None) {
            auto changed = method(attr(battle, "_gun_state").get(), "adopt_descriptor", attr(entity.get(), "typeDescriptor").get());
            setAttr(battle, "_targeting_signature", Py_None);
            if (gun_edge.get() != Py_None) method(battle, "_advance_local_gun_to", entity.get(), p::item(gun_edge.get(), 1).get());
            if (truthy(changed) && gun_edge.get() != Py_None && attr(battle, "_sender").get() != Py_None)
                method(attr(battle, "_sender").get(), "send_current");
        }
        if (local && attr(battle, "_local_physics").get() != Py_None) {
            auto type = attr(entity.get(), "typeDescriptor");
            auto factors = method(battle, "_local_factors", type.get());
            setAttr(battle, "_local_physics", method(vehicle_physics, "derive_params", attr(entity.get(), "typeDescriptor").get(), factors.get()).get());
            setAttr(battle, "_local_suspension_params", Py_None);
            setAttr(battle, "_local_suspension_probe_trace", p::Ref(PyTuple_New(0)).get());
            setAttr(battle, "_local_suspension_disabled", Py_False);
            setAttr(battle, "_local_suspension_report", Py_None);
            setAttr(battle, "_local_suspension_failed_this_tick", Py_False);
            setAttr(battle, "_local_spring_ground_memory", Py_None);
            setAttr(battle, "_local_pseudo_ground_memory", Py_None);
            for (const char* name : {"_local_suspension_pitch_velocity", "_local_suspension_roll_velocity",
                                     "_local_suspension_support_vertical_speed"})
                setAttr(battle, name, p::number(0.).get());
            setAttr(battle, "_local_suspension_support_gradient", Py_None);
        }
        if (local && attr(battle, "_local_matrix").get() != Py_None) method(battle, "_update_local_hull_aiming", entity.get(), p::number(0.).get());
        if (local) method(battle, "_report_local_siege_edge", p::text("state").get(), siege.get(), left.get(), get(state, "input_seq").get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}


bool textIs(PyObject* v, const char* text) { return v && equal(v, p::text(text).get()); }

// BattleRuntime._present_critical(record, events, attacker_id)
PyObject* presentCritical(PyObject*, PyObject* args) {
    PyObject *battle, *record, *events, *attacker;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &record, &events, &attacker)) return nullptr;
    try {
        const bool local = truthy(get(record, "local"));
        if (truthy(attr(battle, "_worker_mode")) || (!local && !truthy(get(record, "spot_visible", Py_True)))) Py_RETURN_FALSE;
        auto engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) throw p::Error("record has no engine_id");
        auto entity = method(battle, "_server_entity", engine);
        if (entity.get() == Py_None || attr(entity.get(), "typeDescriptor").get() == Py_None) Py_RETURN_FALSE;
        method(battle, "_sync_fire_effect", entity.get());
        if (PyObject_IsTrue(events) <= 0) Py_RETURN_FALSE;
        bool shown = false;
        auto constants = attr(attr(battle, "_runtime").get(), "constants");
        {
            p::Ref iterator(PyObject_GetIter(events));
            while (PyObject* raw = PyIter_Next(iterator.get())) {
                p::Ref event(raw);
                if (!textIs(get(event.get(), "kind").get(), "ammo_rack") || !textIs(get(event.get(), "state").get(), "destroyed")) continue;
                auto callback = optionalAttr(entity.get(), "showAmmoBayEffect");
                if (callback.get() == Py_None || !PyCallable_Check(callback.get())) throw p::Error("#1513 ammo-bay effect boundary is unavailable");
                auto modes = optionalAttr(constants.get(), "AMMOBAY_DESTRUCTION_MODE");
                if (modes.get() == Py_None || !PyObject_HasAttrString(modes.get(), "HE_DETONATION"))
                    throw p::Error("#1513 ammo-bay destruction mode is unavailable");
                std::int64_t mode = 0;
                if (!pyInt(attr(modes.get(), "HE_DETONATION").get(), mode)) throw p::Error("#1513 ammo-bay destruction mode is invalid");
                p::Ref unused(PyObject_CallFunctionObjArgs(callback.get(), integer(mode).get(), p::number(0.).get(), p::number(0.).get(), nullptr));
                shown = true;
            }
            if (PyErr_Occurred()) throw p::Error("Native critical event iteration failed");
        }
        if (!local || attr(battle, "_avatar").get() == Py_None || !truthy(method(battle, "_damage_info_is_serviceable")))
            return p::boolean(shown).release();
        auto indices = optionalAttr(constants.get(), "DAMAGE_INFO_INDICES");
        if (indices.get() == Py_None && !PyObject_HasAttrString(constants.get(), "DAMAGE_INFO_INDICES")) replace(indices, p::dict());
        auto suffix = [](PyObject* cause) -> std::string {
            if (textIs(cause, "fire")) return "_AT_FIRE";
            if (textIs(cause, "ramming")) return "_AT_RAMMING";
            if (textIs(cause, "world_collision")) return "_AT_WORLD_COLLISION";
            if (textIs(cause, "drowning")) return "_AT_DROWNING";
            return "_AT_SHOT";
        };
        std::int64_t attacker_value = 0;
        if (!pyInt(PyObject_IsTrue(attacker) > 0 ? attacker : integer(0).get(), attacker_value)) throw p::Error("Native critical attacker is invalid");
        p::Ref iterator(PyObject_GetIter(events));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref event(raw);
            auto kind = get(event.get(), "kind"), state = get(event.get(), "state");
            auto cause = get(event.get(), "cause", p::text("shot").get());
            auto old = get(event.get(), "old_state");
            std::int64_t extra = 0;
            std::string code;
            auto extraIndex = [&](const char* why) {
                auto value = method(battle, "_critical_extra_index", attr(entity.get(), "typeDescriptor").get(), get(event.get(), "name").get());
                if (PyObject_RichCompareBool(value.get(), integer(0).get(), Py_LE) == 1) throw p::Error(why);
                if (!pyInt(value.get(), extra)) throw p::Error(why);
            };
            if (textIs(kind.get(), "device")) {
                extraIndex("#1513 device critical extra is invalid");
                const bool repaired = textIs(cause.get(), "repair") ||
                    (textIs(old.get(), "destroyed") && (textIs(state.get(), "critical") || textIs(state.get(), "normal"))) ||
                    (textIs(old.get(), "critical") && textIs(state.get(), "normal"));
                if (repaired) code = textIs(state.get(), "normal") ? "DEVICE_REPAIRED" : "DEVICE_REPAIRED_TO_CRITICAL";
                else code = std::string(textIs(state.get(), "destroyed") ? "DEVICE_DESTROYED" : "DEVICE_CRITICAL") + suffix(cause.get());
            } else if (textIs(kind.get(), "crew")) {
                extraIndex("#1513 crew critical extra is invalid");
                if (textIs(state.get(), "normal")) code = "TANKMAN_RESTORED";
                else if (textIs(cause.get(), "world_collision") || textIs(cause.get(), "drowning")) code = "TANKMAN_HIT" + suffix(cause.get());
                else if (textIs(cause.get(), "fire")) code = "TANKMAN_HIT";
                else code = "TANKMAN_HIT_AT_SHOT";
            } else if (textIs(kind.get(), "fire")) {
                if (truthy(state)) code = textIs(cause.get(), "ramming") ? "DEVICE_STARTED_FIRE_AT_RAMMING" : "DEVICE_STARTED_FIRE_AT_SHOT";
                else code = "FIRE_STOPPED";
            } else continue;
            auto index = method(indices.get(), "get", p::text(code).get());
            if (index.get() == Py_None) throw p::Error("#1513 damage-info index is unavailable: " + code);
            std::int64_t damage = 0;
            if (!pyInt(index.get(), damage)) throw p::Error("#1513 damage-info index is invalid: " + code);
            if (truthy(method(battle, "_show_damage_info", engine, integer(damage).get(), integer(extra).get(), integer(attacker_value).get())))
                shown = true;
        }
        if (PyErr_Occurred()) throw p::Error("Native critical event iteration failed");
        auto report = literal(3);
        put(report.get(), "phase", p::text("critical_present"));
        p::Ref all(PySequence_List(events));
        put(report.get(), "events", p::Ref(PyList_GetSlice(all.get(), 0, 24)));
        put(report.get(), "shown", p::boolean(shown));
        method(battle, "_report_critical_presentation", record, report.get());
        return p::boolean(shown).release();
    } catch (const std::exception& e) { return fail(e); }
}


// BattleRuntime._sync_remote_visual_components(record, vehicle, team_visible, world_visible)
bool syncRemoteVisualComponents(PyObject* battle, PyObject* record, PyObject* vehicle, bool team_visible, bool world_visible) {
    auto started = remoteVisualComponents(record);
    bool world = started.first, minimap = started.second;
    world_visible = world_visible && team_visible;
    std::int64_t id = 0;
    if (!pyInt(get(record, "engine_id").get(), id)) throw p::Error("record engine_id is not an integer");
    auto entity = integer(id);
    if (!world_visible && equal(attr(battle, "_outlined_engine_id").get(), entity.get())) method(battle, "_clear_target_outline");
    auto finish = [&]() {
        storeRemoteVisualComponents(record, world, minimap);
        put(record, "spot_world_marker_visible", p::boolean(world));
        if (truthy(get(record, "native_remote"))) setAttr(vehicle, "_offlineNativeMarkerVisible", world ? Py_True : Py_False);
    };
    try {
        auto binding = attr(battle, "_binding");
        auto dead = [&]() {
            if (!truthy(method(battle, "_record_alive", record, vehicle))) method(battle, "_present_vehicle_dead", record, Py_True);
        };
        if (team_visible) {
            if (world_visible && !world && !minimap) {
                method(binding.get(), "start_vehicle_visual", entity.get(), Py_True);
                world = minimap = true;
                dead();
            } else {
                if (!minimap) { method(binding.get(), "start_vehicle_minimap", entity.get()); minimap = true; }
                if (world_visible && !world) { method(binding.get(), "start_vehicle_marker", entity.get()); world = true; dead(); }
                else if (!world_visible && world) { method(binding.get(), "stop_vehicle_marker", entity.get()); world = false; }
            }
        } else if (world && minimap) {
            method(binding.get(), "stop_vehicle_visual", entity.get(), Py_False);
            world = minimap = false;
        } else if (world) {
            method(binding.get(), "stop_vehicle_visual", entity.get(), Py_False);
            world = false;
        } else if (minimap) {
            method(binding.get(), "stop_vehicle_minimap", entity.get());
            minimap = false;
        }
    } catch (...) {
        // The pinned finally block preserves each component's completed signal.
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        try { finish(); } catch (...) { Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace); throw; }
        PyErr_Restore(type, value, trace);
        throw;
    }
    finish();
    return world || minimap;
}

PyObject* remoteVisualComponentsPython(PyObject*, PyObject* args) {
    PyObject *battle, *record, *vehicle, *team, *world;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &record, &vehicle, &team, &world)) return nullptr;
    try { return p::boolean(syncRemoteVisualComponents(battle, record, vehicle, p::flag(team), p::flag(world))).release(); }
    catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._set_record_spot_visibility(record, visible, marker_visible=None)
PyObject* spotVisibility(PyObject*, PyObject* args) {
    PyObject *battle, *record, *visible_raw, *marker_raw, *query, *set_draw_visibility, *idle;
    if (!PyArg_ParseTuple(args, "OOOOOOO", &battle, &record, &visible_raw, &marker_raw, &query, &set_draw_visibility, &idle)) return nullptr;
    try {
        const bool visible = p::flag(visible_raw);
        auto marker_value = p::retain(marker_raw);
        if (marker_raw == Py_None) replace(marker_value, method(record, "pop", p::text("_spot_marker_transition").get(), p::boolean(visible).get()));
        const bool marker = truthy(marker_value);
        if (truthy(get(record, "spot_visible", Py_True)) != visible) {
            put(record, "spot_visible", p::boolean(visible));
            method(query, "lifecycle", battle, record);
        }
        put(record, "spot_visible", p::boolean(visible));
        put(record, "spot_marker_visible", p::boolean(marker));
        if (!truthy(get(record, "presentation")) || !truthy(get(record, "ready"))) return p::boolean(visible).release();
        auto engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) throw p::Error("record has no engine_id");
        auto vehicle = method(attr(battle, "_remote_factory").get(), "get", engine);
        if (vehicle.get() == Py_None || attr(vehicle.get(), "model").get() == Py_None) throw p::Error("spotted remote vehicle has no model");
        setAttr(vehicle.get(), "_spot_visible", visible ? Py_True : Py_False);
        const bool alive = truthy(method(battle, "_record_alive", record, vehicle.get()));
        const bool draw = visible || !alive;
        p::Ref signature(PyTuple_Pack(4, p::boolean(visible).get(), p::boolean(marker).get(), p::boolean(draw).get(), p::boolean(alive).get()));
        if (!equal(signature.get(), get(record, "_spot_presentation_signature").get())) {
            if (truthy(get(record, "native_remote"))) {
                setAttr(vehicle.get(), "_offlineNativeDrawVisible", draw ? Py_True : Py_False);
                p::Ref unused(PyObject_CallFunctionObjArgs(set_draw_visibility, vehicle.get(), draw ? Py_True : Py_False, nullptr));
                setAttr(vehicle.get(), "targetCaps", p::list().get());
                if (!draw) {
                    auto stop = optionalAttr(vehicle.get(), "update_tracks");
                    bool stopped = false;
                    if (stop.get() != Py_None && PyCallable_Check(stop.get())) {
                        auto mode = optionalAttr(vehicle.get(), "engineMode");
                        if (mode.get() == Py_None && !PyObject_HasAttrString(vehicle.get(), "engineMode"))
                            replace(mode, p::Ref(PyTuple_Pack(2, idle, integer(0).get())));
                        p::Ref feature_args(PyTuple_Pack(3, p::number(0.).get(), p::number(0.).get(), mode.get()));
                        stopped = truthy(method(battle, "_run_optional_feature", p::text("remote track animation").get(), stop.get(), feature_args.get()));
                    }
                    put(record, "_remote_track_pending", p::boolean(!stopped));
                } else {
                    put(record, "_remote_track_pending", p::boolean(true));
                    method(record, "pop", p::text("_remote_track_state_signature").get(), Py_None);
                }
            } else {
                method(attr(vehicle.get(), "appearance").get(), "changeVisibility", draw ? Py_True : Py_False);
            }
            method(battle, "_sync_fire_effect", vehicle.get(), draw ? Py_None : Py_False);
            put(record, "_spot_presentation_signature", p::retain(signature.get()));
        }
        method(battle, "_sync_remote_visual_components", record, vehicle.get(), p::boolean(marker).get(), p::boolean(visible).get());
        if (marker && truthy(get(record, "deferred_health_presentation", Py_False))) {
            auto provider = optionalAttr(attr(battle, "_avatar").get(), "guiSessionProvider");
            auto present = optionalAttr(provider.get(), "setVehicleHealth");
            if (present.get() == Py_None || !PyCallable_Check(present.get())) throw p::Error("#1513 remote vehicle health presenter is unavailable");
            auto state_raw = get(record, "state");
            auto state = truthy(state_raw) ? p::retain(state_raw.get()) : p::dict();
            auto health_raw = method(state.get(), "get", p::text("display_health").get(), method(state.get(), "get", p::text("health").get(), integer(0).get()).get());
            std::int64_t health = 0;
            if (!pyInt(truthy(health_raw) ? health_raw.get() : integer(0).get(), health)) throw p::Error("Native deferred health is invalid");
            p::Ref unused(PyObject_CallFunctionObjArgs(present.get(), Py_False, engine, integer(std::max<std::int64_t>(0, health)).get(),
                                                       integer(0).get(), integer(0).get(), nullptr));
            method(record, "pop", p::text("deferred_health_presentation").get(), Py_None);
        }
        return p::boolean(visible).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._apply_spot_presentation(record, entity, remembered)
PyObject* spotPresentation(PyObject*, PyObject* args) {
    PyObject *battle, *record, *entity, *remembered_raw;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &record, &entity, &remembered_raw)) return nullptr;
    try {
        auto remembered = p::retain(remembered_raw);
        if (!truthy(method(battle, "_record_alive", record, entity)) && truthy(get(record, "dead_marker_known", Py_False)))
            replace(remembered, p::boolean(true));
        auto fn = attr(battle, "_spot_presentation_visibility");
        p::Ref positional(PyTuple_Pack(2, entity, remembered.get()));
        auto keywords = p::dict();
        put(keywords.get(), "was_model_visible", p::boolean(truthy(get(record, "spot_visible", Py_False))));
        p::Ref pair(PyObject_Call(fn.get(), positional.get(), keywords.get()));
        auto model = p::item(pair.get(), 0), marker = p::item(pair.get(), 1);
        if (p::size(pair.get()) != 2) throw p::Error("too many values to unpack");
        put(record, "spot_marker_visible", p::retain(marker.get()));
        put(record, "_spot_marker_transition", p::retain(marker.get()));
        try {
            method(battle, "_set_record_spot_visibility", record, model.get());
        } catch (...) {
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            method(record, "pop", p::text("_spot_marker_transition").get(), Py_None);
            PyErr_Restore(type, value, trace);
            throw;
        }
        method(record, "pop", p::text("_spot_marker_transition").get(), Py_None);
        return PyTuple_Pack(2, model.get(), marker.get());
    } catch (const std::exception& e) { return fail(e); }
}


// BattleRuntime._apply_turret_detachment(record, entity, state=None)
PyObject* turretDetachment(PyObject*, PyObject* args) {
    PyObject *battle, *record, *entity, *state = Py_None;
    if (!PyArg_ParseTuple(args, "OOO|O", &battle, &record, &entity, &state)) return nullptr;
    try {
        const bool changed = !truthy(optionalAttr(entity, "isTurretDetached"));
        method(battle, "_mark_turret_detached", entity);
        if (truthy(attr(battle, "_worker_mode"))) method(battle, "_queue_detached_turret", record, entity, state);
        return p::boolean(changed).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._finalize_dead_remote_wreck(record, entity)
PyObject* finalizeDeadWreck(PyObject*, PyObject* args) {
    PyObject *battle, *record, *entity;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &record, &entity)) return nullptr;
    try {
        method(record, "pop", p::text("wreck_known").get(), Py_None);
        method(record, "pop", p::text("deferred_health_presentation").get(), Py_None);
        auto factory = attr(battle, "_remote_factory");
        if (factory.get() == Py_None) Py_RETURN_FALSE;
        p::Ref getter(p::retain(Py_None));
        if (!callableAttr(factory.get(), "get", getter)) Py_RETURN_FALSE;
        auto engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) throw p::Error("record has no engine_id");
        p::Ref vehicle(PyObject_CallFunctionObjArgs(getter.get(), engine, nullptr));
        if (PyObject_IsTrue(vehicle.get()) <= 0) replace(vehicle, p::retain(entity));
        if (vehicle.get() == Py_None) Py_RETURN_FALSE;
        if (truthy(get(record, "native_remote"))) {
            setAttr(vehicle.get(), "_offlineNativeDrawVisible", Py_True);
            setAttr(vehicle.get(), "targetCaps", p::list().get());
        }
        if (optionalAttr(vehicle.get(), "model").get() == Py_None) {
            method(record, "pop", p::text("_spot_presentation_signature").get(), Py_None);
            Py_RETURN_FALSE;
        }
        try {
            auto spot = get(record, "spot_visible", Py_False);
            auto marker = get(record, "spot_marker_visible", spot.get());
            return method(battle, "_set_record_spot_visibility", record, p::boolean(truthy(spot)).get(), p::boolean(truthy(marker)).get()).release();
        } catch (const p::Error&) {
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            PyErr_NormalizeException(&type, &value, &trace);
            p::Ref held_type(type ? type : p::retain(Py_None).release()), held_value(value ? value : p::retain(Py_None).release());
            Py_XDECREF(trace);
            method(record, "pop", p::text("_spot_presentation_signature").get(), Py_None);
            callKw(battle, "_warn_optional_failure", {p::text("dead wreck presentation").get(), held_value.get()}, {{"disable", Py_False}});
            Py_RETURN_FALSE;
        }
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._apply_health(record, state, attacker_id=0, reason_id=None, force_cause=False,
//                             attack_reason_id=None, suppress_combat_presentation=False,
//                             suppress_postmortem_killer=False)
PyObject* applyHealth(PyObject*, PyObject* args) {
    PyObject *battle, *record, *state, *attacker_raw, *reason_raw, *force_raw, *attack_raw, *suppress_raw, *postmortem_raw;
    PyObject *critical_damage, *environment, *drive;
    if (!PyArg_ParseTuple(args, "OOOOOOOOOOOO", &battle, &record, &state, &attacker_raw, &reason_raw, &force_raw, &attack_raw,
                          &suppress_raw, &postmortem_raw, &critical_damage, &environment, &drive)) return nullptr;
    try {
        if (!PyDict_GetItemString(state, "health")) Py_RETURN_NONE;
        const bool local = truthy(get(record, "local"));
        const bool requested = p::flag(suppress_raw) && !local;
        const std::int64_t health = std::max<std::int64_t>(0, strictInt(get(state, "health", integer(0).get()).get()));
        std::int64_t reason = 0;
        if (reason_raw == Py_None) { auto r = get(state, "death_reason", integer(0).get()); reason = std::max<std::int64_t>(0, strictInt(truthy(r) ? r.get() : integer(0).get())); }
        else reason = std::max<std::int64_t>(0, strictInt(reason_raw));
        const std::int64_t attack_reason = attack_raw == Py_None ? reason : std::max<std::int64_t>(0, strictInt(attack_raw));
        PyObject* engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) { PyErr_SetObject(PyExc_KeyError, p::text("engine_id").get()); throw p::Error("engine_id"); }
        auto display_raw = get(state, "display_health", integer(health).get());
        const std::int64_t display = std::max<std::int64_t>(0, strictInt(truthy(display_raw) ? display_raw.get() : integer(0).get()));
        const bool crew_active = truthy(get(state, "alive", p::boolean(health > 0).get())) && health > 0;
        const bool dead = health <= 0 || !crew_active;
        const bool crew_knockout = health > 0 && !crew_active;
        auto critical_raw = get(state, "critical");
        auto critical = truthy(critical_raw) ? p::retain(critical_raw.get()) : p::dict();
        const bool ammo_rack_death = health <= 0 && truthy(method(critical.get(), "get", p::text("ammo_rack_death").get(), Py_False));
        if (ammo_rack_death && reason == 0) {
            auto cause = get(record, "ammo_death_cause");
            if (textIs(cause.get(), "fire")) reason = 1;
            else if (textIs(cause.get(), "world_collision")) reason = 3;
        }
        std::int64_t native_health = dead && display > 0 ? display : health;
        auto constants = [&]() { return attr(attr(battle, "_runtime").get(), "constants"); };
        if (ammo_rack_death) native_health = strictInt(attr(attr(constants().get(), "SPECIAL_VEHICLE_HEALTH").get(), "TURRET_DETACHED").get());
        const bool suppress = requested && !dead;
        p::Ref signature(PyTuple_Pack(4, integer(health).get(), integer(display).get(), p::boolean(crew_active).get(), integer(reason).get()));
        auto last = attr(battle, "_last_health");
        auto previous_signature = method(last.get(), "get", engine);
        const bool durable_changed = !equal(previous_signature.get(), signature.get());
        bool previous_dead = false;
        if (previous_signature.get() != Py_None) {
            auto first = p::item(previous_signature.get(), 0), third = p::item(previous_signature.get(), 2);
            previous_dead = PyObject_RichCompareBool(first.get(), integer(0).get(), Py_LE) == 1 || !truthy(third);
        }
        const bool worker = truthy(attr(battle, "_worker_mode"));
        // int(attacker_id) is evaluated only where the pinned code converts it.
        auto attackerId = [&]() { return integer(strictInt(attacker_raw)); };
        auto attack = integer(attack_reason), reason_value = integer(reason);
        if (previous_dead && dead) {
            if (ammo_rack_death) {
                auto entity = method(battle, "_server_entity", engine);
                if (entity.get() == Py_None) Py_RETURN_NONE;
                auto previous_native = attr(entity.get(), "health");
                method(battle, "_detach_late_ammo_turret", record, entity.get(), state);
                auto special = method(battle, "_ammo_bay_special_health", entity.get());
                if (!equal(previous_native.get(), special.get()) && !worker) {
                    auto provider = attr(attr(battle, "_avatar").get(), "guiSessionProvider");
                    method(provider.get(), "setVehicleHealth", p::boolean(local).get(), engine, special.get(), attackerId().get(), attack.get());
                }
            }
            if (PyObject_SetItem(last.get(), engine, signature.get()) < 0) throw p::Error("Native health signature write failed");
            Py_RETURN_NONE;
        }
        if (!durable_changed && !p::flag(force_raw)) Py_RETURN_NONE;
        auto entity = method(battle, "_server_entity", engine);
        if (entity.get() == Py_None) Py_RETURN_NONE;
        if (dead && !previous_dead && !local) {
            put(record, "dead_marker_known", method(battle, "_combat_target_is_spotted", record));
            method(environment, "lifecycle", battle, record);
        }
        if (PyObject_SetItem(last.get(), engine, signature.get()) < 0) throw p::Error("Native health signature write failed");
        auto previous = optionalAttr(entity.get(), "health");
        if (previous.get() == Py_None && !PyObject_HasAttrString(entity.get(), "health")) replace(previous, integer(health));
        auto outlined = [&]() { return equal(attr(battle, "_outlined_engine_id").get(), engine); };
        if (dead && !previous_dead && outlined()) method(battle, "_clear_target_outline");
        if (dead && !previous_dead && !crew_knockout) {
            auto is = [&](const char* name, std::int64_t fallback) {
                return equal(dispatch(battle, "_attack_reason", "attack_reason", {p::text(name).get(), integer(fallback).get()}).get(), reason_value.get());
            };
            const char* cause = is("FIRE", 1) ? "fire" : is("RAM", 2) ? "ramming" : is("WORLD_COLLISION", 3) ? "world_collision"
                              : is("DROWNING", 5) ? "drowning" : is("OVERTURN", 7) ? "overturn" : "shot";
            auto payload = method(critical_damage, "apply_death", entity.get(), p::text(cause).get());
            if (payload.get() != Py_None) {
                auto canonical = method(battle, "_critical_state", payload.get());
                if (PyObject_SetItem(state, p::text("critical").get(), canonical.get()) < 0) throw p::Error("Native death critical write failed");
                put(record, "critical_state", p::retain(canonical.get()));
                put(record, "state", p::retain(state));
                if (!worker && !suppress) method(battle, "_sync_fire_effect", entity.get());
                if (local) {
                    const std::string c = cause;
                    const bool attribute = c != "drowning" && c != "world_collision" && c != "overturn";
                    callKw(battle, "_queue_local_damage_report", {}, {{"critical", payload.get()}, {"attribute_attacker", attribute ? Py_True : Py_False}});
                }
            }
        }
        auto crewActive = p::boolean(crew_active);
        p::Ref fn(p::retain(Py_None));
        if (worker) {
            setAttr(entity.get(), "health", integer(native_health).get());
            if (ammo_rack_death) method(battle, "_apply_turret_detachment", record, entity.get(), state);
            if (callableAttr(entity.get(), "set_health", fn)) p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), previous.get(), nullptr));
            auto previous_crew = optionalAttr(entity.get(), "isCrewActive");
            if (previous_crew.get() == Py_None && !PyObject_HasAttrString(entity.get(), "isCrewActive")) replace(previous_crew, p::retain(crewActive.get()));
            setAttr(entity.get(), "isCrewActive", crewActive.get());
            if (callableAttr(entity.get(), "set_isCrewActive", fn)) p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), previous_crew.get(), nullptr));
            if (callableAttr(entity.get(), "retain_wreck_model", fn) && truthy(get(record, "presentation")) && dead)
                p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), nullptr));
            Py_RETURN_NONE;
        }
        setAttr(entity.get(), "health", integer(native_health).get());
        if (ammo_rack_death) method(battle, "_apply_turret_detachment", record, entity.get(), state);
        const bool native_remote = truthy(get(record, "native_remote"));
        if (!suppress && callableAttr(entity.get(), "onHealthChanged", fn))
            p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), integer(native_health).get(), attackerId().get(), attack.get(), nullptr));
        else if (!native_remote && callableAttr(entity.get(), "set_health", fn))
            p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), previous.get(), nullptr));
        auto previous_crew = optionalAttr(entity.get(), "isCrewActive");
        if (previous_crew.get() == Py_None && !PyObject_HasAttrString(entity.get(), "isCrewActive")) replace(previous_crew, p::retain(crewActive.get()));
        setAttr(entity.get(), "isCrewActive", crewActive.get());
        if (!equal(previous_crew.get(), crewActive.get()) && (!suppress || !native_remote) && callableAttr(entity.get(), "set_isCrewActive", fn))
            p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), previous_crew.get(), nullptr));
        const bool presentation = truthy(get(record, "presentation"));
        if (presentation && !suppress) {
            auto provider = optionalAttr(attr(battle, "_avatar").get(), "guiSessionProvider");
            if (!callableAttr(provider.get(), "setVehicleHealth", fn)) throw p::Error("#1513 remote vehicle health presenter is unavailable");
            p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), Py_False, engine, integer(native_health).get(), attackerId().get(), attack.get(), nullptr));
            method(record, "pop", p::text("deferred_health_presentation").get(), Py_None);
        } else if (presentation) {
            put(record, "deferred_health_presentation", p::boolean(true));
        }
        auto alive_attr = optionalAttr(entity.get(), "isAlive");
        const bool entity_alive = alive_attr.get() != Py_None && PyCallable_Check(alive_attr.get())
            ? truthy(p::Ref(PyObject_CallFunctionObjArgs(alive_attr.get(), nullptr))) : truthy(alive_attr);
        if (!entity_alive) {
            if (outlined()) method(battle, "_clear_target_outline");
            if (!suppress) {
                method(battle, "_release_target_lock", engine);
                if (!native_remote) method(battle, "_present_vehicle_dead", record, Py_False);
            }
        }
        if (local) {
            if (dead) {
                method(drive, "local_death", battle);
                setAttr(battle, "_local_speed", p::number(0.).get());
                auto sender = attr(battle, "_sender");
                if (sender.get() != Py_None) { setAttr(sender.get(), "forward", p::number(0.).get()); setAttr(sender.get(), "turn", p::number(0.).get()); }
            }
            method(attr(battle, "_avatar").get(), "updateVehicleHealth", engine, integer(ammo_rack_death ? native_health : display).get(),
                   reason_value.get(), crewActive.get(), Py_False);
        }
        if (!previous_dead && dead) {
            if (!suppress) {
                if (callableAttr(attr(battle, "_binding").get(), "arena_vehicle_killed", fn)) {
                    p::Ref unused(PyObject_CallFunctionObjArgs(fn.get(), engine, attackerId().get(), reason_value.get(), nullptr));
                    method(battle, "_report_local_kill_contract", record, attackerId().get(), reason_value.get());
                    if (local && p::flag(postmortem_raw)) {
                        p::Ref clear(p::retain(Py_None));
                        if (!callableAttr(attr(attr(battle, "_runtime").get(), "compatibility").get(), "clear_postmortem_killer", clear))
                            throw p::Error("#1513 postmortem killer compatibility boundary is unavailable");
                        p::Ref unused2(PyObject_CallFunctionObjArgs(clear.get(), attr(battle, "_avatar").get(), nullptr));
                    }
                }
            }
            if (!local) method(battle, "_fallback_postmortem_viewpoint", engine);
        }
        auto factory = attr(battle, "_remote_factory");
        if (!previous_dead && dead && presentation && factory.get() != Py_None) {
            try {
                method(factory.get(), "request_wreck", engine);
            } catch (const p::Error&) {
                PyObject *type, *value, *trace;
                PyErr_Fetch(&type, &value, &trace);
                PyErr_NormalizeException(&type, &value, &trace);
                p::Ref held_type(type ? type : p::retain(Py_None).release()), held_value(value ? value : p::retain(Py_None).release());
                Py_XDECREF(trace);
                callKw(battle, "_warn_optional_failure", {p::text("dead wreck replacement").get(), held_value.get()}, {{"disable", Py_False}});
            }
            method(battle, "_finalize_dead_remote_wreck", record, entity.get());
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}


// BattleRuntime._apply_record_pose(record, pose)
PyObject* recordPose(PyObject*, PyObject* args) {
    PyObject *battle, *record, *pose, *replica, *engine_rotation, *moving_speed;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &record, &pose, &replica, &engine_rotation, &moving_speed)) return nullptr;
    try {
        if (truthy(method(replica, "owns", battle, record))) Py_RETURN_FALSE;
        auto state_raw = get(record, "state");
        auto state = truthy(state_raw) ? p::retain(state_raw.get()) : p::dict();
        auto read = [&](const char* name, PyObject* fallback) { return method(pose, "get", p::text(name).get(), fallback); };
        auto zero = p::number(0.);
        auto yaw = read("yaw", zero.get());
        if (truthy(get(record, "local"))) Py_RETURN_NONE;
        auto now = method(battle, "_clock");
        auto x = read("x", zero.get()), y = read("y", zero.get()), z = read("z", zero.get());
        auto pitch = read("pitch", zero.get()), roll = read("roll", zero.get());
        auto aim_yaw = read("aim_yaw", yaw.get()), gun_pitch = read("gun_pitch", zero.get());
        p::Ref xyz(PyTuple_Pack(3, x.get(), y.get(), z.get()));
        auto collision = method(battle, "_projectile_plain_pose", xyz.get(), pose);
        auto siege_raw = read("siege_state", method(state.get(), "get", p::text("siege_state").get(), integer(0).get()).get());
        if (PyObject_SetItem(collision.get(), p::text("siege_state").get(),
                             integer(strictInt(truthy(siege_raw) ? siege_raw.get() : integer(0).get())).get()) < 0)
            throw p::Error("Native collision pose write failed");
        put(record, "projectile_collision_pose", p::retain(collision.get()));
        auto health_raw = method(state.get(), "get", p::text("health").get(), integer(1).get());
        const bool alive = truthy(method(state.get(), "get", p::text("alive").get(), Py_True)) &&
                           strictInt(truthy(health_raw) ? health_raw.get() : integer(0).get()) > 0;
        PyObject* parts[] = {x.get(), y.get(), z.get(), yaw.get(), pitch.get(), roll.get(), aim_yaw.get(), gun_pitch.get()};
        p::Ref signature(PyTuple_New(8));
        for (int i = 0; i < 8; ++i) { Py_INCREF(parts[i]); PyTuple_SET_ITEM(signature.get(), i, parts[i]); }
        PyObject* engine = PyDict_GetItemString(record, "engine_id");
        if (!engine) { PyErr_SetObject(PyExc_KeyError, p::text("engine_id").get()); throw p::Error("engine_id"); }
        p::Ref identity(PyLong_FromVoidPtr(record));
        p::Ref lifecycle(PyTuple_Pack(3, attr(battle, "_generation").get(), engine, identity.get()));
        const bool pose_changed = !equal(signature.get(), get(record, "_remote_pose_signature").get()) ||
                                  !equal(lifecycle.get(), get(record, "_remote_pose_lifecycle").get());
        if (pose_changed) {
            method(record, "pop", p::text("_remote_pose_signature").get(), Py_None);
            auto vector = method(battle, "_vector", xyz.get());
            p::Ref rotation(PyObject_CallFunctionObjArgs(engine_rotation, yaw.get(), pitch.get(), roll.get(), nullptr));
            callKw(attr(battle, "_binding").get(), "set_vehicle_pose", {engine, vector.get(), rotation.get()}, {{"now", now.get()}});
            put(record, "_remote_pose_signature", p::retain(signature.get()));
            put(record, "_remote_pose_lifecycle", p::retain(lifecycle.get()));
            method(record, "pop", p::text("_remote_motion_settled_signature").get(), Py_None);
        } else {
            const double moving = p::scalar(moving_speed);
            auto field = [&](const char* name) { return number(method(state.get(), "get", p::text(name).get()).get()); };
            const bool intended = std::fabs(field("speed")) > moving || std::fabs(field("movement_dir")) > .5 ||
                                  std::fabs(field("rotation_dir")) > .5;
            if (truthy(get(record, "native_remote")) && !intended &&
                !equal(get(record, "_remote_motion_settled_signature").get(), signature.get())) {
                auto vehicle = method(attr(battle, "_remote_factory").get(), "get", engine);
                p::Ref settle(p::retain(Py_None));
                if (callableAttr(vehicle.get(), "settle_motion", settle)) {
                    p::Ref feature_args(PyTuple_Pack(1, now.get()));
                    if (truthy(method(battle, "_run_optional_feature", p::text("remote motion presentation").get(), settle.get(), feature_args.get())))
                        put(record, "_remote_motion_settled_signature", p::retain(signature.get()));
                }
            }
        }
        p::Ref aim_signature(PyTuple_Pack(6, lifecycle.get(), yaw.get(), pitch.get(), aim_yaw.get(), gun_pitch.get(),
                                          get(record, "presented_siege_state").get()));
        const bool aim_changed = !equal(aim_signature.get(), get(record, "_remote_aim_signature").get());
        if (aim_changed) {
            method(record, "pop", p::text("_remote_aim_signature").get(), Py_None);
            method(attr(battle, "_binding").get(), "update_vehicle_aim", engine, yaw.get(), aim_yaw.get(), gun_pitch.get());
            put(record, "_remote_aim_signature", p::retain(aim_signature.get()));
        }
        auto kind = get(record, "kind");
        if (textIs(kind.get(), "bot") || textIs(kind.get(), "player")) {
            auto turn = method(battle, "_remember_remote_track_turn", record, yaw.get(), now.get());
            p::Ref track(PyTuple_Pack(3, p::number(number(method(state.get(), "get", p::text("speed").get()).get())).get(),
                                      p::boolean(alive).get(), turn.get()));
            if (pose_changed || truthy(get(record, "_remote_track_pending")) ||
                !equal(track.get(), get(record, "_remote_track_state_signature").get())) {
                p::Ref feature_args(PyTuple_Pack(4, record, state.get(), now.get(), turn.get()));
                const bool updated = truthy(method(battle, "_run_optional_feature", p::text("remote track animation").get(),
                                                   attr(battle, "_update_bot_tracks").get(), feature_args.get()));
                put(record, "_remote_track_pending", p::boolean(!updated));
                if (updated) put(record, "_remote_track_state_signature", p::retain(track.get()));
            }
        }
        return p::boolean(pose_changed || aim_changed).release();
    } catch (const std::exception& e) { return fail(e); }
}


void incrementRevision(PyObject* battle) {
    auto revision = attr(battle, "_records_revision");
    setAttr(battle, "_records_revision", p::Ref(PyNumber_Add(revision.get(), integer(1).get())).get());
}
// _server_entity(engine) is not None, with ReferenceError meaning "not visible".
bool serverEntityVisible(PyObject* battle, PyObject* engine) {
    auto fn = attr(battle, "_server_entity");
    PyObject* entity = PyObject_CallFunctionObjArgs(fn.get(), engine, nullptr);
    if (!entity) {
        if (PyErr_ExceptionMatches(PyExc_ReferenceError)) { PyErr_Clear(); return false; }
        throw p::Error("Native server entity lookup failed");
    }
    p::Ref held(entity);
    return entity != Py_None;
}
p::Ref engineOf(PyObject* record) {
    PyObject* engine = PyDict_GetItemString(record, "engine_id");
    if (!engine) { PyErr_SetObject(PyExc_KeyError, p::text("engine_id").get()); throw p::Error("engine_id"); }
    return p::retain(engine);
}
// try: body() finally: always() -- the Python exception from body survives.
template<class Body, class Always> void tryFinally(Body body, Always always) {
    try { body(); }
    catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        try { always(); } catch (...) { Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace); throw; }
        PyErr_Restore(type, value, trace);
        throw;
    }
    always();
}
p::Ref deadState(PyObject* base, PyObject* event) {
    auto state = copyDict(get(base, "state").get());
    auto incoming = get(event, "state");
    if (truthy(incoming)) method(state.get(), "update", incoming.get());
    put(state.get(), "health", integer(0));
    put(state.get(), "alive", p::boolean(false));
    return state;
}

// BattleRuntime._flush_tombstone(record)
PyObject* flushTombstone(PyObject*, PyObject* args) {
    PyObject *battle, *record;
    if (!PyArg_ParseTuple(args, "OO", &battle, &record)) return nullptr;
    try {
        if (truthy(get(record, "presentation"))) {
            auto factory = attr(battle, "_remote_factory");
            if (factory.get() != Py_None) {
                if (equal(attr(battle, "_outlined_engine_id").get(), get(record, "engine_id").get())) method(battle, "_clear_target_outline");
                if (!truthy(get(record, "native_remote"))) method(battle, "_stop_remote_visual", record);
                method(factory.get(), "destroy", engineOf(record).get());
            }
            put(record, "visible_destroy_requested", p::boolean(true));
            Py_RETURN_NONE;
        }
        if (truthy(get(record, "visible_destroy_requested"))) Py_RETURN_NONE;
        auto engine = engineOf(record);
        if (!serverEntityVisible(battle, engine.get())) Py_RETURN_NONE;
        auto binding = attr(battle, "_binding");
        tryFinally([&] { method(binding.get(), "arena_vehicle_removed", engine.get()); },
                   [&] { method(binding.get(), "destroy_entity", engine.get()); });
        put(record, "visible_destroy_requested", p::boolean(true));
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._destroy_entity(event)
PyObject* destroyEntity(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto records = attr(battle, "_records");
        auto key = get(event, "entity");
        auto record = method(records.get(), "get", key.get());
        if (record.get() != Py_None && truthy(get(record.get(), "local"))) {
            put(record.get(), "state", deadState(record.get(), event));
            method(battle, "_materialize_record", record.get());
            Py_RETURN_NONE;
        }
        if (record.get() == Py_None) {
            auto pending_all = attr(battle, "_pending_bot_creates");
            auto pending = method(pending_all.get(), "get", key.get());
            if (pending.get() != Py_None && truthy(get(event, "keep_corpse"))) {
                put(pending.get(), "state", deadState(pending.get(), event));
                Py_RETURN_NONE;
            }
            if (pending.get() != Py_None) {
                if (truthy(method(battle, "_pending_event_references", key.get()))) {
                    p::Ref text(PyObject_Str(key.get()));
                    throw p::Error("pending entity " + p::string(text.get()) + " was removed before its ordered event applied");
                }
                method(pending_all.get(), "pop", key.get(), Py_None);
                auto order = attr(battle, "_pending_bot_create_order");
                auto remove = attr(order.get(), "remove");
                PyObject* removed = PyObject_CallFunctionObjArgs(remove.get(), key.get(), nullptr);
                if (!removed) { if (!PyErr_ExceptionMatches(PyExc_ValueError)) throw p::Error("pending order removal failed"); PyErr_Clear(); }
                else Py_DECREF(removed);
            }
            Py_RETURN_NONE;
        }
        PyObject* r = record.get();
        if (!truthy(attr(battle, "_worker_mode"))) method(battle, "_fallback_postmortem_viewpoint", engineOf(r).get());
        if (truthy(get(event, "keep_corpse"))) {
            put(r, "state", deadState(r, event));
            method(battle, "_materialize_record", r);
            Py_RETURN_NONE;
        }
        if (truthy(get(r, "presentation"))) {
            if (equal(attr(battle, "_outlined_engine_id").get(), get(r, "engine_id").get())) method(battle, "_clear_target_outline");
            method(records.get(), "pop", key.get(), Py_None);
            incrementRevision(battle);
            if (truthy(get(r, "arena_added"))) method(attr(battle, "_binding").get(), "arena_vehicle_removed", engineOf(r).get());
            auto factory = attr(battle, "_remote_factory");
            if (factory.get() != Py_None) {
                if (!truthy(get(r, "native_remote"))) method(battle, "_stop_remote_visual", r);
                method(factory.get(), "destroy", engineOf(r).get());
            }
            Py_RETURN_NONE;
        }
        if (truthy(get(r, "ready"))) {
            method(records.get(), "pop", key.get(), Py_None);
            incrementRevision(battle);
            auto server = attr(battle, "_server");
            p::Ref forget(p::retain(Py_None));
            if (callableAttr(server.get(), "forgetVehicleEnter", forget)) p::Ref unused(PyObject_CallFunctionObjArgs(forget.get(), engineOf(r).get(), nullptr));
            if (serverEntityVisible(battle, engineOf(r).get())) {
                auto binding = attr(battle, "_binding");
                tryFinally([&] { method(binding.get(), "arena_vehicle_removed", engineOf(r).get()); },
                           [&] { method(binding.get(), "destroy_entity", engineOf(r).get()); });
            }
        } else {
            put(r, "tombstone", p::boolean(true));
            method(r, "pop", p::text("pending_pose").get(), Py_None);
            const bool visible = serverEntityVisible(battle, engineOf(r).get());
            put(r, "visible_destroy_requested", p::boolean(false));
            if (visible) method(battle, "_flush_tombstone", r);
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._update_entity(event)
PyObject* updateEntity(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto records = attr(battle, "_records");
        auto key = get(event, "entity");
        auto record = method(records.get(), "get", key.get());
        if (record.get() != Py_None && truthy(get(record.get(), "tombstone"))) Py_RETURN_NONE;
        if (record.get() == Py_None) {
            auto raw = get(event, "state");
            auto state = truthy(raw) ? p::retain(raw.get()) : p::dict();
            auto create = literal(5);
            put(create.get(), "type", p::text("create"));
            put(create.get(), "entity", p::retain(key.get()));
            put(create.get(), "kind", get(event, "kind"));
            put(create.get(), "id", get(event, "id"));
            put(create.get(), "state", p::retain(state.get()));
            method(battle, "_create_remote", create.get());
            replace(record, method(records.get(), "get", key.get()));
            if (record.get() == Py_None) Py_RETURN_NONE;
        }
        PyObject* r = record.get();
        auto pose = get(event, "pose");
        auto incoming_raw = get(event, "state");
        auto incoming = truthy(incoming_raw) ? p::retain(incoming_raw.get()) : p::dict();
        auto stamp_of = [&]() {
            auto stamp = get(event, "presentation_time_us");
            return stamp.get() != Py_None ? integer(strictInt(stamp.get())) : p::retain(Py_None);
        };
        if (truthy(get(event, "interpolated")) && !truthy(incoming)) {
            if (pose.get() == Py_None) Py_RETURN_NONE;
            if (truthy(get(r, "ready"))) {
                put(r, "presented_pose", p::Ref(PyDict_Copy(pose.get())));
                put(r, "presentation_time_us", stamp_of());
                put(r, "presentation_bracket", get(event, "presentation_bracket"));
                method(battle, "_apply_record_pose", r, pose.get());
                Py_RETURN_NONE;
            }
        }
        auto state = copyDict(get(r, "state").get());
        p::Ref copied(PyDict_Copy(incoming.get()));
        method(state.get(), "update", copied.get());
        put(r, "state", state);
        if (pose.get() != Py_None) {
            put(r, "pending_pose", p::Ref(PyDict_Copy(pose.get())));
            put(r, "presented_pose", p::Ref(PyDict_Copy(pose.get())));
            put(r, "presentation_time_us", stamp_of());
            put(r, "presentation_bracket", get(event, "presentation_bracket"));
        }
        method(battle, "_materialize_record", r);
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._apply_sync_event(event)
PyObject* syncEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto phase = attr(battle, "state");
        for (const char* stopped : {"failed", "stopped", "leaving"}) if (textIs(phase.get(), stopped)) Py_RETURN_NONE;
        auto kind = get(event, "type");
        const bool bot = textIs(get(event, "kind").get(), "bot");
        if (textIs(kind.get(), "create")) {
            method(battle, bot ? "_queue_bot_create" : "_create_remote", event);
        } else if (textIs(kind.get(), "update")) {
            auto records = attr(battle, "_records");
            const int present = PySequence_Contains(records.get(), get(event, "entity").get());
            if (present < 0) throw p::Error("Native record lookup failed");
            if (bot && !present) method(battle, "_queue_bot_create", event);
            else {
                auto bots = attr(battle, "_bots");
                if (bot && bots.get() != Py_None && truthy(method(bots.get(), "is_authority"))) {
                    p::Ref copy(PyDict_Copy(event));
                    method(copy.get(), "pop", p::text("pose").get(), Py_None);
                    method(battle, "_update_entity", copy.get());
                } else method(battle, "_update_entity", event);
            }
        } else if (textIs(kind.get(), "destroy")) {
            method(battle, "_destroy_entity", event);
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._queue_bot_create(event)
PyObject* queueBotCreate(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto key = get(event, "entity");
        auto records = attr(battle, "_records");
        if (!truthy(key)) Py_RETURN_FALSE;
        const int present = PySequence_Contains(records.get(), key.get());
        if (present < 0) throw p::Error("Native record lookup failed");
        if (present) Py_RETURN_FALSE;
        auto state = copyDict(get(event, "state").get());
        auto pose = get(event, "pose");
        if (PyDict_Check(pose.get()))
            for (const char* name : {"x", "y", "z", "yaw", "pitch", "roll", "aim_yaw", "turret_yaw", "gun_pitch", "siege_state"})
                if (PyDict_GetItemString(pose.get(), name)) put(state.get(), name, get(pose.get(), name));
        auto pending_all = attr(battle, "_pending_bot_creates");
        auto pending = method(pending_all.get(), "get", key.get());
        if (pending.get() == Py_None) {
            auto created = literal(6);
            put(created.get(), "type", p::text("create"));
            put(created.get(), "entity", p::retain(key.get()));
            put(created.get(), "kind", p::text("bot"));
            put(created.get(), "id", get(event, "id"));
            put(created.get(), "state", p::retain(state.get()));
            put(created.get(), "initial_state", p::Ref(PyDict_Copy(state.get())));
            if (PyObject_SetItem(pending_all.get(), key.get(), created.get()) < 0) throw p::Error("pending create write failed");
            method(attr(battle, "_pending_bot_create_order").get(), "append", key.get());
        } else {
            method(get(pending.get(), "state").get(), "update", state.get());
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._flush_pending_bot_create(now)
PyObject* flushBotCreate(PyObject*, PyObject* args) {
    PyObject *battle, *now;
    if (!PyArg_ParseTuple(args, "OO", &battle, &now)) return nullptr;
    try {
        auto order = attr(battle, "_pending_bot_create_order");
        if (PyObject_IsTrue(order.get()) <= 0) Py_RETURN_FALSE;
        if (attr(battle, "_bot_create_started").get() == Py_None) setAttr(battle, "_bot_create_started", now);
        auto key = p::item(order.get(), 0);
        auto pending_all = attr(battle, "_pending_bot_creates");
        auto last_team = attr(battle, "_last_bot_create_team");
        if (last_team.get() != Py_None) {
            p::Ref iterator(PyObject_GetIter(order.get()));
            while (PyObject* raw = PyIter_Next(iterator.get())) {
                p::Ref candidate(raw);
                auto pending = method(pending_all.get(), "get", candidate.get());
                auto pending_state = truthy(pending) ? get(pending.get(), "state") : p::retain(Py_None);
                auto team = truthy(pending_state) ? get(pending_state.get(), "team") : p::retain(Py_None);
                if (team.get() != Py_None && !equal(integer(strictInt(team.get())).get(), last_team.get())) { replace(key, std::move(candidate)); break; }
            }
            if (PyErr_Occurred()) throw p::Error("pending create iteration failed");
        }
        method(order.get(), "remove", key.get());
        auto event = method(pending_all.get(), "pop", key.get(), Py_None);
        auto records = attr(battle, "_records");
        if (event.get() == Py_None || PySequence_Contains(records.get(), key.get()) == 1) Py_RETURN_FALSE;
        method(battle, "_create_remote", event.get());
        const bool created = PySequence_Contains(records.get(), key.get()) == 1;
        if (created) {
            auto pending_state = get(event.get(), "state");
            auto team = truthy(pending_state) ? get(pending_state.get(), "team") : p::retain(Py_None);
            if (team.get() != Py_None) setAttr(battle, "_last_bot_create_team", integer(strictInt(team.get())).get());
        }
        if (created && PyObject_IsTrue(attr(battle, "_pending_bot_create_order").get()) <= 0 && !truthy(attr(battle, "_bots_ready_reported"))) {
            setAttr(battle, "_bots_ready_reported", Py_True);
            setAttr(battle, "_bot_create_finished", now);
            method(battle, "_report_memory", p::text("bots_ready").get());
        }
        return p::boolean(created).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._flush_pending_entities(now) after the native replica's own flush.
PyObject* flushPendingEntities(PyObject*, PyObject* args) {
    PyObject *battle, *now, *replica;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &now, &replica)) return nullptr;
    try {
        auto native = method(replica, "flush_pending", battle, now);
        if (native.get() != Py_None) return native.release();
        p::Ref items(PyDict_Items(attr(battle, "_records").get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items.get()); ++i) {
            PyObject* record = PyTuple_GET_ITEM(PyList_GET_ITEM(items.get(), i), 1);
            if (truthy(get(record, "tombstone"))) { method(battle, "_flush_tombstone", record); continue; }
            if (truthy(get(record, "ready"))) continue;
            if (truthy(method(battle, "_materialize_record", record))) continue;
            auto deadline = get(record, "ready_deadline");
            if (deadline.get() != Py_None && PyObject_RichCompareBool(now, deadline.get(), Py_GE) == 1) {
                p::Ref text(PyObject_Str(engineOf(record).get()));
                throw p::Error("Vehicle " + p::string(text.get()) + " did not enter world before timeout");
            }
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}


// BattleRuntime._create_remote(event)
PyObject* createRemote(PyObject*, PyObject* args) {
    PyObject *battle, *event, *marks_on_gun, *engine_rotation, *bot_planner;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &event, &marks_on_gun, &engine_rotation, &bot_planner)) return nullptr;
    try {
        auto records = attr(battle, "_records");
        auto key = get(event, "entity");
        const int present = PySequence_Contains(records.get(), key.get());
        if (present < 0) throw p::Error("Native record lookup failed");
        if (present) Py_RETURN_NONE;
        auto state = copyDict(get(event, "state").get());
        auto initial_raw = get(event, "initial_state");
        auto initial = truthy(initial_raw) ? p::Ref(PyDict_Copy(initial_raw.get())) : p::Ref(PyDict_Copy(state.get()));
        if (!PyDict_GetItemString(state.get(), "team") || !PyDict_GetItemString(state.get(), "slot")) Py_RETURN_NONE;
        auto kind = get(event, "kind");
        const bool bot = textIs(kind.get(), "bot"), player = textIs(kind.get(), "player");
        if (bot && (!PyDict_GetItemString(state.get(), "x") || !PyDict_GetItemString(state.get(), "z"))) Py_RETURN_NONE;
        const bool worker = truthy(attr(battle, "_worker_mode"));
        p::Ref descriptor(p::retain(Py_None));
        if (player && (worker || truthy(get(state.get(), "vehicle_compact_descr"))))
            replace(descriptor, method(battle, "_resolve_player_descriptor", state.get()));
        else {
            auto config = attr(battle, "_config");
            auto vehicle = PyDict_GetItemString(state.get(), "vehicle") ? get(state.get(), "vehicle")
                                                                         : p::Ref(PyObject_GetItem(config.get(), p::text("vehicle").get()));
            replace(descriptor, method(battle, "_resolve_descriptor", vehicle.get()));
        }
        auto binding = attr(battle, "_binding");
        auto team = integer(strictInt(get(state.get(), "team", integer(1).get()).get()));
        auto properties = method(binding.get(), "properties_from_compact_descr", method(descriptor.get(), "makeCompactDescr").get(),
                                 team.get(), get(state.get(), "name", p::text("Vehicle").get()).get());
        p::Ref info(PyObject_GetItem(properties.get(), p::text("publicInfo").get()));
        auto set_info = [&](const char* name, p::Ref value) {
            if (PyObject_SetItem(info.get(), p::text(name).get(), value.get()) < 0) throw p::Error("publicInfo write failed");
        };
        set_info("outfit", method(battle, "_remote_outfit", state.get(), kind.get()));
        set_info("prebattleID", method(battle, "_lan_prebattle_id", state.get(), kind.get()));
        set_info("marksOnGun", player ? p::Ref(PyObject_CallFunctionObjArgs(marks_on_gun, get(state.get(), "marks_on_gun").get(), nullptr)) : integer(0));
        auto max_health = attr(descriptor.get(), "maxHealth");
        const std::int64_t cap = strictInt(max_health.get());
        const std::int64_t initial_health = strictInt(get(initial.get(), "health", max_health.get()).get());
        if (PyObject_SetItem(properties.get(), p::text("health").get(), integer(std::max<std::int64_t>(0, std::min(initial_health, cap))).get()) < 0)
            throw p::Error("properties health write failed");
        auto world = method(battle, "_state_world_pose", state.get());
        auto position = p::item(world.get(), 0), yaw = p::item(world.get(), 1);
        if (p::size(world.get()) != 2) throw p::Error("too many values to unpack");
        auto factory = attr(battle, "_remote_factory");
        if (factory.get() == Py_None) throw p::Error("remote vehicle factory is unavailable");
        p::Ref rotation(PyObject_CallFunctionObjArgs(engine_rotation, yaw.get(), nullptr));
        auto engine = method(factory.get(), "create", descriptor.get(), properties.get(), method(battle, "_vector", position.get()).get(), rotation.get());
        if (engine.get() == Py_None) throw p::Error("remote presentation returned no vehicle id");
        p::Ref setter(p::retain(Py_None));
        if (callableAttr(factory.get(), "set_entity_interpolate_motion", setter)) {
            p::Ref interpolate(p::boolean(false));
            if (bot) {
                auto start = attr(battle, "_start_message");
                auto authority = truthy(start) ? method(start.get(), "get", p::text("bot_authority_id").get()) : p::retain(Py_None);
                replace(interpolate, method(battle, "_bot_authority_is_local", authority.get()));
            }
            p::Ref unused(PyObject_CallFunctionObjArgs(setter.get(), engine.get(), interpolate.get(), nullptr));
        }
        auto native_entities = p::boolean(truthy(optionalAttr(factory.get(), "native_entities")));
        auto own_team = optionalAttr(attr(battle, "client").get(), "team");
        if (own_team.get() == Py_None && !PyObject_HasAttrString(attr(battle, "client").get(), "team")) replace(own_team, integer(1));
        auto visibility = [&]() {
            return method(bot_planner, "bot_initially_visible", team.get(), integer(strictInt(own_team.get())).get(), Py_True);
        };
        auto penalty_raw = get(state.get(), "shot_penalty_until", p::number(0.).get());
        auto record = literal(18);
        put(record.get(), "engine_id", p::retain(engine.get()));
        put(record.get(), "state", p::retain(state.get()));
        put(record.get(), "kind", p::retain(kind.get()));
        put(record.get(), "network_id", get(event, "id"));
        put(record.get(), "local", p::boolean(false));
        put(record.get(), "presentation", p::boolean(true));
        put(record.get(), "ready", p::boolean(false));
        put(record.get(), "arena_added", p::retain(native_entities.get()));
        put(record.get(), "native_remote", p::retain(native_entities.get()));
        put(record.get(), "properties", p::retain(properties.get()));
        put(record.get(), "spot_visible", visibility());
        put(record.get(), "spot_marker_visible", visibility());
        put(record.get(), "spot_until", p::number(0.));
        put(record.get(), "radio_spot_until", p::number(0.));
        put(record.get(), "spot_next", p::number(0.));
        put(record.get(), "shot_penalty_until", p::number(number(truthy(penalty_raw) ? penalty_raw.get() : p::number(0.).get())));
        // The pinned literal evaluates each value in order: the clock follows the spotting gates.
        auto now = method(battle, "_clock");
        auto timeout = method(attr(battle, "_config").get(), "get", p::text("startupTimeoutSeconds").get(), p::number(30.).get());
        put(record.get(), "ready_deadline", p::Ref(PyNumber_Add(now.get(), p::number(number(timeout.get())).get())));
        if (PyObject_SetItem(records.get(), key.get(), record.get()) < 0) throw p::Error("record write failed");
        incrementRevision(battle);
        p::Ref seed(PyDict_Copy(state.get()));
        auto placed = literal(4);
        put(placed.get(), "x", p::item(position.get(), 0));
        put(placed.get(), "y", p::item(position.get(), 1));
        put(placed.get(), "z", p::item(position.get(), 2));
        put(placed.get(), "yaw", p::retain(yaw.get()));
        if (PyDict_Update(seed.get(), placed.get()) < 0) throw p::Error("seed pose update failed");
        put(record.get(), "projectile_collision_pose", method(battle, "_projectile_boundary_pose", position.get(), seed.get()));
        method(battle, "_materialize_record", record.get());
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._materialize_record(record)
PyObject* materializeRecord(PyObject*, PyObject* args) {
    PyObject *battle, *record, *set_draw_visibility;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &record, &set_draw_visibility)) return nullptr;
    try {
        auto factory = attr(battle, "_remote_factory");
        if (truthy(get(record, "ready"))) {
        } else if (truthy(get(record, "presentation"))) {
            auto engine = engineOf(record);
            auto error = method(factory.get(), "error", engine.get());
            if (error.get() != Py_None) {
                p::Ref e(PyObject_Str(engine.get())), why(PyObject_Str(error.get()));
                throw p::Error("remote vehicle " + p::string(e.get()) + " failed: " + p::string(why.get()));
            }
            if (!truthy(method(factory.get(), "is_ready", engine.get()))) Py_RETURN_FALSE;
            put(record, "ready", p::boolean(true));
        } else {
            auto engine = engineOf(record);
            p::Ref status(PyTuple_Pack(2, p::text("completed").get(), Py_None));
            p::Ref getter(p::retain(Py_None));
            if (callableAttr(attr(battle, "_server").get(), "vehicleEnterStatus", getter))
                replace(status, p::Ref(PyObject_CallFunctionObjArgs(getter.get(), engine.get(), nullptr)));
            auto phase = p::item(status.get(), 0);
            if (textIs(phase.get(), "failed")) {
                p::Ref e(PyObject_Str(engine.get())), why(PyObject_Str(p::item(status.get(), 1).get()));
                throw p::Error("Vehicle " + p::string(e.get()) + " enter failed: " + p::string(why.get()));
            }
            const bool ready = textIs(phase.get(), "completed") && truthy(method(attr(battle, "_binding").get(), "is_vehicle_ready", engine.get()));
            if (!ready) Py_RETURN_FALSE;
            put(record, "ready", p::boolean(true));
        }
        const bool worker = truthy(attr(battle, "_worker_mode"));
        const bool presentation = truthy(get(record, "presentation"));
        p::Ref vehicle(p::retain(Py_None));
        if (presentation) {
            replace(vehicle, method(factory.get(), "get", engineOf(record).get()));
            if (vehicle.get() == Py_None || attr(vehicle.get(), "model").get() == Py_None) throw p::Error("remote vehicle has no ready presentation");
            if (!truthy(get(record, "presentation_initialized"))) {
                const bool initially = truthy(get(record, "spot_visible", Py_True));
                setAttr(vehicle.get(), "_spot_visible", initially ? Py_True : Py_False);
                if (worker) put(record, "simulation_entity", p::boolean(true));
                else if (truthy(get(record, "native_remote"))) {
                    setAttr(vehicle.get(), "_offlineNativeDrawVisible", initially ? Py_True : Py_False);
                    p::Ref unused(PyObject_CallFunctionObjArgs(set_draw_visibility, vehicle.get(), initially ? Py_True : Py_False, nullptr));
                    setAttr(vehicle.get(), "targetCaps", p::list().get());
                    auto marker = optionalAttr(vehicle.get(), "_offlineNativeMarkerVisible");
                    const bool started = marker.get() == Py_None && !PyObject_HasAttrString(vehicle.get(), "_offlineNativeMarkerVisible")
                        ? initially : truthy(marker);
                    put(record, "visual_started", p::boolean(started));
                    put(record, "world_marker_started", p::boolean(truthy(get(record, "visual_started"))));
                    put(record, "minimap_started", p::boolean(truthy(get(record, "visual_started"))));
                    setAttr(vehicle.get(), "_offlineNativeMarkerVisible", truthy(get(record, "world_marker_started")) ? Py_True : Py_False);
                } else {
                    method(attr(vehicle.get(), "appearance").get(), "changeVisibility", initially ? Py_True : Py_False);
                }
                put(record, "presentation_initialized", p::boolean(true));
            }
        }
        if (!worker && presentation && !truthy(get(record, "arena_added"))) {
            auto info = literal(2);
            put(info.get(), "properties", get(record, "properties"));
            auto state_raw = get(record, "state");
            auto state = truthy(state_raw) ? p::retain(state_raw.get()) : p::dict();
            put(info.get(), "team_killer", p::boolean(truthy(method(state.get(), "get", p::text("team_killer").get(), Py_False))));
            method(attr(battle, "_binding").get(), "arena_vehicle_added", engineOf(record).get(), info.get());
            put(record, "arena_added", p::boolean(true));
            method(battle, "_start_team_chat_when_roster_ready");
        }
        if (!worker && presentation && !truthy(get(record, "native_remote")) && truthy(get(record, "arena_added")) &&
            truthy(get(record, "spot_visible", Py_True)) && !truthy(get(record, "visual_started"))) {
            method(attr(battle, "_binding").get(), "start_vehicle_visual", engineOf(record).get(), Py_True);
            put(record, "visual_started", p::boolean(true));
            put(record, "world_marker_started", p::boolean(true));
            put(record, "minimap_started", p::boolean(true));
            if (!truthy(method(battle, "_record_alive", record, vehicle.get()))) method(battle, "_present_vehicle_dead", record, Py_True);
        }
        if (presentation && !worker) {
            auto spot = get(record, "spot_visible", Py_True);
            auto marker = get(record, "spot_marker_visible", spot.get());
            method(battle, "_set_record_spot_visibility", record, spot.get(), marker.get());
        }
        auto pose = method(record, "pop", p::text("pending_pose").get(), Py_None);
        if (pose.get() != Py_None) method(battle, "_apply_record_pose", record, pose.get());
        auto state_of = [&]() { auto raw = get(record, "state"); return truthy(raw) ? p::retain(raw.get()) : p::dict(); };
        auto state = state_of();
        method(battle, "_apply_siege_state", record, state.get());
        method(battle, "_apply_vehicle_statistics", record, state.get());
        method(battle, "_drain_event_journal");
        replace(state, state_of());
        if (truthy(method(battle, "_pending_combat_for_record", record))) Py_RETURN_TRUE;
        method(battle, "_apply_stun_state", record, state.get());
        auto critical = method(state.get(), "get", p::text("critical").get());
        if (PyDict_Check(critical.get())) method(battle, "_apply_critical_state", record, critical.get(), state.get());
        else if (textIs(get(record, "kind").get(), "player") && PySequence_Contains(state.get(), p::text("critical_revision").get()) == 1 &&
                 PySequence_Contains(state.get(), p::text("critical_base_revision").get()) == 1 &&
                 PySequence_Contains(state.get(), p::text("critical_ack_seq").get()) == 1)
            method(battle, "_reconcile_critical_authority", record, state.get());
        auto attacker = method(battle, "_death_attacker_engine_id", state.get());
        auto reason_raw = method(state.get(), "get", p::text("death_reason").get(), integer(0).get());
        auto reason = integer(std::max<std::int64_t>(0, strictInt(truthy(reason_raw) ? reason_raw.get() : integer(0).get())));
        auto suppress = method(battle, "_should_suppress_postmortem_killer", record, state.get(), attacker.get());
        callKw(battle, "_apply_health", {record, state.get(), attacker.get(), reason.get()}, {{"suppress_postmortem_killer", suppress.get()}});
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
    {"remote_visual_components", remoteVisualComponentsPython, METH_VARARGS, "Split the #1513 marker and minimap signals."},
    {"spot_visibility", spotVisibility, METH_VARARGS, "Apply model and team-knowledge presentation gates."},
    {"spot_presentation", spotPresentation, METH_VARARGS, "Publish a split spotting state through the legacy seam."},
    {"turret_detachment", turretDetachment, METH_VARARGS, "Remove attached turret armour; the worker plans debris."},
    {"finalize_dead_wreck", finalizeDeadWreck, METH_VARARGS, "Expose one finalized corpse without re-entering spotting."},
    {"apply_health", applyHealth, METH_VARARGS, "Apply a durable health/crew transition and its #1513 presentation."},
    {"record_pose", recordPose, METH_VARARGS, "Present one remote pose sample through the matrix and aim owners."},
    {"create_remote", createRemote, METH_VARARGS, "Create one remote vehicle entity and its record."},
    {"materialize_record", materializeRecord, METH_VARARGS, "Finish readiness and apply a record's current state."},
    {"flush_tombstone", flushTombstone, METH_VARARGS, "Destroy a remote vehicle that entered after its removal."},
    {"destroy_entity", destroyEntity, METH_VARARGS, "Apply one replica destroy event."},
    {"update_entity", updateEntity, METH_VARARGS, "Apply one replica update event."},
    {"sync_event", syncEvent, METH_VARARGS, "Route one replica lifecycle event."},
    {"queue_bot_create", queueBotCreate, METH_VARARGS, "Coalesce one bot until its own createEntity callback."},
    {"flush_bot_create", flushBotCreate, METH_VARARGS, "Create the next queued bot in this render callback."},
    {"flush_pending_entities", flushPendingEntities, METH_VARARGS, "Finish pending entity readiness and tombstones."},
    {"present_critical", presentCritical, METH_VARARGS, "Map critical transitions to stock #1513 UI callbacks."},
    {"siege_state", siegeState, METH_VARARGS, "Apply a server-owned Siege transition through #1513's callback."},
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* events_methods();
PyMethodDef* frame_methods_table();
// Lifecycle and event-journal ports share one Python module.
PyMethodDef* lifecycle_methods() {
    static std::vector<PyMethodDef> all;
    if (all.empty()) {
        for (PyMethodDef* table : {methods, events_methods(), frame_methods_table()})
            for (PyMethodDef* m = table; m->ml_name; ++m) all.push_back(*m);
        all.push_back({nullptr, nullptr, 0, nullptr});
    }
    return all.data();
}
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
