// Bot authority around the native simulation owner: the worker frame step, manifests, outgoing
// Bot messages and their launches, poses, belts and the worker's fixed-control diagnostics.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_bots_needs.hpp"
#include "stock_native_bot_clock.hpp"
#include "stock_native_bot_owner.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace bots {
namespace {
using OwnerRef = std::shared_ptr<stock_native_bot::Owner>;
namespace owner = stock_native_bot::owner;
constexpr double MANIFEST_RETRY_SECONDS = 0.25;
constexpr double REMOTE_TRACK_SECONDS = 1.0 / 20.0;
constexpr double TRACK_SCROLL_MIN = -15.0, TRACK_SCROLL_MAX = 30.0;
constexpr double MOVING_SPEED = 0.05, TURNING_RATE = 0.02;
constexpr std::int64_t ENGINE_OFF = 0, ENGINE_IDLE = 1, ENGINE_RUNNING = 2;
constexpr std::int64_t MOVE_FORWARD = 1, MOVE_BACKWARD = 2, ROTATE_LEFT = 4, ROTATE_RIGHT = 8;
const char* const DIAGNOSTIC_FIELDS[] = {
    "alive_bot_ticks", "suspension_param_failures", "visibility_queue_depth", "visibility_queue_max_depth",
    "visibility_oldest_stale_age_ms", "visibility_oldest_stale_max_age_ms", "visibility_admitted", "visibility_completed",
    "visibility_deferred", "visibility_selected_services", "visibility_fire_services", "visibility_new_services",
    "visibility_ordinary_services", "shot_lane_pending_pairs", "shot_lane_pending_max_pairs", "shot_lane_oldest_due_age_ms",
    "shot_lane_oldest_due_max_age_ms", "shot_lane_completed_pairs", "shot_lane_budget_deferred_attempts"};

BotAuthority* authority(const Battle& b) { return b.bots.get(); }
PyObject* shell(const Battle& b) { return b.engine.shell.get(); }
PyObject* runtime(const Battle& b) { return b.bots->runtime.get(); }
// The native owner, created on first use while this client is the round's Bot authority.
OwnerRef prepare(Battle& b) {
    auto* a = authority(b);
    if (!a || !a->runtime) return nullptr;
    PyObject* raw = a->runtime.get();
    if (!isAuthority(b) || py::attr(raw, "adapter").get() == Py_None || p::flag(py::attr(raw, "finished").get()))
        return nullptr;
    if (!a->owner) a->owner = owner::create(b, raw);
    return a->owner;
}
OwnerRef existing(const Battle& b) {
    const auto* a = authority(b);
    return a ? a->owner : nullptr;
}
p::Ref client(const Battle& b) { return p::retain(b.engine.client ? b.engine.client.get() : Py_None); }
// Native world events wait until the battle is live; each accepted send is acknowledged.
bool flushWorld(Battle& b) {
    OwnerRef o = existing(b);
    if (!o) return true;
    auto events = owner::worldEvents(*o, b, runtime(b));
    if (!b.live) return PySequence_Size(events.get()) == 0;
    for (Py_ssize_t i = 0; i < PySequence_Size(events.get()); ++i) {
        auto row = p::item(events.get(), i);
        auto sequence = p::item(row.get(), 0), event = p::item(row.get(), 1);
        if (!p::flag(py::call(client(b).get(), "send_destructible", event.get()).get())) return false;
        owner::acknowledgeWorld(*o, std::uint64_t(p::wide(sequence.get())));
    }
    return true;
}
std::string text(PyObject* v) { return v && v != Py_None ? p::string(p::Ref(PyObject_Str(v)).get()) : std::string(); }
bool messageIs(PyObject* message, const char* type) {
    return PyDict_Check(message) && text(PyDict_GetItemString(message, "type")) == type;
}
p::Ref field(PyObject* message, const char* name) { return p::value(message, name); }
bool has(PyObject* message, const char* name) { return PyDict_Check(message) && PyDict_GetItemString(message, name); }
p::Ref callKeywords(PyObject* fn, std::initializer_list<PyObject*> positional, PyObject* keywords) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(positional.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : positional) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    return p::Ref(PyObject_Call(fn, tuple.get(), keywords));
}
std::array<Value, 4> manifestIdentity(const Battle& b) {
    const auto* a = authority(b);
    auto epoch = py::optional(client(b).get(), "authority_epoch");
    Value generation;
    generation.kind = Value::Kind::Integer;
    generation.integer = std::int64_t(b.generation);
    return {generation, b.start.get("round_id"),
            cs::read(py::optional(a->runtime.get(), "authority_id").get()), cs::read(epoch.get())};
}
void discardManifest(Battle& b) {
    auto* a = authority(b);
    if (a->runtime) py::call(a->runtime.get(), "discard_pending_manifest");
    a->next_manifest_retry = a->manifest_retry_deadline = 0.;
    a->manifest_retry_identity.reset();
}

