#pragma once
// Bot authority on the hidden worker: simulation step, manifests, outgoing Bot messages, poses and tracks.
#include "stock_native_battle.hpp"
#include "stock_native_bot_seed.hpp"
#include <array>

namespace stock_native_bot { struct Owner; }

namespace native_battle {
// The round's Bot line-up, derived identically on every process from the server roster.
struct BotAssignments {
    std::map<std::pair<std::int64_t, std::int64_t>, std::string> vehicles;   // (team, slot) -> vehicle type
};

struct BotAuthority {
    // The native simulation owner, created when this client becomes the authority. Callers hold a
    // copy across any stock call that can re-enter and retire it.
    std::shared_ptr<stock_native_bot::Owner> owner;
    Object last_snapshot;   // the latest accepted snapshot message: human input, ram probes, takeover
    // The round start (the pinned BotRuntime.battle_start state).
    bool started = false;
    Value round_id;
    std::optional<std::int64_t> authority_id;
    bool finished = false, manifest_sent = false;
    double control_seconds = 0.;      // the worker's fixed control period
    double refresh_bank = 0.;         // control refresh debt the first physical slice owes
    std::int64_t generation = 0;      // the native simulation generation of the latest manifest
    stock_native_bot::ServerOrders orders;
    // The manifest the native owner installs; null until this authority built one.
    std::shared_ptr<const stock_native_bot::RoundSeed> seed;
    // The frozen unsent manifest of this round and authority (Null when none).
    Value pending_manifest, pending_round;
    std::optional<std::int64_t> pending_authority;
    // Each Bot's static collision profile (mass, collision_shape, ram_profile, vehicle, team).
    std::map<std::int64_t, Value> profiles;
    // Player water sampling clock and sequence; they outlive an owner replaced by an authority change.
    double player_environment_check = 0.;
    std::int64_t player_environment_seq = 0;
    // Worker pose presentation per Bot: the lifecycle it belongs to and the writes already made.
    struct PresentedBot {
        const Actor* vehicle = nullptr;
        std::int64_t engine_id = 0;
        std::uint64_t generation = 0;
        std::optional<std::array<double, 3>> clock;           // time, yaw, and
        std::optional<std::array<double, 6>> sample;          // the presented location and rotation
        std::optional<std::array<double, 11>> pose;           // position, rotation, speed, epoch, alive, siege
        std::optional<std::array<double, 5>> aim;
        std::optional<std::array<double, 9>> projectile;
    };
    std::map<std::int64_t, PresentedBot> presented;
    // One manifest enqueue tenure: generation, round, authority id, authority epoch.
    double next_manifest_retry = 0., manifest_retry_deadline = 0.;
    std::optional<std::array<Value, 4>> manifest_retry_identity;
    // Hidden-worker progress.
    std::uint64_t frame_callbacks = 0, authority_callbacks = 0, generated = 0, enqueued = 0, send_failed = 0, simulation_caps = 0;
    std::size_t bot_count = 0;
    // Fixed-control debt: the worker's PERF line.
    std::uint64_t control_steps = 0, catchup_callbacks = 0, debt_callbacks = 0, astar_budget_exhausted = 0;
    std::int64_t astar_max_pending = 0;
    double max_control_step = 0., control_debt = 0., max_control_debt = 0.;
    // Visible-client Bot belt turn rates measured from presented yaw.
    std::map<std::int64_t, double> yaw_rates;
    // The Bot line-up startup prepares before the round start.
    std::shared_ptr<BotAssignments> assignments;
};

namespace bots {
// The pinned _prepare_bot_vehicle_assignments: false when the roster cannot be filled from this catalogue.
// Creates the BotAuthority when startup calls it first.
bool prepareVehicleAssignments(Battle& b, PyObject* player_descriptor);
const std::map<std::pair<std::int64_t, std::int64_t>, std::string>& vehicleAssignments(const Battle& b);
// Runs the round start, materializes the authority manifest's Bots and sends its opening messages.
void start(Battle& b);
// Retirement closes the native owner.
void retire(Battle& b);
// The pinned BotRuntime.battle_start: the outgoing messages (the authority manifest) of one start.
std::vector<Value> roundStart(Battle& b, const Value& message);
// BotRuntime.pending_manifest / discard_pending_manifest / mark_manifest_enqueued.
const Value* pendingManifest(const Battle& b);
bool discardPendingManifest(Battle& b);
bool markManifestEnqueued(Battle& b, const Value& message);
// The worker's native owner drives this round's Bots (the pinned native_bot_runtime.enabled).
bool enabled(const Battle& b);
bool isAuthority(const Battle& b);

// Per frame while live: the Bot simulation step and its outgoing messages.
void update(Battle& b, double now, double dt);
// Applies the accepted authority pose set; returns the presented Bot count.
std::size_t presentPoses(Battle& b, double now);
// Hidden worker: drains the transport; false when the round ended inside it.
bool drainWorkerTransport(Battle& b);
void retryManifest(Battle& b, double now);
void workerProbe(Battle& b);
// Remote Bot belts and engine mode for a vehicle presented outside the native replica, from its
// presented yaw; false when the 20 Hz feed is not due or the stock update refused it.
bool tracks(Battle& b, Actor& vehicle, double yaw, double now);
// The same feed with a caller-measured turn rate (rad/s).
bool tracksWithTurn(Battle& b, Actor& vehicle, double turn, double now);
// Measures a presented yaw's turn rate for the belt feed.
double rememberTrackTurn(Actor& vehicle, double yaw, double now);

// Authority changes from a roster or snapshot.
bool reconcileAuthority(Battle& b, PyObject* player_id);
// Server-owned combat state for the authority simulation.
void snapshot(Battle& b, PyObject* message);
// Projectile and world ingress the native owner consumes.
bool projectileIngress(Battle& b, PyObject* message, double now);
bool projectileObserve(Battle& b, PyObject* message, double now);
bool projectileEvent(Battle& b, PyObject* event, double now);
std::size_t projectileAdvance(Battle& b, double now, bool force_progress);
bool worldIngress(Battle& b, PyObject* event, double now);
bool acknowledgeLaunch(Battle& b, std::int64_t bot_id, std::int64_t sequence);
bool acknowledgeRam(Battle& b, std::int64_t player_id, std::int64_t sequence);
// Worker prebattle prewarm of the native world receipts; returns the prewarmed count.
std::size_t prewarm(Battle& b, double now);
std::size_t prewarmPlayers(Battle& b, double now);
// The worker's native water observations (the pinned _publish_player_environment).
bool playerEnvironment(Battle& b, double dt, double now);
// Value-typed entry points the other subsystems call.
// The worker's render callback started (progress counter).
void frameStarted(Battle& b, double rule_dt, double raw_dt);
void applySnapshot(Battle& b, const Value& snapshot);
void reconcileAuthority(Battle& b, const Value& player_id);
bool worldIngress(Battle& b, const Value& event);
// nullopt when no native owner handles detached turrets yet.
std::optional<bool> detachedIngress(Battle& b, const Value& message);
bool detachedQueue(Battle& b, Actor& vehicle, PyObject* entity, const Value* state);
// Advances native detached turrets and sends the terminal tail when due; returns the sent row count.
std::size_t detachedAdvance(Battle& b, double now);
void prewarmBots(Battle& b, double now);
// The worker Bot world's per-frame foliage, motion and registry step; false without a live owner.
bool frame(Battle& b, double now, bool force);
// The round's Bot skill context (tactics, skill mode, lineup pins, round) the visible client's peer drive reads.
Value peerSkillContext(const Battle& b);
// Resolves the players' destructible contacts and sends each verdict; returns the sent count.
std::size_t playerContacts(Battle& b, const Value& players, double now);
// A Bot's static collision profile, or null before its manifest.
const Value* currentState(const Battle& b, std::int64_t bot_id);
// Every live record entered the native space (the worker acquires draw-off after this).
bool readyForDrawOff(const Battle& b);
// One worker PERF row: fixed-control debt.
p::Ref controlSnapshot(const Battle& b);
}

// The hidden worker's process session (the pinned authority_worker.WorkerSession).
namespace worker {
void install(PyObject* config, PyObject* lobby_ready, PyObject* bigworld);
bool start();
void stop(bool restore_account);
void onEvent(const std::string& kind, PyObject* message);
// worker_session_install/start/stop for the stock bootstrap entry.
PyMethodDef* methods();
}

// Defined by the startup subsystem; the worker session drives a round through them.
namespace startup {
// Creates and installs the round's Battle from the worker config; false with `error` when rejected.
bool startRound(PyObject* config, PyObject* message, PyObject* client, std::string& error);
// Stops the current round's Battle (the pinned BattleRuntime.stop(show_login=False, restore_account=...)).
void stopRound(bool restore_account);
// One LAN message for the current round: snapshot, events, battle_live, bot_observation, player_destructible_contact.
void receive(Battle& b, const std::string& kind, PyObject* message);
}
}
