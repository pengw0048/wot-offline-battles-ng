// Visible-client visibility: local spotting, team radio memory, Sixth Sense, map space visibility and spotting inputs.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_visibility_core.hpp"
#include "stock_native_battle_vehicles.hpp"
#include "stock_native_client_drive_status.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_world_pipeline.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace native_battle {
namespace env = stock_native_client_environment;
namespace query = stock_native_client_query;
namespace world_query = stock_native_world;

Visibility::Visibility() = default;
Visibility::~Visibility() = default;

namespace visibility {
namespace {
constexpr double space_check_seconds = .5;
constexpr double observation_seconds = 10.;
constexpr double max_spot_memory_seconds = 14.;
constexpr int sixth_detail_limit = 16;

// getattr(o, name, None) through the attribute ABI old-style #1513 classes use; dicts read their keys.
p::Ref field(PyObject* o, const char* name) {
    if (!o || o == Py_None) return p::retain(Py_None);
    if (PyDict_Check(o)) return p::value(o, name);
    auto key = p::text(name);
    PyObject* v = p::directAttribute(o, key.get());
    if (!v && PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return p::retain(Py_None); }
    return p::Ref(v);
}
double number(PyObject* o, const char* name, double fallback) {
    auto v = field(o, name);
    if (v.get() == Py_None) return fallback;
    const double out = PyFloat_AsDouble(v.get());
    if (PyErr_Occurred()) { PyErr_Clear(); return fallback; }
    return std::isfinite(out) ? out : fallback;
}
Point point(PyObject* v) {
    if (PyList_Check(v) || PyTuple_Check(v)) return p::numbers<3>(v);
    return {p::numeric(v, "x"), p::numeric(v, "y"), p::numeric(v, "z")};
}
std::string key(const std::string& kind, std::int64_t id) { return kind + ":" + std::to_string(id); }

env::ActorView view(const VisibilityCore::Installed& item) {
    const auto& a = *item.actor;
    env::ActorView out;
    out.key = a.key; out.engine_id = a.engine_id; out.id = a.id; out.local = a.local;
    out.team = int(a.state.get("team").numeric());
    out.alive = a.alive; out.ready = a.ready; out.missing = a.missing; out.tombstone = a.tombstone;
    // Remote replica poses are presented as float matrices; the local pose is the double drive state.
    out.position = a.local ? Point{a.current[0], a.current[1], a.current[2]}
                           : Point{float(a.current[0]), float(a.current[1]), float(a.current[2])};
    out.speed = a.speed;
    out.shot_until = a.presented.shot_penalty_until;
    out.dead_marker_known = a.presented.dead_marker_known;
    out.model = a.presented.spot_visible;
    out.marker = a.presented.spot_marker_visible;
    out.direct_until = a.presented.spot_until;
    out.radio_until = a.presented.radio_spot_until;
    out.next_probe = a.presented.spot_next;
    return out;
}
native_visibility::Pose pose(const Actor& a) {
    native_visibility::Pose out;
    out.position = a.local ? Point{a.current[0], a.current[1], a.current[2]}
                           : Point{float(a.current[0]), float(a.current[1]), float(a.current[2])};
    out.yaw = a.current[3]; out.pitch = a.current[4]; out.roll = a.current[5];
    out.relative_turret_yaw = a.current[6] - a.current[3];
    return out;
}
double distance(const Point& a, const Point& b) { return std::hypot(a[0] - b[0], a[2] - b[2]); }
// The #1513 view range: the 50 m floor, the clamped vision damage factor and the still-gated binoculars.
double range(const env::Profile& p, double factor, double stationary) {
    double v = std::max(50., p.base_view) * std::max(0., p.misc_vision * std::max(.5, std::min(1., factor))) * std::max(0., p.crew_vision);
    if (p.binoculars && stationary >= std::max(0., p.binocular_delay)) v *= std::max(1., p.binocular_factor);
    return std::max(50., v);
}
// BW::Vector3 length in float, as the stock sight test measures it.
double length(const Point& a, const Point& b) {
    const float x = float(a[0]) - float(b[0]), y = float(a[1]) - float(b[1]), z = float(a[2]) - float(b[2]);
    return double(std::sqrt(x * x + y * y + z * z));
}

bool sight(Battle& b, VisibilityCore& c, const env::ActorView& observer, const env::ActorView& target, const env::Profile& source,
           const env::Profile& profile, double local_still, double target_still, bool fired, double now) {
    const double separation = distance(observer.position, target.position);
    if (separation <= 50.) return true;
    if (separation > 445.) return false;
    const auto* observer_actor = b.vehicle(observer.key);
    const auto* target_actor = b.vehicle(target.key);
    if (!observer_actor || !target_actor) return false;
    native_visibility::PairInput pair;
    pair.observer = pose(*observer_actor);
    pair.target = pose(*target_actor);
    pair.observer_checkpoints = source.checkpoints;
    pair.target_checkpoints = profile.checkpoints;
    const auto server = projectiles::serverTimeMs(b, now);
    pair.observer_phase = server ? std::uint64_t(std::max(0., std::trunc(*server)) / 2000.) : 0;
    auto& d = pair.detection;
    d.distance = separation;
    d.view_range = range(source, stock_native_client_drive::statusFactor(*observer_actor, wot::offline_bot_step::CriticalStat::vision), local_still);
    d.base_camouflage = profile.camouflage;
    d.moving = std::abs(target.speed) > .5;
    d.fired_recently = fired;
    const auto& aspect = d.moving || (profile.net && target_still < std::max(0., profile.net_delay)) ? profile.moving_aspect : profile.still_aspect;
    d.additive = aspect[0];
    d.multiplier = aspect[1];
    d.shot_factor = profile.shot_factor;
    static const native_visibility::FoliageSnapshot no_foliage;
    const auto* foliage = world::foliageSnapshot(b);
    auto prepared = native_visibility::prepare_visibility(pair, foliage ? *foliage : no_foliage);
    auto scene = stock_native_client_drive::sceneAccess(b);
    if (!scene.pipeline || !scene.registry) throw p::Error("Native local sight scene is unavailable");
    std::set<world_query::Key> broken;
    for (const auto& id : scene.registry->skin.destroyed) broken.insert({std::get<0>(id), std::get<1>(id)});
    for (const auto& id : scene.registry->skin.speculative) broken.insert({std::get<0>(id), std::get<1>(id)});
    std::vector<std::uint8_t> clear;
    for (std::size_t i = 0; i < prepared.rays.size(); ++i) {
        const auto& r = prepared.rays[i];
        world_query::Hit hit;
        const auto result = scene.pipeline->horizontal(r.start, r.end, hit, &broken);
        if (result == world_query::QueryStatus::Failed) throw p::Error("Native local sight query failed");
        const bool open = result == world_query::QueryStatus::Miss || length(hit.point, r.start) + 1.5 >= length(r.end, r.start);
        clear.push_back(open);
        if (native_visibility::should_stop(prepared, i, open)) break;
    }
    return native_visibility::reduce_visibility(prepared, clear).detected;
}

env::Profile profile(Battle& b, const Actor& a, PyObject* d) {
    env::Profile out;
    const auto checkpoints = stock_native_bot_descriptor::checkpoints(d);
    if (checkpoints) { out.checkpoints.available = true; out.checkpoints.points = *checkpoints; }
    auto chassis = field(d, "chassis"), hull = field(d, "hull"), turret = field(d, "turret"), gun = field(d, "gun");
    auto hull_position = field(chassis.get(), "hullPosition"), positions = field(hull.get(), "turretPositions");
    if (hull_position.get() != Py_None && positions.get() != Py_None && p::size(positions.get()) > 0) {
        auto first = p::item(positions.get(), 0);
        const auto h = point(hull_position.get()), t = point(first.get());
        for (unsigned j = 0; j < 3; ++j) out.checkpoints.turret_mount[j] = h[j] + t[j];
        out.checkpoints.turret_mount_available = true;
    }
    auto static_yaw = field(gun.get(), "staticTurretYaw");
    out.checkpoints.static_turret_yaw_present = static_yaw.get() != Py_None;
    out.checkpoints.static_turret_yaw = out.checkpoints.static_turret_yaw_present ? p::scalar(static_yaw.get()) : 0.;
    // A remote human's spotting inputs travel with its accepted effective parameters.
    const bool remote_player = a.kind == "player" && !a.local;
    const auto& effective = a.state.get("effective_params").resolved();
    p::Ref raw(p::retain(Py_None));
    if (remote_player) {
        const auto& spot = effective.get("spotting").resolved();
        if (spot.kind != Value::Kind::Object) throw p::Error("Remote player effective spotting profile is unavailable");
        raw.~Ref(); new (&raw) p::Ref(cs::write(spot));
    } else {
        auto computed = spottingProfile(b, d, a.local);
        raw.~Ref(); new (&raw) p::Ref(std::move(computed));
    }
    out.crew_vision = p::numeric(raw.get(), "vision_factor", 1.);
    out.binoculars = p::truth(raw.get(), "has_binoculars");
    out.binocular_factor = p::numeric(raw.get(), "binocular_factor", 1.);
    out.binocular_delay = p::numeric(raw.get(), "binocular_delay", 3.);
    out.net = p::truth(raw.get(), "has_camouflage_net");
    out.net_delay = p::numeric(raw.get(), "camouflage_net_delay", 3.);
    out.moving_aspect = p::numbers<2>(field(raw.get(), "invisibility_moving").get());
    out.still_aspect = p::numbers<2>(field(raw.get(), "invisibility_still").get());
    if (remote_player) {
        const auto& camouflage = effective.get("camouflage");
        out.camouflage = {camouflage.get("base_moving").numeric(), camouflage.get("base_still").numeric()};
        out.shot_factor = camouflage.get("shot_factor").numeric(1.);
    } else {
        out.camouflage = baseInvisibility(d, raw.get(), Py_None);
        out.shot_factor = shotInvisibilityFactor(d);
    }
    out.base_view = number(turret.get(), "circularVisionRadius", 400.);
    auto misc = field(d, "miscAttrs");
    out.misc_vision = number(misc.get(), "circularVisionRadiusFactor", 1.);
    auto devices = field(d, "optionalDevices");
    if (devices.get() != Py_None)
        for (Py_ssize_t j = 0; j < p::size(devices.get()); ++j) {
            auto device = p::item(devices.get(), j);
            auto id = field(device.get(), "id");
            const double delay = number(device.get(), "activateWhenStillSec", 0.);
            if (delay <= 0. || !PyTuple_Check(id.get()) || p::size(id.get()) != 2) continue;
            out.optional_devices.emplace_back(p::wide(p::item(id.get(), 1).get()), delay);
        }
    return out;
}

// Applies spotting output: presentation edges through the vehicle owner, then the local vision HUD.
bool apply(Battle& b, VisibilityCore& c, const env::Output& output) {
    bool changed = false;
    for (const auto& t : output.transitions) {
        auto* a = b.vehicle(t.key);
        if (!a || a->engine_id != t.engine_id) continue;
        try {
            vehicles::setSpotVisibility(b, *a, t.model, t.marker);
            if (!current(b, &c)) return changed;
            c.spotting.acknowledge(t);
            a->visible = t.model;
            changed = true;
        } catch (const std::exception& e) {
            b.warn("native visibility " + t.key, describe(e), false);
        }
    }
    auto& v = *b.visibility;
    if (v.vision_feed_failed || !current(b, &c)) return changed;
    try {
        auto avatar = b.engine.avatar.get();
        if (output.vision_radius) {
            auto attrs = p::dict();
            p::set(attrs.get(), "circularVisionRadius", p::number(*output.vision_radius));
            py::call(avatar, "syncVehicleAttrs", attrs.get());
        }
        for (const auto& device : output.devices) {
            if (!current(b, &c)) break;
            auto vehicle_id = py::attr(avatar, "playerVehicleID");
            auto id = p::integer(device.id);
            py::call(avatar, "updateVehicleOptionalDeviceStatus", vehicle_id.get(), id.get(), device.active ? Py_True : Py_False);
        }
    } catch (const std::exception& e) {
        v.vision_feed_failed = true;
        b.warn("battle HUD vision feed", describe(e));
    }
    return changed;
}

// One spotting pass; radio_only re-evaluates enemy memory after a team observation.
bool runSpotting(Battle& b, VisibilityCore& c, double now, bool hud_only, bool radio_only) {
    stock_native_client_drive::synchronizeStatus(b, now);
    if (!current(b, &c)) return false;
    env::Frame input;
    input.now = now;
    input.hud_only = hud_only;
    input.radio_only = radio_only;
    input.local_team = int(localTeam(b));
    input.local_key = key("player", b.local_player);
    std::vector<env::ActorView> actors;
    for (const auto& row : c.installed) {
        const auto found = b.vehicles->actors.find(row.first);
        if (found == b.vehicles->actors.end() || found->second != row.second.actor) continue;
        actors.push_back(view(row.second));
        const auto& a = *found->second;
        if (a.local) {
            input.presentation_origin = {a.current[0], a.current[1], a.current[2]};
            input.local_vision_factor = stock_native_client_drive::statusFactor(a, wot::offline_bot_step::CriticalStat::vision);
        }
    }
    if (b.visibility->spectated_engine_id)
        for (const auto& a : actors)
            if (a.engine_id == *b.visibility->spectated_engine_id) input.presentation_origin = a.position;
    if (c.spg && b.engine.avatar) {
        auto handler = field(b.engine.avatar.get(), "inputHandler");
        auto mode = field(handler.get(), "_AvatarInputHandler__ctrlModeName");
        const auto name = p::string(mode.get());
        input.spg_aiming = name == c.strategic || name == c.arty;
    }
    const auto output = c.spotting.frame(input, actors, c.profiles,
        [&](const env::ActorView& observer, const env::ActorView& target, const env::Profile& source, const env::Profile& profile,
            double local_still, double target_still, bool fired) {
            try { return sight(b, c, observer, target, source, profile, local_still, target_still, fired, now); }
            catch (const std::exception& e) {
                const auto text = describe(e);
                auto& last = c.sight_failures[target.key];
                if (last != text) { last = text; log("native spotting " + target.key + ": " + text); }
                return false;
            }
        });
    return apply(b, c, output);
}

// The team radio memory the worker relayed: the local client's own lease per enemy, and its radio-linked allies.
bool radio(Battle& b, VisibilityCore& c, const Value& message, double now) {
    if (message.get("type").resolved().text != "bot_observation") return false;
    if (message.get("round_id") != b.vehicles->round) return false;
    const std::int64_t local_id = b.local_player;
    const std::int64_t team = localTeam(b);
    std::map<std::string, double> deadlines;
    std::set<std::string> allies;
    for (const auto& link : message.get("radio_links").resolved().array) {
        if (link.get("kind").resolved().text != "human" || std::int64_t(link.get("id").numeric()) != local_id) continue;
        for (const auto& peer : link.get("allies").resolved().array)
            allies.insert(key(peer.get("kind").resolved().text == "human" ? "player" : "bot", std::int64_t(peer.get("id").numeric())));
    }
    const auto* local = b.localVehicle();
    const bool spectating = local && !local->alive;
    for (const auto& contact : message.get("contacts").resolved().array) {
        if (std::int64_t(contact.get("observing_team").numeric()) != team) continue;
        const auto& kind = contact.get("target_kind").resolved().text;
        if (kind != "human" && kind != "bot") continue;
        double lease = contact.get("time_left").numeric(-1.);
        if (lease < 0. || lease > max_spot_memory_seconds || contact.get("visible").truth() != (lease > 0.))
            throw p::Error("Native radio memory lease is invalid");
        if (!spectating) {
            const auto& recipients = contact.get("radio_recipients").resolved();
            if (recipients.kind != Value::Kind::Null) {
                lease = 0.;
                for (const auto& row : recipients.array)
                    if (row.get("kind").resolved().text == "human" && std::int64_t(row.get("id").numeric()) == local_id)
                        lease = std::max(lease, row.get("time_left").numeric());
            } else {
                bool seen = false;
                for (const auto& id : contact.get("visible_by_player_ids").resolved().array)
                    if (std::int64_t(id.numeric()) == local_id) { seen = true; break; }
                if (!seen) lease = 0.;
            }
        }
        deadlines[key(kind == "human" ? "player" : "bot", std::int64_t(contact.get("target_id").numeric()))] = now + lease;
    }
    c.spotting.radio(deadlines, std::move(allies));
    return runSpotting(b, c, now, false, true);
}

bool hasSixthSense(Battle& b) { return b.visibility->local.sixth_sense && gun::livingSkillCount(b, "commander_sixthsense") > 0; }

void deliverSixthSense(Battle& b) {
    auto* c = core(b);
    if (!c) return;
    c->sixth_callback = Object();
    const auto* local = b.localVehicle();
    if (!local || !local->alive) { ++c->sixth_counts["suppressed_dead"]; return; }
    if (!b.running() || !b.live) { ++c->sixth_counts["suppressed_not_battle"]; return; }
    if (!hasSixthSense(b)) { ++c->sixth_counts["suppressed_skill"]; return; }
    try {
        auto provider = py::attr(b.engine.avatar.get(), "guiSessionProvider");
        auto shared = py::attr(provider.get(), "shared");
        auto state = py::attr(shared.get(), "vehicleState");
        auto view = py::attr(b.engine.modules.get(), "vehicle_view_state");
        auto observed = py::attr(view.get(), "OBSERVED_BY_ENEMY");
        py::call(state.get(), "notifyStateChanged", observed.get(), Py_True);
    } catch (const std::exception& e) {
        ++c->sixth_counts["presentation_failed"];
        b.warn("Sixth Sense presentation", describe(e), false);
        return;
    }
    if (++c->sixth_counts["presented"] <= sixth_detail_limit + 1) log("SIXTH result=presented");
    else ++c->sixth_counts["detail_dropped"];
}

// The local vehicle was freshly observed by an enemy team: one continuous episode schedules one delayed indicator.
bool observeLocal(Battle& b, VisibilityCore& c, const Value& message, double now) {
    bool visible = false;
    const std::int64_t team = localTeam(b);
    for (const auto& contact : message.get("contacts").resolved().array) {
        if (contact.resolved().kind != Value::Kind::Object) continue;
        if (contact.get("target_kind").resolved().text == "human" && std::int64_t(contact.get("target_id").numeric(-1.)) == b.local_player &&
            std::int64_t(contact.get("observing_team").numeric()) != team && contact.get("fresh").truth()) {
            visible = true;
            break;
        }
    }
    if (!visible) return false;
    const bool observed = now < c.observed_until;
    c.observed_until = now + observation_seconds;
    if (observed || !hasSixthSense(b)) return true;
    c.sixth_callback = own(schedule(b, b.visibility->local.sixth_sense_delay, [](Battle& battle) { deliverSixthSense(battle); }));
    ++c.sixth_counts["scheduled"];
    return true;
}

// The selected gameplay bit and the exact client/server masks.
VisibilityCore::Space contract(Battle& b) {
    auto flags = field(b.engine.modules.get(), "client_visibility_flags");
    auto mask = field(b.engine.modules.get(), "arena_visibility_mask");
    if (flags.get() == Py_None || !PyCallable_Check(mask.get())) throw p::Error("#1513 space visibility boundary is unavailable");
    VisibilityCore::Space out;
    try {
        auto client = py::attr(flags.get(), "CLIENT_MASK"), server = py::attr(flags.get(), "SERVER_MASK");
        out.client = PyLong_AsUnsignedLongLongMask(client.get());
        out.server = PyLong_AsUnsignedLongLongMask(server.get());
        auto gameplay = py::attr(b.visibility->local.arena_type.get(), "gameplayID");
        auto selected = py::invoke(mask.get(), gameplay.get());
        out.selected = PyLong_AsUnsignedLongLongMask(selected.get());
        if (PyErr_Occurred()) throw p::Error("mask");
    } catch (const std::exception&) {
        PyErr_Clear();
        throw p::Error("#1513 space visibility contract is invalid");
    }
    if ((out.client & out.server) || ((out.client | out.server) & 0xffffffffULL) != 0xffffffffULL || !out.selected ||
        (out.selected & (out.selected - 1)) || (out.selected & ~out.server))
        throw p::Error("#1513 gameplay visibility mask is invalid");
    return out;
}
struct SpacePending {};
// BigWorld.spaces[space_id], or SpacePending while native publication lags the geometry mapping.
p::Ref liveSpace(Battle& b, std::int64_t space_id) {
    auto spaces = field(b.engine.bigworld.get(), "spaces");
    if (spaces.get() == Py_None) throw p::Error("#1513 live space visibility data is unavailable");
    auto id = p::integer(space_id);
    PyObject* space = PyObject_GetItem(spaces.get(), id.get());
    if (space) return p::Ref(space);
    if (PyErr_ExceptionMatches(PyExc_KeyError)) { PyErr_Clear(); throw SpacePending(); }
    throw p::Error("#1513 live space lookup failed");
}
std::uint64_t itemsMask(PyObject* space) {
    auto mask = py::attr(space, "itemsVisibilityMask");
    const auto value = PyLong_AsUnsignedLongLongMask(mask.get());
    if (PyErr_Occurred()) throw p::Error("#1513 space visibility mask is unreadable");
    return value;
}
void setItemsMask(PyObject* space, std::uint64_t value) {
    p::Ref mask(PyLong_FromUnsignedLongLong(value));
    py::set(space, "itemsVisibilityMask", mask.get());
}
std::string maskMismatch(std::uint64_t expected, std::uint64_t actual) {
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    do { hex.insert(hex.begin(), digits[expected & 15]); expected >>= 4; } while (expected);
    return "#1513 typed gameplay visibility mask was not applied: expected=0x" + hex + " actual=" + std::to_string(actual);
}
void warnSpace(Battle& b, VisibilityCore& c, const std::string& error) {
    c.space.reset();
    if (c.space_warning) return;
    c.space_warning = true;
    b.warn("map visibility filtering", error);
}
}

bool current(const Battle& b, const VisibilityCore* c) {
    return native_battle::current() == &b && b.visibility && b.visibility->core.get() == c && !b.vehicles->retired;
}
VisibilityCore* core(Battle& b) { return b.visibility ? b.visibility->core.get() : nullptr; }
std::int64_t localTeam(const Battle& b) {
    if (const auto* local = b.localVehicle()) {
        const auto& team = local->state.get("team");
        if (team.resolved().kind != Value::Kind::Null) return std::int64_t(team.numeric());
    }
    return p::integral(b.engine.client.get(), "team");
}

void configureLocal(Battle& b, LocalVisibilityInputs inputs) {
    if (!b.visibility) b.visibility = std::make_unique<Visibility>();
    b.visibility->local = std::move(inputs);
}

void bind(Battle& b) {
    if (b.worker) return;
    if (!b.visibility) b.visibility = std::make_unique<Visibility>();
    auto& v = *b.visibility;
    if (!v.core) {
        v.core = std::make_unique<VisibilityCore>();
        auto& c = *v.core;
        c.epoch = b.vehicles->epoch;
        c.query = own(p::Ref(query::createFor(b)));
        if (const auto* local = b.localVehicle()) {
            auto entity = b.entity(local->engine_id);
            auto descriptor = field(entity.get(), "typeDescriptor"), type = field(descriptor.get(), "type"), tags = field(type.get(), "tags");
            auto spg = p::text("SPG");
            c.spg = tags.get() != Py_None && PySequence_Contains(tags.get(), spg.get()) == 1;
        }
        auto handler = field(b.engine.modules.get(), "avatar_input_handler"), modes = field(handler.get(), "_CTRL_MODE");
        c.strategic = p::string(field(modes.get(), "STRATEGIC").get());
        c.arty = p::string(field(modes.get(), "ARTY").get());
    }
    ingest(b);
}

void ingest(Battle& b) {
    auto* c = core(b);
    if (!c || b.vehicles->retired) return;
    for (const auto& failure : query::ingestFor(c->query.get(), b)) b.warn("native query " + failure.entity, failure.error, false);
    if (!current(b, c)) return;
    if (c->epoch != b.vehicles->epoch) {
        c->installed.clear();
        c->profiles.clear();
        c->spotting = env::Spotting();
        c->epoch = b.vehicles->epoch;
    }
    std::set<std::string> present;
    const auto accepted = b.vehicles->actors;
    for (const auto& row : accepted) {
        if (!current(b, c)) break;
        const auto& a = row.second;
        present.insert(row.first);
        if (!a || a->missing || a->tombstone || !a->ready) continue;
        try {
            auto* d = query::descriptor(*a);
            if (!d) continue;
            const auto& effective = a->state.get("effective_params");
            auto found = c->installed.find(row.first);
            if (found != c->installed.end() && found->second.actor == a && found->second.descriptor.get() == d &&
                found->second.revision == a->descriptor_revision && found->second.effective == effective)
                continue;
            VisibilityCore::Installed item{a, borrow(d), a->descriptor_revision, effective};
            auto built = profile(b, *a, d);
            if (!current(b, c)) break;
            c->spotting.seed(view(item));
            c->profiles[row.first] = std::move(built);
            c->installed[row.first] = std::move(item);
        } catch (const std::exception& e) {
            c->installed.erase(row.first);
            c->profiles.erase(row.first);
            b.warn("native environment " + row.first, describe(e), false);
        }
    }
    if (!current(b, c)) return;
    for (auto i = c->installed.begin(); i != c->installed.end();)
        if (!present.count(i->first)) { c->profiles.erase(i->first); i = c->installed.erase(i); }
        else ++i;
    c->spotting.forget(present);
}

void retire(Battle& b) {
    auto* c = core(b);
    if (!c) return;
    try { clearOutline(b); } catch (const std::exception&) { PyErr_Clear(); }
    if ((c = core(b)) == nullptr) return;
    if (c->sixth_callback && c->sixth_callback.get() != Py_None) {
        ++c->sixth_counts["suppressed_reset"];
        cancel(b, c->sixth_callback);
    }
    if (c->sixth_counts["scheduled"]) {
        auto& n = c->sixth_counts;
        log("SIXTH summary scheduled=" + std::to_string(n["scheduled"]) + " presented=" + std::to_string(n["presented"]) +
            " presentation_failed=" + std::to_string(n["presentation_failed"]) + " suppressed_dead=" + std::to_string(n["suppressed_dead"]) +
            " suppressed_not_battle=" + std::to_string(n["suppressed_not_battle"]) + " suppressed_skill=" + std::to_string(n["suppressed_skill"]) +
            " suppressed_reset=" + std::to_string(n["suppressed_reset"]) + " detail_dropped=" + std::to_string(n["detail_dropped"]));
    }
    if (c->query) {
        p::Ref args(PyTuple_Pack(1, c->query.get()));
        PyObject* closed = query::close(nullptr, args.get());
        if (closed) Py_DECREF(closed);
        else PyErr_Clear();
    }
    b.visibility->core.reset();
    b.visibility->outlined_engine_id.reset();
}

void spotting(Battle& b, double now, bool hud_only) {
    auto* c = core(b);
    if (!c) return;
    runSpotting(b, *c, now, hud_only, false);
}

bool observation(Battle& b, const Value& message) {
    if (!b.running() || message.resolved().kind != Value::Kind::Object) return false;
    if (message.get("round_id") != b.start.get("round_id")) return false;
    auto* c = core(b);
    if (!c) return false;
    try {
        const double now = b.clock();
        bool team = false;
        try { team = radio(b, *c, message, now); }
        catch (const std::exception& e) { b.warn("native team visibility", describe(e), false); }
        if (!current(b, c)) return team;
        const bool enemy = observeLocal(b, *c, message, now);
        return team || enemy;
    } catch (const std::exception& e) {
        b.fail(describe(e));
        return false;
    }
}

void spectate(Battle& b, std::optional<std::int64_t> engine_id) {
    if (b.visibility) b.visibility->spectated_engine_id = engine_id;
}

std::uint32_t prepareMappingVisibility(Battle& b, std::int64_t space_id) {
    auto* c = core(b);
    if (!b.visibility) b.visibility = std::make_unique<Visibility>();
    auto space = contract(b);
    // Only standard CTF arenas exist offline; the native mapping helper narrows its all-bits mask to this wire bit.
    if (space.selected != 1) throw p::Error("#1513 standard gameplay visibility bit is not 0x1");
    space.id = space_id;
    if (!c) { b.visibility->core = std::make_unique<VisibilityCore>(); c = b.visibility->core.get(); c->epoch = b.vehicles->epoch; }
    c->space = space;
    c->next_space_check = b.clock() + space_check_seconds;
    return std::uint32_t(space.selected);
}

void configureSpaceVisibility(Battle& b, std::optional<std::int64_t> space_id) {
    if (!b.visibility) b.visibility = std::make_unique<Visibility>();
    if (!b.visibility->core) { b.visibility->core = std::make_unique<VisibilityCore>(); b.visibility->core->epoch = b.vehicles->epoch; }
    auto& c = *b.visibility->core;
    try {
        auto space = contract(b);
        space.id = space_id ? *space_id : p::integral(b.engine.avatar.get(), "spaceID");
        c.space = space;
        c.next_space_check = b.clock() + space_check_seconds;
        auto live = liveSpace(b, space.id);
        if (itemsMask(live.get()) != space.selected) setItemsMask(live.get(), space.selected);
        const auto actual = itemsMask(live.get());
        if (actual != space.selected) throw p::Error(maskMismatch(space.selected, actual));
    } catch (const SpacePending&) {
        // The mapping already holds the bit; maintenance finishes once BigWorld publishes the space.
    } catch (const std::exception& e) {
        warnSpace(b, c, describe(e));
    }
}

void maintainSpaceVisibility(Battle& b, double now) {
    auto* c = core(b);
    if (!c || !c->space || now < c->next_space_check) return;
    c->next_space_check = now + space_check_seconds;
    const auto space = *c->space;
    try {
        auto live = liveSpace(b, space.id);
        const auto current_mask = itemsMask(live.get());
        if ((current_mask & space.server) == space.selected) return;
        const auto corrected = (current_mask & space.client) | space.selected;
        setItemsMask(live.get(), corrected);
        const auto actual = itemsMask(live.get());
        if (actual != corrected) throw p::Error(maskMismatch(corrected, actual));
    } catch (const SpacePending&) {
    } catch (const std::exception& e) {
        warnSpace(b, *c, describe(e));
    }
}

void disableSpaceVisibility(Battle& b) {
    if (auto* c = core(b)) c->space.reset();
}

p::Ref spottingProfile(Battle& b, PyObject* descriptor, bool local) { return params::spottingProfile(b, descriptor, local); }

double visionRadius(Battle& b, PyObject* descriptor, const Actor* vehicle, double still_seconds, bool local) {
    auto spotting_profile = spottingProfile(b, descriptor, local);
    env::Profile view;
    auto turret = field(descriptor, "turret"), misc = field(descriptor, "miscAttrs");
    view.base_view = number(turret.get(), "circularVisionRadius", 400.);
    view.misc_vision = number(misc.get(), "circularVisionRadiusFactor", 1.);
    view.crew_vision = p::numeric(spotting_profile.get(), "vision_factor", 1.);
    view.binoculars = p::truth(spotting_profile.get(), "has_binoculars");
    view.binocular_factor = p::numeric(spotting_profile.get(), "binocular_factor", 1.);
    view.binocular_delay = p::numeric(spotting_profile.get(), "binocular_delay", 3.);
    const double factor = vehicle ? stock_native_client_drive::statusFactor(*vehicle, wot::offline_bot_step::CriticalStat::vision) : 1.;
    return range(view, factor, still_seconds);
}

std::array<double, 2> baseInvisibility(PyObject* descriptor, PyObject* spotting_profile, PyObject* camouflage_id) {
    auto crew_factor = p::value(spotting_profile, "camouflage_factor");
    auto calculator = field(descriptor, "computeBaseInvisibility");
    if (PyCallable_Check(calculator.get())) {
        PyObject* values = PyObject_CallFunctionObjArgs(calculator.get(), crew_factor.get(), camouflage_id, nullptr);
        if (values) {
            p::Ref held(values);
            if ((PyList_Check(values) || PyTuple_Check(values)) && PySequence_Size(values) >= 2) {
                auto first = p::item(values, 0), second = p::item(values, 1);
                const double moving = PyFloat_AsDouble(first.get());
                const double still = PyFloat_AsDouble(second.get());
                if (!PyErr_Occurred()) return {std::isfinite(moving) ? moving : 0., std::isfinite(still) ? still : 0.};
            }
        }
        PyErr_Clear();
    }
    // VehicleDescr.computeBaseInvisibility: type invisibility scaled by crew and miscAttrs factors.
    auto type = field(descriptor, "type"), values = field(type.get(), "invisibility");
    std::array<double, 2> base{0., 0.};
    if ((PyList_Check(values.get()) || PyTuple_Check(values.get())) && PySequence_Size(values.get()) >= 2) {
        auto first = p::item(values.get(), 0), second = p::item(values.get(), 1);
        base = {PyFloat_AsDouble(first.get()), PyFloat_AsDouble(second.get())};
        if (PyErr_Occurred()) { PyErr_Clear(); base = {0., 0.}; }
    }
    auto misc = field(descriptor, "miscAttrs");
    const double crew = crew_factor.get() == Py_None ? 0. : PyFloat_AsDouble(crew_factor.get());
    if (PyErr_Occurred()) PyErr_Clear();
    const double factor = std::max(0., std::isfinite(crew) ? crew : 0.) * std::max(0., number(misc.get(), "invisibilityFactor", 1.));
    return {std::max(0., base[0]) * factor, std::max(0., base[1]) * factor};
}

double shotInvisibilityFactor(PyObject* descriptor) {
    auto gun = field(descriptor, "gun");
    return std::max(0., std::min(1., number(gun.get(), "invisibilityFactorAtShot", 1.)));
}

PyObject* queryOwner(const Battle& b) {
    return b.visibility && b.visibility->core && b.visibility->core->query ? b.visibility->core->query.get() : nullptr;
}
}
}
