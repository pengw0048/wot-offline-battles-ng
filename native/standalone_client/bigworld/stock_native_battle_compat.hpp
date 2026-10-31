#pragma once
// Native replacements for the battle-time stock hooks compat.py installs on #1513 classes.
// The lobby-wide Python install (account, login, garage) stays Python; every hook a battle
// reaches is replaced once by a native callable that takes its battle branch while a configured
// Battle is current and otherwise calls the stock original.
#include "stock_native_battle.hpp"

namespace native_battle {
// Per-round stock-hook state (compat.py configure_battle .. deactivate_map).
struct Compat {
    bool active = false;
    std::int64_t player_vehicle_id = 0, postmortem_vehicle_id = 0;
    std::map<PyObject*, Object> marker_plugins;   // live VehicleMarkerPlugin instances by identity
    Object target_lock_candidate, control_mode_listener, network_client, gui_type, bonus_type;
    bool target_focus_clear_attempted = false, strategic_camera_failure_reported = false, shell_tooltip_warning_logged = false;
    std::string player_name = "OfflinePlayer";
    std::int64_t player_team = 1, arena_type_id = 0;
    std::optional<double> server_time_origin, clock_origin;
};

namespace compat {
// Objects scoped to one stock call; the native Vehicle/Compound attribute hooks compare against them.
struct Scopes {
    PyObject* entering_vehicle = nullptr;      // inside PlayerAvatar.vehicle_onEnterWorld
    PyObject* starting_visual = nullptr;       // inside Vehicle.startVisual
    PyObject* starting_wg_physics = nullptr;   // inside Vehicle.__startWGPhysics
    PyObject* syncing_gun_angles = nullptr;    // inside Vehicle.set_gunAnglesPacked
    PyObject* syncing_aux_physics = nullptr;   // inside PlayerAvatar.__onSetOwnVehicleAuxPhysicsData
    PyObject* refreshing_models = nullptr;     // inside CompoundAppearance.__onModelsRefresh
    PyObject* target_lock_input_avatar = nullptr;
    bool target_lock_input_pending = false;
};
const Scopes& scopes();
// Stock code objects whose direct calls the scoped attribute hooks recognise.
enum class Code {
    AvatarVehicleEnter, AvatarStartVehicleVisual, VehicleStartVisual, VehicleStartWgPhysics, VehicleSetGunAngles,
    AvatarAuxPhysics, GunRotatorStabilised, GunRotatorPredictLockedTarget, ProjectileSegmentMayHit,
    CameraAccelerationUpdate, ArcadeOscillatorAcceleration, SniperOscillatorAcceleration, CustomEffectUpdate,
    CrashedTrackSetupAssembler, CrashedTrackModelLoaded, CompoundModelsRefresh, ArcadeHandleKeyEvent, SniperHandleKeyEvent,
    Count
};
PyObject* code(Code which);
// Whether a configured battle is current (compat.py `_battle_active`).
bool active();

// Installs every battle hook into the stock classes once; the lobby-wide Python install must have run.
bool install(Battle& b);
// Restores the stock originals; called when the process retires the compatibility layer.
void uninstall(Battle& b);

// configure_battle: enables the normal battle UI/input path for the next Avatar and installs the hooks.
void configure(Battle& b, PyObject* gui_type, PyObject* bonus_type, PyObject* player_name, PyObject* player_team,
               PyObject* arena_type_id);
// deactivate_map: leaves the battle path (hooks stay installed and fall through to stock).
void deactivate(Battle& b);
void activateMap(Battle& b);

// The stock marker plugin's cached player identity.
bool synchroniseMarkerIdentity(Battle& b, std::int64_t expected_vehicle_id);
bool assertMarkerIdentity(Battle& b, std::int64_t expected_vehicle_id);
bool assertMarkerDamageType(Battle& b, PyObject* avatar, std::int64_t expected_vehicle_id);

void setNetworkClient(Battle& b, PyObject* client);
void setControlModeListener(Battle& b, PyObject* listener);

// Target lock and the occlusion-checked cursor vehicle.
bool setTargetLockCandidate(Battle& b, PyObject* vehicle);
bool clearTargetFocus(Battle& b);
bool releaseTargetLock(Battle& b, PyObject* avatar, std::int64_t vehicle_id);
// The team-command reader's target: the live, spotted cursor vehicle (never a lock target), or None.
p::Ref teamCommandTarget(Battle& b);

// Postmortem attachment and the delayed death camera.
std::int64_t setPostmortemVehicle(Battle& b, std::int64_t vehicle_id);
std::int64_t clearPostmortemVehicle(Battle& b);
bool clearPostmortemKiller(Battle& b, PyObject* avatar);

// Every stock pose provider of the player's Vehicle bound to (or restored from) the live overlay.
bool bindVehiclePoseSources(Battle& b, PyObject* avatar, PyObject* vehicle);
bool restoreVehiclePoseSources(Battle& b, PyObject* avatar, PyObject* vehicle, PyObject* native_matrix, PyObject* native_stabilised);
// A native Vehicle member read past the overlay hooks.
p::Ref nativeVehicleAttribute(PyObject* vehicle, const char* name);
// The deferred Avatar server proxy binds its concrete bridge and replays queued requests.
void attachAvatarServer(Battle& b, PyObject* avatar, PyObject* server);
}
}
