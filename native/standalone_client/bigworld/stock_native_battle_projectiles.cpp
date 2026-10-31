// Projectile presentation and the local gun's stock edges: canonical launch metas, native tracers, impacts and
// explosions, authority epochs and the server-time anchor, fire intents and bursts, HUD and reload-sound edges.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_projectiles.hpp"
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_client_weapon_python.hpp"
#include "stock_native_projectile_wire.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace {
namespace shot = stock_native_projectile;
namespace weapon = stock_native_client_weapon_runtime;

// ---- Value and engine helpers -------------------------------------------------------------------------------------
Value number(double v) { Value x; x.kind = Value::Kind::Number; x.number = v; return x; }
Value integer(std::int64_t v) { Value x; x.kind = Value::Kind::Integer; x.integer = v; return x; }
Value boolean(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
Value text(std::string v) { Value x; x.kind = Value::Kind::Text; x.text = std::move(v); return x; }
Value object() { Value x; x.kind = Value::Kind::Object; return x; }
Value points(const Point& v) { Value x; x.kind = Value::Kind::Array; for (double c : v) x.array.push_back(number(c)); return x; }
bool present(const Value& v, const char* key) { const auto& r = v.resolved(); return r.kind == Value::Kind::Object && r.object.count(key); }
bool isNull(const Value& v) { return v.resolved().kind == Value::Kind::Null; }
std::optional<std::int64_t> whole(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Integer) return r.integer;
    if (r.kind == Value::Kind::Number && std::isfinite(r.number) && std::floor(r.number) == r.number) return std::int64_t(r.number);
    if (r.kind == Value::Kind::Text) {
        try { std::size_t end = 0; const auto n = std::stoll(r.text, &end); if (end == r.text.size()) return n; } catch (...) {}
    }
    return std::nullopt;
}
std::string idText(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Text) return r.text;
    if (r.kind == Value::Kind::Integer) return std::to_string(r.integer);
    if (r.kind == Value::Kind::Number) return std::to_string(std::int64_t(r.number));
    return {};
}
std::optional<Point> pointOf(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind != Value::Kind::Array || r.array.size() < 3) return std::nullopt;
    Point out;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& c = r.array[i].resolved();
        if (c.kind != Value::Kind::Integer && c.kind != Value::Kind::Number) return std::nullopt;
        out[i] = c.numeric();
        if (!std::isfinite(out[i])) return std::nullopt;
    }
    return out;
}
p::Ref tuple(const Point& v) { return p::Ref(Py_BuildValue("(ddd)", v[0], v[1], v[2])); }
p::Ref vector3(const Battle& b, const Point& v) { return py::call(b.engine.math.get(), "Vector3", p::number(v[0]).get(), p::number(v[1]).get(), p::number(v[2]).get()); }
// start + velocity * t + gravity * t^2 / 2
Point trajectory(const Point& start, const Point& velocity, double gravity, double elapsed) {
    return {start[0] + velocity[0] * elapsed, start[1] + velocity[1] * elapsed - .5 * gravity * elapsed * elapsed, start[2] + velocity[2] * elapsed};
}
std::string current(const std::exception& e) { return describe(e); }

// ---- Ownership -----------------------------------------------------------------------------------------------------
bool owns(const Battle& b, const Value& launch) {
    if (!projectiles::isAuthority(b)) return false;
    const auto kind = launch.get("shooter_kind").resolved().text;
    if (b.worker) return kind == "bot";
    return kind == "player" && whole(launch.get("shooter_id")) == b.local_player;
}
bool nativeEnabled(const Battle& b) { return !b.worker && !b.replay && b.vehicles; }

// The collision-confirmed age of one visual segment.
double visualAge(const Value& launch) {
    const auto start = whole(launch.get("segment_start_time_ms")).value_or(0);
    std::int64_t through = start;
    if (present(launch, "base_checked_ms")) through = whole(launch.get("base_checked_ms")).value_or(start);
    else if (present(launch, "checked_through_ms")) through = whole(launch.get("checked_through_ms")).value_or(start);
    const auto maximum_ms = whole(launch.get("max_time_ms")).value_or(10000);
    const double maximum = std::max(0., double(maximum_ms - start) / 1000.);
    const double age = double(through - start) / 1000.;
    const double bounded = age < maximum ? age : maximum;
    return bounded > 0. ? bounded : 0.;
}

PlayerProjectiles* bindPlayer(Battle& b, double now) {
    auto& s = *b.projectiles;
    if (!s.player || !s.player->current(b)) {
        // The opening battle_start is observed before the final running edge; scene and query owners install here.
        needs::prepareDrive(b);
        if (!needs::bindQuery(b)) throw p::Error("Native player projectile query is unavailable");
        s.player = createPlayerProjectiles(b, now);
    }
    return s.player.get();
}

// ---- Shot effects and descriptors ----------------------------------------------------------------------------------
p::Ref descriptorShot(PyObject* descriptor, std::optional<std::int64_t> shell_index) {
    auto gun = py::optional(descriptor, "gun"), raw = py::optional(gun.get(), "shots");
    p::Ref shots(PySequence_Tuple(p::flag(raw.get()) ? raw.get() : p::Ref(PyTuple_New(0)).get()));
    std::int64_t index = 0;
    if (shell_index) index = *shell_index;
    else {
        auto active = py::optional(descriptor, "activeGunShotIndex");
        index = active.get() == Py_None ? 0 : p::wide(active.get());
    }
    const Py_ssize_t count = PyTuple_GET_SIZE(shots.get());
    if (!count) return p::retain(Py_None);
    index = std::max<std::int64_t>(0, std::min<std::int64_t>(index, count - 1));
    return p::retain(PyTuple_GET_ITEM(shots.get(), Py_ssize_t(index)));
}
p::Ref sourceEntity(Battle& b, const Value& launch) {
    auto* actor = b.vehicle(launch.get("shooter_kind").resolved().text + ":" + idText(launch.get("shooter_id")));
    return actor ? b.entity(actor->engine_id) : p::retain(Py_None);
}
p::Ref sourceDescriptor(Battle& b, ProjectileMeta* meta, const Value& launch) {
    if (meta && meta->source_descriptor) return p::retain(meta->source_descriptor.get());
    const auto vehicle = launch.get("source_vehicle").resolved().text;
    if (vehicle.empty()) return p::retain(Py_None);
    try {
        auto resolved = needs::resolveDescriptor(b, vehicle);
        if (meta && resolved.get() != Py_None) meta->source_descriptor = borrow(resolved.get());
        return resolved;
    } catch (const std::exception&) { PyErr_Clear(); return p::retain(Py_None); }
}
// The launch's shot effects index: the shooter's mounted descriptor shell, else the frozen source shell.
p::Ref effectsIndex(Battle& b, ProjectileMeta& meta) {
    auto source = sourceEntity(b, meta.launch);
    auto mounted_descriptor = source.get() != Py_None ? py::optional(source.get(), "typeDescriptor") : p::retain(Py_None);
    auto descriptor = mounted_descriptor.get() != Py_None ? std::move(mounted_descriptor) : sourceDescriptor(b, &meta, meta.launch);
    if (descriptor.get() != Py_None) {
        auto mounted = descriptorShot(descriptor.get(), whole(meta.launch.get("shell_index")));
        auto shell = py::optional(mounted.get(), "shell");
        if (PyDict_Check(shell.get())) {
            auto* found = PyDict_GetItemString(shell.get(), "effectsIndex");
            if (found && found != Py_None) return p::retain(found);
        } else {
            auto found = py::optional(shell.get(), "effectsIndex");
            if (found.get() != Py_None) return found;
        }
    }
    const auto& frozen = meta.launch.get("source_shot").get("shell").get("effectsIndex");
    return isNull(frozen) ? p::retain(Py_None) : p::Ref(cs::write(frozen));
}
p::Ref shotEffects(Battle& b, PyObject* index) {
    auto cache = py::attr(b.engine.vehicles.get(), "g_cache");
    p::Ref integral(PyNumber_Int(index));
    return p::Ref(PyObject_GetItem(py::attr(cache.get(), "shotEffects").get(), integral.get()));
}
bool lookupError() {
    return PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_IndexError) ||
           PyErr_ExceptionMatches(PyExc_KeyError) || PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError);
}
bool admittedVisual(const Projectiles& s, const std::string& id) {
    const auto found = s.visuals.find(id);
    return found == s.visuals.end() || found->second.admitted;
}
p::Ref unitDirection(const Battle& b, const Point& value) {
    auto direction = vector3(b, value);
    if (p::scalar(py::attr(direction.get(), "length").get()) <= 0.) return p::retain(Py_None);
    py::call(direction.get(), "normalise");
    return direction;
}

