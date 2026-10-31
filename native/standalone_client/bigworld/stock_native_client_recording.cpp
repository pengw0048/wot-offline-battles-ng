// The per-frame offline replay recording functions of the staged offline_replay module; `g` is that
// module's globals(), so its helpers, constants and recorder classes stay the module's own.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"
#include <cstddef>

namespace stock_native_client_lifecycle {
namespace recording {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref call(PyObject* fn, std::initializer_list<PyObject*> args) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    return p::Ref(PyObject_CallObject(fn, tuple.get()));
}
p::Ref callGlobal(PyObject* g, const char* name, std::initializer_list<PyObject*> args) {
    return call(global(g, name).get(), args);
}
// getattr(o, name, fallback)
p::Ref attrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
// log('...%s' % value)
void logText(PyObject* g, const char* format, std::initializer_list<PyObject*> args) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    p::Ref text(PyString_Format(p::text(format).get(), tuple.get()));
    callGlobal(g, "log", {text.get()});
}
// `except Exception as error:` body runner; leaves the error set for caught().
template<class Body> bool contained(Body body) {
    try { body(); return true; }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        if (!PyErr_ExceptionMatches(PyExc_Exception)) throw;
        return false;
    }
}
void retire(PyObject* g, PyObject* recorder) {
    if (PyObject_HasAttrString(recorder, "poll_messages")) {
        method(global(g, "_FINISHING").get(), "append", recorder);
        callGlobal(g, "_schedule_finishing", {});
    }
}

void pollProcessStatusImpl(PyObject* client, PyObject* g) {
    auto recorder = attrOr(client, "_offline_replay_recorder", Py_None);
    if (recorder.get() == Py_None || !PyObject_HasAttrString(recorder.get(), "poll_messages")) return;
    callGlobal(g, "_drain_process", {recorder.get()});
    if (!truthy(attr(recorder.get(), "failed"))) return;
    setAttr(client, "_offline_replay_recorder", Py_None);
    auto finishing = global(g, "_FINISHING");
    if (!member(finishing.get(), recorder.get())) method(finishing.get(), "append", recorder.get());
    callGlobal(g, "_schedule_finishing", {});
}

// offline_replay.poll_process_status(client)
PyObject* pollProcessStatus(PyObject*, PyObject* args) {
    PyObject *client, *g;
    if (!PyArg_ParseTuple(args, "OO", &client, &g)) return nullptr;
    try { pollProcessStatusImpl(client, g); Py_RETURN_NONE; } catch (const std::exception& e) { return fail(e); }
}

