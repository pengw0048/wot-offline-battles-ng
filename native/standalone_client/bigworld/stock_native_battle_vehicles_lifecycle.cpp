// Replica lifecycle events: create, update and destroy of stock vehicle entities, Bot create pacing, tombstones.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include <algorithm>

namespace native_battle {
namespace vehicles {
namespace {
const Value& field(const Value& v, const char* name) { return v.resolved().get(name); }
bool presented(const Actor& v) { return v.engine_id != 0; }
Value object() { Value v; v.kind = Value::Kind::Object; return v; }
Value text(const std::string& s) { Value v; v.kind = Value::Kind::Text; v.text = s; return v; }
Value& writable(Value& state) {
    if (state.reference) { Value copy = *state.reference; state = std::move(copy); }
    if (state.kind != Value::Kind::Object) state.kind = Value::Kind::Object;
    return state;
}
void merge(Value& state, const Value& incoming) {
    const auto& source = incoming.resolved();
    if (source.kind != Value::Kind::Object) return;
    auto& target = writable(state);
    for (const auto& item : source.object) target.object[item.first] = item.second;
}
std::optional<Pose> poseOf(const Value& raw) {
    const auto& s = raw.resolved();
    if (s.kind != Value::Kind::Object || !s.object.count("x") || !s.object.count("y") || !s.object.count("z")) return {};
    Pose out{};
    unsigned i = 0;
    for (const char* n : {"x", "y", "z", "yaw", "pitch", "roll", "aim_yaw", "gun_pitch"}) out[i++] = s.get(n).numeric();
    if (!s.object.count("aim_yaw")) out[6] = out[3];
    return out;
}
// The base state with the destroy event's incoming state, marked dead.
Value deadState(const Value& base, const Value& event) {
    Value state = base;
    merge(state, field(event, "state"));
    auto& out = writable(state);
    out.object["health"] = Value(); out.object["health"].kind = Value::Kind::Integer; out.object["health"].integer = 0;
    out.object["alive"] = Value(); out.object["alive"].kind = Value::Kind::Boolean; out.object["alive"].boolean = false;
    return state;
}
bool entityVisible(Battle& b, const Actor& v) {
    try { return b.entity(v.engine_id).get() != Py_None; }
    catch (const p::Error&) {
        if (!PyErr_ExceptionMatches(PyExc_ReferenceError)) throw;
        PyErr_Clear();
        return false;
    }
}
// arena_vehicle_removed, then destroy_entity even when the first call raised.
void removeServerEntity(Battle& b, std::int64_t engine_id) {
    auto id = p::integer(engine_id);
    try {
        py::call(b.engine.binding.get(), "arena_vehicle_removed", id.get());
    } catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        py::call(b.engine.binding.get(), "destroy_entity", id.get());
        PyErr_Restore(type, value, trace);
        throw;
    }
    py::call(b.engine.binding.get(), "destroy_entity", id.get());
}
void clearOutlineOf(Battle& b, const Actor& v) {
    if (b.visibility && b.visibility->outlined_engine_id == v.engine_id) visibility::clearOutline(b);
}
}

// BattleRuntime._apply_sync_event(event)
void apply(Battle& b, const Value& event) {
    if (b.phase == Phase::Failed || b.phase == Phase::Stopped || b.phase == Phase::Leaving) return;
    const std::string type = field(event, "type").resolved().text;
    const std::string key = field(event, "entity").resolved().text;
    const bool bot = field(event, "kind").resolved().text == "bot";
    Actor* v = b.vehicle(key);
    if (type == "create") {
        if (bot) queueBotCreate(b, event);
        else if (v) create(b, *v, event);
    } else if (type == "update") {
        if (bot && (!v || !presented(*v))) queueBotCreate(b, event);
        else if (v) {
            if (bot && b.worker) {
                Value copy = event;
                writable(copy).object.erase("pose");
                update(b, *v, copy);
            } else update(b, *v, event);
        }
    } else if (type == "destroy") {
        destroy(b, key, event);
    }
}

// BattleRuntime._update_entity(event)
void update(Battle& b, Actor& v, const Value& event) {
    if (presented(v) && v.tombstone) return;
    if (!presented(v)) {
        auto created = object();
        created.object["type"] = text("create");
        created.object["entity"] = field(event, "entity");
        created.object["kind"] = field(event, "kind");
        created.object["id"] = field(event, "id");
        created.object["state"] = field(event, "state").truth() ? field(event, "state") : object();
        create(b, v, created);
        if (!presented(v)) return;
    }
    auto& d = v.presented;
    const auto pose = poseOf(field(event, "pose"));
    const auto& incoming = field(event, "state");
    const auto& stamp = field(event, "presentation_time_us");
    auto time = [&]() -> std::optional<std::int64_t> {
        if (stamp.resolved().kind == Value::Kind::Null) return {};
        return std::int64_t(stamp.numeric());
    };
    if (field(event, "interpolated").truth() && !incoming.truth()) {
        if (!pose) return;
        if (v.ready) {
            d.presented_pose = *pose;
            d.presented_time_us = time();
            applyPose(b, v, *pose);
            return;
        }
    }
    merge(d.state, incoming);
    if (pose) {
        d.pending_pose = *pose;
        d.presented_pose = *pose;
        d.presented_time_us = time();
    }
    materialize(b, v);
}

// BattleRuntime._destroy_entity(event)
void destroy(Battle& b, const std::string& key, const Value& event) {
    Actor* found = b.vehicle(key);
    auto& roster = *b.roster;
    if (found && found->local) {
        found->presented.state = deadState(found->presented.state, event);
        materialize(b, *found);
        return;
    }
    if (!found || !presented(*found)) {
        auto pending = roster.pending_bot_creates.find(key);
        if (pending == roster.pending_bot_creates.end()) return;
        if (field(event, "keep_corpse").truth()) {
            auto& entry = writable(pending->second);
            entry.object["state"] = deadState(field(pending->second, "state"), event);
            return;
        }
        if (events::references(b, key)) throw p::Error("pending entity " + key + " was removed before its ordered event applied");
        roster.pending_bot_creates.erase(pending);
        roster.pending_order.erase(std::remove(roster.pending_order.begin(), roster.pending_order.end(), key), roster.pending_order.end());
        return;
    }
    Actor& v = *found;
    auto& d = v.presented;
    if (!b.worker) feedback::fallbackPostmortemViewpoint(b, v.engine_id);
    if (field(event, "keep_corpse").truth()) {
        d.state = deadState(d.state, event);
        materialize(b, v);
        return;
    }
    if (d.remote) {
        clearOutlineOf(b, v);
        const std::int64_t engine_id = v.engine_id;
        const bool arena_added = d.arena_added, native_remote = v.native_remote;
        if (!native_remote) stopRemoteVisual(b, v);
        retire(b, v);
        if (arena_added) py::call(b.engine.binding.get(), "arena_vehicle_removed", p::integer(engine_id).get());
        remote::destroy(b, engine_id);
        return;
    }
    if (v.ready) {
        const std::int64_t engine_id = v.engine_id;
        retire(b, v);
        auto forget = py::optional(b.engine.server.get(), "forgetVehicleEnter");
        if (py::callable(forget)) py::invoke(forget.get(), p::integer(engine_id).get());
        Actor probe;
        probe.engine_id = engine_id;
        if (entityVisible(b, probe)) removeServerEntity(b, engine_id);
        return;
    }
    v.tombstone = true;
    d.pending_pose.reset();
    d.destroy_requested = false;
    if (entityVisible(b, v)) flushTombstone(b, v);
}

// Forget a vehicle's presentation: it no longer has a stock entity.
void retire(Battle& b, Actor& v) {
    v.engine_id = 0;
    v.ready = false;
    v.presented = cs::Presentation();
    ++b.roster->revision;
}

// BattleRuntime._flush_tombstone(record): destroy a vehicle that entered after its removal.
void flushTombstone(Battle& b, Actor& v) {
    auto& d = v.presented;
    if (d.remote) {
        clearOutlineOf(b, v);
        if (!v.native_remote) stopRemoteVisual(b, v);
        remote::destroy(b, v.engine_id);
        d.destroy_requested = true;
        return;
    }
    if (d.destroy_requested || !entityVisible(b, v)) return;
    removeServerEntity(b, v.engine_id);
    d.destroy_requested = true;
}

// BattleRuntime._queue_bot_create(event): coalesce one Bot until its own createEntity callback.
bool queueBotCreate(Battle& b, const Value& event) {
    const std::string key = field(event, "entity").resolved().text;
    if (key.empty()) return false;
    if (Actor* v = b.vehicle(key)) if (presented(*v)) return false;
    Value state = field(event, "state").truth() ? field(event, "state") : object();
    const auto& pose = field(event, "pose").resolved();
    if (pose.kind == Value::Kind::Object)
        for (const char* name : {"x", "y", "z", "yaw", "pitch", "roll", "aim_yaw", "turret_yaw", "gun_pitch", "siege_state"})
            if (pose.object.count(name)) writable(state).object[name] = pose.get(name);
    auto& roster = *b.roster;
    auto pending = roster.pending_bot_creates.find(key);
    if (pending == roster.pending_bot_creates.end()) {
        auto created = object();
        created.object["type"] = text("create");
        created.object["entity"] = text(key);
        created.object["kind"] = text("bot");
        created.object["id"] = field(event, "id");
        created.object["state"] = state;
        created.object["initial_state"] = state;
        roster.pending_bot_creates.emplace(key, std::move(created));
        roster.pending_order.push_back(key);
    } else {
        auto& entry = writable(pending->second);
        merge(entry.object["state"], state);
    }
    return true;
}

// BattleRuntime._flush_pending_bot_create(now): one queued Bot per render callback, alternating teams.
void flushPendingBotCreate(Battle& b, double now) {
    auto& roster = *b.roster;
    if (roster.pending_order.empty()) return;
    if (!roster.bot_create_started) roster.bot_create_started = now;
    std::string key = roster.pending_order.front();
    if (roster.last_bot_team) {
        for (const auto& candidate : roster.pending_order) {
            auto found = roster.pending_bot_creates.find(candidate);
            if (found == roster.pending_bot_creates.end()) continue;
            const auto& team = field(field(found->second, "state"), "team");
            if (team.resolved().kind != Value::Kind::Null && std::int64_t(team.numeric()) != *roster.last_bot_team) { key = candidate; break; }
        }
    }
    roster.pending_order.erase(std::find(roster.pending_order.begin(), roster.pending_order.end(), key));
    auto found = roster.pending_bot_creates.find(key);
    if (found == roster.pending_bot_creates.end()) return;
    const Value event = std::move(found->second);
    roster.pending_bot_creates.erase(found);
    Actor* v = b.vehicle(key);
    if (!v || presented(*v)) return;
    create(b, *v, event);
    if (!presented(*v)) return;
    const auto& team = field(field(event, "state"), "team");
    if (team.resolved().kind != Value::Kind::Null) roster.last_bot_team = std::int64_t(team.numeric());
    if (roster.pending_order.empty() && !roster.bots_ready_reported) {
        roster.bots_ready_reported = true;
        roster.bot_create_finished = now;
        log("bots ready after " + std::to_string(now - roster.bot_create_started.value_or(now)) + " s");
    }
}

// BattleRuntime._flush_pending_entities(now): readiness of entities still loading, and their tombstones.
void flushPendingEntities(Battle& b, double now) {
    for (auto& entry : b.vehicles->actors) {
        Actor& v = *entry.second;
        if (!presented(v)) continue;
        if (v.tombstone) { flushTombstone(b, v); continue; }
        if (v.ready) continue;
        if (materialize(b, v)) continue;
        if (v.presented.ready_deadline > 0. && now >= v.presented.ready_deadline)
            throw p::Error("Vehicle " + std::to_string(v.engine_id) + " did not enter world before timeout");
    }
}
// The player's own vehicle entered the arena (the pinned local record: local, not ready).
void installLocal(Battle& b, std::int64_t engine_id, const Value& local_state) {
    const std::string key = "player:" + std::to_string(b.local_player);
    auto& entry = b.vehicles->actors[key];
    if (!entry) {
        entry = std::make_shared<Actor>();
        entry->key = key;
        entry->kind = "player";
        entry->id = b.local_player;
    }
    entry->local = true;
    entry->engine_id = engine_id;
    entry->ready = false;
    entry->presented = cs::Presentation();
    entry->presented.state = local_state;
    entry->presented.network_id = b.local_player;
    ++b.roster->revision;
}

void markLocalReady(Battle& b) {
    if (Actor* own = b.localVehicle()) own->ready = true;
}

// Teardown: every remote entity leaves, then the table is forgotten.
void quiesce(Battle& b) {
    if (b.remote) remote::destroyAll(b);
    for (auto& entry : b.vehicles->actors) {
        entry.second->engine_id = 0;
        entry.second->ready = false;
        entry.second->presented = cs::Presentation();
    }
    b.vehicles->actors.clear();
    if (b.roster) {
        b.roster->pending_bot_creates.clear();
        b.roster->pending_order.clear();
        ++b.roster->revision;
    }
}

void receiveManifest(Battle& b, const Value& message) { remote::receiveManifest(b, message); }
void receiveSnapshot(Battle& b, const Value& message, double now) { remote::receiveSnapshot(b, message, now); }
// The raw LAN snapshot: the replica also consumes its receive-thread `_native_snapshot` capsule.
void receiveSnapshotMessage(Battle& b, PyObject* message, double now) {
    remote::receiveSnapshot(b, remote::readMessage(b, message, false), now);
}
void advance(Battle& b, double now) { remote::advance(b, now); }
}
}