// (effectsDescr, effectMaterial, direction) for a world hit, or nullopt when the verdict is not ours to present.
std::optional<tracers::Explosion> explosion(Battle& b, const std::string& id, PyObject* impact, const std::string& outcome) {
    auto& s = *b.projectiles;
    if (outcome != "impact") return std::nullopt;
    const auto found = s.metas.find(id);
    if (found == s.metas.end() || found->second.hit_vehicle != std::optional<bool>(false)) return std::nullopt;
    if (!admittedVisual(s, id)) return std::nullopt;
    auto& meta = found->second;
    auto index = effectsIndex(b, meta);
    if (index.get() == Py_None) return std::nullopt;
    PyObject* effects = nullptr;
    try { effects = shotEffects(b, index.get()).release(); }
    catch (const std::exception&) { if (!PyErr_Occurred() || !lookupError()) throw; PyErr_Clear(); return std::nullopt; }
    p::Ref held(effects);
    auto material = needs::surfaceEffectMaterial(b, impact);
    if (material.get() == Py_None) return std::nullopt;
    std::optional<Point> velocity = meta.terminal_velocity;
    if (!velocity) {
        const auto visual = s.visuals.find(id);
        if (visual != s.visuals.end()) velocity = visual->second.velocity;
    }
    if (!velocity) return std::nullopt;
    // __addExplosionEffect keys the effect at position +/- velocityDir; use a unit direction.
    auto direction = unitDirection(b, *velocity);
    if (direction.get() == Py_None) return std::nullopt;
    auto event = object();
    event.object["projectile_id"] = text(id);
    needs::reportEffect(b, "world_explosion", material.get(), index.get(), impact, direction.get(), &event);
    return tracers::Explosion{borrow(held.get()), borrow(material.get()), borrow(direction.get())};
}

// Plays the stock armour-hit family on one visible retained wreck.
bool wreckHit(Battle& b, const std::string& id, const Value& event) {
    if (b.worker) return false;
    auto& s = *b.projectiles;
    if (!admittedVisual(s, id)) return false;
    const auto& wreck = event.get("wreck_hit").resolved();
    if (wreck.kind != Value::Kind::Object || wreck.object.size() != 2 || !wreck.object.count("target_kind") || !wreck.object.count("target_id"))
        return false;
    const auto kind = wreck.get("target_kind").resolved().text;
    const auto target_id = whole(wreck.get("target_id"));
    if ((kind != "player" && kind != "bot") || !target_id || *target_id <= 0) return false;
    auto* target_vehicle = b.vehicle(kind + ":" + std::to_string(*target_id));
    if (!target_vehicle) return false;
    auto target = b.entity(target_vehicle->engine_id);
    if (target.get() == Py_None || !p::flag(py::optional(target.get(), "isStarted").get()) || vehicles::alive(b, *target_vehicle, target.get()) ||
        (!target_vehicle->local && !target_vehicle->presented.spot_visible))
        return false;
    const auto found = s.metas.find(id);
    if (found == s.metas.end()) return false;
    auto& meta = found->second;
    auto index = effectsIndex(b, meta);
    if (index.get() == Py_None) return false;
    PyObject *stages = nullptr, *effects = nullptr;
    try {
        auto descriptor = shotEffects(b, index.get());
        p::Ref hit(PyObject_GetItem(descriptor.get(), p::text("armorHit").get()));
        // stages, effects, unused = armorHit
        p::Ref iterator(PyObject_GetIter(hit.get()));
        PyObject* parts[4] = {};
        for (int i = 0; i < 4; ++i) {
            parts[i] = PyIter_Next(iterator.get());
            if (!parts[i]) break;
        }
        const bool exact = parts[2] && !parts[3];
        if (parts[3]) Py_DECREF(parts[3]);
        if (parts[2]) Py_DECREF(parts[2]);
        if (!exact) {
            Py_XDECREF(parts[0]); Py_XDECREF(parts[1]);
            if (!PyErr_Occurred()) PyErr_SetString(PyExc_ValueError, "armorHit does not unpack into three values");
            throw p::Error("armorHit");
        }
        stages = parts[0]; effects = parts[1];
    } catch (const std::exception&) {
        if (!PyErr_Occurred() || !lookupError()) throw;
        PyErr_Clear();
        return false;
    }
    p::Ref held_stages(stages), held_effects(effects);
    const auto impact = pointOf(event.get("impact"));
    if (!impact) return false;
    std::optional<Point> direction_value = meta.terminal_velocity;
    if (!direction_value) {
        const auto visual = s.visuals.find(id);
        if (visual != s.visuals.end()) direction_value = visual->second.velocity;
    }
    if (!direction_value) {
        auto source = sourceEntity(b, meta.launch);
        auto position = py::optional(source.get(), "position");
        if (position.get() != Py_None) {
            Point delta;
            for (int i = 0; i < 3; ++i) delta[i] = (*impact)[i] - p::scalar(p::item(position.get(), i).get());
            direction_value = delta;
        }
    }
    if (!direction_value) return false;
    auto direction = unitDirection(b, *direction_value);
    if (direction.get() == Py_None) return false;
    auto position = vector3(b, *impact);
    auto terrain = py::optional(b.engine.avatar.get(), "terrainEffects");
    auto add_effect = py::optional(terrain.get(), "addNew");
    if (!py::callable(add_effect) || !b.featureEnabled("projectile impact presentation")) return false;
    auto impact_tuple = tuple(*impact);
    needs::reportEffect(b, "wreck_hit", p::text("armorHit").get(), index.get(), impact_tuple.get(), direction.get(), nullptr);
    return b.feature("projectile impact presentation", [&] {
        // The #1513 impact trio needs the shooter and target identities, as for a live vehicle hit.
        auto offset = py::call(direction.get(), "scale", p::number(0.4).get());
        p::Ref start(PyNumber_Subtract(position.get(), offset.get()));
        p::Ref end(PyNumber_Add(position.get(), offset.get()));
        auto* shooter = b.vehicle(meta.shooter_kind + ":" + std::to_string(meta.shooter_id));
        const std::int64_t attacker = shooter ? shooter->engine_id : 0;
        auto identity = needs::impactSoundIdentity(b, *target_vehicle, attacker, direction.get(), 0.);
        p::Ref positional(PyTuple_Pack(4, position.get(), held_effects.get(), held_stages.get(), Py_None));
        p::Ref keywords(PyDict_New());
        if (PyDict_Update(keywords.get(), identity.get()) < 0) throw p::Error("impact identity");
        p::set(keywords.get(), "dir", p::retain(direction.get()));
        p::set(keywords.get(), "start", p::retain(start.get()));
        p::set(keywords.get(), "end", p::retain(end.get()));
        p::set(keywords.get(), "showShockWave", p::boolean(false));
        p::set(keywords.get(), "showFlashBang", p::boolean(false));
        p::Ref unused(PyObject_Call(add_effect.get(), positional.get(), keywords.get()));
    });
}

// ---- Epochs --------------------------------------------------------------------------------------------------------
// An authority change retires native visual owners without impact feedback.
void resetVisuals(Battle& b) {
    auto& s = *b.projectiles;
    s.visuals.clear();
    try {
        if (!tracers::reset(b)) b.warn("projectile visual epoch reset", "presenter retained old-epoch visual owners", false);
    } catch (const std::exception& e) { b.warn("projectile visual epoch reset", current(e), false); }
}
void epochChanged(Battle& b) {
    auto& s = *b.projectiles;
    resetVisuals(b);
    s.visual_terminals.clear();
    s.resolution_acks.clear();
    s.local_presentations.clear();
    s.local_presented.clear();
    s.metas.clear();
}

// ---- Server-time anchor (worker before its Bot owner exists, replay) -------------------------------------------------
double oneWay(const Battle& b) {
    auto minimum = py::optional(b.engine.client.get(), "minimum_rtt_ms");
    auto rtt = minimum.get() != Py_None ? std::move(minimum) : py::optional(b.engine.client.get(), "rtt_ms");
    if (rtt.get() == Py_None) return 0.;
    p::Ref value(PyNumber_Float(rtt.get()));
    const double half = PyFloat_AS_DOUBLE(value.get()) / 2000.;
    return std::max(0., std::min(.25, half));
}
bool anchorMessage(Battle& b, const Value& message) {
    auto& s = *b.projectiles;
    if (message.resolved().kind != Value::Kind::Object) return false;
    const double now = b.clock();
    double local_anchor = now;
    // The transport stamps how long the message waited between its network thread and this frame.
    const double delay = message.get("_client_dispatch_delay").numeric(0.);
    if (delay > 0.) local_anchor = std::max(0., now - std::min(60., delay));
    auto anchored = [&](double one_way) { return std::max(0., local_anchor - one_way); };
    if (present(message, "server_time_ms")) {
        const auto server = whole(message.get("server_time_ms"));
        if (!server || *server < 0) return false;
        if (!s.server_time_ms || *server >= *s.server_time_ms) {
            s.server_time_ms = *server;
            s.server_local_time = anchored(oneWay(b));
        }
    }
    if (present(message, "motion_time_us")) {
        const auto motion = whole(message.get("motion_time_us"));
        if (!motion || *motion < 0) return false;
        const double one_way = oneWay(b);
        if (!s.motion_time_us || *motion >= *s.motion_time_us) {
            s.motion_time_us = *motion;
            s.motion_local_time = anchored(one_way);
        }
    }
    Value epoch = message.get("authority_epoch");
    if (isNull(epoch)) {
        auto client_epoch = py::optional(b.engine.client.get(), "authority_epoch");
        if (client_epoch.get() != Py_None) epoch = cs::read(client_epoch.get());
    }
    if (!isNull(epoch)) projectiles::setEpoch(b, epoch, now);
    return true;
}

