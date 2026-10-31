// Native ports of the pinned BattleRuntime snapshot ingest. Every step the pinned method takes
// through `self` is still called through the instance, so ported and unported steps both apply.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace snapshot_ingest {
namespace {
using namespace py;

bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("snapshot comparison failed");
    return result != 0;
}
p::Ref getattrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
p::Ref valueGet(PyObject* mapping, PyObject* key, PyObject* fallback = nullptr) {
    return fallback ? method(mapping, "get", key, fallback) : method(mapping, "get", key);
}
p::Ref text(const char* value) { return p::text(value); }

// BattleRuntime._apply_rules(rules): team base capture points, presented once per change.
PyObject* applyRules(PyObject*, PyObject* args) {
    PyObject *battle, *rules, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &rules, &g)) return nullptr;
    try {
        auto source = p::flag(rules) ? p::retain(rules) : p::dict();
        auto incoming = valueGet(source.get(), text("bases").get());
        if (!truthy(incoming)) replace(incoming, p::dict());
        auto arena = getattrOr(attr(battle, "_avatar").get(), "arena", Py_None);
        auto callback = getattrOr(arena.get(), "onTeamBasePointsUpdate", Py_None);
        const bool worker = truthy(attr(battle, "_worker_mode"));
        if (!worker && !PyCallable_Check(callback.get())) Py_RETURN_FALSE;
        bool changed = false;
        auto stored = method(attr(battle, "_rules_state").get(), "setdefault", text("bases").get(), p::dict().get());
        auto zero = integer(0);
        auto hundred = integer(100);
        for (long team = 1; team <= 2; ++team) {
            auto team_value = integer(team);
            auto key = p::Ref(PyObject_Str(team_value.get()));
            auto inner = valueGet(incoming.get(), team_value.get(), p::dict().get());
            auto raw = valueGet(incoming.get(), key.get(), inner.get());
            if (!truthy(raw)) replace(raw, p::dict());
            // max(0, min(int(points), 100))
            auto points = p::Ref(PyNumber_Int(valueGet(raw.get(), text("points").get(), zero.get()).get()));
            auto capped = test(hundred.get(), points.get(), Py_LT) ? p::retain(hundred.get()) : std::move(points);
            auto bounded = test(capped.get(), zero.get(), Py_GT) ? std::move(capped) : integer(0);
            const double time_left = number(valueGet(raw.get(), text("time_left").get()).get());
            auto invaders = p::Ref(PyNumber_Int(p::Ref(PyFloat_FromDouble(number(valueGet(raw.get(), text("invaders").get()).get()))).get()));
            auto current = literal(4);
            put(current.get(), "points", std::move(bounded));
            put(current.get(), "time_left", p::Ref(PyFloat_FromDouble(time_left > 0.0 ? time_left : 0.0)));
            put(current.get(), "invaders", test(invaders.get(), zero.get(), Py_GT) ? std::move(invaders) : integer(0));
            put(current.get(), "stopped", p::retain(truthy(valueGet(raw.get(), text("stopped").get(), Py_False)) ? Py_True : Py_False));
            if (!test(valueGet(stored.get(), key.get()).get(), current.get(), Py_NE)) continue;
            if (PyObject_SetItem(stored.get(), key.get(), current.get()) < 0) throw p::Error("bases");
            if (!worker) {
                p::Ref call_args(PyTuple_Pack(6, team_value.get(), zero.get(), PyDict_GetItemString(current.get(), "points"),
                                              PyDict_GetItemString(current.get(), "time_left"), PyDict_GetItemString(current.get(), "invaders"),
                                              PyDict_GetItemString(current.get(), "stopped")));
                p::Ref(PyObject_CallObject(callback.get(), call_args.get()));
            }
            changed = true;
        }
        return p::retain(changed ? Py_True : Py_False).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime.on_snapshot(message): apply one accepted snapshot; a failure restores the previous one.
PyObject* onSnapshot(PyObject*, PyObject* args) {
    PyObject *battle, *message, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &message, &g)) return nullptr;
    try {
        if (textIn(attr(battle, "state").get(), {"failed", "stopped", "leaving"})) Py_RETURN_FALSE;
        auto previous = attr(battle, "_last_snapshot");
        auto last = [&] { return attr(battle, "_last_snapshot"); };
        try {
            setAttr(battle, "_last_snapshot",
                    p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type),
                                                        p::flag(message) ? message : p::dict().get(), nullptr)).get());
            callKw(battle, "_restore_local_equipment_snapshot", {last().get()}, {{"present", Py_True}});
            for (const char* step : {"_ack_local_ram_contacts", "_ack_local_destructible_contacts", "_observe_destructibles_disabled",
                                     "_observe_projectile_message", "_reconcile_projectile_snapshot", "_reconcile_detached_turret_snapshot"})
                method(battle, step, last().get());
            if (member(last().get(), text("rules").get()))
                dispatch(battle, "_apply_rules", "apply_rules", {valueGet(last().get(), text("rules").get()).get()}, {g});
            if (valueGet(last().get(), text("battle_result").get()).get() != Py_None)
                method(battle, "_apply_battle_result", p::Ref(PyObject_GetItem(last().get(), text("battle_result").get())).get());
            if (member(last().get(), text("destructibles").get()))
                method(battle, "_apply_destructible_state", valueGet(last().get(), text("destructibles").get()).get());
            if (attr(battle, "_bots").get() != Py_None) {
                if (member(last().get(), text("bot_authority_id").get()))
                    method(battle, "_reconcile_bot_authority", valueGet(last().get(), text("bot_authority_id").get()).get());
                method(attr(battle, "_bots").get(), "apply_snapshot", last().get());
                method(battle, "_remember_ram_bot_snapshot", last().get());
            }
            auto sync = attr(battle, "_sync");
            if (sync.get() != Py_None) {
                try {
                    method(sync.get(), "snapshot", message);
                } catch (const std::exception& e) {
                    // The Bot state store has consumed this frame; contain the presentation timeline alone.
                    if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                    auto error = caught(e);
                    method(battle, "_ignore_live_payload", text("snapshot_sync").get(), error.get(), message);
                }
            }
            Py_RETURN_TRUE;
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            auto error = caught(e);
            setAttr(battle, "_last_snapshot", previous.get());
            method(battle, "_ignore_live_payload", text("snapshot").get(), error.get(), message);
            Py_RETURN_FALSE;
        }
    } catch (const std::exception& e) { return fail(e); }
}

