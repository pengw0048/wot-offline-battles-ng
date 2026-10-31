// The battle render callback, ported from the pinned BattleRuntime._frame. Each step still
// calls its own runtime method until that method is native; module constants come from
// the runtime's globals().
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"
#include <vector>

namespace stock_native_client_lifecycle {
namespace {
using namespace py;

p::Ref frameGlobal(PyObject* g, const char* name) {
    PyObject* v = PyDict_GetItemString(g, name);
    if (!v) { PyErr_Format(PyExc_NameError, "global name '%s' is not defined", name); throw p::Error(name); }
    return p::retain(v);
}
p::Ref call0(PyObject* fn) { return p::Ref(PyObject_CallFunctionObjArgs(fn, nullptr)); }
double clockNow(PyObject* g) { return p::scalar(call0(frameGlobal(g, "_PROFILE_CLOCK").get()).get()); }
void increment(PyObject* o, const char* name) {
    setAttr(o, name, p::Ref(PyNumber_Add(attr(o, name).get(), integer(1).get())).get());
}
bool isCallable(const p::Ref& v) { return v.get() != Py_None && PyCallable_Check(v.get()); }
Py_ssize_t length(PyObject* v) {
    const Py_ssize_t n = PyObject_Size(v);
    if (n < 0) throw p::Error("len() failed");
    return n;
}
// self._run_optional_feature(name, fn, args[, on_error][, disable=...][, kwargs=...]) through its native port.
p::Ref feature(PyObject* battle, const char* name, PyObject* fn, std::initializer_list<PyObject*> args,
               PyObject* on_error = nullptr, PyObject* disable = nullptr, PyObject* kwargs = nullptr) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    auto label = p::text(name);
    std::vector<std::pair<const char*, PyObject*>> keywords;
    if (disable) keywords.push_back({"disable", disable});
    if (kwargs) keywords.push_back({"kwargs", kwargs});
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, "_run_optional_feature")) {
        auto fn_attr = attr(battle, "_run_optional_feature");
        p::Ref positional(on_error ? PyTuple_Pack(4, label.get(), fn, tuple.get(), on_error) : PyTuple_Pack(3, label.get(), fn, tuple.get()));
        auto kw = p::dict();
        for (const auto& item : keywords) put(kw.get(), item.first, p::retain(item.second));
        return p::Ref(PyObject_Call(fn_attr.get(), positional.get(), kw.get()));
    }
    return dispatch(battle, "_run_optional_feature", "run_optional_feature", {}, {},
                    {label.get(), fn, tuple.get(), on_error ? on_error : Py_None, disable ? disable : Py_True, kwargs ? kwargs : Py_None});
}
p::Ref feature(PyObject* battle, const char* name, PyObject* fn) {
    auto label = p::text(name);
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, "_run_optional_feature")) return callKw(battle, "_run_optional_feature", {label.get(), fn}, {});
    return dispatch(battle, "_run_optional_feature", "run_optional_feature", {}, {},
                    {label.get(), fn, p::Ref(PyTuple_New(0)).get(), Py_None, Py_True, Py_None});
}
// `try: body() except Exception: pass`
template<class Body> void swallow(Body body) {
    try { body(); } catch (const std::exception&) { PyErr_Clear(); }
}
// Sum probe count/duration deltas when both tuples are complete.
void addProbeDeltas(PyObject* kinds, PyObject* before, PyObject* after, PyObject* target, bool durations, bool guard) {
    const Py_ssize_t n = PySequence_Size(kinds);
    auto apply = [&]() {
        for (Py_ssize_t index = 0; index < n; ++index) {
            auto name = p::item(kinds, index);
            auto a = p::item(after, index), b = p::item(before, index);
            p::Ref current(PyObject_GetItem(target, name.get()));
            p::Ref next(durations
                ? PyNumber_Add(current.get(), p::number(std::max(0., p::scalar(p::Ref(PyNumber_Float(a.get())).get()) - p::scalar(p::Ref(PyNumber_Float(b.get())).get()))).get())
                : PyNumber_Add(current.get(), integer(std::max<std::int64_t>(0, strictInt(a.get()) - strictInt(b.get()))).get()));
            if (PyObject_SetItem(target, name.get(), next.get()) < 0) throw p::Error("probe total write failed");
        }
    };
    if (!guard) { apply(); return; }
    try { apply(); }
    catch (const std::exception&) {
        if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError) &&
            !PyErr_ExceptionMatches(PyExc_OverflowError)) throw;
        PyErr_Clear();
        for (Py_ssize_t index = 0; index < n; ++index)
            if (PyObject_SetItem(target, p::item(kinds, index).get(), durations ? p::number(0.).get() : integer(0).get()) < 0)
                throw p::Error("probe total reset failed");
    }
}
bool completeSequence(PyObject* v, Py_ssize_t n) { return (PyList_Check(v) || PyTuple_Check(v)) && PySequence_Size(v) == n; }
p::Ref zeros(PyObject* kinds, bool durations) {
    auto out = p::dict();
    p::Ref iterator(PyObject_GetIter(kinds));
    while (PyObject* raw = PyIter_Next(iterator.get())) {
        p::Ref name(raw);
        if (PyObject_SetItem(out.get(), name.get(), durations ? p::number(0.).get() : integer(0).get()) < 0) throw p::Error("probe zero failed");
    }
    if (PyErr_Occurred()) throw p::Error("probe kinds iteration failed");
    return out;
}
p::Ref tryCall(const p::Ref& fn) {
    PyObject* v = PyObject_CallFunctionObjArgs(fn.get(), nullptr);
    if (!v) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}

