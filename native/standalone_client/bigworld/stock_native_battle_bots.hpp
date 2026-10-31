#pragma once
// Bot authority on the hidden worker: simulation step, manifests, outgoing Bot messages, poses and tracks.
#include "stock_native_battle.hpp"
#include <array>

namespace native_battle {
struct BotAuthority {
    // The Python BotRuntime still builds the round manifest and holds the per-Bot rows the native
    // owner's snapshot bridges read; everything around it is here.
    Object runtime;
    Object native;          // offline_bot_runtime_native
    Object owner;           // the native simulation owner, created when this client becomes the authority
    Object last_snapshot;   // the latest accepted snapshot message, for an authority takeover
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
};

namespace bots {
// Startup installs the round's BotRuntime; retirement closes the native owner.
void install(Battle& b, PyObject* runtime);
void retire(Battle& b);
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
// Measures a presented yaw's turn rate for the belt feed.
double rememberTrackTurn(Actor& vehicle, double yaw, double now);

// The round roster (battle_start/roster) and authority changes.
void battleStart(Battle& b, PyObject* message);
bool reconcileAuthority(Battle& b, PyObject* player_id);
// Server-owned combat state for the authority simulation.
void snapshot(Battle& b, PyObject* message);
// Projectile and world ingress the native owner consumes.
bool projectileIngress(Battle& b, PyObject* message, double now);
bool projectileObserve(Battle& b, PyObject* message, double now);
bool projectileEvent(Battle& b, PyObject* event, double now);
std::size_t projectileAdvance(Battle& b, double now, bool force_progress);
bool worldIngress(Battle& b, PyObject* event, double now);
bool detachedQueue(Battle& b, Actor& vehicle, PyObject* entity, PyObject* state, double now);
bool detachedIngress(Battle& b, PyObject* message, double now);
bool acknowledgeLaunch(Battle& b, std::int64_t bot_id, std::int64_t sequence);
bool acknowledgeRam(Battle& b, std::int64_t player_id, std::int64_t sequence);
// Worker prebattle prewarm of the native world receipts; returns the prewarmed count.
std::size_t prewarm(Battle& b, double now);
std::size_t prewarmPlayers(Battle& b, double now);
// The worker's native water observations (the pinned _publish_player_environment).
bool playerEnvironment(Battle& b, double dt, double now);
// Every live record entered the native space (the worker acquires draw-off after this).
bool readyForDrawOff(const Battle& b);
// One worker PERF row: fixed-control debt and Bot diagnostics.
p::Ref controlSnapshot(const Battle& b);
p::Ref diagnosticTotals(const Battle& b);
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