// The outgoing authority message through its LAN client sender.
bool send(Battle& b, PyObject* message) {
    auto lan = client(b);
    if (messageIs(message, "bot_manifest")) {
        auto profiles = field(message, "player_collision_profiles");
        auto fn = py::attr(lan.get(), "send_bot_manifest");
        auto bots = field(message, "bots");
        return p::flag((profiles.get() != Py_None ? py::invoke(fn.get(), bots.get(), profiles.get()) : py::invoke(fn.get(), bots.get())).get());
    }
    if (messageIs(message, "bot_state")) {
        Object armors = own(field(message, "human_ram_armors"));
        if (armors.get() == Py_None && b.worker) {
            OwnerRef o = existing(b);
            armors = own(o ? owner::humanRam(*o, b, runtime(b)) : p::Ref(PyList_New(0)));
        }
        auto kwargs = p::dict();
        if (has(message, "push_contacts")) p::set(kwargs.get(), "push_contacts", field(message, "push_contacts"));
        if (b.worker) {
            OwnerRef o = existing(b);
            auto turrets = o ? owner::detachedRows(*o) : p::Ref(PyList_New(0));
            if (p::flag(turrets.get())) p::set(kwargs.get(), "detached_turrets", turrets);
        }
        if (has(message, "sample_time_us")) {
            p::set(kwargs.get(), "sample_time_us", field(message, "sample_time_us"));
            p::set(kwargs.get(), "source_batch_horizon_us", field(message, "source_batch_horizon_us"));
        }
        if (armors.get() != Py_None) p::set(kwargs.get(), "human_ram_armors", p::retain(armors.get()));
        auto rows = field(message, "rows");
        auto projected = py::optional(lan.get(), "send_projected_bot_state");
        if (py::callable(projected)) {
            p::set(kwargs.get(), "edge_sample_time_us", field(message, "edge_sample_time_us"));
            p::set(kwargs.get(), "edge_revision", field(message, "edge_revision"));
            return p::flag(callKeywords(projected.get(), {rows.get()}, kwargs.get()).get());
        }
        return p::flag(callKeywords(py::attr(lan.get(), "send_bot_state").get(), {rows.get()}, kwargs.get()).get());
    }
    if (messageIs(message, "bot_observation")) {
        auto kwargs = p::dict();
        p::set(kwargs.get(), "radio_links", field(message, "radio_links"));
        p::set(kwargs.get(), "player_vision_ranges", field(message, "player_vision_ranges"));
        auto contacts = field(message, "contacts"), affordances = field(message, "affordances");
        return p::flag(callKeywords(py::attr(lan.get(), "send_bot_observation").get(), {contacts.get(), affordances.get()}, kwargs.get()).get());
    }
    if (messageIs(message, "bot_ram")) {
        auto kwargs = p::dict();
        for (const char* key : {"contact_positions", "track_contacts_bot", "track_contacts_target", "track_impact_speed"})
            if (has(message, key)) p::set(kwargs.get(), key, field(message, key));
        std::vector<p::Ref> values;
        for (const char* key : {"bot_id", "target_kind", "target_id", "ram_seq", "damage_to_bot", "damage_to_target",
                                "ram_contact_player_id", "ram_contact_seq"})
            values.push_back(field(message, key));
        p::Ref tuple(PyTuple_New(Py_ssize_t(values.size())));
        for (std::size_t i = 0; i < values.size(); ++i) PyTuple_SET_ITEM(tuple.get(), Py_ssize_t(i), values[i].release());
        return p::flag(p::Ref(PyObject_Call(py::attr(lan.get(), "send_bot_ram").get(), tuple.get(), kwargs.get())).get());
    }
    if (messageIs(message, "rules_state")) {
        auto rules = field(message, "rules");
        auto bases = PyDict_Check(rules.get()) ? p::value(rules.get(), "bases") : p::retain(Py_None);
        return p::flag(py::call(lan.get(), "send_rules_state", bases.get()).get());
    }
    if (messageIs(message, "battle_result")) {
        auto winner = field(message, "winner"), reason = field(message, "reason"), base = field(message, "base_team");
        return p::flag(py::call(lan.get(), "send_battle_result", winner.get(), reason.get(), base.get()).get());
    }
    return false;
}

