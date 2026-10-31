// The vehicle table: replica events applied to stock entities, and every per-vehicle presentation edge.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace vehicles {
namespace {
const Value& field(const Value& v, const char* name) { return v.resolved().get(name); }
std::int64_t integer(const Value& v, std::int64_t fallback = 0) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Integer) return r.integer;
    if (r.kind == Value::Kind::Null) return fallback;
    return std::int64_t(r.numeric(double(fallback)));
}
bool has(const Value& v, const char* name) {
    const auto& r = v.resolved();
    return r.kind == Value::Kind::Object && r.object.count(name);
}
Value boolean(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
Value number(std::int64_t v) { Value x; x.kind = Value::Kind::Integer; x.integer = v; return x; }
// A writable copy of the presented state (accepted rows are shared and immutable).
Value& writable(Value& state) {
    if (state.reference) { Value copy = *state.reference; state = std::move(copy); }
    if (state.kind != Value::Kind::Object) state.kind = Value::Kind::Object;
    return state;
}
// state.update(incoming)
void merge(Value& state, const Value& incoming) {
    const auto& source = incoming.resolved();
    if (source.kind != Value::Kind::Object) return;
    auto& target = writable(state);
    for (const auto& item : source.object) target.object[item.first] = item.second;
}
p::Ref engineId(const Actor& v) { return p::integer(v.engine_id); }
void removeKey(std::vector<std::string>& order, const std::string& key) {
    order.erase(std::remove(order.begin(), order.end(), key), order.end());
}
}

// ---- queries ----------------------------------------------------------------------------------------------

p::Ref entity(const Battle& b, const Actor& vehicle) { return b.entity(vehicle.engine_id); }

bool alive(const Battle& b, const Actor& vehicle, PyObject* entity) {
    const auto& state = vehicle.presented.state.resolved();
    if (has(state, "alive")) return field(state, "alive").truth() && integer(field(state, "health"), 1) > 0;
    auto value = py::optional(entity, "isAlive");
    if (PyCallable_Check(value.get())) return p::flag(py::invoke(value.get()).get());
    return p::flag(value.get());
}

bool spotted(const Actor& vehicle) { return vehicle.presented.spot_marker_visible; }

Point position(const Battle& b, const Actor& vehicle) {
    if (vehicle.local && b.local) return b.local->position;
    const auto& state = vehicle.presented.state.resolved();
    if (has(state, "x") && has(state, "y") && has(state, "z"))
        return {field(state, "x").numeric(), field(state, "y").numeric(), field(state, "z").numeric()};
    auto found = entity(b, vehicle);
    if (found.get() == Py_None) throw p::Error("combat presentation entity is unavailable");
    auto at = py::attr(found.get(), "position");
    return {p::scalar(py::attr(at.get(), "x").get()), p::scalar(py::attr(at.get(), "y").get()), p::scalar(py::attr(at.get(), "z").get())};
}

// ---- descriptors and placement -----------------------------------------------------------------------------

std::int64_t attackReason(Battle& b, const char* member, std::int64_t fallback) {
    auto group = py::optional(b.engine.constants.get(), "ATTACK_REASON");
    auto indices = py::optional(b.engine.constants.get(), "ATTACK_REASON_INDICES");
    auto name = py::optional(group.get(), member);
    PyObject* index = indices.get() != Py_None ? PyObject_GetItem(indices.get(), name.get()) : nullptr;
    if (!index) { PyErr_Clear(); return fallback; }
    p::Ref held(index);
    PyObject* converted = PyNumber_Int(index);
    if (!converted) { PyErr_Clear(); return fallback; }
    p::Ref value(converted);
    return p::wide(value.get());
}

p::Ref vector(Battle& b, const Point& at) {
    return py::call(b.engine.math.get(), "Vector3", p::number(at[0]).get(), p::number(at[1]).get(), p::number(at[2]).get());
}

// BigWorld rotation vector in roll, pitch, yaw order.
p::Ref rotation(double yaw, double pitch, double roll) {
    return p::Ref(Py_BuildValue("(ddd)", roll, pitch, yaw));
}

// The remote outfit a human published for this arena's season; '' otherwise.
p::Ref remoteOutfit(Battle& b, const Value& state, bool player) {
    if (!player || !b.startup || !b.startup->arena_type) return p::text("");
    auto season = outfitSeason(b);
    const auto& outfits = field(state, "outfits");
    if (!season || outfits.resolved().kind != Value::Kind::Object) return p::text("");
    const auto& encoded = outfits.resolved().get(std::to_string(*season));
    if (encoded.resolved().kind != Value::Kind::Text || encoded.resolved().text.empty()) return p::text("");
    auto module = p::Ref(PyImport_ImportModule("base64"));
    PyObject* raw = PyObject_CallMethod(module.get(), const_cast<char*>("b64decode"), const_cast<char*>("s"), encoded.resolved().text.c_str());
    if (!raw) { PyErr_Clear(); return p::text(""); }
    p::Ref decoded(raw);
    const Py_ssize_t size = PyString_Size(decoded.get());
    if (size <= 0 || size > 64 * 1024) return p::text("");
    p::Ref again(PyObject_CallMethod(module.get(), const_cast<char*>("b64encode"), const_cast<char*>("O"), decoded.get()));
    if (p::string(again.get()) != encoded.resolved().text) return p::text("");
    return decoded;
}

