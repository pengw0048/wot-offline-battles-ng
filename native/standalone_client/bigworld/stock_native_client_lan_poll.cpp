// The staged LANClient main-thread poll chain: one BigWorld callback drains the native transport,
// dispatches the ready batch and keeps its coarse timing. `g` is lan_client's globals(); every
// call on the client goes through its attributes, so the worker and replay subclasses still override.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace lan_poll {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref callGlobal(PyObject* g, const char* name) { return p::Ref(PyObject_CallFunctionObjArgs(global(g, name).get(), nullptr)); }
bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("LAN poll comparison failed");
    return result != 0;
}
void adjust(PyObject* o, const char* name, long delta) {
    p::Ref one(PyInt_FromLong(1));
    p::Ref value(delta > 0 ? PyNumber_InPlaceAdd(attr(o, name).get(), one.get()) : PyNumber_InPlaceSubtract(attr(o, name).get(), one.get()));
    setAttr(o, name, value.get());
}
// try: body() finally: cleanup(); an exception from cleanup replaces the body's.
template<class Body, class Cleanup> void tryFinally(Body body, Cleanup cleanup) {
    try { body(); }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        try { cleanup(); }
        catch (...) { Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace); throw; }
        PyErr_Restore(type, value, trace);
        throw;
    }
    cleanup();
}
void finish(PyObject* performance, const char* name, PyObject* started) {
    method(performance, "finish", p::text(name).get(), started);
}
// generation is not None and (generation != self._transport_generation or self._stopping)
bool superseded(PyObject* client, PyObject* generation) {
    return generation != Py_None &&
           (test(generation, attr(client, "_transport_generation").get(), Py_NE) || truthy(attr(client, "_stopping")));
}