// Publishes accepted Bot launches in fire-sequence order; a refused launch blocks that Bot's later ones.
bool resolveFire(Battle& b, PyObject* message) {
    OwnerRef o = existing(b);
    if (!o) return false;
    auto sender = py::optional(client(b).get(), "send_projectile_launch");
    if (!py::callable(sender)) return false;
    bool complete = true;
    Object wave = own(owner::projectileLaunches(*o, runtime(b), message));
    while (p::flag(wave.get())) {
        for (Py_ssize_t i = 0; i < PySequence_Size(wave.get()); ++i) {
            auto row = p::item(wave.get(), i);
            auto bot = p::item(row.get(), 0), sequence = p::item(row.get(), 1), args = p::item(row.get(), 2), kwargs = p::item(row.get(), 3);
            bool accepted = false;
            PyObject* result = PyObject_Call(sender.get(), args.get(), kwargs.get());
            if (result) {
                accepted = PyObject_RichCompareBool(result, sequence.get(), Py_EQ) == 1;
                Py_DECREF(result);
            } else if (PyErr_ExceptionMatches(PyExc_Exception)) {
                owner::transportError(*o, describe(p::Error("projectile launch send failed")));
            } else {
                throw p::Error("projectile launch send interrupted");
            }
            owner::projectileLaunchResult(*o, p::wide(bot.get()), p::wide(sequence.get()), accepted);
            complete = complete && accepted;
        }
        wave = own(owner::projectileLaunches(*o, runtime(b), Py_None));
    }
    return complete;
}

// Sends one authority message; an accepted Bot state then publishes its launches.
bool enqueue(Battle& b, PyObject* message) {
    const bool accepted = send(b, message);
    if (accepted && messageIs(message, "bot_state")) resolveFire(b, message);
    return accepted;
}

// Commits one frozen manifest only after the transport accepted it.
bool enqueueManifest(Battle& b, PyObject* message, double now) {
    auto* a = authority(b);
    if (!messageIs(message, "bot_manifest") || !a->runtime) return false;
    auto pending = py::call(a->runtime.get(), "pending_manifest");
    if (pending.get() == Py_None || PyObject_RichCompareBool(pending.get(), message, Py_NE) == 1) return false;
    const auto identity = manifestIdentity(b);
    const bool same = a->manifest_retry_identity && *a->manifest_retry_identity == identity;
    if (same && a->manifest_retry_deadline > 0. && now + 1.0e-9 >= a->manifest_retry_deadline) {
        discardManifest(b);
        throw p::Error("worker bot manifest enqueue timed out");
    }
    if (same && a->next_manifest_retry > 0. && now + 1.0e-9 < a->next_manifest_retry) return false;
    if (!send(b, message)) {
        a->next_manifest_retry = now + MANIFEST_RETRY_SECONDS;
        if (!same) a->manifest_retry_deadline = now + b.config.get("startupTimeoutSeconds").numeric(30.);
        a->manifest_retry_identity = identity;
        return false;
    }
    if (!p::flag(py::call(a->runtime.get(), "mark_manifest_enqueued", message).get()))
        throw p::Error("enqueued bot manifest does not match the pending payload");
    a->next_manifest_retry = a->manifest_retry_deadline = 0.;
    a->manifest_retry_identity.reset();
    return true;
}

// Sends what BotRuntime.battle_start returned: the manifest through its retry tenure, the rest directly.
void dispatchOutgoing(Battle& b, PyObject* outgoing) {
    for (Py_ssize_t i = 0; i < PySequence_Size(outgoing); ++i) {
        auto message = p::item(outgoing, i);
        if (messageIs(message.get(), "bot_manifest")) enqueueManifest(b, message.get(), b.clock());
        else send(b, message.get());
    }
}

