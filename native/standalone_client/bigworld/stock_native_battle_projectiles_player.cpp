// The visible player's native shot owner: trigger freezing, chord advance, damage resolution and the reliable
// launch/progress/terminal queue. Battle state comes from native_battle; stock objects stay engine handles.
#include "pch.hpp"
#include "stock_native_battle_projectiles.hpp"
#include "stock_native_client_projectile.hpp"
#include "stock_native_client_projectile_world.hpp"
#include "stock_native_client_input.hpp"
#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_input_controls_python.hpp"
#include "stock_native_client_weapon_runtime.hpp"
#include "stock_native_client_weapon_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_drive_status.hpp"
#include "stock_native_projectile_wire.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <chrono>
#include <set>

namespace native_battle {
namespace {
namespace proj = stock_native_client_projectile;
namespace shot = stock_native_projectile;
namespace damage = stock_native_projectile_damage;
namespace weapon = stock_native_client_weapon_runtime;

p::Ref field(PyObject* o, const char* n) {
    if (!o || o == Py_None) return p::retain(Py_None);
    if (PyDict_Check(o)) return p::value(o, n);
    auto name = p::text(n);
    auto* v = p::directAttribute(o, name.get());
    if (!v && PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}
std::optional<std::int64_t> integerOf(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Integer) return r.integer;
    if (r.kind == Value::Kind::Number && std::isfinite(r.number)) return std::int64_t(r.number);
    return std::nullopt;
}
Value number(double v) { Value x; x.kind = Value::Kind::Number; x.number = v; return x; }
Value integer(std::int64_t v) { Value x; x.kind = Value::Kind::Integer; x.integer = v; return x; }
Value boolean(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
Value text(std::string v) { Value x; x.kind = Value::Kind::Text; x.text = std::move(v); return x; }
Value points(const Point& v) { Value x; x.kind = Value::Kind::Array; for (double c : v) x.array.push_back(number(c)); return x; }
Value object() { Value x; x.kind = Value::Kind::Object; return x; }

// The canonical source_shot wire of one frozen descriptor (shell physics and stun, never cosmetic keys).
Value sourceWire(const damage::LaunchDescriptor& d) {
    const auto& s = d.shot;
    auto out = object(), shell = object();
    out.object["speed"] = number(d.speed);
    out.object["gravity"] = number(d.gravity);
    out.object["maxDistance"] = number(d.maximum);
    Value piercing; piercing.kind = Value::Kind::Array; piercing.array = {number(s.shell.piercing_near), number(s.shell.piercing_far)};
    out.object["piercingPower"] = piercing;
    out.object["deadeye"] = boolean(s.deadeye);
    shell.object["kind"] = text(s.shell.kind);
    shell.object["caliber"] = number(s.shell.caliber);
    Value damage_values; damage_values.kind = Value::Kind::Array;
    damage_values.array = {number(s.shell.damage), number(s.device_damage_mean.value_or(0.))};
    shell.object["damage"] = damage_values;
    shell.object["explosionRadius"] = number(d.raw_radius);
    if (d.explosion_factors) {
        shell.object["explosionDamageFactor"] = number(s.shell.explosion_damage_factor);
        shell.object["explosionDamageAbsorptionFactor"] = number(s.shell.explosion_absorption_factor);
        shell.object["explosionEdgeDamageFactor"] = number(s.edge_damage_factor);
    }
    if (s.stun) {
        const auto& v = *s.stun;
        auto stun = object();
        const char* names[] = {"stunRadius", "stunDuration", "stunFactor", "guaranteedStunDuration", "damageDurationCoeff",
                               "guaranteedStunEffect", "damageEffectCoeff"};
        const double values[] = {v.radius, v.duration, v.factor, v.guaranteed_duration, v.damage_duration, v.guaranteed_effect,
                                 v.damage_effect};
        for (unsigned i = 0; i < 7; ++i) stun.object[names[i]] = number(values[i]);
        shell.object["stun"] = stun;
    }
    out.object["shell"] = shell;
    return out;
}
Value launchWire(const proj::Frozen& f, std::int64_t epoch) {
    const auto& l = f.flight;
    auto out = object();
    auto& o = out.object;
    o["round_id"] = integer(f.round);
    o["projectile_id"] = text(l.id);
    o["shooter_kind"] = text("player");
    o["shooter_id"] = integer(l.shooter);
    o["source_vehicle"] = text(l.vehicle);
    o["source_shot"] = sourceWire(f.source);
    o["shot_seq"] = integer(l.sequence);
    o["fire_intent_seq"] = integer(l.sequence);
    o["fire_input_seq"] = integer(f.input_sequence);
    o["shell_index"] = integer(l.shell_index);
    o["burst_group_seq"] = integer(l.group_sequence);
    o["burst_index"] = integer(l.burst_index);
    o["burst_count"] = integer(l.burst_count);
    o["origin"] = points(l.origin);
    o["velocity"] = points(l.velocity);
    o["range_origin"] = points(l.range_origin);
    o["gravity"] = number(l.gravity);
    o["max_distance"] = number(l.maximum_distance);
    o["max_time_ms"] = integer(l.maximum_ms);
    o["is_he"] = boolean(l.he);
    o["splash_radius"] = number(l.splash_radius);
    o["penetration_factor"] = number(l.penetration_factor);
    o["launch_server_time_ms"] = integer(l.launch_ms);
    o["authority_epoch"] = integer(epoch);
    return out;
}
Value presentation(const proj::Frozen& f, std::int64_t epoch) {
    auto out = launchWire(f, epoch);
    const auto& l = f.flight;
    auto& o = out.object;
    o["kind"] = text("shot");
    o["attacker"] = integer(l.shooter);
    o["maxDistance"] = number(l.maximum_distance);
    o["segment_origin"] = points(l.segment_origin);
    o["segment_velocity"] = points(l.segment_velocity);
    o["segment_start_time_ms"] = integer(l.segment_ms);
    o["ricochet_count"] = integer(l.segment);
    o["base_penetration_multiplier"] = number(l.multiplier);
    o["checked_through_ms"] = integer(l.checked_ms);
    o["checked_distance"] = number(l.checked_distance);
    o["piercing_loss"] = number(l.piercing_loss);
    o["local_launch_time"] = number(f.local_time);
    o["local_gun_committed"] = boolean(true);
    return out;
}
// The exact float wire rounding every queued projectile message carries.
void roundWire(PyObject* value) {
    if (PyDict_Check(value)) {
        Py_ssize_t at = 0; PyObject *key, *child;
        while (PyDict_Next(value, &at, &key, &child)) {
            if (PyFloat_CheckExact(child)) {
                auto rounded = p::number(proj::wireRound(PyFloat_AS_DOUBLE(child)));
                if (PyDict_SetItem(value, key, rounded.get()) < 0) throw p::Error("Native projectile wire normalization failed");
            } else roundWire(child);
        }
    } else if (PyList_Check(value)) {
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(value); ++i) {
            auto* child = PyList_GET_ITEM(value, i);
            if (PyFloat_CheckExact(child)) PyList_SetItem(value, i, p::number(proj::wireRound(PyFloat_AS_DOUBLE(child))).release());
            else roundWire(child);
        }
    }
}
bool parseFrozen(PyObject* raw, proj::Frozen& out) {
    if (!shot::readPlayerLaunch(raw, out.flight)) return false;
    out.player = out.flight.shooter;
    out.round = p::integral(raw, "round_id");
    out.input_sequence = p::integral(raw, "fire_input_seq");
    auto source = field(raw, "source_shot"), shell = field(source.get(), "shell");
    out.source.speed = p::numeric(source.get(), "speed");
    out.source.gravity = out.flight.gravity;
    out.source.maximum = out.flight.maximum_distance;
    out.source.raw_radius = p::numeric(shell.get(), "explosionRadius");
    out.source.explosion_factors = PyDict_GetItemString(shell.get(), "explosionDamageFactor") != nullptr;
    out.source.shot = out.flight.shot;
    proj::normalize(out);
    return true;
}
void applyDeferred(const DeferredSettings& deferred, stock_native_client_weapon::Gun& gun, std::int64_t current_setting,
                   std::int64_t next_setting, std::int64_t partial_setting) {
    if (!deferred.present) return;
    for (const auto& row : deferred.rows) {
        if (row.first == current_setting) gun.select(int(row.second));
        else if (row.first == partial_setting) gun.partialReload();
        else if (row.first == next_setting) gun.request(int(row.second));
    }
}

class Owner final : public PlayerProjectiles {
    std::shared_ptr<cs::State> state_;
    std::uint64_t epoch_, generation_;
    bool retired_ = false;
    Object client_;
    stock_native_client_drive::SceneAccess scene_;
    proj::World world_;
    shot::Owner flights_;
    proj::Lifecycle lifecycle_;
    std::vector<damage::LaunchDescriptor> shots_;
    std::vector<std::array<std::optional<unsigned>, 2>> stickers_;
    Value effective_;
    std::uint64_t descriptor_revision_ = ~std::uint64_t(0);
    PyObject* descriptor_identity_ = nullptr;
    BW::MatrixProviderPtr stabilised_;
    proj::Barrel water_barrel_;
    std::map<std::string, std::int64_t> visuals_;
    std::set<std::string> visual_terminals_;
    std::set<std::pair<std::string, std::int64_t>> terminal_presentations_;
    // Seconds a shot keeps its camouflage penalty (spotting.SHOT_CAMOUFLAGE_SECONDS).
    static constexpr double shot_camouflage_ = .75;
    std::int64_t current_setting_ = 0, next_setting_ = 0, partial_setting_ = 0;