// LANClient._poll()
PyObject* poll(PyObject*, PyObject* args) {
    PyObject *client, *g;
    if (!PyArg_ParseTuple(args, "OO", &client, &g)) return nullptr;
    try {
        auto performance = attr(client, "_transport_performance");
        // Nested polls would count their shared wall interval twice.
        if (truthy(attr(performance.get(), "poll_depth"))) setAttr(performance.get(), "valid", Py_False);
        adjust(performance.get(), "poll_depth", 1);
        auto started = method(performance.get(), "clock");
        tryFinally([&] { method(client, "_poll_messages"); },
                   [&] { finish(performance.get(), "poll", started.get()); adjust(performance.get(), "poll_depth", -1); });
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// LANClient._poll_messages(): drain, dispatch, keep-alive pings, then reschedule.
PyObject* pollMessages(PyObject*, PyObject* args) {
    PyObject *client, *g;
    if (!PyArg_ParseTuple(args, "OO", &client, &g)) return nullptr;
    try {
        auto generation = attr(client, "_transport_generation");
        setAttr(client, "_poll_callback", Py_None);
        method(client, "_drain_native_transport", generation.get());
        method(client, "_take_ready_messages", generation.get());
        if (test(generation.get(), attr(client, "_transport_generation").get(), Py_NE)) Py_RETURN_NONE;
        auto now = callGlobal(g, "_monotonic_time");
        method(client, "_report_snapshot_stall", now.get());
        if (truthy(attr(client, "connected")) &&
            test(p::Ref(PyNumber_Subtract(now.get(), attr(client, "_last_ping").get())).get(), global(g, "PING_INTERVAL").get(), Py_GE)) {
            setAttr(client, "_last_ping", now.get());
            adjust(client, "_ping_seq", 1);
            auto ping = literal(3);
            put(ping.get(), "type", p::text("ping"));
            put(ping.get(), "seq", attr(client, "_ping_seq"));
            put(ping.get(), "client_time", p::retain(now.get()));
            method(client, "_send", ping.get());
            p::Ref scope(PyTuple_Pack(2, attr(client, "round_id").get(), attr(client, "authority_epoch").get()));
            if (test(attr(client, "_worker_ping_scope").get(), scope.get(), Py_NE)) {
                setAttr(client, "_worker_ping_scope", scope.get());
                for (const char* name : {"worker_rtt_ms", "worker_frame_ms", "_worker_ping_started", "_worker_pong_time"})
                    setAttr(client, name, Py_None);
            }
            if (attr(client, "round_id").get() != Py_None &&
                test(attr(client, "player_id").get(), global(g, "WORKER_AUTHORITY_ID").get(), Py_NE)) {
                if (attr(client, "_worker_ping_started").get() == Py_None) setAttr(client, "_worker_ping_started", now.get());
                auto worker_ping = literal(3);
                put(worker_ping.get(), "type", p::text("worker_ping"));
                put(worker_ping.get(), "seq", attr(client, "_ping_seq"));
                put(worker_ping.get(), "client_time", p::retain(now.get()));
                method(client, "_send", worker_ping.get());
            }
        }
        auto error = attr(client, "last_error");
        if (error.get() != Py_None) {
            auto payload = literal(1);
            put(payload.get(), "message", attr(client, "last_error"));
            method(client, "_notify", p::text("error").get(), payload.get());
            setAttr(client, "last_error", Py_None);
        }
        if (truthy(attr(client, "running"))) method(client, "_schedule_poll");
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// LANClient._take_ready_messages(generation): detach one received batch and dispatch it.
PyObject* takeReadyMessages(PyObject*, PyObject* args) {
    PyObject *client, *generation, *g;
    if (!PyArg_ParseTuple(args, "OOO", &client, &generation, &g)) return nullptr;
    try {
        auto messages = p::Ref(PyList_New(0));
        auto lock = attr(client, "_pending_lock");
        method(lock.get(), "__enter__");
        tryFinally([&] {
            auto pending = attr(client, "_pending");
            if (truthy(pending)) {
                replace(messages, std::move(pending));
                setAttr(client, "_pending", p::Ref(PyList_New(0)).get());
            }
        }, [&] { method(lock.get(), "__exit__", Py_None, Py_None, Py_None); });
        method(client, "_dispatch_pending_messages", messages.get(), generation);
        const Py_ssize_t count = PyObject_Size(messages.get());
        if (count < 0) throw p::Error("len");
        return PyInt_FromSsize_t(count);
    } catch (const std::exception& e) { return fail(e); }
}

// LANClient._dispatch_message(message, generation=None)
PyObject* dispatchMessage(PyObject*, PyObject* args) {
    PyObject *client, *message, *generation, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &client, &message, &generation, &g)) return nullptr;
    try {
        if (superseded(client, generation)) Py_RETURN_FALSE;
        auto performance = attr(client, "_transport_performance");
        auto started = method(performance.get(), "clock");
        auto delta = global(g, "snapshot_delta");
        p::Ref prepared = none();
        bool dropped = false;
        try {
            tryFinally([&] { replace(prepared, method(delta.get(), "materialize", message)); },
                       [&] { finish(performance.get(), "materialize", started.get()); });
        } catch (const std::exception&) {
            if (!PyErr_Occurred() || !PyErr_ExceptionMatches(attr(delta.get(), "SnapshotDeltaError").get())) throw;
            PyErr_Clear();
            dropped = true;
        }
        if (dropped) {
            if (superseded(client, generation)) Py_RETURN_FALSE;
            adjust(client, "_snapshot_drop_streak", 1);
            setAttr(client, "_snapshot_drop_reason", p::text("actor_materialization").get());
            method(client, "_ignore_runtime_payload", p::text("snapshot").get(), attr(client, "_snapshot_drop_reason").get(), message);
            Py_RETURN_TRUE;
        }
        if (superseded(client, generation)) Py_RETURN_FALSE;
        auto handle_started = method(performance.get(), "clock");
        adjust(performance.get(), "handle_depth", 1);
        tryFinally([&] { method(client, "_handle_message", prepared.get()); },
                   [&] { adjust(performance.get(), "handle_depth", -1); finish(performance.get(), "handle", handle_started.get()); });
        if (generation == Py_None) Py_RETURN_TRUE;
        p::Ref same(PyObject_RichCompare(generation, attr(client, "_transport_generation").get(), Py_EQ));
        if (!truthy(same)) return same.release();
        return p::retain(truthy(attr(client, "_stopping")) ? Py_False : Py_True).release();
    } catch (const std::exception& e) { return fail(e); }
}

// LANClient._notify(kind, message, replay_hint=None)
PyObject* notify(PyObject*, PyObject* args) {
    PyObject *client, *kind, *message, *hint, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &client, &kind, &message, &hint, &g)) return nullptr;
    try {
        auto performance = attr(client, "_transport_performance");
        auto started = truthy(attr(performance.get(), "handle_depth")) ? method(performance.get(), "clock") : none();
        tryFinally([&] { method(client, "_notify_message", kind, message, hint); },
                   [&] { finish(performance.get(), "notify", started.get()); });
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// LANClient._notify_message(kind, message, replay_hint): replay capture, then the session consumer.
PyObject* notifyMessage(PyObject*, PyObject* args) {
    PyObject *client, *kind, *message, *hint, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &client, &kind, &message, &hint, &g)) return nullptr;
    try {
        auto replay = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.offline_replay"));
        method(replay.get(), "observe_wire", client, message, hint);
        auto on_event = attr(client, "on_event");
        if (on_event.get() == Py_None || kind == Py_None) Py_RETURN_NONE;
        p::Ref delivered = p::retain(message);
        if (PyDict_Check(message) && member(message, p::text("_client_received_time").get())) {
            // A duration crosses from the socket clock, never either clock's epoch.
            auto dispatched = callGlobal(g, "_monotonic_time");
            auto received = p::Ref(PyObject_CallFunctionObjArgs(global(g, "_finite_float").get(),
                method(message, "get", p::text("_client_received_time").get()).get(), dispatched.get(), nullptr));
            replace(delivered, p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), message, nullptr)));
            auto delay = p::Ref(PyNumber_Subtract(dispatched.get(), received.get()));
            auto zero = p::Ref(PyFloat_FromDouble(0.0));
            put(delivered.get(), "_client_dispatch_delay", test(delay.get(), zero.get(), Py_GT) ? std::move(delay) : std::move(zero));
        }
        p::Ref(PyObject_CallFunctionObjArgs(on_event.get(), kind, delivered.get(), nullptr));
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// LANClient.transport_performance_snapshot(): cumulative coarse main-thread costs, or None.
PyObject* performanceSnapshot(PyObject*, PyObject* args) {
    PyObject *client, *g;
    if (!PyArg_ParseTuple(args, "OO", &client, &g)) return nullptr;
    PyObject* result = nullptr;
    PyObject* performance = PyObject_GetAttrString(client, "_transport_performance");
    if (performance) {
        result = PyObject_CallMethod(performance, const_cast<char*>("snapshot"), nullptr);
        Py_DECREF(performance);
    }
    if (result) return result;
    if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
    PyErr_Clear();
    Py_RETURN_NONE;
}

