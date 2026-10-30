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
// self._run_optional_feature(name, fn, args[, disable][, disable=...][, kwargs=...])
p::Ref feature(PyObject* battle, const char* name, PyObject* fn, std::initializer_list<PyObject*> args,
               PyObject* disable_positional = nullptr, std::initializer_list<std::pair<const char*, PyObject*>> keywords = {}) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    if (disable_positional) return callKw(battle, "_run_optional_feature", {p::text(name).get(), fn, tuple.get(), disable_positional}, keywords);
    return callKw(battle, "_run_optional_feature", {p::text(name).get(), fn, tuple.get()}, keywords);
}
p::Ref feature(PyObject* battle, const char* name, PyObject* fn) {
    return callKw(battle, "_run_optional_feature", {p::text(name).get(), fn}, {});
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
            method(battle, "_flush_pending_bot_create", now.get());
            method(battle, "_flush_pending_entities", now.get());
            method(battle, "_start_team_chat_when_roster_ready");
            method(battle, "_drain_event_journal");
            feature(battle, "foliage camouflage", attr(battle, "_refresh_fallen_tree_foliage").get(), {now.get()});
            method(battle, "_retry_bot_manifest", now.get());
            method(battle, "_maybe_send_battle_ready");
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
            feature(battle, "detached turret lifecycle", attr(battle, "_advance_detached_turrets").get(), {now.get()}, nullptr,
                    {{"disable", Py_False}});
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
            try { method(battle, "_prewarm_critical_layout"); } catch (const std::exception& e) { warn("critical layout prewarm", e); }
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
                                                  {states.get(), now.get()}, nullptr, {{"disable", Py_False}});
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
                    feature(battle, "spotting", attr(battle, "_update_spotting").get(), {now.get()}, nullptr, {{"disable", Py_False}});
                else if (attr(battle, "_prebattle_deadline").get() != Py_None) {
                    auto kwargs = p::dict(); put(kwargs.get(), "hud_only", p::boolean(true));
                    feature(battle, "spotting", attr(battle, "_update_spotting").get(), {now.get()}, nullptr,
                            {{"disable", Py_False}, {"kwargs", kwargs.get()}});
                }
            }
            stage("spot");
            if (truthy(attr(battle, "_battle_live")) && !worker && !replay) {
                auto validate = optionalAttr(attr(attr(battle, "_runtime").get(), "compatibility").get(), "validate_target_lock");
                if (!isCallable(validate)) throw p::Error("#1513 target-lock lifecycle boundary is unavailable");
                feature(battle, "target lock validation", validate.get(), {attr(battle, "_avatar").get()}, nullptr, {{"disable", Py_False}});
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
        method(battle, "_schedule", frameGlobal(g, "FRAME_SECONDS").get(), attr(battle, "_frame").get());
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

PyMethodDef frame_methods[] = {
    {"frame", frame, METH_VARARGS, "Run one battle render callback."},
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* frame_methods_table() { return frame_methods; }
}
