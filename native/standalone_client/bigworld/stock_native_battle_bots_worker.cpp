// The hidden worker's process session: LAN connection and retry, round start and retirement, the
// world-draw lease, simulation progress and the launcher's status file.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_subsystems.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <algorithm>
#include <iterator>
#if defined(_WIN32) && !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include <windows.h>
#include <psapi.h>
#endif

namespace native_battle {
namespace worker {
namespace {
constexpr double MONITOR_SECONDS = 0.10, RETRY_SECONDS = 1.0, BUSY_RETRY_SECONDS = 5.0, RETRY_MAX_SECONDS = 15.0;
constexpr double PROGRESS_SECONDS = 1.0, STATUS_SECONDS = 2.0;
constexpr std::int64_t AUTHORITY_ID = -1;

double wall() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
Value null() { return Value(); }
Value boolean(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
Value integer(std::int64_t v) { Value x; x.kind = Value::Kind::Integer; x.integer = v; return x; }
Value number(double v) { Value x; x.kind = Value::Kind::Number; x.number = v; return x; }
Value text(const std::string& v) { Value x; x.kind = Value::Kind::Text; x.text = v; return x; }
Value object() { Value x; x.kind = Value::Kind::Object; return x; }
std::optional<std::int64_t> exact(PyObject* v) {
    if (!v || v == Py_None || PyBool_Check(v) || !(PyInt_Check(v) || PyLong_Check(v))) return {};
    const auto n = PyLong_AsLongLong(v);
    if (PyErr_Occurred()) { PyErr_Clear(); return {}; }
    return n;
}

void encode(const Value& raw, std::ostringstream& out) {
    const Value& v = raw.resolved();
    switch (v.kind) {
    case Value::Kind::Null: case Value::Kind::Shared: out << "null"; return;
    case Value::Kind::Boolean: out << (v.boolean ? "true" : "false"); return;
    case Value::Kind::Integer: out << v.integer; return;
    case Value::Kind::Number:
        if (!std::isfinite(v.number)) { out << "null"; return; }
        { char buffer[32]; std::snprintf(buffer, sizeof buffer, "%.17g", v.number); out << buffer; }
        return;
    case Value::Kind::Text:
        out << '"';
        for (unsigned char c : v.text) {
            if (c == '"' || c == '\\') out << '\\' << c;
            else if (c < 0x20) { char buffer[8]; std::snprintf(buffer, sizeof buffer, "\\u%04x", c); out << buffer; }
            else out << c;
        }
        out << '"';
        return;
    case Value::Kind::Array: {
        out << '[';
        for (std::size_t i = 0; i < v.array.size(); ++i) { if (i) out << ", "; encode(v.array[i], out); }
        out << ']';
        return;
    }
    case Value::Kind::Object: {
        out << '{';
        bool first = true;
        for (const auto& entry : v.object) {
            if (!first) out << ", ";
            first = false;
            encode(text(entry.first), out);
            out << ": ";
            encode(entry.second, out);
        }
        out << '}';
        return;
    }
    }
}

// Disables world drawing for the hidden worker and restores what it found.
struct DrawLease {
    Object bigworld;
    bool active = false, original = true;
    bool read() const {
        auto boundary = py::optional(bigworld.get(), "worldDrawEnabled");
        if (!py::callable(boundary)) throw p::Error("BigWorld.worldDrawEnabled is unavailable");
        auto value = py::invoke(boundary.get());
        if (!PyBool_Check(value.get()) && !PyInt_Check(value.get())) throw p::Error("world draw getter returned an invalid state");
        return p::flag(value.get());
    }
    void set(bool enabled) { py::call(bigworld.get(), "worldDrawEnabled", p::boolean(enabled).get()); }
    void acquire() {
        if (active) return;
        original = read();
        set(false);
        if (read()) { set(original); throw p::Error("world draw disable readback mismatch"); }
        active = true;
    }
    void restore() {
        if (!active) return;
        set(original);
        if (read() != original) throw p::Error("world draw restore readback mismatch");
        active = false;
    }
};

struct Session {
    Object config, lobby_ready, bigworld, client;
    std::string status_path, state = "idle";
    bool stopped = false, handling_failure = false;
    std::uint64_t generation = 0;
    std::optional<std::int64_t> active_round;
    Object pending_start;
    double pending_deadline = -1., retry_delay = RETRY_SECONDS, next_status = 0., next_progress = 0.;
    std::int64_t last_progress_frame = 0;
    std::set<std::int64_t> retired_rounds;
    Object monitor_id, retry_id;
    DrawLease draw;
    // One status window: the previous runtime sample and when it was taken.
    std::optional<Value> probe_sample;
    std::optional<std::int64_t> probe_round;
    std::uint64_t probe_battle = 0;
    double probe_time = 0., process_time = -1., process_cpu = -1.;
};
std::unique_ptr<Session> session;

bool authorityHere() {
    return session->client && p::flag(py::call(session->client.get(), "is_bot_authority").get());
}
void log(const std::string& line) { native_battle::log(line); }

// A native BigWorld.callback target bound to one session generation.
PyObject* callbackEntry(PyObject* self, PyObject*);
PyMethodDef monitorDef = {"worker_monitor", callbackEntry, METH_NOARGS, nullptr};
PyMethodDef retryDef = {"worker_retry", callbackEntry, METH_NOARGS, nullptr};
Object schedule(double delay, PyMethodDef* def) {
    p::Ref self(Py_BuildValue("(sK)", def == &monitorDef ? "monitor" : "retry", static_cast<unsigned long long>(session->generation)));
    p::Ref fn(PyCFunction_New(def, self.get()));
    return own(py::call(session->bigworld.get(), "callback", p::number(delay).get(), fn.get()));
}
void cancel(Object& id) {
    if (!id || id.get() == Py_None) return;
    PyObject* done = PyObject_CallMethod(session->bigworld.get(), const_cast<char*>("cancelCallback"), const_cast<char*>("O"), id.get());
    if (done) Py_DECREF(done); else PyErr_Clear();
    id = Object();
}

Value processPerformance(double now) {
    auto out = object();
    out.object["source"] = text("windows_process_api");
    out.object["gpu_measured"] = boolean(false);
#if defined(_WIN32) && !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
    FILETIME creation, exit_time, kernel, user;
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof memory;
    HANDLE process = GetCurrentProcess();
    if (GetProcessTimes(process, &creation, &exit_time, &kernel, &user) && GetProcessMemoryInfo(process, &memory, memory.cb)) {
        auto seconds = [](const FILETIME& t) { return double((std::uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10000000.; };
        const double cpu = seconds(kernel) + seconds(user);
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const std::int64_t processors = std::max<std::int64_t>(1, info.dwNumberOfProcessors);
        Value core = null();
        if (session->process_time >= 0. && now > session->process_time && cpu >= session->process_cpu)
            core = number(100. * (cpu - session->process_cpu) / (now - session->process_time));
        session->process_time = now;
        session->process_cpu = cpu;
        out.object["available"] = boolean(true);
        out.object["cpu_core_percent"] = core;
        out.object["cpu_machine_percent"] = core.kind == Value::Kind::Null ? null() : number(core.number / double(processors));
        out.object["working_set_bytes"] = integer(std::int64_t(memory.WorkingSetSize));
        out.object["logical_processors"] = integer(processors);
        return out;
    }
#endif
    out.object["available"] = boolean(false);
    for (const char* name : {"cpu_core_percent", "cpu_machine_percent", "working_set_bytes", "logical_processors"}) out.object[name] = null();
    return out;
}

// The worker's own progress sample: Bot authority counters and diagnostics.
Value sample(const Battle& b) {
    const auto* a = b.bots.get();
    auto out = object();
    out.object["round_finished"] = boolean(b.result.kind != Value::Kind::Null);
    if (!a) return out;
    out.object["frame_callbacks"] = integer(std::int64_t(a->frame_callbacks));
    out.object["authority_callbacks"] = integer(std::int64_t(a->authority_callbacks));
    out.object["bot_state_generated"] = integer(std::int64_t(a->generated));
    out.object["bot_state_enqueued"] = integer(std::int64_t(a->enqueued));
    out.object["bot_state_send_failed"] = integer(std::int64_t(a->send_failed));
    out.object["bot_count"] = integer(std::int64_t(a->bot_count));
    out.object["simulation_caps"] = integer(std::int64_t(a->simulation_caps));
    out.object["bot_state_revision"] = a->last_snapshot ? cs::read(p::value(a->last_snapshot.get(), "bot_state_revision").get()) : null();
    out.object["control"] = cs::read(bots::controlSnapshot(b).get());
    return out;
}
std::optional<std::int64_t> delta(const Value& current, const Value& previous, const char* name) {
    const Value &a = current.get(name), &b = previous.get(name);
    if ((a.kind != Value::Kind::Integer && a.kind != Value::Kind::Number) || (b.kind != Value::Kind::Integer && b.kind != Value::Kind::Number))
        return {};
    return std::max<std::int64_t>(0, std::int64_t(a.numeric()) - std::int64_t(b.numeric()));
}
Value runtimeStatus(double now) {
    const Battle* b = current();
    if (!b || !b->bots) { session->probe_sample.reset(); return object(); }
    auto value = sample(*b);
    const bool same = session->probe_sample && session->probe_battle == b->generation && session->probe_round == session->active_round &&
                      now > session->probe_time;
    auto rate = [&](std::optional<std::int64_t> d, double seconds) { return d ? number(double(*d) / seconds) : null(); };
    auto optional = [](std::optional<std::int64_t> d) { return d ? integer(*d) : null(); };
    if (same) {
        const Value& previous = *session->probe_sample;
        const double seconds = now - session->probe_time;
        value.object["window_seconds"] = number(seconds);
        value.object["render_callback_hz"] = rate(delta(value, previous, "frame_callbacks"), seconds);
        value.object["callback_hz"] = rate(delta(value, previous, "authority_callbacks"), seconds);
        value.object["bot_publication_hz"] = rate(delta(value, previous, "bot_state_enqueued"), seconds);
        value.object["revision_delta"] = optional(delta(value, previous, "bot_state_revision"));
        value.object["send_failed_delta"] = optional(delta(value, previous, "bot_state_send_failed"));
        const Value &control = value.get("control"), &previous_control = previous.get("control");
        value.object["catchup_delta"] = optional(delta(control, previous_control, "catchup_callbacks"));
        value.object["debt_callback_delta"] = optional(delta(control, previous_control, "debt_callbacks"));
        value.object["astar_budget_exhausted_delta"] = optional(delta(control, previous_control, "astar_budget_exhausted_callbacks"));
    } else {
        value.object["window_seconds"] = null();
    }
    session->probe_sample = value;
    session->probe_round = session->active_round;
    session->probe_battle = b->generation;
    session->probe_time = now;
    return value;
}

bool writeStatus(bool force = false) {
    const double now = wall();
    if (!force && now < session->next_status) return false;
    session->next_status = now + STATUS_SECONDS;
    Value draw = null();
    try { if (session->draw.bigworld) draw = boolean(session->draw.read()); } catch (const std::exception&) { PyErr_Clear(); }
    auto status = object();
    status.object["schema"] = integer(1);
    status.object["role"] = text("simulation_worker");
#if defined(_WIN32) && !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
    status.object["process_id"] = integer(std::int64_t(GetCurrentProcessId()));
#else
    status.object["process_id"] = null();
#endif
    status.object["heartbeat_epoch"] = number(now);
    status.object["state"] = text(session->state);
    status.object["connected"] = boolean(session->client && p::flag(py::optional(session->client.get(), "connected").get()));
    status.object["phase"] = session->client ? cs::read(py::optional(session->client.get(), "phase").get()) : null();
    status.object["round_id"] = session->active_round ? integer(*session->active_round) : null();
    status.object["bot_authority_id"] = session->client ? cs::read(py::optional(session->client.get(), "bot_authority_id").get()) : null();
    status.object["world_draw_enabled"] = draw;
    status.object["process_performance"] = processPerformance(now);
    status.object["runtime"] = runtimeStatus(now);
    std::ostringstream out;
    encode(status, out);
    out << '\n';
    const std::string temporary = session->status_path + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream << out.str();
        if (!stream) return false;
    }
    std::remove(session->status_path.c_str());
    return std::rename(temporary.c_str(), session->status_path.c_str()) == 0;
}

void scheduleMonitor() {
    if (session->stopped || session->monitor_id) return;
    session->monitor_id = schedule(MONITOR_SECONDS, &monitorDef);
}
void scheduleRetry(double delay = -1., bool grow = true) {
    if (session->stopped || session->retry_id) return;
    if (delay < 0.) delay = session->retry_delay;
    if (grow) session->retry_delay = std::min(RETRY_MAX_SECONDS, std::max(RETRY_SECONDS, delay * 2.));
    session->retry_id = schedule(delay, &retryDef);
}

void retireRuntime(const std::string& reason) {
    const auto round = session->active_round;
    if (reason != "round_complete" && session->client) py::set(session->client.get(), "bot_authority_id", Py_None);
    std::string error;
    try { if (current()) startup::stopRound(true); } catch (const std::exception& e) { error = describe(e); }
    try { session->draw.restore(); } catch (const std::exception& e) { if (error.empty()) error = describe(e); }
    session->probe_sample.reset();
    if (!error.empty()) throw p::Error(error);
    session->active_round.reset();
    session->last_progress_frame = 0;
    session->next_progress = 0.;
    if (round && reason != "round_complete") session->retired_rounds.insert(*round);
    session->state = reason == "round_complete" ? "waiting" : "standby";
    writeStatus(true);
}

bool failure(const std::string& error);
bool retireOrFail(const std::string& reason) {
    try { retireRuntime(reason); return true; } catch (const std::exception& e) { return failure(describe(e)); }
}

bool failure(const std::string& error) {
    if (session->handling_failure || session->stopped) return false;
    session->handling_failure = true;
    std::string lowered = error;
    for (auto& c : lowered) c = char(std::tolower(static_cast<unsigned char>(c)));
    const bool busy = lowered.find("battle already in progress") != std::string::npos;
    if (busy) session->retry_delay = BUSY_RETRY_SECONDS;
    log("simulation worker failed: " + error);
    if (session->active_round) session->retired_rounds.insert(*session->active_round);
    session->pending_start = Object();
    session->pending_deadline = -1.;
    if (session->client) py::set(session->client.get(), "bot_authority_id", Py_None);
    bool cleanup_failed = false;
    try { retireRuntime("worker_failed"); } catch (const std::exception& e) { cleanup_failed = true; log("worker cleanup failed: " + describe(e)); }
    Object client = session->client;
    ++session->generation;
    if (client) {
        try {
            py::set(client.get(), "on_event", Py_None);
            py::call(client.get(), "stop");
            if (session->client.get() == client.get()) session->client = Object();
        } catch (const std::exception& e) {
            cleanup_failed = true;
            log("worker transport cleanup failed: " + describe(e));
        }
    }
    session->state = cleanup_failed ? "failed" : "retrying";
    if (cleanup_failed) session->stopped = true;
    writeStatus(true);
    session->handling_failure = false;
    if (cleanup_failed) return false;
    scheduleRetry(-1., !busy);
    return true;
}

bool startRound(PyObject* message) {
    const auto round = exact(PyDict_GetItemString(message, "round_id"));
    if (!round || session->retired_rounds.count(*round) || !authorityHere()) return false;
    if (current() && session->active_round == round) return false;
    if (current() && session->active_round != round) {
        if (session->active_round && *round < *session->active_round) return false;
        try { retireRuntime("round_complete"); } catch (const std::exception& e) { failure(describe(e)); return false; }
    }
    bool lobby = false;
    try { lobby = p::flag(py::invoke(session->lobby_ready.get()).get()); } catch (const std::exception& e) { failure(describe(e)); return false; }
    if (!lobby) {
        if (session->pending_start && exact(PyDict_GetItemString(session->pending_start.get(), "round_id")) != round) {
            failure("worker received overlapping pending rounds");
            return false;
        }
        session->pending_start = own(p::Ref(PyDict_Copy(message)));
        session->pending_deadline = wall() + cs::read(session->config.get()).get("startupTimeoutSeconds").numeric(30.);
        session->state = "waiting_lobby";
        writeStatus(true);
        return true;
    }
    session->pending_start = Object();
    session->pending_deadline = -1.;
    session->last_progress_frame = 0;
    session->next_progress = 0.;
    if (current()) { failure("worker received overlapping round lifecycles"); return false; }
    // The worker plays its own dummy Avatar projection, parked below the map.
    PyObject* dummy = nullptr;
    PyObject* players = PyDict_GetItemString(message, "players");
    if (players && players != Py_None)
        for (Py_ssize_t i = 0; i < PySequence_Size(players); ++i) {
            auto row = p::item(players, i);
            if (PyDict_Check(row.get()) && exact(PyDict_GetItemString(row.get(), "id")) == AUTHORITY_ID) { dummy = row.get(); break; }
        }
    if (!dummy) { failure("worker dummy Avatar projection is missing"); return false; }
    p::Ref config(PyDict_Copy(session->config.get()));
    p::set(config.get(), "worker_mode", p::boolean(true));
    p::set(config.get(), "native_remote_vehicles", p::boolean(false));
    p::set(config.get(), "bot_track_animation", p::boolean(false));
    p::set(config.get(), "map", p::value(message, "map"));
    auto spawn = p::dict();
    for (const char* axis : {"x", "y", "z"}) p::set(spawn.get(), axis, p::value(dummy, axis));
    auto yaw = p::value(dummy, "yaw");
    p::set(spawn.get(), "yaw", yaw.get() == Py_None ? p::number(0.) : std::move(yaw));
    p::set(config.get(), "spawn", spawn);
    p::set(config.get(), "vehicle", p::value(dummy, "vehicle"));
    p::set(config.get(), "name", p::value(dummy, "name"));
    session->active_round = round;
    session->state = "loading";
    std::string error;
    bool accepted = false;
    try { accepted = startup::startRound(config.get(), message, session->client.get(), error); }
    catch (const std::exception& e) { failure(describe(e)); return false; }
    if (!accepted) { failure(error.empty() ? "worker battle runtime rejected start" : error); return false; }
    writeStatus(true);
    return true;
}

bool publishProgress(const Battle& b) {
    const double now = wall();
    if (now < session->next_progress || !b.bots || b.result.kind != Value::Kind::Null) return false;
    const auto frame = std::int64_t(b.bots->frame_callbacks);
    if (frame <= 0 || frame > 2147483647 || frame <= session->last_progress_frame) return false;
    if (!p::flag(py::call(session->client.get(), "send_simulation_progress", p::integer(frame).get()).get())) return false;
    session->last_progress_frame = frame;
    session->next_progress = now + PROGRESS_SECONDS;
    return true;
}

void monitor() {
    if (session->stopped) return;
    if (!current() && session->pending_start) {
        if (!authorityHere()) {
            session->pending_start = Object();
            session->pending_deadline = -1.;
            session->state = "standby";
        } else if (session->pending_deadline >= 0. && wall() >= session->pending_deadline) {
            failure("worker lobby restoration timed out");
            return;
        }
    }
    if (!current() && session->pending_start) {
        bool lobby = false;
        try { lobby = p::flag(py::invoke(session->lobby_ready.get()).get()); } catch (const std::exception& e) { failure(describe(e)); return; }
        if (lobby) {
            Object pending = std::move(session->pending_start);
            session->pending_start = Object();
            session->pending_deadline = -1.;
            if (!startRound(pending.get())) return;
        }
    }
    if (Battle* b = current()) {
        if (!authorityHere() || (session->active_round && session->retired_rounds.count(*session->active_round))) {
            if (!retireOrFail("authority_lost")) return;
        } else if (b->running()) {
            try { publishProgress(*b); } catch (const std::exception& e) { failure(describe(e)); return; }
            if (!bots::readyForDrawOff(*b)) {
                session->state = "loading_models";
                writeStatus();
                scheduleMonitor();
                return;
            }
            try { session->draw.acquire(); } catch (const std::exception& e) { failure(describe(e)); return; }
            session->state = "battle";
        } else if (b->phase == Phase::Failed) {
            failure(b->error.empty() ? "worker runtime failed" : b->error);
            return;
        }
    }
    writeStatus();
    scheduleMonitor();
}

bool connect() {
    if (session->stopped) return false;
    const auto generation = ++session->generation;
    auto module = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.authority_worker"));
    p::Ref self(Py_BuildValue("(sK)", "event", static_cast<unsigned long long>(generation)));
    static PyMethodDef eventDef = {"worker_event", nullptr, METH_VARARGS, nullptr};
    eventDef.ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
        PyObject *kind, *message;
        if (!PyArg_ParseTuple(args, "OO", &kind, &message)) return nullptr;
        if (!session || session->stopped || PyLong_AsUnsignedLongLong(PyTuple_GET_ITEM(self, 1)) != session->generation) Py_RETURN_NONE;
        try { onEvent(p::string(kind), message); }
        catch (const std::exception& e) { failure(describe(e)); }
        Py_RETURN_NONE;
    };
    p::Ref on_event(PyCFunction_New(&eventDef, self.get()));
    auto config = cs::read(session->config.get());
    auto host = p::text(config.get("host").kind == Value::Kind::Text ? config.get("host").text : std::string("127.0.0.1"));
    auto port = p::integer(std::int64_t(config.get("port").numeric(28782.)));
    auto kwargs = p::dict();
    p::set(kwargs.get(), "on_event", on_event);
    p::set(kwargs.get(), "bigworld", p::retain(session->bigworld.get()));
    p::Ref positional(PyTuple_Pack(2, host.get(), port.get()));
    session->client = own(p::Ref(PyObject_Call(py::attr(module.get(), "AuthorityWorkerLANClient").get(), positional.get(), kwargs.get())));
    session->state = "connecting";
    try {
        if (!p::flag(py::call(session->client.get(), "start").get())) throw p::Error("worker LAN client could not start");
    } catch (const std::exception& e) {
        session->client = Object();
        log("worker connect failed: " + describe(e));
        scheduleRetry();
        return false;
    }
    scheduleMonitor();
    writeStatus(true);
    return true;
}

std::string transportFailure(const std::string& kind, PyObject* message) {
    std::string detail, code;
    if (PyDict_Check(message)) {
        PyObject* m = PyDict_GetItemString(message, "message");
        PyObject* c = PyDict_GetItemString(message, "code");
        if (m && p::flag(m)) detail = p::string(p::Ref(PyObject_Str(m)).get());
        if (c && p::flag(c)) code = p::string(p::Ref(PyObject_Str(c)).get());
    }
    auto trim = [](std::string s) { s.erase(0, s.find_first_not_of(" \t\r\n")); s.erase(s.find_last_not_of(" \t\r\n") + 1); return s; };
    detail = trim(detail);
    code = trim(code);
    if (!detail.empty() && !code.empty()) return detail + " (" + kind + ", code " + code + ")";
    if (!detail.empty()) return detail + " (" + kind + ")";
    if (!code.empty()) return "worker transport lost (" + kind + ", code " + code + ")";
    std::vector<std::string> keys;
    if (PyDict_Check(message)) {
        Py_ssize_t cursor = 0;
        PyObject *key, *value;
        while (PyDict_Next(message, &cursor, &key, &value)) keys.push_back(p::string(p::Ref(PyObject_Str(key)).get()));
    }
    std::sort(keys.begin(), keys.end());
    std::string joined;
    for (std::size_t i = 0; i < keys.size() && i < 12; ++i) joined += (i ? "," : "") + keys[i];
    return "worker transport lost (" + kind + ", no message; payload keys: " + (joined.empty() ? "none" : joined) + ")";
}
}

void onEvent(const std::string& kind, PyObject* raw) {
    Object holder = own(PyDict_Check(raw) ? p::retain(raw) : p::dict());
    PyObject* message = holder.get();
    if (kind == "welcome" || kind == "roster") {
        if (kind == "welcome") session->retry_delay = RETRY_SECONDS;
        if (session->client && !authorityHere()) { session->pending_start = Object(); session->pending_deadline = -1.; }
        if (current() && !authorityHere() && !retireOrFail("authority_lost")) return;
        if (p::string(p::value(message, "phase").get()) == "waiting") {
            session->pending_start = Object();
            session->pending_deadline = -1.;
            if (current() && !retireOrFail("round_complete")) return;
            session->state = "waiting";
        } else if (!current()) {
            session->state = "standby";
        }
    } else if (kind == "battle_start") {
        if (authorityHere()) startRound(message);
        else session->state = "standby";
    } else if (kind == "snapshot" || kind == "events" || kind == "battle_live" || kind == "bot_observation" ||
               kind == "player_destructible_contact") {
        if (current() && !authorityHere()) { retireOrFail("authority_lost"); return; }
        Battle* b = current();
        if (!b || exact(PyDict_GetItemString(message, "round_id")) != session->active_round) return;
        startup::receive(*b, kind, message);
    } else if (kind == "battle_failed") {
        auto text_value = p::value(message, "message");
        failure(p::flag(text_value.get()) ? p::string(p::Ref(PyObject_Str(text_value.get())).get()) : "worker battle failed");
    } else if (kind == "error" || kind == "connection_lost" || kind == "disconnected") {
        failure(transportFailure(kind, message));
    }
}

namespace {
PyObject* callbackEntry(PyObject* self, PyObject*) {
    if (!session || PyLong_AsUnsignedLongLong(PyTuple_GET_ITEM(self, 1)) != session->generation) Py_RETURN_NONE;
    const std::string which = PyString_AsString(PyTuple_GET_ITEM(self, 0));
    try {
        if (which == "monitor") {
            session->monitor_id = Object();
            monitor();
        } else {
            session->retry_id = Object();
            if (session->stopped) Py_RETURN_NONE;
            if (!p::flag(py::invoke(session->lobby_ready.get()).get())) scheduleRetry(RETRY_SECONDS, false);
            else connect();
        }
    } catch (const std::exception& e) { failure(describe(e)); }
    Py_RETURN_NONE;
}
}

void install(PyObject* config, PyObject* lobby_ready, PyObject* bigworld) {
    session.reset(new Session);
    session->config = own(p::Ref(PyDict_Copy(config)));
    session->lobby_ready = borrow(lobby_ready);
    session->bigworld = borrow(bigworld);
    session->draw.bigworld = borrow(bigworld);
    auto settings = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.config"));
    auto path = p::string(py::attr(settings.get(), "CONFIG_PATH").get());
    const auto slash = path.find_last_of("/\\");
    session->status_path = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) + "authority_worker_status.json";
}

bool start() {
    if (!session || session->stopped || session->client) return false;
    return connect();
}

void stop(bool restore_account) {
    if (!session) return;
    if (session->stopped && !current() && !session->client && !session->draw.active) return;
    session->stopped = true;
    ++session->generation;
    cancel(session->monitor_id);
    cancel(session->retry_id);
    Object client = session->client;
    if (client) py::set(client.get(), "bot_authority_id", Py_None);
    std::string error;
    try {
        if (current()) startup::stopRound(restore_account);
    } catch (const std::exception& e) { error = describe(e); }
    try { session->draw.restore(); } catch (const std::exception& e) { if (error.empty()) error = describe(e); }
    if (client) {
        try {
            py::set(client.get(), "on_event", Py_None);
            py::call(client.get(), "stop");
            if (session->client.get() == client.get()) session->client = Object();
        } catch (const std::exception& e) { if (error.empty()) error = describe(e); }
    }
    session->active_round.reset();
    session->pending_start = Object();
    session->pending_deadline = -1.;
    session->last_progress_frame = 0;
    session->next_progress = 0.;
    session->probe_sample.reset();
    session->state = "stopped";
    writeStatus(true);
    if (!error.empty()) throw p::Error(error);
}

namespace {
PyObject* installEntry(PyObject*, PyObject* args) {
    PyObject *config, *lobby_ready, *bigworld;
    if (!PyArg_ParseTuple(args, "OOO", &config, &lobby_ready, &bigworld)) return nullptr;
    try { install(config, lobby_ready, bigworld); Py_RETURN_TRUE; }
    catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyObject* startEntry(PyObject*, PyObject*) {
    try { return PyBool_FromLong(start()); }
    catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyObject* stopEntry(PyObject*, PyObject* args) {
    int restore = 0;
    if (!PyArg_ParseTuple(args, "|i", &restore)) return nullptr;
    try { stop(restore != 0); Py_RETURN_NONE; }
    catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyMethodDef entries[] = {
    {"worker_session_install", installEntry, METH_VARARGS, "Install the hidden worker's native session."},
    {"worker_session_start", startEntry, METH_NOARGS, "Connect the hidden worker to the LAN server."},
    {"worker_session_stop", stopEntry, METH_VARARGS, "Stop the hidden worker session."},
    {nullptr, nullptr, 0, nullptr}};
}
PyMethodDef* methods() { return entries; }
}
}