// ---- Metas ---------------------------------------------------------------------------------------------------------
const char* const immutable_fields[] = {
    "projectile_id", "shooter_kind", "shooter_id", "source_vehicle", "source_shot", "shot_seq", "shell_index", "burst_group_seq",
    "burst_index", "burst_count", "origin", "velocity", "range_origin", "gravity", "max_distance", "max_time_ms", "is_he",
    "splash_radius", "penetration_factor", "launch_server_time_ms", "fire_intent_seq", "fire_input_seq"};
std::string mismatches(const Value& current, const Value& canonical) {
    std::string names;
    for (const char* name : immutable_fields)
        if (current.get(name).resolved() != canonical.get(name).resolved()) names += (names.empty() ? "" : ",") + std::string(name);
    return names;
}
ProjectileMeta fresh(const Value& launch) {
    ProjectileMeta meta;
    meta.launch = launch.resolved();
    meta.id = idText(launch.get("projectile_id"));
    meta.shooter_kind = launch.get("shooter_kind").resolved().text;
    meta.shooter_id = whole(launch.get("shooter_id")).value_or(0);
    meta.local_launch = present(launch, "local_launch_time");
    return meta;
}
// The visible owner's launch presentation record: the canonical spelling over the event's own fields.
ProjectileMeta* installFresh(Battle& b, const Value& event) {
    const auto normalized = projectiles::normalize(event);
    if (!normalized) {
        b.warn("native projectile presentation", "invalid native launch " + idText(event.get("projectile_id")), false);
        return nullptr;
    }
    Value merged = event.resolved();
    for (const auto& field : normalized->resolved().object) merged.object[field.first] = field.second;
    auto meta = fresh(merged);
    auto& s = *b.projectiles;
    auto& slot = s.metas[meta.id];
    slot = std::move(meta);
    return &slot;
}
void reportFailure(const std::string& id, const std::string& stage, const std::string& reason, const std::string& detail = "none") {
    log("PROJECTILE FAILURE id=" + id + " stage=" + stage + " reason=" + reason + " error=" + detail.substr(0, 160));
}

// ---- The visible owner's presentation of its frame and snapshot ----------------------------------------------------
void applyEdges(Battle& b, const std::vector<PlayerProjectiles::Edge>& edges) {
    auto& s = *b.projectiles;
    for (const auto& edge : edges) {
        const auto found = s.metas.find(edge.id);
        if (found == s.metas.end()) continue;
        auto& meta = found->second;
        if (edge.kind == PlayerProjectiles::Edge::Kind::Pending) {
            meta.pending_resolution = true;
            meta.hit_vehicle = edge.hit_vehicle;
            if (edge.terminal_velocity) meta.terminal_velocity = edge.terminal_velocity;
            continue;
        }
        auto stop = object();
        stop.object["outcome"] = text("impact");
        stop.object["impact"] = edge.impact;
        projectiles::stopVisual(b, edge.id, stop);
        for (const auto& field : edge.segment.resolved().object) meta.launch.object[field.first] = field.second;
        meta.pending_resolution = false;
    }
}
bool visual(Battle& b, const std::string& id, double now, const std::optional<Point>& visual_start);

// The native path's tracer start, reporting its outcome to the owner so a delayed retry stays possible.
bool presentVisual(Battle& b, const std::string& id, double now, const std::optional<Point>& visual_start = {}) {
    const bool result = visual(b, id, now, visual_start);
    auto& s = *b.projectiles;
    if (s.player) {
        const auto meta = s.metas.find(id);
        if (meta != s.metas.end()) {
            const auto marker = s.visuals.find(id);
            const bool settled = result || (marker != s.visuals.end() && !marker->second.admitted);
            s.player->visualResult(id, whole(meta->second.launch.get("ricochet_count")).value_or(0), settled);
        }
    }
    return result;
}

// Starts one native visual per segment, independent of cursor cadence.
bool visual(Battle& b, const std::string& id, double now, const std::optional<Point>& visual_start_argument) {
    auto& s = *b.projectiles;
    if (b.worker || !b.engine.avatar) return false;
    if (s.visual_terminals.contains(id)) return false;
    const auto meta_found = s.metas.find(id);
    if (meta_found == s.metas.end()) return false;
    auto& meta = meta_found->second;
    auto& launch = meta.launch;
    const auto ricochets = whole(launch.get("ricochet_count")).value_or(0);
    const bool owned = owns(b, launch);
    // Physical continuation may run ahead of the echo; its tracer follows the committed armour response.
    if (meta.local_launch && owned && ricochets > meta.presented_ricochet_count) return false;
    // An echo cannot restart a deferred or locally retired segment.
    if (owned && meta.pending_resolution) return false;
    double confirmed = visualAge(launch);
    const double segment_start_s = double(whole(launch.get("segment_start_time_ms")).value_or(0)) / 1000.;
    const auto bot_stamp = whole(launch.get("bot_presentation_time_us"));
    if (!b.replay && bot_stamp) {
        const auto server_time = whole(b.start.get("server_time_ms")).value_or(0);
        const bool historical = server_time > whole(launch.get("launch_server_time_ms")).value_or(0);
        if (!launch.get("muzzle_presented").truth() && !historical) return false;
        if (!historical && ricochets > meta.presented_ricochet_count) return false;
        auto* actor = b.vehicle("bot:" + idText(launch.get("shooter_id")));
        if (historical && actor && actor->cursor) {
            const double value = (*actor->cursor - double(*bot_stamp)) / 1000000.0 - segment_start_s;
            confirmed = value > 0. ? value : 0.;
        } else if (!meta.bot_visual_launch_time) {
            // A newly displayed muzzle owns a new visual flight, not a mid-flight tracer.
            confirmed = 0.;
        }
        if (!meta.bot_visual_launch_time) meta.bot_visual_launch_time = now - confirmed - segment_start_s;
        const double value = now - *meta.bot_visual_launch_time - segment_start_s;
        confirmed = value > 0. ? value : 0.;
    }
    auto visual_found = s.visuals.find(id);
    if (visual_found != s.visuals.end() && visual_found->second.ricochet_count > ricochets)
        // A delayed launch callback must not replace the current bounce segment.
        return visual_found->second.active;
    if (visual_found != s.visuals.end() && visual_found->second.ricochet_count != ricochets) {
        meta.hit_vehicle = true;
        auto stop = object();
        stop.object["impact"] = launch.get("segment_origin");
        stop.object["resolved_time_ms"] = launch.get("segment_start_time_ms");
        projectiles::stopVisual(b, id, stop);
        visual_found = s.visuals.end();
    } else if (visual_found != s.visuals.end()) {
        // Only a failed initial creation may try the newest checked pose.
        if (visual_found->second.active || !visual_found->second.admitted) return visual_found->second.active;
    }
    auto descriptor = sourceDescriptor(b, &meta, launch);
    if (descriptor.get() == Py_None) return false;
    const auto origin = pointOf(launch.get("segment_origin")), velocity = pointOf(launch.get("segment_velocity"));
    if (!origin || !velocity) return false;
    const double gravity = launch.get("gravity").numeric();
    if (visual_found == s.visuals.end()) {
        ProjectileVisual fresh_visual;
        fresh_visual.origin = *origin;
        fresh_visual.velocity = *velocity;
        fresh_visual.gravity = gravity;
        fresh_visual.segment_start_ms = whole(launch.get("segment_start_time_ms")).value_or(0);
        fresh_visual.ricochet_count = ricochets;
        auto* shooter = b.vehicle(meta.shooter_kind + ":" + std::to_string(meta.shooter_id));
        fresh_visual.attacker_id = shooter && shooter->engine_id > 0 ? shooter->engine_id : meta.shooter_id;
        // Ask the presenter for cosmetic capacity, never authority capacity.
        try {
            fresh_visual.admitted = tracers::admit(b, fresh_visual.attacker_id, id, now);
        } catch (const std::exception& e) {
            b.warn("projectile visual admission", current(e));
            fresh_visual.admitted = false;
        }
        visual_found = s.visuals.emplace(id, fresh_visual).first;
    }
    auto& visual_state = visual_found->second;
    if (!visual_state.admitted || !b.featureEnabled("projectile visual launch")) return false;
    // A late first snapshot starts at its checked trajectory pose; the stock simulator then advances locally.
    const auto reference_origin = trajectory(*origin, *velocity, gravity, confirmed);
    const Point reference_velocity{(*velocity)[0], (*velocity)[1] - gravity * confirmed, (*velocity)[2]};
    std::optional<Point> visual_start = visual_start_argument;
    if (confirmed > 0. || ricochets) visual_start.reset();
    bool active = false;
    const bool launched = b.feature("projectile visual launch", [&] {
        double remaining = std::max(0.001, launch.get("max_distance").numeric() - launch.get("checked_distance").numeric());
        if (bot_stamp) {
            // The authority cursor may be ahead of this displayed flight; only displayed segments spend its range.
            const double spent = ricochets == 0 ? 0. : meta.visual_segment_distance.value_or(launch.get("checked_distance").numeric());
            remaining = std::max(0.001, launch.get("max_distance").numeric() - spent);
        }
        active = tracers::play(b, descriptor.get(), whole(launch.get("shell_index")).value_or(0), reference_origin, reference_velocity, gravity,
                               remaining, visual_state.attacker_id, id, ricochets != 0, visual_start);
    });
    visual_state.active = launched && active;
    return visual_state.active;
}