// `try: body() except (TypeError, ValueError, OverflowError):` — false when one of those was raised.
template<class Body> bool conversion(Body body) {
    try { body(); return true; }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError) &&
            !PyErr_ExceptionMatches(PyExc_OverflowError))
            throw;
        PyErr_Clear();
        return false;
    }
}
p::Ref asInt(PyObject* v) { return p::Ref(PyNumber_Int(v)); }
p::Ref orEmpty(p::Ref v) { return truthy(v) ? std::move(v) : p::Ref(PyTuple_New(0)); }
p::Ref bisectCall(PyObject* g, const char* name, PyObject* list, PyObject* key) {
    return method(global(g, "bisect").get(), name, list, key);
}

// BattleRuntime._remove_ram_bot_history_index(revision, sample_time_us, states)
PyObject* removeRamBotHistoryIndex(PyObject*, PyObject* args) {
    PyObject *battle, *revision, *sample_time_us, *states, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &revision, &sample_time_us, &states, &g)) return nullptr;
    try {
        if (sample_time_us == Py_None) Py_RETURN_NONE;
        p::Ref sample_key(PyTuple_Pack(2, sample_time_us, revision));
        auto index_map = attr(battle, "_ram_bot_history_index");
        p::Ref iterator(PyObject_GetIter(states));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref bot_id(raw);
            auto timeline = method(index_map.get(), "get", bot_id.get());
            if (!truthy(timeline)) continue;
            auto index = bisectCall(g, "bisect_left", timeline.get(), sample_key.get());
            const Py_ssize_t length = PyObject_Size(timeline.get());
            if (length < 0) throw p::Error("len");
            if (test(index.get(), integer(length).get(), Py_LT) &&
                test(p::Ref(PyObject_GetItem(timeline.get(), index.get())).get(), sample_key.get(), Py_EQ))
                method(timeline.get(), "pop", index.get());
            if (!truthy(timeline)) method(index_map.get(), "pop", bot_id.get(), Py_None);
        }
        if (PyErr_Occurred()) throw p::Error("states");
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._remember_ram_bot_snapshot(snapshot): the canonical Bot bodies player contacts reference.
PyObject* rememberRamBotSnapshot(PyObject*, PyObject* args) {
    PyObject *battle, *snapshot, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &snapshot, &g)) return nullptr;
    try {
        // Native peers and trigger ledgers retain the accepted receipts on a visible client.
        if (!truthy(attr(battle, "_worker_mode")) && !truthy(attr(battle, "_replay_mode")) &&
            getattrOr(battle, "_native_client_state", Py_None).get() != Py_None)
            Py_RETURN_FALSE;
        auto bots = attr(battle, "_bots");
        if (!PyDict_Check(snapshot) || bots.get() == Py_None) Py_RETURN_FALSE;
        p::Ref revision = p::retain(Py_None), sample_time_us = p::retain(Py_None);
        if (!conversion([&] {
                replace(revision, asInt(valueGet(snapshot, text("bot_state_revision").get()).get()));
                replace(sample_time_us, asInt(valueGet(snapshot, text("bot_state_time_us").get()).get()));
            }))
            Py_RETURN_FALSE;
        auto zero = integer(0);
        if (test(revision.get(), zero.get(), Py_LT) || test(sample_time_us.get(), zero.get(), Py_LT)) Py_RETURN_FALSE;
        auto states = p::dict();
        auto current = getattrOr(bots.get(), "states", p::dict().get());
        if (!truthy(current)) replace(current, p::dict());
        p::Ref rows(PyObject_GetIter(orEmpty(valueGet(snapshot, text("bots").get())).get()));
        while (PyObject* item = PyIter_Next(rows.get())) {
            p::Ref raw(item);
            if (!PyDict_Check(raw.get()) || valueGet(raw.get(), text("id").get()).get() == Py_None) continue;
            p::Ref bot_id = p::retain(Py_None);
            if (!conversion([&] { replace(bot_id, asInt(p::Ref(PyObject_GetItem(raw.get(), text("id").get())).get())); })) continue;
            // Dynamic pose fields come from this wire revision; static fields may come from the newer local state.
            auto state = p::dict();
            auto current_state = method(current.get(), "get", bot_id.get());
            if (PyDict_Check(current_state.get())) {
                for (const char* name : {"mass", "collision_shape", "ram_profile", "vehicle", "team"}) {
                    auto key = text(name);
                    if (member(current_state.get(), key.get()))
                        if (PyObject_SetItem(state.get(), key.get(), p::Ref(PyObject_GetItem(current_state.get(), key.get())).get()) < 0)
                            throw p::Error("state");
                }
            }
            method(state.get(), "update", raw.get());
            if (PyObject_SetItem(states.get(), bot_id.get(), state.get()) < 0) throw p::Error("states");
        }
        if (PyErr_Occurred()) throw p::Error("bots");
        auto history = attr(battle, "_ram_bot_history");
        auto times = attr(battle, "_ram_bot_history_times");
        auto previous_states = method(history.get(), "get", revision.get());
        auto previous_time = method(times.get(), "get", revision.get());
        if (PyDict_Check(previous_states.get()))
            dispatch(battle, "_remove_ram_bot_history_index", "remove_ram_bot_history_index",
                     {revision.get(), previous_time.get(), previous_states.get()}, {g});
        if (!member(attr(battle, "_ram_bot_history").get(), revision.get()))
            method(attr(battle, "_ram_bot_history_order").get(), "append", revision.get());
        if (PyObject_SetItem(attr(battle, "_ram_bot_history").get(), revision.get(), states.get()) < 0 ||
            PyObject_SetItem(attr(battle, "_ram_bot_history_times").get(), revision.get(), sample_time_us.get()) < 0)
            throw p::Error("history");
        p::Ref sample_key(PyTuple_Pack(2, sample_time_us.get(), revision.get()));
        auto index_map = attr(battle, "_ram_bot_history_index");
        p::Ref ids(PyObject_GetIter(states.get()));
        while (PyObject* item = PyIter_Next(ids.get())) {
            p::Ref bot_id(item);
            bisectCall(g, "insort_right", method(index_map.get(), "setdefault", bot_id.get(), p::Ref(PyList_New(0)).get()).get(),
                       sample_key.get());
        }
        if (PyErr_Occurred()) throw p::Error("index");
        // A later sample can complete a bracket; invalidate resolved lookups once per history advance.
        method(attr(battle, "_ram_bot_lookup_cache").get(), "clear");
        while (true) {
            auto order = attr(battle, "_ram_bot_history_order");
            const Py_ssize_t length = PyObject_Size(order.get());
            if (length < 0) throw p::Error("len");
            if (length <= 512) break;
            auto expired = method(order.get(), "pop", integer(0).get());
            auto expired_states = method(attr(battle, "_ram_bot_history").get(), "pop", expired.get(), Py_None);
            auto expired_time = method(attr(battle, "_ram_bot_history_times").get(), "pop", expired.get(), Py_None);
            if (PyDict_Check(expired_states.get()))
                dispatch(battle, "_remove_ram_bot_history_index", "remove_ram_bot_history_index",
                         {expired.get(), expired_time.get(), expired_states.get()}, {g});
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// The local player's row of snapshot.players: (admitted, resolved) through `read`, or false.
template<class Read> bool localContactRow(PyObject* battle, PyObject* snapshot, Read read, bool& returned_false) {
    returned_false = false;
    auto local_id = asInt(attr(attr(battle, "client").get(), "player_id").get());
    p::Ref rows(PyObject_GetIter(orEmpty(valueGet(snapshot, text("players").get())).get()));
    while (PyObject* item = PyIter_Next(rows.get())) {
        p::Ref raw(item);
        if (!PyDict_Check(raw.get())) continue;
        bool skip = false;
        if (!conversion([&] {
                if (test(asInt(valueGet(raw.get(), text("id").get()).get()).get(), local_id.get(), Py_NE)) { skip = true; return; }
                read(raw.get(), returned_false);
            }))
            return false;
        if (skip) continue;
        return true;
    }
    if (PyErr_Occurred()) throw p::Error("players");
    return true;
}

// BattleRuntime._ack_local_ram_contacts(snapshot): release resolved local ram receipts.
PyObject* ackLocalRamContacts(PyObject*, PyObject* args) {
    PyObject *battle, *snapshot, *drive, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &snapshot, &drive, &g)) return nullptr;
    try {
        if (truthy(method(drive, "enabled", battle))) return method(drive, "ack_contacts", battle, snapshot, Py_False).release();
        if (attr(battle, "client").get() == Py_None || !PyDict_Check(snapshot)) Py_RETURN_FALSE;
        p::Ref admitted = p::retain(Py_None), resolved = p::retain(Py_None);
        bool unused = false;
        if (!localContactRow(battle, snapshot, [&](PyObject* raw, bool&) {
                replace(admitted, asInt(valueGet(raw, text("ram_contact_admitted_seq").get(), integer(0).get()).get()));
                replace(resolved, asInt(valueGet(raw, text("ram_contact_resolved_seq").get(), integer(0).get()).get()));
            }, unused)) {
            replace(admitted, p::retain(Py_None));
            replace(resolved, p::retain(Py_None));
        }
        auto zero = integer(0);
        if (admitted.get() == Py_None || resolved.get() == Py_None || test(admitted.get(), zero.get(), Py_LT) ||
            test(resolved.get(), zero.get(), Py_LT) || test(resolved.get(), admitted.get(), Py_GT))
            Py_RETURN_FALSE;
        auto previous = attr(battle, "_local_ram_admitted_seq");
        p::Ref changed = p::retain(test(admitted.get(), previous.get(), Py_GT) ? Py_True : Py_False);
        setAttr(battle, "_local_ram_admitted_seq", test(admitted.get(), previous.get(), Py_GT) ? admitted.get() : previous.get());
        auto receipts = attr(battle, "_local_ram_receipts");
        p::Ref keys(PySequence_List(receipts.get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keys.get()); ++i) {
            PyObject* seq = PyList_GET_ITEM(keys.get(), i);
            if (!test(seq, resolved.get(), Py_LE)) continue;
            method(receipts.get(), "pop", seq, Py_None);
            replace(changed, p::retain(Py_True));
        }
        if (truthy(receipts)) {
            auto values = method(receipts.get(), "values");
            auto newest = p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyReversed_Type), values.get(), nullptr));
            auto last = p::Ref(PyIter_Next(newest.get()));
            setAttr(battle, "_local_ram_receipt",
                    p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), last.get(), nullptr)).get());
        } else {
            setAttr(battle, "_local_ram_receipt", Py_None);
        }
        return changed.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._ack_local_destructible_contacts(snapshot): settle resolved and rejected local contacts.
