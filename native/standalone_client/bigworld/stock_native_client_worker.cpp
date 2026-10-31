// Native ports of the pinned BattleRuntime worker steps: control diagnostics, outgoing Bot
// messages and the per-frame transport drain, plus the remote Bot track feed they share a frame with.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace worker_steps {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref flt(double v) { return p::Ref(PyFloat_FromDouble(v)); }
bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("worker comparison failed");
    return result != 0;
}
// Python max(a, b) / min(a, b): the first argument unless the second is strictly greater / smaller.
p::Ref maxOf(PyObject* a, PyObject* b) { return p::retain(test(b, a, Py_GT) ? b : a); }
p::Ref minOf(PyObject* a, PyObject* b) { return p::retain(test(b, a, Py_LT) ? b : a); }
p::Ref getattrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
p::Ref get(PyObject* mapping, const char* name, PyObject* fallback = nullptr) {
    return fallback ? method(mapping, "get", p::text(name).get(), fallback) : method(mapping, "get", p::text(name).get());
}
void bump(PyObject* o, const char* name, PyObject* by) {
    setAttr(o, name, p::Ref(PyNumber_InPlaceAdd(attr(o, name).get(), by)).get());
}
// `try: body() except (AttributeError, TypeError, ValueError, OverflowError):` — false on one of those.
template<class Body> bool converted(Body body) {
    try { body(); return true; }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        if (!PyErr_ExceptionMatches(PyExc_AttributeError) && !PyErr_ExceptionMatches(PyExc_TypeError) &&
            !PyErr_ExceptionMatches(PyExc_ValueError) && !PyErr_ExceptionMatches(PyExc_OverflowError))
            throw;
        PyErr_Clear();
        return false;
    }
}
p::Ref asInt(PyObject* v) { return p::Ref(PyNumber_Int(v)); }
p::Ref asFloat(PyObject* v) { return p::Ref(PyNumber_Float(v)); }