// The worker's fixed-control debt after one Bot update.
void recordControl(Battle& b, const stock_native_bot::Clock& clock, std::int64_t sample_before) {
    auto* a = authority(b);
    const std::int64_t advanced = std::max<std::int64_t>(0, clock.sample_us - sample_before);
    const bool moved = advanced > 0;
    const double seconds = double(advanced) / 1000000.;
    std::int64_t steps = std::int64_t(clock.callback_steps);
    double max_step = std::max(0., clock.callback_max_step);
    if (!std::isfinite(max_step)) max_step = seconds;
    const double debt = std::max(0., clock.bank);
    a->control_debt = debt;
    a->max_control_debt = std::max(a->max_control_debt, debt);
    if (moved) {
        if (steps <= 0) steps = 1;
        a->control_steps += std::uint64_t(steps);
        a->max_control_step = std::max(a->max_control_step, max_step);
        const double period = std::max(0., clock.period);
        if (steps > 1 || seconds > period + 1.0e-9) ++a->catchup_callbacks;
        if (debt + 1.0e-9 >= period && period > 0.) ++a->debt_callbacks;
    }
}
// The #1513 collision preparation scope brackets every native world query of one Bot step.
struct CollisionPreparation {
    Battle& b;
    explicit CollisionPreparation(Battle& battle) : b(battle) { world::beginCollisionPreparation(b); }
    ~CollisionPreparation() { world::endCollisionPreparation(b); }
};
}

void install(Battle& b, PyObject* runtime) {
    if (!b.bots) b.bots = std::make_unique<BotAuthority>();
    b.bots->runtime = borrow(runtime);
    py::set(runtime, "_standalone_battle", shell(b));
}

void retire(Battle& b) {
    auto* a = authority(b);
    if (!a) return;
    if (a->owner) {
        OwnerRef o = std::move(a->owner);
        owner::close(*o);
    }
    b.bots.reset();
}

bool enabled(const Battle& b) { return b.worker && authority(b) && authority(b)->runtime; }

bool isAuthority(const Battle& b) {
    const auto* a = authority(b);
    return a && a->runtime && p::flag(py::call(a->runtime.get(), "is_authority").get());
}

void update(Battle& b, double now, double dt) {
    auto* a = authority(b);
    if (!a || !b.live || b.replay) return;
    if (b.worker) ++a->authority_callbacks;
    // Only the hidden worker's native owner simulates; a visible client is never the Bot authority.
    if (!enabled(b)) return;
    PyObject* raw = a->runtime.get();
    OwnerRef o = prepare(b);
    if (!o) return;
    auto players = owner::players(b, raw);
    auto results = owner::playerContacts(*o, b, raw, players.get(), now);
    if (flushWorld(b)) {
        for (Py_ssize_t i = 0; i < PySequence_Size(results.get()); ++i) {
            auto row = p::item(results.get(), i);
            auto player = p::value(row.get(), "player_id"), seq = p::value(row.get(), "seq");
            auto accepted = p::value(row.get(), "accepted"), token = p::value(row.get(), "token");
            if (p::flag(py::call(client(b).get(), "send_player_destructible_contact_result", player.get(), seq.get(), accepted.get(),
                                 token.get()).get()))
                owner::acknowledgePlayerContact(*o, std::uint64_t(p::wide(p::value(row.get(), "serial").get())), now);
        }
    }
    const std::int64_t sample_before = owner::clock(*o).sample_us;
    Object outgoing;
    {
        CollisionPreparation scope(b);
        outgoing = own(owner::update(*o, b, raw, dt, now, players.get()));
    }
    flushWorld(b);
    recordControl(b, owner::clock(*o), sample_before);
    for (Py_ssize_t i = 0; i < PySequence_Size(outgoing.get()); ++i) {
        auto message = p::item(outgoing.get(), i);
        const bool state = messageIs(message.get(), "bot_state");
        if (state) ++a->generated;
        const bool accepted = enqueue(b, message.get());
        if (state) ++(accepted ? a->enqueued : a->send_failed);
    }
}

std::size_t presentPoses(Battle& b, double now) {
    auto* a = authority(b);
    if (!a || !b.live || b.replay || !enabled(b)) return 0;
    OwnerRef o = prepare(b);
    if (!o) return 0;
    a->bot_count = owner::presentPoses(*o, b, runtime(b), now);
    return a->bot_count;
}

bool drainWorkerTransport(Battle& b) {
    auto drain = py::optional(client(b).get(), "dispatch_ready_messages");
    if (!b.worker || !py::callable(drain)) return true;
    const auto generation = b.generation;
    const auto* before = authority(b);
    const Value round = b.start.get("round_id");
    py::invoke(drain.get());
    // Dispatch can finish or replace the round; no old frame may advance on a new lifecycle.
    Battle* now = current();
    return now == &b && b.running() && b.generation == generation && authority(b) == before && b.start.get("round_id") == round;
}