// Same-team LAN humans share the native squad identity.
std::int64_t prebattleId(Battle& b, const Value& state, bool player) {
    if (!player) return 0;
    const std::int64_t team = integer(field(state, "team"));
    std::size_t members = 0;
    for (const auto& row : field(b.start, "players").resolved().array)
        if (integer(field(row, "team")) == team) ++members;
    return (team == 1 || team == 2) && members >= 2 ? team : 0;
}

std::int64_t marksOnGun(Battle& b, const Value& raw) {
    const auto& v = raw.resolved();
    if (v.kind != Value::Kind::Integer && v.kind != Value::Kind::Number && v.kind != Value::Kind::Text && v.kind != Value::Kind::Null) return 0;
    const std::int64_t marks = v.kind == Value::Kind::Null ? 0 : std::int64_t(v.numeric(0.));
    return std::max<std::int64_t>(0, std::min<std::int64_t>(marks, 3));
}

// ---- creation and readiness --------------------------------------------------------------------------------

// BattleRuntime._create_remote(event): one remote vehicle entity for a replica create event.
void create(Battle& b, Actor& v, const Value& event) {
    if (v.presented.remote || v.engine_id) return;
    Value state = field(event, "state");
    const Value initial = has(event, "initial_state") ? field(event, "initial_state") : state;
    if (!has(state, "team") || !has(state, "slot")) return;
    const bool bot = v.kind == "bot", player = v.kind == "player";
    if (bot && (!has(state, "x") || !has(state, "z"))) return;
    auto descriptor = player && (b.worker || field(state, "vehicle_compact_descr").truth())
        ? playerDescriptor(b, state)
        : descriptorFor(b, has(state, "vehicle") ? field(state, "vehicle").resolved().text : field(b.config, "vehicle").resolved().text);
    const std::int64_t team = integer(field(state, "team"), 1);
    const std::string name = has(state, "name") ? field(state, "name").resolved().text : "Vehicle";
    auto compact = py::call(descriptor.get(), "makeCompactDescr");
    auto properties = py::call(b.engine.binding.get(), "properties_from_compact_descr", compact.get(), p::integer(team).get(), p::text(name).get());
    p::Ref info(PyObject_GetItem(properties.get(), p::text("publicInfo").get()));
    auto put = [&](PyObject* into, const char* key, p::Ref value) {
        if (PyObject_SetItem(into, p::text(key).get(), value.get()) < 0) throw p::Error("publicInfo write failed");
    };
    put(info.get(), "outfit", remoteOutfit(b, state, player));
    put(info.get(), "prebattleID", p::integer(prebattleId(b, state, player)));
    put(info.get(), "marksOnGun", p::integer(player ? marksOnGun(b, field(state, "marks_on_gun")) : 0));
    const std::int64_t cap = p::wide(py::attr(descriptor.get(), "maxHealth").get());
    const std::int64_t health = has(initial, "health") ? integer(field(initial, "health")) : cap;
    put(properties.get(), "health", p::integer(std::max<std::int64_t>(0, std::min(health, cap))));
    const auto placed = worldPose(b, state);
    const std::int64_t engine = remote::create(b, descriptor.get(), properties.get(), placed.first, placed.second);
    if (!engine) throw p::Error("remote presentation returned no vehicle id");
    remote::setInterpolateMotion(b, engine, bot && botAuthorityIsLocal(b, field(b.start, "bot_authority_id")));
    // The live remote factory always creates native #1513 Vehicle entities.
    const bool native_entities = true;
    auto& d = v.presented;
    v.engine_id = engine;
    d.state = state;
    d.network_id = integer(field(event, "id"));
    d.remote = true;
    v.ready = false;
    d.arena_added = native_entities;
    v.native_remote = native_entities;
    d.properties = own(std::move(properties));
    const bool visible = botInitiallyVisible(b, team);
    d.spot_visible = d.spot_marker_visible = visible;
    d.spot_until = d.radio_spot_until = d.spot_next = 0.;
    d.shot_penalty_until = field(state, "shot_penalty_until").numeric(0.);
    d.ready_deadline = b.clock() + field(b.config, "startupTimeoutSeconds").numeric(30.);
    ++b.roster->revision;
    Value seed = state;
    auto& seeded = writable(seed);
    seeded.object["x"] = Value(); seeded.object["x"].kind = Value::Kind::Number; seeded.object["x"].number = placed.first[0];
    seeded.object["y"] = Value(); seeded.object["y"].kind = Value::Kind::Number; seeded.object["y"].number = placed.first[1];
    seeded.object["z"] = Value(); seeded.object["z"].kind = Value::Kind::Number; seeded.object["z"].number = placed.first[2];
    seeded.object["yaw"] = Value(); seeded.object["yaw"].kind = Value::Kind::Number; seeded.object["yaw"].number = placed.second;
    d.collision_pose = projectiles::boundaryPose(b, placed.first, seed);
    materialize(b, v);
}