// BattleRuntime._record_worker_control_diagnostics(sample_time_before)
PyObject* recordWorkerControlDiagnostics(PyObject*, PyObject* args) {
    PyObject *battle, *sample_time_before, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &sample_time_before, &g)) return nullptr;
    try {
        auto bots = attr(battle, "_bots");
        if (!truthy(attr(battle, "_worker_mode")) || bots.get() == Py_None) Py_RETURN_FALSE;
        auto zero = integer(0);
        auto zero_float = flt(0.0);
        p::Ref after = integer(0), before = integer(0);
        if (!converted([&] {
                auto a = asInt(attr(bots.get(), "_sample_time_us").get());
                auto b = asInt(sample_time_before);
                replace(after, std::move(a));
                replace(before, std::move(b));
            })) {
            replace(after, integer(0));
            replace(before, integer(0));
        }
        auto advanced = maxOf(zero.get(), p::Ref(PyNumber_Subtract(after.get(), before.get())).get());
        const bool moved = test(advanced.get(), zero.get(), Py_GT);
        auto micro = flt(1000000.0);
        p::Ref control_steps = integer(0);
        if (!converted([&] { replace(control_steps, maxOf(zero.get(), asInt(attr(bots.get(), "_last_update_control_steps").get()).get())); }))
            replace(control_steps, integer(moved ? 1 : 0));
        p::Ref max_control_step = flt(0.0);
        auto seconds_advanced = [&] { return p::Ref(PyNumber_TrueDivide(advanced.get(), micro.get())); };
        if (!converted([&] { replace(max_control_step, maxOf(zero_float.get(), asFloat(attr(bots.get(), "_last_update_max_control_step").get()).get())); }))
            replace(max_control_step, seconds_advanced());
        const double step_value = PyFloat_AsDouble(max_control_step.get());
        if (std::isnan(step_value) || std::isinf(step_value)) replace(max_control_step, seconds_advanced());
        auto debt = maxOf(zero_float.get(), flt(number(getattrOr(bots.get(), "_accumulator", zero_float.get()).get())).get());
        setAttr(battle, "_worker_probe_control_debt", debt.get());
        setAttr(battle, "_worker_probe_max_control_debt", maxOf(attr(battle, "_worker_probe_max_control_debt").get(), debt.get()).get());
        if (moved) {
            if (!test(control_steps.get(), zero.get(), Py_GT)) replace(control_steps, integer(1));
            bump(battle, "_worker_probe_control_steps", control_steps.get());
            setAttr(battle, "_worker_probe_max_control_step",
                    maxOf(attr(battle, "_worker_probe_max_control_step").get(), max_control_step.get()).get());
            auto control_seconds = maxOf(zero_float.get(), flt(number(getattrOr(bots.get(), "_control_seconds", zero_float.get()).get())).get());
            auto epsilon = flt(1.0e-9);
            if (test(control_steps.get(), integer(1).get(), Py_GT) ||
                test(seconds_advanced().get(), p::Ref(PyNumber_Add(control_seconds.get(), epsilon.get())).get(), Py_GT))
                bump(battle, "_worker_probe_catchup_callbacks", integer(1).get());
            if (test(p::Ref(PyNumber_Add(debt.get(), epsilon.get())).get(), control_seconds.get(), Py_GE) &&
                test(control_seconds.get(), zero_float.get(), Py_GT))
                bump(battle, "_worker_probe_control_debt_callbacks", integer(1).get());
        }
        auto navigator = getattrOr(bots.get(), "navigator", Py_None);
        auto searches = getattrOr(navigator.get(), "searches", Py_None);
        auto pending = PyDict_Check(searches.get()) ? integer(PyDict_Size(searches.get())) : integer(0);
        setAttr(battle, "_worker_probe_astar_max_pending", maxOf(attr(battle, "_worker_probe_astar_max_pending").get(), pending.get()).get());
        if (moved && test(pending.get(), zero.get(), Py_GT)) {
            p::Ref remaining = none();
            if (!converted([&] { replace(remaining, asInt(attr(navigator.get(), "search_frame_budget").get())); })) replace(remaining, none());
            if (remaining.get() != Py_None && test(remaining.get(), zero.get(), Py_LE))
                bump(battle, "_worker_probe_astar_budget_exhausted", integer(1).get());
        }
        return p::retain(moved ? Py_True : Py_False).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._worker_diagnostic_totals(): bounded non-negative Bot diagnostic integers.
PyObject* workerDiagnosticTotals(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto provider = getattrOr(attr(battle, "_bots").get(), "diagnostic_totals", Py_None);
        p::Ref raw = p::dict();
        if (PyCallable_Check(provider.get())) {
            PyObject* value = PyObject_CallFunctionObjArgs(provider.get(), nullptr);
            if (value) replace(raw, p::Ref(value));
            else if (PyErr_ExceptionMatches(PyExc_Exception)) { PyErr_Clear(); replace(raw, p::dict()); }
            else return nullptr;
        }
        auto result = p::dict();
        if (PyDict_Check(raw.get())) {
            auto fields = global(g, "_WORKER_DIAGNOSTIC_FIELDS");
            p::Ref iterator(PyObject_GetIter(fields.get()));
            while (PyObject* item = PyIter_Next(iterator.get())) {
                p::Ref name(item);
                auto value = method(raw.get(), "get", name.get());
                if (PyBool_Check(value.get()) || !(PyInt_Check(value.get()) || PyLong_Check(value.get())) ||
                    test(value.get(), integer(0).get(), Py_LT))
                    continue;
                if (PyObject_SetItem(result.get(), name.get(), asInt(value.get()).get()) < 0) throw p::Error("totals");
            }
            if (PyErr_Occurred()) throw p::Error("fields");
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// _worker_control_snapshot's finite_attribute(owner, name)
p::Ref finiteAttribute(PyObject* owner, const char* name) {
    if (owner == Py_None || !PyObject_HasAttrString(owner, name)) return none();
    const double value = number(attr(owner, name).get(), std::nan(""));
    return std::isnan(value) || std::isinf(value) ? none() : flt(value);
}

// BattleRuntime._worker_control_snapshot()
PyObject* workerControlSnapshot(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto navigator = getattrOr(attr(battle, "_bots").get(), "navigator", Py_None);
        auto searches = getattrOr(navigator.get(), "searches", Py_None);
        auto pending = PyDict_Check(searches.get()) ? integer(PyDict_Size(searches.get())) : none();
        auto thousand = flt(1000.0);
        auto ms = [&](const char* name) { return p::Ref(PyNumber_Multiply(attr(battle, name).get(), thousand.get())); };
        auto out = literal(13);
        put(out.get(), "control_steps", attr(battle, "_worker_probe_control_steps"));
        put(out.get(), "catchup_callbacks", attr(battle, "_worker_probe_catchup_callbacks"));
        put(out.get(), "debt_callbacks", attr(battle, "_worker_probe_control_debt_callbacks"));
        put(out.get(), "max_control_step_ms", ms("_worker_probe_max_control_step"));
        put(out.get(), "control_debt_ms", ms("_worker_probe_control_debt"));
        put(out.get(), "max_control_debt_ms", ms("_worker_probe_max_control_debt"));
        put(out.get(), "astar_pending", pending);
        put(out.get(), "astar_credit", finiteAttribute(navigator.get(), "search_credit"));
        put(out.get(), "astar_budget_remaining", finiteAttribute(navigator.get(), "search_frame_budget"));
        put(out.get(), "astar_completed", finiteAttribute(navigator.get(), "search_completed"));
        put(out.get(), "astar_failed", finiteAttribute(navigator.get(), "search_failed"));
        put(out.get(), "astar_budget_exhausted_callbacks", attr(battle, "_worker_probe_astar_budget_exhausted"));
        put(out.get(), "astar_max_pending", attr(battle, "_worker_probe_astar_max_pending"));
        return out.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._drain_worker_transport(): admit queued echoes after the Bot update committed.
PyObject* drainWorkerTransport(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto drain = getattrOr(attr(battle, "client").get(), "dispatch_ready_messages", Py_None);
        if (!truthy(attr(battle, "_worker_mode")) || !PyCallable_Check(drain.get())) Py_RETURN_TRUE;
        auto generation = attr(battle, "_generation");
        auto bots = attr(battle, "_bots");
        auto round_id = [&] {
            auto start = attr(battle, "_start_message");
            return get(truthy(start) ? start.get() : p::dict().get(), "round_id");
        };
        auto round = round_id();
        p::Ref(PyObject_CallFunctionObjArgs(drain.get(), nullptr));
        // Dispatch can finish or replace the round; no old frame may advance on a new lifecycle.
        p::Ref running(PyObject_RichCompare(attr(battle, "state").get(), p::text("running").get(), Py_EQ));
        if (!truthy(running)) return running.release();
        p::Ref same_generation(PyObject_RichCompare(attr(battle, "_generation").get(), generation.get(), Py_EQ));
        if (!truthy(same_generation)) return same_generation.release();
        if (attr(battle, "_bots").get() != bots.get()) Py_RETURN_FALSE;
        return PyObject_RichCompare(round_id().get(), round.get(), Py_EQ);
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._remember_remote_track_turn(record, yaw, now): guest-side belt turn rate.
PyObject* rememberRemoteTrackTurn(PyObject*, PyObject* args) {
    PyObject *battle, *record, *yaw, *now, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &record, &yaw, &now, &g)) return nullptr;
    try {
        auto previous = get(record, "track_pose_sample");
        p::Ref turn = flt(0.0);
        if (previous.get() != Py_None) {
            const double elapsed_raw = PyFloat_AsDouble(asFloat(now).get()) -
                                       PyFloat_AsDouble(asFloat(p::Ref(PySequence_GetItem(previous.get(), 0)).get()).get());
            const double elapsed = elapsed_raw > 0.0 ? elapsed_raw : 0.0;
            if (elapsed > 0.0) {
                const double pi = 3.141592653589793;
                p::Ref shifted(PyFloat_FromDouble(PyFloat_AsDouble(asFloat(yaw).get()) -
                                                  PyFloat_AsDouble(asFloat(p::Ref(PySequence_GetItem(previous.get(), 1)).get()).get()) + pi));
                p::Ref wrapped(PyNumber_Remainder(shifted.get(), flt(2.0 * pi).get()));
                replace(turn, flt((PyFloat_AsDouble(wrapped.get()) - pi) / elapsed));
            }
        }
        p::Ref sample(PyTuple_Pack(2, asFloat(now).get(), asFloat(yaw).get()));
        if (PyObject_SetItem(record, p::text("track_pose_sample").get(), sample.get()) < 0) throw p::Error("sample");
        return turn.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._update_bot_tracks(record, state, now, turn_override=None): one Bot's belts and engine mode.
PyObject* updateBotTracks(PyObject*, PyObject* args) {
    PyObject *battle, *record, *state, *now_raw, *turn_override, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &record, &state, &now_raw, &turn_override, &g)) return nullptr;
    try {
        // Belt scroll and engine mode are presentation owned by the visible client.
        auto factory = attr(battle, "_remote_factory");
        if (truthy(attr(battle, "_worker_mode")) || factory.get() == Py_None) Py_RETURN_FALSE;
        auto vehicle = method(factory.get(), "get", p::Ref(PyObject_GetItem(record, p::text("engine_id").get())).get());
        if (vehicle.get() == Py_None) Py_RETURN_FALSE;
        if (truthy(get(record, "native_remote")) && !truthy(getattrOr(vehicle.get(), "_offlineNativeDrawVisible", Py_True)) &&
            !truthy(get(record, "_remote_track_pending")))
            Py_RETURN_TRUE;
        p::Ref now = p::retain(now_raw);
        if (turn_override != Py_None) {
            // Carry the 20 Hz deadline so every render rate averages the same feed cadence.
            replace(now, asFloat(now_raw));
            auto next_update = get(record, "_remote_track_next_update");
            const double current = PyFloat_AsDouble(now.get());
            if (next_update.get() != Py_None && current + 1.0e-9 < PyFloat_AsDouble(asFloat(next_update.get()).get())) Py_RETURN_FALSE;
            if (next_update.get() == Py_None) replace(next_update, p::retain(now.get()));
            const double base = PyFloat_AsDouble(asFloat(next_update.get()).get());
            const double elapsed = current - base > 0.0 ? current - base : 0.0;
            auto interval = global(g, "REMOTE_TRACK_PRESENTATION_SECONDS");
            auto periods = maxOf(integer(1).get(), p::Ref(PyNumber_Add(asInt(p::Ref(PyNumber_TrueDivide(flt(elapsed).get(), interval.get())).get()).get(),
                                                                         integer(1).get())).get());
            p::Ref deadline(PyNumber_Add(flt(base).get(), p::Ref(PyNumber_Multiply(periods.get(), interval.get())).get()));
            if (PyObject_SetItem(record, p::text("_remote_track_next_update").get(), deadline.get()) < 0) throw p::Error("deadline");
        }
        const bool alive = truthy(get(state, "alive", Py_True)) &&
                           test(asInt(truthy(get(state, "health", integer(1).get())) ? get(state, "health", integer(1).get()).get() : integer(0).get()).get(),
                                integer(0).get(), Py_GT);
        auto speed = flt(number(get(state, "speed").get()));
        auto turn = flt(number(turn_override == Py_None ? method(attr(battle, "_bot_yaw_rates").get(), "get", get(state, "id").get()).get()
                                                        : turn_override));
        auto alive_value = p::retain(alive ? Py_True : Py_False);
        auto mode = method(battle, "_bot_engine_mode", alive_value.get(), speed.get(), turn.get());
        auto params = method(battle, "_bot_track_params", record, vehicle.get());
        auto airborne = p::retain(truthy(get(state, "airborne", Py_False)) ? Py_True : Py_False);
        auto direction = flt(number(get(state, "movement_dir").get()));
        auto scroll = unpack(method(global(g, "vehicle_physics").get(), "track_scroll", params.get(), speed.get(), turn.get(),
                                    airborne.get(), direction.get()).get(), 2);
        auto limits = unpack(global(g, "TRACK_SCROLL_LIMITS").get(), 2);
        auto clamp = [&](PyObject* value) { return maxOf(limits[0].get(), minOf(limits[1].get(), value).get()); };
        auto updated = method(vehicle.get(), "update_tracks", clamp(scroll[0].get()).get(), clamp(scroll[1].get()).get(), mode.get());
        method(battle, "_report_bot_tracks", vehicle.get(), scroll[0].get(), scroll[1].get(), mode.get(), now.get());
        return p::retain(truthy(updated) ? Py_True : Py_False).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._send_bot_message(message): one outgoing authority message through the LAN client.
PyObject* sendBotMessage(PyObject*, PyObject* args) {
    PyObject *battle, *message, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &message, &bot_module, &g)) return nullptr;
    try {
        auto client = [&] { return attr(battle, "client"); };
        auto kind = get(message, "type");
        auto is = [&](const char* name) { return test(kind.get(), p::text(name).get(), Py_EQ); };
        auto call = [&](const char* name, std::vector<p::Ref> positional, PyObject* mapping) {
            auto fn = attr(client().get(), name);
            p::Ref tuple(PyTuple_New(Py_ssize_t(positional.size())));
            for (std::size_t i = 0; i < positional.size(); ++i) PyTuple_SET_ITEM(tuple.get(), Py_ssize_t(i), positional[i].release());
            return callWithMapping(fn.get(), tuple.get(), {}, mapping ? mapping : p::dict().get());
        };
        if (is("bot_manifest")) {
            auto profiles = get(message, "player_collision_profiles");
            std::vector<p::Ref> positional;
            positional.push_back(get(message, "bots"));
            if (profiles.get() != Py_None) positional.push_back(get(message, "player_collision_profiles"));
            return call("send_bot_manifest", std::move(positional), nullptr).release();
        }
        if (is("bot_state")) {
            auto worker = [&] { return truthy(attr(battle, "_worker_mode")); };
            auto armors = get(message, "human_ram_armors");
            if (armors.get() == Py_None && worker()) replace(armors, method(battle, "_human_ram_armor_results"));
            auto kwargs = p::dict();
            if (member(message, p::text("push_contacts").get()))
                put(kwargs.get(), "push_contacts", p::Ref(PyObject_GetItem(message, p::text("push_contacts").get())));
            if (worker()) {
                auto turrets = method(bot_module, "detached_rows", attr(battle, "_bots").get());
                if (truthy(turrets)) put(kwargs.get(), "detached_turrets", std::move(turrets));
            }
            if (member(message, p::text("sample_time_us").get())) {
                put(kwargs.get(), "sample_time_us", get(message, "sample_time_us"));
                put(kwargs.get(), "source_batch_horizon_us", get(message, "source_batch_horizon_us"));
            }
            if (armors.get() != Py_None) put(kwargs.get(), "human_ram_armors", std::move(armors));
            auto projected = getattrOr(client().get(), "send_projected_bot_state", Py_None);
            if (PyCallable_Check(projected.get())) {
                put(kwargs.get(), "edge_sample_time_us", get(message, "edge_sample_time_us"));
                put(kwargs.get(), "edge_revision", get(message, "edge_revision"));
                p::Ref positional(PyTuple_Pack(1, get(message, "rows").get()));
                return callWithMapping(projected.get(), positional.get(), {}, kwargs.get()).release();
            }
            std::vector<p::Ref> positional;
            positional.push_back(get(message, "rows"));
            return call("send_bot_state", std::move(positional), kwargs.get()).release();
        }
        if (is("bot_observation")) {
            auto fn = attr(client().get(), "send_bot_observation");
            p::Ref positional(PyTuple_Pack(2, get(message, "contacts").get(), get(message, "affordances").get()));
            auto radio = get(message, "radio_links");
            auto ranges = get(message, "player_vision_ranges");
            return callWithMapping(fn.get(), positional.get(), {{"radio_links", radio.get()}, {"player_vision_ranges", ranges.get()}},
                                   p::dict().get()).release();
        }
        if (is("bot_ram")) {
            auto kwargs = p::dict();
            if (member(message, p::text("contact_positions").get()))
                put(kwargs.get(), "contact_positions", p::Ref(PyObject_GetItem(message, p::text("contact_positions").get())));
            for (const char* key : {"track_contacts_bot", "track_contacts_target", "track_impact_speed"})
                if (member(message, p::text(key).get())) put(kwargs.get(), key, p::Ref(PyObject_GetItem(message, p::text(key).get())));
            std::vector<p::Ref> positional;
            for (const char* key : {"bot_id", "target_kind", "target_id", "ram_seq", "damage_to_bot", "damage_to_target",
                                    "ram_contact_player_id", "ram_contact_seq"})
                positional.push_back(get(message, key));
            return call("send_bot_ram", std::move(positional), kwargs.get()).release();
        }
        if (is("rules_state")) {
            auto rules = get(message, "rules");
            if (!truthy(rules)) replace(rules, p::dict());
            std::vector<p::Ref> positional;
            positional.push_back(get(rules.get(), "bases"));
            return call("send_rules_state", std::move(positional), nullptr).release();
        }
        if (is("battle_result")) {
            std::vector<p::Ref> positional;
            for (const char* key : {"winner", "reason", "base_team"}) positional.push_back(get(message, key));
            return call("send_battle_result", std::move(positional), nullptr).release();
        }
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._enqueue_bot_message(message): join one state enqueue with ordered launch progress.
PyObject* enqueueBotMessage(PyObject*, PyObject* args) {
    PyObject *battle, *message, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &message, &bot_module, &g)) return nullptr;
    try {
        auto accepted = dispatch(battle, "_send_bot_message", "send_bot_message", {message}, {bot_module, g});
        if (truthy(accepted)) method(battle, "_resolve_bot_fire", message);
        return accepted.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"record_worker_control_diagnostics", recordWorkerControlDiagnostics, METH_VARARGS, "Observe fixed-control and A* debt."},
    {"worker_diagnostic_totals", workerDiagnosticTotals, METH_VARARGS, "Bounded non-negative Bot diagnostic integers."},
    {"worker_control_snapshot", workerControlSnapshot, METH_VARARGS, "The worker's fixed-control snapshot."},
    {"drain_worker_transport", drainWorkerTransport, METH_VARARGS, "Admit queued echoes after the Bot update."},
    {"remember_remote_track_turn", rememberRemoteTrackTurn, METH_VARARGS, "Measure guest-side belt turning."},
    {"update_bot_tracks", updateBotTracks, METH_VARARGS, "Drive one Bot's belts and engine mode."},
    {"send_bot_message", sendBotMessage, METH_VARARGS, "Send one outgoing authority message."},
    {"enqueue_bot_message", enqueueBotMessage, METH_VARARGS, "Send one state message and resolve its launches."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* worker_steps_methods() { return worker_steps::methods; }
}
