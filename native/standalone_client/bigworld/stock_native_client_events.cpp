// Ordered LAN event journal: intake, readiness, dispatch and record-state merges, ported
// from the pinned battle runtime. Module constants come from the runtime's globals().
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace {
using namespace py;

p::Ref global(PyObject* g, const char* name) {
    PyObject* v = PyDict_GetItemString(g, name);
    if (!v) { PyErr_Format(PyExc_NameError, "global name '%s' is not defined", name); throw p::Error(name); }
    return p::retain(v);
}
bool member(PyObject* sequence, PyObject* v) {
    const int found = PySequence_Contains(sequence, v);
    if (found < 0) throw p::Error("Native membership test failed");
    return found != 0;
}
bool textIn(PyObject* v, std::initializer_list<const char*> options) {
    for (const char* option : options) if (equal(v, p::text(option).get())) return true;
    return false;
}
// The exception a Python `except Exception as error` would bind for the current failure.
p::Ref caught(const std::exception& e) {
    PyObject *type, *value, *trace;
    PyErr_Fetch(&type, &value, &trace);
    if (!type) return p::Ref(PyObject_CallFunction(PyExc_RuntimeError, const_cast<char*>("s"), e.what()));
    PyErr_NormalizeException(&type, &value, &trace);
    Py_XDECREF(type); Py_XDECREF(trace);
    return p::Ref(value);
}
p::Ref str(PyObject* v) { return p::Ref(PyObject_Str(v)); }
[[noreturn]] void raise(const std::string& message) { throw p::Error(message); }
p::Ref module(const char* name) { return p::Ref(PyImport_ImportModule(name)); }

// BattleRuntime._record_is_event_ready (staticmethod)
bool recordIsEventReady(PyObject* record) {
    if (record == Py_None) return false;
    if (!truthy(get(record, "ready", Py_True))) return false;
    if (truthy(get(record, "presentation")) && !truthy(get(record, "arena_added", Py_False)) &&
        !truthy(get(record, "simulation_entity", Py_False))) return false;
    return true;
}
// BattleRuntime._stun_entity_key (staticmethod)
p::Ref stunEntityKey(PyObject* event) {
    auto kind = get(event, "target_kind"), target = get(event, "target_id");
    if (!textIn(kind.get(), {"player", "bot"}) || !exactInteger(target.get()) ||
        PyObject_RichCompareBool(target.get(), integer(0).get(), Py_LE) == 1)
        raise("stun event has an invalid target identity");
    return key(p::string(str(kind.get()).get()).c_str(), integer(strictInt(target.get())).get());
}
// BattleRuntime._combat_event_source (staticmethod)
p::Ref combatEventSource(PyObject* event) {
    if (!PyDict_GetItemString(event, "source")) raise("ordered combat event has no source");
    auto source = get(event, "source");
    if (!textIn(source.get(), {"shot", "fire", "ram", "client_simulation", "player_left", "environment"}))
        raise("ordered combat event has invalid source: " + p::string(str(source.get()).get()));
    return source;
}
p::Ref recordsGet(PyObject* battle, PyObject* k) { return method(attr(battle, "_records").get(), "get", k); }
bool pendingCreate(PyObject* battle, PyObject* k) { return member(attr(battle, "_pending_bot_creates").get(), k); }
std::string keyText(PyObject* k) { return p::string(str(k).get()); }

PyObject* pendingEventReferences(PyObject*, PyObject* args) {
    PyObject *battle, *k;
    if (!PyArg_ParseTuple(args, "OO", &battle, &k)) return nullptr;
    try {
        p::Ref iterator(PyObject_GetIter(attr(battle, "_event_journal").get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref event(raw);
            if (equal(eventEntityKey(event.get(), false).get(), k) || equal(eventEntityKey(event.get(), true).get(), k)) Py_RETURN_TRUE;
        }
        if (PyErr_Occurred()) return nullptr;
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* knownEventState(PyObject*, PyObject* args) {
    PyObject *battle, *k;
    if (!PyArg_ParseTuple(args, "OO", &battle, &k)) return nullptr;
    try {
        auto record = recordsGet(battle, k);
        auto state_of = [](PyObject* holder) { auto s = get(holder, "state"); return truthy(s) ? p::retain(s.get()) : p::dict(); };
        if (record.get() != Py_None) return PyTuple_Pack(2, record.get(), state_of(record.get()).get());
        auto pending = method(attr(battle, "_pending_bot_creates").get(), "get", k);
        if (pending.get() != Py_None) return PyTuple_Pack(2, pending.get(), state_of(pending.get()).get());
        raise("ordered LAN event references unknown entity " + keyText(k));
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* missingProjectileAttackerAllowed(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto source = get(event, "source"), kind = get(event, "kind");
        const bool hit = textIn(kind.get(), {"hit", "bot_hit", "bot_human_hit", "bot_bot_hit"});
        if (textIn(source.get(), {"fire"}) && hit) Py_RETURN_TRUE;
        if (!textIn(source.get(), {"shot"}) || !hit) Py_RETURN_FALSE;
        auto projectile = get(event, "projectile_id");
        if (projectile.get() == Py_None) Py_RETURN_FALSE;
        return p::boolean(member(attr(battle, "_projectile_lineage").get(), str(projectile.get()).get())).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* mergeShotEventState(PyObject*, PyObject* args) {
    PyObject *battle, *event, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &g)) return nullptr;
    try {
        auto k = eventEntityKey(event, true);
        if (k.get() == Py_None) raise("ordered shot event has no attacker");
        auto known = method(battle, "_known_event_state", k.get());
        auto holder = p::item(known.get(), 0), state = p::item(known.get(), 1);
        auto seconds = attr(global(g, "spotting").get(), "SHOT_CAMOUFLAGE_SECONDS");
        auto deadline = p::Ref(PyNumber_Add(method(battle, "_clock").get(), seconds.get()));
        if (holder.get() == recordsGet(battle, k.get()).get()) {
            put(holder.get(), "shot_penalty_until", deadline);
            method(module("gui.mods.offline_lan_0922.native_client_environment").get(), "lifecycle", battle, holder.get());
        } else {
            p::Ref copied(PyDict_Copy(state.get()));
            put(copied.get(), "shot_penalty_until", deadline);
            put(holder.get(), "state", copied);
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* combatEventState(PyObject*, PyObject* args) {
    PyObject *battle, *event, *raw_state, *target_key;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &event, &raw_state, &target_key)) return nullptr;
    try {
        if (!PyDict_GetItemString(event, "health")) raise("ordered combat event has no health");
        if (!PyDict_GetItemString(event, "death_reason")) raise("ordered combat event has no death_reason");
        auto state = copyDict(raw_state);
        const std::int64_t health = std::max<std::int64_t>(0, strictInt(get(event, "health", integer(0).get()).get()));
        put(state.get(), "health", integer(health));
        const bool alive = health > 0 && !truthy(get(event, "dead", Py_False));
        put(state.get(), "alive", p::boolean(alive));
        put(state.get(), "display_health", integer(std::max<std::int64_t>(0, strictInt(get(event, "display_health", integer(health).get()).get()))));
        std::int64_t death_reason = 0;
        if (!pyInt(get(event, "death_reason").get(), death_reason)) raise("ordered combat event has invalid death_reason");
        if (death_reason < 0) raise("ordered combat event has invalid death_reason");
        if (alive && death_reason != 0) raise("nonfatal combat event has nonzero death_reason");
        put(state.get(), "death_reason", integer(death_reason));
        auto attacker = eventEntityKey(event, true);
        if (attacker.get() != Py_None) {
            const std::string text = keyText(attacker.get());
            const auto colon = text.find(':');
            put(state.get(), "death_attacker_kind", p::text(text.substr(0, colon)));
            put(state.get(), "death_attacker_id", integer(strictInt(p::text(text.substr(colon + 1)).get())));
        }
        auto critical = get(event, "critical");
        if (PyDict_Check(critical.get()) && keyText(target_key).rfind("bot:", 0) == 0)
            put(state.get(), "critical", method(battle, "_critical_state", critical.get()));
        for (const char* name : {"critical_revision", "critical_base_revision", "critical_ack_seq",
                                 "combat_revision", "combat_base_revision", "combat_ack_seq"})
            if (PyDict_GetItemString(event, name)) put(state.get(), name, get(event, name));
        return state.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* mergeCombatEventState(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto target = eventEntityKey(event, false);
        if (target.get() == Py_None) raise("ordered combat event has no target");
        auto known = method(battle, "_known_event_state", target.get());
        auto holder = p::item(known.get(), 0), state = p::item(known.get(), 1);
        auto attacker = eventEntityKey(event, true);
        if (attacker.get() != Py_None) {
            auto fn = attr(battle, "_known_event_state");
            PyObject* result = PyObject_CallFunctionObjArgs(fn.get(), attacker.get(), nullptr);
            if (result) Py_DECREF(result);
            else if (PyErr_ExceptionMatches(PyExc_RuntimeError)) {
                PyObject *type, *value, *trace;
                PyErr_Fetch(&type, &value, &trace);
                if (!truthy(method(battle, "_missing_projectile_attacker_allowed", event))) { PyErr_Restore(type, value, trace); throw p::Error("attacker"); }
                Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
            } else throw p::Error("attacker");
        }
        put(holder.get(), "state", method(battle, "_combat_event_state", event, state.get(), target.get()));
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* mergeStunEventState(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto target = stunEntityKey(event);
        auto known = method(battle, "_known_event_state", target.get());
        auto holder = p::item(known.get(), 0), raw = p::item(known.get(), 1);
        auto active = get(event, "active"), end = get(event, "stun_end_server_time_ms");
        if (!PyBool_Check(active.get())) raise("stun event has an invalid active state");
        if (!exactInteger(end.get()) || PyObject_RichCompareBool(end.get(), integer(0).get(), Py_LT) == 1 ||
            (active.get() == Py_True) != (PyObject_RichCompareBool(end.get(), integer(0).get(), Py_GT) == 1))
            raise("stun event has an invalid end time");
        auto state = copyDict(raw.get());
        put(state.get(), "stun_end_server_time_ms", integer(strictInt(end.get())));
        auto factors = get(event, "stun_factors");
        put(state.get(), "stun_factors", truthy(end) ? copyDict(factors.get()) : p::dict());
        if (active.get() == Py_True) {
            auto kind = get(event, "attacker_kind"), attacker = get(event, "attacker_id");
            if (!textIn(kind.get(), {"player", "bot"})) raise("stun event has an invalid attacker identity");
            if (!exactInteger(attacker.get()) || PyObject_RichCompareBool(attacker.get(), integer(0).get(), Py_LE) == 1)
                raise("stun event has an invalid attacker identity");
            put(state.get(), "stun_attacker_kind", p::retain(kind.get()));
            put(state.get(), "stun_attacker_id", integer(strictInt(attacker.get())));
        } else {
            put(state.get(), "stun_attacker_kind", p::text(""));
            put(state.get(), "stun_attacker_id", integer(0));
        }
        put(holder.get(), "state", state);
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* combatAttackReason(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto source = combatEventSource(event);
        if (textIn(source.get(), {"player_left"})) {
            if (!PyDict_GetItemString(event, "attack_reason") || get(event, "attack_reason").get() != Py_None)
                raise("player_left event must have null attack_reason");
            if (!PyDict_GetItemString(event, "death_reason") || !equal(get(event, "death_reason").get(), integer(0).get()))
                raise("player_left event must have zero death_reason");
            if (PyDict_GetItemString(event, "attacker") || PyDict_GetItemString(event, "attacker_bot"))
                raise("player_left event must not have an attacker");
            Py_RETURN_NONE;
        }
        if (!PyDict_GetItemString(event, "attack_reason")) raise("ordered combat event has no attack_reason");
        std::int64_t reason = 0;
        if (!pyInt(get(event, "attack_reason").get(), reason)) raise("ordered combat event has invalid attack_reason");
        if (reason < 0) raise("ordered combat event has invalid attack_reason");
        auto reasonOf = [&](const char* name, std::int64_t fallback) {
            return dispatch(battle, "_attack_reason", "attack_reason", {p::text(name).get(), integer(fallback).get()});
        };
        auto shot = reasonOf("SHOT", 0), fire = reasonOf("FIRE", 1), ram = reasonOf("RAM", 2);
        auto value = integer(reason);
        if (textIn(source.get(), {"client_simulation"})) return value.release();
        if (textIn(source.get(), {"environment"})) {
            const bool dead = truthy(get(event, "dead", Py_False));
            auto world = reasonOf("WORLD_COLLISION", 3), drowning = reasonOf("DROWNING", 5), overturn = reasonOf("OVERTURN", 7);
            auto death = get(event, "death_reason");
            const bool valid = (equal(value.get(), world.get()) && equal(death.get(), dead ? world.get() : integer(0).get())) ||
                               ((equal(value.get(), drowning.get()) || equal(value.get(), overturn.get())) && dead && equal(death.get(), value.get()));
            if (!valid) raise("environment event has invalid cause");
            return value.release();
        }
        auto expected = textIn(source.get(), {"shot"}) ? std::move(shot) : textIn(source.get(), {"fire"}) ? std::move(fire) : std::move(ram);
        if (!equal(value.get(), expected.get()))
            raise("ordered combat event attack_reason does not match source: " + std::to_string(reason) + " != " +
                  p::string(str(expected.get()).get()));
        return value.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* validateCombatEventContract(PyObject*, PyObject* args) {
    PyObject *battle, *event, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &g)) return nullptr;
    try {
        auto cosmetic = get(event, "cosmetic_only", Py_False);
        if (!PyBool_Check(cosmetic.get())) raise("combat cosmetic_only flag is invalid");
        if (cosmetic.get() == Py_True) {
            auto damage = get(event, "damage"), health = get(event, "health");
            const bool bad = !textIn(get(event, "source").get(), {"shot"}) || !equal(damage.get(), integer(0).get()) ||
                PyBool_Check(damage.get()) || truthy(get(event, "dead", Py_False)) || !equal(get(event, "death_reason").get(), integer(0).get()) ||
                PyBool_Check(health.get()) || !exactInteger(health.get()) || PyObject_RichCompareBool(health.get(), integer(0).get(), Py_LE) == 1 ||
                !(equal(get(event, "shot_result").get(), integer(0).get()) || equal(get(event, "shot_result").get(), integer(1).get())) ||
                truthy(get(event, "splash", Py_False)) || get(event, "critical").get() != Py_None;
            if (bad) raise("state-changing combat cannot be cosmetic_only");
        }
        auto source = combatEventSource(event);
        auto attack_reason = method(battle, "_combat_attack_reason", event);
        auto kind = get(event, "kind");
        const bool any_hit = textIn(kind.get(), {"hit", "bot_hit", "bot_human_hit", "bot_bot_hit"});
        bool allowed = false;
        if (textIn(source.get(), {"shot", "ram"})) allowed = any_hit;
        else if (textIn(source.get(), {"fire"})) allowed = any_hit || textIn(kind.get(), {"health"});
        else if (textIn(source.get(), {"client_simulation", "player_left"})) allowed = textIn(kind.get(), {"health"});
        else allowed = get(event, "push_attributed").get() == Py_True ? (any_hit || textIn(kind.get(), {"health"})) : textIn(kind.get(), {"health"});
        if (!allowed) raise("ordered combat event source " + p::string(str(source.get()).get()) + " does not allow kind " + p::string(str(kind.get()).get()));
        auto blocked = get(event, "blocked_damage", integer(0).get());
        if (!exactInteger(blocked.get()) || PyObject_RichCompareBool(blocked.get(), integer(0).get(), Py_LT) == 1 ||
            PyObject_RichCompareBool(blocked.get(), integer(5000).get(), Py_GT) == 1)
            raise("ordered combat event has invalid blocked_damage");
        if (truthy(blocked) && (!textIn(source.get(), {"shot"}) || truthy(get(event, "splash", Py_False)) ||
                                strictInt(get(event, "shot_result", integer(2).get()).get()) == 2))
            raise("ordered combat event has inconsistent blocked_damage");
        if (PyDict_GetItemString(event, "damage_sticker")) {
            auto sticker = get(event, "damage_sticker");
            auto maximum = attr(global(g, "lan_protocol").get(), "MAX_PROJECTILE_DAMAGE_STICKER");
            if (!textIn(source.get(), {"shot"}) || truthy(get(event, "splash", Py_False)) || !exactInteger(sticker.get()) ||
                PyObject_RichCompareBool(sticker.get(), integer(0).get(), Py_LT) == 1 ||
                PyObject_RichCompareBool(sticker.get(), maximum.get(), Py_GT) == 1)
                raise("ordered combat event has invalid damage_sticker");
        }
        auto attacker = eventEntityKey(event, true);
        const bool unowned_fire = textIn(source.get(), {"fire"}) && textIn(kind.get(), {"health"});
        if (unowned_fire && (PyDict_GetItemString(event, "attacker") || PyDict_GetItemString(event, "attacker_bot")))
            raise("unowned fire event must not have an attacker");
        if (textIn(source.get(), {"shot", "fire", "ram"}) && !unowned_fire && attacker.get() == Py_None)
            raise("ordered " + p::string(str(source.get()).get()) + " combat event has no attacker");
        if (textIn(source.get(), {"client_simulation", "player_left"}) && attacker.get() != Py_None)
            raise("ordered " + p::string(str(source.get()).get()) + " combat event must not have an attacker");
        if (textIn(source.get(), {"environment"}) && attacker.get() != Py_None && get(event, "push_attributed").get() != Py_True)
            raise("environment attacker has no push attribution");
        return PyTuple_Pack(2, source.get(), attack_reason.get());
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* prepareOrderedEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &g)) return nullptr;
    try {
        auto kind = get(event, "kind");
        if (member(global(g, "_SHOT_EVENT_KINDS").get(), kind.get())) {
            method(battle, "_merge_shot_event_state", event);
            auto normalized = method(battle, "_projectile_wire_meta", event);
            if (normalized.get() != Py_None)
                method(attr(battle, "_projectile_lineage").get(), "add", p::Ref(PyObject_GetItem(normalized.get(), p::text("projectile_id").get())).get());
        } else if (member(global(g, "_COMBAT_EVENT_KINDS").get(), kind.get())) {
            method(battle, "_validate_combat_event_contract", event);
            if (!truthy(get(event, "cosmetic_only", Py_False))) method(battle, "_merge_combat_event_state", event);
        } else if (textIn(kind.get(), {"stun"})) {
            method(battle, "_merge_stun_event_state", event);
        } else if (!member(global(g, "_SIMPLE_EVENT_KINDS").get(), kind.get())) {
            raise("ordered LAN event kind is unsupported: " + p::string(str(kind.get()).get()));
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* onEvents(PyObject*, PyObject* args) {
    PyObject *battle, *message;
    if (!PyArg_ParseTuple(args, "OO", &battle, &message)) return nullptr;
    try {
        if (textIn(attr(battle, "state").get(), {"failed", "stopped", "leaving"})) Py_RETURN_FALSE;
        auto ignore = [&](PyObject* error) { method(battle, "_ignore_live_payload", p::text("events").get(), error, message); };
        try {
            method(battle, "_observe_destructibles_disabled", message);
            method(battle, "_observe_projectile_message", truthy(p::retain(message)) ? message : p::dict().get());
        } catch (const std::exception& e) {
            auto error = caught(e);
            ignore(error.get());
            Py_RETURN_FALSE;
        }
        auto body = truthy(p::retain(message)) ? p::retain(message) : p::dict();
        auto events_raw = method(body.get(), "get", p::text("events").get());
        auto events = truthy(events_raw) ? p::retain(events_raw.get()) : p::Ref(PyTuple_New(0));
        auto accepted = attr(battle, "_accepted_event_ids"), applied = attr(battle, "_applied_event_ids");
        auto journal = attr(battle, "_event_journal");
        p::Ref iterator(PyObject_GetIter(events.get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref raw_event(raw);
            p::Ref event_id(p::retain(Py_None));
            try {
                if (!PyDict_Check(raw_event.get())) raise("ordered LAN event is malformed");
                p::Ref event(PyDict_Copy(raw_event.get()));
                replace(event_id, get(event.get(), "event_id"));
                if (!truthy(event_id)) raise("ordered LAN event has no event_id");
                replace(event_id, str(event_id.get()));
                if (member(accepted.get(), event_id.get())) continue;
                method(battle, "_prepare_ordered_event", event.get());
                method(accepted.get(), "add", event_id.get());
                method(journal.get(), "append", event.get());
            } catch (const std::exception& e) {
                auto error = caught(e);
                if (truthy(event_id)) {
                    auto text = str(event_id.get());
                    method(accepted.get(), "add", text.get());
                    method(applied.get(), "add", text.get());
                }
                ignore(error.get());
            }
        }
        if (PyErr_Occurred()) return nullptr;
        method(battle, "_drain_event_journal");
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* eventIsReady(PyObject*, PyObject* args) {
    PyObject *battle, *event, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &g)) return nullptr;
    try {
        auto kind = get(event, "kind");
        const bool worker = truthy(attr(battle, "_worker_mode"));
        if (textIn(kind.get(), {"environment_critical"}) && !worker) {
            for (bool attacker : {false, true}) {
                auto k = eventEntityKey(event, attacker);
                auto record = recordsGet(battle, k.get());
                if ((pendingCreate(battle, k.get()) || record.get() != Py_None) && !recordIsEventReady(record.get())) Py_RETURN_FALSE;
            }
            Py_RETURN_TRUE;
        }
        const bool replay = truthy(attr(battle, "_replay_mode"));
        if (!worker && !replay && textIn(kind.get(), {"projectile_impact", "projectile_ricochet"})) {
            auto metas = attr(battle, "_projectile_meta");
            auto raw_meta = method(metas.get(), "get", str(get(event, "projectile_id").get()).get());
            auto meta = truthy(raw_meta) ? p::retain(raw_meta.get()) : p::dict();
            auto launch = method(meta.get(), "get", p::text("bot_presentation_time_us").get());
            if (launch.get() != Py_None) {
                auto elapsed = method(event, "get", p::text("resolved_time_ms").get(),
                                      method(event, "get", p::text("segment_start_time_ms").get(), integer(0).get()).get());
                auto visual = method(meta.get(), "get", p::text("bot_visual_launch_time").get());
                if (visual.get() == Py_None ||
                    PyObject_RichCompareBool(method(battle, "_clock").get(),
                        p::number(p::scalar(visual.get()) + double(strictInt(elapsed.get())) / 1000.).get(), Py_LT) == 1) {
                    put(event, "_presentation_wait", p::boolean(true));
                    Py_RETURN_FALSE;
                }
            }
        }
        if (member(global(g, "_SHOT_EVENT_KINDS").get(), kind.get())) {
            auto k = eventEntityKey(event, true);
            auto record = recordsGet(battle, k.get());
            if (record.get() == Py_None && !pendingCreate(battle, k.get())) raise("ordered LAN event lost entity " + keyText(k.get()) + " before apply");
            if (!recordIsEventReady(record.get())) Py_RETURN_FALSE;
            if (!worker && !replay && get(event, "attacker_bot").get() != Py_None && get(event, "bot_presentation_time_us").get() != Py_None) {
                auto state_raw = get(record.get(), "state");
                auto state = truthy(state_raw) ? p::retain(state_raw.get()) : p::dict();
                if (method(state.get(), "get", p::text("alive").get(), Py_True).get() == Py_False) Py_RETURN_TRUE;
                auto presented = get(record.get(), "presentation_time_us");
                auto launch = p::Ref(PyObject_GetItem(event, p::text("bot_presentation_time_us").get()));
                const bool ready = presented.get() != Py_None && strictInt(presented.get()) >= strictInt(launch.get());
                if (!ready) {
                    put(event, "_presentation_wait", p::boolean(true));
                    p::Ref clock(PyObject_CallFunctionObjArgs(global(g, "_PROFILE_CLOCK").get(), nullptr));
                    method(event, "setdefault", p::text("_pose_wait_started").get(), clock.get());
                } else if (PyDict_GetItemString(event, "_pose_wait_started")) {
                    p::Ref clock(PyObject_CallFunctionObjArgs(global(g, "_PROFILE_CLOCK").get(), nullptr));
                    auto started = method(event, "pop", p::text("_pose_wait_started").get());
                    const double waited = std::max(0., p::scalar(clock.get()) - p::scalar(started.get()));
                    p::Ref line(PyString_FromFormat("[Offline LAN 0.9.22] BOT SHOT POSE READY projectile=%s wait_ms=", p::string(str(get(event, "projectile_id").get()).get()).c_str()));
                    // Python formats the wait; the engine's precompiled header redefines snprintf.
                    p::Ref format(PyString_FromString("%.3f")), value(PyTuple_Pack(1, p::number(waited * 1000.).get()));
                    p::Ref wait(PyString_Format(format.get(), value.get()));
                    std::string text = p::string(line.get()) + p::string(wait.get()) + " pose_launch_us=" + p::string(str(launch.get()).get()) +
                                       " presented_us=" + p::string(str(presented.get()).get()) + "\n";
                    method(attr(module("sys").get(), "stdout").get(), "write", p::text(text).get());
                }
                return p::boolean(ready).release();
            }
            Py_RETURN_TRUE;
        }
        if (member(global(g, "_COMBAT_EVENT_KINDS").get(), kind.get())) {
            auto target = eventEntityKey(event, false);
            auto record = recordsGet(battle, target.get());
            if (record.get() == Py_None && !pendingCreate(battle, target.get())) raise("ordered LAN event lost entity " + keyText(target.get()) + " before apply");
            if (!recordIsEventReady(record.get())) Py_RETURN_FALSE;
            auto attacker = eventEntityKey(event, true);
            if (attacker.get() == Py_None) Py_RETURN_TRUE;
            auto attacker_record = recordsGet(battle, attacker.get());
            if (attacker_record.get() == Py_None && !pendingCreate(battle, attacker.get())) {
                if (truthy(method(battle, "_missing_projectile_attacker_allowed", event))) Py_RETURN_TRUE;
                raise("ordered LAN event lost entity " + keyText(attacker.get()) + " before apply");
            }
            return p::boolean(recordIsEventReady(attacker_record.get())).release();
        }
        if (textIn(kind.get(), {"stun"})) {
            auto target = stunEntityKey(event);
            auto record = recordsGet(battle, target.get());
            if (record.get() == Py_None && !pendingCreate(battle, target.get())) raise("ordered LAN event lost entity " + keyText(target.get()) + " before apply");
            return p::boolean(recordIsEventReady(record.get())).release();
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* applyOrderedEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &g)) return nullptr;
    try {
        auto kind = get(event, "kind");
        if (textIn(kind.get(), {"authority"})) {
            if (truthy(attr(battle, "_worker_mode")))
                method(module("gui.mods.offline_lan_0922.native_bot_runtime").get(), "projectile_event", attr(battle, "_bots").get(), event,
                       method(battle, "_clock").get());
            method(battle, "_set_projectile_epoch", get(event, "authority_epoch").get(), method(battle, "_clock").get());
            auto bots = attr(battle, "_bots");
            if (bots.get() != Py_None) {
                auto changed = method(battle, "_reconcile_bot_authority", get(event, "player_id").get());
                auto last = attr(battle, "_last_snapshot");
                if (truthy(changed) && last.get() != Py_None) method(bots.get(), "apply_snapshot", last.get());
            }
        } else if (member(global(g, "_SHOT_EVENT_KINDS").get(), kind.get())) {
            dispatch(battle, "_show_shot", "show_shot", {event}, {{"update_state", Py_False}}, {event, Py_False, g});
            method(battle, "_accept_projectile_event", event);
        } else if (member(global(g, "_COMBAT_EVENT_KINDS").get(), kind.get())) {
            callKw(battle, "_apply_combat_event", {event}, {{"update_state", Py_False}});
        } else if (textIn(kind.get(), {"vehicle_statistics"})) dispatch(battle, "_apply_vehicle_statistics_event", "vehicle_statistics_event", {event});
        else if (textIn(kind.get(), {"environment_critical"})) dispatch(battle, "_apply_environment_critical_feedback", "environment_critical_feedback", {event}, {g});
        else if (textIn(kind.get(), {"assist"})) dispatch(battle, "_apply_assist_event", "assist_event", {event});
        else if (textIn(kind.get(), {"detection"})) dispatch(battle, "_apply_detection_event", "detection_event", {event});
        else if (textIn(kind.get(), {"stun"})) {
            auto target = recordsGet(battle, stunEntityKey(event).get());
            if (target.get() == Py_None) raise("stun event has no ready target");
            auto state = get(target.get(), "state");
            method(battle, "_apply_stun_state", target.get(), truthy(state) ? state.get() : p::dict().get());
        } else if (textIn(kind.get(), {"destructible"})) {
            PyObject** own = _PyObject_GetDictPtr(battle);
            const bool overridden = own && *own && PyDict_GetItemString(*own, "_apply_destructible_event");
            auto applied = overridden ? method(battle, "_apply_destructible_event", event) :
                dispatch(battle, "_apply_destructible_event", "destructible_event", {event},
                         {module("gui.mods.offline_lan_0922.native_client_drive").get()});
            if (applied.get() == Py_None) Py_RETURN_FALSE;
        } else if (textIn(kind.get(), {"projectile_ricochet"})) method(battle, "_apply_projectile_ricochet_event", event);
        else if (textIn(kind.get(), {"projectile_impact"})) method(battle, "_apply_projectile_terminal_event", event);
        else if (textIn(kind.get(), {"battle_result"})) dispatch(battle, "_apply_battle_result", "battle_result", {event});
        else if (textIn(kind.get(), {"bot_manifest"})) {
        } else raise("ordered LAN event kind is unsupported: " + p::string(str(kind.get()).get()));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* drainEventJournal(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto journal = attr(battle, "_event_journal");
        auto applied = attr(battle, "_applied_event_ids");
        auto combat = global(g, "_COMBAT_EVENT_KINDS");
        Py_ssize_t attempts = PySequence_Size(journal.get());
        auto blocked = p::Ref(PySet_New(nullptr));
        Py_ssize_t index = 0;
        auto ignore = [&](PyObject* error, PyObject* event) {
            auto start = attr(battle, "_start_message");
            auto details = literal(3);
            put(details.get(), "round_id", truthy(start) ? method(start.get(), "get", p::text("round_id").get()) : p::retain(Py_None));
            put(details.get(), "event_id", get(event, "event_id"));
            put(details.get(), "event_kind", get(event, "kind"));
            method(battle, "_ignore_live_payload", p::text("events").get(), error, details.get());
        };
        auto pop = [&](Py_ssize_t i) { return method(journal.get(), "pop", integer(i).get()); };
        while (index < PySequence_Size(journal.get()) && attempts > 0) {
            --attempts;
            auto event = p::item(journal.get(), index);
            if (textIn(get(event.get(), "kind").get(), {"authority"}) && PySet_Size(blocked.get()) > 0) break;
            auto dependencies = p::Ref(PySet_New(nullptr));
            auto projectile = get(event.get(), "projectile_id");
            if (projectile.get() != Py_None) {
                p::Ref dep(PyTuple_Pack(2, p::text("projectile").get(), str(projectile.get()).get()));
                PySet_Add(dependencies.get(), dep.get());
            }
            if (member(combat.get(), get(event.get(), "kind").get()) && !truthy(get(event.get(), "cosmetic_only", Py_False))) {
                p::Ref dep(PyTuple_Pack(2, p::text("health").get(), eventEntityKey(event.get(), false).get()));
                PySet_Add(dependencies.get(), dep.get());
            }
            p::Ref overlap(PyNumber_And(dependencies.get(), blocked.get()));
            if (PySet_Size(overlap.get()) > 0) {
                method(blocked.get(), "update", dependencies.get());
                ++index;
                continue;
            }
            bool ready = false;
            try {
                ready = truthy(method(battle, "_event_is_ready", event.get()));
            } catch (const std::exception& e) {
                auto error = caught(e);
                ignore(error.get(), event.get());
                method(applied.get(), "add", str(p::Ref(PyObject_GetItem(event.get(), p::text("event_id").get())).get()).get());
                pop(index);
                continue;
            }
            if (!ready) {
                if (truthy(method(event.get(), "pop", p::text("_presentation_wait").get(), Py_False))) {
                    method(blocked.get(), "update", dependencies.get());
                    ++index;
                    continue;
                }
                Py_RETURN_FALSE;
            }
            bool done = true;
            try {
                done = truthy(method(battle, "_apply_ordered_event", event.get()));
            } catch (const std::exception& e) {
                auto error = caught(e);
                ignore(error.get(), event.get());
                done = true;
            }
            if (!done) {
                method(journal.get(), "append", pop(index).get());
                continue;
            }
            method(applied.get(), "add", str(p::Ref(PyObject_GetItem(event.get(), p::text("event_id").get())).get()).get());
            pop(index);
        }
        return p::boolean(PyObject_IsTrue(journal.get()) <= 0).release();
    } catch (const std::exception& e) { return fail(e); }
}


// BattleRuntime._apply_combat_event(event, update_state=True)
PyObject* applyCombatEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event, *update_raw = Py_True, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &event, &update_raw, &g)) return nullptr;
    try {
        auto contract = method(battle, "_validate_combat_event_contract", event);
        if (p::size(contract.get()) != 2) raise("too many values to unpack");
        auto source = p::item(contract.get(), 0), attack_reason = p::item(contract.get(), 1);
        auto target_key = eventEntityKey(event, false);
        if (target_key.get() == Py_None) raise("ordered combat event has no target");
        auto record = recordsGet(battle, target_key.get());
        if (record.get() == Py_None) raise("ordered combat event target is unavailable: " + keyText(target_key.get()));
        PyObject* r = record.get();
        auto latest_raw = get(r, "state");
        auto latest = truthy(latest_raw) ? p::retain(latest_raw.get()) : p::dict();
        const bool cosmetic = truthy(get(event, "cosmetic_only", Py_False));
        auto state = cosmetic ? p::retain(latest.get()) : method(battle, "_combat_event_state", event, latest.get(), target_key.get());
        const bool update_state = p::flag(update_raw);
        if (update_state && !cosmetic) put(r, "state", p::retain(state.get()));
        auto attacker = get(event, "attacker_bot");
        const char* attacker_kind = "bot";
        if (attacker.get() == Py_None) { replace(attacker, get(event, "attacker")); attacker_kind = "player"; }
        auto attacker_record = recordsGet(battle, key(attacker_kind, attacker.get()).get());
        auto attacker_text = [&]() { return std::string(attacker_kind) + ":" + keyText(attacker.get()); };
        if (attacker.get() != Py_None && attacker_record.get() == Py_None &&
            !truthy(method(battle, "_missing_projectile_attacker_allowed", event)))
            raise("ordered combat event attacker is unavailable: " + attacker_text());
        auto attacker_id = attacker_record.get() != Py_None ? get(attacker_record.get(), "engine_id") : integer(0);
        auto entity = method(battle, "_server_entity", p::Ref(PyObject_GetItem(r, p::text("engine_id").get())).get());
        if (entity.get() == Py_None) raise("ordered combat event target has no native entity: " + keyText(target_key.get()));
        if (attacker_record.get() != Py_None &&
            method(battle, "_server_entity", p::Ref(PyObject_GetItem(attacker_record.get(), p::text("engine_id").get())).get()).get() == Py_None)
            raise("ordered combat event attacker has no native entity: " + attacker_text());
        dispatch(battle, "_present_damage_sticker", "present_damage_sticker", {event, r});
        const bool blind = attacker_record.get() != Py_None && truthy(dispatch(battle, "_is_blind_local_attack", "blind_local_attack", {r, attacker_record.get()}));
        const bool dead_target = number(method(state.get(), "get", p::text("health").get(), p::number(0.).get()).get()) <= 0. ||
                                 !truthy(method(state.get(), "get", p::text("alive").get(), Py_True));
        auto suppress_postmortem = method(battle, "_should_suppress_postmortem_killer", r, state.get(), attacker_id.get());
        const bool local = truthy(get(r, "local"));
        if (attacker.get() != Py_None && !local && !cosmetic)
            setAttr(entity.get(), "last_killer_id", integer(strictInt(truthy(attacker_id) ? attacker_id.get() : integer(0).get())).get());
        if (local && attacker.get() != Py_None && !cosmetic) {
            p::Ref last(PyTuple_Pack(2, p::text(attacker_kind).get(), integer(strictInt(attacker.get())).get()));
            setAttr(battle, "_local_last_attacker", last.get());
        }
        if (textIn(source.get(), {"player_left"}) && attacker_record.get() != Py_None) raise("player_left event has an attacker");
        if (attacker_record.get() != Py_None) {
            dispatch(battle, "_present_combat_hit", "present_combat_hit", {event, r, attacker_record.get(), attacker_id.get()}, {g});
            dispatch(battle, "_present_combat_feedback", "present_combat_feedback", {event, r, attacker_record.get(), attack_reason.get()});
        } else if (textIn(source.get(), {"environment", "fire"})) {
            method(battle, "_present_environment_feedback", event, r, attack_reason.get());
        }
        if (cosmetic) {
            std::string line = "[Offline LAN 0.9.22] COSMETIC HIT PRESENT projectile=" + keyText(get(event, "projectile_id").get()) +
                " target=" + keyText(target_key.get()) + " current_health=" + keyText(method(latest.get(), "get", p::text("health").get()).get()) +
                " event_health=" + keyText(get(event, "health").get()) + "\n";
            method(attr(module("sys").get(), "stdout").get(), "write", p::text(line).get());
            Py_RETURN_TRUE;
        }
        auto critical = get(event, "critical");
        if (PyDict_Check(critical.get())) {
            auto canonical = method(battle, "_critical_state", critical.get());
            const bool apply = truthy(method(battle, "_reconcile_critical_authority", r, event, canonical.get()));
            if (entity.get() != Py_None && apply && !equal(canonical.get(), get(r, "critical_state").get())) {
                auto gun_edge = p::retain(Py_None);
                auto gun = attr(battle, "_gun_state");
                if (local && gun.get() != Py_None) replace(gun_edge, method(battle, "_advance_local_gun_edge", gun.get()));
                p::Ref presentation(PyDict_Copy(critical.get()));
                auto keywords = p::dict();
                put(keywords.get(), "devices", p::Ref(PyObject_GetItem(canonical.get(), p::text("devices").get())));
                put(keywords.get(), "destroyed", p::Ref(PyObject_GetItem(canonical.get(), p::text("destroyed").get())));
                if (PyDict_Merge(presentation.get(), keywords.get(), 1) < 0) throw p::Error("critical presentation merge failed");
                if (local && truthy(attr(battle, "_local_critical_owned")) &&
                    equal(integer(strictInt(get(r, "critical_base_revision", integer(-1).get()).get())).get(), attr(battle, "_local_critical_base_revision").get())) {
                    auto filtered = p::list();
                    auto items = method(critical.get(), "get", p::text("events").get(), p::Ref(PyTuple_New(0)).get());
                    p::Ref iterator(PyObject_GetIter(items.get()));
                    while (PyObject* raw = PyIter_Next(iterator.get())) {
                        p::Ref item(raw);
                        if (!textIn(method(item.get(), "get", p::text("name").get()).get(), {"leftTrackHealth", "rightTrackHealth"}))
                            p::append(filtered.get(), item);
                    }
                    if (PyErr_Occurred()) throw p::Error("critical events iteration failed");
                    put(presentation.get(), "events", filtered);
                }
                auto events = method(global(g, "critical_damage").get(), "apply_payload", entity.get(), presentation.get());
                if (gun_edge.get() != Py_None) method(battle, "_advance_local_gun_to", entity.get(), p::item(gun_edge.get(), 1).get());
                if (PyObject_SetItem(state.get(), p::text("critical").get(), canonical.get()) < 0) throw p::Error("state critical write failed");
                put(r, "critical_state", p::retain(canonical.get()));
                p::Ref iterator(PyObject_GetIter(events.get()));
                while (PyObject* raw = PyIter_Next(iterator.get())) {
                    p::Ref item(raw);
                    if (textIn(method(item.get(), "get", p::text("kind").get()).get(), {"ammo_rack"}) &&
                        textIn(method(item.get(), "get", p::text("state").get()).get(), {"destroyed"}))
                        put(r, "ammo_death_cause", method(item.get(), "get", p::text("cause").get()));
                }
                if (PyErr_Occurred()) throw p::Error("critical events iteration failed");
                method(battle, "_present_critical", r, events.get(), attacker_id.get());
            }
        }
        if (PyDict_GetItemString(event, "display_health")) {
            auto health = p::Ref(PyObject_GetItem(state.get(), p::text("health").get()));
            put(state.get(), "display_health", integer(std::max<std::int64_t>(0, strictInt(get(event, "display_health", health.get()).get()))));
        }
        const std::int64_t death_reason = strictInt(p::Ref(PyObject_GetItem(event, p::text("death_reason").get())).get());
        auto damage = get(event, "damage", integer(0).get());
        const bool force = std::max<std::int64_t>(0, strictInt(truthy(damage) ? damage.get() : integer(0).get())) > 0 ||
                           truthy(get(event, "dead", Py_False));
        callKw(battle, "_apply_health", {r, state.get(), attacker_id.get(), integer(death_reason).get()},
               {{"force_cause", force ? Py_True : Py_False},
                {"attack_reason_id", attack_reason.get() == Py_None ? integer(0).get() : attack_reason.get()},
                {"suppress_combat_presentation", (blind && !dead_target) ? Py_True : Py_False},
                {"suppress_postmortem_killer", suppress_postmortem.get()}});
        if (!update_state) put(r, "state", p::retain(latest.get()));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// '%s:%s' % (first, second)
p::Ref formatKey(PyObject* first, PyObject* second) {
    p::Ref values(PyTuple_Pack(2, first, second));
    return p::Ref(PyString_Format(p::text("%s:%s").get(), values.get()));
}
// BattleRuntime._feedback_entity_key (staticmethod)
p::Ref feedbackEntityKey(PyObject* event, const char* role, const char* label) {
    auto kind = method(event, "get", p::text(std::string(role) + "_kind").get());
    auto actor = method(event, "get", p::text(std::string(role) + "_id").get());
    if (!textIn(kind.get(), {"player", "bot"}) || actor.get() == Py_None)
        raise(std::string(label) + " event has an invalid " + role + " identity");
    return formatKey(kind.get(), actor.get());
}
// BattleRuntime._combat_record_team (staticmethod)
std::int64_t combatRecordTeam(PyObject* record) {
    auto state = method(record, "get", p::text("state").get());
    if (!truthy(state)) replace(state, p::dict());
    const int has = PySequence_Contains(state.get(), p::text("team").get());
    if (has < 0) throw p::Error("Native team lookup failed");
    if (!has) raise("combat feedback record has no team");
    p::Ref value(PyObject_GetItem(state.get(), p::text("team").get()));
    std::int64_t team;
    if (!pyInt(value.get(), team)) raise("combat feedback record has invalid team");
    if (team <= 0) raise("combat feedback record has invalid team");
    return team;
}
// BattleRuntime._combat_target_is_spotted (staticmethod)
bool combatTargetIsSpotted(PyObject* record) {
    auto fallback = method(record, "get", p::text("spot_visible").get(), Py_True);
    return truthy(method(record, "get", p::text("spot_marker_visible").get(), fallback.get()));
}
// self._run_optional_feature(name, fn, args)
p::Ref optionalFeature(PyObject* battle, const char* name, PyObject* fn, PyObject* call_args) {
    auto label = p::text(name);
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, "_run_optional_feature"))
        return callKw(battle, "_run_optional_feature", {label.get(), fn, call_args}, {});
    return dispatch(battle, "_run_optional_feature", "run_optional_feature", {}, {},
                    {label.get(), fn, call_args, Py_None, Py_True, Py_None});
}
// The stock BATTLE_EVENT_TYPE group, or the pinned RuntimeError.
p::Ref battleEventTypes(PyObject* battle, const char* missing) {
    auto runtime = attr(battle, "_runtime");
    auto common = optionalAttr(runtime.get(), "battle_feedback_common");
    auto types = optionalAttr(common.get(), "BATTLE_EVENT_TYPE");
    if (types.get() == Py_None) raise(missing);
    return types;
}
p::Ref battleEventsCallback(PyObject* battle) {
    p::Ref callback = p::retain(Py_None);
    if (!callableAttr(attr(battle, "_avatar").get(), "onBattleEvents", callback))
        raise("#1513 battle-event feedback boundary is unavailable");
    return callback;
}
// int(value) with Python's int/long result.
p::Ref toInt(PyObject* value) { return p::Ref(PyNumber_Int(value)); }
p::Ref feedbackRow(PyObject* type, PyObject* target, PyObject* details) {
    auto row = literal(4);
    put(row.get(), "eventType", p::retain(type));
    put(row.get(), "targetID", p::retain(target));
    put(row.get(), "count", integer(1));
    put(row.get(), "details", p::retain(details));
    return row;
}

PyObject* attackReason(PyObject*, PyObject* args) {
    PyObject *battle, *member, *fallback;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &member, &fallback)) return nullptr;
    try {
        auto runtime = attr(battle, "_runtime");
        auto constants = attr(runtime.get(), "constants");
        auto group = optionalAttr(constants.get(), "ATTACK_REASON");
        PyObject* raw = PyObject_GetAttrString(constants.get(), "ATTACK_REASON_INDICES");
        if (!raw) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) return nullptr;
            PyErr_Clear(); raw = PyDict_New();
        }
        p::Ref indices(raw);
        PyObject* named = PyObject_GetAttr(group.get(), member);
        if (!named) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) return nullptr;
            PyErr_Clear(); named = p::retain(Py_None).release();
        }
        p::Ref name(named);
        PyObject* value = PyObject_GetItem(indices.get(), name.get());
        PyObject* index = value ? PyNumber_Int(value) : nullptr;
        Py_XDECREF(value);
        if (index) return index;
        if (!PyErr_ExceptionMatches(PyExc_KeyError) && !PyErr_ExceptionMatches(PyExc_TypeError) &&
            !PyErr_ExceptionMatches(PyExc_ValueError)) return nullptr;
        PyErr_Clear();
        return PyNumber_Int(fallback);
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* blindLocalAttack(PyObject*, PyObject* args) {
    PyObject *battle, *target, *attacker;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &target, &attacker)) return nullptr;
    try {
        if (!truthy(method(attacker, "get", p::text("local").get())) || truthy(method(target, "get", p::text("local").get())))
            Py_RETURN_FALSE;
        const auto target_team = combatRecordTeam(target);
        if (target_team == combatRecordTeam(attacker)) Py_RETURN_FALSE;
        return p::boolean(!combatTargetIsSpotted(target)).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* vehicleStatisticsEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        auto kind = method(event, "get", p::text("actor_kind").get());
        auto raw_id = method(event, "get", p::text("actor_id").get());
        PyObject* converted = PyNumber_Int(raw_id.get());
        if (!converted) {
            if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError)) return nullptr;
            PyErr_Clear(); Py_RETURN_FALSE;
        }
        p::Ref actor(converted);
        auto record = recordsGet(battle, formatKey(kind.get(), actor.get()).get());
        if (record.get() == Py_None) Py_RETURN_FALSE;
        auto previous = method(record.get(), "get", p::text("state").get());
        if (!truthy(previous)) replace(previous, p::dict());
        p::Ref state(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), previous.get(), nullptr));
        auto frags_default = method(state.get(), "get", p::text("frags").get(), integer(0).get());
        auto frags = method(event, "get", p::text("frags").get(), frags_default.get());
        put(state.get(), "frags", toInt(frags.get()));
        auto killer_default = method(state.get(), "get", p::text("team_killer").get(), Py_False);
        auto killer = method(event, "get", p::text("team_killer").get(), killer_default.get());
        put(state.get(), "team_killer", p::boolean(truthy(killer)));
        if (PyObject_SetItem(record.get(), p::text("state").get(), state.get()) < 0) return nullptr;
        return dispatch(battle, "_apply_vehicle_statistics", "vehicle_statistics", {record.get(), state.get()}).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* presentDirectSpot(PyObject*, PyObject* args) {
    PyObject *battle, *record;
    if (!PyArg_ParseTuple(args, "OO", &battle, &record)) return nullptr;
    try {
        if (truthy(method(record, "get", p::text("spot_feedback_sent").get()))) Py_RETURN_FALSE;
        auto types = battleEventTypes(battle, "#1513 spotting feedback constants are unavailable");
        p::Ref pack = p::retain(Py_None);
        if (!callableAttr(types.get(), "packVisibility", pack)) raise("#1513 visibility feedback packer is unavailable");
        auto callback = battleEventsCallback(battle);
        p::Ref engine(PyObject_GetItem(record, p::text("engine_id").get()));
        auto target = toInt(engine.get());
        auto spotted = toInt(attr(types.get(), "SPOTTED").get());
        auto first = feedbackRow(spotted.get(), target.get(), integer(0).get());
        auto visibility = toInt(attr(types.get(), "TARGET_VISIBILITY").get());
        auto packed = toInt(p::Ref(PyObject_CallFunctionObjArgs(pack.get(), Py_True, Py_True, nullptr)).get());
        auto second = feedbackRow(visibility.get(), target.get(), packed.get());
        p::Ref rows(PyList_New(2));
        PyList_SET_ITEM(rows.get(), 0, first.release());
        PyList_SET_ITEM(rows.get(), 1, second.release());
        p::Ref(PyObject_CallFunctionObjArgs(callback.get(), rows.get(), nullptr));
        if (PyObject_SetItem(record, p::text("spot_feedback_sent").get(), Py_True) < 0) return nullptr;
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* detectionEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        auto observer = recordsGet(battle, feedbackEntityKey(event, "observer", "detection").get());
        if (observer.get() == Py_None || !truthy(method(observer.get(), "get", p::text("local").get()))) Py_RETURN_FALSE;
        auto target = recordsGet(battle, feedbackEntityKey(event, "target", "detection").get());
        if (target.get() == Py_None) raise("detection event has no known target");
        p::Ref call_args(PyTuple_Pack(1, target.get()));
        optionalFeature(battle, "spotting feedback", attr(battle, "_present_direct_spot").get(), call_args.get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* assistEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event;
    if (!PyArg_ParseTuple(args, "OO", &battle, &event)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        auto assister = recordsGet(battle, feedbackEntityKey(event, "assister", "assist").get());
        if (assister.get() == Py_None || !truthy(method(assister.get(), "get", p::text("local").get()))) Py_RETURN_FALSE;
        auto target = recordsGet(battle, feedbackEntityKey(event, "target", "assist").get());
        if (target.get() == Py_None) raise("assist event has no known target");
        auto category = method(event, "get", p::text("category").get());
        auto name = method(attr(battle, "_ASSIST_EVENT_TYPES").get(), "get", category.get());
        if (name.get() == Py_None) raise("assist category is unsupported: " + p::string(str(category.get()).get()));
        auto types = battleEventTypes(battle, "#1513 battle feedback constants are unavailable");
        auto callback = battleEventsCallback(battle);
        auto raw_damage = method(event, "get", p::text("damage").get(), integer(0).get());
        auto damage = toInt(truthy(raw_damage) ? raw_damage.get() : integer(0).get());
        if (PyObject_RichCompareBool(integer(0).get(), damage.get(), Py_GT) == 1) replace(damage, integer(0));
        auto row = literal(4);
        put(row.get(), "eventType", toInt(p::Ref(PyObject_GetAttr(types.get(), name.get())).get()));
        p::Ref engine(PyObject_GetItem(target.get(), p::text("engine_id").get()));
        put(row.get(), "targetID", toInt(engine.get()));
        put(row.get(), "count", integer(1));
        auto pack = attr(types.get(), "packDamage");
        auto reason = dispatch(battle, "_attack_reason", "attack_reason", {p::text("SHOT").get(), integer(0).get()});
        put(row.get(), "details", toInt(p::Ref(PyObject_CallFunctionObjArgs(pack.get(), damage.get(), reason.get(), nullptr)).get()));
        p::Ref rows(PyList_New(1));
        PyList_SET_ITEM(rows.get(), 0, row.release());
        p::Ref(PyObject_CallFunctionObjArgs(callback.get(), rows.get(), nullptr));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* environmentCriticalFeedback(PyObject*, PyObject* args) {
    PyObject *battle, *event, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &g)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        auto count = method(event, "get", p::text("critical_count").get());
        auto reasons = literal(3);
        put(reasons.get(), "world_collision", integer(3));
        put(reasons.get(), "drowning", integer(5));
        put(reasons.get(), "fire", integer(1));
        auto reason = method(reasons.get(), "get", method(event, "get", p::text("cause").get()).get());
        const int typed = PyObject_IsInstance(count.get(), global(g, "_INTEGER_TYPES").get());
        if (typed < 0) return nullptr;
        if (PyBool_Check(count.get()) || !typed ||
            PyObject_RichCompareBool(integer(1).get(), count.get(), Py_LE) != 1 ||
            PyObject_RichCompareBool(count.get(), integer(65535).get(), Py_LE) != 1 || reason.get() == Py_None)
            Py_RETURN_FALSE;
        auto target = recordsGet(battle, eventEntityKey(event, false).get());
        auto attacker = recordsGet(battle, eventEntityKey(event, true).get());
        if (target.get() == Py_None || attacker.get() == Py_None) Py_RETURN_FALSE;
        auto result = dispatch(battle, "_present_combat_feedback", "present_combat_feedback", {event, target.get(), attacker.get(), reason.get()});
        if (truthy(method(attacker.get(), "get", p::text("local").get())) || truthy(method(target.get(), "get", p::text("local").get()))) {
            auto writer = attr(module("gui.mods.offline_lan_0922.native_input_diagnostics").get(), "write_json_record");
            auto values = p::dict();
            put(values.get(), "event_id", method(event, "get", p::text("event_id").get()));
            put(values.get(), "cause", method(event, "get", p::text("cause").get()));
            put(values.get(), "mask", method(event, "get", p::text("critical_mask").get()));
            put(values.get(), "count", p::retain(count.get()));
            put(values.get(), "reason", p::retain(reason.get()));
            put(values.get(), "target", eventEntityKey(event, false));
            put(values.get(), "presented", p::retain(result.get()));
            p::Ref copied(PyObject_Call(reinterpret_cast<PyObject*>(&PyDict_Type), p::Ref(PyTuple_New(0)).get(), values.get()));
            p::Ref(PyObject_CallFunctionObjArgs(writer.get(), p::text("ENV CRITICAL FEEDBACK").get(), copied.get(), nullptr));
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* destructibleEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event, *drive;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &drive)) return nullptr;
    try {
        if (truthy(method(drive, "enabled", battle))) return method(drive, "ingress", battle, event).release();
        return method(battle, "_apply_destructible_event_visible", event).release();
    } catch (const std::exception& e) { return fail(e); }
}

// `a, b, ... = value` with Python 2's iteration and error messages.
std::vector<p::Ref> unpack(PyObject* value, std::size_t count) {
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
p::Ref positive(p::Ref value) {
    const int greater = PyObject_RichCompareBool(value.get(), integer(0).get(), Py_GT);
    if (greater < 0) throw p::Error("Native comparison failed");
    return greater ? std::move(value) : integer(0);
}
bool greaterThanZero(PyObject* value) {
    const int greater = PyObject_RichCompareBool(value, integer(0).get(), Py_GT);
    if (greater < 0) throw p::Error("Native comparison failed");
    return greater != 0;
}
// int(event.get(name, 0) or 0), clamped below at zero.
p::Ref nonNegative(PyObject* event, const char* name) {
    auto raw = method(event, "get", p::text(name).get(), integer(0).get());
    return positive(toInt(truthy(raw) ? raw.get() : integer(0).get()));
}
p::Ref eventRow(PyObject* types, const char* type, PyObject* target, p::Ref details) {
    auto row = literal(4);
    put(row.get(), "eventType", toInt(attr(types, type).get()));
    put(row.get(), "targetID", p::retain(target));
    put(row.get(), "count", integer(1));
    put(row.get(), "details", std::move(details));
    return row;
}
std::int64_t flag(PyObject* flags, const char* name) { return strictInt(attr(flags, name).get()); }

PyObject* presentCombatFeedback(PyObject*, PyObject* args) {
    PyObject *battle, *event, *target_record, *attacker_record, *reason_argument;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &event, &target_record, &attacker_record, &reason_argument)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        const bool attributed_terrain = equal(method(event, "get", p::text("source").get()).get(), p::text("environment").get()) &&
            method(event, "get", p::text("push_attributed").get()).get() == Py_True;
        // A confirmed shove's terrain outcome remains credited after the target falls out of spotting.
        if (truthy(dispatch(battle, "_is_blind_local_attack", "blind_local_attack", {target_record, attacker_record})) && !attributed_terrain)
            Py_RETURN_FALSE;
        auto types = battleEventTypes(battle, "#1513 battle feedback constants are unavailable");
        auto damage = nonNegative(event, "damage");
        auto reason = p::retain(reason_argument);
        if (reason.get() == Py_None) replace(reason, dispatch(battle, "_combat_attack_reason", "combat_attack_reason", {event}));
        auto shell = unpack(method(battle, "_feedback_shell_fields", event, attacker_record).get(), 2);
        PyObject *shell_type = shell[0].get(), *shell_is_gold = shell[1].get();
        auto critical = method(event, "get", p::text("critical").get());
        auto critical_items = [&]() {
            auto owner = truthy(critical) ? p::retain(critical.get()) : p::dict();
            auto items = method(owner.get(), "get", p::text("events").get());
            return truthy(items) ? std::move(items) : p::Ref(PyTuple_New(0));
        };
        // CRIT counts newly damaged devices/crew, not fire, ammo-rack death effects or repairs.
        auto critical_events = p::list();
        {
            p::Ref iterator(PyObject_GetIter(critical_items().get()));
            while (PyObject* raw = PyIter_Next(iterator.get())) {
                p::Ref item(raw);
                auto kind = method(item.get(), "get", p::text("kind").get());
                auto state = method(item.get(), "get", p::text("state").get());
                if (equal(kind.get(), p::text("crew").get()) && equal(state.get(), p::text("destroyed").get())) {
                    p::append(critical_events.get(), item);
                } else if (equal(kind.get(), p::text("device").get()) && textIn(state.get(), {"critical", "destroyed"})) {
                    auto old_state = method(item.get(), "get", p::text("old_state").get(), p::text("normal").get());
                    if (!equal(old_state.get(), state.get()) && !equal(old_state.get(), p::text("destroyed").get()))
                        p::append(critical_events.get(), item);
                }
            }
            if (PyErr_Occurred()) throw p::Error("critical events iteration failed");
        }
        auto critical_count = integer(PyList_GET_SIZE(critical_events.get()));
        if (method(event, "get", p::text("critical_feedback_separate").get()).get() == Py_True) replace(critical_count, integer(0));
        if (equal(method(event, "get", p::text("kind").get()).get(), p::text("environment_critical").get()))
            replace(critical_count, toInt(p::Ref(PyObject_GetItem(event, p::text("critical_count").get())).get()));
        const bool attacker_local = truthy(method(attacker_record, "get", p::text("local").get()));
        if (attacker_local)
            method(battle, "_assert_player_identity", p::Ref(PyObject_GetItem(attacker_record, p::text("engine_id").get())).get());
        const auto target_team = combatRecordTeam(target_record);
        const bool enemy = target_team != combatRecordTeam(attacker_record);
        auto runtime = attr(battle, "_runtime");
        auto avatar = attr(battle, "_avatar");
        auto engine_id = [](PyObject* record) { return toInt(p::Ref(PyObject_GetItem(record, p::text("engine_id").get())).get()); };
        if (attacker_local && enemy) {
            p::Ref assert_damage = p::retain(Py_None);
            if (!callableAttr(attr(runtime.get(), "compatibility").get(), "assert_vehicle_marker_damage_type", assert_damage))
                raise("#1513 vehicle-marker damage boundary is unavailable");
            p::Ref(PyObject_CallFunctionObjArgs(assert_damage.get(), avatar.get(), engine_id(attacker_record).get(), nullptr));
        }
        auto output = p::list();
        auto pack_damage = [&](PyObject* amount) {
            return toInt(method(types.get(), "packDamage", amount, reason.get(), Py_False, shell_type, shell_is_gold).get());
        };
        auto pack_crits = [&]() {
            return toInt(method(types.get(), "packCrits", critical_count.get(), reason.get(), shell_type, shell_is_gold).get());
        };
        if (attacker_local && enemy) {
            auto target_id = engine_id(target_record);
            if (greaterThanZero(damage.get())) {
                auto type = toInt(attr(types.get(), "DAMAGE").get());
                auto row = literal(4);
                put(row.get(), "eventType", std::move(type));
                put(row.get(), "targetID", p::retain(target_id.get()));
                put(row.get(), "count", integer(1));
                put(row.get(), "details", pack_damage(damage.get()));
                p::append(output.get(), row);
            }
            if (greaterThanZero(critical_count.get())) {
                auto type = toInt(attr(types.get(), "CRIT").get());
                auto row = literal(4);
                put(row.get(), "eventType", std::move(type));
                put(row.get(), "targetID", p::retain(target_id.get()));
                put(row.get(), "count", integer(1));
                put(row.get(), "details", pack_crits());
                p::append(output.get(), row);
            }
            if (truthy(method(event, "get", p::text("dead").get())))
                p::append(output.get(), eventRow(types.get(), "KILL", target_id.get(), integer(0)));
        }
        if (attacker_local) {
            auto target_id = engine_id(target_record);
            if (equal(combatEventSource(event).get(), p::text("shot").get()) &&
                textIn(method(event, "get", p::text("kind").get()).get(), {"hit", "bot_hit", "bot_human_hit", "bot_bot_hit"})) {
                auto constants = attr(runtime.get(), "constants");
                auto flags_type = optionalAttr(constants.get(), "VEHICLE_HIT_FLAGS");
                if (flags_type.get() == Py_None) raise("#1513 VEHICLE_HIT_FLAGS are unavailable");
                const auto shot_result = std::max<std::int64_t>(0, std::min<std::int64_t>(
                    strictInt(method(event, "get", p::text("shot_result").get(), integer(2).get()).get()), 2));
                bool direct_he = false;
                if (!truthy(method(event, "get", p::text("splash").get()))) {
                    p::Ref indices = attr(constants.get(), "SHELL_TYPES_INDICES");
                    p::Ref he(PyObject_GetItem(indices.get(), p::text("HIGH_EXPLOSIVE").get()));
                    direct_he = equal(shell_type, toInt(he.get()).get());
                }
                // Origin (direct / nearby) is independent of blast damage.
                const bool external_blast = truthy(method(event, "get", p::text("splash").get()));
                const bool blast_damage = external_blast || (direct_he && shot_result != 2);
                const bool has_damage = greaterThanZero(damage.get());
                auto flags = flag(flags_type.get(), external_blast ? "ATTACK_IS_EXTERNAL_EXPLOSION" : "ATTACK_IS_DIRECT_PROJECTILE");
                if (external_blast) {
                    if (has_damage) flags |= flag(flags_type.get(), "MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_EXPLOSION");
                } else if (direct_he) {
                    if (!has_damage) flags |= flag(flags_type.get(), "MATERIAL_WITH_POSITIVE_DF_NOT_PIERCED_BY_PROJECTILE");
                    else if (shot_result == 2) flags |= flag(flags_type.get(), "MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE");
                    else flags |= flag(flags_type.get(), "MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_EXPLOSION");
                } else if (shot_result == 2) flags |= flag(flags_type.get(), "MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE");
                else if (shot_result == 1) flags |= flag(flags_type.get(), "MATERIAL_WITH_POSITIVE_DF_NOT_PIERCED_BY_PROJECTILE");
                else flags |= flag(flags_type.get(), "RICOCHET");
                const char* critical_cause = blast_damage ? "explosion" : "shot";
                const auto pierced_flag = flag(flags_type.get(), blast_damage ? "DEVICE_PIERCED_BY_EXPLOSION" : "DEVICE_PIERCED_BY_PROJECTILE");
                const auto device_flag = flag(flags_type.get(), blast_damage ? "DEVICE_DAMAGED_BY_EXPLOSION" : "DEVICE_DAMAGED_BY_PROJECTILE");
                const auto chassis_flag = flag(flags_type.get(), blast_damage ? "CHASSIS_DAMAGED_BY_EXPLOSION" : "CHASSIS_DAMAGED_BY_PROJECTILE");
                const auto gun_flag = flag(flags_type.get(), blast_damage ? "GUN_DAMAGED_BY_EXPLOSION" : "GUN_DAMAGED_BY_PROJECTILE");
                {
                    p::Ref iterator(PyObject_GetIter(critical_items().get()));
                    while (PyObject* raw = PyIter_Next(iterator.get())) {
                        p::Ref item(raw);
                        if (!equal(method(item.get(), "get", p::text("cause").get(), p::text("shot").get()).get(), p::text(critical_cause).get()))
                            continue;
                        auto kind = method(item.get(), "get", p::text("kind").get());
                        auto state = method(item.get(), "get", p::text("state").get());
                        if (equal(kind.get(), p::text("fire").get()) && truthy(state)) {
                            flags |= flag(flags_type.get(), "FIRE_STARTED");
                        } else if (member(critical_events.get(), item.get())) {
                            // #1513's voice selector tests DEVICE_PIERCED for module-only hits and ricochets.
                            flags |= pierced_flag;
                            if (!equal(kind.get(), p::text("device").get())) continue;
                            const auto name = p::string(str(method(item.get(), "get", p::text("name").get(), p::text("").get()).get()).get());
                            flags |= device_flag;
                            if (name == "leftTrackHealth" || name == "rightTrackHealth") flags |= chassis_flag;
                            else if (name == "gunHealth") flags |= gun_flag;
                        }
                    }
                    if (PyErr_Occurred()) throw p::Error("critical events iteration failed");
                }
                // A module hit must not replace the requested plain HE voice.
                if (direct_he)
                    flags &= ~(flag(flags_type.get(), "GUN_DAMAGED_BY_PROJECTILE") | flag(flags_type.get(), "GUN_DAMAGED_BY_EXPLOSION") |
                               flag(flags_type.get(), "CHASSIS_DAMAGED_BY_PROJECTILE") | flag(flags_type.get(), "CHASSIS_DAMAGED_BY_EXPLOSION"));
                if (truthy(method(event, "get", p::text("dead").get()))) flags |= flag(flags_type.get(), "VEHICLE_KILLED");
                p::Ref callback = p::retain(Py_None);
                if (!callableAttr(avatar.get(), "showShotResults", callback)) raise("#1513 shot-result feedback boundary is unavailable");
                bool internal_destroyed = false;
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(critical_events.get()) && !internal_destroyed; ++i) {
                    PyObject* item = PyList_GET_ITEM(critical_events.get(), i);
                    if (!equal(method(item, "get", p::text("state").get()).get(), p::text("destroyed").get())) continue;
                    const bool internal = equal(method(item, "get", p::text("kind").get()).get(), p::text("crew").get()) ||
                        (equal(method(item, "get", p::text("kind").get()).get(), p::text("device").get()) &&
                         !textIn(method(item, "get", p::text("name").get()).get(), {"leftTrackHealth", "rightTrackHealth", "gunHealth"}));
                    internal_destroyed = internal &&
                        equal(method(item, "get", p::text("cause").get(), p::text("shot").get()).get(), p::text("shot").get());
                }
                p::Ref shifted(PyNumber_Lshift(integer(flags).get(), integer(32).get()));
                p::Ref packed(PyNumber_Or(shifted.get(), target_id.get()));
                p::Ref results(PyList_New(1));
                PyList_SET_ITEM(results.get(), 0, packed.release());
                if (enemy && !greaterThanZero(damage.get()) && shot_result == 2 && internal_destroyed && !direct_he &&
                    !external_blast && !truthy(method(event, "get", p::text("dead").get()))) {
                    auto voice = module("gui.mods.offline_lan_0922.critical_voice");
                    method(voice.get(), "present", avatar.get(), callback.get(), results.get());
                } else {
                    p::Ref(PyObject_CallFunctionObjArgs(callback.get(), results.get(), nullptr));
                }
            }
        }
        if (truthy(method(target_record, "get", p::text("local").get())) &&
            (enemy || method(event, "get", p::text("push_attributed").get()).get() == Py_True)) {
            auto attacker_id = engine_id(attacker_record);
            auto blocked = nonNegative(event, "blocked_damage");
            if (greaterThanZero(blocked.get())) {
                auto type = toInt(attr(types.get(), "TANKING").get());
                auto row = literal(4);
                put(row.get(), "eventType", std::move(type));
                put(row.get(), "targetID", p::retain(attacker_id.get()));
                put(row.get(), "count", integer(1));
                put(row.get(), "details", pack_damage(blocked.get()));
                p::append(output.get(), row);
            }
            if (greaterThanZero(damage.get())) {
                auto type = toInt(attr(types.get(), "RECEIVED_DAMAGE").get());
                auto row = literal(4);
                put(row.get(), "eventType", std::move(type));
                put(row.get(), "targetID", p::retain(attacker_id.get()));
                put(row.get(), "count", integer(1));
                put(row.get(), "details", pack_damage(damage.get()));
                p::append(output.get(), row);
            }
            if (greaterThanZero(critical_count.get())) {
                auto type = toInt(attr(types.get(), "RECEIVED_CRIT").get());
                auto row = literal(4);
                put(row.get(), "eventType", std::move(type));
                put(row.get(), "targetID", p::retain(attacker_id.get()));
                put(row.get(), "count", integer(1));
                put(row.get(), "details", pack_crits());
                p::append(output.get(), row);
            }
        }
        const bool any = PyList_GET_SIZE(output.get()) != 0;
        if (any) p::Ref(PyObject_CallFunctionObjArgs(battleEventsCallback(battle).get(), output.get(), nullptr));
        if (attacker_local && greaterThanZero(damage.get())) {
            auto writer = attr(module("gui.mods.offline_lan_0922.native_input_diagnostics").get(), "write_json_record");
            auto values = p::dict();
            put(values.get(), "event_id", method(event, "get", p::text("event_id").get()));
            put(values.get(), "source", method(event, "get", p::text("source").get()));
            put(values.get(), "reason", p::retain(reason.get()));
            put(values.get(), "damage", p::retain(damage.get()));
            put(values.get(), "enemy", p::boolean(enemy));
            put(values.get(), "target", method(target_record, "get", p::text("network_id").get()));
            put(values.get(), "spotted", p::boolean(combatTargetIsSpotted(target_record)));
            auto native_types = p::list();
            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(output.get()); ++i)
                p::append(native_types.get(), p::Ref(PyObject_GetItem(PyList_GET_ITEM(output.get(), i), p::text("eventType").get())));
            put(values.get(), "native_types", std::move(native_types));
            p::Ref copied(PyObject_Call(reinterpret_cast<PyObject*>(&PyDict_Type), p::Ref(PyTuple_New(0)).get(), values.get()));
            p::Ref(PyObject_CallFunctionObjArgs(writer.get(), p::text("DAMAGE FEEDBACK").get(), copied.get(), nullptr));
        }
        return p::boolean(any).release();
    } catch (const std::exception& e) { return fail(e); }
}

p::Ref number3(PyObject* g, PyObject* event) {
    auto convert = global(g, "_number");
    p::Ref out(PyTuple_New(3));
    const char* names[] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i)
        PyTuple_SET_ITEM(out.get(), i, p::Ref(PyObject_CallFunctionObjArgs(convert.get(), method(event, "get", p::text(names[i]).get()).get(), nullptr)).release());
    return out;
}
p::Ref item(PyObject* sequence, Py_ssize_t index) { return p::Ref(PySequence_GetItem(sequence, index)); }
p::Ref minus(PyObject* a, PyObject* b) { return p::Ref(PyNumber_Subtract(a, b)); }