PyObject* frame(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        if (!equal(attr(battle, "state").get(), p::text("running").get())) Py_RETURN_NONE;
        const bool worker = truthy(attr(battle, "_worker_mode"));
        if (worker) increment(battle, "_worker_frame_callbacks");
        auto diagnostics = attr(battle, "_frame_diagnostics");
        const bool profiling = diagnostics.get() != Py_None && truthy(attr(diagnostics.get(), "enabled"));
        const double entry_wall = profiling ? clockNow(g) : 0.;
        auto now = method(battle, "_clock");
        auto frame_start = attr(battle, "_last_frame_time");
        auto raw_dt = frame_start.get() == Py_None ? p::number(0.) : p::Ref(PyNumber_Subtract(now.get(), frame_start.get()));
        auto rule_dt = p::number(std::max(0., p::scalar(raw_dt.get())));
        if (worker) {
            auto recorder = optionalAttr(attr(battle, "client").get(), "record_frame_interval");
            if (isCallable(recorder)) p::Ref unused(PyObject_CallFunctionObjArgs(recorder.get(), rule_dt.get(), nullptr));
        }
        if ((worker || attr(battle, "_worker_probe").get() != Py_None) && p::scalar(raw_dt.get()) > 0.1000001)
            increment(battle, "_worker_probe_simulation_caps");
        auto dt = p::retain(rule_dt.get());
        auto tick_dt = p::retain(rule_dt.get());
        setAttr(battle, "_last_frame_time", now.get());
        if (PyObject_SetItem(attr(battle, "_soft_static_recast_budget").get(), integer(0).get(), frameGlobal(g, "BOT_SOFT_RECAST_BUDGET").get()) < 0)
            throw p::Error("recast budget write failed");
        auto offframe = attr(battle, "_offframe_seconds");
        setAttr(battle, "_offframe_seconds", p::number(0.).get());
        setAttr(battle, "_effect_reports", integer(0).get());
        for (const char* name : {"_projectile_interface_budget", "_projectile_interface_capped_round", "_spotted_signature"})
            setAttr(battle, name, Py_None);
        p::Ref lan_poll(p::retain(Py_None));
        if (profiling) {
            PyObject* getter = PyObject_GetAttrString(attr(battle, "client").get(), "transport_performance_snapshot");
            if (getter) replace(lan_poll, p::Ref(getter));
            else PyErr_Clear();
        }
        auto frame_id = profiling ? callKw(diagnostics.get(), "begin", {p::number(entry_wall).get(), raw_dt.get(), offframe.get()},
                                           {{"lan_poll_getter", lan_poll.get()}})
                                  : integer(0);
        auto combat = attr(battle, "_combat_diagnostics");
        const bool live = truthy(attr(battle, "_battle_live"));
        if (combat.get() != Py_None && live && profiling) {
            const char* trigger;
            auto projectiles = attr(battle, "_projectiles");
            auto bots = attr(battle, "_bots");
            if (projectiles.get() != Py_None && length(projectiles.get()) > 0) trigger = "projectiles";
            else if (bots.get() != Py_None && truthy(optionalAttr(bots.get(), "_shot_lane_pending_pairs"))) trigger = "lane_queue";
            else if (p::scalar(raw_dt.get()) > 0.1) trigger = "slow_frame";
            else trigger = "profile_window";
            method(combat.get(), "begin_frame", frame_id.get(), now.get(), p::text(trigger).get());
            if (!worker) method(combat.get(), "begin_control");
            method(diagnostics.get(), "note_combat_captures", method(combat.get(), "drain_completed").get());
        }
        auto stages = p::dict();
        auto kinds = frameGlobal(g, "PROBE_KINDS");
        auto probes = zeros(kinds.get(), false), probe_durations = zeros(kinds.get(), true);
        p::Ref pose_before(PySequence_Tuple(attr(battle, "_local_position").get()));
        bool transitioned = false;
        p::Ref outgoing(PyTuple_New(0));
        auto bot_count = integer(0);
        auto projectile_perf = p::dict();
        double boundary = entry_wall;
        auto stage = [&](const char* name) {
            if (!profiling) return;
            const double next = clockNow(g);
            put(stages.get(), name, p::number(std::max(0., next - boundary)));
            boundary = next;
        };
        const bool replay = [&] { return truthy(attr(battle, "_replay_mode")); }();
        p::Ref offline_replay(p::retain(Py_None));
        try {
            feature(battle, "map visibility filtering", attr(battle, "_maintain_standard_space_visibility").get(), {now.get()},
                    attr(battle, "_disable_standard_space_visibility").get());
            dispatch(battle, "_flush_pending_bot_create", "flush_bot_create", {now.get()});
            method(battle, "_flush_pending_entities", now.get());
            dispatch(battle, "_start_team_chat_when_roster_ready", "start_team_chat", {});
            dispatch(battle, "_drain_event_journal", "drain_event_journal", {}, {g});
            feature(battle, "foliage camouflage", attr(battle, "_refresh_fallen_tree_foliage").get(), {now.get()});
            dispatch(battle, "_retry_bot_manifest", "retry_bot_manifest", {now.get()});
            dispatch(battle, "_maybe_send_battle_ready", "maybe_send_battle_ready", {});
            replace(offline_replay, p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.offline_replay")));
            method(offline_replay.get(), "ensure_recording", battle);
            stage("house");
            auto sync = attr(battle, "_sync");
            if (sync.get() != Py_None) {
                method(sync.get(), "advance", now.get());
                if (!worker && truthy(attr(battle, "_battle_live")))
                    feature(battle, "Bot motion diagnostics", attr(battle, "_log_bot_motion_probes").get(), {now.get()});
            }
            if (truthy(attr(battle, "_battle_live")) && worker) method(battle, "_publish_player_environment", rule_dt.get(), now.get());
            stage("sync");
            if (truthy(attr(battle, "_battle_live")) && !worker && !replay) method(battle, "_tick_critical_states", rule_dt.get());
            if (!worker && !replay)
                feature(battle, "Expert damaged-device presentation", attr(battle, "_tick_expert_target").get(), {now.get()},
                        attr(battle, "_disable_expert_presentation").get());
            feature(battle, "detached turret lifecycle", attr(battle, "_advance_detached_turrets").get(), {now.get()}, nullptr, Py_False);
            stage("critical");
            if (truthy(attr(battle, "_battle_live")) && !worker && !replay) {
                method(battle, "_tick_drowning", rule_dt.get(), now.get());
                method(battle, "_tick_overturn", rule_dt.get(), now.get());
            }
            stage("drown");
            auto bots = attr(battle, "_bots");
            if (!truthy(attr(battle, "_battle_live")) && attr(battle, "_prebattle_deadline").get() != Py_None && bots.get() != Py_None) {
                auto prewarm = optionalAttr(bots.get(), "prewarm_world_receipts");
                if (isCallable(prewarm)) {
                    auto totals = optionalAttr(bots.get(), "probe_totals"), duration_totals = optionalAttr(bots.get(), "probe_duration_totals");
                    swallow([&] {
                        p::Ref before(p::retain(Py_None)), before_durations(p::retain(Py_None));
                        if (profiling && isCallable(totals)) replace(before, call0(totals.get()));
                        if (profiling && isCallable(duration_totals)) replace(before_durations, call0(duration_totals.get()));
                        p::Ref unused(PyObject_CallFunctionObjArgs(prewarm.get(), now.get(), nullptr));
                        const Py_ssize_t n = PySequence_Size(kinds.get());
                        if (profiling && before.get() != Py_None) {
                            auto after = call0(totals.get());
                            if (length(before.get()) >= n && length(after.get()) >= n)
                                addProbeDeltas(kinds.get(), before.get(), after.get(), probes.get(), false, false);
                        }
                        if (profiling && before_durations.get() != Py_None) {
                            auto after = call0(duration_totals.get());
                            if (length(before_durations.get()) >= n && length(after.get()) >= n)
                                addProbeDeltas(kinds.get(), before_durations.get(), after.get(), probe_durations.get(), true, false);
                        }
                    });
                }
            }
            if (attr(battle, "_destructibles").get() != Py_None &&
                ((!truthy(attr(battle, "_battle_live")) && attr(battle, "_prebattle_deadline").get() != Py_None) ||
                 (worker && truthy(attr(battle, "_battle_live")))))
                swallow([&] { method(battle, "_prewarm_player_tree_registries", now.get()); });
            auto warn = [&](const char* what, const std::exception& e) {
                PyObject *type, *value, *trace;
                PyErr_Fetch(&type, &value, &trace);
                if (!type) { value = PyObject_CallFunction(PyExc_RuntimeError, const_cast<char*>("s"), e.what()); }
                else { PyErr_NormalizeException(&type, &value, &trace); Py_XDECREF(type); Py_XDECREF(trace); }
                p::Ref error(value);
                method(battle, "_warn_optional_failure", p::text(what).get(), error.get());
            };
            try { method(battle, "_prewarm_bot_destructible_registries", now.get()); } catch (const std::exception& e) { warn("bot destructible prewarm", e); }
            try { dispatch(battle, "_prewarm_critical_layout", "prewarm_critical_layout", {}, {g}); } catch (const std::exception& e) { warn("critical layout prewarm", e); }
            stage("prewarm");
            if (!truthy(attr(battle, "_battle_live")) && truthy(method(battle, "_projectile_is_authority")) &&
                attr(battle, "_projectiles").get() != Py_None) {
                auto projectile = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.native_client_projectile"));
                if (truthy(method(projectile.get(), "enabled", battle))) method(projectile.get(), "frame_sample", battle, now.get());
                else method(battle, "_sample_projectile_positions", now.get(), method(battle, "_projectile_record_poses").get());
            }
            if (!truthy(attr(battle, "_battle_live")) && truthy(method(battle, "_prebattle_transition_ready", now.get()))) {
                const double live_edge = number(attr(battle, "_prebattle_deadline").get());
                method(battle, "_begin_battle");
                const double interval_start = frame_start.get() == Py_None ? number(now.get()) : number(frame_start.get());
                const double live_dt = std::max(0., number(now.get()) - std::max(interval_start, live_edge));
                replace(dt, p::number(live_dt));
                replace(rule_dt, p::number(live_dt));
                transitioned = true;
                if (worker) method(battle, "_publish_player_environment", rule_dt.get(), now.get());
                else if (!replay) {
                    method(battle, "_tick_critical_states", rule_dt.get());
                    method(battle, "_tick_drowning", rule_dt.get(), now.get());
                    method(battle, "_tick_overturn", rule_dt.get(), now.get());
                }
            }
            stage("transition");
            if (truthy(attr(battle, "_battle_live")) && !worker && !replay) {
                setAttr(battle, "_local_frame_stages", profiling ? stages.get() : Py_None);
                auto reset = [&] { setAttr(battle, "_local_frame_stages", Py_None); };
                try {
                    method(battle, "_drive_local", dt.get());
                    method(battle, "_advance_local_player_burst");
                } catch (...) {
                    PyObject *type, *value, *trace;
                    PyErr_Fetch(&type, &value, &trace);
                    reset();
                    PyErr_Restore(type, value, trace);
                    throw;
                }
                reset();
            }
            stage("local");
            if (!worker && !replay) feature(battle, "live reload sound pump", attr(battle, "_pump_live_reload_sound").get(), {now.get()});
            if (truthy(attr(battle, "_battle_live")) && !worker && !replay) {
                feature(battle, "target outline", attr(battle, "_update_target_outline").get(), {now.get()},
                        attr(battle, "_disable_target_outline_presentation").get());
                feature(battle, "compound diagnostics", attr(battle, "_report_local_compound").get(), {now.get()});
            }
            stage("outline");
            replace(bots, attr(battle, "_bots"));
            if (truthy(attr(battle, "_battle_live")) && bots.get() != Py_None && !replay) {
                if (worker) increment(battle, "_worker_probe_authority_callbacks");
                method(battle, "_advance_artillery_arcs", now.get());
                auto players = method(battle, "_authority_players");
                if (worker) method(battle, "_resolve_player_destructible_contacts", players.get(), now.get());
                auto totals = optionalAttr(bots.get(), "probe_totals"), duration_totals = optionalAttr(bots.get(), "probe_duration_totals");
                p::Ref before(p::retain(Py_None)), before_durations(p::retain(Py_None));
                if (profiling && isCallable(totals)) replace(before, tryCall(totals));
                if (profiling && isCallable(duration_totals)) replace(before_durations, tryCall(duration_totals));
                auto set_camera = optionalAttr(bots.get(), "set_camera_position");
                if (isCallable(set_camera))
                    p::Ref unused(PyObject_CallFunctionObjArgs(set_camera.get(), worker ? Py_None : attr(battle, "_local_position").get(), nullptr));
                if (worker) {
                    auto last = attr(battle, "_last_snapshot");
                    method(bots.get(), "set_planning_snapshot", truthy(last) ? last.get() : p::dict().get(), now.get(), attr(battle, "_generation").get());
                }
                auto control_before = optionalAttr(bots.get(), "_sample_time_us");
                auto sensor = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.destructibles_sensor"));
                auto scope = method(sensor.get(), "collision_preparation_scope_1513");
                auto exit = attr(scope.get(), "__exit__");
                method(scope.get(), "__enter__");
                try {
                    replace(outgoing, callKw(bots.get(), "update", {rule_dt.get(), now.get()}, {{"players", players.get()}}));
                } catch (...) {
                    PyObject *type, *value, *trace;
                    PyErr_Fetch(&type, &value, &trace);
                    PyErr_NormalizeException(&type, &value, &trace);
                    PyObject* suppressed = PyObject_CallFunctionObjArgs(exit.get(), type ? type : Py_None, value ? value : Py_None,
                                                                       trace ? trace : Py_None, nullptr);
                    if (!suppressed) { Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace); throw p::Error("scope exit failed"); }
                    const bool swallowed = PyObject_IsTrue(suppressed) > 0;
                    Py_DECREF(suppressed);
                    if (!swallowed) { PyErr_Restore(type, value, trace); throw; }
                    Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
                    replace(outgoing, p::Ref(PyTuple_New(0)));
                }
                if (PyErr_Occurred() == nullptr) p::Ref unused(PyObject_CallFunctionObjArgs(exit.get(), Py_None, Py_None, Py_None, nullptr));
                if (worker) method(battle, "_record_worker_control_diagnostics", control_before.get());
                p::Ref after(p::retain(Py_None)), after_durations(p::retain(Py_None));
                if (profiling && isCallable(totals)) replace(after, tryCall(totals));
                if (profiling && isCallable(duration_totals)) replace(after_durations, tryCall(duration_totals));
                const Py_ssize_t n = PySequence_Size(kinds.get());
                if (completeSequence(before.get(), n) && completeSequence(after.get(), n))
                    addProbeDeltas(kinds.get(), before.get(), after.get(), probes.get(), false, true);
                if (completeSequence(before_durations.get(), n) && completeSequence(after_durations.get(), n))
                    addProbeDeltas(kinds.get(), before_durations.get(), after_durations.get(), probe_durations.get(), true, true);
            }
            stage("bots_update");
            replace(bots, attr(battle, "_bots"));
            if (truthy(attr(battle, "_battle_live")) && bots.get() != Py_None && !replay) {
                auto states = optionalAttr(bots.get(), "presentation_states");
                if (!isCallable(states)) throw p::Error("authority bot presentation boundary is unavailable");
                auto presented = worker ? method(battle, "_present_authority_bot_poses", states.get(), now.get())
                                        : feature(battle, "bot pose presentation", attr(battle, "_present_authority_bot_poses").get(),
                                                  {states.get(), now.get()}, nullptr, Py_False);
                if (truthy(presented)) replace(bot_count, p::item(presented.get(), 0));
            }
            stage("bot_present");
            replace(bots, attr(battle, "_bots"));
            if (truthy(attr(battle, "_battle_live")) && bots.get() != Py_None && !replay) {
                p::Ref iterator(PyObject_GetIter(outgoing.get()));
                while (PyObject* raw = PyIter_Next(iterator.get())) {
                    p::Ref message(raw);
                    const bool is_state = equal(method(message.get(), "get", p::text("type").get()).get(), p::text("bot_state").get());
                    if (is_state) increment(battle, "_worker_probe_bot_generated");
                    const bool accepted = truthy(method(battle, "_enqueue_bot_message", message.get()));
                    if (is_state) increment(battle, accepted ? "_worker_probe_bot_enqueued" : "_worker_probe_bot_send_failed");
                }
                if (PyErr_Occurred()) throw p::Error("outgoing iteration failed");
            }
            if (worker && truthy(attr(battle, "_battle_live")) && !truthy(method(battle, "_drain_worker_transport"))) Py_RETURN_NONE;
            if (truthy(attr(battle, "_battle_live")) &&
                (truthy(method(battle, "_projectile_is_authority")) || truthy(attr(battle, "_projectile_visual_meta")))) {
                method(battle, "_advance_projectiles", now.get());
                replace(projectile_perf, p::Ref(PyDict_Copy(attr(battle, "_projectile_perf").get())));
            }
            stage("bot_events");
            if (!worker && !replay) {
                if (truthy(attr(battle, "_battle_live")))
                    feature(battle, "spotting", attr(battle, "_update_spotting").get(), {now.get()}, nullptr, Py_False);
                else if (attr(battle, "_prebattle_deadline").get() != Py_None) {
                    auto kwargs = p::dict(); put(kwargs.get(), "hud_only", p::boolean(true));
                    feature(battle, "spotting", attr(battle, "_update_spotting").get(), {now.get()}, nullptr, Py_False, kwargs.get());
                }
            }
            stage("spot");
            if (truthy(attr(battle, "_battle_live")) && !worker && !replay) {
                auto validate = optionalAttr(attr(attr(battle, "_runtime").get(), "compatibility").get(), "validate_target_lock");
                if (!isCallable(validate)) throw p::Error("#1513 target-lock lifecycle boundary is unavailable");
                feature(battle, "target lock validation", validate.get(), {attr(battle, "_avatar").get()}, nullptr, Py_False);
            }
            if (replay) method(battle, "_present_replay_local", dt.get());
            else method(offline_replay.get(), "record_local", battle);
            setAttr(battle, "_worker_probe_bot_count", bot_count.get());
            feature(battle, "authority worker probe", attr(battle, "_advance_authority_worker_probe").get());
            stage("lock");
        } catch (const std::exception& e) {
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            if (!type) value = PyObject_CallFunction(PyExc_RuntimeError, const_cast<char*>("s"), e.what());
            else { PyErr_NormalizeException(&type, &value, &trace); Py_XDECREF(type); Py_XDECREF(trace); }
            p::Ref error(value);
            method(battle, "_fail", error.get());
            Py_RETURN_NONE;
        }
        const double schedule_start = profiling ? clockNow(g) : 0.;
        dispatch(battle, "_schedule", "schedule", {frameGlobal(g, "FRAME_SECONDS").get(), attr(battle, "_frame").get()}, {Py_False, g});
        if (!profiling) Py_RETURN_NONE;
        const double schedule_end = clockNow(g);
        put(stages.get(), "schedule", p::number(std::max(0., schedule_end - schedule_start)));
        auto velocity = p::Ref(PyObject_CallFunctionObjArgs(frameGlobal(g, "_xyz").get(), attr(battle, "_local_camera_velocity").get(), nullptr));
        double square = 0.;
        for (Py_ssize_t i = 0; i < 3; ++i) { const double v = p::scalar(p::item(velocity.get(), i).get()); square += v * v; }
        auto camera_speed = p::number(std::sqrt(square));
        auto pose_step = p::Ref(PyObject_CallFunctionObjArgs(frameGlobal(g, "_distance_2d").get(), pose_before.get(), attr(battle, "_local_position").get(), nullptr));
        auto bots = attr(battle, "_bots");
        bool authority = false;
        auto is_authority = optionalAttr(bots.get(), "is_authority");
        if (isCallable(is_authority)) {
            PyObject* v = PyObject_CallFunctionObjArgs(is_authority.get(), nullptr);
            if (v) { authority = PyObject_IsTrue(v) > 0; Py_DECREF(v); } else PyErr_Clear();
        }
        p::Ref probe_timing(p::text("off"));
        auto timing_state = optionalAttr(bots.get(), "probe_timing_state");
        if (isCallable(timing_state)) {
            PyObject* v = PyObject_CallFunctionObjArgs(timing_state.get(), nullptr);
            if (v) { replace(probe_timing, p::Ref(PyObject_Str(v))); Py_DECREF(v); } else { PyErr_Clear(); replace(probe_timing, p::text("failed")); }
        }
        auto emit_due = optionalAttr(diagnostics.get(), "emit_due");
        if (isCallable(emit_due) && truthy(call0(emit_due.get()))) {
            auto load = optionalAttr(bots.get(), "load_report");
            if (isCallable(load)) swallow([&] { method(diagnostics.get(), "note_bot_load", call0(load.get()).get()); });
            swallow([&] { method(diagnostics.get(), "note_collections", method(battle, "_collection_counts").get()); });
            auto note_worker = optionalAttr(diagnostics.get(), "note_worker_runtime");
            if (worker && isCallable(note_worker))
                swallow([&] {
                    auto report = literal(4);
                    put(report.get(), "control", method(battle, "_worker_control_snapshot"));
                    put(report.get(), "combat_checkpoint", combat.get() != Py_None ? method(combat.get(), "checkpoint") : p::retain(Py_None));
                    put(report.get(), "bot_diagnostics", method(battle, "_worker_diagnostic_totals"));
                    auto presentation = literal(4);
                    for (const char* name : {"pose_writes", "pose_skips", "aim_writes", "aim_skips"})
                        put(presentation.get(), name, attr(battle, (std::string("_authority_") + name).c_str()));
                    put(report.get(), "presentation", presentation);
                    p::Ref unused(PyObject_CallFunctionObjArgs(note_worker.get(), report.get(), nullptr));
                });
        }
        auto start = attr(battle, "_start_message");
        auto config = attr(battle, "_config");
        auto context = literal(14);
        put(context.get(), "round", method(truthy(start) ? start.get() : p::dict().get(), "get", p::text("round_id").get(), p::text("-").get()));
        put(context.get(), "authority_time", p::retain(now.get()));
        put(context.get(), "map", method(truthy(config) ? config.get() : p::dict().get(), "get", p::text("map").get(), p::text("-").get()));
        put(context.get(), "phase", p::text(truthy(attr(battle, "_battle_live")) ? "live" : "prebattle"));
        put(context.get(), "role", p::text(worker ? "worker" : authority ? "authority" : "guest"));
        put(context.get(), "probe_timing", p::retain(probe_timing.get()));
        put(context.get(), "bot_count", p::retain(bot_count.get()));
        put(context.get(), "outgoing_count", integer(length(outgoing.get())));
        put(context.get(), "pose_step", p::retain(pose_step.get()));
        put(context.get(), "speed", p::number(number(attr(battle, "_local_speed").get())));
        put(context.get(), "camera_speed", p::retain(camera_speed.get()));
        put(context.get(), "airborne", p::boolean(truthy(attr(battle, "_local_airborne"))));
        put(context.get(), "grind", integer(strictInt(attr(battle, "_local_grind").get())));
        put(context.get(), "transitioned", p::boolean(transitioned));
        callKw(diagnostics.get(), "finish", {frame_id.get(), p::number(entry_wall).get(), tick_dt.get(), dt.get(), stages.get(), probes.get(), context.get()},
               {{"probe_durations", probe_durations.get()}, {"projectile", projectile_perf.get()},
                {"combat", combat.get() != Py_None ? method(combat.get(), "finish_frame").get() : Py_None}});
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}