// ---- Fire intents and local presentation ---------------------------------------------------------------------------
bool rejectLocalFire(Battle& b, const std::string& reason) {
    double remaining = -1.;
    if (b.gun->gun) if (auto* gun = stock_native_client_weapon::gun(b.gun->gun.get())) remaining = gun->reload_time;
    char line[160];
    snprintf(line, sizeof(line), "LOCAL FIRE rejected reason=%s reload=%.3f", reason.c_str(), remaining);
    log(line);
    return false;
}
// Presents one own shot after Avatar.shoot installs its native waiting-for-shot token.
void deferLocalPresentation(Battle& b, const Value& event) {
    auto& s = *b.projectiles;
    const auto id = idText(event.get("projectile_id"));
    const auto token = ++s.presentation_tokens;
    s.local_presentations[id] = token;
    const auto round = b.round_id;
    Value frozen = event.resolved();
    schedule(b, 0., [id, token, round, frozen](Battle& battle) {
        auto& state = *battle.projectiles;
        const auto found = state.local_presentations.find(id);
        if (battle.round_id != round || found == state.local_presentations.end() || found->second != token) return;
        state.local_presentations.erase(found);
        if (!battle.running() || !battle.engine.server) return;
        auto* local = battle.vehicle("player:" + idText(frozen.get("shooter_id")));
        if (!local || local->tombstone) return;
        battle.feature("local shot presentation", [&] { needs::showShot(battle, frozen, true); }, false);
    });
}
void publishShot(Battle& b, double now) {
    b.feature("local shot publication", [&] {
        gun::publishKind(b, "ammo", now, true);
        auto* gun = stock_native_client_weapon::gun(b.gun->gun.get());
        if (gun) gun::publishKind(b, "reload", now, true, gun->reload_time, gun->reload_duration);
        needs::sendCurrentInput(b);
    }, false);
}
bool accepted(Battle& b, PlayerProjectiles* owner, const PlayerProjectiles::Fired& fired, double now) {
    if (b.projectiles->player.get() != owner) return false;
    if (!fired.rejection.empty()) return rejectLocalFire(b, fired.rejection);
    // The presentation record stock muzzle and tracer edges consume; physics never consults it.
    installFresh(b, fired.presentation);
    b.gun->intent.reset();
    publishShot(b, now);
    if (b.projectiles->player.get() == owner) deferLocalPresentation(b, fired.presentation);
    return true;
}
void advanceBurst(Battle& b) {
    auto* owner = b.projectiles->player.get();
    if (!owner) return;
    const double now = b.clock();
    if (!owner->burstDue(now)) return;
    auto fired = owner->trigger(b, now, true);
    if (fired.rejection.empty()) { accepted(b, owner, fired, now); return; }
    gun::cancelBurst(b, true);
}
}

// ---- RecentIds, owners ---------------------------------------------------------------------------------------------
bool RecentIds::add(const std::string& id) {
    if (!ids_.insert(id).second) return false;
    order_.push_back(id);
    while (order_.size() > limit_) { ids_.erase(order_.front()); order_.pop_front(); }
    return true;
}
Projectiles::Projectiles() = default;
// The damage reader borrows this round's HE tuning; never leave it pointing at a destroyed round.
Projectiles::~Projectiles() { stock_native_projectile_damage::releaseHeTuning(&he_tuning); }

// ---- The live mechanical reload sound ------------------------------------------------------------------------------
// Owns this Avatar's AmmoController reload effect: one bounded cue schedule on the battle clock, no extra callbacks.
class ReloadSound {
public:
    struct Cue { double when; enum class Action { Control, Mechanical, Loop, Stop } action; std::string field; };
    Object original, settings, desc, controller, bound_settings, stand_in, lane;
    std::vector<Cue> queue;
    std::string kind;
    std::optional<int> shell_index;
    std::optional<double> deadline, last_tick;
    std::optional<std::tuple<double, double, double, std::string, int, int, std::vector<int>>> last_edge;
    std::optional<std::pair<int, std::vector<int>>> last_gun;
    bool closed = false, loop_running = false, short_played = false;
    std::set<std::string> errors;
    int cycles = 0;

    std::string name(const char* field) const {
        auto value = py::optional(desc.get(), field);
        return value.get() == Py_None || !p::flag(value.get()) ? std::string() : p::string(value.get());
    }
    double descNumber(const char* field, double fallback) const {
        auto value = py::optional(desc.get(), field);
        if (value.get() == Py_None) return fallback;
        PyObject* f = PyNumber_Float(value.get());
        if (!f) { PyErr_Clear(); return fallback; }
        const double out = PyFloat_AS_DOUBLE(f);
        Py_DECREF(f);
        return std::isfinite(out) ? out : fallback;
    }
    bool hasDesc(const char* field) const { return desc && PyObject_HasAttrString(desc.get(), field); }
    bool soundsEnabled() const {
        p::Ref helpers(PyImport_ImportModule("helpers"));
        return !p::flag(py::call(helpers.get(), "gEffectsDisabled").get());
    }
    p::Ref sounds() const {
        p::Ref module(PyImport_ImportModule("SoundGroups"));
        return py::attr(module.get(), "g_instance");
    }
    void soundError(const std::string& event, const std::exception& e) {
        const auto detail = describe(e);
        if (errors.insert(event).second) log("LIVE_REPLAY_SOUND sound_error event=" + event + " error=" + detail);
    }
    void control(const char* field) {
        const auto event = name(field);
        if (event.empty()) return;
        try { if (soundsEnabled()) py::call(sounds().get(), "playSound2D", p::text(event).get()); }
        catch (const std::exception& e) { soundError(event, e); }
    }
    void stopLane() {
        Object sound = std::move(lane);
        lane = Object();
        if (!sound) return;
        try { py::call(sound.get(), "stop"); } catch (const std::exception& e) { soundError("mechanical_lane_stop", e); }
    }
    void mechanical(const char* field, double now) {
        const auto event = name(field);
        if (event.empty()) return;
        stopLane();
        try {
            if (soundsEnabled()) {
                lane = own(py::call(sounds().get(), "getSound2D", p::text(event).get()));
                py::call(lane.get(), "play");
                last_tick = now;
                if (kind == "short") short_played = true;
            }
        } catch (const std::exception& e) { soundError(event, e); }
    }
    void stopLoop() { if (loop_running) control("stopLoop"); loop_running = false; }
    void stop() { queue.clear(); kind.clear(); deadline.reset(); stopLoop(); stopLane(); }
    void add(double when, Cue::Action action, const char* field) { queue.push_back({when, action, field}); }
    // One explicit reload publication: plans the cycle's cues on the battle clock.
    void edge(double remaining, double duration, const std::string& reason, const HudEdge& gun, double now) {
        if (closed) return;
        const auto signature = std::make_tuple(now, remaining, duration, reason, gun.gun_clip, gun.gun_shot, gun.gun_ammo);
        if (last_edge == signature) return;
        last_edge = signature;
        const double raw = std::max(0., gun.gun_reload_time);
        if (reason == "complete" || raw <= 0.) { pump(now); queue.clear(); kind.clear(); deadline.reset(); stopLoop(); return; }
        const bool barrel = hasDesc("shellDuration");
        const int clip = gun.gun_clip;
        const std::string next_kind = barrel && clip == 0 ? "long" : "short";
        const int next_shell = gun.gun_shot;
        const auto& ammo = gun.gun_ammo;
        bool spent = false;
        if (last_gun) {
            spent = clip < last_gun->first;
            if (!spent && ammo.size() == last_gun->second.size())
                for (std::size_t i = 0; i < ammo.size(); ++i) if (ammo[i] < last_gun->second[i]) { spent = true; break; }
        }
        const bool fresh_cycle = reason == "start" || kind != next_kind || shell_index != next_shell || spent;
        last_gun = std::make_pair(clip, ammo);
        queue.clear();
        if (fresh_cycle) { stopLoop(); stopLane(); last_tick.reset(); short_played = false; ++cycles; }
        kind = next_kind;
        shell_index = next_shell;
        deadline = now + raw;
        const double end = std::max(now, *deadline);
        try {
            const auto caliber = name("caliber");
            if (!caliber.empty()) py::call(sounds().get(), "setSwitch", p::text("SWITCH_ext_rld_automat_caliber").get(), p::text(caliber).get());
        } catch (const std::exception& e) { soundError("caliber", e); }
        constexpr double min_spacing = .06;
        constexpr int max_cues = 64;
        if (kind == "long") {
            const int available = next_shell >= 0 && std::size_t(next_shell) < ammo.size() ? std::max(0, ammo[next_shell]) : 0;
            auto clip_size = py::attr(py::attr(settings.get(), "clip").get(), "size");
            const int requested = std::min(std::max(0, int(p::wide(clip_size.get()))), available);
            if (fresh_cycle) add(now, Cue::Action::Control, "startLong");
            const double length = std::max(0., descNumber("duration", 0.));
            const double window_start = std::max(now, end - length);
            const double tail = std::min(std::max(0., descNumber("shellDtLast", 0.)), end - window_start);
            const double last = std::max(window_start, end - tail);
            const double spacing = std::max(min_spacing, descNumber("shellDt", .5));
            const double room = std::max(0., last - window_start);
            const int planned = requested ? std::min({requested, max_cues, 1 + int(std::floor((room + 1e-9) / spacing))}) : 0;
            if (!loop_running && requested) add(window_start, Cue::Action::Loop, "startLoop");
            for (int i = 0; i < planned; ++i) {
                const double t = planned == 1 ? last : window_start + room * i / (planned - 1);
                if (last_tick && t < *last_tick + spacing - 1e-9) continue;
                add(t, Cue::Action::Mechanical, i == planned - 1 ? "loopShellLast" : "loopShell");
            }
            add(end, Cue::Action::Stop, "stopLoop");
        } else if (!short_played) {
            const double lead = std::max(0., descNumber(barrel ? "shellDuration" : "duration", 0.));
            const double due = std::max(now, end - lead);
            if (barrel && clip == 1 && fresh_cycle) add(now, Cue::Action::Control, "lastShellAlert");
            add(due, Cue::Action::Mechanical, "soundEvent");
        }
        std::stable_sort(queue.begin(), queue.end(), [](const Cue& a, const Cue& b) { return a.when < b.when; });
        pump(now);
    }
    void pump(double now) {
        if (closed || queue.empty()) return;
        std::size_t due = 0;
        while (due < queue.size() && queue[due].when <= now + 1e-9) ++due;
        if (!due) return;
        std::vector<Cue> batch(queue.begin(), queue.begin() + std::ptrdiff_t(due));
        queue.erase(queue.begin(), queue.begin() + std::ptrdiff_t(due));
        const Cue* latest = nullptr;
        for (const auto& cue : batch) if (cue.action == Cue::Action::Mechanical) latest = &cue;
        constexpr double late_grace = .15;
        for (const auto& cue : batch) {
            if (cue.action == Cue::Action::Stop) stopLoop();
            else if (cue.action == Cue::Action::Mechanical) { if (&cue == latest && now - cue.when <= late_grace) mechanical(cue.field.c_str(), now); }
            else if (cue.action == Cue::Action::Loop) {
                if (deadline && now < *deadline && !loop_running) { control(cue.field.c_str()); loop_running = true; }
            } else if (now - cue.when <= late_grace) control(cue.field.c_str());
        }
    }
    bool owns(PyObject* candidate) const {
        if (closed || candidate != controller.get()) return false;
        auto current_settings = py::call(candidate, "getGunSettings");
        return py::attr(current_settings.get(), "reloadEffect").get() == stand_in.get();
    }
    void close() {
        if (closed) return;
        stop();
        if (controller) {
            try {
                auto current_settings = py::call(controller.get(), "getGunSettings");
                if (py::attr(current_settings.get(), "reloadEffect").get() == stand_in.get()) {
                    p::Ref keywords(PyDict_New());
                    p::set(keywords.get(), "reloadEffect", p::retain(original ? original.get() : Py_None));
                    auto replace = py::attr(current_settings.get(), "_replace");
                    p::Ref restored(PyObject_Call(replace.get(), p::Ref(PyTuple_New(0)).get(), keywords.get()));
                    py::set(controller.get(), "_AmmoController__gunSettings", restored.get());
                }
            } catch (const std::exception&) { PyErr_Clear(); }
        }
        closed = true;
        controller = Object();
        bound_settings = Object();
    }
};
std::optional<double> nextReloadCue(const ReloadSound& sound) {
    if (sound.closed || sound.queue.empty()) return std::nullopt;
    return sound.queue.front().when;
}
void pumpReloadSound(ReloadSound& sound, double now) { sound.pump(now); }
void stopReloadSound(ReloadSound& sound) { sound.stop(); }
LocalGun::LocalGun() = default;
LocalGun::~LocalGun() { if (reload_sound) reload_sound->close(); }

