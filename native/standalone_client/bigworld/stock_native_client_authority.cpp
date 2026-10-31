// Native ports of the pinned BattleRuntime authority steps: critical-state revisions, the worker
// probe sample, draw-off readiness, the Bot manifest enqueue and the player descriptor donation.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace authority_steps {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref flt(double v) { return p::Ref(PyFloat_FromDouble(v)); }
bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("authority comparison failed");
    return result != 0;
}
p::Ref getattrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
p::Ref mget(PyObject* mapping, const char* name, PyObject* fallback = nullptr) {
    return fallback ? method(mapping, "get", p::text(name).get(), fallback) : method(mapping, "get", p::text(name).get());
}
Py_ssize_t length(PyObject* v) {
    const Py_ssize_t n = PyObject_Size(v);
    if (n < 0) throw p::Error("len");
    return n;
}
p::Ref item(PyObject* mapping, const char* name) { return p::Ref(PyObject_GetItem(mapping, p::text(name).get())); }
void store(PyObject* mapping, const char* name, PyObject* value) {
    if (PyObject_SetItem(mapping, p::text(name).get(), value) < 0) throw p::Error(name);
}
bool contains(PyObject* container, PyObject* value) {
    const int found = PySequence_Contains(container, value);
    if (found < 0) throw p::Error("membership");
    return found != 0;
}
// `value or ()` iterated as a list.
p::Ref rows(PyObject* value) { return p::Ref(PySequence_List(truthy(p::retain(value)) ? value : p::Ref(PyTuple_New(0)).get())); }
// `try: provider() except Exception: fallback`, after `getattr(owner, name, None)` and callable().
p::Ref provided(PyObject* owner, const char* name, PyObject* fallback) {
    auto provider = getattrOr(owner, name, Py_None);
    if (!PyCallable_Check(provider.get())) return p::retain(fallback);
    PyObject* result = PyObject_CallFunctionObjArgs(provider.get(), nullptr);
    if (result) return p::Ref(result);
    if (!PyErr_ExceptionMatches(PyExc_Exception)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}

// BattleRuntime.acknowledge_local_damage_report(base_revision, ack_seq, server_revision)
PyObject* acknowledgeLocalDamageReport(PyObject*, PyObject* args) {
    PyObject *battle, *base_raw, *ack_raw, *server_raw, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &base_raw, &ack_raw, &server_raw, &g)) return nullptr;
    try {
        auto base_revision = positive(toInt(base_raw));
        auto ack_seq = positive(toInt(ack_raw));
        auto server_revision = positive(toInt(server_raw));
        if (test(server_revision.get(), attr(battle, "_local_critical_server_revision").get(), Py_LT)) Py_RETURN_FALSE;
        setAttr(battle, "_local_critical_server_revision", server_revision.get());
        if (test(base_revision.get(), attr(battle, "_local_critical_base_revision").get(), Py_NE)) {
            setAttr(battle, "_local_critical_base_revision", base_revision.get());
            setAttr(battle, "_local_critical_next_seq", integer(0).get());
            setAttr(battle, "_local_critical_owned", Py_False);
            setAttr(battle, "_local_damage_report", Py_None);
            Py_RETURN_FALSE;
        }
        auto report = attr(battle, "_local_damage_report");
        if (report.get() != Py_None && test(ack_seq.get(), toInt(mget(report.get(), "critical_seq", integer(0).get()).get()).get(), Py_GE)) {
            bool repair_complete = truthy(mget(report.get(), "tracks"));
            if (repair_complete) {
                auto tracks = rows(mget(report.get(), "tracks").get());
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(tracks.get()) && repair_complete; ++i)
                    repair_complete = test(mget(PyList_GET_ITEM(tracks.get(), i), "state").get(), p::text("critical").get(), Py_EQ);
            }
            setAttr(battle, "_local_damage_report", Py_None);
            setAttr(battle, "_local_critical_owned", repair_complete ? Py_False : Py_True);
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._reconcile_critical_authority(record, source, canonical=None)
PyObject* reconcileCriticalAuthority(PyObject*, PyObject* args) {
    PyObject *battle, *record, *source, *canonical, *critical, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &record, &source, &canonical, &critical, &g)) return nullptr;
    try {
        auto native = method(critical, "reconcile", battle, record, source, canonical);
        if (native.get() != Py_None) return native.release();
        if (test(mget(record, "kind").get(), p::text("player").get(), Py_NE)) Py_RETURN_TRUE;
        for (const char* name : {"critical_revision", "critical_base_revision", "critical_ack_seq"})
            if (!contains(source, p::text(name).get())) raise("#1513 player critical state has no revision contract");
        auto revision = positive(toInt(item(source, "critical_revision").get()));
        auto base_revision = positive(toInt(item(source, "critical_base_revision").get()));
        auto ack_seq = positive(toInt(item(source, "critical_ack_seq").get()));
        auto previous_revision = toInt(mget(record, "critical_revision", integer(-1).get()).get());
        if (test(revision.get(), previous_revision.get(), Py_LT)) Py_RETURN_FALSE;
        store(record, "critical_revision", revision.get());
        store(record, "critical_base_revision", base_revision.get());
        store(record, "critical_ack_seq", ack_seq.get());
        if (truthy(mget(record, "local"))) {
            auto pending = attr(battle, "_local_damage_report");
            auto acknowledged = dispatch(battle, "acknowledge_local_damage_report", "acknowledge_local_damage_report",
                                         {base_revision.get(), ack_seq.get(), revision.get()}, {g});
            if (truthy(acknowledged) && truthy(attr(battle, "_local_critical_owned")) && PyDict_Check(pending.get()) && PyDict_Check(canonical)) {
                p::Ref pending_tracks(PySet_New(nullptr));
                auto tracks = rows(mget(pending.get(), "tracks").get());
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(tracks.get()); ++i) {
                    PyObject* row = PyList_GET_ITEM(tracks.get(), i);
                    if (PyDict_Check(row) && PySet_Add(pending_tracks.get(), str(mget(row, "name").get()).get()) < 0) throw p::Error("tracks");
                }
                auto states = p::dict();
                auto devices = rows(mget(canonical, "devices").get());
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(devices.get()); ++i) {
                    PyObject* row = PyList_GET_ITEM(devices.get(), i);
                    if (!PyDict_Check(row)) continue;
                    // A Python 2.7 dict comprehension evaluates the value before the key.
                    auto state = str(mget(row, "state").get());
                    auto name = str(mget(row, "name").get());
                    if (PyDict_SetItem(states.get(), name.get(), state.get()) < 0) throw p::Error("states");
                }
                p::Ref destroyed(PySet_New(nullptr));
                auto destroyed_rows = rows(mget(canonical, "destroyed").get());
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(destroyed_rows.get()); ++i)
                    if (PySet_Add(destroyed.get(), str(PyList_GET_ITEM(destroyed_rows.get(), i)).get()) < 0) throw p::Error("destroyed");
                bool all_repaired = PySet_GET_SIZE(pending_tracks.get()) > 0;
                if (all_repaired) {
                    p::Ref allowed(Py_BuildValue("(ss)", "normal", "critical"));
                    p::Ref iterator(PyObject_GetIter(pending_tracks.get()));
                    while (all_repaired) {
                        PyObject* raw = PyIter_Next(iterator.get());
                        if (!raw) { if (PyErr_Occurred()) throw p::Error("tracks"); break; }
                        p::Ref name(raw);
                        auto state = method(states.get(), "get", name.get());
                        all_repaired = contains(allowed.get(), state.get()) && !contains(destroyed.get(), name.get());
                    }
                }
                if (all_repaired) setAttr(battle, "_local_critical_owned", Py_False);
            }
            if (truthy(attr(battle, "_local_critical_owned")) && test(base_revision.get(), attr(battle, "_local_critical_base_revision").get(), Py_EQ) &&
                PyDict_Check(canonical)) {
                // The owning client advances only tracks; server-owned gun, engine, fire and crew stay canonical.
                p::Ref names(Py_BuildValue("(ss)", "leftTrackHealth", "rightTrackHealth"));
                replace(names, p::Ref(PyFrozenSet_New(names.get())));
                auto local_raw = mget(record, "critical_state");
                auto local = truthy(local_raw) ? std::move(local_raw) : p::dict();
                auto empty = [] { return p::Ref(PyTuple_New(0)); };
                p::Ref merged(PyList_New(0));
                auto canonical_devices = p::Ref(PySequence_List(mget(canonical, "devices", empty().get()).get()));
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(canonical_devices.get()); ++i) {
                    PyObject* row = PyList_GET_ITEM(canonical_devices.get(), i);
                    if (!contains(names.get(), mget(row, "name").get()))
                        p::append(merged.get(), p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), row, nullptr)));
                }
                p::Ref local_rows(PyList_New(0));
                auto local_devices = p::Ref(PySequence_List(mget(local.get(), "devices", empty().get()).get()));
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(local_devices.get()); ++i) {
                    PyObject* row = PyList_GET_ITEM(local_devices.get(), i);
                    if (contains(names.get(), mget(row, "name").get()))
                        p::append(local_rows.get(), p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), row, nullptr)));
                }
                store(canonical, "devices", p::Ref(PySequence_Concat(merged.get(), local_rows.get())).get());
                p::Ref kept(PySet_New(nullptr));
                p::Ref iterator(PyObject_GetIter(mget(canonical, "destroyed", empty().get()).get()));
                while (PyObject* raw = PyIter_Next(iterator.get())) {
                    p::Ref name(raw);
                    if (!contains(names.get(), name.get()) && PySet_Add(kept.get(), name.get()) < 0) throw p::Error("destroyed");
                }
                if (PyErr_Occurred()) throw p::Error("destroyed");
                p::Ref owned(PySet_New(nullptr));
                p::Ref local_iterator(PyObject_GetIter(mget(local.get(), "destroyed", empty().get()).get()));
                while (PyObject* raw = PyIter_Next(local_iterator.get())) {
                    p::Ref name(raw);
                    if (contains(names.get(), name.get()) && PySet_Add(owned.get(), name.get()) < 0) throw p::Error("destroyed");
                }
                if (PyErr_Occurred()) throw p::Error("destroyed");
                p::Ref joined(PyNumber_Or(kept.get(), owned.get()));
                p::Ref sorted(PySequence_List(joined.get()));
                if (PyList_Sort(sorted.get()) < 0) throw p::Error("sort");
                store(canonical, "destroyed", sorted.get());
            }
        }
        return p::boolean(test(revision.get(), previous_revision.get(), Py_GT)).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._authority_worker_probe_sample(): one worker progress row for the authority probe.