void retryManifest(Battle& b, double now) {
    auto* a = authority(b);
    if (!b.worker || !b.running() || !a || !a->runtime) return;
    PyObject* runtime = a->runtime.get();
    if (b.result.kind != Value::Kind::Null || p::flag(py::optional(runtime, "finished").get())) { discardManifest(b); return; }
    const auto identity = manifestIdentity(b);
    if (a->manifest_retry_identity && *a->manifest_retry_identity != identity) { discardManifest(b); return; }
    if (cs::read(py::optional(runtime, "round_id").get()) != b.start.get("round_id")) { discardManifest(b); return; }
    auto client_round = py::optional(client(b).get(), "round_id");
    if (client_round.get() != Py_None && cs::read(client_round.get()) != b.start.get("round_id")) { discardManifest(b); return; }
    auto check = py::optional(client(b).get(), "is_bot_authority");
    if (py::callable(check) && !p::flag(py::invoke(check.get()).get())) { discardManifest(b); return; }
    auto pending = py::call(runtime, "pending_manifest");
    if (pending.get() == Py_None) {
        a->next_manifest_retry = a->manifest_retry_deadline = 0.;
        a->manifest_retry_identity.reset();
        return;
    }
    if (a->manifest_retry_deadline > 0. && now + 1.0e-9 >= a->manifest_retry_deadline) {
        discardManifest(b);
        throw p::Error("worker bot manifest enqueue timed out");
    }
    if (a->next_manifest_retry > 0. && now + 1.0e-9 < a->next_manifest_retry) return;
    enqueueManifest(b, pending.get(), now);
}

// The opt-in AuthorityWorkerProbe diagnostic is not carried into the native battle.
void workerProbe(Battle&) {}

double rememberTrackTurn(Actor& vehicle, double yaw, double now) {
    auto& sample = vehicle.presented.track_turn_sample;
    double turn = 0.;
    if (sample) {
        const double elapsed = std::max(0., now - (*sample)[0]);
        if (elapsed > 0.) {
            const double pi = 3.141592653589793;
            const double shifted = std::fmod(yaw - (*sample)[1] + pi, 2. * pi);
            turn = ((shifted < 0. ? shifted + 2. * pi : shifted) - pi) / elapsed;
        }
    }
    sample = std::array<double, 2>{now, yaw};
    return turn;
}

bool tracks(Battle& b, Actor& vehicle, double yaw, double now) {
    // Belt scroll and engine mode are presentation owned by the visible client.
    if (b.worker || !b.engine.remote_factory) return false;
    auto engine = p::integer(vehicle.engine_id);
    auto entity = py::call(b.engine.remote_factory.get(), "get", engine.get());
    if (entity.get() == Py_None) return false;
    if (vehicle.native_remote && PyObject_HasAttrString(entity.get(), "_offlineNativeDrawVisible") &&
        !p::flag(py::attr(entity.get(), "_offlineNativeDrawVisible").get()) && !vehicle.presented.track_pending)
        return true;
    return tracksWithTurn(b, vehicle, rememberTrackTurn(vehicle, yaw, now), now);
}