// LANClient._report_snapshot_stall(now): name a replica that has accepted nothing for seconds.
PyObject* reportSnapshotStall(PyObject*, PyObject* args) {
    PyObject *client, *now, *g;
    if (!PyArg_ParseTuple(args, "OOO", &client, &now, &g)) return nullptr;
    try {
        if (test(attr(client, "phase").get(), p::text("battle").get(), Py_NE)) Py_RETURN_FALSE;
        auto accepted = attr(client, "_snapshot_accepted_time");
        if (accepted.get() == Py_None) Py_RETURN_FALSE;
        auto age = p::Ref(PyNumber_Subtract(now, accepted.get()));
        if (test(age.get(), global(g, "SNAPSHOT_STALL_SECONDS").get(), Py_LT)) Py_RETURN_FALSE;
        auto last = attr(client, "_snapshot_stall_log");
        if (last.get() != Py_None &&
            test(p::Ref(PyNumber_Subtract(now, last.get())).get(), global(g, "RUNTIME_DROP_LOG_INTERVAL").get(), Py_LT))
            Py_RETURN_FALSE;
        setAttr(client, "_snapshot_stall_log", now);
        auto reason = attr(client, "_snapshot_drop_reason");
        p::Ref values(PyTuple_Pack(4, age.get(), attr(client, "round_id").get(),
                                   p::Ref(PyNumber_Int(attr(client, "_snapshot_drop_streak").get())).get(),
                                   truthy(reason) ? reason.get() : p::text("none").get()));
        auto text = p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] snapshot stream stalled age=%.1fs "
                                                    "round=%s dropped=%d last_reason=%s").get(), values.get()));
        auto builtins = p::Ref(PyImport_ImportModule("__builtin__"));
        method(builtins.get(), "print", text.get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"lan_poll", poll, METH_VARARGS, "LANClient._poll: one timed main-thread poll."},
    {"lan_poll_messages", pollMessages, METH_VARARGS, "LANClient._poll_messages: drain, dispatch, ping, reschedule."},
    {"lan_take_ready_messages", takeReadyMessages, METH_VARARGS, "LANClient._take_ready_messages: dispatch one received batch."},
    {"lan_dispatch_message", dispatchMessage, METH_VARARGS, "LANClient._dispatch_message: materialize and handle one message."},
    {"lan_notify", notify, METH_VARARGS, "LANClient._notify: timed consumer notification."},
    {"lan_notify_message", notifyMessage, METH_VARARGS, "LANClient._notify_message: replay capture and the session consumer."},
    {"lan_performance_snapshot", performanceSnapshot, METH_VARARGS, "LANClient.transport_performance_snapshot."},
    {"lan_report_snapshot_stall", reportSnapshotStall, METH_VARARGS, "LANClient._report_snapshot_stall."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* lan_poll_methods() { return lan_poll::methods; }
}