PyObject* authorityWorkerProbeSample(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto bots = attr(battle, "_bots");
        auto kinds = p::Ref(PySequence_List(global(g, "PROBE_KINDS").get()));
        auto totals = provided(bots.get(), "probe_totals", Py_None);
        auto probes = p::dict();
        if (PyList_Check(totals.get()) || PyTuple_Check(totals.get())) {
            for (Py_ssize_t index = 0; index < PyList_GET_SIZE(kinds.get()); ++index) {
                if (index >= length(totals.get())) break;
                p::Ref value(PySequence_GetItem(totals.get(), index));
                PyObject* converted = PyNumber_Int(value.get());
                if (!converted) {
                    if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError) &&
                        !PyErr_ExceptionMatches(PyExc_OverflowError)) throw p::Error("probe totals");
                    PyErr_Clear();
                    continue;
                }
                if (PyObject_SetItem(probes.get(), PyList_GET_ITEM(kinds.get(), index), p::Ref(converted).get()) < 0) throw p::Error("probes");
            }
        }
        auto durations = provided(bots.get(), "probe_duration_totals", Py_None);
        auto probe_seconds = p::dict();
        if (PyList_Check(durations.get()) || PyTuple_Check(durations.get())) {
            for (Py_ssize_t index = 0; index < PyList_GET_SIZE(kinds.get()); ++index) {
                if (index >= length(durations.get())) break;
                p::Ref value(PySequence_GetItem(durations.get(), index));
                PyObject* converted = PyNumber_Float(value.get());
                if (!converted) {
                    if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError) &&
                        !PyErr_ExceptionMatches(PyExc_OverflowError)) throw p::Error("probe durations");
                    PyErr_Clear();
                    continue;
                }
                p::Ref seconds(converted);
                const double v = PyFloat_AS_DOUBLE(converted);
                if (v >= 0.0 && std::isfinite(v) && PyObject_SetItem(probe_seconds.get(), PyList_GET_ITEM(kinds.get(), index), seconds.get()) < 0)
                    throw p::Error("probe seconds");
            }
        }
        p::Ref probe_timing = p::text("off");
        auto timing_provider = getattrOr(bots.get(), "probe_timing_state", Py_None);
        if (PyCallable_Check(timing_provider.get())) {
            PyObject* state = PyObject_CallFunctionObjArgs(timing_provider.get(), nullptr);
            PyObject* text = state ? PyObject_Str(state) : nullptr;
            Py_XDECREF(state);
            if (text) replace(probe_timing, p::Ref(text));
            else {
                if (!PyErr_ExceptionMatches(PyExc_Exception)) throw p::Error("probe timing");
                PyErr_Clear();
                replace(probe_timing, p::text("failed"));
            }
        }
        auto diagnostic_totals = dispatch(battle, "_worker_diagnostic_totals", "worker_diagnostic_totals", {}, {g});
        auto last = attr(battle, "_last_snapshot");
        auto snapshot = truthy(last) ? std::move(last) : p::dict();
        auto empty = p::dict();
        auto frame_performance = provided(attr(battle, "_frame_diagnostics").get(), "snapshot", empty.get());
        auto row = literal(17);
        put(row.get(), "round_finished", p::boolean(attr(battle, "_battle_result").get() != Py_None));
        put(row.get(), "frame_callbacks", attr(battle, "_worker_frame_callbacks"));
        put(row.get(), "authority_callbacks", attr(battle, "_worker_probe_authority_callbacks"));
        put(row.get(), "bot_state_generated", attr(battle, "_worker_probe_bot_generated"));
        put(row.get(), "bot_state_enqueued", attr(battle, "_worker_probe_bot_enqueued"));
        put(row.get(), "bot_state_send_failed", attr(battle, "_worker_probe_bot_send_failed"));
        put(row.get(), "bot_state_revision", mget(snapshot.get(), "bot_state_revision"));
        put(row.get(), "bot_probes", probes);
        put(row.get(), "bot_probe_seconds", probe_seconds);
        put(row.get(), "probe_timing", probe_timing);
        put(row.get(), "bot_count", attr(battle, "_worker_probe_bot_count"));
        put(row.get(), "simulation_caps", attr(battle, "_worker_probe_simulation_caps"));
        put(row.get(), "alive_bot_ticks", mget(diagnostic_totals.get(), "alive_bot_ticks"));
        put(row.get(), "bot_diagnostics", diagnostic_totals);
        put(row.get(), "control", dispatch(battle, "_worker_control_snapshot", "worker_control_snapshot", {}, {g}));
        auto presentation = literal(4);
        put(presentation.get(), "pose_writes", attr(battle, "_authority_pose_writes"));
        put(presentation.get(), "pose_skips", attr(battle, "_authority_pose_skips"));
        put(presentation.get(), "aim_writes", attr(battle, "_authority_aim_writes"));
        put(presentation.get(), "aim_skips", attr(battle, "_authority_aim_skips"));
        put(row.get(), "presentation", presentation);
        put(row.get(), "frame_performance", frame_performance);
        return row.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime.authority_worker_ready_for_draw_off(): every live record entered the native space.