namespace {
// The stock reloadEffect stand-in: #1513's AmmoController starts its effect, which the native schedule owns.
struct StandIn { PyObject_HEAD };
PyObject* standInStart(PyObject*, PyObject*) { Py_RETURN_NONE; }
PyObject* standInStop(PyObject*, PyObject*) { Py_RETURN_NONE; }
PyMethodDef stand_in_methods[] = {{"start", standInStart, METH_VARARGS, nullptr}, {"stop", standInStop, METH_VARARGS, nullptr},
                                  {nullptr, nullptr, 0, nullptr}};
PyTypeObject StandInType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.ReloadEffect"};
p::Ref standIn() {
    if (!StandInType.tp_methods) {
        StandInType.tp_basicsize = sizeof(StandIn);
        StandInType.tp_flags = Py_TPFLAGS_DEFAULT;
        StandInType.tp_methods = stand_in_methods;
        StandInType.tp_new = PyType_GenericNew;
        if (PyType_Ready(&StandInType) < 0) throw p::Error("native reload effect type failed");
    }
    return p::Ref(PyObject_CallObject(reinterpret_cast<PyObject*>(&StandInType), nullptr));
}
// Also cancels the native nested timer, which stop() alone can miss.
void retireNative(PyObject* effect) {
    if (!effect || effect == Py_None) return;
    py::call(effect, "stop");
    for (PyObject* owner : {effect, py::optional(effect, "_BarrelReload__reloadSequence").release()}) {
        p::Ref held(owner == effect ? p::retain(effect) : p::Ref(owner));
        auto clear = py::optional(held.get(), "clearCallbacks");
        if (py::callable(clear)) py::invoke(clear.get());
    }
}
ReloadSound* ensureReloadSound(Battle& b) {
    auto provider = py::attr(b.engine.avatar.get(), "guiSessionProvider");
    auto ammo = py::attr(py::attr(provider.get(), "shared").get(), "ammo");
    auto& audio = b.gun->reload_sound;
    if (audio && audio->owns(ammo.get())) return audio.get();
    if (audio) { audio->close(); audio.reset(); }
    auto settings = py::call(ammo.get(), "getGunSettings");
    auto effect = py::attr(settings.get(), "reloadEffect");
    if (effect.get() == Py_None) return nullptr;   // Stock has not installed an effect, or this gun has none.
    if (py::optional(ammo.get(), "_AmmoController__gunSettings").get() != settings.get())
        throw p::Error("live reload sound: #1513 AmmoController owner is unavailable");
    auto sound = std::make_unique<ReloadSound>();
    sound->original = borrow(effect.get());
    sound->settings = borrow(settings.get());
    sound->desc = own(py::optional(effect.get(), "_desc"));
    sound->stand_in = own(standIn());
    p::Ref keywords(PyDict_New());
    p::set(keywords.get(), "reloadEffect", p::retain(sound->stand_in.get()));
    auto replace = py::attr(settings.get(), "_replace");
    p::Ref replacement(PyObject_Call(replace.get(), p::Ref(PyTuple_New(0)).get(), keywords.get()));
    retireNative(effect.get());
    py::set(ammo.get(), "_AmmoController__gunSettings", replacement.get());
    sound->controller = borrow(ammo.get());
    sound->bound_settings = borrow(replacement.get());
    audio = std::move(sound);
    return audio.get();
}
}

