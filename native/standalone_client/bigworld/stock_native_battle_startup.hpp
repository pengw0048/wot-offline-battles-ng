#pragma once
// Round startup and teardown, LAN session entry points, the engine binding and server bridge,
// garage loadout, descriptors, spawn planning, replay recording and frame diagnostics.
#include "stock_native_battle.hpp"
#include <array>

namespace native_battle {
// The spawn contract a validated navigation graph carries for this map.
struct SpawnPlan {
    std::string map;
    std::array<std::array<std::array<double, 4>, 15>, 2> formations{};   // per team: x, y, z, yaw
    std::array<std::array<double, 2>, 2> bases{};                          // per team: x, z
    std::optional<std::array<double, 2>> base_radii;
};

// Everything the garage knew about the player's own vehicle, frozen before the lobby Account retires.
struct GarageLoadout {
    bool known = false;                                   // a garage item (or replay row) existed
    std::map<std::int64_t, std::int64_t> shells;          // shell compact descr -> count; empty when !shells_known
    bool shells_known = false;
    std::vector<std::int64_t> shell_order;
    std::optional<std::vector<std::int64_t>> equipment_ids;
    Object equipments, battle_boosters, crew;             // stock item tuples (loadout law inputs)
    std::int64_t crew_group = 0;
    Object camouflage_id;                                 // int or None
    Object outfit;                                        // compact outfit string
    Object fitting_descr;                                 // the mounted compact descriptor, or None
    std::string fitting_name;
    std::vector<std::int64_t> personal_mission_ids;
};

enum class Stage { None, CreatingMap, LoadingEntities };

struct Startup {
    Object frame_callback, ammo_callback;
    Object arena_type;              // the stock ArenaType of this round's map
    std::map<std::string, Object> descriptors;   // one prepared descriptor per vehicle type (or donated compact descr)
    std::vector<std::string> prepared_names;     // types proven loadable this round, offered as substitutes
    std::set<std::string> unusable_reported;
    Stage stage = Stage::None;
    double deadline = 0., vehicle_ready_deadline = 0.;
    bool map_create_attempted = false, lobby_retire_started = false, client_ready_received = false;
    bool battle_ready_sent = false, team_chat_initialized = false, round_finished_notified = false;
    std::optional<std::array<double, 4>> arena_bounds;   // min x, min z, max x, max z
    Object navigation_graph;                             // prebaked graph dict (consumed by navigation and Bots)
    std::optional<SpawnPlan> spawn;
    std::map<std::pair<std::int64_t, std::int64_t>, std::int64_t> spawn_slots;   // (team, slot) -> physical slot
    std::map<std::pair<std::int64_t, std::int64_t>, std::pair<Point, double>> spawn_cache;
    GarageLoadout garage;
    bool garage_frozen = false;
    Value effective_params;                       // the server-accepted local effective parameters (canonical)
    Value last_snapshot;                          // the latest accepted authority snapshot of this round
    bool has_sixth_sense = false, has_expert = false, has_deadeye = false;
    Object local_descriptor;
    std::int64_t local_vehicle_id = 0;
    Object on_local_leave;                         // lan_session callback
    // Stock patches active for this round; each is restored only while still ours.
    Object gui_loader_type, gui_loading_original, gui_page_original, gui_loading_wrapper, gui_page_wrapper;
    // Teardown: a lobby restore waits one native callback boundary.
    bool restore_pending = false;
    Object restore_callback;
    std::vector<Object> retired_owners;
    // Map space visibility: (space id, selected bit) once the gameplay mapping is latched.
    std::optional<std::pair<std::int64_t, std::int64_t>> space_visibility;
    double next_space_visibility_check = 0.;
    // Team chat and commands: the stock radio adapter, and per-issuer accepted sequences.
    Object radio;
    std::map<std::pair<std::string, std::int64_t>, std::int64_t> chat_seen, command_seen;
    std::set<std::pair<std::string, std::int64_t>> command_terminals;
    // Replay recording of the local presentation.
    Object recorder;
};

// Bounded per-window frame timing, printed as one PERF line per window.
struct Diagnostics {
    bool enabled = false;
    double window_start = 0., window_seconds = 10., worst_frame = 0., total_frame = 0.;
    std::uint64_t frames = 0;
    std::map<std::string, double> stages;
};

namespace startup {
// ---- LAN session entry points (lan_session.py / authority_worker.py battle dispatch) ----
// BattleRuntime.start(config, message, lan_client, on_local_leave); false when it did not start.
bool start(PyObject* shell, PyObject* runtime_modules, PyObject* config, PyObject* message, PyObject* client, PyObject* on_local_leave);
void stop(Battle& b, bool restore_account);
void snapshot(Battle& b, PyObject* message);
void battleLive(Battle& b, const Value& message);
bool roster(Battle& b, const Value& message);
bool teamChat(Battle& b, const Value& message);
bool teamChatAck(Battle& b, const Value& message);
bool teamCommand(Battle& b, const Value& message);
bool teamCommandAck(Battle& b, const Value& message);
bool teamCommandTerminal(Battle& b, const Value& message);
void replayLocal(Battle& b, PyObject* message);
// The shell's state string: idle, creating_map, loading_entities, running, failed, stopped, leaving.
std::string stateName(const Battle* b);

// ---- frame-loop helpers (the frame body itself is owned by the integration) ----
void frame(Battle& b);              // defined by the frame loop
void scheduleFrame(Battle& b, double delay);
void beginBattle(Battle& b);
bool prebattleTransitionReady(Battle& b, double now);
double prebattleSeconds(const Battle& b);
double battleSeconds(const Battle& b);
void resetPrebattleVisuals(Battle& b);
void showPrebattleCrosshair(Battle& b);
void startTeamChatWhenRosterReady(Battle& b);
void maybeSendBattleReady(Battle& b);
void maintainSpaceVisibility(Battle& b, double now);
bool observeDestructiblesDisabled(Battle& b, const Value& message);

// ---- garage, descriptors, spawn ----
const GarageLoadout& garage(Battle& b);
p::Ref localBattleDescriptor(Battle& b, const std::string& vehicle_name);
p::Ref descriptorFor(Battle& b, const std::string& vehicle_name);      // the pinned _resolve_descriptor
p::Ref playerDescriptor(Battle& b, const Value& state);                // the pinned _resolve_player_descriptor
std::pair<Point, double> formationPose(Battle& b, std::int64_t team, std::int64_t slot);
std::pair<Point, double> worldPose(Battle& b, const Value& state);     // the pinned _state_world_pose
std::optional<double> groundY(Battle& b, double x, double z, double hint, bool allow_wide);
std::optional<std::int64_t> outfitSeason(Battle& b);
Value localState(Battle& b);
std::optional<std::array<double, 4>> arenaBounds(const Battle& b);
PyObject* navigationGraph(const Battle& b);   // borrowed; None when the map has no baked graph
std::int64_t skillCount(Battle& b, const char* skill);

// ---- engine binding (the pinned BigWorldVehicleBinding) ----
namespace binding {
p::Ref properties(Battle& b, PyObject* compact_descr, std::int64_t team, const std::string& name);
p::Ref createVehicle(Battle& b, PyObject* properties, PyObject* position, PyObject* rotation);
void arenaVehicleAdded(Battle& b, std::int64_t entity_id, PyObject* properties, bool team_killer,
                       PyObject* personal_mission_ids = nullptr, std::int64_t crew_group = 0);
void startVehicleVisual(Battle& b, std::int64_t entity_id, bool immediate);
void stopVehicleVisual(Battle& b, std::int64_t entity_id, bool is_player);
void startVehicleMarker(Battle& b, std::int64_t entity_id);
void stopVehicleMarker(Battle& b, std::int64_t entity_id);
void startVehicleMinimap(Battle& b, std::int64_t entity_id);
void stopVehicleMinimap(Battle& b, std::int64_t entity_id);
void arenaVehicleKilled(Battle& b, std::int64_t entity_id, std::int64_t attacker_id, std::int64_t reason);
void arenaVehicleStatistics(Battle& b, std::int64_t entity_id, std::int64_t frags);
void arenaTeamKiller(Battle& b, std::int64_t entity_id);
void avatarSelectVehicle(Battle& b, std::int64_t entity_id);
void avatarVehicleEntered(Battle& b);
void avatarClientReady(Battle& b);
std::uint64_t avatarAuxPhysics(Battle& b, const std::array<double, 6>& values, std::int64_t gear);
void avatarReady(Battle& b);
// "prebattle" | "battle" | "afterbattle"; the outcome only for afterbattle.
void arenaPeriod(Battle& b, const char* period, double duration, std::optional<std::pair<std::int64_t, std::int64_t>> outcome = {});
void setVehiclePose(Battle& b, std::int64_t entity_id, PyObject* position, PyObject* rotation, PyObject* relax_time, PyObject* now);
bool settleVehicleMotion(Battle& b, std::int64_t entity_id, PyObject* now);
void updateVehicleAim(Battle& b, std::int64_t entity_id, double hull_yaw, double aim_yaw, double gun_pitch);
bool updateVehicleSiegeState(Battle& b, std::int64_t entity_id, std::int64_t state, double time_to_next_mode);
void destroyEntity(Battle& b, std::int64_t entity_id);
bool isVehicleReady(Battle& b, std::int64_t entity_id);
}

// ---- the stock-facing Avatar/Vehicle mailbox (the pinned AvatarServerBridge) ----
namespace server {
// The Python object compat attaches as the Avatar's server; null before entities exist.
PyObject* object(Battle& b);
std::int64_t vehicleId(Battle& b);
// ("pending" | "started" | "completed" | "failed", message)
std::pair<std::string, std::string> vehicleEnterStatus(Battle& b, std::int64_t vehicle_id);
void forgetVehicleEnter(Battle& b, std::int64_t vehicle_id);
}

// ---- replay recording and diagnostics ----
void ensureRecording(Battle& b);
void recordLocal(Battle& b);
void finishRecording(Battle& b, const char* reason);
void diagnosticsFrame(Battle& b, double entry_wall, double frame_seconds, const std::map<std::string, double>& stages);
}
}