PyObject* authorityWorkerReadyForDrawOff(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        if (!truthy(attr(battle, "_worker_mode")) || test(attr(battle, "state").get(), p::text("running").get(), Py_NE)) Py_RETURN_FALSE;
        if (truthy(attr(battle, "_pending_bot_create_order")) || truthy(attr(battle, "_pending_bot_creates"))) Py_RETURN_FALSE;
        auto records = attr(battle, "_records");
        auto values = method(records.get(), "values");
        p::Ref iterator(PyObject_GetIter(values.get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref record(raw);
            if (!truthy(mget(record.get(), "tombstone")) && !truthy(mget(record.get(), "ready"))) Py_RETURN_FALSE;
        }
        if (PyErr_Occurred()) throw p::Error("records");
        return p::boolean(truthy(records)).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._enqueue_bot_manifest(message, now=None): commit one frozen manifest after local enqueue.
PyObject* enqueueBotManifest(PyObject*, PyObject* args) {
    PyObject *battle, *message, *now_raw, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &message, &now_raw, &bot_module, &g)) return nullptr;
    try {
        if (!PyDict_Check(message) || test(mget(message, "type").get(), p::text("bot_manifest").get(), Py_NE) ||
            attr(battle, "_bots").get() == Py_None)
            Py_RETURN_FALSE;
        auto provider = getattrOr(attr(battle, "_bots").get(), "pending_manifest", Py_None);
        auto pending = PyCallable_Check(provider.get()) ? p::Ref(PyObject_CallFunctionObjArgs(provider.get(), nullptr)) : none();
        if (pending.get() == Py_None || test(pending.get(), message, Py_NE)) Py_RETURN_FALSE;
        auto now_value = now_raw == Py_None ? method(battle, "_clock") : p::retain(now_raw);
        const double now = PyFloat_AsDouble(p::Ref(PyNumber_Float(now_value.get())).get());
        auto start = attr(battle, "_start_message");
        auto identity = p::Ref(PyTuple_New(4));
        PyTuple_SET_ITEM(identity.get(), 0, attr(battle, "_generation").release());
        PyTuple_SET_ITEM(identity.get(), 1, mget(truthy(start) ? start.get() : p::dict().get(), "round_id").release());
        PyTuple_SET_ITEM(identity.get(), 2, getattrOr(attr(battle, "_bots").get(), "authority_id", Py_None).release());
        PyTuple_SET_ITEM(identity.get(), 3, getattrOr(attr(battle, "client").get(), "authority_epoch", Py_None).release());
        auto ahead = flt(now + 1.0e-9);
        auto zero = flt(0.0);
        auto same = [&] { return test(attr(battle, "_bot_manifest_retry_identity").get(), identity.get(), Py_EQ); };
        if (same() && test(attr(battle, "_bot_manifest_retry_deadline").get(), zero.get(), Py_GT) &&
            test(ahead.get(), attr(battle, "_bot_manifest_retry_deadline").get(), Py_GE)) {
            method(battle, "_discard_pending_bot_manifest");
            raise("worker bot manifest enqueue timed out");
        }
        if (same() && test(attr(battle, "_next_bot_manifest_retry").get(), zero.get(), Py_GT) &&
            test(ahead.get(), attr(battle, "_next_bot_manifest_retry").get(), Py_LT))
            Py_RETURN_FALSE;
        auto now_float = flt(now);
        const bool accepted = truthy(dispatch(battle, "_send_bot_message", "send_bot_message", {message}, {bot_module, g}));
        if (!accepted) {
            setAttr(battle, "_next_bot_manifest_retry", p::Ref(PyNumber_Add(now_float.get(), global(g, "BOT_MANIFEST_RETRY_SECONDS").get())).get());
            if (test(attr(battle, "_bot_manifest_retry_identity").get(), identity.get(), Py_NE)) {
                auto config = attr(battle, "_config");
                auto timeout = mget(truthy(config) ? config.get() : p::dict().get(), "startupTimeoutSeconds", flt(30.0).get());
                setAttr(battle, "_bot_manifest_retry_deadline", p::Ref(PyNumber_Add(now_float.get(), p::Ref(PyNumber_Float(timeout.get())).get())).get());
            }
            setAttr(battle, "_bot_manifest_retry_identity", identity.get());
            Py_RETURN_FALSE;
        }
        auto marker = getattrOr(attr(battle, "_bots").get(), "mark_manifest_enqueued", Py_None);
        if (!PyCallable_Check(marker.get()) || !truthy(p::Ref(PyObject_CallFunctionObjArgs(marker.get(), message, nullptr))))
            raise("enqueued bot manifest does not match the pending payload");
        setAttr(battle, "_next_bot_manifest_retry", zero.get());
        setAttr(battle, "_bot_manifest_retry_deadline", flt(0.0).get());
        setAttr(battle, "_bot_manifest_retry_identity", Py_None);
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._resolve_player_descriptor(state): the exact mounted human descriptor donated at join time.
PyObject* resolvePlayerDescriptor(PyObject*, PyObject* args) {
    PyObject *battle, *state, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &state, &g)) return nullptr;
    try {
        auto encoded = method(global(g, "lan_protocol").get(), "_canonical_vehicle_compact_descr", mget(state, "vehicle_compact_descr").get());
        if (encoded.get() == Py_None) raise("player mounted vehicle descriptor is unavailable");
        p::Ref key(Py_BuildValue("(sO)", "player", encoded.get()));
        auto cache = attr(battle, "_descriptor_cache");
        auto cached = method(cache.get(), "get", key.get());
        if (cached.get() != Py_None) return cached.release();
        auto descriptor = [&] {
            try {
                auto raw = method(global(g, "base64").get(), "b64decode", method(encoded.get(), "encode", p::text("ascii").get()).get());
                auto vehicles = attr(attr(battle, "_runtime").get(), "vehicles");
                return callWithMapping(attr(vehicles.get(), "VehicleDescr").get(), p::Ref(PyTuple_New(0)).get(), {{"compactDescr", raw.get()}},
                                       p::dict().get());
            } catch (const std::exception& e) {
                if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                auto error = caught(e);
                p::Ref text(PyString_Format(p::text("player mounted vehicle descriptor is unreadable: %s").get(), error.get()));
                PyErr_SetObject(PyExc_RuntimeError, text.get());
                throw p::Error("descriptor");
            }
        }();
        auto vehicle = mget(state, "vehicle");
        auto expected = str(truthy(vehicle) ? vehicle.get() : p::text("").get());
        auto name = getattrOr(attr(descriptor.get(), "type").get(), "name", p::text("").get());
        auto actual = str(truthy(name) ? name.get() : p::text("").get());
        if (!truthy(expected) || test(actual.get(), expected.get(), Py_NE)) raise("player mounted vehicle descriptor type mismatch");
        auto factory = attr(battle, "_remote_factory");
        if (factory.get() == Py_None) raise("#1513 vehicle descriptor geometry owner is unavailable");
        auto prepared = method(factory.get(), "prepare_descriptor", descriptor.get());
        if (PyObject_SetItem(cache.get(), key.get(), prepared.get()) < 0) throw p::Error("descriptor cache");
        return prepared.release();
    } catch (const std::exception& e) { return fail(e); }
}


PyMethodDef methods[] = {
    {"acknowledge_local_damage_report", acknowledgeLocalDamageReport, METH_VARARGS, "Retire one canonically acknowledged checkpoint."},
    {"reconcile_critical_authority", reconcileCriticalAuthority, METH_VARARGS, "Apply one critical-state revision contract."},
    {"authority_worker_probe_sample", authorityWorkerProbeSample, METH_VARARGS, "One worker authority-probe row."},
    {"authority_worker_ready_for_draw_off", authorityWorkerReadyForDrawOff, METH_VARARGS, "Whether every live record is native."},
    {"enqueue_bot_manifest", enqueueBotManifest, METH_VARARGS, "Commit one frozen Bot manifest after enqueue."},
    {"resolve_player_descriptor", resolvePlayerDescriptor, METH_VARARGS, "Prepare the donated player descriptor."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* authority_steps_methods() { return authority_steps::methods; }
}
