#pragma once
// The battle world: destructibles, fallen-tree foliage, detached turrets and the ram Bot history.
#include "stock_native_battle.hpp"
#include "stock_native_bot_foliage_snapshot.hpp"
#include "stock_native_bot_turret_obstacles.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_detached_turret_runtime.hpp"
#include <deque>
#include <functional>

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
    // Visible obstacle geometry of accepted body rows (the worker's Bot runtime owns its own).
    stock_native_bot_turret::Obstacles obstacles;
};

struct FallenFoliage;
struct DestructibleCatalog;
struct WorldSpace;
struct TurretPresentation;

struct World {
    World();
    ~World();
    bool destructibles_disabled = false;
    std::string destructibles_disabled_reason;
    // This map's baked destructible catalog; null when the map has none or interactions are disabled.
    std::unique_ptr<DestructibleCatalog> catalog;
    // The battle space's single native destructible owner: registry, scene, backend, events and query pipeline.
    std::shared_ptr<WorldSpace> space;
    std::unique_ptr<FallenFoliage> foliage;
    DetachedTurrets turrets;
    std::shared_ptr<TurretPresentation> presentation;   // visible client only, created on first use
    RamBotHistory ram;
};

namespace world {
using SceneAccess = stock_native_client_drive::SceneAccess;
using Hit = stock_native_world::Hit;
using QueryStatus = stock_native_world::QueryStatus;
using Surface = stock_native_world::Surface;
using TurretSweep = stock_native_world::TurretSweep;
using TreeIdentity = stock_native_world::TreeIdentity;

// Round start: this map's baked foliage and destructible catalog (`config_dir` holds foliage/ and destructibles/).
void start(Battle& b, const std::string& config_dir);
// After the stock map creator promoted the Avatar: the native owner of b.engine.avatar.spaceID, with every catalog
// identity staged for live proof. Startup then applies the start message's "destructibles" via applyDestructibleState.
void openSpace(Battle& b);
void retire(Battle& b);

// The shared native destructible space. Local drive, Bot runtime, projectiles, queries and foliage use this
// instead of creating their own registry/scene/backend/events; empty before openSpace or after retire.
SceneAccess sceneAccess(Battle& b);
// The worker Bot runtime's detached-turret sweep veto for native destruction (default: every sweep is allowed).
void setTurretSweep(Battle& b, std::function<bool(const TurretSweep&)> veto);

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
// Native destruction publication: ordered (sequence, canonical event) rows for the LAN client, then its ack.
p::Ref packDestructibleEvents(Battle& b);
bool acknowledgeDestructibleEvents(Battle& b, std::uint64_t sequence);

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

// Collision queries that replace the destructible sensor's filters. Each hides accepted broken original skins and
// recasts compiled skins inside their accepted owner (ground_collision_filter + collide_motion_segment), or keeps
// only non-destructible materials (prepare_navigation_collision_filter). Failed means no current space.
QueryStatus ground(Battle& b, const Point& start, const Point& end, Hit& out);
QueryStatus horizontal(Battle& b, const Point& start, const Point& end, Hit& out);
QueryStatus navigation(Battle& b, const Point& start, const Point& end, Hit& out);
// The planning-ray law alone: false for original destructible materials 71..85.
bool keepNavigationSurface(const Surface& surface);
// Registers exact native tree identities in the occupied chunk, its eight neighbours and the forward chunk.
std::size_t prewarmTrees(Battle& b, const Point& position, double yaw);
// The trusted visible client's tree wire: tree, pending, other or hard (trusted_tree_identity_status_1513).
TreeIdentity trustedTreeStatus(Battle& b, std::uint32_t chunk_id, std::uint32_t item_index);
bool isolated(const Battle& b, std::uint32_t chunk_id, std::uint32_t item_index);

// Readers for other subsystems; refreshFallenFoliage keeps accepted fallen trees on their native matrices.
void refreshFallenFoliage(Battle& b, double now, bool force);
// A Bot's canonical body at one receipt: the bracketing revisions interpolated to time_us, with ram_vx/vy/vz.
std::optional<Value> ramBotState(const Battle& b, std::int64_t bot_id, std::int64_t revision, std::int64_t time_us);
// Static and fallen-tree foliage of this map; null when the map has none or the feature is disabled.
const stock_native_bot_foliage::TreeState* trees(const Battle& b);
bool activateFallenTree(Battle& b, std::uint32_t chunk_id, std::uint32_t item_index);
// Accepted visible detached-turret obstacles (shell/hull/navigation/target queries); null on the worker.
const stock_native_bot_turret::Obstacles* turretObstacles(const Battle& b);
std::map<std::string, std::int64_t> registryCounts(const Battle& b);
}
}
