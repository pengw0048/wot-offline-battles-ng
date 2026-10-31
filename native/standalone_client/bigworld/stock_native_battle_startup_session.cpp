// LAN session entry points: snapshot orchestration, roster authority, team radio, replay recording and PERF.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_startup_internal.hpp"
#include "stock_native_battle_local.hpp"
#include <algorithm>

namespace native_battle {
namespace startup {
namespace {
constexpr std::size_t kWarningTextLimit = 240;

Value& writable(Value& v) {
    if (v.reference) { Value copy = *v.reference; v = std::move(copy); }
    if (v.kind != Value::Kind::Object) v.kind = Value::Kind::Object;
    return v;
}
bool stopped(const Battle& b) { return b.phase == Phase::Failed || b.phase == Phase::Stopped || b.phase == Phase::Leaving; }
// One single-line diagnostic safe for logs and the wire.
std::string bounded(const std::string& error) {
    std::string out;
    bool space = false;
    for (char c : error) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { space = !out.empty(); continue; }
        if (space) { out += ' '; space = false; }
        out += c;
    }
    if (out.empty()) out = "Error";
    return out.substr(0, kWarningTextLimit);
}
// The radio relay applies only to the visible client's current round.
bool radioCurrent(Battle& b, const Value& message) {
    return b.running() && !b.worker && b.engine.avatar && server::radio(b) &&
           field(message, "round_id") == field(b.start, "round_id");
}
// (engine id, account dbid) of a ready human/Bot the arena roster knows.
std::optional<std::pair<std::int64_t, p::Ref>> radioIdentity(Battle& b, const std::string& kind, const Value& network_id) {
    const auto& id = network_id.resolved();
    if (id.kind != Value::Kind::Integer && id.kind != Value::Kind::Number) return {};
    Actor* v = b.vehicle(std::string(kind == "human" ? "player" : "bot") + ":" + std::to_string(std::int64_t(id.numeric())));
    if (!v || v->tombstone || !v->ready || !v->engine_id) return {};
    auto arena = py::optional(b.engine.avatar.get(), "arena");
    auto vehicles = py::optional(arena.get(), "vehicles");
    PyObject* info = PyDict_Check(vehicles.get()) ? PyDict_GetItem(vehicles.get(), p::integer(v->engine_id).get()) : nullptr;
    if (!info || !PyDict_Check(info)) return {};
    PyObject* account = PyDict_GetItemString(info, "accountDBID");
    if (!account || !p::flag(account)) return {};
    return std::make_pair(v->engine_id, p::retain(account));
}
// The per-issuer sequence key of one radio message.
std::pair<std::string, std::int64_t> issuerKey(const Value& message) {
    return {text(field(message, "round_id"), std::to_string(integer(field(message, "round_id")))), integer(field(message, "issuer_id"))};
}
}

void ignoreLivePayload(Battle& b, const char* kind, const std::string& error, PyObject* message) {
    const std::string reason = bounded(error);
    auto callback = py::optional(b.engine.client.get(), "_ignore_runtime_payload");
    if (py::callable(callback)) {
        PyObject* handled = PyObject_CallFunctionObjArgs(callback.get(), p::text(kind).get(), p::text(reason).get(), message ? message : Py_None, nullptr);
        if (handled) { const bool done = PyObject_IsTrue(handled) > 0; Py_DECREF(handled); if (done) return; }
        else PyErr_Clear();
    }
    b.warn(std::string("recoverable ") + kind + " payload", reason, false);
}

// One authority snapshot: each consumer ingests it; a bad frame is contained, not fatal.
void snapshot(Battle& b, PyObject* message) {
    if (stopped(b)) return;
    auto& s = *b.startup;
    Value previous = std::move(s.last_snapshot);
    struct Source {
        Battle& b;
        ~Source() { b.source_message = nullptr; b.source_value = nullptr; }
    } source{b};
    try {
        s.last_snapshot = fromPython(PyDict_Check(message) ? message : p::dict().get());
        const Value& last = s.last_snapshot;
        if (PyDict_Check(message)) { b.source_message = message; b.source_value = &last; }
        if (!b.worker) gun::restoreEquipment(b, last, true);
        if (b.local) {
            local::acknowledgeContacts(b, last, false);
            local::acknowledgeContacts(b, last, true);
        }
        observeDestructiblesDisabled(b, last);
        projectiles::observe(b, last);
        projectiles::reconcile(b, last);
        world::reconcileDetachedTurrets(b, last);
        if (has(last, "rules")) events::applyRules(b, field(last, "rules"));
        if (field(last, "battle_result").resolved().kind != Value::Kind::Null) events::applyBattleResult(b, field(last, "battle_result"));
        if (has(last, "destructibles")) world::applyDestructibleState(b, field(last, "destructibles"));
        if (b.bots) {
            if (has(last, "bot_authority_id")) bots::reconcileAuthority(b, field(last, "bot_authority_id"));
            bots::applySnapshot(b, last);
            world::rememberRamBotSnapshot(b, last);
        }
        try {
            vehicles::receiveSnapshotMessage(b, message, b.clock());
        } catch (const std::exception& e) {
            // The Bot state store consumed this frame; contain the presentation timeline alone.
            ignoreLivePayload(b, "snapshot_sync", describe(e), message);
        }
    } catch (const std::exception& e) {
        s.last_snapshot = std::move(previous);
        ignoreLivePayload(b, "snapshot", describe(e), message);
    }
}

// A loading-phase roster is the only durable authority update before live snapshots.
bool roster(Battle& b, const Value& message) {
    if (stopped(b) || field(message, "round_id") != field(b.start, "round_id") || !has(message, "bot_authority_id")) return false;
    projectiles::observe(b, message);
    writable(b.start).object["bot_authority_id"] = field(message, "bot_authority_id");
    observeDestructiblesDisabled(b, message);
    if (!b.bots) return true;
    bots::reconcileAuthority(b, field(message, "bot_authority_id"));
    return true;
}

bool teamCommandAck(Battle& b, const Value& message) {
    if (!radioCurrent(b, message)) return false;
    auto responders = p::list();
    for (const auto& bot : field(message, "recipient_bot_ids").resolved().array)
        if (auto identity = radioIdentity(b, "bot", bot)) p::append(responders.get(), std::move(identity->second));
    return p::flag(py::call(server::radio(b), "receive_ack", toPython(field(message, "command_seq")).get(), toPython(field(message, "accepted")).get(),
                            responders.get()).get());
}

bool teamChatAck(Battle& b, const Value& message) {
    if (!radioCurrent(b, message)) return false;
    return p::flag(py::call(server::radio(b), "receive_chat_ack", toPython(field(message, "chat_seq")).get(), toPython(field(message, "accepted")).get()).get());
}

bool teamChat(Battle& b, const Value& message) {
    if (!radioCurrent(b, message) || integer(field(message, "team"), -1) != asInt(py::attr(b.engine.client.get(), "team").get())) return false;
    auto issuer = radioIdentity(b, "human", field(message, "issuer_id"));
    if (!issuer) return false;
    auto key = issuerKey(message);
    const std::int64_t sequence = integer(field(message, "chat_seq"));
    auto& seen = b.startup->chat_seen[key];
    if (sequence <= seen) return false;
    const bool shown = p::flag(py::call(server::radio(b), "receive_team_chat", toPython(field(message, "text")).get(), issuer->second.get()).get());
    if (shown) seen = sequence;
    return shown;
}

// The same-team relay owns the command echo; admission receipts only close the request.
bool teamCommand(Battle& b, const Value& message) {
    if (!radioCurrent(b, message) || integer(field(message, "team"), -1) != asInt(py::attr(b.engine.client.get(), "team").get())) return false;
    auto issuer = radioIdentity(b, "human", field(message, "issuer_id"));
    if (!issuer) return false;
    p::Ref target(p::retain(Py_None));
    if (has(message, "target_id")) {
        auto found = radioIdentity(b, text(field(message, "target_kind")), field(message, "target_id"));
        if (!found) return false;
        replace(target, p::integer(found->first));
    }
    auto key = issuerKey(message);
    const std::int64_t sequence = integer(field(message, "command_seq"));
    auto& seen = b.startup->command_seen[key];
    if (sequence <= seen) return false;
    auto radio_module = module("gui.mods.offline_lan_0922.tactical_radio");
    auto fields = py::call(py::attr(radio_module.get(), "COMMAND_DETAIL_FIELDS").get(), "get", toPython(field(message, "command")).get(),
                           p::Ref(Py_BuildValue("(()())")).get());
    auto details = p::dict();
    p::Ref raw(toPython(message));
    for (Py_ssize_t group = 0; group < 2; ++group) {
        auto names = p::item(fields.get(), group);
        p::Ref iterator(PyObject_GetIter(names.get()));
        while (PyObject* name = PyIter_Next(iterator.get())) {
            p::Ref held(name);
            PyObject* value = PyDict_GetItem(raw.get(), name);
            if (value) PyDict_SetItem(details.get(), name, value);
        }
        if (PyErr_Occurred()) throw p::Error("command details");
    }
    auto receive = py::attr(server::radio(b), "receive_command");
    p::Ref positional(PyTuple_Pack(5, toPython(field(message, "command")).get(), issuer->second.get(), target.get(),
                                   toPython(field(message, "cell_index")).get(), PyDict_Size(details.get()) ? details.get() : Py_None));
    const bool shown = p::flag(p::Ref(PyObject_CallObject(receive.get(), positional.get())).get());
    if (shown) seen = sequence;
    return shown;
}

bool teamCommandTerminal(Battle& b, const Value& message) {
    if (!radioCurrent(b, message)) return false;
    const std::pair<std::string, std::int64_t> key{issuerKey(message).first, integer(field(message, "command_seq"))};
    if (!b.startup->command_terminals.insert(key).second) return false;
    log("BOT ORDER END seq=" + std::to_string(key.second) + " command=" + text(field(message, "command")) + " reason=" + text(field(message, "code")));
    return true;
}

void replayLocal(Battle& b, PyObject* message) {
    auto validated = py::call(module("gui.mods.offline_lan_0922.offline_replay").get(), "validate_local", message);
    gun::applyReplayLocal(b, validated.get());
}

// ---------------------------------------------------------------------------------------------------- replay recording

namespace {
constexpr double kLocalRowSeconds = 1. / 30.;
p::Ref replayModule() { return module("gui.mods.offline_lan_0922.offline_replay"); }
}

// The native recorder starts once per round, after the client is ready and the battle-ready barrier was sent.
void ensureRecording(Battle& b) {
    PyObject* client = b.engine.client.get();
    auto replay = replayModule();
    py::call(replay.get(), "poll_process_status", client);
    auto& s = *b.startup;
    if (b.worker || p::flag(py::optional(client, "is_offline_replay").get()) || !s.client_ready_received || !s.battle_ready_sent) return;
    p::Ref round(toPython(field(b.start, "round_id")));
    if (PyObject_RichCompareBool(py::optional(client, "_offline_replay_attempted").get(), round.get(), Py_EQ) == 1) return;
    py::set(client, "_offline_replay_attempted", round.get());
    auto mode = py::call(replay.get(), "native_recording_mode");
    if (!p::flag(mode.get())) return;
    try {
        auto header = p::dict();
        p::set(header.get(), "welcome", py::optional(client, "_offline_replay_welcome"));
        p::set(header.get(), "start", py::call(replay.get(), "capture_message", toPython(b.start).get()));
        auto last = py::optional(client, "last_snapshot");
        p::set(header.get(), "snapshot", py::call(replay.get(), "capture_message", p::flag(last.get()) ? last.get() : p::dict().get()));
        auto config = p::dict();
        auto keys = py::attr(replay.get(), "CONFIG_KEYS");
        p::Ref iterator(PyObject_GetIter(keys.get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref key(raw);
            const std::string name = p::string(key.get());
            if (has(b.config, name.c_str())) PyDict_SetItem(config.get(), key.get(), toPython(field(b.config, name.c_str())).get());
        }
        if (PyErr_Occurred()) throw p::Error("config keys");
        p::set(header.get(), "config", config);
        p::set(header.get(), "player_id", p::integer(b.local_player));
        p::set(header.get(), "live", py::optional(client, "_offline_replay_live"));
        if (!PyDict_Check(PyDict_GetItemString(header.get(), "welcome"))) throw p::Error("recording has no negotiated welcome");
        py::call(PyDict_GetItemString(header.get(), "snapshot"), "setdefault", p::text("type").get(), p::text("snapshot").get());
        auto os = module("os");
        auto root = py::call(os.get(), "getcwdu");
        p::set(header.get(), "local_descriptor_contract",
               py::call(module("gui.mods.offline_lan_0922.replay_presentation").get(), "descriptor_contract", b.startup->local_descriptor.get()));
        p::set(header.get(), "local_crew_group", p::integer(garage(b).crew_group));
        p::set(header.get(), "overlay_signature", py::call(module("gui.mods.offline_lan_0922.replay_resources").get(), "active_signature", root.get()));
        auto directory = py::call(py::attr(os.get(), "path").get(), "join", root.get(), p::text("replays").get(), p::text("offline").get());
        auto recorder_type = py::attr(module("gui.mods.offline_lan_0922.native_replay").get(), "NativeRecorder");
        auto clock = py::attr(b.engine.bigworld.get(), "time");
        auto recorder = callKw(recorder_type.get(), {directory.get(), mode.get(), header.get()}, {{"clock", clock.get()}});
        py::set(client, "_offline_replay_recorder", recorder.get());
        py::call(recorder.get(), "attach", client);
        py::call(replay.get(), "log", p::text("record_start_requested mode=" + p::string(str(mode.get()).get()) + " native_writer=True path=" +
                                               p::string(str(py::attr(recorder.get(), "path").get()).get())).get());
    } catch (const std::exception& e) {
        py::call(replay.get(), "log", p::text("record_start_failed error=" + describe(e)).get());
    }
}

// One local presentation row per 1/30 s of recording time.
void recordLocal(Battle& b) {
    PyObject* client = b.engine.client.get();
    auto recorder = py::optional(client, "_offline_replay_recorder");
    if (recorder.get() == Py_None || p::flag(py::attr(recorder.get(), "closed").get())) return;
    const double elapsed = b.clock() - asFloat(py::attr(recorder.get(), "started").get());
    if (elapsed - asFloat(py::attr(recorder.get(), "last_pose_at").get()) < kLocalRowSeconds) return;
    try {
        auto row = local::replayState(b);
        p::set(row.get(), "position", p::Ref(Py_BuildValue("[ddd]", b.local->position[0], b.local->position[1], b.local->position[2])));
        Actor* own = b.localVehicle();
        auto entity = own && own->engine_id ? b.entity(own->engine_id) : p::retain(Py_None);
        auto read_int = [&](const char* name) { auto v = py::optional(entity.get(), name); return p::integer(v.get() == Py_None ? 0 : asInt(v.get())); };
        p::set(row.get(), "health", read_int("health"));
        p::set(row.get(), "gun_angles", read_int("gunAngles"));
        auto rotator = py::optional(b.engine.avatar.get(), "gunRotator");
        if (rotator.get() != Py_None) {
            for (const char* name : {"turretYaw", "gunPitch", "dispersionAngle"}) {
                auto v = py::optional(rotator.get(), name);
                const char* key = std::string(name) == "turretYaw" ? "turret_yaw" : std::string(name) == "gunPitch" ? "gun_pitch" : "dispersion_angle";
                p::set(row.get(), key, p::number(v.get() == Py_None ? 0. : asFloat(v.get())));
            }
        }
        auto gun_row = gun::replayState(b);
        if (gun_row.get() != Py_None) p::set(row.get(), "gun", gun_row);
        // Replay keeps the original observer's visibility decisions.
        auto visibility = p::dict();
        for (const auto& entry : b.vehicles->actors) {
            const Actor& v = *entry.second;
            if (v.kind != "bot" || !v.engine_id) continue;
            p::Ref flags(Py_BuildValue("[OO]", v.presented.spot_visible ? Py_True : Py_False, v.presented.spot_marker_visible ? Py_True : Py_False));
            PyDict_SetItemString(visibility.get(), entry.first.c_str(), flags.get());
        }
        p::set(row.get(), "visibility", visibility);
        py::call(recorder.get(), "append", p::text("local").get(), row.get());
        py::set(recorder.get(), "last_pose_at", p::number(elapsed).get());
    } catch (const std::exception& e) {
        const std::string error = describe(e);
        try { py::call(recorder.get(), "abort"); } catch (const std::exception&) { PyErr_Clear(); }
        py::set(client, "_offline_replay_recorder", Py_None);
        py::call(replayModule().get(), "log", p::text("local_record_failed error=" + error).get());
    }
}

void finishRecording(Battle& b, const char* reason) {
    if (!b.engine.client) return;
    py::call(replayModule().get(), "finish", b.engine.client.get(), p::text(reason).get());
}

// ---------------------------------------------------------------------------------------------------- PERF

// One PERF line per window: frame count, mean and worst frame, and the mean of each frame stage.
void diagnosticsFrame(Battle& b, double entry_wall, double frame_seconds, const std::map<std::string, double>& stages) {
    if (!b.diagnostics) return;
    auto& d = *b.diagnostics;
    if (!d.enabled) { d.enabled = true; d.window_start = entry_wall; }
    ++d.frames;
    d.total_frame += frame_seconds;
    d.worst_frame = std::max(d.worst_frame, frame_seconds);
    for (const auto& stage : stages) d.stages[stage.first] += stage.second;
    if (entry_wall - d.window_start < d.window_seconds || !d.frames) return;
    std::string line = "PERF " + std::string(b.worker ? "worker" : "client") + " frames=" + std::to_string(d.frames) +
                       " fps=" + std::to_string(double(d.frames) / std::max(1.e-6, entry_wall - d.window_start)) +
                       " mean_ms=" + std::to_string(d.total_frame * 1000. / double(d.frames)) + " worst_ms=" + std::to_string(d.worst_frame * 1000.);
    for (const auto& stage : d.stages) line += " " + stage.first + "_ms=" + std::to_string(stage.second * 1000. / double(d.frames));
    log(line);
    d.window_start = entry_wall;
    d.frames = 0;
    d.total_frame = d.worst_frame = 0.;
    d.stages.clear();
}
}
}
