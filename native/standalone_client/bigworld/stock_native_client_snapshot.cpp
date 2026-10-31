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

PyMethodDef methods[] = {
    {"apply_rules", applyRules, METH_VARARGS, "Present changed team base capture points."},
    {"on_snapshot", onSnapshot, METH_VARARGS, "Apply one accepted snapshot."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* snapshot_ingest_methods() { return snapshot_ingest::methods; }
}