PyObject* ackLocalDestructibleContacts(PyObject*, PyObject* args) {
    PyObject *battle, *snapshot, *drive, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &snapshot, &drive, &g)) return nullptr;
    try {
        if (truthy(method(drive, "enabled", battle))) return method(drive, "ack_contacts", battle, snapshot, Py_True).release();
        if (attr(battle, "client").get() == Py_None || !PyDict_Check(snapshot)) Py_RETURN_FALSE;
        p::Ref admitted = p::retain(Py_None), resolved = p::retain(Py_None);
        p::Ref resolved_exact(PyTuple_New(0)), rejected(PyTuple_New(0));
        auto limit = global(g, "DESTRUCTIBLE_CONTACT_INFLIGHT_LIMIT");
        auto ints = [](PyObject* values) {
            p::Ref out(PyList_New(0));
            p::Ref iterator(PyObject_GetIter(values));
            while (PyObject* item = PyIter_Next(iterator.get())) { p::Ref held(item); p::append(out.get(), asInt(item)); }
            if (PyErr_Occurred()) throw p::Error("ints");
            return p::Ref(PyList_AsTuple(out.get()));
        };
        bool rejected_row = false;
        if (!localContactRow(battle, snapshot, [&](PyObject* raw, bool& returned_false) {
                replace(admitted, asInt(valueGet(raw, text("destructible_contact_admitted_seq").get(), integer(0).get()).get()));
                replace(resolved, asInt(valueGet(raw, text("destructible_contact_resolved_seq").get(), integer(0).get()).get()));
                auto raw_exact = valueGet(raw, text("destructible_contact_resolved_seqs").get(), p::Ref(PyTuple_New(0)).get());
                if (!(PyList_Check(raw_exact.get()) || PyTuple_Check(raw_exact.get())) ||
                    test(integer(PyObject_Size(raw_exact.get())).get(), limit.get(), Py_GT)) { returned_false = true; return; }
                replace(resolved_exact, ints(raw_exact.get()));
                const Py_ssize_t count = PyTuple_GET_SIZE(resolved_exact.get());
                bool invalid = false;
                for (Py_ssize_t i = 0; i < count && !invalid; ++i)
                    invalid = !test(PyTuple_GET_ITEM(resolved_exact.get(), i), integer(0).get(), Py_GT);
                if (invalid || p::Ref(PySet_New(resolved_exact.get())).get() == nullptr ||
                    PySet_Size(p::Ref(PySet_New(resolved_exact.get())).get()) != count) { returned_false = true; return; }
                auto raw_rejected = valueGet(raw, text("destructible_contact_rejected_seqs").get(), p::Ref(PyTuple_New(0)).get());
                if (!(PyList_Check(raw_rejected.get()) || PyTuple_Check(raw_rejected.get())) || PyObject_Size(raw_rejected.get()) > 16) {
                    returned_false = true;
                    return;
                }
                replace(rejected, ints(raw_rejected.get()));
            }, rejected_row)) {
            replace(admitted, p::retain(Py_None));
            replace(resolved, p::retain(Py_None));
        }
        if (rejected_row) Py_RETURN_FALSE;
        if (admitted.get() == Py_None || resolved.get() == Py_None) Py_RETURN_FALSE;
        auto contact_seq = attr(battle, "_local_destructible_contact_seq");
        if (!(test(integer(0).get(), resolved.get(), Py_LE) && test(resolved.get(), admitted.get(), Py_LE) &&
              test(admitted.get(), contact_seq.get(), Py_LE)))
            Py_RETURN_FALSE;
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(resolved_exact.get()); ++i) {
            PyObject* value = PyTuple_GET_ITEM(resolved_exact.get(), i);
            if (test(value, resolved.get(), Py_LE) || test(value, admitted.get(), Py_GT)) Py_RETURN_FALSE;
        }
        p::Ref exact(PySet_New(resolved_exact.get()));
        auto previous = attr(battle, "_local_destructible_admitted_seq");
        p::Ref changed = p::retain(test(admitted.get(), previous.get(), Py_GT) ? Py_True : Py_False);
        setAttr(battle, "_local_destructible_admitted_seq", test(admitted.get(), previous.get(), Py_GT) ? admitted.get() : previous.get());
        auto contacts = attr(battle, "_local_destructible_contacts");
        auto safe_poses = attr(battle, "_local_destructible_safe_poses");
        auto settled = [&](PyObject* seq) { return test(seq, resolved.get(), Py_LE) || member(exact.get(), seq); };
        p::Ref rollback(PyList_New(0));
        p::Ref iterator(PyObject_GetIter(contacts.get()));
        while (PyObject* item = PyIter_Next(iterator.get())) {
            p::Ref seq(item);
            if (settled(seq.get()) && member(rejected.get(), seq.get()) && member(safe_poses.get(), seq.get())) p::append(rollback.get(), std::move(seq));
        }
        if (PyErr_Occurred()) throw p::Error("contacts");
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rollback.get()); ++i) {
            auto applied = method(battle, "_apply_local_destructible_rejection", PyList_GET_ITEM(rollback.get(), i));
            if (truthy(applied)) replace(changed, std::move(applied));
        }
        p::Ref keys(PySequence_List(attr(battle, "_local_destructible_contacts").get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keys.get()); ++i) {
            PyObject* seq = PyList_GET_ITEM(keys.get(), i);
            if (!settled(seq)) continue;
            method(attr(battle, "_local_destructible_contacts").get(), "pop", seq, Py_None);
            method(attr(battle, "_local_destructible_safe_poses").get(), "pop", seq, Py_None);
            replace(changed, p::retain(Py_True));
        }
        return changed.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._reconcile_detached_turret_snapshot(message): accept newer landed-turret rows.
