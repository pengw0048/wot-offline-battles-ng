#pragma once
// The battle world: destructibles, fallen-tree foliage, detached turrets and the ram Bot history.
#include "stock_native_battle.hpp"
#include "stock_native_bot_foliage_snapshot.hpp"
#include "stock_native_detached_turret_runtime.hpp"
#include <deque>

namespace native_battle {
// The canonical Bot bodies each snapshot revision carried, for player ram receipts that name a past revision.
struct RamBotHistory {
    static constexpr std::size_t limit = 512;
    struct Revision {
        std::int64_t time_us = 0;
        std::map<std::int64_t, Value> bots;
    };
    std::map<std::int64_t, Revision> revisions;
    std::deque<std::int64_t> order;                                             // arrival order, oldest first
    std::map<std::int64_t, std::vector<std::pair<std::int64_t, std::int64_t>>> timelines;   // Bot -> (time_us, revision)
};

// Server-accepted landed turrets and the visible client's presentation of them.
struct DetachedTurrets {
    std::map<std::string, stock_native_detached_turret::Row> rows;   // by actor key, e.g. "bot:7"
    std::vector<std::string> order;                                  // acceptance order of rows
    std::set<std::string> geometry;           // rows whose obstacle geometry is installed or not required
    std::map<std::string, double> visual_retry;
    std::map<std::string, std::int64_t> visual_revisions;
};

struct FallenFoliage;

struct World {
    World();
    ~World();
    bool destructibles_disabled = false;
    std::string destructibles_disabled_reason;
    // Stock-facing Python owners this subsystem still drives: the #1513 destructible sensor, the detached-turret
    // entity manager and the visible obstacle registry.
    Object sensor, turret_manager, turret_obstacles;
    std::unique_ptr<FallenFoliage> foliage;
    DetachedTurrets turrets;
    RamBotHistory ram;
};

namespace world {
// Round start: the stock owners and the baked foliage of this map (`config_dir` holds foliage/manifest.json).
void start(Battle& b, Object sensor, Object turret_manager, Object turret_obstacles, const std::string& config_dir);
void retire(Battle& b);

// Frame steps.
void foliage(Battle& b, double now);
void detachedTurrets(Battle& b, double now);
void prewarm(Battle& b, double now);       // countdown on both sides, live on the worker

// Snapshot and event ingress.
bool observeDestructiblesDisabled(Battle& b, const Value& message);
void disableDestructibles(Battle& b, const std::string& error);
bool applyDestructibleState(Battle& b, const Value& events);
bool reconcileDetachedTurrets(Battle& b, const Value& message);
bool rememberRamBotSnapshot(Battle& b, const Value& snapshot);

// The LAN player_destructible_contact (worker) and player_destructible_contact_result (visible) messages.
void destructibleContact(Battle& b, const Value& message);
void destructibleContactResult(Battle& b, const Value& message);

// Remote vehicle death edges.
bool detachTurret(Battle& b, Actor& vehicle, PyObject* entity, const Value* state);
std::int64_t markTurretDetached(Battle& b, PyObject* entity);
std::int64_t ammoBaySpecialHealth(Battle& b, PyObject* entity);
bool detachLateAmmoTurret(Battle& b, Actor& vehicle, PyObject* entity, const Value* state);
// Startup waits in BattleLoading until raw wreck assets settled or the vehicle deadline passed.
bool wreckPrewarmReady(Battle& b, double vehicle_ready_deadline);

// Readers for other subsystems; refreshFallenFoliage keeps accepted fallen trees on their native matrices.
void refreshFallenFoliage(Battle& b, double now, bool force);
// A Bot's canonical body at one receipt: the bracketing revisions interpolated to time_us, with ram_vx/vy/vz.
std::optional<Value> ramBotState(const Battle& b, std::int64_t bot_id, std::int64_t revision, std::int64_t time_us);
// Static and fallen-tree foliage of this map; null when the map has none or the feature is disabled.
const stock_native_bot_foliage::TreeState* trees(const Battle& b);
bool activateFallenTree(Battle& b, std::uint32_t chunk_id, std::uint32_t item_index);
std::map<std::string, std::int64_t> registryCounts(const Battle& b);
}
}