// ---- Projectiles API -----------------------------------------------------------------------------------------------
namespace projectiles {
bool isAuthority(const Battle& b) {
    if (b.replay) return false;
    if (!b.worker) return b.local_player > 0;
    auto checker = py::optional(b.engine.client.get(), "is_bot_authority");
    if (!py::callable(checker)) return false;
    try { return p::flag(py::invoke(checker.get()).get()); }
    catch (const std::exception&) { PyErr_Clear(); return false; }
}

std::optional<std::int64_t> estimatedServerTime(const Battle& b, double now) {
    const auto& s = *b.projectiles;
    if (!s.server_time_ms) return std::nullopt;
    const double elapsed = std::max(0., now - s.server_local_time);
    return *s.server_time_ms + std::int64_t(elapsed * 1000.);
}

std::optional<Value> normalize(const Value& raw_value) {
    const auto& raw = raw_value.resolved();
    if (raw.kind != Value::Kind::Object) return std::nullopt;
    Value row = raw;
    if (!present(row, "max_distance") && present(row, "maxDistance")) row.object["max_distance"] = row.get("maxDistance");
    std::string kind = row.get("shooter_kind").resolved().text;
    Value shooter = row.get("shooter_id");
    if (kind.empty() && isNull(row.get("shooter_kind"))) {
        if (!isNull(row.get("attacker_bot"))) { kind = "bot"; shooter = row.get("attacker_bot"); }
        else if (!isNull(row.get("attacker"))) { kind = "player"; shooter = row.get("attacker"); }
    }
    if ((kind != "player" && kind != "bot") || isNull(shooter)) return std::nullopt;
    row.object["shooter_kind"] = text(kind);
    row.object["shooter_id"] = shooter;
    // The player wire carries its fire identities; a Bot launch never does.
    if (kind == "bot" && (present(row, "fire_intent_seq") || present(row, "fire_input_seq"))) return std::nullopt;
    p::Ref packed(cs::write(row));
    shot::Launch l;
    const bool valid = kind == "player" ? shot::readPlayerLaunch(packed.get(), l) : shot::readLaunch(packed.get(), l);
    if (!valid) { PyErr_Clear(); return std::nullopt; }
    auto out = object();
    auto& o = out.object;
    o["projectile_id"] = text(l.id);
    o["shooter_kind"] = text(kind);
    o["shooter_id"] = integer(l.shooter);
    o["source_vehicle"] = text(l.vehicle);
    o["source_shot"] = row.get("source_shot").resolved();
    o["shot_seq"] = integer(l.sequence);
    o["burst_group_seq"] = integer(l.group_sequence);
    o["burst_index"] = integer(l.burst_index);
    o["burst_count"] = integer(l.burst_count);
    o["shell_index"] = integer(l.shell_index);
    o["origin"] = points(l.origin);
    o["velocity"] = points(l.velocity);
    o["range_origin"] = points(l.range_origin);
    o["segment_origin"] = points(l.segment_origin);
    o["segment_velocity"] = points(l.segment_velocity);
    o["segment_start_time_ms"] = integer(l.segment_ms);
    o["ricochet_count"] = integer(l.segment);
    o["base_penetration_multiplier"] = number(l.multiplier);
    o["gravity"] = number(l.gravity);
    o["max_distance"] = number(l.maximum_distance);
    o["max_time_ms"] = integer(l.maximum_ms);
    o["is_he"] = boolean(l.he);
    o["splash_radius"] = number(l.splash_radius);
    o["penetration_factor"] = number(l.penetration_factor);
    o["launch_server_time_ms"] = integer(l.launch_ms);
    o["base_checked_ms"] = integer(l.checked_ms);
    o["checked_distance"] = number(l.checked_distance);
    o["piercing_loss"] = number(l.piercing_loss);
    if (kind == "player") {
        o["fire_intent_seq"] = row.get("fire_intent_seq").resolved();
        o["fire_input_seq"] = row.get("fire_input_seq").resolved();
    }
    if (l.presentation_us) {
        if (kind != "bot") return std::nullopt;
        o["bot_presentation_time_us"] = integer(*l.presentation_us);
    }
    if (present(row, "muzzle_presented")) o["muzzle_presented"] = row.get("muzzle_presented");
    return out;
}

ProjectileMeta& install(Battle& b, const Value& normalized) {
    auto& s = *b.projectiles;
    const auto id = idText(normalized.get("projectile_id"));
    auto found = s.metas.find(id);
    if (found == s.metas.end()) {
        auto& meta = s.metas.emplace(id, fresh(normalized)).first->second;
        needs::botConfirmLaunch(b, normalized);
        return meta;
    }
    auto& meta = found->second;
    // Launch fields are immutable; only the acknowledged cursor and penetration state may advance.
    if (!mismatches(meta.launch, normalized).empty()) throw p::Error("canonical projectile launch changed");
    const auto current_count = whole(meta.launch.get("ricochet_count")).value_or(0);
    const auto incoming = whole(normalized.get("ricochet_count")).value_or(0);
    // An older echo can follow a locally queued ricochet; it does not rewind the second segment.
    if (!b.worker && owns(b, meta.launch) && incoming < current_count) return meta;
    if (incoming < current_count || incoming > current_count + 1) throw p::Error("canonical projectile segment regressed");
    const char* segment_fields[] = {"segment_origin", "segment_velocity", "segment_start_time_ms", "base_penetration_multiplier"};
    if (incoming == current_count) {
        for (const char* name : segment_fields)
            if (meta.launch.get(name).resolved() != normalized.get(name).resolved()) throw p::Error("canonical projectile segment changed");
    } else {
        for (const char* name : segment_fields) meta.launch.object[name] = normalized.get(name);
        meta.launch.object["ricochet_count"] = integer(incoming);
        meta.pending_resolution = false;
    }
    const auto checked = whole(normalized.get("base_checked_ms")).value_or(0);
    if (checked >= whole(meta.launch.get("base_checked_ms")).value_or(0)) {
        meta.launch.object["base_checked_ms"] = integer(checked);
        // A pending local resolution keeps its monotonic frontier; otherwise the server's acknowledgement is current.
        for (const char* name : {"checked_distance", "piercing_loss"}) {
            const double kept = meta.launch.get(name).numeric(0.), next = normalized.get(name).numeric(0.);
            meta.launch.object[name] = number(meta.pending_resolution && kept > next ? kept : next);
        }
    }
    needs::botConfirmLaunch(b, normalized);
    return meta;
}

bool ensureVisual(Battle& b, const std::string& projectile_id, double now, const std::optional<Point>& visual_start) {
    if (nativeEnabled(b)) return presentVisual(b, projectile_id, now, visual_start);
    return visual(b, projectile_id, now, visual_start);
}

std::optional<Point> visualStart(Battle& b, PyObject* entity, const Value& normalized) {
    if (!entity || entity == Py_None || !p::flag(py::optional(entity, "isStarted").get()) || normalized.resolved().kind != Value::Kind::Object ||
        whole(normalized.get("ricochet_count")).value_or(0) != 0 || visualAge(normalized) > 0.)
        return std::nullopt;
    auto cameras = py::optional(b.engine.modules.get(), "cameras");
    auto on_screen = py::optional(cameras.get(), "isPointOnScreen");
    if (!py::callable(on_screen)) return std::nullopt;
    try {
        auto compound = py::attr(py::attr(entity, "appearance").get(), "compoundModel");
        auto node = py::call(compound.get(), "node", p::text("HP_gunFire").get());
        auto matrix = py::call(b.engine.math.get(), "Matrix", node.get());
        auto native_point = py::attr(matrix.get(), "translation");
        Point point;
        for (int i = 0; i < 3; ++i) point[i] = p::scalar(p::item(native_point.get(), i).get());
        if (!p::flag(py::invoke(on_screen.get(), native_point.get()).get())) return std::nullopt;
        const auto reference = pointOf(normalized.get("segment_origin"));
        if (!reference) return std::nullopt;
        double offset = 0.;
        for (int i = 0; i < 3; ++i) offset += (point[i] - (*reference)[i]) * (point[i] - (*reference)[i]);
        // Mirror ProjectileMover.add so the frontier bridge records the start the presenter accepts.
        constexpr double limit = tracers::visual_start_max_diff;
        if (offset > limit * limit) return std::nullopt;
        return point;
    } catch (const std::exception&) {
        // The stock visual start is optional; fall back to the immutable canonical launch point.
        PyErr_Clear();
        return std::nullopt;
    }
}

bool stopVisual(Battle& b, const std::string& id, const Value& event) {
    auto& s = *b.projectiles;
    if (b.worker || !b.engine.avatar) { s.visuals.erase(id); return false; }
    std::optional<Point> impact = pointOf(event.get("impact"));
    const auto visual_found = s.visuals.find(id);
    if (!impact && visual_found != s.visuals.end()) {
        const auto resolved = event.get("resolved_time_ms");
        if (!isNull(resolved)) {
            const double elapsed = std::max(0., (resolved.numeric() - double(visual_found->second.segment_start_ms)) / 1000.);
            impact = trajectory(visual_found->second.origin, visual_found->second.velocity, visual_found->second.gravity, elapsed);
        }
    }
    if (!impact) return false;
    bool stopped = false;
    try {
        auto impact_tuple = tuple(*impact);
        auto blast = explosion(b, id, impact_tuple.get(), event.get("outcome").resolved().text);
        const auto& hit = event.get("hit_vehicle").resolved();
        const bool missed = hit.kind == Value::Kind::Boolean && !hit.boolean;
        stopped = tracers::stop(b, id, *impact, blast, missed);
    } catch (const std::exception& e) {
        // Terminal authority has already been applied; a cosmetic retirement failure must not poison the journal.
        b.warn("projectile visual retirement", current(e), false);
    }
    s.visuals.erase(id);
    return stopped;
}

bool setEpoch(Battle& b, const Value& epoch_value, double now) {
    const auto& r = epoch_value.resolved();
    if (r.kind == Value::Kind::Boolean) return false;
    const auto epoch = whole(r);
    if (!epoch || *epoch < 0) return false;
    auto& s = *b.projectiles;
    if (s.epoch == epoch) return true;
    // An authority epoch fences presentation as well as simulation; old tracers retire without terminal feedback.
    epochChanged(b);
    s.epoch = *epoch;
    return true;
}

p::Ref showLocalShotWithoutExtra(Battle& b, PyObject* entity, PyObject* burst_count) {
    auto shoot = [&] { return py::call(entity, "showShooting", burst_count, Py_False); };
    auto extras = py::optional(py::optional(entity, "typeDescriptor").get(), "extrasDict");
    auto original = PyDict_Check(extras.get()) ? p::retain(PyDict_GetItemString(extras.get(), "shoot") ? PyDict_GetItemString(extras.get(), "shoot") : Py_None)
                                               : py::call(extras.get(), "get", p::text("shoot").get());
    // No shoot extra: the stock call has no cosmetic work to suppress, only its dispersion handshake.
    if (original.get() == Py_None) return shoot();
    b.feature("shot muzzle retirement", [&] { py::call(original.get(), "stopFor", entity); }, false);
    // The muted extra keeps stock shot bookkeeping (its attributes) while its startFor/stopFor do nothing.
    auto muted = standIn();
    if (PyObject_SetItem(extras.get(), p::text("shoot").get(), muted.get()) < 0) {
        // A non-mutable descriptor cannot be intercepted; keep stock shot convergence.
        PyErr_Clear();
        return shoot();
    }
    PyObject* result = PyObject_CallMethod(entity, const_cast<char*>("showShooting"), const_cast<char*>("(OO)"), burst_count, Py_False);
    PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
    if (!result) PyErr_Fetch(&type, &value, &trace);
    if (PyObject_SetItem(extras.get(), p::text("shoot").get(), original.get()) < 0) {
        Py_XDECREF(result); Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
        throw p::Error("shoot extra restore failed");
    }
    if (!result) { PyErr_Restore(type, value, trace); throw p::Error("showShooting failed"); }
    return p::Ref(result);
}

bool observe(Battle& b, const Value& message) {
    if (nativeEnabled(b)) {
        const double now = b.clock();
        const auto previous = b.projectiles->epoch;
        const bool result = bindPlayer(b, now)->observe(b, message, now);
        if (b.projectiles->epoch != previous) epochChanged(b);
        return result;
    }
    if (b.worker) {
        const auto accepted = needs::botProjectileObserve(b, message, b.clock());
        if (accepted) return *accepted;
    }
    return anchorMessage(b, message);
}

bool snapshot(Battle& b, const Value& message) {
    auto& s = *b.projectiles;
    const double now = b.clock();
    if (nativeEnabled(b)) {
        const auto previous = s.epoch;
        auto rows = bindPlayer(b, now)->ingress(b, message, now);
        if (s.epoch != previous) epochChanged(b);
        for (const auto& row : rows) {
            const auto id = idText(row.get("projectile_id"));
            auto found = s.metas.find(id);
            ProjectileMeta* meta = found == s.metas.end() ? installFresh(b, row) : &found->second;
            if (!meta) continue;
            // Ordered ricochet feedback owns the visual segment transition.
            if (whole(row.get("ricochet_count")).value_or(0) > whole(meta->launch.get("ricochet_count")).value_or(0)) continue;
            presentVisual(b, id, now);
        }
        return true;
    }
    if (b.worker) {
        const auto accepted = needs::botProjectileIngress(b, message, now);
        if (accepted) return *accepted;
    }
    // Replay and the worker before its Bot owner exists present the ledger; no Python authority restores it.
    const auto& rows = message.get("projectiles").resolved();
    if (rows.kind != Value::Kind::Array) return false;
    for (const auto& raw : rows.array) {
        const auto normalized = normalize(raw);
        if (!normalized) {
            reportFailure(idText(raw.get("projectile_id")), "canonical_snapshot", "invalid_cursor");
            continue;
        }
        const auto id = idText(normalized->get("projectile_id"));
        // The ordered terminal can overtake an older snapshot; fence the row.
        if (s.visual_terminals.contains(id) || s.resolution_acks.contains(id)) continue;
        install(b, *normalized);
        visual(b, id, now, {});
    }
    const auto revision = whole(message.get("projectile_revision")).value_or(-1);
    if (revision > s.revision) s.revision = revision;
    return true;
}

bool accept(Battle& b, const Value& event) {
    if (nativeEnabled(b)) {
        const double now = b.clock();
        const auto previous = b.projectiles->epoch;
        const bool result = bindPlayer(b, now)->event(b, event, now);
        if (b.projectiles->epoch != previous) epochChanged(b);
        return result;
    }
    if (b.worker) return needs::botProjectileEvent(b, event, b.clock());
    return false;
}

bool ricochet(Battle& b, const Value& event) {
    if (b.worker) return needs::botProjectileEvent(b, event, b.clock());
    auto& s = *b.projectiles;
    const auto normalized = normalize(event);
    if (!normalized || whole(normalized->get("ricochet_count")).value_or(0) != 1) throw p::Error("canonical projectile ricochet is malformed");
    const auto id = idText(normalized->get("projectile_id"));
    if (s.visual_terminals.contains(id) || s.resolution_acks.contains(id)) return false;
    const auto event_epoch = whole(event.get("authority_epoch"));
    if (s.epoch && event_epoch && *event_epoch != *s.epoch) return false;
    auto found = s.metas.find(id);
    if (found == s.metas.end()) {
        // A non-owner may see a whole short flight between snapshots; restore its validated segment.
        install(b, *normalized);
        found = s.metas.find(id);
    }
    auto& meta = found->second;
    if (meta.local_launch && owns(b, meta.launch)) {
        const auto mismatch = mismatches(meta.launch, *normalized);
        if (!mismatch.empty()) { reportFailure(id, "canonical_echo", "launch_mismatch", mismatch); return false; }
    }
    if (whole(meta.launch.get("ricochet_count")).value_or(0) == 0) {
        meta.hit_vehicle = true;
        stopVisual(b, id, event);
    }
    auto& advanced = install(b, *normalized);
    advanced.pending_resolution = false;
    advanced.presented_ricochet_count = 1;
    advanced.visual_segment_distance = normalized->get("checked_distance").numeric();
    const double now = b.clock();
    ensureVisual(b, id, now);
    if (isAuthority(b)) accept(b, event);
    return true;
}

bool terminal(Battle& b, const Value& event) {
    if (nativeEnabled(b)) {
        const double now = b.clock();
        const auto previous = b.projectiles->epoch;
        bindPlayer(b, now)->event(b, event, now);
        if (b.projectiles->epoch != previous) epochChanged(b);
    }
    if (b.worker) return needs::botProjectileEvent(b, event, b.clock());
    auto& s = *b.projectiles;
    const auto id = idText(event.get("projectile_id"));
    if (id.empty()) throw p::Error("projectile terminal event has no id");
    const auto found = s.metas.find(id);
    const auto& hit = event.get("hit_vehicle").resolved();
    if (found != s.metas.end() && hit.kind == Value::Kind::Boolean) {
        auto& meta = found->second;
        meta.hit_vehicle = hit.boolean;
        const auto visual_found = s.visuals.find(id);
        const auto resolved = event.get("resolved_time_ms");
        if (visual_found != s.visuals.end() && !isNull(resolved)) {
            const double elapsed = std::max(0., (resolved.numeric() - meta.launch.get("segment_start_time_ms").numeric(0.)) / 1000.);
            const auto& v = visual_found->second;
            meta.terminal_velocity = Point{v.velocity[0], v.velocity[1] - v.gravity * elapsed, v.velocity[2]};
        }
    }
    if (event.get("wreck_hit").resolved().kind == Value::Kind::Object) wreckHit(b, id, event);
    stopVisual(b, id, event);
    // A same-epoch terminal fence keeps a duplicated snapshot row from resurrecting the tracer.
    s.visual_terminals.add(id);
    s.metas.erase(id);
    return true;
}

void fireIntentResult(Battle& b, const Value& message) {
    if (b.worker || message.resolved().kind != Value::Kind::Object) return;
    if (message.get("round_id").resolved() != b.start.get("round_id").resolved()) return;
    const auto& accepted_value = message.get("accepted").resolved();
    if (accepted_value.kind != Value::Kind::Boolean || accepted_value.boolean) return;
    auto& s = *b.projectiles;
    const auto id = idText(message.get("projectile_id"));
    const auto found = s.metas.find(id);
    if (found == s.metas.end() || !owns(b, found->second.launch)) return;
    const auto sequence = whole(message.get("intent_seq"));
    if (!sequence || *sequence != whole(found->second.launch.get("fire_intent_seq")).value_or(0)) return;
    auto reason = message.get("reason").resolved().text;
    if (reason.empty()) reason = "rejected";
    reportFailure(id, "launch_rejected", reason);
    const auto group = whole(found->second.launch.get("burst_group_seq"));
    if (nativeEnabled(b) && s.player) s.player->discard(b, id);
    s.metas.erase(id);
    stopVisual(b, id, object());
    s.visual_terminals.add(id);
    // Local gun state was committed at transport acceptance; a delayed rejection cannot rewind it.
    if (b.gun->burst && group && b.gun->burst->group_seq == *group) gun::cancelBurst(b, true);
}

bool shoot(Battle& b, bool continuing) {
    if (!nativeEnabled(b)) return false;
    const double now = b.clock();
    auto* owner = bindPlayer(b, now);
    return accepted(b, owner, owner->trigger(b, now, continuing), now);
}

void advance(Battle& b, double now) {
    if (nativeEnabled(b)) {
        auto result = bindPlayer(b, now)->advance(b, now, false);
        applyEdges(b, result.edges);
        return;
    }
    if (b.worker) needs::botProjectileAdvance(b, now, false);
}

void prebattleSample(Battle& b, double now) {
    if (nativeEnabled(b)) bindPlayer(b, now)->frameSample(b, now);
}

void retire(Battle& b) {
    if (!b.projectiles) return;
    auto& s = *b.projectiles;
    stock_native_projectile_damage::releaseHeTuning(&s.he_tuning);
    if (s.player) { s.player->close(); s.player.reset(); }
    // The stock mover unregisters its camera handler and deletes its models while the Avatar still exists.
    try { tracers::destroy(b); } catch (const std::exception& e) { b.warn("projectile visual teardown", describe(e), false); }
    if (b.gun) {
        gun::retireSound(b);
        weapon::closeWeapon(b);
    }
}
}

