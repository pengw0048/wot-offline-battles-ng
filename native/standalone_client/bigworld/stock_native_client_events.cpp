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
            return method(battle, "_attack_reason", p::text(name).get(), integer(fallback).get());
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
                    char buffer[64]; std::snprintf(buffer, sizeof(buffer), "%.3f", waited * 1000.);
                    std::string text = p::string(line.get()) + buffer + " pose_launch_us=" + p::string(str(launch.get()).get()) +
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
            callKw(battle, "_show_shot", {event}, {{"update_state", Py_False}});
            method(battle, "_accept_projectile_event", event);
        } else if (member(global(g, "_COMBAT_EVENT_KINDS").get(), kind.get())) {
            callKw(battle, "_apply_combat_event", {event}, {{"update_state", Py_False}});
        } else if (textIn(kind.get(), {"vehicle_statistics"})) method(battle, "_apply_vehicle_statistics_event", event);
        else if (textIn(kind.get(), {"environment_critical"})) method(battle, "_apply_environment_critical_feedback", event);
        else if (textIn(kind.get(), {"assist"})) method(battle, "_apply_assist_event", event);
        else if (textIn(kind.get(), {"detection"})) method(battle, "_apply_detection_event", event);
        else if (textIn(kind.get(), {"stun"})) {
            auto target = recordsGet(battle, stunEntityKey(event).get());
            if (target.get() == Py_None) raise("stun event has no ready target");
            auto state = get(target.get(), "state");
            method(battle, "_apply_stun_state", target.get(), truthy(state) ? state.get() : p::dict().get());
        } else if (textIn(kind.get(), {"destructible"})) {
            if (method(battle, "_apply_destructible_event", event).get() == Py_None) Py_RETURN_FALSE;
        } else if (textIn(kind.get(), {"projectile_ricochet"})) method(battle, "_apply_projectile_ricochet_event", event);
        else if (textIn(kind.get(), {"projectile_impact"})) method(battle, "_apply_projectile_terminal_event", event);
        else if (textIn(kind.get(), {"battle_result"})) method(battle, "_apply_battle_result", event);
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
        method(battle, "_present_damage_sticker", event, r);
        const bool blind = attacker_record.get() != Py_None && truthy(method(battle, "_is_blind_local_attack", r, attacker_record.get()));
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
            method(battle, "_present_combat_hit", event, r, attacker_record.get(), attacker_id.get());
            method(battle, "_present_combat_feedback", event, r, attacker_record.get(), attack_reason.get());
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
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* events_methods() { return event_methods; }
}