PyObject* reconcileDetachedTurretSnapshot(PyObject*, PyObject* args) {
    PyObject *battle, *message, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &message, &bot_module, &g)) return nullptr;
    try {
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get()))) {
            auto accepted = method(bot_module, "detached_ingress", bots.get(), message, method(battle, "_clock").get());
            if (accepted.get() != Py_None) return accepted.release();
        }
        auto start = attr(battle, "_start_message");
        if (!PyDict_Check(message) ||
            test(valueGet(message, text("round_id").get()).get(),
                 valueGet(truthy(start) ? start.get() : p::dict().get(), text("round_id").get()).get(), Py_NE))
            Py_RETURN_FALSE;
        auto rows = valueGet(message, text("detached_turrets").get());
        if (!(PyList_Check(rows.get()) || PyTuple_Check(rows.get()))) Py_RETURN_FALSE;
        auto schema = global(g, "turret_obstacle_schema");
        auto limit = attr(schema.get(), "MAX_ACTIVE_TURRETS");
        bool changed = false;
        auto zero = integer(0);
        auto motion = text("motion_seq");
        p::Ref slice(PySequence_GetSlice(rows.get(), 0, PyNumber_AsSsize_t(limit.get(), PyExc_OverflowError)));
        p::Ref iterator(PyObject_GetIter(slice.get()));
        while (PyObject* item = PyIter_Next(iterator.get())) {
            p::Ref raw(item);
            auto row = method(schema.get(), "normalize_record", raw.get());
            if (row.get() == Py_None) continue;
            auto key = method(schema.get(), "row_key", row.get());
            auto turret_rows = attr(battle, "_detached_turret_rows");
            auto previous = method(turret_rows.get(), "get", key.get());
            if (previous.get() != Py_None &&
                test(method(row.get(), "get", motion.get(), zero.get()).get(), method(previous.get(), "get", motion.get(), zero.get()).get(), Py_LE))
                continue;
            if (previous.get() == Py_None && test(integer(PyObject_Size(turret_rows.get())).get(), limit.get(), Py_GE)) break;
            if (PyObject_SetItem(turret_rows.get(), key.get(), row.get()) < 0) throw p::Error("rows");
            auto proposals = attr(battle, "_detached_turret_proposals");
            auto pending = method(proposals.get(), "get", key.get());
            if (pending.get() == Py_None ||
                test(method(pending.get(), "get", motion.get(), zero.get()).get(), method(row.get(), "get", motion.get(), zero.get()).get(), Py_LE))
                method(proposals.get(), "pop", key.get(), Py_None);
            method(attr(battle, "_detached_turret_geometry").get(), "discard", key.get());
            changed = true;
        }
        if (PyErr_Occurred()) throw p::Error("turrets");
        if (changed && !truthy(attr(battle, "_worker_mode")))
            method(p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.native_client_turret_presentation")).get(), "ingress", battle);
        return p::retain(changed ? Py_True : Py_False).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"apply_rules", applyRules, METH_VARARGS, "Present changed team base capture points."},
    {"on_snapshot", onSnapshot, METH_VARARGS, "Apply one accepted snapshot."},
    {"remove_ram_bot_history_index", removeRamBotHistoryIndex, METH_VARARGS, "Drop one revision from each Bot's RAM timeline."},
    {"remember_ram_bot_snapshot", rememberRamBotSnapshot, METH_VARARGS, "Retain the Bot bodies player contacts reference."},
    {"ack_local_ram_contacts", ackLocalRamContacts, METH_VARARGS, "Release resolved local ram receipts."},
    {"ack_local_destructible_contacts", ackLocalDestructibleContacts, METH_VARARGS, "Settle resolved local destructible contacts."},
    {"reconcile_detached_turret_snapshot", reconcileDetachedTurretSnapshot, METH_VARARGS, "Accept newer landed-turret rows."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* snapshot_ingest_methods() { return snapshot_ingest::methods; }
}