bool tracksWithTurn(Battle& b, Actor& vehicle, double turn, double now) {
    if (b.worker || !b.engine.remote_factory) return false;
    auto& shown = vehicle.presented;
    // Carry the 20 Hz deadline so every render rate averages the same feed cadence.
    if (shown.track_next_update > 0. && now + 1.0e-9 < shown.track_next_update) return false;
    auto engine = p::integer(vehicle.engine_id);
    auto entity = py::call(b.engine.remote_factory.get(), "get", engine.get());
    if (entity.get() == Py_None) return false;
    const double base = shown.track_next_update > 0. ? shown.track_next_update : now;
    const double periods = std::max(1., std::floor(std::max(0., now - base) / REMOTE_TRACK_SECONDS) + 1.);
    shown.track_next_update = base + periods * REMOTE_TRACK_SECONDS;
    const Value& state = shown.state.kind == Value::Kind::Null ? vehicle.state : shown.state;
    const bool alive = state.get("alive").truth(true) && state.get("health").numeric(1.) > 0.;
    const double speed = state.get("speed").numeric();
    std::int64_t power = ENGINE_OFF, flags = 0;
    if (alive) {
        if (speed > MOVING_SPEED) flags |= MOVE_FORWARD;
        else if (speed < -MOVING_SPEED) flags |= MOVE_BACKWARD;
        if (turn < -TURNING_RATE) flags |= ROTATE_LEFT;
        else if (turn > TURNING_RATE) flags |= ROTATE_RIGHT;
        power = flags ? ENGINE_RUNNING : ENGINE_IDLE;
    }
    auto descriptor = py::attr(entity.get(), "typeDescriptor");
    const auto scroll = local::trackScroll(b, vehicle, descriptor.get(), speed, turn, state.get("airborne").truth(),
                                           state.get("movement_dir").numeric());
    auto clamp = [](double value) { return std::max(TRACK_SCROLL_MIN, std::min(TRACK_SCROLL_MAX, value)); };
    auto left = p::number(clamp(scroll[0])), right = p::number(clamp(scroll[1]));
    p::Ref mode(Py_BuildValue("(LL)", static_cast<long long>(power), static_cast<long long>(flags)));
    return p::flag(py::call(entity.get(), "update_tracks", left.get(), right.get(), mode.get()).get());
}

void battleStart(Battle& b, PyObject* message) {
    auto* a = authority(b);
    if (!a || !a->runtime) return;
    auto outgoing = py::call(a->runtime.get(), "battle_start", message);
    dispatchOutgoing(b, outgoing.get());
}

bool reconcileAuthority(Battle& b, PyObject* player_id) {
    auto* a = authority(b);
    if (!a || !a->runtime) return false;
    PyObject* runtime = a->runtime.get();
    if (PyObject_RichCompareBool(py::optional(runtime, "authority_id").get(), player_id, Py_EQ) == 1) return false;
    Object start = own(p::Ref(cs::write(b.start)));
    if (!PyDict_Check(start.get())) start = own(p::dict());
    p::set(start.get(), "bot_authority_id", p::retain(player_id));
    auto lan = client(b);
    if (b.worker) {
        auto protocol = p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.lan_protocol"));
        auto capability = py::attr(protocol.get(), "HUMAN_RAM_TIMELINE_CAPABILITY");
        auto mine = py::optional(lan.get(), "capabilities"), server = py::optional(lan.get(), "server_capabilities");
        if (mine.get() != Py_None && server.get() != Py_None && PySequence_Contains(mine.get(), capability.get()) == 1 &&
            PySequence_Contains(server.get(), capability.get()) == 1)
            p::set(start.get(), "human_ram_timeline", p::boolean(true));
    }
    if (!b.worker) {
        // A visible client tracks the authority lineage only and never builds a takeover manifest.
        if (p::flag(py::call(runtime, "battle_start", start.get()).get())) throw p::Error("visible client attempted to become bot authority");
        return true;
    }
    Object snapshot = a->last_snapshot && PyDict_Check(a->last_snapshot.get()) ? a->last_snapshot : own(p::dict());
    // The server manifest owns identity/profile/route; snapshot.bots owns the live pose and fire sequence.
    Object manifest = own(p::value(snapshot.get(), "bot_manifest"));
    if (manifest.get() == Py_None) manifest = own(p::value(start.get(), "bot_manifest"));
    auto live = p::dict();
    auto rows = p::value(snapshot.get(), "bots");
    if (p::flag(rows.get()))
        for (Py_ssize_t i = 0; i < PySequence_Size(rows.get()); ++i) {
            auto raw = p::item(rows.get(), i);
            if (PyDict_Check(raw.get()) && PyDict_GetItemString(raw.get(), "id") && PyDict_GetItemString(raw.get(), "id") != Py_None) {
                auto id = p::Ref(PyNumber_Int(PyDict_GetItemString(raw.get(), "id")));
                if (PyDict_SetItem(live.get(), id.get(), raw.get()) < 0) throw p::Error("live Bot rows");
            }
        }
    p::Ref takeover(PyList_New(0));
    if (p::flag(manifest.get()))
        for (Py_ssize_t i = 0; i < PySequence_Size(manifest.get()); ++i) {
            auto raw = p::item(manifest.get(), i);
            if (!PyDict_Check(raw.get()) || !PyDict_GetItemString(raw.get(), "id") || PyDict_GetItemString(raw.get(), "id") == Py_None) continue;
            p::Ref merged(PyDict_Copy(raw.get()));
            auto id = p::Ref(PyNumber_Int(PyDict_GetItemString(raw.get(), "id")));
            if (PyObject* row = PyDict_GetItem(live.get(), id.get())) PyDict_Update(merged.get(), row);
            p::append(takeover.get(), merged);
        }
    if (!PyList_GET_SIZE(takeover.get()) && p::flag(rows.get()))
        for (Py_ssize_t i = 0; i < PySequence_Size(rows.get()); ++i) {
            auto raw = p::item(rows.get(), i);
            if (PyDict_Check(raw.get())) p::append(takeover.get(), p::Ref(PyDict_Copy(raw.get())));
        }
    p::set(start.get(), "bot_manifest", takeover);
    auto result = p::value(snapshot.get(), "battle_result");
    if (result.get() != Py_None) p::set(start.get(), "battle_result", result);
    dispatchOutgoing(b, py::call(runtime, "battle_start", start.get()).get());
    if (b.engine.remote_factory) {
        // Native pose providers interpolate only while this client owns the Bots.
        auto setter = py::optional(b.engine.remote_factory.get(), "set_entity_interpolate_motion");
        const bool local = PyObject_RichCompareBool(player_id, p::integer(b.local_player).get(), Py_EQ) == 1;
        if (py::callable(setter))
            for (auto& entry : b.vehicles->actors) {
                auto& v = *entry.second;
                if (v.kind != "bot" || !v.native_remote || v.tombstone) continue;
                auto engine = p::integer(v.engine_id);
                if (p::flag(py::invoke(setter.get(), engine.get(), p::boolean(local).get()).get())) v.presented.track_turn_sample.reset();
            }
    }
    return true;
}