// BattleRuntime._optional_feature_enabled(feature)
bool featureEnabled(PyObject* battle, PyObject* name) {
    PyObject* raw = PyObject_GetAttrString(battle, "_disabled_optional_features");
    if (!raw) { if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("feature lookup failed"); PyErr_Clear(); raw = (Py_INCREF(Py_None), Py_None); }
    p::Ref disabled(raw);
    if (disabled.get() == Py_None) { replace(disabled, p::Ref(PySet_New(nullptr))); setAttr(battle, "_disabled_optional_features", disabled.get()); }
    p::Ref text(PyObject_Str(name));
    const int found = PySequence_Contains(disabled.get(), text.get());
    if (found < 0) throw p::Error("feature membership failed");
    return found == 0;
}
PyObject* optionalFeatureEnabled(PyObject*, PyObject* args) {
    PyObject *battle, *name;
    if (!PyArg_ParseTuple(args, "OO", &battle, &name)) return nullptr;
    try { return p::boolean(featureEnabled(battle, name)).release(); } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._run_optional_feature(feature, callback, args=(), on_error=None, disable=True, kwargs=None)
PyObject* runOptionalFeature(PyObject*, PyObject* args) {
    PyObject *battle, *name, *callback, *call_args, *on_error, *disable, *kwargs;
    if (!PyArg_ParseTuple(args, "OOOOOOO", &battle, &name, &callback, &call_args, &on_error, &disable, &kwargs)) return nullptr;
    try {
        if (!truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {name}))) Py_RETURN_FALSE;
        p::Ref positional(PySequence_Tuple(call_args));
        p::Ref keywords(PyObject_IsTrue(kwargs) > 0 ? PyDict_Copy(kwargs) : PyDict_New());
        PyObject* result = PyObject_Call(callback, positional.get(), keywords.get());
        if (result) return result;
        if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        PyErr_NormalizeException(&type, &value, &trace);
        Py_XDECREF(type); Py_XDECREF(trace);
        p::Ref error(value);
        if (on_error != Py_None && PyCallable_Check(on_error)) {
            PyObject* cleaned = PyObject_CallFunctionObjArgs(on_error, nullptr);
            if (cleaned) Py_DECREF(cleaned);
            else {
                if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
                PyObject *ctype, *cvalue, *ctrace;
                PyErr_Fetch(&ctype, &cvalue, &ctrace);
                PyErr_NormalizeException(&ctype, &cvalue, &ctrace);
                Py_XDECREF(ctype); Py_XDECREF(ctrace);
                p::Ref cleanup(cvalue);
                p::Ref message(PyString_FromFormat("%s; disable cleanup failed: %s", PyString_AsString(p::Ref(PyObject_Str(error.get())).get()),
                                                   PyString_AsString(p::Ref(PyObject_Str(cleanup.get())).get())));
                replace(error, p::Ref(PyObject_CallFunctionObjArgs(PyExc_RuntimeError, message.get(), nullptr)));
            }
        }
        callKw(battle, "_warn_optional_failure", {name, error.get()}, {{"disable", disable}});
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

// The callable BattleRuntime._schedule hands to BigWorld.callback (its pinned `invoke` closure).
struct Scheduled {
    PyObject_HEAD
    PyObject *battle, *function, *generation, *token, *clock;
    bool ammo, measured;
};
void scheduledDealloc(PyObject* self) {
    auto* s = reinterpret_cast<Scheduled*>(self);
    Py_XDECREF(s->battle); Py_XDECREF(s->function); Py_XDECREF(s->generation); Py_XDECREF(s->token); Py_XDECREF(s->clock);
    PyObject_Del(self);
}
PyObject* scheduledCall(PyObject* self, PyObject*, PyObject*) {
    auto* s = reinterpret_cast<Scheduled*>(self);
    try {
        PyObject* battle = s->battle;
        const char* token_name = s->ammo ? "_ammo_callback_token" : "_callback_token";
        const char* id_name = s->ammo ? "_ammo_callback_id" : "_callback_id";
        if (attr(battle, token_name).get() == s->token) { setAttr(battle, token_name, Py_None); setAttr(battle, id_name, Py_None); }
        if (!equal(s->generation, attr(battle, "_generation").get())) Py_RETURN_NONE;
        if (!s->measured) { p::Ref unused(PyObject_CallFunctionObjArgs(s->function, nullptr)); Py_RETURN_NONE; }
        p::Ref started(PyObject_CallFunctionObjArgs(s->clock, nullptr));
        auto settle = [&] {
            p::Ref ended(PyObject_CallFunctionObjArgs(s->clock, nullptr));
            p::Ref spent(PyNumber_Subtract(ended.get(), started.get()));
            setAttr(battle, "_offframe_seconds", p::Ref(PyNumber_Add(attr(battle, "_offframe_seconds").get(), spent.get())).get());
        };
        PyObject* result = PyObject_CallFunctionObjArgs(s->function, nullptr);
        if (!result) {
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            try { settle(); } catch (...) { Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace); throw; }
            PyErr_Restore(type, value, trace);
            return nullptr;
        }
        Py_DECREF(result);
        settle();
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}
PyTypeObject ScheduledType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_client_lifecycle_native.Scheduled"};

// BattleRuntime._schedule(delay, function, ammo=False)
PyObject* schedule(PyObject*, PyObject* args) {
    PyObject *battle, *delay, *function, *ammo_raw, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &delay, &function, &ammo_raw, &g)) return nullptr;
    try {
        const bool ammo = p::flag(ammo_raw);
        auto generation = attr(battle, "_generation");
        auto bigworld = attr(attr(battle, "_runtime").get(), "bigworld");
        if (ammo && attr(battle, "_ammo_callback_id").get() != Py_None) {
            PyObject* cancelled = PyObject_CallMethod(bigworld.get(), const_cast<char*>("cancelCallback"), const_cast<char*>("O"),
                                                      attr(battle, "_ammo_callback_id").get());
            if (cancelled) Py_DECREF(cancelled);
            else if (PyErr_ExceptionMatches(PyExc_Exception)) PyErr_Clear();
            else return nullptr;
            setAttr(battle, "_ammo_callback_id", Py_None);
            setAttr(battle, "_ammo_callback_token", Py_None);
        }
        p::Ref token(PyObject_CallObject(reinterpret_cast<PyObject*>(&PyBaseObject_Type), nullptr));
        setAttr(battle, ammo ? "_ammo_callback_token" : "_callback_token", token.get());
        auto underlying = frameGlobal(g, "_underlying_function");
        p::Ref target(PyObject_CallFunctionObjArgs(underlying.get(), function, nullptr));
        p::Ref frame_target(PyObject_CallFunctionObjArgs(underlying.get(), attr(battle, "_frame").get(), nullptr));
        if (!ScheduledType.tp_call) {
            ScheduledType.tp_basicsize = sizeof(Scheduled);
            ScheduledType.tp_dealloc = scheduledDealloc;
            ScheduledType.tp_call = scheduledCall;
            ScheduledType.tp_flags = Py_TPFLAGS_DEFAULT;
            if (PyType_Ready(&ScheduledType) < 0) throw p::Error("scheduled callback type failed");
        }
        auto* invoke = PyObject_New(Scheduled, &ScheduledType);
        if (!invoke) throw p::Error("scheduled callback allocation failed");
        Py_INCREF(battle); Py_INCREF(function);
        invoke->battle = battle; invoke->function = function;
        invoke->generation = generation.release(); invoke->token = p::retain(token.get()).release();
        invoke->clock = frameGlobal(g, "_PROFILE_CLOCK").release();
        invoke->ammo = ammo;
        invoke->measured = target.get() != frame_target.get();
        p::Ref callable(reinterpret_cast<PyObject*>(invoke));
        PyObject* id = PyObject_CallMethod(bigworld.get(), const_cast<char*>("callback"), const_cast<char*>("OO"), delay, callable.get());
        const char* token_name = ammo ? "_ammo_callback_token" : "_callback_token";
        if (!id) {
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            if (attr(battle, token_name).get() == token.get()) setAttr(battle, token_name, Py_None);
            PyErr_Restore(type, value, trace);
            return nullptr;
        }
        p::Ref callback_id(id);
        if (attr(battle, token_name).get() == token.get()) setAttr(battle, ammo ? "_ammo_callback_id" : "_callback_id", callback_id.get());
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}