// ---- Gun API -------------------------------------------------------------------------------------------------------
namespace gun {
void bind(Battle& b, PyObject* gun_object, std::optional<double> last_tick) {
    b.gun->gun = borrow(gun_object);
    b.gun->last_tick = last_tick;
}

void ammoTick(Battle& b) {
    // Replay timeline presentation owns every reload, clip and ammo edge.
    if (b.replay || !b.running() || !b.engine.server) return;
    try {
        auto vehicle_id = py::attr(b.engine.server.get(), "vehicle_id");
        auto entity = b.entity(p::wide(vehicle_id.get()));
        weapon::bindWeapon(b, entity.get());
        weapon::advanceWeapon(b, b.clock());
        // Use the firing client's native marker, never the server marker.
        b.feature("local gun-marker selection", [&] {
            auto rotator = py::optional(b.engine.avatar.get(), "gunRotator");
            if (rotator.get() != Py_None && p::flag(py::attr(rotator.get(), "showServerMarker").get()))
                py::set(rotator.get(), "showServerMarker", Py_False);
        });
    } catch (const std::exception& e) {
        b.fail(describe(e));
        return;
    }
    constexpr double ammo_seconds = .1;
    cancel(b, b.gun->ammo_callback);
    b.gun->ammo_callback = own(schedule(b, ammo_seconds, [](Battle& battle) { ammoTick(battle); }));
}

void edge(Battle& b) {
    if (b.replay || !b.gun || !b.gun->gun || b.worker || !b.gun->last_tick) return;
    if (!b.engine.server) throw p::Error("player gun edge has no server identity");
    weapon::advanceWeapon(b, b.clock());
}

void frame(Battle& b, double now) {
    if (b.worker || b.replay) return;
    if (b.live) advanceBurst(b);
    b.feature("live reload sound pump", [&] { weapon::audioFrame(b, now); });
}

bool cancelBurst(Battle& b, bool apply_settings) {
    auto& s = *b.projectiles;
    if (!s.player) { b.gun->burst.reset(); return false; }
    const auto burst = b.gun->burst;
    const double now = b.clock();
    const bool changed = s.player->cancel(b, now, apply_settings);
    if (changed && burst && burst->committed > 0) {
        b.feature("shot muzzle retirement", [&] {
            auto entity = b.entity(p::wide(py::attr(b.engine.server.get(), "vehicle_id").get()));
            auto extras = py::optional(py::optional(entity.get(), "typeDescriptor").get(), "extrasDict");
            auto extra = py::call(extras.get(), "get", p::text("shoot").get());
            if (extra.get() != Py_None) py::call(extra.get(), "stopFor", entity.get());
        }, false);
    }
    if (changed && apply_settings) publishShot(b, now);
    return changed;
}

std::vector<std::string> publish(Battle& b, PyObject* entity, const std::vector<HudEdge>& edges) {
    std::vector<std::string> accepted_ids;
    const auto owner = b.gun->runtime;
    auto alive = [&] { return b.gun->runtime == owner && b.running(); };
    for (const auto& e : edges) {
        if (!alive()) break;
        try {
            auto vehicle_id = py::attr(b.engine.server.get(), "vehicle_id");
            auto* avatar = b.engine.avatar.get();
            switch (e.kind) {
            case HudEdge::Kind::Ammo:
                py::call(avatar, "updateVehicleAmmo", vehicle_id.get(), p::integer(e.compact).get(), p::integer(e.quantity).get(),
                         p::integer(e.clip).get(), p::integer(0).get());
                break;
            case HudEdge::Kind::Shell:
                py::call(avatar, "updateVehicleSetting", vehicle_id.get(), p::integer(e.setting).get(), p::integer(e.compact).get());
                break;
            case HudEdge::Kind::Equipment:
                py::call(avatar, "updateVehicleAmmo", vehicle_id.get(), p::integer(e.compact).get(), p::integer(e.quantity).get(),
                         p::integer(e.stage).get(), p::integer(e.remaining_seconds).get());
                break;
            case HudEdge::Kind::Targeting: {
                auto aim = py::call(entity, "getAimParams");
                auto turret = p::item(aim.get(), 0), pitch = p::item(aim.get(), 1);
                if (!alive()) return accepted_ids;
                auto update = py::attr(avatar, "updateTargetingInfo");
                p::Ref arguments(PyTuple_New(9));
                PyTuple_SET_ITEM(arguments.get(), 0, turret.release());
                PyTuple_SET_ITEM(arguments.get(), 1, pitch.release());
                for (int i = 0; i < 7; ++i) PyTuple_SET_ITEM(arguments.get(), 2 + i, p::number(e.targeting[i]).release());
                p::Ref unused(PyObject_Call(update.get(), arguments.get(), nullptr));
                if (!alive()) return accepted_ids;
                b.gun->targeting_signature = e.targeting;
                break;
            }
            case HudEdge::Kind::Reload: {
                py::call(avatar, "updateVehicleGunReloadTime", vehicle_id.get(), p::number(e.remaining).get(), p::number(e.duration).get());
                if (!alive()) return accepted_ids;
                b.gun->reload_event = std::make_pair(e.remaining, e.duration);
                // The native owner already selected and froze this edge; the resource owner supplies only the effect.
                ReloadSound* audio = nullptr;
                if (!b.replay) b.feature("live reload sound", [&] { audio = ensureReloadSound(b); });
                if (!alive()) return accepted_ids;
                if (audio) {
                    if (!e.live) audio->stop();
                    else b.feature("live reload sound", [&] { audio->edge(e.remaining, e.duration, e.reason, e, e.now); });
                }
                break;
            }
            }
            accepted_ids.push_back(e.id);
        } catch (const std::exception& error) {
            if (!alive()) return accepted_ids;
            const char* names[] = {"ammo", "shell", "equipment", "targeting", "reload"};
            b.warn(std::string("native weapon ") + names[int(e.kind)], describe(error), false);
        }
    }
    return accepted_ids;
}

double reloadFactor(Battle& b) { return weapon::reloadFactor(b); }

bool publishKind(Battle& b, const std::string& kind, double now, bool force, double remaining, double duration) {
    if (b.replay) {
        if (!b.gun->replay_publishing || !b.engine.server || !b.gun->gun) return false;
        // Replay presents its recorded gun directly; the live gun runtime never runs.
        auto* gun = stock_native_client_weapon::gun(b.gun->gun.get());
        if (!gun) return false;
        auto vehicle_id = py::attr(b.engine.server.get(), "vehicle_id");
        if (kind == "reload") {
            const auto event = std::make_pair(std::max(0., remaining), std::max(0., duration));
            if (!force && b.gun->reload_event == event) return false;
            py::call(b.engine.avatar.get(), "updateVehicleGunReloadTime", vehicle_id.get(), p::number(event.first).get(), p::number(event.second).get());
            b.gun->reload_event = event;
            return true;
        }
        if (kind == "ammo") {
            auto shots = py::attr(b.gun->gun.get(), "shots");
            std::optional<std::int64_t> current_shell;
            for (Py_ssize_t index = 0; index < p::size(shots.get()); ++index) {
                auto mounted = p::item(shots.get(), index), shell = py::optional(mounted.get(), "shell");
                auto compact_raw = PyDict_Check(shell.get()) ? p::value(shell.get(), "compactDescr") : py::optional(shell.get(), "compactDescr");
                const std::int64_t compact = compact_raw.get() == Py_None ? 0 : p::wide(compact_raw.get());
                const int quantity = std::size_t(index) < gun->ammunition.size() ? gun->ammunition[index] : 0;
                const int in_clip = index == gun->shot ? gun->clip : 0;
                py::call(b.engine.avatar.get(), "updateVehicleAmmo", vehicle_id.get(), p::integer(compact).get(),
                         p::integer(std::max(0, std::min(quantity, 65535))).get(), p::integer(std::max(0, std::min(in_clip, 255))).get(),
                         p::integer(0).get());
                if (index == gun->shot) current_shell = compact;
            }
            if (current_shell) {
                auto settings = py::attr(b.engine.constants.get(), "VEHICLE_SETTING");
                py::call(b.engine.avatar.get(), "updateVehicleSetting", vehicle_id.get(), py::attr(settings.get(), "CURRENT_SHELLS").get(),
                         p::integer(*current_shell).get());
            }
            return true;
        }
        return false;
    }
    if (b.worker || !b.running()) return false;
    return weapon::publish(b, kind, now, force, remaining, duration);
}

void replayMuzzle(Battle& b, PyObject* entity, PyObject* burst_count) {
    auto extras = py::attr(py::attr(entity, "typeDescriptor").get(), "extrasDict");
    p::Ref extra(PyObject_GetItem(extras.get(), p::text("shoot").get()));
    // An already-running native extra owns callbacks and must be retired before the next shot.
    py::call(extra.get(), "stopFor", entity);
    py::call(extra.get(), "startFor", entity, burst_count);
    ++b.gun->replay_muzzle_count;
}

bool equipmentSnapshot(Battle& b, const Value& snapshot, bool present_now) {
    if (b.worker || b.replay || !b.running()) return false;
    return weapon::equipmentSnapshot(b, snapshot, present_now);
}

void retireSound(Battle& b) {
    if (b.gun && b.gun->reload_sound) { b.gun->reload_sound->close(); b.gun->reload_sound.reset(); }
}
}
}