void snapshot(Battle& b, PyObject* message) {
    if (b.bots) b.bots->last_snapshot = borrow(message);
    OwnerRef o = existing(b);
    if (!o) return;
    owner::snapshot(*o, runtime(b), message);
    if (PyDict_Check(message) && PyDict_GetItemString(message, "battle_result") && PyDict_GetItemString(message, "battle_result") != Py_None)
        py::set(runtime(b), "finished", Py_True);
}

bool projectileIngress(Battle& b, PyObject* message, double now) {
    OwnerRef o = existing(b);
    return o && owner::projectileIngress(*o, runtime(b), message, now);
}
bool projectileObserve(Battle& b, PyObject* message, double now) {
    OwnerRef o = existing(b);
    return o && owner::projectileObserve(*o, runtime(b), message, now);
}
bool projectileEvent(Battle& b, PyObject* event, double now) {
    OwnerRef o = existing(b) ? existing(b) : prepare(b);
    return o && owner::projectileEvent(*o, runtime(b), event, now);
}
std::size_t projectileAdvance(Battle& b, double now, bool force_progress) {
    OwnerRef o = existing(b);
    if (!o) return 0;
    auto rows = owner::projectileAdvance(*o, b, runtime(b), now, force_progress);
    auto lan = client(b);
    std::size_t sent_count = 0;
    for (Py_ssize_t i = 0; i < PySequence_Size(rows.get()); ++i) {
        auto row = p::item(rows.get(), i);
        auto serial = p::item(row.get(), 0), kind = p::item(row.get(), 1), wire = p::item(row.get(), 2);
        const std::string name = "send_projectile_" + text(kind.get());
        bool sent = false;
        PyObject* result = PyObject_Call(py::attr(lan.get(), name.c_str()).get(), p::Ref(PyTuple_New(0)).get(), wire.get());
        if (result) { sent = PyObject_IsTrue(result) > 0; Py_DECREF(result); }
        else if (PyErr_ExceptionMatches(PyExc_Exception)) {
            owner::transportError(*o, describe(p::Error("projectile send failed")));
        } else {
            throw p::Error("projectile send interrupted");
        }
        owner::projectileAcknowledge(*o, std::uint64_t(p::wide(serial.get())), sent);
        sent_count += sent ? 1 : 0;
    }
    return sent_count;
}
bool worldIngress(Battle& b, PyObject* event, double now) {
    OwnerRef o = prepare(b);
    return o && owner::worldIngress(*o, runtime(b), event, now);
}
bool detachedQueue(Battle& b, Actor& vehicle, PyObject* entity, PyObject* state, double now) {
    OwnerRef o = existing(b) ? existing(b) : prepare(b);
    return o && owner::detachedQueue(*o, runtime(b), vehicle, entity, state, now);
}
bool detachedIngress(Battle& b, PyObject* message, double now) {
    OwnerRef o = existing(b);
    return o && owner::detachedIngress(*o, runtime(b), message, now);
}
bool acknowledgeLaunch(Battle& b, std::int64_t bot_id, std::int64_t sequence) {
    OwnerRef o = existing(b);
    return o && owner::acknowledgeLaunch(*o, bot_id, sequence);
}
bool acknowledgeRam(Battle& b, std::int64_t player_id, std::int64_t sequence) {
    OwnerRef o = existing(b);
    if (o) owner::acknowledgeRam(*o, player_id, sequence);
    return bool(o);
}
std::size_t prewarm(Battle& b, double now) {
    OwnerRef o = prepare(b);
    return o ? owner::prewarm(*o, b, runtime(b), now) : 0;
}
std::size_t prewarmPlayers(Battle& b, double now) {
    OwnerRef o = prepare(b);
    return o ? owner::prewarmPlayers(*o, b, runtime(b), now) : 0;
}

