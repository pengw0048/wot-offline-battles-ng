#pragma once
// Remote vehicle presentation: the remote factory, replica interpolation, and pose/aim/track feeds of stock entities.
#include "stock_native_battle.hpp"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace native_battle {
struct RemoteVehicle;
struct ReplicaBinding;

// Belt inputs derived once per descriptor from #1513 physics.
struct TrackParams {
    double center = 1.5, speed_cap = 0.;
    bool around_center = true;
};

// One copied-physics pose published to stock consumers of a Vehicle (replaces compat `_vehicle_property_overlays`).
struct Overlay {
    bool pose_active = false;
    double speed = 0., turn_speed = 0.;
    std::map<std::string, Object> fields;   // position, yaw, matrix, steady_rotation_matrix, stabilised_matrix,
                                            // speed, turn_speed, speedInfo, velocity, acceleration, health,
                                            // isCrewActive, _collision_filter
    PyObject* field(const char* name) const { auto f = fields.find(name); return f == fields.end() ? nullptr : f->second.get(); }
};

struct MotionProbe {
    std::string key;
    std::uint64_t frames = 0, held = 0, moving_held = 0;
    double max_gap_ms = 0., source_age_ms = 0.;
    std::optional<double> source_time_us, presentation_time_us, ideal_delay_us, actual_delay_us;
    std::optional<std::int64_t> source_revision;
    Pose source{}, presented{};
};

struct Remote {
    Remote();
    ~Remote();
    bool interpolate_motion = false, authority_geometry = false;
    std::map<std::int64_t, std::unique_ptr<RemoteVehicle>> vehicles;
    std::set<std::int64_t> failed_creates;
    std::map<PyObject*, Object> descriptors;       // prepared descriptors pinned for the round
    std::map<PyObject*, Object> hit_testers;       // worker: BSP models this owner loaded, released at teardown
    std::set<std::string> reported_capabilities;
    // Replica.
    std::map<std::string, std::unique_ptr<ReplicaBinding>> bindings;
    std::map<std::string, Value> pending_creates;   // create events retried until the lifecycle presents them
    std::map<PyObject*, std::pair<Object, std::shared_ptr<const Value>>> static_versions;
    std::optional<double> last_advance;
    std::uint64_t bindings_revision = 0, received = 0, lifecycle_events = 0;
};

namespace remote {
// Process shutdown; release global presentation handles while Python is alive.
void finaliseScriptOwners();
// Creates the Remote subsystem for this round (visible client: interpolation off, worker: authority geometry).
void install(Battle& b, bool interpolate_motion, bool authority_geometry);

// Accepted authority messages into the replica (cs::State) and the vehicle lifecycle.
// `message` is the LAN message dict; its `_native_snapshot` capsule (merged on the receive thread) is consumed.
Value readMessage(Battle& b, PyObject* message, bool manifest);
Value readMessage(Battle& b, const Value& message);
void receiveManifest(Battle& b, const Value& message);
void receiveSnapshot(Battle& b, const Value& message, double now);
// Per frame: interpolation and the native pose/aim/track/motion writes of replica-owned vehicles.
void advance(Battle& b, double now);
// Whether the replica owns this vehicle's pose (the lifecycle must not write it).
bool owns(const Battle& b, const Actor& vehicle);
// Drains the per-Bot presentation counters for motion diagnostics.
std::vector<MotionProbe> probes(Battle& b, double now);

// Remote factory: create/readiness/destroy of presentation entities.
std::int64_t create(Battle& b, PyObject* descriptor, PyObject* properties, const Point& position, double yaw);
bool isReady(Battle& b, std::int64_t engine_id);
void destroy(Battle& b, std::int64_t engine_id);
void destroyAll(Battle& b);
p::Ref get(const Battle& b, std::int64_t engine_id);
p::Ref prepareDescriptor(Battle& b, PyObject* descriptor);
void requestWreck(Battle& b, std::int64_t engine_id);
bool engineOwns(const Battle& b, std::int64_t engine_id);
bool engineActive(const Battle& b);
bool setInterpolateMotion(Battle& b, std::int64_t engine_id, bool enabled);

// Pose, aim and motion writers for vehicles the replica does not own (worker, authority Bots).
bool setPose(Battle& b, std::int64_t engine_id, const Point& position, double yaw, double pitch, double roll,
             std::optional<double> relax_time, std::optional<double> now);
bool setAim(Battle& b, std::int64_t engine_id, double hull_yaw, double aim_yaw, double gun_pitch);
bool settleMotion(Battle& b, std::int64_t engine_id, std::optional<double> now);
bool updateSiegePose(Battle& b, std::int64_t engine_id);
// Unblended (body, ground) authority matrices; nullopt when the vehicle has no attached presentation.
std::optional<std::pair<Object, Object>> collisionMatrices(Battle& b, std::int64_t engine_id, PyObject* ground = nullptr);
bool presentHitImpulse(Battle& b, std::int64_t engine_id, PyObject* direction, double impulse);
// Belt speed and engine mode of one presented vehicle: (power, movement flags).
bool updateTracks(Battle& b, std::int64_t engine_id, double left, double right, std::array<int, 2> mode);
struct TrackFeedState {
    std::optional<std::pair<double, double>> written;
    std::string outcome;
    bool hidden = false;
    std::optional<bool> engine_owned;
};
std::optional<TrackFeedState> trackFeedState(const Battle& b, std::int64_t engine_id);
TrackParams trackParams(PyObject* descriptor);

// Stock visibility gates of a native remote Vehicle.
void setDrawVisibility(Battle& b, PyObject* vehicle, bool visible);
bool setEngineAudible(PyObject* entity, bool audible);
bool closePresentationExtras(PyObject* appearance, bool visible);
// The ground decal, dust/exhaust and bound-effect gates closePresentationExtras combines; the decal state of a
// retired compound is forgotten with clearGroundDecalVisibilityState.
bool setGroundDecalVisibility(PyObject* appearance, bool visible);
bool stopGroundEffects(PyObject* appearance);
bool stopBoundVisualEffects(PyObject* appearance);
void clearGroundDecalVisibilityState(PyObject* appearance);
// #1513 precise descriptor collision at a supplied body matrix (remote_vehicle.collide_vehicle_at_matrix): the
// sorted SegmentCollisionResultExt rows of every attached component.
p::Ref collideAtMatrix(Battle& b, PyObject* vehicle, PyObject* vehicle_matrix, PyObject* start, PyObject* end);

// The pose overlay stock Vehicle hooks read.
Overlay* overlay(PyObject* vehicle, bool create = false);
struct OverlayPose {
    PyObject* position;          // Math.Vector3
    double yaw;
    PyObject* matrix;            // provider
    double speed = 0., turn_speed = 0.;
    PyObject* velocity = nullptr, *acceleration = nullptr;
    PyObject* steady_rotation_matrix = nullptr, *stabilised_matrix = nullptr;
};
void publishOverlay(PyObject* vehicle, const OverlayPose& pose);
void clearOverlay(PyObject* vehicle);
void clearOverlays();

// Engine audio relay: keeps native engine start/state events for an audition rebuilt on reveal.
bool installEngineAudio();
void uninstallEngineAudio();
bool bindEngineMotion(PyObject* appearance, PyObject* speed_link, PyObject* rotation_link);
void releaseEngineAudio(PyObject* appearance);
}
}
