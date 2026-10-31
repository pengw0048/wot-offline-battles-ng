#pragma once
// Vehicle table: replica intake, stock entity lifecycle and every per-vehicle presentation edge.
#include "stock_native_battle.hpp"

namespace native_battle {
// The local damage report a server acknowledgement retires.
struct LocalCriticalReport {
    std::int64_t server_revision = 0, base_revision = 0, next_seq = 0;
    bool owned = false;
    std::optional<Value> report;
};

struct Vehicles {
    // Bots wait here until their own createEntity callback; humans are created on arrival.
    std::map<std::string, Value> pending_bot_creates;
    std::vector<std::string> pending_order;
    std::optional<std::int64_t> last_bot_team;
    bool bots_ready_reported = false;
    std::optional<double> bot_create_started, bot_create_finished;
    std::uint64_t revision = 0;     // bumped on every create/remove; consumers cache against it
    LocalCriticalReport local_critical;
};

namespace vehicles {
// Replica intake: battle_start/roster manifests and authority snapshots.
void receiveManifest(Battle& b, const Value& message);
void receiveSnapshot(Battle& b, const Value& message, double now);
// Per frame: replica interpolation, pending creates and readiness, tombstones.
void advance(Battle& b, double now);
void flushPendingBotCreate(Battle& b, double now);
void flushPendingEntities(Battle& b, double now);

// Queries every subsystem may use.
bool alive(const Battle& b, const Actor& vehicle, PyObject* entity);   // the pinned _record_alive
p::Ref entity(const Battle& b, const Actor& vehicle);                  // stock entity or None
bool spotted(const Actor& vehicle);                                    // marker/team-knowledge gate
Point position(const Battle& b, const Actor& vehicle);

// Presentation edges other subsystems drive.
bool setSpotVisibility(Battle& b, Actor& vehicle, bool visible, std::optional<bool> marker = {});
void presentDead(Battle& b, Actor& vehicle, bool remembered);
struct HealthEdge {
    std::int64_t attacker = 0;
    std::optional<std::int64_t> reason, attack_reason;
    bool force = false, suppress_combat = false, suppress_postmortem_killer = false;
};
void applyHealth(Battle& b, Actor& vehicle, Value& state, const HealthEdge& edge);
bool applyCritical(Battle& b, Actor& vehicle, const Value& payload, const Value* authority = nullptr);
// Re-applies the vehicle's current presented state (siege, statistics, stun, critical, health).
bool materialize(Battle& b, Actor& vehicle);
}
}
