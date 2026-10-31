#pragma once
// Vehicle laws shared by the player's own vehicle and the copied Bots: the pinned vehicle_physics constants, the
// local drive constants the battle runtime defined, the derived drive parameters and the belt law. Pure C++.
#include <array>
#include <cmath>

namespace native_battle {
namespace local {
namespace law {
constexpr double pi = 3.141592653589793;
inline double radians(double degrees) { return degrees * (pi / 180.); }

// vehicle_physics: Wargaming tuning from #1513 physics_shared and the offline drive model.
constexpr double G = 9.81, GRAVITY_FACTOR = 1.25, GRAVITY = G * GRAVITY_FACTOR, WEIGHT_SCALE = 0.001, COHESION = 1.3;
constexpr double DRIVE_TRACTION = 1.0, SLOPE_GRIP_LNG_FULL = 1.0, SLOPE_GRIP_LNG_MIN = 0.1;
inline const double SLOPE_GRIP_LNG_FULL_Y = std::cos(radians(27.5)), SLOPE_GRIP_LNG_MIN_Y = std::cos(radians(32.0));
constexpr double SLOPE_GRIP_SDW_FULL = 1.0, SLOPE_GRIP_SDW_MIN = 0.1;
inline const double SLOPE_GRIP_SDW_FULL_Y = std::cos(radians(24.5)), SLOPE_GRIP_SDW_MIN_Y = std::cos(radians(29.0));
inline const double SLIP_THRESHOLD_TAN = std::tan(radians(27.5));
constexpr double SLIP_DRAG = 10.0, POWER_FACTOR = GRAVITY_FACTOR, BKWD_POWER_FRACTION = 1.0;
constexpr double ANG_ACCELERATION_TIME = 0.05, SPEED_AFFECT_ROT_DECREASE = 0.0;
constexpr double COH_DECAY_Y = 0.969, COH_DECAY_FACTOR = 5.78, COH_DECAY_POW = 3.0, COH_DECAY_BOUND = 0.5;
constexpr double SLOPE_COH_DECAY = 0.25, SLOPE_COH_DECAY_Y = 0.72;
constexpr double COAST_BRAKE_SHARE = 0.5, STEER_RESIST_MULT = 1.6, ENGINE_MIN_V = 1.5, SCROLL_CAP = 0.995;
constexpr double FALL_SAFE_SPEED = 10.0;
constexpr double GROUND_FOLLOW_BASE = 0.6, GROUND_FOLLOW_MIN = 0.8, GROUND_FOLLOW_MAX = 2.5, GROUND_PITCH_LIMIT = 0.96;
constexpr double SUSP_COMPRESSION_MIN = 0.85, SUSP_COMPRESSION_MIN_MASS = 60.0;
constexpr double SUSP_COMPRESSION_MAX = 0.88, SUSP_COMPRESSION_MAX_MASS = 30.0;
constexpr double CLEARANCE_SCALE = 1.75, CLEARANCE_MIN = 0.55, CLEARANCE_MAX = 0.60;
constexpr double CLEARANCE_TO_LENGTH_MIN = 0.085, CLEARANCE_TO_LENGTH_MAX = 0.112;
constexpr double TRACK_LENGTH_MIN = 0.60, TRACK_LENGTH_MAX = 0.64, HARD_RATIO_MIN = 0.50, HARD_RATIO_MAX = 0.52;
constexpr int NUM_SPRING_PAIRS = 5;
constexpr double SPRING_TRACK_WIDTH_RATIO = 0.45;
constexpr std::array<double, 3> TRIAL_HULL_INERTIA_FACTORS{{1.0, 1.0, 1.8}};
constexpr double SERVER_PHYSICS_HZ = 30.0;
constexpr int SERVER_PHYSICS_SUBSTEPS = 2;
constexpr double SERVER_PHYSICS_STEP = 1.0 / (SERVER_PHYSICS_HZ * SERVER_PHYSICS_SUBSTEPS);
constexpr int SERVER_PHYSICS_MAX_SUBSTEPS = 12, SERVER_PHYSICS_CONSTRAINT_ITERATIONS = 10;
constexpr double SERVER_PHYSICS_DAMPING_RATIO_BASE = 0.75, SERVER_PHYSICS_DAMPING_RATIO_SCALE = 0.25;
constexpr double SERVER_PHYSICS_MAX_SPRING_FORCE_FACTOR = 6.0;
constexpr double AIRBORNE_ANGULAR_DAMPING = 4.0, AIRBORNE_ANGULAR_SPEED_LIMIT = 0.6;
constexpr double FREEZE_ANG_ACCEL_EPSILON = 0.35, FREEZE_ACCEL_EPSILON = 0.4;
constexpr double FREEZE_VEL_EPSILON = 0.15, FREEZE_ANG_VEL_EPSILON = 0.06;
constexpr double ALLOWED_PENETRATION = 0.01, CONTACT_PENETRATION = 0.1, TRACKS_PENETRATION = 0.01;
constexpr std::array<double, 4> HARD_CONTACT_YAW_DELTAS{{0.55, -0.55, 1.0, -1.0}};
constexpr double HARD_CONTACT_ENTRY_FACTOR = 0.60, HARD_CONTACT_SLIDE_DECAY = 0.85;
constexpr double HARD_CONTACT_BRAKE_DECAY = 0.35, HARD_CONTACT_STOP_SPEED = 0.05;
constexpr int HARD_CONTACT_GRIND_TICKS = 4;
constexpr double SLIDE_MAX = 7.0, SLIDE_DRAG = 1.5, SLIDE_HOLD_TAN = 0.50, SLIDE_KINETIC = 0.45;
constexpr double OVERSPEED_MAX_FACTOR = 1.10;

// battle_runtime, world_collision, tank_collision and lan_client constants the local drive reads.
constexpr double NETWORK_INPUT_SECONDS = 1.0 / 30.0, GUN_TRAVERSE_LIMIT_EPSILON = 1.0e-5;
constexpr double DESTRUCTIBLE_POSE_MAX_ANGLE_STEP = pi / 36.0;
constexpr unsigned DESTRUCTIBLE_POSE_MAX_SWEEP_STEPS = 16, MAX_PENDING_LANDING_IMPACTS = 32;
constexpr double GROUND_PLANE_EPSILON = 0.35, MAX_DRIVABLE_GRADIENT = 1.28;
constexpr double CONTACT_BROADPHASE_PADDING = 0.25, PLAYER_LANDING_MAX_IMPACT_SPEED = 200.0;
}

// The drive parameters vehicle_physics.derive_params projects from a mounted descriptor (SI units).
struct DriveParams {
    double mass = 5730., power = 45. * 735.49875, forward = 32. / 3.6, reverse = 12. / 3.6;
    double rotation = law::radians(38.0), friction = 0.6867, brake = law::COHESION * law::GRAVITY;
    double track_center = 1.5, min_plane_normal_y = std::cos(law::radians(25.0)), native_power_ratio = 1.;
    std::array<double, 3> terrain{{1.1, 1.4, 2.6}};
    bool rotation_around_center = true;
    bool operator==(const DriveParams& o) const {
        return mass == o.mass && power == o.power && forward == o.forward && reverse == o.reverse && rotation == o.rotation &&
               friction == o.friction && brake == o.brake && track_center == o.track_center &&
               min_plane_normal_y == o.min_plane_normal_y && native_power_ratio == o.native_power_ratio &&
               terrain == o.terrain && rotation_around_center == o.rotation_around_center;
    }
    bool operator!=(const DriveParams& o) const { return !(*this == o); }
};

// Signed turn-axis offset when a chassis cannot counter-rotate (vehicle_physics.track_pivot_offset).
inline double trackPivotOffset(bool around_center, double track_center, double v, double omega, bool airborne = false,
                               double drive_intent = 0.) {
    if (airborne || around_center || std::abs(omega) <= 1.0e-9) return 0.;
    const double radius = std::fmax(0., track_center - std::abs(v / omega));
    const double direction = (v < -1.0e-9 || (std::abs(v) <= 1.0e-9 && drive_intent < 0.)) ? -1. : 1.;
    return radius * direction * (omega > 0. ? 1. : -1.);
}
// Left/right belt speeds, clamped strictly below maxMovement (vehicle_physics.track_scroll).
inline std::array<double, 2> trackScroll(bool around_center, double track_center, double speed_forward, double v,
                                         double omega, bool airborne = false, double drive_intent = 0.) {
    v += trackPivotOffset(around_center, track_center, v, omega, airborne, drive_intent) * omega;
    const double cap = speed_forward * law::SCROLL_CAP;
    auto clamp = [cap](double s) { return s > cap ? cap : s < -cap ? -cap : s; };
    return {{clamp(v + omega * track_center), clamp(v - omega * track_center)}};
}
inline std::array<double, 2> trackScroll(const DriveParams& p, double v, double omega, bool airborne = false,
                                         double drive_intent = 0.) {
    return trackScroll(p.rotation_around_center, p.track_center, p.forward, v, omega, airborne, drive_intent);
}
}
}