// Python's `except X as error`: the bound exception, or rethrow when it does not match.
p::Ref catchMatching(std::initializer_list<PyObject*> types) {
    if (!PyErr_Occurred()) throw;
    bool matches = false;
    for (PyObject* type : types) matches = matches || PyErr_ExceptionMatches(type);
    if (!matches) throw;
    PyObject *type, *value, *trace;
    PyErr_Fetch(&type, &value, &trace);
    PyErr_NormalizeException(&type, &value, &trace);
    Py_XDECREF(type); Py_XDECREF(trace);
    return p::Ref(value);
}

// BattleRuntime._maintain_standard_space_visibility(now)
PyObject* maintainSpaceVisibility(PyObject*, PyObject* args) {
    PyObject *battle, *now, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &now, &g)) return nullptr;
    try {
        auto boundary = attr(battle, "_standard_space_visibility");
        if (boundary.get() == Py_None || PyObject_RichCompareBool(now, attr(battle, "_next_space_visibility_check").get(), Py_LT) == 1)
            Py_RETURN_FALSE;
        setAttr(battle, "_next_space_visibility_check",
                p::Ref(PyNumber_Add(now, frameGlobal(g, "SPACE_VISIBILITY_CHECK_SECONDS").get())).get());
        if (p::size(boundary.get()) != 4) throw p::Error("boundary unpack failed");
        auto space_id = p::item(boundary.get(), 0), selected = p::item(boundary.get(), 1);
        auto client_bits = p::item(boundary.get(), 2), server_bits = p::item(boundary.get(), 3);
        try {
            auto space = method(battle, "_live_standard_space", space_id.get());
            auto current = attr(space.get(), "itemsVisibilityMask");
            if (equal(p::Ref(PyNumber_And(current.get(), server_bits.get())).get(), selected.get())) Py_RETURN_FALSE;
            p::Ref corrected(PyNumber_Or(p::Ref(PyNumber_And(current.get(), client_bits.get())).get(), selected.get()));
            setAttr(space.get(), "itemsVisibilityMask", corrected.get());
            auto actual = attr(space.get(), "itemsVisibilityMask");
            if (!equal(actual.get(), corrected.get())) {
                p::Ref text(PyString_Format(p::Ref(PyString_FromString("#1513 typed gameplay visibility mask was not applied: expected=0x%x actual=%r")).get(),
                                            p::Ref(PyTuple_Pack(2, corrected.get(), actual.get())).get()));
                throw p::Error(p::string(text.get()));
            }
        } catch (const std::exception& e) {
            PyObject* pending = frameGlobal(g, "_LiveSpaceVisibilityPending").release();
            p::Ref held(pending);
            if (PyErr_Occurred() && PyErr_ExceptionMatches(pending)) { PyErr_Clear(); Py_RETURN_FALSE; }
            p::Ref error(p::retain(Py_None));
            if (PyErr_Occurred()) replace(error, catchMatching({PyExc_Exception}));
            else replace(error, p::Ref(PyObject_CallFunction(PyExc_RuntimeError, const_cast<char*>("s"), e.what())));
            setAttr(battle, "_standard_space_visibility", Py_None);
            method(battle, "_warn_standard_space_visibility", error.get());
            Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._start_team_chat_when_roster_ready()
PyObject* startTeamChat(PyObject*, PyObject* args) {
    PyObject* battle;
    if (!PyArg_ParseTuple(args, "O", &battle)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode")) || truthy(attr(battle, "_team_chat_initialized")) ||
            !equal(attr(battle, "state").get(), p::text("running").get())) Py_RETURN_FALSE;
        auto provider = optionalAttr(attr(battle, "_avatar").get(), "guiSessionProvider");
        auto arena_getter = optionalAttr(provider.get(), "getArenaDP");
        if (!isCallable(arena_getter)) Py_RETURN_FALSE;
        auto arena = call0(arena_getter.get());
        auto allies = optionalAttr(arena.get(), "getAlliesVehiclesNumber");
        if (!isCallable(allies)) Py_RETURN_FALSE;
        bool teammate = false;
        try {
            teammate = strictInt(call0(allies.get()).get()) > 1;
        } catch (const std::exception&) {
            catchMatching({PyExc_TypeError, PyExc_ValueError, PyExc_OverflowError, PyExc_ReferenceError});
            Py_RETURN_FALSE;
        }
        if (!teammate) Py_RETURN_FALSE;
        auto started = method(battle, "_run_optional_feature", p::text("team chat initialization").get(),
                              attr(attr(battle, "_server").get(), "start_team_chat").get());
        if (truthy(started)) setAttr(battle, "_team_chat_initialized", Py_True);
        return p::boolean(truthy(started)).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._prewarm_critical_layout()
PyObject* prewarmCriticalLayout(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        if (!truthy(attr(battle, "_worker_mode")) || truthy(attr(battle, "_battle_live")) || attr(battle, "_prebattle_deadline").get() == Py_None)
            Py_RETURN_FALSE;
        auto records = attr(battle, "_records");
        p::Ref keys(PySequence_List(records.get()));
        if (PyList_Sort(keys.get()) < 0) throw p::Error("record sort failed");
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keys.get()); ++i) {
            PyObject* k = PyList_GET_ITEM(keys.get(), i);
            p::Ref record(PyObject_GetItem(records.get(), k));
            if (!truthy(get(record.get(), "ready")) || truthy(get(record.get(), "tombstone")) || truthy(get(record.get(), "local"))) continue;
            auto entity = method(battle, "_server_entity", get(record.get(), "engine_id").get());
            if (entity.get() == Py_None || !truthy(optionalAttr(entity.get(), "isStarted"))) continue;
            auto descriptor = optionalAttr(entity.get(), "typeDescriptor");
            if (descriptor.get() == Py_None || get(record.get(), "critical_layout_prewarmed").get() == descriptor.get()) continue;
            bool completed = true;
            try {
                completed = truthy(method(frameGlobal(g, "critical_damage").get(), "prewarm_layout", descriptor.get()));
            } catch (const std::exception&) {
                auto error = catchMatching({PyExc_Exception});
                completed = true;
                p::Ref text(PyObject_Str(k));
                method(battle, "_warn_optional_failure", p::text("critical layout prewarm for " + p::string(text.get())).get(), error.get());
            }
            if (completed) { put(record.get(), "critical_layout_prewarmed", p::retain(descriptor.get())); Py_RETURN_TRUE; }
        }
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._maybe_send_battle_ready()
PyObject* maybeSendBattleReady(PyObject*, PyObject* args) {
    PyObject* battle;
    if (!PyArg_ParseTuple(args, "O", &battle)) return nullptr;
    try {
        if (truthy(attr(battle, "_ready_sent")) || truthy(attr(battle, "_battle_live"))) Py_RETURN_FALSE;
        auto start = attr(battle, "_start_message");
        auto count = [&](const char* name) {
            auto rows = method(start.get(), "get", p::text(name).get());
            return truthy(rows) ? length(rows.get()) : 0;
        };
        auto collect = [&](const char* kind) {
            std::vector<p::Ref> out;
            p::Ref values(PyDict_Values(attr(battle, "_records").get()));
            for (Py_ssize_t i = 0; i < PyList_GET_SIZE(values.get()); ++i) {
                PyObject* record = PyList_GET_ITEM(values.get(), i);
                if (equal(get(record, "kind").get(), p::text(kind).get()) && !truthy(get(record, "tombstone"))) out.push_back(p::retain(record));
            }
            return out;
        };
        auto anyNotReady = [](const std::vector<p::Ref>& rows) {
            for (const auto& row : rows) if (!truthy(get(row.get(), "ready"))) return true;
            return false;
        };
        const Py_ssize_t expected_players = count("players");
        auto players = collect("player");
        if (Py_ssize_t(players.size()) != expected_players || anyNotReady(players)) Py_RETURN_FALSE;
        const Py_ssize_t expected_bots = count("bots");
        auto bots = collect("bot");
        if (Py_ssize_t(bots.size()) != expected_bots || truthy(attr(battle, "_pending_bot_create_order")) ||
            truthy(attr(battle, "_pending_bot_creates")) || anyNotReady(bots)) Py_RETURN_FALSE;
        auto ready = optionalAttr(attr(battle, "client").get(), "send_battle_ready");
        if (!isCallable(ready)) Py_RETURN_FALSE;
        auto planner = attr(battle, "_spawn_planner");
        auto bases = optionalAttr(planner.get(), "capture_bases");
        if (bases.get() == Py_None && !PyObject_HasAttrString(planner.get(), "capture_bases")) replace(bases, optionalAttr(planner.get(), "bases"));
        if (!truthy(p::Ref(PyObject_CallFunctionObjArgs(ready.get(), bases.get(), nullptr)))) throw p::Error("LAN server did not accept battle readiness");
        setAttr(battle, "_ready_sent", Py_True);
        method(battle, "_report_lineup_windows", integer(Py_ssize_t(bots.size())).get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._retry_bot_manifest(now)
PyObject* retryBotManifest(PyObject*, PyObject* args) {
    PyObject *battle, *now_raw;
    if (!PyArg_ParseTuple(args, "OO", &battle, &now_raw)) return nullptr;
    try {
        auto bots = attr(battle, "_bots");
        if (!truthy(attr(battle, "_worker_mode")) || !equal(attr(battle, "state").get(), p::text("running").get()) || bots.get() == Py_None)
            Py_RETURN_FALSE;
        auto discard = [&] { method(battle, "_discard_pending_bot_manifest"); };
        if (attr(battle, "_battle_result").get() != Py_None || truthy(optionalAttr(bots.get(), "finished"))) { discard(); Py_RETURN_FALSE; }
        auto start = attr(battle, "_start_message");
        auto start_round = truthy(start) ? method(start.get(), "get", p::text("round_id").get()) : p::retain(Py_None);
        auto client = attr(battle, "client");
        p::Ref identity(PyTuple_Pack(4, attr(battle, "_generation").get(), start_round.get(), optionalAttr(bots.get(), "authority_id").get(),
                                     optionalAttr(client.get(), "authority_epoch").get()));
        auto retry_identity = attr(battle, "_bot_manifest_retry_identity");
        if (retry_identity.get() != Py_None && !equal(retry_identity.get(), identity.get())) { discard(); Py_RETURN_FALSE; }
        if (!equal(optionalAttr(bots.get(), "round_id").get(), start_round.get())) { discard(); Py_RETURN_FALSE; }
        auto client_round = PyObject_HasAttrString(client.get(), "round_id") ? attr(client.get(), "round_id") : p::retain(start_round.get());
        if (client_round.get() != Py_None && !equal(client_round.get(), start_round.get())) { discard(); Py_RETURN_FALSE; }
        auto check = optionalAttr(client.get(), "is_bot_authority");
        if (isCallable(check) && !truthy(call0(check.get()))) { discard(); Py_RETURN_FALSE; }
        auto provider = optionalAttr(bots.get(), "pending_manifest");
        auto pending = isCallable(provider) ? call0(provider.get()) : p::retain(Py_None);
        if (pending.get() == Py_None) {
            setAttr(battle, "_next_bot_manifest_retry", p::number(0.).get());
            setAttr(battle, "_bot_manifest_retry_deadline", p::number(0.).get());
            setAttr(battle, "_bot_manifest_retry_identity", Py_None);
            Py_RETURN_FALSE;
        }
        const double now = p::scalar(p::Ref(PyNumber_Float(now_raw)).get());
        const double deadline = number(attr(battle, "_bot_manifest_retry_deadline").get());
        if (deadline > 0. && now + 1.0e-9 >= deadline) { discard(); throw p::Error("worker bot manifest enqueue timed out"); }
        const double next = number(attr(battle, "_next_bot_manifest_retry").get());
        if (next > 0. && now + 1.0e-9 < next) Py_RETURN_FALSE;
        return callKw(battle, "_enqueue_bot_manifest", {pending.get()}, {{"now", p::number(now).get()}}).release();
    } catch (const std::exception& e) { return fail(e); }
}

bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("Native frame comparison failed");
    return result != 0;
}
// `x += 1` on a runtime attribute.
void bump(PyObject* o, const char* name) {
    setAttr(o, name, p::Ref(PyNumber_InPlaceAdd(attr(o, name).get(), integer(1).get())).get());
}
// `except Exception:` around a body; BaseException still propagates.
template<class Body> bool contained(Body body) {
    try { body(); return true; }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        if (!PyErr_ExceptionMatches(PyExc_Exception)) throw;
        return false;
    }
}

// BattleRuntime._clock(): BigWorld.time() as a float, or wall time when it is unavailable.
PyObject* runtimeClock(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto function = optionalAttr(attr(attr(battle, "_runtime").get(), "bigworld").get(), "time");
        if (PyCallable_Check(function.get())) {
            PyObject* raw = PyObject_CallFunctionObjArgs(function.get(), nullptr);
            if (raw) {
                PyObject* value = PyNumber_Float(raw);
                Py_DECREF(raw);
                if (value) return value;
            }
            if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
            PyErr_Clear();
        }
        return method(frameGlobal(g, "time").get(), "time").release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._tick_critical_states(dt): the native critical owner, else canonical state with track repair.
PyObject* tickCriticalStates(PyObject*, PyObject* args) {
    PyObject *battle, *dt, *critical, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &dt, &critical, &g)) return nullptr;
    try {
        if (truthy(method(critical, "frame", battle, dt))) Py_RETURN_NONE;
        if (test(dt, p::Ref(PyFloat_FromDouble(0.0)).get(), Py_LE)) Py_RETURN_NONE;
        auto key = p::Ref(PyString_Format(p::text("player:%s").get(), attr(attr(battle, "client").get(), "player_id").get()));
        auto record = method(attr(battle, "_records").get(), "get", key.get());
        if (record.get() == Py_None) Py_RETURN_NONE;
        auto engine = p::Ref(PyObject_GetItem(record.get(), p::text("engine_id").get()));
        auto entity = method(battle, "_server_entity", engine.get());
        if (entity.get() == Py_None || attr(entity.get(), "typeDescriptor").get() == Py_None) Py_RETURN_NONE;
        if (!truthy(method(battle, "_record_alive", record.get(), entity.get()))) Py_RETURN_NONE;
        if (!PyObject_HasAttrString(entity.get(), "maxHealth"))
            setAttr(entity.get(), "maxHealth", p::Ref(PyNumber_Int(attr(attr(entity.get(), "typeDescriptor").get(), "maxHealth").get())).get());
        auto now = method(battle, "_clock");
        auto factor = method(battle, "_local_repair_factor", attr(entity.get(), "typeDescriptor").get());
        auto payload = method(battle, "_tick_local_track_repair", entity.get(), dt, factor.get());
        if (payload.get() != Py_None) {
            auto critical = method(battle, "_critical_state", payload.get());
            if (PyObject_SetItem(record.get(), p::text("critical_state").get(), critical.get()) < 0) throw p::Error("critical_state");
            auto previous = method(record.get(), "get", p::text("state").get());
            auto state = p::flag(previous.get()) ? p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), previous.get(), nullptr)) : p::dict();
            if (PyObject_SetItem(state.get(), p::text("critical").get(), p::Ref(PyObject_GetItem(record.get(), p::text("critical_state").get())).get()) < 0 ||
                PyObject_SetItem(record.get(), p::text("state").get(), state.get()) < 0) throw p::Error("state");
            // Close the repair timer before publishing the repaired-device transition.
            method(battle, "_present_repair_progress", entity.get());
            auto events = method(payload.get(), "get", p::text("events").get());
            dispatch(battle, "_present_critical", "present_critical",
                     {record.get(), events.get(), p::Ref(PyObject_GetItem(record.get(), p::text("engine_id").get())).get()});
            if (truthy(method(payload.get(), "get", p::text("events").get())) ||
                test(now.get(), attr(battle, "_next_critical_report_time").get(), Py_GE)) {
                method(battle, "_queue_local_track_repair", payload.get());
                setAttr(battle, "_next_critical_report_time",
                        p::Ref(PyNumber_Add(now.get(), frameGlobal(g, "CRITICAL_REPAIR_NETWORK_SECONDS").get())).get());
            }
        }
        method(battle, "_tick_equipment_cooldowns", now.get());
        method(battle, "_present_repair_progress", entity.get());
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._advance_artillery_arcs(now)
PyObject* advanceArtilleryArcs(PyObject*, PyObject* args) {
    PyObject *battle, *now, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &now, &bot_module, &g)) return nullptr;
    try {
        // The native Bot runtime owns arc previews when it is enabled.
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get()))) return integer(0).release();
        auto artillery = attr(battle, "_artillery");
        if (artillery.get() == Py_None) return integer(0).release();
        return method(artillery.get(), "advance", now, frameGlobal(g, "ARTILLERY_ARC_RAYS_PER_FRAME").get(),
                      attr(battle, "_artillery_arc_probe").get()).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._advance_authority_worker_probe(): the opt-in probe, never authoritative.
