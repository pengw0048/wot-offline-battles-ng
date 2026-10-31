// Bot authority around the native simulation owner: the worker frame step, manifests, outgoing
// Bot messages and their launches, poses, belts and the worker's fixed-control diagnostics.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_remote.hpp"
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_bots_needs.hpp"
#include "stock_native_bot_clock.hpp"
#include "stock_native_bot_owner.hpp"
#include "stock_native_battle_link.hpp"
#include "stock_native_bot_value.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace bots {
namespace {
using OwnerRef = std::shared_ptr<stock_native_bot::Owner>;
bool feedTracks(Battle& b, Actor& vehicle, PyObject* entity, double turn, double now);
namespace owner = stock_native_bot::owner;
namespace v = stock_native_bot_value;
constexpr double MANIFEST_RETRY_SECONDS = 0.25;
constexpr double REMOTE_TRACK_SECONDS = 1.0 / 20.0;
constexpr double TRACK_SCROLL_MIN = -15.0, TRACK_SCROLL_MAX = 30.0;
constexpr double MOVING_SPEED = 0.05, TURNING_RATE = 0.02;
constexpr std::int64_t ENGINE_OFF = 0, ENGINE_IDLE = 1, ENGINE_RUNNING = 2;
constexpr std::int64_t MOVE_FORWARD = 1, MOVE_BACKWARD = 2, ROTATE_LEFT = 4, ROTATE_RIGHT = 8;
BotAuthority* authority(const Battle& b) { return b.bots.get(); }
// The native owner, created on first use while this client is the round's Bot authority.
OwnerRef prepare(Battle& b) {
    auto* a = authority(b);
    if (!a || !a->seed) return nullptr;
    if (!isAuthority(b) || a->finished) return nullptr;
    if (!a->owner) a->owner = owner::create(b);
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
    const auto events = owner::worldEvents(*o, b);
    if (!b.live) return events.empty();
    for (const auto& row : events) {
        auto message = v::object();
        message.object["type"] = v::text("destructible");
        message.object["event"] = row.second;
        if (!link::send(b, message)) return false;
        owner::acknowledgeWorld(*o, row.first);
    }
    return true;
}
std::string text(PyObject* v) { return v && v != Py_None ? p::string(p::Ref(PyObject_Str(v)).get()) : std::string(); }
const std::string& typeOf(const Value& message) {
    static const std::string none;
    const auto& type = message.resolved().get("type").resolved();
    return type.kind == Value::Kind::Text ? type.text : none;
}
std::array<Value, 4> manifestIdentity(const Battle& b) {
    const auto* a = authority(b);
    const auto epoch = link::authorityEpoch(b);
    Value owner_id;
    if (a->authority_id) owner_id = v::integer(*a->authority_id);
    return {v::integer(std::int64_t(b.generation)), b.start.get("round_id"), owner_id, epoch ? v::integer(*epoch) : Value()};
}
void discardManifest(Battle& b) {
    auto* a = authority(b);
    discardPendingManifest(b);
    a->next_manifest_retry = a->manifest_retry_deadline = 0.;
    a->manifest_retry_identity.reset();
}
// Sends each player destructible contact verdict; an accepted send is acknowledged.
std::size_t sendContactResults(Battle& b, stock_native_bot::Owner& o, const Value& results, double now) {
    std::size_t sent = 0;
    for (const auto& entry : results.resolved().array) {
        const auto& row = entry.resolved();
        auto message = v::object();
        message.object["type"] = v::text("player_destructible_contact_result");
        message.object["player_id"] = row.get("player_id");
        message.object["contact_seq"] = row.get("seq");
        message.object["accepted"] = row.get("accepted");
        message.object["token"] = row.get("token");
        if (link::send(b, message)) {
            owner::acknowledgePlayerContact(o, std::uint64_t(v::wide(row.get("serial"))), now);
            ++sent;
        }
    }
    return sent;
}

// The outgoing authority message, shaped as its link message.
bool send(Battle& b, const Value& source) {
    const auto& message = source.resolved();
    if (message.kind != Value::Kind::Object) return false;
    const std::string& type = typeOf(message);
    auto out = v::object();
    auto& d = out.object;
    d["type"] = v::text(type);
    const auto copy = [&](const char* key) { d[key] = message.get(key); };
    const auto optional = [&](const char* key) { if (message.object.count(key)) copy(key); };
    if (type == "bot_manifest") {
        copy("bots");
        if (message.get("player_collision_profiles").resolved().kind != Value::Kind::Null) copy("player_collision_profiles");
        return link::send(b, out);
    }
    if (type == "bot_state") {
        Value armors = message.get("human_ram_armors");
        if (armors.resolved().kind == Value::Kind::Null && b.worker) {
            OwnerRef o = existing(b);
            armors = o ? owner::humanRam(*o, b) : v::array();
        }
        copy("rows");
        optional("push_contacts");
        if (b.worker) {
            OwnerRef o = existing(b);
            if (o) {
                Value turrets = owner::detachedRows(*o);
                if (turrets.truth()) d["detached_turrets"] = std::move(turrets);
            }
        }
        if (message.object.count("sample_time_us")) { copy("sample_time_us"); copy("source_batch_horizon_us"); }
        if (armors.resolved().kind != Value::Kind::Null) d["human_ram_armors"] = std::move(armors);
        copy("edge_sample_time_us");
        copy("edge_revision");
        return link::send(b, out);
    }
    if (type == "bot_observation") {
        for (const char* key : {"contacts", "affordances", "radio_links", "player_vision_ranges"}) copy(key);
        return link::send(b, out);
    }
    if (type == "bot_ram") {
        for (const char* key : {"bot_id", "target_kind", "target_id", "ram_seq", "damage_to_bot", "damage_to_target"}) copy(key);
        for (const char* key : {"ram_contact_player_id", "ram_contact_seq", "contact_positions", "track_contacts_bot", "track_contacts_target",
                                "track_impact_speed"})
            optional(key);
        return link::send(b, out);
    }
    if (type == "rules_state") {
        const auto& rules = message.get("rules").resolved();
        d["bases"] = rules.kind == Value::Kind::Object ? rules.get("bases") : Value();
        return link::send(b, out);
    }
    if (type == "battle_result") {
        for (const char* key : {"winner", "reason", "base_team"}) copy(key);
        return link::send(b, out);
    }
    return false;
}

// Publishes accepted Bot launches in fire-sequence order; a refused launch blocks that Bot's later ones.
bool resolveFire(Battle& b, const Value& message) {
    OwnerRef o = existing(b);
    if (!o) return false;
    bool complete = true;
    auto wave = owner::projectileLaunches(*o, &message);
    while (!wave.empty()) {
        for (const auto& launch : wave) {
            bool accepted = false;
            try { accepted = link::send(b, launch.message); }
            catch (const std::exception& e) { owner::transportError(*o, "projectile launch send failed: " + describe(e)); }
            owner::projectileLaunchResult(*o, launch.bot, launch.sequence, accepted);
            complete = complete && accepted;
        }
        wave = owner::projectileLaunches(*o, nullptr);
    }
    return complete;
}

// Sends one authority message; an accepted Bot state then publishes its launches.
bool enqueue(Battle& b, const Value& message) {
    const bool accepted = send(b, message);
    if (accepted && typeOf(message) == "bot_state") resolveFire(b, message);
    return accepted;
}

// Commits one frozen manifest only after the transport accepted it.
bool enqueueManifest(Battle& b, const Value& message, double now) {
    auto* a = authority(b);
    const Value* pending = pendingManifest(b);
    if (message.get("type").resolved().text != "bot_manifest" || !pending || *pending != message) return false;
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
    if (!markManifestEnqueued(b, message)) throw p::Error("enqueued bot manifest does not match the pending payload");
    a->next_manifest_retry = a->manifest_retry_deadline = 0.;
    a->manifest_retry_identity.reset();
    return true;
}

// The worker's Bot start carries human_ram_timeline when both ends speak it.
void markHumanRamTimeline(Battle& b, Value& start) {
    if (link::negotiated(b, "human_ram_timeline_v1")) start.object["human_ram_timeline"] = v::boolean(true);
}
// Sends what the round start returned: the manifest through its retry tenure.
void dispatchOutgoing(Battle& b, const std::vector<Value>& outgoing, bool materialize = false) {
    for (const auto& message : outgoing) {
        if (message.get("type").resolved().text != "bot_manifest") throw p::Error("unexpected Bot round start message");
        if (materialize) {
            // The authority already owns the Bot poses it publishes: create them now, not on the server echo.
            for (const auto& state : message.get("bots").resolved().array) {
                const auto& id = state.resolved().get("id").resolved();
                if (state.resolved().kind != Value::Kind::Object || id.kind == Value::Kind::Null) continue;
                Value event;
                event.kind = Value::Kind::Object;
                for (const auto& field : {std::make_pair("type", std::string("create")), std::make_pair("entity", "bot:" + std::to_string(id.integer)),
                                          std::make_pair("kind", std::string("bot"))}) {
                    Value text;
                    text.kind = Value::Kind::Text;
                    text.text = field.second;
                    event.object[field.first] = std::move(text);
                }
                event.object["id"] = id;
                event.object["state"] = state.resolved();
                vehicles::queueBotCreate(b, event);
            }
        }
        enqueueManifest(b, message, b.clock());
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

bool enabled(const Battle& b) { return b.worker && authority(b) && authority(b)->started; }

bool isAuthority(const Battle& b) {
    const auto* a = authority(b);
    return a && a->started && a->authority_id && *a->authority_id == b.local_player;
}

void update(Battle& b, double now, double dt) {
    auto* a = authority(b);
    if (!a || !b.live || b.replay) return;
    if (b.worker) ++a->authority_callbacks;
    // Only the hidden worker's native owner simulates; a visible client is never the Bot authority.
    if (!enabled(b)) return;
    OwnerRef o = prepare(b);
    if (!o) return;
    const Value players = owner::players(*o, b);
    const Value results = owner::playerContacts(*o, b, players, now);
    if (flushWorld(b)) sendContactResults(b, *o, results, now);
    const std::int64_t sample_before = owner::clock(*o).sample_us;
    const Value outgoing = owner::update(*o, b, dt, now, players);
    flushWorld(b);
    recordControl(b, owner::clock(*o), sample_before);
    for (const auto& message : outgoing.array) {
        const bool state = typeOf(message) == "bot_state";
        if (state) ++a->generated;
        const bool accepted = enqueue(b, message);
        if (state) ++(accepted ? a->enqueued : a->send_failed);
    }
}

std::size_t presentPoses(Battle& b, double now) {
    auto* a = authority(b);
    if (!a || !b.live || b.replay || !enabled(b)) return 0;
    OwnerRef o = prepare(b);
    if (!o) return 0;
    a->bot_count = owner::presentPoses(*o, b, now);
    return a->bot_count;
}

bool drainWorkerTransport(Battle& b) {
    if (!b.worker) return true;
    const auto generation = b.generation;
    const auto* before = authority(b);
    const Value round = b.start.get("round_id");
    link::drain(b);
    // Dispatch can finish or replace the round; no old frame may advance on a new lifecycle.
    Battle* now = current();
    return now == &b && b.running() && b.generation == generation && authority(b) == before && b.start.get("round_id") == round;
}

void retryManifest(Battle& b, double now) {
    auto* a = authority(b);
    if (!b.worker || !b.running() || !a || !a->started) return;
    if (b.result.kind != Value::Kind::Null || a->finished) { discardManifest(b); return; }
    const auto identity = manifestIdentity(b);
    if (a->manifest_retry_identity && *a->manifest_retry_identity != identity) { discardManifest(b); return; }
    if (a->round_id != b.start.get("round_id")) { discardManifest(b); return; }
    const auto client_round = link::roundId(b);
    if (client_round && v::integer(*client_round) != b.start.get("round_id")) { discardManifest(b); return; }
    if (!link::authority(b)) { discardManifest(b); return; }
    const Value* pending = pendingManifest(b);
    if (!pending || !isAuthority(b)) {
        a->next_manifest_retry = a->manifest_retry_deadline = 0.;
        a->manifest_retry_identity.reset();
        return;
    }
    if (a->manifest_retry_deadline > 0. && now + 1.0e-9 >= a->manifest_retry_deadline) {
        discardManifest(b);
        throw p::Error("worker bot manifest enqueue timed out");
    }
    if (a->next_manifest_retry > 0. && now + 1.0e-9 < a->next_manifest_retry) return;
    const Value manifest = *pending;
    enqueueManifest(b, manifest, now);
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
    if (b.worker || !vehicle.engine_id) return false;
    auto entity = b.entity(vehicle.engine_id);
    if (entity.get() == Py_None) return false;
    if (vehicle.native_remote && PyObject_HasAttrString(entity.get(), "_offlineNativeDrawVisible") &&
        !p::flag(py::attr(entity.get(), "_offlineNativeDrawVisible").get()) && !vehicle.presented.track_pending)
        return true;
    return feedTracks(b, vehicle, entity.get(), rememberTrackTurn(vehicle, yaw, now), now);
}

bool tracksWithTurn(Battle& b, Actor& vehicle, double turn, double now) {
    if (b.worker || !vehicle.engine_id) return false;
    const auto& shown = vehicle.presented;
    if (shown.track_next_update > 0. && now + 1.0e-9 < shown.track_next_update) return false;
    auto entity = b.entity(vehicle.engine_id);
    if (entity.get() == Py_None) return false;
    return feedTracks(b, vehicle, entity.get(), turn, now);
}

namespace {
bool feedTracks(Battle& b, Actor& vehicle, PyObject* entity, double turn, double now) {
    auto& shown = vehicle.presented;
    // Carry the 20 Hz deadline so every render rate averages the same feed cadence.
    if (shown.track_next_update > 0. && now + 1.0e-9 < shown.track_next_update) return false;
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
    auto descriptor = py::attr(entity, "typeDescriptor");
    const auto scroll = local::trackScroll(b, vehicle, descriptor.get(), speed, turn, state.get("airborne").truth(),
                                           state.get("movement_dir").numeric());
    auto clamp = [](double value) { return std::max(TRACK_SCROLL_MIN, std::min(TRACK_SCROLL_MAX, value)); };
    return remote::updateTracks(b, vehicle.engine_id, clamp(scroll[0]), clamp(scroll[1]), {int(power), int(flags)});
}
}

void reconcileAuthority(Battle& b, const Value& player) {
    auto* a = authority(b);
    if (!a || !a->started) return;
    const auto& player_id = player.resolved();
    // A Python bool was an int here: comparing it rejected it as an id.
    const bool integral = player_id.kind == Value::Kind::Integer || player_id.kind == Value::Kind::Boolean;
    if (player_id.kind == Value::Kind::Null ? !a->authority_id : a->authority_id && integral && *a->authority_id == v::wide(player_id))
        return;
    Value start = b.start.resolved().kind == Value::Kind::Object ? b.start.resolved() : Value();
    start.kind = Value::Kind::Object;
    start.object["bot_authority_id"] = player_id;
    if (!b.worker) {
        // A visible client tracks the authority lineage only and never builds a manifest.
        if (!roundStart(b, start).empty()) throw p::Error("visible client attempted to become bot authority");
        return;
    }
    markHumanRamTimeline(b, start);
    // The hidden worker joins only a waiting room and owns the Bots from the round start; a late
    // takeover of an already published line-up has no restore path here.
    const Value& snapshot = a->last_snapshot.resolved();
    const bool becomes = integral && v::wide(player_id) == b.local_player;
    if (becomes && (snapshot.get("bot_manifest").truth() || snapshot.get("bots").truth() || start.get("bot_manifest").truth()))
        throw p::Error("a Bot authority takeover is not supported by the hidden worker");
    const Value& result = snapshot.get("battle_result");
    if (result.resolved().kind != Value::Kind::Null) start.object["battle_result"] = result;
    dispatchOutgoing(b, roundStart(b, start));
}

void applySnapshot(Battle& b, const Value& message) {
    // The worker keeps the accepted (shared) snapshot for its owner's human input, ram probes and takeover.
    if (b.bots && b.worker) b.bots->last_snapshot = message;
    OwnerRef o = existing(b);
    if (!o) return;
    owner::snapshot(*o, message);
    if (message.resolved().get("battle_result").resolved().kind != Value::Kind::Null) b.bots->finished = true;
}

bool projectileIngress(Battle& b, const Value& message, double now) {
    OwnerRef o = existing(b);
    return o && owner::projectileIngress(*o, message, now);
}
bool projectileObserve(Battle& b, const Value& message, double now) {
    OwnerRef o = existing(b);
    return o && owner::projectileObserve(*o, message, now);
}
bool projectileEvent(Battle& b, const Value& event, double now) {
    OwnerRef o = existing(b) ? existing(b) : prepare(b);
    return o && owner::projectileEvent(*o, event, now);
}
std::size_t projectileAdvance(Battle& b, double now, bool force_progress) {
    OwnerRef o = existing(b);
    if (!o) return 0;
    std::size_t sent_count = 0;
    for (auto& row : owner::projectileAdvance(*o, b, now, force_progress)) {
        row.wire.object["type"].kind = Value::Kind::Text;
        row.wire.object["type"].text = "projectile_" + row.kind;
        bool sent = false;
        try { sent = link::send(b, row.wire); }
        catch (const std::exception& e) { owner::transportError(*o, "projectile send failed: " + describe(e)); }
        owner::projectileAcknowledge(*o, row.serial, sent);
        sent_count += sent ? 1 : 0;
    }
    return sent_count;
}
bool worldIngress(Battle& b, const Value& event) {
    OwnerRef o = prepare(b);
    return o && owner::worldIngress(*o, event, b.clock());
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
    return o ? owner::prewarm(*o, b, now) : 0;
}
std::size_t prewarmPlayers(Battle& b, double now) {
    OwnerRef o = prepare(b);
    return o ? owner::prewarmPlayers(*o, b, now) : 0;
}

// The hidden worker's water observations; sent when the native owner produced a batch.
bool playerEnvironment(Battle& b, double dt, double now) {
    if (dt <= 0.) return false;
    OwnerRef o = prepare(b);
    if (!o) return false;
    auto batch = owner::playerEnvironment(*o, b, dt, link::authority(b));
    if (!batch) return false;
    auto message = v::object();
    message.object["type"] = v::text("player_environment");
    message.object["observations"] = std::move(batch->second);
    message.object["sample_seq"] = v::integer(batch->first);
    const bool sent = link::send(b, message);
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

void frameStarted(Battle& b, double, double) {
    if (b.bots) ++b.bots->frame_callbacks;
}
std::optional<bool> detachedIngress(Battle& b, const Value& message) {
    OwnerRef o = existing(b);
    if (!o) return std::nullopt;
    return owner::detachedIngress(*o, b, message);
}
bool detachedQueue(Battle& b, Actor& vehicle, PyObject* entity, const Value* state) {
    OwnerRef o = existing(b) ? existing(b) : prepare(b);
    return o && owner::detachedQueue(*o, b, vehicle, entity, state, b.clock());
}
std::size_t detachedAdvance(Battle& b, double now) {
    OwnerRef o = existing(b);
    if (!o) return 0;
    auto rows = owner::detachedAdvance(*o, b, now);
    if (!rows) return 0;
    // After the battle result no bot_state carries proposals: bot_state {rows: [], detached_turrets}.
    const std::size_t count = rows->array.size();
    Value message;
    message.kind = Value::Kind::Object;
    message.object["type"].kind = Value::Kind::Text;
    message.object["type"].text = "bot_state";
    message.object["rows"].kind = Value::Kind::Array;
    message.object["detached_turrets"] = std::move(*rows);
    link::send(b, message);
    return count;
}
void prewarmBots(Battle& b, double now) { prewarm(b, now); }
std::size_t playerContacts(Battle& b, const Value& players, double now) {
    OwnerRef o = prepare(b);
    if (!o) return 0;
    const Value results = owner::playerContacts(*o, b, players, now);
    return flushWorld(b) ? sendContactResults(b, *o, results, now) : 0;
}
const Value* currentState(const Battle& b, std::int64_t bot_id) {
    const auto* a = authority(b);
    if (!a) return nullptr;
    const auto found = a->profiles.find(bot_id);
    return found == a->profiles.end() ? nullptr : &found->second;
}

void start(Battle& b) {
    if (!b.bots) b.bots = std::make_unique<BotAuthority>();
    b.bots->started = true;
    Value message = b.start.resolved().kind == Value::Kind::Object ? b.start.resolved() : Value();
    message.kind = Value::Kind::Object;
    if (b.worker) markHumanRamTimeline(b, message);
    dispatchOutgoing(b, roundStart(b, message), true);
}

// The worker Bot world's per-frame foliage, motion and registry step; false without a live owner.
bool frame(Battle& b, double now, bool force) {
    OwnerRef o = existing(b);
    return o ? owner::frame(*o, now, force) : false;
}
}
}