PyObject* presentCombatHit(PyObject*, PyObject* args) {
    PyObject *battle, *event, *target_record, *attacker_record, *attacker_id, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &event, &target_record, &attacker_record, &attacker_id, &g)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        if (!equal(combatEventSource(event).get(), p::text("shot").get()) ||
            !textIn(method(event, "get", p::text("kind").get()).get(), {"hit", "bot_hit", "bot_human_hit", "bot_bot_hit"}))
            Py_RETURN_FALSE;
        if (!truthy(method(event, "get", p::text("world_pose").get()))) raise("shot hit event has no world impact pose");
        auto shell_pair = unpack(method(battle, "_event_shell", attacker_record, event).get(), 2);
        PyObject *shot = shell_pair[0].get(), *shell = shell_pair[1].get();
        auto attacker_position = method(battle, "_record_position", attacker_record);
        auto target_position = method(battle, "_record_position", target_record);
        auto impact = number3(g, event);
        PyObject* origin = truthy(method(event, "get", p::text("splash").get(), Py_False)) ? impact.get() : attacker_position.get();
        p::Ref delta(PyTuple_New(3));
        for (int i = 0; i < 3; ++i)
            PyTuple_SET_ITEM(delta.get(), i, minus(item(target_position.get(), i).get(), item(origin, i).get()).release());
        auto direction = method(battle, "_vector", delta.get());
        const int degenerate = PyObject_RichCompareBool(attr(direction.get(), "length").get(), p::number(0.001).get(), Py_LE);
        if (degenerate < 0) throw p::Error("direction length comparison failed");
        // A legal HE blast can damage its own shooter; use a harmless presentation normal.
        if (degenerate) {
            p::Ref up(Py_BuildValue("(ddd)", 0., 1., 0.));
            replace(direction, method(battle, "_vector", up.get()));
        }
        method(direction.get(), "normalise");
        auto damage = nonNegative(event, "damage");
        const auto shot_result = std::max<std::int64_t>(0, std::min<std::int64_t>(
            strictInt(method(event, "get", p::text("shot_result").get(), integer(2).get()).get()), 2));
        bool same_vehicle = attacker_record == target_record;
        if (!same_vehicle) {
            auto identity = [](PyObject* record) {
                return p::Ref(PyTuple_Pack(2, method(record, "get", p::text("kind").get()).get(),
                                           method(record, "get", p::text("network_id").get()).get()));
            };
            same_vehicle = equal(identity(attacker_record).get(), identity(target_record).get());
        }
        auto rules = global(g, "combat_rules");
        auto avatar = attr(battle, "_avatar");
        if (truthy(method(target_record, "get", p::text("local").get())) && !same_vehicle) {
            // The hit indicator points from the player back toward the attacker (0.8.2 convention).
            auto east = p::Ref(PyNumber_Negative(minus(item(attacker_position.get(), 0).get(), item(target_position.get(), 0).get()).get()));
            auto north = p::Ref(PyNumber_Negative(minus(item(attacker_position.get(), 2).get(), item(target_position.get(), 2).get()).get()));
            auto yaw = method(module("math").get(), "atan2", east.get(), north.get());
            auto marker = unpack(method(battle, "_hit_indicator_marker", event, shot, damage.get()).get(), 2);
            auto show = attr(avatar.get(), "showOwnVehicleHitDirection");
            auto attacker_int = toInt(PyObject_IsTrue(attacker_id) > 0 ? attacker_id : integer(0).get());
            auto mask = method(battle, "_critical_hit_mask", method(event, "get", p::text("critical").get()).get());
            auto is_he = method(rules.get(), "is_he", shot);
            auto engine = toInt(p::Ref(PyObject_GetItem(target_record, p::text("engine_id").get())).get());
            p::Ref(PyObject_CallFunctionObjArgs(show.get(), yaw.get(), attacker_int.get(), marker[0].get(), mask.get(),
                                               marker[1].get(), is_he.get(), engine.get(), nullptr));
        }
        // An armour effect belongs to the visible world model, not to a radio-only marker.
        if (!truthy(method(target_record, "get", p::text("local").get())) &&
            !truthy(method(target_record, "get", p::text("spot_visible").get(), Py_True)))
            Py_RETURN_FALSE;
        if (!truthy(method(battle, "_projectile_cosmetic_allowed", event, attr(battle, "_projectile_visual_meta").get())))
            Py_RETURN_FALSE;
        auto effects_index = p::Ref(PyObject_CallFunctionObjArgs(global(g, "_field").get(), shell, p::text("effectsIndex").get(), Py_None, nullptr));
        if (effects_index.get() == Py_None) raise("combat shell effects index is unavailable");
        auto cache = attr(attr(attr(battle, "_runtime").get(), "vehicles").get(), "g_cache");
        p::Ref effects_descr(PyObject_GetItem(attr(cache.get(), "shotEffects").get(), effects_index.get()));
        // Retail rocks the hull before it spawns the armour effect.
        p::Ref impulse_args(PyTuple_Pack(4, event, target_record, effects_descr.get(), direction.get()));
        optionalFeature(battle, "vehicle hit impulse", attr(battle, "_present_hit_impulse").get(), impulse_args.get());
        auto damage_factor = method(battle, "_hit_damage_factor", event, target_record);
        if (truthy(method(event, "get", p::text("splash").get(), Py_False)))
            return method(battle, "_present_splash_hit", target_record, effects_descr.get(), effects_index.get(), impact.get(),
                          direction.get(), damage_factor.get()).release();
        const bool he = truthy(method(rules.get(), "is_he", shot));
        static const char* const by_result[] = {"armorRicochet", "armorResisted", "armorHit"};
        const char* group = he ? (greaterThanZero(damage.get()) ? "armorHit" : "armorResisted") : by_result[shot_result];
        auto effect_group = p::text(group);
        auto parts = unpack(p::Ref(PyObject_GetItem(effects_descr.get(), effect_group.get())).get(), 3);
        auto hit_position = method(battle, "_vector", impact.get());
        auto terrain = optionalAttr(avatar.get(), "terrainEffects");
        p::Ref add_effect = p::retain(Py_None);
        if (!callableAttr(terrain.get(), "addNew", add_effect)) raise("#1513 terrain hit-effects boundary is unavailable");
        if (!truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {p::text("projectile impact presentation").get()})))
            Py_RETURN_FALSE;
        callKw(battle, "_report_effect", {p::text("armour_hit").get(), effect_group.get(), effects_index.get(), number3(g, event).get(),
                                          direction.get()}, {{"event", event}});
        try {
            const bool local = truthy(method(target_record, "get", p::text("local").get()));
            auto offset = method(direction.get(), "scale", p::number(0.4).get());
            p::Ref start(PyNumber_Subtract(hit_position.get(), offset.get()));
            auto far_offset = method(direction.get(), "scale", p::number(0.4).get());
            p::Ref end(PyNumber_Add(hit_position.get(), far_offset.get()));
            auto identity = method(battle, "_impact_sound_identity", target_record, attacker_id, direction.get(), damage_factor.get());
            // f(..., k=v, **identity): the mapping is copied, then explicit keywords join it.
            p::Ref keywords(PyDict_New());
            if (PyDict_Update(keywords.get(), identity.get()) < 0) throw p::Error("sound identity is not a mapping");
            const std::pair<const char*, PyObject*> named[] = {{"dir", direction.get()}, {"start", start.get()}, {"end", end.get()},
                {"showShockWave", local ? Py_True : Py_False}, {"showFlashBang", local ? Py_True : Py_False}};
            for (const auto& entry : named) {
                if (PyDict_GetItemString(keywords.get(), entry.first)) {
                    PyErr_Format(PyExc_TypeError, "%.200s%s got multiple values for keyword argument '%.200s'",
                                 PyEval_GetFuncName(add_effect.get()), PyEval_GetFuncDesc(add_effect.get()), entry.first);
                    throw p::Error("duplicate keyword");
                }
                put(keywords.get(), entry.first, p::retain(entry.second));
            }
            p::Ref positional(PyTuple_Pack(4, hit_position.get(), parts[1].get(), parts[0].get(), Py_None));
            p::Ref(PyObject_Call(add_effect.get(), positional.get(), keywords.get()));
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            auto error = caught(e);
            method(battle, "_warn_optional_failure", p::text("projectile impact presentation").get(), error.get());
            Py_RETURN_FALSE;
        }
        if (he && greaterThanZero(damage.get()))
            method(battle, "_present_splash_hit", target_record, effects_descr.get(), effects_index.get(), impact.get(),
                   direction.get(), damage_factor.get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// sys.stdout.write(text) through the live stream object.
void writeStdout(PyObject* text) {
    PyObject* stream = PySys_GetObject(const_cast<char*>("stdout"));
    if (!stream) throw p::Error("sys.stdout is unavailable");
    method(stream, "write", text);
}
// self._run_optional_feature(name, fn, args=call_args)
void optionalFeatureArgs(PyObject* battle, const char* name, PyObject* fn, PyObject* call_args) {
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, "_run_optional_feature")) {
        callKw(battle, "_run_optional_feature", {p::text(name).get(), fn}, {{"args", call_args}});
        return;
    }
    optionalFeature(battle, name, fn, call_args);
}
// int(value) with `except (TypeError, ValueError[, OverflowError, IndexError])` falling back.
p::Ref intOr(PyObject* value, std::int64_t fallback) {
    PyObject* converted = PyNumber_Int(value);
    if (converted) return p::Ref(converted);
    if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError) &&
        !PyErr_ExceptionMatches(PyExc_OverflowError)) throw p::Error("int conversion failed");
    PyErr_Clear();
    return integer(fallback);
}
void boneAngles(PyObject* battle, PyObject* entity, PyObject* angles) {
    const char* pairs[][2] = {{"turretMatrix", "yaw"}, {"gunMatrix", "pitch"}};
    for (const auto& pair : pairs) {
        try {
            auto math = attr(attr(battle, "_runtime").get(), "math");
            auto matrix = method(math.get(), "Matrix", attr(attr(entity, "appearance").get(), pair[0]).get());
            put(angles, pair[0], p::Ref(PyNumber_Float(attr(matrix.get(), pair[1]).get())));
        } catch (const std::exception&) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            PyErr_Clear();
            put(angles, pair[0], p::retain(Py_None));
        }
    }
}
void botFireDiagnostic(PyObject* battle, PyObject* event, PyObject* record, PyObject* entity, PyObject* projectile_id,
                       PyObject* sync_record) {
    auto angles = p::dict();
    boneAngles(battle, entity, angles.get());
    try {
        auto start = attr(battle, "_start_message");
        auto row = literal(16);
        auto from = [&](PyObject* owner, const char* key) { return method(owner, "get", p::text(key).get()); };
        put(row.get(), "round", from(truthy(start) ? start.get() : p::dict().get(), "round_id"));
        put(row.get(), "projectile", p::retain(projectile_id));
        put(row.get(), "bot", from(record, "network_id"));
        put(row.get(), "worker_launch_time_us", from(event, "bot_launch_time_us"));
        put(row.get(), "pose_launch_time_us", from(event, "bot_presentation_time_us"));
        put(row.get(), "presented_time_us", from(record, "presentation_time_us"));
        put(row.get(), "matrix_angles", p::retain(angles.get()));
        auto aim = p::Ref(PySequence_List(method(record, "get", p::text("_remote_aim_signature").get(), p::Ref(PyTuple_New(0)).get()).get()));
        put(row.get(), "presented_aim", p::Ref(PySequence_GetSlice(aim.get(), 1, PY_SSIZE_T_MAX)));
        put(row.get(), "presented_pose", from(record, "_remote_pose_signature"));
        put(row.get(), "ideal_delay_us", from(sync_record, "interpolation_delay_us"));
        put(row.get(), "display_delay_us", from(sync_record, "presentation_delay_us"));
        put(row.get(), "observed_delay_us", from(sync_record, "observed_delay_us"));
        put(row.get(), "consumer_resume_pending", p::boolean(truthy(from(sync_record, "consumer_resume_pending"))));
        put(row.get(), "snapshot_interval_us", from(sync_record, "snapshot_interval_us"));
        put(row.get(), "shot_yaw", from(event, "shot_yaw"));
        put(row.get(), "shot_pitch", from(event, "shot_pitch"));
        p::Ref separators(PyTuple_Pack(2, p::text(",").get(), p::text(":").get()));
        auto encoded = callKw(module("json").get(), "dumps", {row.get()}, {{"separators", separators.get()}});
        p::Ref line(PyNumber_Add(p::Ref(PyNumber_Add(p::text("[Offline LAN 0.9.22] BOT FIRE PRESENT ").get(), encoded.get())).get(),
                                 p::text("\n").get()));
        writeStdout(line.get());
    } catch (const std::exception&) {
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
        PyErr_Clear();
    }
}