PyObject* advanceAuthorityWorkerProbe(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        // Dedicated workers remain draw-disabled; the diagnostic toggles draw/window stages.
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        auto config = attr(battle, "_config");
        auto settings = method(p::flag(config.get()) ? config.get() : p::dict().get(), "get", p::text("authority_worker_probe").get());
        if (!p::flag(settings.get())) replace(settings, p::dict());
        const bool enabled = PyDict_Check(settings.get()) &&
            truthy(method(settings.get(), "get", p::text("enabled").get(), Py_False));
        auto checker = optionalAttr(attr(battle, "_bots").get(), "is_authority");
        bool authority = false;
        if (PyCallable_Check(checker.get()) &&
            !contained([&] { authority = truthy(p::Ref(PyObject_CallFunctionObjArgs(checker.get(), nullptr))); })) {
            PyErr_Clear();
            authority = false;
        }
        auto round_id = [&] {
            auto start = attr(battle, "_start_message");
            return method(p::flag(start.get()) ? start.get() : p::dict().get(), "get", p::text("round_id").get());
        };
        auto report = [&](PyObject* error) {
            auto record = literal(6);
            put(record.get(), "schema", integer(1));
            put(record.get(), "probe", p::text("authority_worker"));
            put(record.get(), "event", p::text("probe_error"));
            put(record.get(), "process_id", method(frameGlobal(g, "os").get(), "getpid"));
            put(record.get(), "round_id", round_id());
            put(record.get(), "message", p::Ref(PyObject_Str(error)));
            p::Ref(PyObject_CallFunctionObjArgs(frameGlobal(g, "write_probe_record").get(), record.get(), nullptr));
        };
        auto probe = attr(battle, "_worker_probe");
        if (probe.get() != Py_None) {
            if (truthy(attr(probe.get(), "active")) && (!enabled || !truthy(attr(battle, "_battle_live")) || !authority)) {
                const char* reason = !authority ? "authority_lost" : !enabled ? "probe_disabled" : "battle_not_live";
                method(probe.get(), "stop", p::text(reason).get());
                Py_RETURN_FALSE;
            }
            if (!truthy(attr(probe.get(), "active"))) Py_RETURN_FALSE;
            bool ticked = contained([&] {
                bump(battle, "_worker_probe_authority_callbacks");
                method(probe.get(), "tick");
            });
            if (ticked) Py_RETURN_TRUE;
            // A measurement must never terminate or alter the round.
            auto error = caught(std::runtime_error("probe tick"));
            if (!contained([&] { method(probe.get(), "stop", p::text("probe_error").get()); })) PyErr_Clear();
            report(error.get());
            Py_RETURN_FALSE;
        }
        if (truthy(attr(battle, "_worker_probe_attempted")) || !enabled || !truthy(attr(battle, "_battle_live")) || !authority ||
            test(attr(battle, "_worker_probe_bot_count").get(), integer(0).get(), Py_LE))
            Py_RETURN_FALSE;
        setAttr(battle, "_worker_probe_attempted", Py_True);
        bool started = false;
        const bool ok = contained([&] {
            auto seconds = p::Ref(PyNumber_Float(method(settings.get(), "get", p::text("stageSeconds").get(),
                                                        p::Ref(PyFloat_FromDouble(15.0)).get()).get()));
            auto bigworld = attr(attr(battle, "_runtime").get(), "bigworld");
            auto sample = attr(battle, "_authority_worker_probe_sample");
            auto context = literal(4);
            put(context.get(), "process_id", method(frameGlobal(g, "os").get(), "getpid"));
            put(context.get(), "round_id", round_id());
            auto config_now = attr(battle, "_config");
            put(context.get(), "map", method(p::flag(config_now.get()) ? config_now.get() : p::dict().get(), "get", p::text("map").get()));
            put(context.get(), "player_id", optionalAttr(attr(battle, "client").get(), "player_id"));
            auto created = callWithMapping(frameGlobal(g, "AuthorityWorkerProbe").get(),
                                           p::Ref(PyTuple_Pack(2, bigworld.get(), sample.get())).get(),
                                           {{"stage_seconds", seconds.get()}, {"context", context.get()}}, p::dict().get());
            replace(probe, std::move(created));
            setAttr(battle, "_worker_probe", probe.get());
            if (!truthy(method(probe.get(), "start"))) return;
            bump(battle, "_worker_probe_authority_callbacks");
            method(probe.get(), "tick");
            started = true;
        });
        if (ok) return p::boolean(started).release();
        auto error = caught(std::runtime_error("probe start"));
        if (!contained([&] { if (probe.get() != Py_None) method(probe.get(), "stop", p::text("start_failed").get()); })) PyErr_Clear();
        report(error.get());
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef frame_methods[] = {
    {"frame", frame, METH_VARARGS, "Run one battle render callback."},
    {"optional_feature_enabled", optionalFeatureEnabled, METH_VARARGS, "Whether a presentation feature is still enabled."},
    {"run_optional_feature", runOptionalFeature, METH_VARARGS, "Run one presentation boundary without widening frame failure."},
    {"schedule", schedule, METH_VARARGS, "Schedule one generation-fenced runtime callback."},
    {"maintain_space_visibility", maintainSpaceVisibility, METH_VARARGS, "Restore the gameplay space visibility bit."},
    {"start_team_chat", startTeamChat, METH_VARARGS, "Start stock Chat2 once a teammate exists."},
    {"prewarm_critical_layout", prewarmCriticalLayout, METH_VARARGS, "Build one ready vehicle's hit layout per countdown frame."},
    {"maybe_send_battle_ready", maybeSendBattleReady, METH_VARARGS, "Open the countdown once the line-up has entered."},
    {"retry_bot_manifest", retryBotManifest, METH_VARARGS, "Retry the worker's bot manifest at bounded cadence."},
    {"clock", runtimeClock, METH_VARARGS, "BigWorld.time() as a float, or wall time."},
    {"tick_critical_states", tickCriticalStates, METH_VARARGS, "Present canonical critical state and local track repair."},
    {"advance_artillery_arcs", advanceArtilleryArcs, METH_VARARGS, "Advance the artillery arc preview rays."},
    {"advance_authority_worker_probe", advanceAuthorityWorkerProbe, METH_VARARGS, "Advance the opt-in authority worker probe."},
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* frame_methods_table() { return frame_methods; }
}