// Readiness of the stock entity behind a vehicle; false while its compound still loads.
bool ready(Battle& b, Actor& v) {
    if (v.ready) return true;
    auto id = engineId(v);
    if (v.presented.remote) {
        if (!remote::isReady(b, v.engine_id)) return false;
        v.ready = true;
        return true;
    }
    auto getter = py::optional(b.engine.server.get(), "vehicleEnterStatus");
    std::string phase = "completed";
    if (py::callable(getter)) {
        auto status = py::invoke(getter.get(), id.get());
        phase = p::string(p::item(status.get(), 0).get());
        if (phase == "failed")
            throw p::Error("Vehicle " + std::to_string(v.engine_id) + " enter failed: " +
                           p::string(p::Ref(PyObject_Str(p::item(status.get(), 1).get())).get()));
    }
    if (phase != "completed" || !p::flag(py::call(b.engine.binding.get(), "is_vehicle_ready", id.get()).get())) return false;
    v.ready = true;
    return true;
}

// BattleRuntime._materialize_record(record): finish readiness, then present the vehicle's current state.
bool materialize(Battle& b, Actor& v) {
    if (!ready(b, v)) return false;
    auto& d = v.presented;
    auto id = engineId(v);
    p::Ref vehicle(p::retain(Py_None));
    if (d.remote) {
        replace(vehicle, remote::get(b, v.engine_id));
        if (vehicle.get() == Py_None || py::attr(vehicle.get(), "model").get() == Py_None) throw p::Error("remote vehicle has no ready presentation");
        if (!d.initialized) {
            const bool initially = d.spot_visible;
            py::set(vehicle.get(), "_spot_visible", initially ? Py_True : Py_False);
            if (b.worker) d.simulation_entity = true;
            else if (v.native_remote) {
                py::set(vehicle.get(), "_offlineNativeDrawVisible", initially ? Py_True : Py_False);
                remote::setDrawVisibility(b, vehicle.get(), initially);
                py::set(vehicle.get(), "targetCaps", p::list().get());
                auto marker = py::optional(vehicle.get(), "_offlineNativeMarkerVisible");
                const bool started = marker.get() == Py_None && !PyObject_HasAttrString(vehicle.get(), "_offlineNativeMarkerVisible")
                    ? initially : p::flag(marker.get());
                d.world_marker_started = d.minimap_started = started;
                py::set(vehicle.get(), "_offlineNativeMarkerVisible", started ? Py_True : Py_False);
            } else {
                py::call(py::attr(vehicle.get(), "appearance").get(), "changeVisibility", initially ? Py_True : Py_False);
            }
            d.initialized = true;
        }
    }
    if (!b.worker && d.remote && !d.arena_added) {
        auto info = p::dict();
        PyDict_SetItemString(info.get(), "properties", d.properties.get());
        p::set(info.get(), "team_killer", p::boolean(field(d.state, "team_killer").truth()));
        py::call(b.engine.binding.get(), "arena_vehicle_added", id.get(), info.get());
        d.arena_added = true;
        startup::startTeamChatWhenRosterReady(b);
    }
    if (!b.worker && d.remote && !v.native_remote && d.arena_added && d.spot_visible && !(d.world_marker_started || d.minimap_started)) {
        py::call(b.engine.binding.get(), "start_vehicle_visual", id.get(), Py_True);
        d.world_marker_started = d.minimap_started = true;
        if (!alive(b, v, vehicle.get())) presentDead(b, v, true);
    }
    if (d.remote && !b.worker) setSpotVisibility(b, v, d.spot_visible, d.spot_marker_visible);
    if (d.pending_pose) {
        const Pose pose = *d.pending_pose;
        d.pending_pose.reset();
        applyPose(b, v, pose);
    }
    applySiege(b, v, d.state);
    applyStatistics(b, v, d.state);
    events::drain(b);
    if (events::pendingCombatFor(b, v)) return true;
    applyStun(b, v, d.state);
    const auto& critical = field(d.state, "critical");
    if (critical.resolved().kind == Value::Kind::Object) {
        Value authority = d.state;
        applyCritical(b, v, critical, &authority);
    } else if (v.kind == "player" && has(d.state, "critical_revision") && has(d.state, "critical_base_revision") && has(d.state, "critical_ack_seq")) {
        reconcileCriticalAuthority(b, v, d.state, nullptr);
    }
    HealthEdge edge;
    edge.attacker = deathAttackerEngineId(b, d.state);
    edge.reason = std::max<std::int64_t>(0, integer(field(d.state, "death_reason")));
    edge.suppress_postmortem_killer = suppressPostmortemKiller(b, v, d.state, edge.attacker);
    Value state = d.state;
    applyHealth(b, v, state, edge);
    return true;
}
}
}