PyObject* showShot(PyObject*, PyObject* args) {
    PyObject *battle, *event, *update_state, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &event, &update_state, &g)) return nullptr;
    try {
        auto key = eventEntityKey(event, true);
        if (key.get() == Py_None) raise("ordered shot event has no attacker");
        auto record = recordsGet(battle, key.get());
        if (record.get() == Py_None) raise("ordered shot event attacker is unavailable: " + keyText(key.get()));
        auto projectile_text = str(method(event, "get", p::text("projectile_id").get(), p::text("").get()).get());
        const bool local = truthy(method(record.get(), "get", p::text("local").get()));
        const bool replay = truthy(attr(battle, "_replay_mode"));
        if (local && !replay) {
            auto presented = attr(battle, "_local_projectile_presented");
            if (member(presented.get(), projectile_text.get()) ||
                member(attr(battle, "_local_projectile_presentations").get(), projectile_text.get()))
                Py_RETURN_TRUE;
            method(presented.get(), "add", projectile_text.get());
        }
        if (truthy(p::retain(update_state))) {
            auto seconds = attr(global(g, "spotting").get(), "SHOT_CAMOUFLAGE_SECONDS");
            p::Ref until(PyNumber_Add(method(battle, "_clock").get(), seconds.get()));
            put(record.get(), "shot_penalty_until", std::move(until));
            method(module("gui.mods.offline_lan_0922.native_client_environment").get(), "lifecycle", battle, record.get());
        }
        // #1513 Vehicle.showShooting owns a descriptor burst's effects through one native call.
        p::Ref local_burst = p::retain(Py_None);
        if (local) {
            auto intent = attr(battle, "_local_fire_intent");
            if (PyDict_Check(intent.get())) {
                auto burst = method(intent.get(), "get", p::text("player_burst").get());
                if (burst.get() != Py_None)
                    replace(local_burst, p::Ref(PyTuple_Pack(2, p::Ref(PyObject_GetItem(burst.get(), p::text("committed").get())).get(),
                                                             p::Ref(PyObject_GetItem(burst.get(), p::text("count").get())).get())));
            }
        }
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_TRUE;
        auto entity = method(battle, "_server_entity", p::Ref(PyObject_GetItem(record.get(), p::text("engine_id").get())).get());
        if (entity.get() == Py_None) raise("ordered shot event attacker has no native entity: " + keyText(key.get()));
        if (equal(method(record.get(), "get", p::text("kind").get()).get(), p::text("bot").get())) {
            auto sync = attr(battle, "_sync");
            p::Ref sync_record = p::dict();
            if (sync.get() != Py_None) {
                PyObject* entities = PyObject_GetAttrString(sync.get(), "_entities");
                if (!entities) {
                    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("sync entities lookup failed");
                    PyErr_Clear(); entities = PyDict_New();
                }
                p::Ref held(entities);
                replace(sync_record, method(held.get(), "get", key.get(), p::dict().get()));
            }
            botFireDiagnostic(battle, event, record.get(), entity.get(), projectile_text.get(), sync_record.get());
        }
        auto transient = p::list();
        p::Ref deferred = p::retain(Py_None);
        try {
            p::Ref normalized = p::retain(Py_None);
            bool visual_admitted = true;
            auto burst_index = intOr(method(event, "get", p::text("burst_index").get(), integer(0).get()).get(), 0);
            setAttr(entity.get(), "_offlineLANShotIndex", nonNegative(event, "shell_index").get());
            auto number = global(g, "_number");
            if (member(event, p::text("shot_yaw").get()) && member(event, p::text("shot_pitch").get()) &&
                truthy(optionalAttr(entity.get(), "_offlineLANPresentation"))) {
                setAttr(entity.get(), "_offlineLANShotYaw",
                        p::Ref(PyObject_CallFunctionObjArgs(number.get(), method(event, "get", p::text("shot_yaw").get()).get(), nullptr)).get());
                setAttr(entity.get(), "_offlineLANShotPitch",
                        p::Ref(PyObject_CallFunctionObjArgs(number.get(), method(event, "get", p::text("shot_pitch").get()).get(), nullptr)).get());
                p::append(transient.get(), p::text("_offlineLANShotYaw"));
                p::append(transient.get(), p::text("_offlineLANShotPitch"));
            }
            bool canonical = true;
            for (const char* name : {"origin", "velocity", "gravity", "maxDistance"}) canonical = canonical && member(event, p::text(name).get());
            if (canonical && !member(attr(battle, "_projectile_visual_terminals").get(), projectile_text.get())) {
                replace(normalized, method(battle, "_projectile_wire_meta", event));
                if (normalized.get() != Py_None) {
                    p::Ref launch_visual(PyDict_Copy(normalized.get()));
                    put(normalized.get(), "source_descriptor", attr(entity.get(), "typeDescriptor"));
                    auto current = method(attr(battle, "_projectile_meta").get(), "get", projectile_text.get());
                    if (current.get() != Py_None) {
                        // The delayed muzzle presents the immutable launch, not a new authority cursor.
                        p::Ref iterator(PyObject_GetIter(global(g, "_PROJECTILE_IMMUTABLE_FIELDS").get()));
                        while (PyObject* raw = PyIter_Next(iterator.get())) {
                            p::Ref field(raw);
                            auto a = method(current.get(), "get", field.get());
                            auto b = method(normalized.get(), "get", field.get());
                            const int differ = PyObject_RichCompareBool(a.get(), b.get(), Py_NE);
                            if (differ < 0) throw p::Error("immutable field comparison failed");
                            if (differ) raise("canonical projectile launch changed");
                        }
                        if (PyErr_Occurred()) throw p::Error("immutable fields iteration failed");
                        replace(normalized, std::move(current));
                    } else {
                        replace(normalized, method(battle, "_install_projectile_meta", normalized.get()));
                    }
                    put(normalized.get(), "source_descriptor", attr(entity.get(), "typeDescriptor"));
                    put(normalized.get(), "muzzle_presented", p::retain(Py_True));
                    if (method(normalized.get(), "get", p::text("bot_presentation_time_us").get()).get() != Py_None) {
                        put(normalized.get(), "presented_ricochet_count", integer(0));
                        p::Ref rendered(PyDict_Copy(normalized.get()));
                        for (const char* field : {"segment_origin", "segment_velocity", "segment_start_time_ms", "ricochet_count"})
                            put(rendered.get(), field, p::Ref(PyObject_GetItem(launch_visual.get(), p::text(field).get())));
                        replace(normalized, std::move(rendered));
                    }
                    replace(burst_index, p::Ref(PyObject_GetItem(normalized.get(), p::text("burst_index").get())));
                }
                auto projectile_id = method(event, "get", p::text("projectile_id").get());
                auto origin = method(event, "get", p::text("origin").get());
                auto velocity = method(event, "get", p::text("velocity").get());
                p::Ref gravity(PyObject_CallFunctionObjArgs(number.get(), method(event, "get", p::text("gravity").get()).get(), nullptr));
                if (normalized.get() != Py_None) {
                    auto visual_start = method(battle, "_projectile_visual_start", entity.get(), normalized.get());
                    if (method(normalized.get(), "get", p::text("bot_presentation_time_us").get()).get() != Py_None)
                        replace(deferred, p::Ref(PyTuple_Pack(2, normalized.get(), visual_start.get())));
                    else
                        callKw(battle, "_ensure_projectile_visual", {normalized.get(), method(battle, "_clock").get()},
                               {{"visual_start", visual_start.get()}});
                    auto visual = method(attr(battle, "_projectile_visual_meta").get(), "get",
                                         p::Ref(PyObject_GetItem(normalized.get(), p::text("projectile_id").get())).get());
                    visual_admitted = visual.get() == Py_None || truthy(method(visual.get(), "get", p::text("admitted").get(), Py_True));
                }
                auto max_distance = method(event, "get", p::text("maxDistance").get());
                // RemoteVehicle.showShooting owns muzzle and recoil; the native tracer owns the flight.
                const std::pair<const char*, PyObject*> values[] = {
                    {"_offlineLANShotOrigin", origin.get()}, {"_offlineLANShotVelocity", velocity.get()},
                    {"_offlineLANShotGravity", gravity.get()}, {"_offlineLANShotMaxDistance", max_distance.get()},
                    {"_offlineLANProjectileID", projectile_id.get()}, {"_offlineLANCanonicalTracerOwned", Py_True}};
                for (const auto& value : values) {
                    setAttr(entity.get(), value.first, value.second);
                    p::append(transient.get(), p::text(value.first));
                }
            }
            if (local_burst.get() != Py_None) replace(burst_index, p::retain(PyTuple_GET_ITEM(local_burst.get(), 0)));
            if (equal(burst_index.get(), integer(0).get())) {
                auto gun = attr(attr(entity.get(), "typeDescriptor").get(), "gun");
                p::Ref one_tuple(Py_BuildValue("(i)", 1));
                auto burst = p::Ref(PyObject_CallFunctionObjArgs(global(g, "_field").get(), gun.get(), p::text("burst").get(), one_tuple.get(), nullptr));
                p::Ref descriptor_count = integer(1);
                PyObject* first = PyObject_GetItem(burst.get(), integer(0).get());
                if (first) {
                    p::Ref held(first);
                    replace(descriptor_count, intOr(held.get(), 1));
                } else if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_IndexError)) {
                    PyErr_Clear();
                } else throw p::Error("burst lookup failed");
                auto burst_count = normalized.get() != Py_None ? method(normalized.get(), "get", p::text("burst_count").get()) :
                    method(event, "get", p::text("burst_count").get(), descriptor_count.get());
                replace(burst_count, intOr(burst_count.get(), 1));
                const int above = PyObject_RichCompareBool(burst_count.get(), integer(1).get(), Py_GT);
                if (above < 0) throw p::Error("burst count comparison failed");
                if (!above) replace(burst_count, integer(1));
                if (local_burst.get() != Py_None) replace(burst_count, p::retain(PyTuple_GET_ITEM(local_burst.get(), 1)));
                if (visual_admitted &&
                    truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {p::text("shot muzzle presentation").get()})) &&
                    truthy(method(battle, "_shot_muzzle_drawn", record.get()))) {
                    if (replay && local) {
                        p::Ref call_args(PyTuple_Pack(2, entity.get(), burst_count.get()));
                        optionalFeatureArgs(battle, "shot muzzle presentation", attr(battle, "_show_replay_local_muzzle").get(), call_args.get());
                    } else {
                        p::Ref call_args(PyTuple_Pack(2, burst_count.get(), Py_False));
                        optionalFeatureArgs(battle, "shot muzzle presentation", attr(entity.get(), "showShooting").get(), call_args.get());
                    }
                } else if (local && !replay) {
                    p::Ref call_args(PyTuple_Pack(2, entity.get(), burst_count.get()));
                    optionalFeatureArgs(battle, "local shot convergence", attr(battle, "_show_local_shot_without_extra").get(), call_args.get());
                }
            }
            if (deferred.get() != Py_None)
                callKw(battle, "_ensure_projectile_visual", {PyTuple_GET_ITEM(deferred.get(), 0), method(battle, "_clock").get()},
                       {{"visual_start", PyTuple_GET_ITEM(deferred.get(), 1)}});
        } catch (const std::exception&) {
            // finally: remove the transient attributes and keep the original error.
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(transient.get()); ++i)
                if (PyObject_DelAttr(entity.get(), PyList_GET_ITEM(transient.get(), i)) < 0) PyErr_Clear();
            PyErr_Restore(type, value, trace);
            throw;
        }
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(transient.get()); ++i)
            if (PyObject_DelAttr(entity.get(), PyList_GET_ITEM(transient.get(), i)) < 0) {
                if (!PyErr_ExceptionMatches(PyExc_Exception)) throw p::Error("transient attribute cleanup failed");
                PyErr_Clear();
            }
        auto projectile_id = method(event, "get", p::text("projectile_id").get());
        auto timeline = method(attr(battle, "_fire_timeline").get(), "get", str(projectile_id.get()).get());
        if (timeline.get() != Py_None && method(timeline.get(), "get", p::text("shown").get()).get() == Py_None) {
            p::Ref shown(PyObject_CallObject(global(g, "_PROFILE_CLOCK").get(), nullptr));
            put(timeline.get(), "shown", p::retain(shown.get()));
            auto seq = [&](const char* name) {
                auto raw = method(event, "get", p::text(name).get(), integer(0).get());
                return toInt(truthy(raw) ? raw.get() : integer(0).get());
            };
            p::Ref elapsed(PyNumber_Subtract(shown.get(), p::Ref(PyObject_GetItem(timeline.get(), p::text("trigger").get())).get()));
            const int positive_elapsed = PyObject_RichCompareBool(elapsed.get(), p::number(0.).get(), Py_GT);
            if (positive_elapsed < 0) throw p::Error("fire timeline comparison failed");
            p::Ref since(PyNumber_Multiply(positive_elapsed ? elapsed.get() : p::number(0.).get(), p::number(1000.).get()));
            p::Ref values(PyTuple_Pack(4, seq("fire_intent_seq").get(), seq("shot_seq").get(), projectile_id.get(), since.get()));
            writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] FIRE SHOWN intent=%d shot_seq=%d "
                                                       "projectile=%s since_trigger_ms=%.0f\n").get(), values.get())).get());
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* presentDamageSticker(PyObject*, PyObject* args) {
    PyObject *battle, *event, *target_record;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &target_record)) return nullptr;
    try {
        const char* feature_name = "projectile damage stickers";
        if (truthy(attr(battle, "_worker_mode")) || !member(event, p::text("damage_sticker").get()) ||
            truthy(method(event, "get", p::text("splash").get(), Py_False)) ||
            !truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {p::text(feature_name).get()})))
            Py_RETURN_FALSE;
        auto target = method(battle, "_server_entity", method(target_record, "get", p::text("engine_id").get()).get());
        if (target.get() == Py_None) Py_RETURN_FALSE;
        if (!truthy(optionalAttr(target.get(), "isStarted")) || optionalAttr(target.get(), "typeDescriptor").get() == Py_None)
            Py_RETURN_FALSE;
        try {
            auto decoder = attr(module("VehicleEffects").get(), "DamageFromShotDecoder");
            p::Ref code(PyObject_GetItem(event, p::text("damage_sticker").get()));
            auto decoded = method(decoder.get(), "decodeSegment", code.get(), attr(target.get(), "typeDescriptor").get());
            auto items = unpack(decoded.get(), 4);
            PyObject *component = items[0].get(), *sticker = items[1].get(), *start = items[2].get(), *end = items[3].get();
            // #1513 VehicleStickers creates only hull/turret/gun owners.
            if (equal(component, p::text("chassis").get())) Py_RETURN_FALSE;
            auto appearance = optionalAttr(target.get(), "appearance");
            auto add = optionalAttr(appearance.get(), "addDamageSticker");
            if (!PyObject_IsTrue(component) || start == Py_None || end == Py_None || equal(start, end) || !PyCallable_Check(add.get()))
                raise("#1513 damage-sticker boundary is unavailable");
            p::Ref(PyObject_CallFunctionObjArgs(add.get(), code.get(), component, sticker, start, end, nullptr));
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            auto error = caught(e);
            callKw(battle, "_warn_optional_failure", {p::text(feature_name).get(), error.get()}, {{"disable", Py_False}});
            Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* battleResult(PyObject*, PyObject* args) {
    PyObject *battle, *result;
    if (!PyArg_ParseTuple(args, "OO", &battle, &result)) return nullptr;
    try {
        if (!PyDict_Check(result)) Py_RETURN_FALSE;
        setAttr(battle, "_battle_result", p::Ref(PyDict_Copy(result)).get());
        if (truthy(attr(battle, "_worker_mode"))) {
            // Stop authority simulation during the server's result-publication window.
            setAttr(battle, "_battle_live", Py_False);
            setAttr(battle, "_round_finished_notified", Py_True);
            method(battle, "_report_memory", p::text("round_end").get());
            Py_RETURN_TRUE;
        }
        if (truthy(attr(battle, "_round_finished_notified")) || attr(battle, "_avatar").get() == Py_None ||
            !equal(attr(battle, "state").get(), p::text("running").get()))
            Py_RETURN_FALSE;
        auto finish = optionalAttr(attr(attr(battle, "_runtime").get(), "constants").get(), "FINISH_REASON");
        if (finish.get() == Py_None) raise("FINISH_REASON is unavailable");
        auto reason_value = method(result, "get", p::text("reason").get(), p::text("").get());
        auto lowered = method(str(reason_value.get()).get(), "lower");
        const auto name = p::string(lowered.get());
        auto has = [&](const char* part) { return name.find(part) != std::string::npos; };
        p::Ref reason = p::retain(Py_None);
        if (has("eliminat") || has("exterminat") || name == "team_eliminated") replace(reason, attr(finish.get(), "EXTERMINATION"));
        else if (has("base") || has("captur")) replace(reason, attr(finish.get(), "BASE"));
        else if (has("timeout") || has("time_out")) replace(reason, attr(finish.get(), "TIMEOUT"));
        else {
            PyObject* unknown = PyObject_GetAttrString(finish.get(), "UNKNOWN");
            if (!unknown) {
                if (!PyErr_ExceptionMatches(PyExc_AttributeError)) return nullptr;
                PyErr_Clear(); unknown = integer(4).release();
            }
            p::Ref fallback(unknown);
            PyObject* failure = PyObject_GetAttrString(finish.get(), "FAILURE");
            if (!failure) {
                if (!PyErr_ExceptionMatches(PyExc_AttributeError)) return nullptr;
                PyErr_Clear(); failure = fallback.release();
            }
            replace(reason, p::Ref(failure));
        }
        auto avatar = attr(battle, "_avatar");
        p::Ref callback = p::retain(Py_None);
        if (!callableAttr(avatar.get(), "onRoundFinished", callback)) raise("Avatar.onRoundFinished is unavailable");
        auto clamp = [&](const char* field) {
            const auto value = strictInt(method(result, "get", p::text(field).get(), integer(0).get()).get());
            return integer(std::max<std::int64_t>(0, std::min<std::int64_t>(value, 2)));
        };
        auto winner = clamp("winner");
        auto base_team = clamp("base_team");
        const auto base = PyInt_AsLong(base_team.get());
        if (base == 1 || base == 2) {
            p::Ref captured = p::retain(Py_None);
            if (callableAttr(attr(avatar.get(), "arena").get(), "onTeamBaseCaptured", captured))
                p::Ref(PyObject_CallFunctionObjArgs(captured.get(), base_team.get(), integer(0).get(), nullptr));
        }
        // Publish the afterbattle period first so its owners hold the outcome before the notification runs.
        p::Ref period_args(PyTuple_Pack(2, winner.get(), reason.get()));
        optionalFeature(battle, "afterbattle arena period", attr(battle, "_publish_afterbattle_period").get(), period_args.get());
        p::Ref(PyObject_CallFunctionObjArgs(callback.get(), winner.get(), reason.get(), nullptr));
        setAttr(battle, "_round_finished_notified", Py_True);
        method(battle, "_report_memory", p::text("round_end").get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef event_methods[] = {
    {"apply_combat_event", applyCombatEvent, METH_VARARGS, "Apply one ordered combat event to its target."},
    {"on_events", onEvents, METH_VARARGS, "Accept one ordered events message into the journal."},
    {"prepare_ordered_event", prepareOrderedEvent, METH_VARARGS, "Validate and merge one event's durable state."},
    {"event_is_ready", eventIsReady, METH_VARARGS, "Whether one journaled event may apply now."},
    {"apply_ordered_event", applyOrderedEvent, METH_VARARGS, "Apply one journaled event by kind."},
    {"drain_event_journal", drainEventJournal, METH_VARARGS, "Apply every ready journaled event in order."},
    {"known_event_state", knownEventState, METH_VARARGS, "The record or pending create an event references."},
    {"merge_shot_event_state", mergeShotEventState, METH_VARARGS, "Record a shot's camouflage penalty."},
    {"merge_combat_event_state", mergeCombatEventState, METH_VARARGS, "Merge a combat event into its target state."},
    {"merge_stun_event_state", mergeStunEventState, METH_VARARGS, "Merge a stun event into its target state."},
    {"combat_event_state", combatEventState, METH_VARARGS, "The target state after one combat event."},
    {"combat_attack_reason", combatAttackReason, METH_VARARGS, "Validate a combat event's attack reason."},
    {"validate_combat_event_contract", validateCombatEventContract, METH_VARARGS, "Validate a combat event's source contract."},
    {"missing_projectile_attacker_allowed", missingProjectileAttackerAllowed, METH_VARARGS, "Whether delayed damage may outlive its shooter."},
    {"pending_event_references", pendingEventReferences, METH_VARARGS, "Whether the journal still references an entity."},
    {"attack_reason", attackReason, METH_VARARGS, "The #1513 ATTACK_REASON index of one member."},
    {"blind_local_attack", blindLocalAttack, METH_VARARGS, "Whether a local hit on an unspotted enemy stays silent."},
    {"vehicle_statistics_event", vehicleStatisticsEvent, METH_VARARGS, "Apply one frags/team-killer event."},
    {"present_direct_spot", presentDirectSpot, METH_VARARGS, "Publish the stock ribbon for a credited detection."},
    {"detection_event", detectionEvent, METH_VARARGS, "Draw the spotting ribbon the server credited."},
    {"assist_event", assistEvent, METH_VARARGS, "Feed one server-attributed assist to the damage log."},
    {"environment_critical_feedback", environmentCriticalFeedback, METH_VARARGS, "Present an environment critical checkpoint."},
    {"destructible_event", destructibleEvent, METH_VARARGS, "Route one destructible event to its owner."},
    {"present_damage_sticker", presentDamageSticker, METH_VARARGS, "Add one admitted direct hit to the sticker owner."},
    {"battle_result", battleResult, METH_VARARGS, "Publish the round result to the stock avatar."},
    {"present_combat_feedback", presentCombatFeedback, METH_VARARGS, "Feed accepted combat through stock feedback RPCs."},
    {"present_combat_hit", presentCombatHit, METH_VARARGS, "Present one shot hit through the #1513 hit APIs."},
    {"show_shot", showShot, METH_VARARGS, "Present one ordered shot: muzzle, burst and canonical tracer."},
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* events_methods() { return event_methods; }
}