// offline_replay.ensure_recording(runtime): start the native recorder once per round.
PyObject* ensureRecording(PyObject*, PyObject* args) {
    PyObject *runtime, *g;
    if (!PyArg_ParseTuple(args, "OO", &runtime, &g)) return nullptr;
    try {
        auto client = attr(runtime, "client");
        if (client.get() != Py_None) callGlobal(g, "poll_process_status", {client.get()});
        if (truthy(attrOr(runtime, "_worker_mode", Py_False)) || client.get() == Py_None ||
            truthy(attrOr(client.get(), "is_offline_replay", Py_False)) ||
            !truthy(attrOr(runtime, "_client_ready_received", Py_False)) || !truthy(attrOr(runtime, "_ready_sent", Py_False)))
            Py_RETURN_NONE;
        auto start = attr(runtime, "_start_message");
        auto round_id = method(p::flag(start.get()) ? start.get() : p::dict().get(), "get", p::text("round_id").get());
        if (equal(attrOr(client.get(), "_offline_replay_attempted", Py_None).get(), round_id.get())) Py_RETURN_NONE;
        setAttr(client.get(), "_offline_replay_attempted", round_id.get());
        auto mode = callGlobal(g, "native_recording_mode", {});
        if (!truthy(mode)) Py_RETURN_NONE;
        if (contained([&] {
                auto recorder_type = attr(p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.native_replay")).get(), "NativeRecorder");
                auto header = literal(6);
                put(header.get(), "welcome", attrOr(client.get(), "_offline_replay_welcome", Py_None));
                put(header.get(), "start", callGlobal(g, "capture_message", {attr(runtime, "_start_message").get()}));
                auto last = attr(client.get(), "last_snapshot");
                put(header.get(), "snapshot", callGlobal(g, "capture_message", {p::flag(last.get()) ? last.get() : p::dict().get()}));
                auto config = p::dict();
                auto keys = global(g, "CONFIG_KEYS");
                p::Ref iterator(PyObject_GetIter(keys.get()));
                while (PyObject* raw = PyIter_Next(iterator.get())) {
                    p::Ref key(raw);
                    auto runtime_config = attr(runtime, "_config");
                    if (!member(runtime_config.get(), key.get())) continue;
                    if (PyDict_SetItem(config.get(), key.get(), p::Ref(PyObject_GetItem(attr(runtime, "_config").get(), key.get())).get()) < 0)
                        throw p::Error("config");
                }
                if (PyErr_Occurred()) throw p::Error("config keys");
                put(header.get(), "config", config);
                put(header.get(), "player_id", p::Ref(PyNumber_Int(attr(client.get(), "player_id").get())));
                put(header.get(), "live", attrOr(client.get(), "_offline_replay_live", Py_None));
                if (!PyDict_Check(PyDict_GetItemString(header.get(), "welcome"))) {
                    PyErr_SetString(PyExc_ValueError, "recording has no negotiated welcome");
                    throw p::Error("welcome");
                }
                method(p::Ref(PyObject_GetItem(header.get(), p::text("snapshot").get())).get(), "setdefault",
                       p::text("type").get(), p::text("snapshot").get());
                auto os = global(g, "os");
                auto root = PyObject_HasAttrString(os.get(), "getcwdu") ? method(os.get(), "getcwdu") : method(os.get(), "getcwd");
                auto presentation = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.replay_presentation"));
                auto resources = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.replay_resources"));
                put(header.get(), "local_descriptor_contract",
                    method(presentation.get(), "descriptor_contract", attr(runtime, "_local_descriptor").get()));
                auto loadout = method(runtime, "_garage_loadout_snapshot");
                put(header.get(), "local_crew_group",
                    p::Ref(PyNumber_Int(method(loadout.get(), "get", p::text("crew_group").get(), integer(0).get()).get())));
                put(header.get(), "overlay_signature", method(resources.get(), "active_signature", root.get()));
                auto directory = method(attr(os.get(), "path").get(), "join", root.get(), p::text("replays").get(), p::text("offline").get());
                auto recorder = callWithMapping(recorder_type.get(), p::Ref(PyTuple_Pack(3, directory.get(), mode.get(), header.get())).get(),
                                                {{"clock", attr(runtime, "_clock").get()}}, p::dict().get());
                setAttr(client.get(), "_offline_replay_recorder", recorder.get());
                method(attr(client.get(), "_offline_replay_recorder").get(), "attach", client.get());
                logText(g, "record_start_requested mode=%s native_writer=True path=%s",
                        {mode.get(), attr(attr(client.get(), "_offline_replay_recorder").get(), "path").get()});
            }))
            Py_RETURN_NONE;
        auto error = caught(std::runtime_error("record start"));
        logText(g, "record_start_failed error=%s", {error.get()});
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// offline_replay.local_state(runtime): the local presentation state one replay row carries.
PyObject* localState(PyObject*, PyObject* args) {
    PyObject *runtime, *g;
    if (!PyArg_ParseTuple(args, "OO", &runtime, &g)) return nullptr;
    try {
        auto result = literal(1);
        put(result.get(), "position", p::Ref(PySequence_List(attr(runtime, "_local_position").get())));
        auto zero = p::Ref(PyFloat_FromDouble(0.0));
        auto scalars = global(g, "LOCAL_SCALARS");
        p::Ref iterator(PyObject_GetIter(scalars.get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref name(raw);
            PyObject* value = PyObject_GetAttr(runtime, name.get());
            if (!value) {
                if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("local scalar");
                PyErr_Clear();
                value = p::retain(zero.get()).release();
            }
            p::Ref held(value);
            if (PyObject_SetItem(result.get(), name.get(), held.get()) < 0) throw p::Error("local scalar");
        }
        if (PyErr_Occurred()) throw p::Error("local scalars");
        auto server = attr(runtime, "_server");
        auto entity = server.get() != Py_None ? method(runtime, "_server_entity", attr(server.get(), "vehicle_id").get()) : none();
        put(result.get(), "health", p::Ref(PyNumber_Int(attrOr(entity.get(), "health", integer(0).get()).get())));
        put(result.get(), "gun_angles", p::Ref(PyNumber_Int(attrOr(entity.get(), "gunAngles", integer(0).get()).get())));
        auto rotator = attrOr(attr(runtime, "_avatar").get(), "gunRotator", Py_None);
        if (rotator.get() != Py_None) {
            put(result.get(), "turret_yaw", p::Ref(PyNumber_Float(attrOr(rotator.get(), "turretYaw", zero.get()).get())));
            put(result.get(), "gun_pitch", p::Ref(PyNumber_Float(attrOr(rotator.get(), "gunPitch", zero.get()).get())));
            put(result.get(), "dispersion_angle", p::Ref(PyNumber_Float(attrOr(rotator.get(), "dispersionAngle", zero.get()).get())));
        }
        auto gun = attrOr(runtime, "_gun_state", Py_None);
        if (gun.get() != Py_None) {
            auto copy = global(g, "copy");
            auto values = p::dict();
            for (const char* name : {"ammo", "shot_index", "clip", "reload_time", "reload_duration", "load_started", "pending_index",
                                     "dispersion"}) {
                if (!PyObject_HasAttrString(gun.get(), name)) continue;
                put(values.get(), name, method(copy.get(), "deepcopy", attr(gun.get(), name).get()));
            }
            put(result.get(), "gun", values);
        }
        // Replay keeps the original observer's visibility decisions.
        auto visibility = p::dict();
        auto items = method(attr(runtime, "_records").get(), "items");
        p::Ref rows(PyObject_GetIter(items.get()));
        auto bot = p::text("bot");
        while (PyObject* raw = PyIter_Next(rows.get())) {
            p::Ref item(raw);
            auto pair = unpack(item.get(), 2);
            PyObject* record = pair[1].get();
            if (!equal(method(record, "get", p::text("kind").get()).get(), bot.get())) continue;
            auto spot = method(record, "get", p::text("spot_visible").get(), Py_True);
            auto marker = method(record, "get", p::text("spot_marker_visible").get(), Py_True);
            p::Ref flags(PyList_New(2));
            PyList_SET_ITEM(flags.get(), 0, p::retain(truthy(spot) ? Py_True : Py_False).release());
            PyList_SET_ITEM(flags.get(), 1, p::retain(truthy(marker) ? Py_True : Py_False).release());
            if (PyDict_SetItem(visibility.get(), pair[0].get(), flags.get()) < 0) throw p::Error("visibility");
        }
        if (PyErr_Occurred()) throw p::Error("records");
        put(result.get(), "visibility", visibility);
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// offline_replay.record_local(runtime): one local row per 1/30 s of recording time.
PyObject* recordLocal(PyObject*, PyObject* args) {
    PyObject *runtime, *g;
    if (!PyArg_ParseTuple(args, "OO", &runtime, &g)) return nullptr;
    try {
        auto recorder = attrOr(attr(runtime, "client").get(), "_offline_replay_recorder", Py_None);
        if (recorder.get() == Py_None || truthy(attr(recorder.get(), "closed"))) Py_RETURN_NONE;
        auto now = p::Ref(PyNumber_Float(method(runtime, "_clock").get()));
        auto elapsed = p::Ref(PyNumber_Subtract(now.get(), attr(recorder.get(), "started").get()));
        auto since = p::Ref(PyNumber_Subtract(elapsed.get(), attr(recorder.get(), "last_pose_at").get()));
        const int early = PyObject_RichCompareBool(since.get(), p::Ref(PyFloat_FromDouble(1.0 / 30.0)).get(), Py_LT);
        if (early < 0) throw p::Error("record cadence");
        if (early) Py_RETURN_NONE;
        if (contained([&] {
                auto state = callGlobal(g, "local_state", {runtime});
                method(recorder.get(), "append", p::text("local").get(), state.get());
                setAttr(recorder.get(), "last_pose_at", elapsed.get());
            }))
            Py_RETURN_NONE;
        auto error = caught(std::runtime_error("local record"));
        method(recorder.get(), "abort");
        setAttr(attr(runtime, "client").get(), "_offline_replay_recorder", Py_None);
        retire(g, recorder.get());
        logText(g, "local_record_failed error=%s", {error.get()});
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// lan_client._TransportPerformance: constant-size transport timing of one generation, never authoritative.
// `values` stays the pinned insertion-ordered dict, so snapshot() copies keep its iteration order.
struct Performance {
    PyObject_HEAD
    PyObject* globals;
    PyObject* generation;
    PyObject* values;
    PyObject* handle_depth;
    PyObject* poll_depth;
    PyObject* valid;
};
Performance* performance(PyObject* self) { return reinterpret_cast<Performance*>(self); }
// self.valid = False (#1513's Python 2.7.7 has no Py_SETREF).
void invalidate(Performance* s) {
    PyObject* previous = s->valid;
    Py_INCREF(Py_False);
    s->valid = Py_False;
    Py_XDECREF(previous);
}

// float(_TRANSPORT_PROFILE_CLOCK()) when finite and non-negative; any failure invalidates the totals.
PyObject* performanceClock(PyObject* self, PyObject*) {
    Performance* s = performance(self);
    try {
        bool ok = false;
        p::Ref value = none();
        if (!contained([&] {
                if (!p::flag(s->valid)) { ok = true; return; }
                p::Ref raw(PyObject_CallFunctionObjArgs(global(s->globals, "_TRANSPORT_PROFILE_CLOCK").get(), nullptr));
                p::Ref number(PyNumber_Float(raw.get()));
                const double v = PyFloat_AS_DOUBLE(number.get());
                if (0.0 <= v && v < HUGE_VAL) { replace(value, std::move(number)); ok = true; }
            }))
            PyErr_Clear();
        if (ok) return value.release();
        invalidate(s);
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* performanceFinish(PyObject* self, PyObject* args) {
    PyObject *name, *started;
    if (!PyArg_ParseTuple(args, "OO", &name, &started)) return nullptr;
    Performance* s = performance(self);
    try {
        if (started == Py_None) Py_RETURN_NONE;
        if (contained([&] {
                p::Ref now(performanceClock(self, nullptr));
                p::Ref elapsed(PyNumber_Subtract(now.get(), started));
                const int negative = PyObject_RichCompareBool(elapsed.get(), p::Ref(PyFloat_FromDouble(0.0)).get(), Py_LT);
                if (negative < 0) throw p::Error("elapsed");
                if (negative) { invalidate(s); return; }
                p::Ref wall(PyNumber_Add(name, p::text("_wall_seconds").get()));
                p::Ref total(PyNumber_InPlaceAdd(p::Ref(PyObject_GetItem(s->values, wall.get())).get(), elapsed.get()));
                if (PyObject_SetItem(s->values, wall.get(), total.get()) < 0) throw p::Error("wall");
                p::Ref calls(PyNumber_Add(name, p::text("_calls").get()));
                p::Ref count(PyNumber_InPlaceAdd(p::Ref(PyObject_GetItem(s->values, calls.get())).get(), integer(1).get()));
                if (PyObject_SetItem(s->values, calls.get(), count.get()) < 0) throw p::Error("calls");
            }))
            Py_RETURN_NONE;
        PyErr_Clear();
        invalidate(s);
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* performanceSnapshot(PyObject* self, PyObject*) {
    Performance* s = performance(self);
    try {
        p::Ref result = none();
        if (!contained([&] {
                // A native callback can re-enter the frame loop mid-poll; its parent timer is incomplete.
                if (p::flag(s->poll_depth) || !p::flag(s->valid)) return;
                p::Ref copy(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), s->values, nullptr));
                if (PyObject_SetItem(copy.get(), p::text("generation").get(), s->generation) < 0) throw p::Error("generation");
                replace(result, std::move(copy));
            })) {
            PyErr_Clear();
            invalidate(s);
            Py_RETURN_NONE;
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}
int performanceTraverse(PyObject* self, visitproc visit, void* arg) {
    Performance* s = performance(self);
    Py_VISIT(s->globals); Py_VISIT(s->generation); Py_VISIT(s->values);
    Py_VISIT(s->handle_depth); Py_VISIT(s->poll_depth); Py_VISIT(s->valid);
    return 0;
}
int performanceClear(PyObject* self) {
    Performance* s = performance(self);
    Py_CLEAR(s->globals); Py_CLEAR(s->generation); Py_CLEAR(s->values);
    Py_CLEAR(s->handle_depth); Py_CLEAR(s->poll_depth); Py_CLEAR(s->valid);
    return 0;
}
void performanceDealloc(PyObject* self) {
    PyObject_GC_UnTrack(self);
    performanceClear(self);
    PyObject_GC_Del(self);
}
template<std::size_t Offset> PyObject* getField(PyObject* self, void*) {
    PyObject* v = *reinterpret_cast<PyObject**>(reinterpret_cast<char*>(self) + Offset);
    if (!v) { PyErr_SetString(PyExc_AttributeError, "transport performance field"); return nullptr; }
    Py_INCREF(v);
    return v;
}
template<std::size_t Offset> int setField(PyObject* self, PyObject* value, void*) {
    if (!value) { PyErr_SetString(PyExc_AttributeError, "transport performance fields cannot be deleted"); return -1; }
    PyObject*& slot = *reinterpret_cast<PyObject**>(reinterpret_cast<char*>(self) + Offset);
    PyObject* previous = slot;
    Py_INCREF(value);
    slot = value;
    Py_XDECREF(previous);
    return 0;
}
#define FIELD(name) {const_cast<char*>(#name), getField<offsetof(Performance, name)>, setField<offsetof(Performance, name)>, nullptr, nullptr}
PyGetSetDef performanceFields[] = {FIELD(generation), FIELD(values), FIELD(handle_depth), FIELD(poll_depth), FIELD(valid),
                                   {nullptr, nullptr, nullptr, nullptr, nullptr}};
#undef FIELD
PyMethodDef performanceMethods[] = {
    {"clock", performanceClock, METH_NOARGS, nullptr},
    {"finish", performanceFinish, METH_VARARGS, nullptr},
    {"snapshot", performanceSnapshot, METH_NOARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
PyTypeObject PerformanceType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_client_lifecycle_native.TransportPerformance"};

// TransportPerformance(globals, generation)
PyObject* transportPerformance(PyObject*, PyObject* args) {
    PyObject *g, *generation;
    if (!PyArg_ParseTuple(args, "O!O", &PyDict_Type, &g, &generation)) return nullptr;
    try {
        if (!PerformanceType.tp_flags) {
            PerformanceType.tp_basicsize = sizeof(Performance);
            PerformanceType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
            PerformanceType.tp_dealloc = performanceDealloc;
            PerformanceType.tp_traverse = performanceTraverse;
            PerformanceType.tp_clear = performanceClear;
            PerformanceType.tp_methods = performanceMethods;
            PerformanceType.tp_getset = performanceFields;
            if (PyType_Ready(&PerformanceType) < 0) throw p::Error("transport performance type failed");
        }
        auto values = p::dict();
        for (const char* name : {"poll", "materialize", "handle", "notify"}) {
            put(values.get(), (std::string(name) + "_wall_seconds").c_str(), p::Ref(PyFloat_FromDouble(0.0)));
            put(values.get(), (std::string(name) + "_calls").c_str(), integer(0));
        }
        auto* object = PyObject_GC_New(Performance, &PerformanceType);
        if (!object) throw p::Error("transport performance allocation failed");
        Py_INCREF(g); object->globals = g;
        Py_INCREF(generation); object->generation = generation;
        object->values = values.release();
        object->handle_depth = integer(0).release();
        object->poll_depth = integer(0).release();
        Py_INCREF(Py_True); object->valid = Py_True;
        PyObject_GC_Track(object);
        return reinterpret_cast<PyObject*>(object);
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"poll_process_status", pollProcessStatus, METH_VARARGS, "Retire a failed recorder process."},
    {"ensure_recording", ensureRecording, METH_VARARGS, "Start the offline replay recorder once per round."},
    {"local_state", localState, METH_VARARGS, "The local presentation state of one replay row."},
    {"record_local", recordLocal, METH_VARARGS, "Record one local replay row at 30 Hz."},
    {"TransportPerformance", transportPerformance, METH_VARARGS, "The pinned lan_client._TransportPerformance."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* recording_methods() { return recording::methods; }
}
