#pragma once
// The player's own vehicle: drive, physics, suspension, Siege, contacts, drowning, overturn, the local critical
// tick, engine audio and RPM, body swinging, camera and the LAN input controls.
#include "stock_native_battle.hpp"

namespace native_battle {
struct LocalVehicle {
    // Pose published after every drive slice for the other subsystems.
    Point position{};
    Object descriptor;      // the mounted stock vehicle descriptor
    double yaw = 0., pitch = 0., roll = 0., speed = 0., turn_speed = 0.;
    bool airborne = false;

    // Native owners: drive and world scene, survival warnings, self presentation, local critical tracks.
    std::shared_ptr<void> drive, survival, presentation, critical;

    // Stock Math providers the camera, gun rotator, markers and HUD read.
    Object model, matrix, pose_matrix, stabilised_matrix, steady_rotation_matrix, stabilised_snapshot;
    Object siege_body_matrix, siege_stabilised_matrix, siege_ground_matrix, siege_flat_body_matrix;
    Object siege_aim_matrix, siege_aim_world_matrix;
    double siege_aim_pitch = 0., siege_aim_center_z = 0.;
    std::int64_t siege_edge_reports = 0;

    // Exact physics and suspension parameters derived from the mounted descriptor.
    Object physics, suspension_params;
    bool suspension_disabled = false;
    Point air_lateral{};

    // Body swinging, engine audio and the float links #1513 data providers read.
    Object swinging_animator, swinging_restore, swinging_probe, placing_compensation;
    std::int64_t swinging_move_flags = 0, swinging_event_seq = 0, swinging_events_reported = 0;
    Object engine_state, engine_mode, speed_link, rotation_speed_link;

    // RPM presentation.
    double next_rpm_time = 0., engine_audio_report_at = 0.;
    Object presented_rpm;

    // Camera motion carried across the arcade camera rebinding.
    Point camera_velocity{};

    // The stock Avatar ram-contact callback the native contact observer wraps, and the class it was installed on.
    Object ram_contact_hook, ram_contact_type;
    std::optional<std::pair<bool, std::string>> suspension_report;
    bool siege_hud_seeded = false;

    // LAN input controls; the drive owns them once the player driver exists.
    struct Controls {
        double forward = 0., turn = 0., aim_yaw = 0., gun_pitch = 0., aim_pitch = 0.;
        std::optional<Point> aim_point;
        bool handbrake = false;
    } controls;
    bool service_brake = false;
};

namespace local {
// Per frame on the visible client while live.
void drive(Battle& b, double dt);
void tickCritical(Battle& b, double dt);
void tickDrowning(Battle& b, double dt, double now);
void tickOverturn(Battle& b, double dt, double now);
// A Siege state change of the local vehicle reached presentation.
void siegeChanged(Battle& b, PyObject* entity, bool raised);
// The local vehicle died.
void died(Battle& b);

// Lifecycle: the native world owner once the space exists, the player owners once the entity is ready.
void prepare(Battle& b);
void retire(Battle& b);
// Local presentation lifecycle: the copied pose before stock local-vehicle startup (vehicle_onEnterWorld), its
// attachment to the model and hydraulic providers once the native lifecycle completed, and the arcade camera.
void preparePresentation(Battle& b, PyObject* entity);
void attachPresentation(Battle& b);
void bindArcadeCamera(Battle& b);
// Re-points the arcade/sniper providers at the copied pose after a stock control-mode change.
void bindControlSources(Battle& b, PyObject* handler, PyObject* mode);
// Wraps PlayerAvatar.handleVehicleCollidedVehicle so native contact proofs observe every stock callback.
bool installRamContactHook(Battle& b);
void restoreRamContactHook(Battle& b);
// Seeds the stock Siege indicator cache once the client is ready.
bool seedSiegeHud(Battle& b, Actor& local);
// Advances the visible world owner's scene and destruction ledger (the foliage/frame chain).
// The exact suspension parameters for the mounted descriptor, or None when detailed suspension is excluded.
PyObject* ensureSuspensionParams(Battle& b, PyObject* descriptor);
// Accepted canonical destruction for the visible world owner.
bool ingress(Battle& b, const Value& event, double now);
// Snapshot acknowledgements of the local ram and destructible contact receipts.
bool acknowledgeContacts(Battle& b, const Value& snapshot, bool destructible);
// Stock vehicle-pair contact callbacks observed after the native Avatar hook.
bool observeContact(Battle& b, PyObject* first, PyObject* second, PyObject* hit, double contact_time);
bool observeFeedback(Battle& b, PyObject* first, PyObject* second, double contact_time);

// Warning levels (drowning, overturn): 0 safe, 1 caution, 2 danger.
std::array<int, 2> warningLevels(const Battle& b);

// Local critical lineage: the owner-CAS track state and its pending damage report.
// Reconciles one authority revision; null when the vehicle is not this client's own.
std::optional<bool> reconcileCritical(Battle& b, Actor& vehicle, const Value& source, Value* canonical);
// The pending local damage report (Null when none), for the input sender's track repair.
const Value& damageReport(const Battle& b);

// LAN input controls the stock Avatar forwards: the original #1513 control edges.
bool control(Battle& b, const std::string& kind, PyObject* payload);
bool alignAim(Battle& b, double turret_yaw, double gun_pitch);
bool sendCurrent(Battle& b, std::optional<bool> siege_enabled = {});
bool changeVehicleSetting(Battle& b, int code, PyObject* value);
// The team command/chat requests the stock radial menu and chat send through the input sender.
bool sendTeamCommand(Battle& b, PyObject* request);
bool sendTeamChat(Battle& b, PyObject* request);
bool rejectNativeShotWait(Battle& b);

// The float links #1513 VehicleSpeedProvider and rotation readers poll.
double vehicleSpeed(const Battle& b);
double vehicleRotationSpeed(const Battle& b);
}
}
