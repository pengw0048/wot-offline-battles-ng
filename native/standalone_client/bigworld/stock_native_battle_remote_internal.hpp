#pragma once
// Remote presentation internals shared by its translation units.
#include "stock_native_battle_remote.hpp"
#include <cmath>
#include <functional>

namespace native_battle {
namespace remote {
constexpr double pi = 3.1415926535897932384626433832795;
inline double wrap(double v) { return v - std::floor((v + pi) / (2. * pi)) * (2. * pi); }
inline double principal(double v) { return v < -pi || v > pi ? wrap(v) : v; }
inline double clamp(double v, double low, double high) { return std::max(low, std::min(high, v)); }
inline double blendAngle(double source, double target, double ratio) { return source + wrap(target - source) * ratio; }

// Engine boundary (stock_native_battle_remote_engine.cpp).
PyObject* nativeVector(const Point& v);
Point nativeXYZ(PyObject* vector);
void nativeMatrix(PyObject* matrix, const Point& translation, const Point& ypr, bool rotate);
bool engineOwnsEntity(std::int64_t engine_id, PyObject* python);
// #1513 CompoundAppearance engine mode and belt scroll through its native controllers.
void applyTrackMode(PyObject* appearance, std::array<int, 2> mode);
void applyTrackScroll(PyObject* appearance, double left, double right);
}

// The presentation of one remote Vehicle created by the factory (replaces _NativeRemoteState).
struct RemoteVehicle {
    std::int64_t engine_id = 0;
    bool authority_geometry = false;
    Object matrix, animation, provider, key_from, key_to, identity_placing, aim;   // aim: turretMatrix/gunMatrix target
    Point position{};
    double yaw = 0., pitch = 0., roll = 0., speed = 0., turn_speed = 0.;
    Point velocity{}, acceleration{};
    std::optional<double> last_pose_time;
    Object matrix_cache;                               // matrix identity of the cached write
    std::optional<std::array<double, 6>> matrix_pose;  // xyz + principal ypr already written
    std::array<double, 6> render_pose{};
    std::optional<std::array<double, 6>> render_from, render_to;
    double render_started = 0., render_duration = 0.;
    std::optional<double> aim_relative_yaw, aim_gun_pitch, aim_desired_gun_pitch;
    Object siege_relative_body, siege_body;
    Object entity, model_changed, track_scroll, speed_link, rotation_link;
    std::optional<std::array<int, 2>> track_mode;   // (power, movement flags)
    std::optional<std::array<double, 4>> track_feed;   // rounded left, right, power, flags (contacts folded below)
    std::optional<std::array<bool, 2>> track_feed_contacts;
    bool track_feed_hidden = false;
    std::string track_outcome = "never fed";
    std::optional<std::pair<double, double>> track_written;
    std::map<std::string, bool> capabilities;
    std::map<std::string, std::string> errors;
    bool capabilities_changed = false;
};

// The replica's view of one presented, non-local vehicle.
struct ReplicaBinding {
    std::string key, kind;
    std::shared_ptr<Actor> data;
    std::int64_t engine_id = 0;
    bool ready = false, managed = false, has_written = false, has_aim = false;
    Pose written{};
    TrackParams track;
    bool authority_geometry = false;
    Object descriptor, gun;
    std::optional<double> static_yaw, static_pitch, last_track_time, next_track_update;
    double last_track_yaw = 0.;
    std::array<double, 4> last_aim{};
};

namespace remote {
RemoteVehicle* vehicle(const Battle& b, std::int64_t engine_id);
void attachVehicle(Battle& b, RemoteVehicle& rv, PyObject* entity);
void detachVehicle(Battle& b, RemoteVehicle& rv);
void publishPose(RemoteVehicle& rv);
void bindStockMotion(Battle& b, RemoteVehicle& rv);
void publishCapabilities(Battle& b, RemoteVehicle& rv);
void recordCapability(RemoteVehicle& rv, const std::string& name, bool available, const std::string& error = {});
bool writeAimPitch(RemoteVehicle& rv, double gun_pitch);
// The replica's per-frame writes for one bound vehicle.
void presentReplicaPose(Battle& b, ReplicaBinding& e, RemoteVehicle& rv, double now);
void replicaTracks(Battle& b, ReplicaBinding& e, RemoteVehicle& rv, double now);
void refreshBindings(Battle& b, bool cold);
// Native Python objects the presentation hands to stock code (stock_native_battle_remote_gates.cpp).
Object aimTarget(Battle& b);
Object callback(std::function<PyObject*(PyObject*, PyObject*)> body);
// Forgets the per-appearance decal and audio state of a retired presentation.
void clearAppearanceState(PyObject* appearance);
}
}