    Actor& local() {
        auto* a = state_->find("player:" + std::to_string(state_->local_player_id));
        if (!a || !a->local || !a->ready || !a->has_current || a->tombstone) throw p::Error("Native projectile local actor is unavailable");
        return *a;
    }
    void installShots(Battle& b) {
        auto& a = local();
        auto* descriptor = stock_native_client_query::descriptor(a);
        if (!descriptor) throw p::Error("Native player projectile descriptor is unavailable");
        const auto& next = a.state.get("effective_params");
        if (descriptor_identity_ == descriptor && descriptor_revision_ == a.descriptor_revision && effective_ == next && !shots_.empty()) return;
        std::vector<damage::LaunchDescriptor> installed;
        for (const auto& row : next.get("gun").get("shots").resolved().array) {
            p::Ref source(cs::write(row.get("source_shot")));
            damage::LaunchDescriptor value;
            if (!damage::readLaunchDescriptor(source.get(), value)) throw p::Error("Native accepted player shot is invalid");
            installed.push_back(std::move(value));
        }
        if (installed.empty()) throw p::Error("Native accepted player shots are unavailable");
        std::vector<std::array<std::optional<unsigned>, 2>> cosmetics(installed.size());
        auto gun = field(descriptor, "gun"), mounted = field(gun.get(), "shots");
        auto cache = field(b.engine.vehicles.get(), "g_cache"), effects = field(cache.get(), "shotEffects");
        for (Py_ssize_t i = 0; i < std::min<Py_ssize_t>(p::size(mounted.get()), Py_ssize_t(cosmetics.size())); ++i) {
            auto mounted_shot = p::item(mounted.get(), i), shell_value = field(mounted_shot.get(), "shell");
            auto effect = field(shell_value.get(), "effectsIndex");
            auto* raw = PyObject_GetItem(effects.get(), effect.get());
            if (!raw) { PyErr_Clear(); continue; }
            p::Ref owner(raw);
            auto targets = field(owner.get(), "targetStickers");
            for (unsigned hit = 0; hit < 2; ++hit) {
                auto value = field(targets.get(), hit ? "armorPierced" : "armorResisted");
                if (value.get() == Py_None || PyBool_Check(value.get())) continue;
                const auto n = p::wide(value.get());
                if (n >= 0 && n <= 255) cosmetics[i][hit] = unsigned(n);
            }
        }
        auto provider = field(b.engine.avatar.get(), "_PlayerAvatar__ownVehicleStabMProv");
        BW::MatrixProviderPtr stable;
        if (provider.get() == Py_None || BW::Script::setData(provider.get(), stable, "player stabilized matrix") != 0 || !stable)
            throw p::Error("Native player shot stabilised matrix is unavailable");
        const auto* g = stock_native_client_query::geometry(a);
        if (!g || !g->components[3].bounds_available) throw p::Error("Native player barrel bounds are unavailable");
        auto hull = field(descriptor, "hull"), positions = field(hull.get(), "turretPositions"), index = field(descriptor, "activeTurretPosition");
        const auto active = p::wide(index.get());
        if (active < 0 || active >= p::size(positions.get())) throw p::Error("Native player active turret mount is unavailable");
        auto mount = p::item(positions.get(), static_cast<Py_ssize_t>(active));
        BW::Vector3 turret_mount;
        if (BW::Script::setData(mount.get(), turret_mount, "player turret mount") != 0) throw p::Error("Native player active turret mount is not a Vector3");
        water_barrel_.hull = g->hull_offset;
        water_barrel_.turret = {turret_mount.x, turret_mount.y, turret_mount.z};
        water_barrel_.gun = g->gun_offset;
        water_barrel_.length = g->components[3].maximum[2];
        stabilised_ = stable;
        shots_ = std::move(installed);
        stickers_ = std::move(cosmetics);
        effective_ = next;
        descriptor_revision_ = a.descriptor_revision;
        descriptor_identity_ = descriptor;
    }
    bool freeze(Battle& b, double now, proj::Trigger& t) {
        auto& a = local();
        installShots(b);
        auto rotator = field(b.engine.avatar.get(), "gunRotator");
        auto turret_raw = field(rotator.get(), "_VehicleGunRotator__turretYaw"), pitch_raw = field(rotator.get(), "_VehicleGunRotator__gunPitch");
        const double turret = turret_raw.get() == Py_None ? NAN : p::scalar(turret_raw.get());
        const double pitch = pitch_raw.get() == Py_None ? NAN : p::scalar(pitch_raw.get());
        auto dispersion = field(rotator.get(), "_VehicleGunRotator__dispersionAngles");
        if (!std::isfinite(turret) || !std::isfinite(pitch) || dispersion.get() == Py_None) return false;
        auto angle = p::item(dispersion.get(), 0);
        t.dispersion = p::scalar(angle.get());
        if (t.dispersion < 0.) return false;
        const auto checkpoint = weapon::currentCheckpoint(b);
        if (checkpoint.shot_index < 0 || std::size_t(checkpoint.shot_index) >= shots_.size()) return false;
        t.shell = checkpoint.shot_index;
        t.mounted = shots_[t.shell];
        t.stickers = stickers_[t.shell];
        const auto* g = stock_native_client_query::geometry(a);
        if (!g) return false;
        auto access = stock_native_client_drive::driverAccess(b);
        if (!access.driver) return false;
        auto& controls = access.driver->controls();
        controls.aim_yaw = a.current[3] + turret;
        controls.gun_pitch = pitch;
        b.local->controls.aim_yaw = controls.aim_yaw;
        b.local->controls.gun_pitch = controls.gun_pitch;
        BW::Matrix body, turret_world, gun_world;
        stabilised_->matrix(body);
        turret_world.setRotateY(float(turret));
        const auto offset = BW::Vector3{float(g->hull_offset[0]), float(g->hull_offset[1]), float(g->hull_offset[2])} +
                            BW::Vector3{float(g->turret_offset[0]), float(g->turret_offset[1]), float(g->turret_offset[2])};
        turret_world.translation(offset);
        turret_world.postMultiply(body);
        gun_world.setRotateX(float(pitch));
        gun_world.postMultiply(turret_world);
        const auto origin = turret_world.applyPoint(BW::Vector3{float(g->gun_offset[0]), float(g->gun_offset[1]), float(g->gun_offset[2])});
        const auto direction = gun_world.applyVector(BW::Vector3{0.f, 0.f, float(t.mounted.speed)});
        t.origin = {origin.x, origin.y, origin.z};
        auto unit = direction;
        unit.normalise();
        t.direction = {unit.x, unit.y, unit.z};
        // The source water gate uses the physical barrel end in the current ground transform,
        // separately from the stabilised muzzle used for the shot ray.
        const auto barrel = proj::barrelPoint(water_barrel_, {a.current[0], a.current[1], a.current[2]}, {a.current[3], a.current[4], a.current[5]},
                                              turret, pitch);
        const Point up{barrel[0], barrel[1] + .1, barrel[2]};
        double water = 0.;
        const auto status = scene_.scene->water(barrel, up, water);
        if (status == stock_native_world::QueryStatus::Failed || water > 0.) return false;
        const auto server = flights_.serverTime(now);
        if (!server) return false;
        t.server_ms = *server;
        t.round = std::int64_t(state_->round.numeric());
        t.player = a.id;
        t.team = int(a.state.get("team").numeric());
        t.vehicle = a.state.get("vehicle").resolved().text;
        if (t.vehicle.empty()) t.vehicle = a.manifest.get("vehicle").resolved().text;
        t.position = {a.current[0], a.current[1], a.current[2]};
        t.now = now;
        t.deadeye = weapon::livingSkillCount(b, "gunner_sniper") > 0;
        t.reload_factor = weapon::reloadFactor(b);
        t.perfect = b.config.get("perfect_accuracy").truth();
        return true;
    }
    // The player burst every subshot of one group shares.
    PlayerBurst* burstMirror(Battle& b, const proj::Frozen& f) {
        if (f.flight.burst_count <= 1) return nullptr;
        auto& current = b.gun->burst;
        if (current && current->group_seq == f.flight.group_sequence) return &*current;
        PlayerBurst out;
        out.count = int(f.flight.burst_count);
        out.interval = lifecycle_.burst() ? lifecycle_.burst()->interval : 0.;
        out.committed = int(f.flight.burst_index);
        out.started_at = lifecycle_.burst() ? lifecycle_.burst()->start : f.local_time;
        out.shell_index = int(f.flight.shell_index);
        out.group_seq = f.flight.group_sequence;
        out.generation = b.generation;
        current = std::move(out);
        return &*current;
    }
    std::vector<Edge> dispatch(Battle& b, double now) {
        std::vector<Edge> edges;
        p::Ref rows(shot::packDispatches(flights_.dispatches()));
        for (Py_ssize_t i = 0; i < p::size(rows.get()); ++i) {
            if (!current(b)) break;
            auto row = p::item(rows.get(), i), serial = p::item(row.get(), 0), kind = p::item(row.get(), 1), wire = p::item(row.get(), 2);
            const auto type = p::string(kind.get());
            auto addProof = [&](PyObject* value) {
                auto id = field(value, "projectile_id");
                const auto found = lifecycle_.launches().find(p::string(id.get()));
                if (found == lifecycle_.launches().end()) throw p::Error("Native player projectile launch proof is unavailable");
                auto proof = launchWire(found->second, flights_.epoch().value());
                proof.object.erase("round_id");
                proof.object.erase("authority_epoch");
                p::Ref packed(cs::write(proof));
                p::set(value, "launch_proof", packed);
            };
            if (type == "progress") {
                auto cursors = field(wire.get(), "cursors");
                for (Py_ssize_t c = 0; c < p::size(cursors.get()); ++c) { auto value = p::item(cursors.get(), c); addProof(value.get()); }
            } else {
                addProof(wire.get());
                auto id = field(wire.get(), "projectile_id");
                const auto key = p::string(id.get());
                const auto at = flights_.flights().find(key);
                if (at != flights_.flights().end() && terminal_presentations_.insert({key, at->second.canonical.segment}).second) {
                    Edge edge;
                    edge.kind = Edge::Kind::Pending;
                    edge.id = key;
                    edge.hit_vehicle = at->second.terminal && at->second.terminal->hit_vehicle;
                    if (at->second.terminal_cursor) {
                        const auto& cursor = *at->second.terminal_cursor;
                        Point velocity;
                        for (unsigned c = 0; c < 3; ++c)
                            velocity[c] = cursor.velocity[c] + cursor.gravity[c] * std::max(0., cursor.cursor_time - cursor.launch_time);
                        edge.terminal_velocity = velocity;
                    }
                    edges.push_back(std::move(edge));
                }
            }
            p::set(wire.get(), "type", p::text("projectile_" + type));
            p::set(wire.get(), "round_id", p::integer(std::int64_t(state_->round.numeric())));
            roundWire(wire.get());
            bool sent = false;
            try { sent = stock_native_client_input::enqueue(client_.get(), wire.get()); }
            catch (const std::exception& error) {
                PyErr_Clear();
                log("Native player projectile queue " + type + ": " + error.what());
            }
            flights_.acknowledge(std::uint64_t(p::wide(serial.get())), sent, now);
            if (sent && type == "ricochet") {
                auto id = field(wire.get(), "projectile_id"), impact = field(wire.get(), "impact");
                const auto key = p::string(id.get());
                const auto at = flights_.flights().find(key);
                if (at != flights_.flights().end()) {
                    const auto& l = at->second.canonical;
                    Edge edge;
                    edge.kind = Edge::Kind::Ricochet;
                    edge.id = key;
                    edge.impact = cs::read(impact.get());
                    edge.segment = object();
                    auto& s = edge.segment.object;
                    s["segment_origin"] = points(l.segment_origin);
                    s["segment_velocity"] = points(l.segment_velocity);
                    s["segment_start_time_ms"] = integer(l.segment_ms);
                    s["ricochet_count"] = integer(l.segment);
                    s["base_penetration_multiplier"] = number(l.multiplier);
                    s["checked_distance"] = number(l.checked_distance);
                    s["piercing_loss"] = number(l.piercing_loss);
                    edges.push_back(std::move(edge));
                }
            }
        }
        return edges;
    }
    // Releases an uncommitted admitted launch if the transaction unwinds.
    struct PendingGuard {
        proj::Lifecycle& lifecycle;
        stock_native_client_weapon::Gun& gun;
        bool complete = false;
        ~PendingGuard() {
            if (!complete && lifecycle.pending()) try { lifecycle.complete(false, gun); } catch (...) { PyErr_Clear(); }
        }
    };

public:
    Owner(Battle& b, std::int64_t sequence)
        : state_(b.vehicles), epoch_(state_->epoch), generation_(b.generation), client_(b.engine.client),
          scene_(stock_native_client_drive::sceneAccess(b)),
          world_(state_, scene_, stock_native_client_query::projectileTurrets(b.engine.shell.get())), flights_(world_), lifecycle_(flights_) {
        if (!scene_.registry || !scene_.scene || !scene_.pipeline) throw p::Error("Native player projectile scene is unavailable");
        if (!world_.install(b)) throw p::Error("Native player projectile world installation failed");
        lifecycle_.sequence(sequence);
        auto settings = field(b.engine.constants.get(), "VEHICLE_SETTING");
        current_setting_ = p::integral(settings.get(), "CURRENT_SHELLS");
        next_setting_ = p::integral(settings.get(), "NEXT_SHELLS");
        partial_setting_ = p::integral(settings.get(), "RELOAD_PARTIAL_CLIP");
    }
    bool current(const Battle& b) const override {
        return !retired_ && !state_->retired && state_->epoch == epoch_ && b.generation == generation_ && world_.current();
    }
    bool observe(Battle& b, const Value& message, double now) override {
        if (!current(b)) throw p::Error("Native player projectile owner is stale");
        const auto& round = message.get("round_id");
        if (round.resolved().kind != Value::Kind::Null && round.resolved() != state_->round.resolved()) return false;
        auto incoming = integerOf(message.get("authority_epoch"));
        if (!incoming) {
            auto fallback = field(client_.get(), "authority_epoch");
            if (fallback.get() != Py_None) incoming = p::wide(fallback.get());
        }
        const auto server = integerOf(message.get("server_time_ms"));
        const double delay = std::max(0., std::min(60., message.get("_client_dispatch_delay").numeric()));
        auto minimum = field(client_.get(), "minimum_rtt_ms"), fallback = field(client_.get(), "rtt_ms");
        auto* rtt = minimum.get() == Py_None ? fallback.get() : minimum.get();
        double one_way = 0.;
        if (rtt != Py_None) one_way = std::max(0., std::min(.25, p::scalar(rtt) / 2000.));
        const double anchor = std::max(0., now - delay - one_way);
        if (!anchorAt(b, incoming, server, anchor, now)) return false;
        const auto motion = integerOf(message.get("motion_time_us"));
        if (motion) {
            if (*motion < 0) return false;
            auto& s = *b.projectiles;
            if (!s.motion_time_us || *motion >= *s.motion_time_us) {
                s.motion_time_us = *motion;
                s.motion_local_time = anchor;
            }
        }
        return true;
    }
    // Adopts one authority epoch and server-time anchor; false when the owner rejects it.
    bool anchorAt(Battle& b, std::optional<std::int64_t> incoming, std::optional<std::int64_t> server, double anchor, double now) {
        const auto previous = flights_.epoch();
        if (!flights_.observe(incoming, server, anchor, now)) return false;
        if (previous && flights_.epoch() != previous) {
            lifecycle_.authorityChanged();
            visuals_.clear();
            visual_terminals_.clear();
            terminal_presentations_.clear();
        }
        world_.clock(&flights_, now);
        auto& s = *b.projectiles;
        if (incoming) s.epoch = *incoming;
        if (server && (!s.server_time_ms || *server >= *s.server_time_ms)) {
            s.server_time_ms = *server;
            s.server_local_time = anchor;
        }
        return true;
    }
    std::vector<Value> ingress(Battle& b, const Value& message, double now) override {
        std::vector<Value> output;
        if (!observe(b, message, now) || !world_.ingress(b, now)) return output;
        const auto& rows = message.get("projectiles").resolved();
        if (rows.kind != Value::Kind::Array) return output;
        std::vector<shot::Launch> accepted;
        std::set<std::string> reported;
        for (const auto& row : rows.array) {
            try {
                const auto& id = row.get("projectile_id").resolved();
                if (id.kind == Value::Kind::Null) continue;
                const auto key = id.kind == Value::Kind::Text ? id.text : std::to_string(std::int64_t(id.numeric()));
                reported.insert(key);
                if (visual_terminals_.count(key)) continue;
                p::Ref raw(cs::write(row));
                shot::Launch parsed;
                if (row.get("shooter_kind").resolved().text == "player") {
                    proj::Frozen frozen;
                    if (!parseFrozen(raw.get(), frozen)) { PyErr_Clear(); continue; }
                    if (frozen.round == 0) frozen.round = std::int64_t(state_->round.numeric());
                    parsed = frozen.flight;
                    if (frozen.player == state_->local_player_id && lifecycle_.canonical(frozen, now, false)) accepted.push_back(parsed);
                } else if (!shot::readLaunch(raw.get(), parsed)) { PyErr_Clear(); continue; }
                const auto visual = visuals_.find(key);
                if (visual == visuals_.end() || visual->second < parsed.segment) {
                    visuals_[key] = parsed.segment;
                    Value mirror = row.resolved();
                    mirror.object["maxDistance"] = number(parsed.maximum_distance);
                    output.push_back(std::move(mirror));
                }
            } catch (const std::exception&) { PyErr_Clear(); }
        }
        flights_.reconcile(accepted, reported, now);
        return output;
    }
    bool event(Battle& b, const Value& raw, double now) override {
        if (!current(b)) throw p::Error("Native player projectile event is stale");
        const auto& round = raw.get("round_id").resolved();
        if (round.kind != Value::Kind::Null && round != state_->round.resolved()) return false;
        const auto kind = raw.get("kind").resolved().text;
        if (kind == "projectile_impact") {
            const auto& id_value = raw.get("projectile_id").resolved();
            const auto id = id_value.kind == Value::Kind::Text ? id_value.text : std::to_string(std::int64_t(id_value.numeric()));
            const bool accepted = flights_.terminalEvent(id, integerOf(raw.get("authority_epoch")));
            if (accepted) visual_terminals_.insert(id);
            return accepted;
        }
        if (!observe(b, raw, now)) return false;
        if (kind == "authority") return true;
        if (!world_.ingress(b, now)) throw p::Error("Native player projectile world ingress failed");
        p::Ref packed(cs::write(raw));
        proj::Frozen frozen;
        if (!parseFrozen(packed.get(), frozen)) { PyErr_Clear(); return false; }
        if (frozen.round == 0) frozen.round = std::int64_t(state_->round.numeric());
        if (frozen.player != state_->local_player_id) return false;
        return lifecycle_.canonical(frozen, now);
    }
    Fired trigger(Battle& b, double now, bool continuing) override {
        if (!current(b)) throw p::Error("Native player projectile trigger is stale");
        auto reject = [](const char* reason) { Fired out; out.rejection = reason; return out; };
        if (b.phase != Phase::Running || !b.live || b.replay || b.worker || needs::drownLevel(b) == 2 || needs::overturnLevel(b) == 2 ||
            b.result.resolved().kind != Value::Kind::Null)
            return reject("battle_not_live");
        if (lifecycle_.burst() && !continuing) return reject("burst_pending");
        if (!b.engine.server) return reject("server_unavailable");
        auto& a = local();
        if (!a.alive) return reject("player_dead");
        if (a.siege_state == 1 || a.siege_state == 3) return reject("siege_switching");
        stock_native_client_drive::synchronizeStatus(b, now);
        auto status = stock_native_client_drive::status(a);
        if (!status) throw p::Error("Native player projectile critical status is unavailable");
        const auto& crew = status->critical().crew_roster;
        if (!crew.empty() && std::all_of(crew.begin(), crew.end(), [&](const std::string& name) { return status->casualties().count(name) != 0; }))
            return reject("player_dead");
        const auto gun_status = status->critical().devices.find("gunHealth");
        if (gun_status != status->critical().devices.end() && gun_status->second.destroyed) return reject("gun_destroyed");
        auto* gun = weapon::advanceWeapon(b, now);
        if (!gun) throw p::Error("Native projectile gun is unavailable");
        const bool tail = continuing && lifecycle_.burst() && gun->burst_remaining > 0 && gun->reload_time <= 0. && gun->clip > 0 &&
                          gun->shot == lifecycle_.burst()->shell && gun->ammunition[gun->shot] > 0;
        if (!tail && !gun->ready(true)) return reject("gun_not_ready");
        if (lifecycle_.pending()) return reject("intent_pending");
        if (!p::flag(field(client_.get(), "ready").get())) return reject("sender_unavailable");
        if (p::string(field(client_.get(), "phase").get()) != "battle") return reject("sender_unavailable");
        proj::Trigger frozen;
        if (!freeze(b, now, frozen)) return reject("shot_ray_unavailable");
        if (gun->shot < 0 || std::size_t(gun->shot) >= shots_.size()) return reject("mounted_shot_unavailable");
        frozen.shell = gun->shot;
        frozen.mounted = shots_[gun->shot];
        frozen.stickers = stickers_[gun->shot];
        if (!stock_native_client_input::sendCurrentAt(b, now)) return reject("input_send_failed");
        frozen.input_sequence = p::integral(client_.get(), "_input_seq");
        if (!world_.ingress(b, now)) throw p::Error("Native player projectile world ingress failed");
        const auto* pending = lifecycle_.begin(frozen, *gun, world_.random(), [](Point value) {
            BW::Vector3 vector{float(value[0]), float(value[1]), float(value[2])};
            vector.normalise();
            return Point{vector.x, vector.y, vector.z};
        });
        if (!pending) return reject("projectile_admission_failed");
        PendingGuard guard{lifecycle_, *gun};
        const auto accepted = *pending;
        auto* burst = burstMirror(b, accepted);
        FireIntent intent;
        intent.player_id = accepted.player;
        intent.shot_seq = accepted.flight.sequence;
        intent.input_seq = accepted.input_sequence;
        intent.reload_factor = accepted.reload_factor;
        intent.burst = burst != nullptr;
        b.gun->intent = intent;
        world_.frozen(accepted);
        visuals_[accepted.flight.id] = 0;
        auto message = launchWire(accepted, flights_.epoch().value());
        message.object.erase("projectile_id");
        message.object.erase("source_vehicle");
        message.object["type"] = text("projectile_launch");
        message.object["shells_before_shot"] = integer(accepted.shells_before);
        bool sent = false;
        try {
            p::Ref packed(cs::write(message));
            sent = stock_native_client_input::enqueue(client_.get(), packed.get());
        } catch (const std::exception&) { PyErr_Clear(); }
        // change_vehicle_setting may have deferred shell settings into the intent while it was being sent.
        const auto settled = b.gun->intent ? b.gun->intent->deferred : DeferredSettings{};
        if (settled.present) gun->pending = settled.pending_index;
        const bool committed = lifecycle_.complete(sent, *gun);
        guard.complete = true;
        b.gun->intent.reset();
        if (!committed) {
            if (burst && accepted.flight.burst_index == 0) b.gun->burst.reset();
            return reject("launch_send_failed");
        }
        if (burst) {
            burst->committed = int(accepted.flight.burst_index + 1);
            if (accepted.flight.burst_index + 1 == accepted.flight.burst_count) {
                const auto finished = burst->deferred;
                b.gun->burst.reset();
                applyDeferred(finished, *gun, current_setting_, next_setting_, partial_setting_);
            }
        }
        applyDeferred(settled, *gun, current_setting_, next_setting_, partial_setting_);
        gun->rescale(accepted.reload_factor);
        b.gun->projectile_sequence = accepted.flight.sequence;
        a.presented.shot_penalty_until = now + shot_camouflage_;
        Fired out;
        out.presentation = presentation(accepted, flights_.epoch().value());
        return out;
    }
    bool burstDue(double now) const override { return lifecycle_.burstDue(now); }
    bool cancel(Battle& b, double now, bool apply_settings) override {
        if (!current(b) || !b.gun->gun) return false;
        auto* gun = stock_native_client_weapon::gun(b.gun->gun.get());
        if (!gun) return false;
        const auto burst = b.gun->burst;
        const bool changed = lifecycle_.cancelBurst(*gun, 1.);
        b.gun->burst.reset();
        if (changed && apply_settings) {
            weapon::advanceWeapon(b, now);
            if (burst && burst->deferred.present) {
                gun->pending = burst->deferred.pending_index;
                applyDeferred(burst->deferred, *gun, current_setting_, next_setting_, partial_setting_);
            }
            gun->rescale(weapon::reloadFactor(b));
        }
        return changed;
    }
    Advanced advance(Battle& b, double now, bool force) override {
        if (!current(b)) throw p::Error("Native player projectile frame is stale");
        world_.clock(&flights_, now);
        const auto started = std::chrono::steady_clock::now();
        flights_.advance(now, force);
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const auto& m = flights_.lastAdvanceMetrics();
        auto& s = *b.projectiles;
        s.active = std::size_t(m.cursor.active);
        s.chords = std::size_t(m.cursor.chords);
        s.debt = m.cursor.debt_after;
        s.advance_seconds = elapsed;
        s.terminals = std::size_t(m.cursor.terminals);
        s.scans = std::size_t(m.chord.scans);
        s.candidates = std::size_t(m.chord.candidates);
        Advanced out;
        out.edges = dispatch(b, now);
        lifecycle_.prune();
        out.burst_due = lifecycle_.burstDue(now);
        out.active = !flights_.flights().empty();
        return out;
    }
    bool frameSample(Battle& b, double now) override {
        if (!current(b)) return false;
        world_.clock(&flights_, now);
        return flights_.sample(now);
    }
    void visualResult(const std::string& id, std::int64_t segment, bool accepted) override {
        if (accepted) { visuals_[id] = segment; return; }
        const auto at = visuals_.find(id);
        if (at != visuals_.end() && at->second <= segment) visuals_.erase(at);
    }
    bool resources(Battle& b, double now) override { return current(b) && world_.ingress(b, now); }
    bool discard(Battle& b, const std::string& id) override {
        if (!current(b) || !lifecycle_.launches().count(id)) return false;
        const bool changed = flights_.localFailure(id);
        if (changed) {
            visual_terminals_.insert(id);
            visuals_.erase(id);
            lifecycle_.prune();
        }
        return changed;
    }
    void close() override { retired_ = true; }
};
}

std::unique_ptr<PlayerProjectiles> createPlayerProjectiles(Battle& b, double now) {
    auto owner = std::make_unique<Owner>(b, b.gun->projectile_sequence);
    // The opening authority anchor precedes the first running frame; adopt the one already observed.
    const auto& s = *b.projectiles;
    if (!owner->anchorAt(b, s.epoch, s.server_time_ms, s.server_local_time, now) || !owner->resources(b, now))
        throw p::Error("Native player projectile ingress is unavailable");
    return owner;
}
}