// The hidden worker's water observations; sent when the native owner produced a batch.
bool playerEnvironment(Battle& b, double dt, double now) {
    auto lan = client(b);
    if (dt <= 0. || lan.get() == Py_None) return false;
    OwnerRef o = prepare(b);
    if (!o) return false;
    const bool authority_flag = p::flag(py::call(lan.get(), "is_bot_authority").get());
    auto batch = owner::playerEnvironment(*o, b, runtime(b), dt, authority_flag);
    if (!batch) return false;
    auto sequence = p::integer(batch->first);
    auto sender = py::optional(lan.get(), "send_player_environment");
    const bool sent = py::callable(sender) && p::flag(py::invoke(sender.get(), batch->second.get(), sequence.get()).get());
    owner::acknowledgePlayerEnvironment(*o, b, batch->first, sent);
    return sent;
}

bool readyForDrawOff(const Battle& b) {
    if (!b.worker || !b.running()) return false;
    if (b.roster && (!b.roster->pending_order.empty() || !b.roster->pending_bot_creates.empty())) return false;
    for (const auto& entry : b.vehicles->actors)
        if (!entry.second->tombstone && !entry.second->ready) return false;
    return !b.vehicles->actors.empty();
}

p::Ref controlSnapshot(const Battle& b) {
    const auto* a = authority(b);
    auto out = p::dict();
    if (!a) return out;
    p::set(out.get(), "control_steps", p::integer(std::int64_t(a->control_steps)));
    p::set(out.get(), "catchup_callbacks", p::integer(std::int64_t(a->catchup_callbacks)));
    p::set(out.get(), "debt_callbacks", p::integer(std::int64_t(a->debt_callbacks)));
    p::set(out.get(), "max_control_step_ms", p::number(a->max_control_step * 1000.));
    p::set(out.get(), "control_debt_ms", p::number(a->control_debt * 1000.));
    p::set(out.get(), "max_control_debt_ms", p::number(a->max_control_debt * 1000.));
    p::set(out.get(), "astar_budget_exhausted_callbacks", p::integer(std::int64_t(a->astar_budget_exhausted)));
    p::set(out.get(), "astar_max_pending", p::integer(a->astar_max_pending));
    return out;
}

p::Ref diagnosticTotals(const Battle& b) {
    const auto* a = authority(b);
    auto result = p::dict();
    if (!a || !a->runtime) return result;
    PyObject* raw = PyObject_CallMethod(a->runtime.get(), const_cast<char*>("diagnostic_totals"), nullptr);
    if (!raw) { if (!PyErr_ExceptionMatches(PyExc_Exception)) throw p::Error("Bot diagnostics interrupted"); PyErr_Clear(); return result; }
    p::Ref totals(raw);
    if (!PyDict_Check(totals.get())) return result;
    for (const char* name : DIAGNOSTIC_FIELDS) {
        PyObject* value = PyDict_GetItemString(totals.get(), name);
        if (!value || PyBool_Check(value) || !(PyInt_Check(value) || PyLong_Check(value))) continue;
        if (PyObject_RichCompareBool(value, p::integer(0).get(), Py_LT) == 1) continue;
        p::set(result.get(), name, p::Ref(PyNumber_Int(value)));
    }
    return result;
}
}
}
