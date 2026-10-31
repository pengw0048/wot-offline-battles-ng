// The visible-client replica: accepted snapshots into cs::State, lifecycle events, and per-frame remote presentation.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_remote_internal.hpp"
#include "stock_native_battle_vehicles.hpp"
#include "stock_wire_replica_store.hpp"
#include <algorithm>
#include <chrono>

namespace native_battle {
namespace vehicles {
// The vehicle lifecycle owns create/update/destroy events (stock_native_battle_vehicles_lifecycle.cpp).
void apply(Battle& b, const Value& event);
}
namespace remote {
namespace {
using Sample = cs::Sample;
using Bracket = std::optional<std::array<std::int64_t, 4>>;
constexpr const char* axes[] = {"x", "y", "z", "yaw", "pitch", "roll", "aim_yaw", "gun_pitch"};

Value number(double v) { Value x; x.kind = Value::Kind::Number; x.number = v; return x; }
Value integer(std::int64_t v) { Value x; x.kind = Value::Kind::Integer; x.integer = v; return x; }
Value boolean(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
Value text(const std::string& v) { Value x; x.kind = Value::Kind::Text; x.text = v; return x; }
Value object() { Value x; x.kind = Value::Kind::Object; return x; }
Value poseValue(const Pose& v) {
    auto out = object();
    for (unsigned i = 0; i < 8; ++i) out.object[axes[i]] = number(v[i]);
    return out;
}
Value bracketValue(const Bracket& b) {
    if (!b) return Value();
    Value out; out.kind = Value::Kind::Array;
    for (auto v : *b) out.array.push_back(integer(v));
    return out;
}

// Canonical static versions (outfits, effective params) are immutable containers: share one owned copy per source.
Value readValue(Remote& r, PyObject* raw, const std::string& field = {}) {
    if ((field == "outfits" || field == "effective_params") &&
        (std::string(Py_TYPE(raw)->tp_name) == "_StaticDict" || std::string(Py_TYPE(raw)->tp_name) == "_StaticList")) {
        auto& entry = r.static_versions[raw];
        if (!entry.second) entry = {borrow(raw), std::make_shared<const Value>(cs::read(raw))};
        Value result;
        result.kind = Value::Kind::Shared;
        result.reference = entry.second;
        return result;
    }
    if (PyDict_Check(raw)) {
        Value result = object();
        Py_ssize_t cursor = 0;
        PyObject *key = nullptr, *value = nullptr;
        while (PyDict_Next(raw, &cursor, &key, &value)) {
            const auto name = p::string(key);
            result.object.emplace(name, readValue(r, value, name));
        }
        return result;
    }
    if (PyList_Check(raw) || PyTuple_Check(raw)) {
        Value result; result.kind = Value::Kind::Array;
        const auto count = PySequence_Size(raw);
        result.array.reserve(std::size_t(count));
        for (Py_ssize_t i = 0; i < count; ++i) { auto item = p::item(raw, i); result.array.push_back(readValue(r, item.get())); }
        return result;
    }
    return cs::read(raw);
}
// One immutable owned version per accepted actor row.
Value readRows(Remote& r, PyObject* raw) {
    if (!PyList_Check(raw) && !PyTuple_Check(raw)) return readValue(r, raw);
    Value rows; rows.kind = Value::Kind::Array;
    for (Py_ssize_t i = 0; i < PySequence_Size(raw); ++i) {
        auto item = p::item(raw, i);
        auto value = readValue(r, item.get());
        if (value.kind == Value::Kind::Object) {
            Value version; version.kind = Value::Kind::Shared;
            version.reference = std::make_shared<const Value>(std::move(value));
            rows.array.push_back(std::move(version));
        } else rows.array.push_back(std::move(value));
    }
    return rows;
}

Pose blend(const Pose& a, const Pose& b, double progress) {
    Pose out = a;
    for (unsigned i = 0; i < 8; ++i) out[i] += (i >= 3 && i <= 6 ? wrap(b[i] - a[i]) : b[i] - a[i]) * progress;
    return out;
}
Bracket bracket(const Sample& a, const Sample& b) {
    if (!a.revision || !b.revision || *a.revision < 0 || *b.revision < *a.revision || *b.revision > 2147483647 ||
        *b.revision - *a.revision > 255 || a.time < 0. || b.time < a.time || b.time > 9007199254740991. ||
        ((*a.revision == *b.revision) != (a.time == b.time)))
        return std::nullopt;
    return std::array<std::int64_t, 4>{*a.revision, std::int64_t(a.time), *b.revision, std::int64_t(b.time)};
}

// Confirmed-cursor interpolation of one replica actor (delay convergence, consumer pauses, wreck settling, snaps).
bool interpolate(Actor& a, double now, double render_delta, double alpha, Bracket& presentation, bool& snapped) {
    if (a.dead) {
        if (a.settled && a.current == a.target) return false;
        double distance = 0., angle = 0.;
        for (unsigned i = 0; i < 3; ++i) distance += std::pow(a.target[i] - a.current[i], 2.);
        for (unsigned i = 3; i < 8; ++i) angle = std::max(angle, std::abs(i < 7 ? wrap(a.target[i] - a.current[i]) : a.target[i] - a.current[i]));
        a.settled = distance <= .002 * .002 && angle <= .001;
        a.current = a.settled ? a.target : blend(a.current, a.target, alpha);
        snapped = a.settled;
        return true;
    }
    Pose desired = a.target;
    if (a.timed && a.samples.size() >= 2) {
        if (render_delta >= .1) { a.resume = true; a.resume_sample = a.sample_time; a.has_resume_sample = true; }
        a.render_delta = render_delta;
        const double authority_now = a.anchor_time + std::max(0., now - a.anchor_local) * 1000000.;
        const double previous = a.cursor.value_or(a.samples.front().time);
        a.delay = std::max(a.delay, authority_now - render_delta * 1000000. - previous);
        double rate = std::min(1., std::pow((a.ideal - a.delay) / 1000000., 2.));
        if (a.ideal < a.delay) {
            rate = std::max(rate, a.delay > 120000. ? .10 : .005);
            if (a.delay > 120000. && a.samples.back().time - previous > a.ideal) {
                rate = std::max(rate, std::min(.10, (a.delay - a.ideal) / 1000000.));
                if (a.resume && render_delta < .1) rate = std::max(rate, .5);
            }
        }
        if (a.resume && render_delta < .1) rate = std::min(rate, .5);
        const double step = render_delta * rate * 1000000.;
        a.delay = a.ideal > a.delay ? std::min(a.ideal, a.delay + step) : std::max(a.ideal, a.delay - step);
        if (a.has_observed && render_delta < .1 && a.sample_time > a.resume_sample && std::max(a.delay, a.ideal) <= std::max(60000., a.observed) + 15000.)
            a.resume = false;
        a.cursor = std::min({a.samples.back().time, previous + render_delta * (1. + rate) * 1000000., std::max(authority_now - a.delay, previous)});
        if (a.kind == "bot") {
            const bool held = *a.cursor <= previous;
            bool moving = false;
            for (unsigned i = 0; i < 3; ++i) moving = moving || std::abs(a.samples.back().value[i] - a.samples.front().value[i]) > .01;
            ++a.probe_frames;
            a.probe_held += held;
            a.probe_moving_held += held && moving;
            a.probe_max_gap_ms = std::max(a.probe_max_gap_ms, render_delta * 1000.);
        }
        std::size_t consumed = 0;
        while (a.samples.size() - consumed > 2 && *a.cursor >= a.samples[consumed + 1].time) ++consumed;
        if (consumed) a.samples.erase(a.samples.begin(), a.samples.begin() + consumed);
        const Sample *first = &a.samples.front(), *second = &a.samples.back();
        for (std::size_t i = 1; i < a.samples.size(); ++i)
            if (*a.cursor <= a.samples[i].time) { first = &a.samples[i - 1]; second = &a.samples[i]; break; }
        presentation = bracket(*first, *second);
        desired = blend(first->value, second->value, clamp((*a.cursor - first->time) / std::max(1., second->time - first->time), 0., 1.));
    } else if (a.timed) {
        const Sample endpoint{a.sample_time, a.sample_revision, a.target, {}};
        presentation = bracket(endpoint, endpoint);
        if (!a.cursor) a.cursor = a.sample_time;
    } else {
        const double predict = std::min(std::max(0., now - a.target_time), .05);
        for (unsigned i = 0; i < 3; ++i) desired[i] += a.velocity[i] * predict;
    }
    const Pose& snap = a.timed ? (a.teleport ? a.target : desired) : a.target;
    double distance = 0.;
    for (unsigned i = 0; i < 3; ++i) distance += std::pow(snap[i] - a.current[i], 2.);
    snapped = distance > 625.;
    if (snapped) {
        a.current = snap;
        if (a.timed) {
            const Sample endpoint{a.sample_time, a.sample_revision, a.target, {}};
            a.samples = {endpoint};
            a.cursor = a.sample_time;
            a.ideal = a.delay = 0.;
            a.teleport = false;
            a.has_ideal = a.has_delay = false;
            a.warmup_intervals = 0;
            a.warmup_active = false;
            presentation = bracket(endpoint, endpoint);
        }
    } else a.current = a.timed ? desired : blend(a.current, desired, alpha);
    return true;
}

// The interpolated update a vehicle still entering the world receives through the lifecycle.
Value pendingUpdate(const Actor& a, bool snapped, const Bracket& presentation) {
    auto out = object();
    out.object["type"] = text("update");
    out.object["entity"] = text(a.key);
    out.object["kind"] = text(a.kind);
    out.object["id"] = integer(a.id);
    out.object["pose"] = poseValue(a.current);
    out.object["remote"] = boolean(true);
    out.object["interpolated"] = boolean(true);
    out.object["snap"] = boolean(snapped);
    out.object["presentation_time_us"] = a.timed && !a.dead && a.cursor ? integer(std::int64_t(std::round(*a.cursor))) : Value();
    if (!a.dead) out.object["presentation_bracket"] = bracketValue(presentation);
    return out;
}

void report(Battle& b, const std::vector<std::pair<std::string, std::string>>& errors) {
    for (const auto& error : errors) b.warn("native replica " + error.first, error.second, false);
}

// Binds a ready, presented vehicle to its remote presentation; only on entity or descriptor changes.
void attach(Battle& b, ReplicaBinding& e) {
    e.ready = e.has_written = e.has_aim = false;
    Actor& a = *e.data;
    if (!a.ready || a.local || !a.native_remote || !a.presented.remote) return;
    e.engine_id = a.engine_id;
    RemoteVehicle* rv = vehicle(b, e.engine_id);
    if (!rv || !rv->entity || !engineOwnsEntity(e.engine_id, rv->entity.get())) return;
    if (rv->animation) throw p::Error("Visible client replica requires the confirmed-pose Matrix owner");
    auto descriptor = py::attr(rv->entity.get(), "typeDescriptor");
    auto gun = py::optional(descriptor.get(), "gun");
    if (e.descriptor.get() != descriptor.get()) ++a.descriptor_revision;
    auto static_yaw = py::optional(gun.get(), "staticTurretYaw"), static_pitch = py::optional(gun.get(), "staticPitch");
    e.static_yaw = static_yaw.get() == Py_None ? std::nullopt : std::optional<double>(p::scalar(static_yaw.get()));
    e.static_pitch = static_pitch.get() == Py_None ? std::nullopt : std::optional<double>(p::scalar(static_pitch.get()));
    e.authority_geometry = rv->authority_geometry;
    e.descriptor = borrow(descriptor.get());
    e.gun = borrow(gun.get());
    if (!overlay(rv->entity.get())) throw p::Error("Native client pose overlay was not installed by attach");
    // Exact descriptor extraction is an installation boundary; the belt inputs stay native thereafter.
    e.track = trackParams(descriptor.get());
    e.ready = e.managed = true;
    ++b.remote->bindings_revision;
}

void publishMotion(RemoteVehicle& rv, const Point& position, const Point& velocity, const Point& acceleration, double speed,
                   double turn, double yaw, double now) {
    rv.position = position;
    rv.velocity = velocity;
    rv.acceleration = acceleration;
    rv.speed = speed;
    rv.turn_speed = turn;
    rv.last_pose_time = now;
    rv.yaw = yaw;
    p::Ref at(nativeVector(position)), v(nativeVector(velocity)), acc(nativeVector(acceleration));
    OverlayPose pose{at.get(), yaw, rv.provider.get(), speed, turn, v.get(), acc.get()};
    publishOverlay(rv.entity.get(), pose);
}
}

Value readMessage(Battle& b, PyObject* raw, bool manifest) {
    auto& r = *b.remote;
    if (!PyDict_Check(raw)) return readValue(r, raw);
    auto out = object();
    auto field = [&](const char* name) {
        PyObject* value = PyDict_GetItemString(raw, name);
        if (!value) return;
        const std::string n = name;
        out.object.emplace(name, n == "players" || n == "bots" ? readRows(r, value) : readValue(r, value, name));
    };
    field("round_id");
    for (const char* name : {"players", "bots"}) field(name);
    if (!manifest)
        for (const char* name : {"sequence", "server_tick", "bot_state_revision", "motion_time_us", "bot_state_time_us", "timing",
                                 "server_time_ms", "_client_dispatch_delay", "_client_coalesced_timing", "bot_orders", "bot_order_revision"})
            field(name);
    // Replica snapshots were merged on the receive thread; admission completed only the recipient's own row.
    PyObject* native = manifest ? nullptr : PyDict_GetItemString(raw, "_native_snapshot");
    if (native) {
        auto complete = stock_wire::snapshot_from_capsule(native);
        if (!complete) throw p::Error("Native replica snapshot capsule is invalid");
        auto& players = out.object["players"];
        if (players.kind != Value::Kind::Array) players.kind = Value::Kind::Array;
        std::set<std::int64_t> own_rows;
        for (const auto& row : players.array) own_rows.insert(std::int64_t(row.get("id").numeric()));
        for (const auto& row : complete->get("players").array)
            if (!own_rows.count(std::int64_t(row.get("id").numeric()))) players.array.push_back(row);
        out.object["bots"] = complete->get("bots");
    }
    return out;
}

namespace {
void receive(Battle& b, const Value& data, double now, bool manifest) {
    Remote* owner = b.remote.get();
    if (!owner) return;
    auto& s = *b.vehicles;
    s.local_player_id = b.local_player;
    const auto epoch = s.epoch;
    ++owner->received;
    auto events = manifest ? cs::manifest(s, data) : cs::snapshot(s, data, now);
    if (epoch != s.epoch) {
        owner->bindings.clear();
        owner->last_advance.reset();
        owner->static_versions.clear();
        owner->pending_creates.clear();
    }
    auto live = [&] { return b.remote.get() == owner; };
    // A create the lifecycle could not finish yet is retried with the newest accepted state.
    std::vector<Value> retries;
    for (const auto& pending : owner->pending_creates) {
        const Actor* a = s.find(pending.first);
        if (!a || a->missing) continue;
        Value retry = pending.second;
        if (a->state.resolved().kind == Value::Kind::Object) retry.object["state"] = a->state;
        retries.push_back(std::move(retry));
    }
    for (const auto& retry : retries) {
        vehicles::apply(b, retry);
        if (!live()) return;
    }
    owner->lifecycle_events += events.size();
    for (auto& event : events) {
        const auto& payload = event.payload;
        const std::string key = payload.get("entity").text;
        if (payload.get("type").text == "create") owner->pending_creates[key] = payload;
        vehicles::apply(b, payload);
        if (!live()) return;
    }
    for (auto it = owner->pending_creates.begin(); it != owner->pending_creates.end();) {
        const Actor* a = s.find(it->first);
        if (a && a->engine_id) it = owner->pending_creates.erase(it);
        else ++it;
    }
    // The worker presents its own Bots through setPose; only the visible client binds the replica.
    if (!b.worker) refreshBindings(b, true);
}
}

void receiveManifest(Battle& b, const Value& message) { receive(b, message, b.clock(), true); }
void receiveSnapshot(Battle& b, const Value& message, double now) { receive(b, message, now, false); }

void refreshBindings(Battle& b, bool cold) {
    auto& r = *b.remote;
    auto& s = *b.vehicles;
    if (!cold) {
        bool pending = false;
        for (const auto& row : s.actors) {
            const Actor& a = *row.second;
            if (a.local || a.missing || a.tombstone || !a.engine_id) continue;
            auto found = r.bindings.find(row.first);
            if (found == r.bindings.end() || !found->second || !found->second->ready) { pending = true; break; }
        }
        if (!pending) return;
    }
    std::vector<std::pair<std::string, std::string>> errors;
    std::set<std::string> seen;
    for (const auto& row : s.actors) {
        auto data = row.second;
        if (!data || data->missing || data->tombstone || data->local || !data->engine_id) continue;
        seen.insert(row.first);
        auto& item = r.bindings[row.first];
        if (!cold && item && item->ready) continue;
        r.pending_creates.erase(row.first);
        if (!item || item->data != data || item->engine_id != data->engine_id) {
            item.reset(new ReplicaBinding());
            item->key = row.first;
            item->kind = data->kind;
            item->data = data;
            item->engine_id = data->engine_id;
            ++r.bindings_revision;
        }
        auto& e = *item;
        try {
            data->visible = data->presented.spot_visible;
            RemoteVehicle* rv = vehicle(b, e.engine_id);
            if (e.ready && (!rv || !rv->entity || !engineOwnsEntity(e.engine_id, rv->entity.get()))) e.ready = false;
            if (!e.ready) attach(b, e);
            else {
                auto descriptor = py::attr(rv->entity.get(), "typeDescriptor");
                auto gun = py::optional(descriptor.get(), "gun");
                if (descriptor.get() != e.descriptor.get() || gun.get() != e.gun.get()) attach(b, e);
            }
            rv = vehicle(b, e.engine_id);
            if (e.ready && rv && rv->entity) {
                auto draw = py::optional(rv->entity.get(), "_offlineNativeDrawVisible");
                if (draw.get() != Py_None) data->visible = data->visible && p::flag(draw.get());
                data->detached = p::flag(py::optional(rv->entity.get(), "isTurretDetached").get());
            }
        } catch (const std::exception& error) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            e.ready = false;
            errors.emplace_back(row.first, describe(error));
        }
    }
    for (auto i = r.bindings.begin(); i != r.bindings.end();)
        if (!seen.count(i->first)) i = r.bindings.erase(i);
        else ++i;
    report(b, errors);
}

bool owns(const Battle& b, const Actor& v) {
    if (!b.remote || !v.native_remote || !v.ready || v.local) return false;
    auto found = b.remote->bindings.find(v.key);
    return found != b.remote->bindings.end() && found->second && found->second->managed;
}

// One render frame of confirmed remote presentation. The hidden worker presents through setPose instead.
void advance(Battle& b, double now) {
    if (b.worker || !b.remote) return;
    Remote* owner = b.remote.get();
    refreshBindings(b, false);
    if (b.remote.get() != owner) return;
    if (!std::isfinite(now)) throw p::Error("Native client render time invalid");
    const double render_delta = owner->last_advance ? std::max(0., now - *owner->last_advance) : .016;
    owner->last_advance = now;
    b.vehicles->last_advance = now;
    const double alpha = 1. - std::exp(-20. * clamp(render_delta, .001, .1));
    std::vector<Value> pending;
    std::vector<std::pair<std::string, std::string>> errors;
    for (auto& item : owner->bindings) {
        if (!item.second) continue;
        auto& e = *item.second;
        Actor& a = *e.data;
        if (!a.has_current || !a.has_target || a.local) continue;
        try {
            Bracket presentation;
            bool snapped = false;
            if (!interpolate(a, now, render_delta, alpha, presentation, snapped)) continue;
            a.presentation_bracket = presentation;
            if (!e.ready) { pending.push_back(pendingUpdate(a, snapped, presentation)); continue; }
            RemoteVehicle* rv = vehicle(b, e.engine_id);
            if (!rv || !rv->entity || !engineOwnsEntity(e.engine_id, rv->entity.get())) { e.ready = false; continue; }
            a.presented.presented_pose = a.current;
            a.presented.presented_time_us = a.timed && !a.dead && a.cursor ? std::optional<std::int64_t>(std::int64_t(std::round(*a.cursor)))
                                                                         : std::nullopt;
            presentReplicaPose(b, e, *rv, now);
            if (b.remote.get() != owner) return;
            replicaTracks(b, e, *rv, now);
            if (b.remote.get() != owner) return;
        } catch (const std::exception& error) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            if (b.remote.get() != owner) return;
            e.has_written = e.has_aim = false;
            errors.emplace_back(e.key, describe(error));
        }
    }
    for (const auto& event : pending) {
        vehicles::apply(b, event);
        if (b.remote.get() != owner) return;
    }
    report(b, errors);
}

// The pose, motion, aim and collision pose of one replica-owned vehicle.
void presentReplicaPose(Battle& b, ReplicaBinding& e, RemoteVehicle& rv, double now) {
    Actor& a = *e.data;
    const Pose& current = a.current;
    if (!e.has_written || e.written != current) {
        // Clear before the first native write: a partial failure must retry even when the next sample returns.
        e.has_written = false;
        p::Ref rounded(nativeVector({current[0], current[1], current[2]}));
        const Point actual = nativeXYZ(rounded.get());
        const Point rotation{principal(current[3]), principal(current[4]), principal(current[5])};
        const auto cached = rv.matrix_cache.get() == rv.matrix.get() ? rv.matrix_pose : std::nullopt;
        const bool rotated = !cached || (*cached)[3] != rotation[0] || (*cached)[4] != rotation[1] || (*cached)[5] != rotation[2];
        const bool translated = rotated || (*cached)[0] != actual[0] || (*cached)[1] != actual[1] || (*cached)[2] != actual[2];
        rv.matrix_pose.reset();
        if (translated) nativeMatrix(rv.matrix.get(), actual, rotation, rotated);
        rv.matrix_cache = borrow(rv.matrix.get());
        rv.matrix_pose = std::array<double, 6>{actual[0], actual[1], actual[2], rotation[0], rotation[1], rotation[2]};
        Point velocity{}, acceleration{};
        double speed = 0., turn = 0.;
        if (rv.last_pose_time && now > *rv.last_pose_time) {
            const float inverse = float(1. / (now - *rv.last_pose_time));
            for (unsigned i = 0; i < 3; ++i) {
                velocity[i] = float(float(actual[i] - rv.position[i]) * inverse);
                acceleration[i] = float(float(velocity[i] - rv.velocity[i]) * inverse);
            }
            speed = velocity[0] * std::sin(rotation[0]) + velocity[2] * std::cos(rotation[0]);
            turn = wrap(rotation[0] - rv.yaw) / (now - *rv.last_pose_time);
        }
        rv.pitch = rotation[1];
        rv.roll = rotation[2];
        rv.render_pose = {actual[0], actual[1], actual[2], rotation[0], rotation[1], rotation[2]};
        publishMotion(rv, actual, velocity, acceleration, speed, turn, rotation[0], now);
        e.written = current;
        e.has_written = true;
        a.presented.settled_signature.reset();
    } else if (std::abs(a.speed) <= .05 && std::abs(a.drive) <= .5 && std::abs(a.rotation) <= .5 &&
               (rv.velocity != Point{} || rv.turn_speed != 0.)) {
        publishMotion(rv, rv.position, Point{}, Point{}, 0., 0., rv.yaw, now);
    }
    const std::array<double, 4> input{current[3], current[4], current[6], current[7]};
    if (!e.has_aim || input != e.last_aim) {
        e.has_aim = false;
        const double relative = e.static_yaw.value_or(wrap(current[6] - current[3])), pitch = e.static_pitch.value_or(current[7]);
        if (!rv.aim_relative_yaw || *rv.aim_relative_yaw != relative) {
            nativeMatrix(py::attr(rv.aim.get(), "turretMatrix").get(), {}, {relative, 0., 0.}, true);
            rv.aim_relative_yaw = relative;
        }
        if (!rv.aim_gun_pitch || *rv.aim_gun_pitch != pitch) {
            nativeMatrix(py::attr(rv.aim.get(), "gunMatrix").get(), {}, {0., pitch, 0.}, true);
            rv.aim_gun_pitch = pitch;
        }
        rv.aim_desired_gun_pitch = pitch;
        py::set(rv.entity.get(), "_aim_yaw", p::number(current[6]).get());
        py::set(rv.entity.get(), "_gun_pitch", p::number(current[7]).get());
        e.last_aim = input;
        e.has_aim = true;
    }
    // Collision and RAM consumers read the same current pose as the renderer.
    Value collision = poseValue(current);
    collision.object.erase("aim_yaw");
    collision.object["turret_yaw"] = number(wrap(current[6] - current[3]));
    collision.object["siege_state"] = integer(a.siege_state);
    a.presented.collision_pose = std::move(collision);
}

// The stock 20 Hz belt feed of one replica-owned vehicle.
void replicaTracks(Battle& b, ReplicaBinding& e, RemoteVehicle& rv, double now) {
    Actor& a = *e.data;
    double turn = 0.;
    if (e.last_track_time && now > *e.last_track_time) turn = wrap(a.current[3] - e.last_track_yaw) / (now - *e.last_track_time);
    e.last_track_time = now;
    e.last_track_yaw = a.current[3];
    if (e.next_track_update && now + 1.e-9 < *e.next_track_update) return;
    const double next = e.next_track_update.value_or(now);
    e.next_track_update = next + std::max(1., std::floor(std::max(0., now - next) / .05) + 1.) * .05;
    // Stock may replace the appearance or filter while applying damage: read them only at this feed boundary.
    auto draw = py::optional(rv.entity.get(), "_offlineNativeDrawVisible");
    const bool hidden = draw.get() != Py_None && !p::flag(draw.get());
    const bool was_hidden = rv.track_feed_hidden;
    if (hidden && was_hidden) return;
    if (hidden) { rv.track_feed_hidden = true; rv.track_feed.reset(); }
    else if (was_hidden) { rv.track_feed_hidden = false; rv.track_feed.reset(); rv.track_mode.reset(); }
    double speed = a.speed;
    const int flags = (speed > .05 ? 1 : speed < -.05 ? 2 : 0) | (turn < -.02 ? 4 : turn > .02 ? 8 : 0);
    const int power = a.alive ? (flags ? 2 : 1) : 0;
    if (!a.airborne && !e.track.around_center && std::abs(turn) > 1.e-9) {
        const double radius = std::max(0., e.track.center - std::abs(speed / turn));
        const double direction = speed < -1.e-9 || (std::abs(speed) <= 1.e-9 && a.drive < 0.) ? -1. : 1.;
        speed += radius * direction * (turn > 0. ? 1. : -1.) * turn;
    }
    double left = clamp(clamp(speed + turn * e.track.center, -e.track.speed_cap, e.track.speed_cap), -15., 30.);
    double right = clamp(clamp(speed - turn * e.track.center, -e.track.speed_cap, e.track.speed_cap), -15., 30.);
    if (hidden) left = right = 0.;
    auto appearance = py::optional(rv.entity.get(), "appearance");
    if (appearance.get() == Py_None) return;
    auto flying = py::optional(appearance.get(), "flyingInfoProvider");
    const bool left_contact = !p::flag(py::optional(flying.get(), "isLeftSideFlying").get());
    const bool right_contact = !p::flag(py::optional(flying.get(), "isRightSideFlying").get());
    const std::array<int, 2> mode{power, a.alive ? flags : 0};
    const std::array<double, 4> feed{std::round(left * 1000.) / 1000., std::round(right * 1000.) / 1000., double(mode[0]), double(mode[1])};
    const std::array<bool, 2> contacts{left_contact, right_contact};
    if (rv.track_feed == feed && rv.track_feed_contacts == contacts) { rv.track_outcome = "deduplicated"; return; }
    auto filter = py::optional(rv.entity.get(), "filter");
    auto appearance_filter = py::optional(appearance.get(), "filter");
    auto movement = py::optional(filter.get(), "movementInfo");
    auto setter = py::optional(filter.get(), "setTracksSpeed");
    bool updated = false;
    if (filter.get() == appearance_filter.get() && movement.get() != Py_None && PyCFunction_Check(setter.get())) {
        py::invoke(setter.get(), p::number(left).get(), left_contact ? Py_True : Py_False, p::number(right).get(), right_contact ? Py_True : Py_False);
        updated = true;
    }
    if (!hidden && rv.track_mode != mode) {
        applyTrackMode(appearance.get(), mode);
        rv.track_mode = mode;
    }
    applyTrackScroll(appearance.get(), left, right);
    if (updated) { rv.track_feed = feed; rv.track_feed_contacts = contacts; }
    rv.track_written = std::make_pair(left, right);
    rv.track_outcome = hidden ? "hidden and settling" : updated ? "engine and python" : "python only";
    rv.capabilities["engine_owned_track_motion"] = updated;
}

std::vector<MotionProbe> probes(Battle& b, double now) {
    std::vector<MotionProbe> out;
    for (auto& entry : b.vehicles->actors) {
        Actor& a = *entry.second;
        if (!a.probe_frames) continue;
        MotionProbe row;
        row.key = entry.first;
        row.frames = a.probe_frames;
        row.held = a.probe_held;
        row.moving_held = a.probe_moving_held;
        row.max_gap_ms = a.probe_max_gap_ms;
        row.source_time_us = a.previous_sample;
        row.source_revision = a.sample_revision;
        row.presentation_time_us = a.cursor;
        row.source_age_ms = std::max(0., now - a.target_time) * 1000.;
        if (a.has_ideal) row.ideal_delay_us = a.ideal;
        if (a.has_delay) row.actual_delay_us = a.delay;
        row.source = a.target;
        row.presented = a.current;
        out.push_back(row);
        a.probe_frames = a.probe_held = a.probe_moving_held = 0;
        a.probe_max_gap_ms = 0.;
    }
    return out;
}
}
}
