// The player's own vehicle in a replay: the recorded local row and its per-frame presentation.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_local.hpp"
#include "stock_native_battle_projectiles.hpp"
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_vehicles.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_self_presentation.hpp"
#include <cmath>

namespace native_battle {
namespace vehicles {
// Provided by the vehicles subsystem: publishes the native rotator angle as the server gun echo.
bool echoLocalGunAngles(Battle&, std::optional<double> turret_yaw, std::optional<double> gun_pitch);
}
namespace local {
namespace dr = stock_native_client_drive;
namespace {
constexpr double pi = 3.141592653589793;
// battle_runtime.BOT_MOVING_SPEED / BOT_TURNING_RATE: the recorded motion's engine-mode thresholds.
constexpr double moving_speed = 0.05, turning_rate = 0.02;
// replay_presentation.ANGLE_FIELDS / CONTINUOUS_FIELDS.
const char* const angle_fields[] = {"_local_yaw", "_local_pitch", "_local_roll", "turret_yaw", "gun_pitch"};
const char* const continuous_fields[] = {"_local_speed", "_local_turn_speed", "_local_vertical_speed", "_local_siege_aim_pitch"};

bool has(const Value& row, const char* name) {
    const auto& r = row.resolved();
    return r.kind == Value::Kind::Object && r.object.count(name);
}
double pythonModulo(double value, double divisor) {
    const double r = std::fmod(value, divisor);
    return r < 0. ? r + divisor : r;
}
Value number(double v) { Value out; out.kind = Value::Kind::Number; out.number = v; return out; }

// replay_presentation.blend_pose: continuous pose between two committed rows; discrete fields stay on the left row.
Value blend(const Value& left, const std::optional<Value>& right_row, double moment) {
    Value result = left.resolved();
    if (!right_row) return result;
    const Value& right = right_row->resolved();
    const double start = left.get("_replay_t").numeric(0.), end = right.get("_replay_t").numeric(0.);
    const double span = end - start;
    if (span <= 0. || span > 0.25 || moment <= start) return result;
    // Do not animate across a death, teleport or missing section.
    if ((left.get("health").numeric(1.) > 0.) != (right.get("health").numeric(1.) > 0.)) return result;
    if (left.get("discontinuity").truth() || right.get("discontinuity").truth()) return result;
    const auto& from = left.get("position").resolved().array;
    const auto& to = right.get("position").resolved().array;
    if (from.size() != 3 || to.size() != 3) throw p::Error("replay local position is invalid");
    double delta[3], distance = 0.;
    for (unsigned i = 0; i < 3; ++i) { delta[i] = to[i].numeric() - from[i].numeric(); distance += delta[i] * delta[i]; }
    double speed = 0.;
    for (const Value* row : {&left, &right})
        for (const char* name : {"_local_speed", "_local_vertical_speed"}) speed = std::max(speed, std::abs(row->get(name).numeric(0.)));
    const double bound = 1. + 3. * speed * span;
    if (distance > bound * bound) return result;
    const double a = std::min(1., std::max(0., (moment - start) / span));
    auto& position = result.object["position"];
    position = Value();
    position.kind = Value::Kind::Array;
    for (unsigned i = 0; i < 3; ++i) position.array.push_back(number(from[i].numeric() + a * delta[i]));
    for (const char* name : angle_fields)
        if (has(left, name) && has(right, name)) {
            const double l = left.get(name).numeric(), r = right.get(name).numeric();
            result.object[name] = number(l + a * (pythonModulo(r - l + pi, 2. * pi) - pi));
        }
    for (const char* name : continuous_fields)
        if (has(left, name) && has(right, name)) {
            const double l = left.get(name).numeric(), r = right.get(name).numeric();
            result.object[name] = number(l + a * (r - l));
        }
    return result;
}

// BattleRuntime._apply_replay_gun_pose: #1513's native gun owner and matrices take the recorded realised pose.
bool applyGunPose(Battle& b, const Value& sample) {
    if (!has(sample, "turret_yaw") && !has(sample, "gun_pitch")) return false;
    auto rotator = py::optional(b.engine.avatar.get(), "gunRotator");
    auto force = py::optional(rotator.get(), "forceGunParams");
    auto turret_matrix = py::optional(rotator.get(), "_VehicleGunRotator__updateTurretMatrix");
    auto gun_matrix = py::optional(rotator.get(), "_VehicleGunRotator__updateGunMatrix");
    if (!py::callable(force) || !py::callable(turret_matrix) || !py::callable(gun_matrix))
        throw p::Error("#1513 replay gun-pose interface is unavailable: forceGunParams/updateTurretMatrix/updateGunMatrix");
    auto read = [&](const char* key, const char* attribute) {
        return has(sample, key) ? sample.get(key).numeric() : p::scalar(py::attr(rotator.get(), attribute).get());
    };
    const std::array<double, 3> signature{{read("turret_yaw", "turretYaw"), read("gun_pitch", "gunPitch"), read("dispersion_angle", "dispersionAngle")}};
    auto& l = *b.local;
    if (l.replay_gun_signature == signature) return false;
    gun::setLocked(b, true);
    vehicles::echoLocalGunAngles(b, signature[0], signature[1]);
    auto yaw = p::number(signature[0]), pitch = p::number(signature[1]), dispersion = p::number(signature[2]), zero = p::number(0.);
    py::invoke(force.get(), yaw.get(), pitch.get(), dispersion.get());
    // A recorded sample is a realised pose: commit it like native reset() instead of predicting a new angle.
    py::invoke(turret_matrix.get(), yaw.get(), zero.get());
    py::invoke(gun_matrix.get(), pitch.get(), zero.get());
    l.replay_gun_signature = signature;
    if (!l.replay_applied) log("OFFLINE_REPLAY play_gun_pose interface=forceGunParams native_matrices=True packed_echo=True live_aim_locked=True");
    return true;
}
}

p::Ref replayState(Battle& b) {
    const auto& l = *b.local;
    auto row = p::dict();
    auto put = [&](const char* name, p::Ref value) { p::set(row.get(), name, value); };
    put("_local_yaw", p::number(l.yaw));
    put("_local_pitch", p::number(l.pitch));
    put("_local_roll", p::number(l.roll));
    put("_local_speed", p::number(l.speed));
    put("_local_turn_speed", p::number(l.turn_speed));
    put("_local_airborne", p::boolean(l.airborne));
    put("_local_siege_aim_pitch", p::number(l.siege_aim_pitch));
    // The driver exists once the first live slice ran; before it the vehicle has not moved.
    double vertical = 0., turn = 0., throttle = 0.;
    bool left_flying = false, right_flying = false;
    if (!b.worker && b.live && l.drive) {
        const auto& s = dr::driverAccess(b).driver->state();
        vertical = s.vertical_speed;
        turn = s.drive_turn;
        throttle = s.drive_throttle;
        left_flying = s.left_flying;
        right_flying = s.right_flying;
    }
    put("_local_vertical_speed", p::number(vertical));
    put("_local_left_flying", p::boolean(left_flying));
    put("_local_right_flying", p::boolean(right_flying));
    put("_local_drive_turn", p::number(turn));
    put("_local_drive_throttle", p::number(throttle));
    return row;
}

bool presentReplay(Battle& b, double dt) {
    if (!b.replay || !b.gun || !b.local || !b.engine.server) return false;
    const Value& left = b.gun->replay_local.resolved();
    if (left.kind == Value::Kind::Null) return false;
    auto& l = *b.local;
    auto entity = b.entity(p::wide(py::attr(b.engine.server.get(), "vehicle_id").get()));
    if (entity.get() == Py_None || !l.matrix || !l.model) return false;
    std::optional<Value> right;
    auto future = py::optional(b.engine.client.get(), "_replay_local_future");
    if (future.get() != Py_None) right = cs::read(future.get());
    auto presentation_time = py::optional(b.engine.client.get(), "presentation_time");
    const double moment = py::callable(presentation_time) ? p::scalar(py::invoke(presentation_time.get()).get())
                                                          : left.get("_replay_t").numeric(0.);
    const Value sample = blend(left, right, moment);

    const auto& position = sample.get("position").resolved().array;
    if (position.size() != 3) throw p::Error("replay local position is invalid");
    l.position = {position[0].numeric(), position[1].numeric(), position[2].numeric()};
    auto scalar = [&](const char* name, double& target) { if (has(sample, name)) target = sample.get(name).numeric(); };
    auto flag = [&](const char* name, bool& target) { if (has(sample, name)) target = sample.get(name).truth(); };
    scalar("_local_yaw", l.yaw);
    scalar("_local_pitch", l.pitch);
    scalar("_local_roll", l.roll);
    scalar("_local_speed", l.speed);
    scalar("_local_turn_speed", l.turn_speed);
    scalar("_local_siege_aim_pitch", l.siege_aim_pitch);
    flag("_local_airborne", l.airborne);

    // The recorded pose drives the same self presentation as a live slice; no drive law runs.
    prepare(b);
    auto access = dr::driverAccess(b);
    auto& s = access.driver->state();
    auto& controls = access.driver->controls();
    s.position = l.position;
    s.yaw = l.yaw; s.pitch = l.pitch; s.roll = l.roll;
    s.speed = l.speed; s.turn_speed = l.turn_speed; s.airborne = l.airborne;
    scalar("_local_vertical_speed", s.vertical_speed);
    scalar("_local_drive_throttle", s.drive_throttle);
    flag("_local_left_flying", s.left_flying);
    flag("_local_right_flying", s.right_flying);
    // Replay has no live movement sender: the engine mode follows the recorded motion (_bot_engine_mode).
    controls.forward = l.speed > moving_speed ? 1. : l.speed < -moving_speed ? -1. : 0.;
    s.drive_turn = l.turn_speed < -turning_rate ? -1. : l.turn_speed > turning_rate ? 1. : 0.;
    if (has(sample, "turret_yaw")) controls.aim_yaw = l.yaw + sample.get("turret_yaw").numeric();
    if (has(sample, "gun_pitch")) controls.gun_pitch = sample.get("gun_pitch").numeric();
    b.vehicles->updateLocal({l.position[0], l.position[1], l.position[2], l.yaw, l.pitch, l.roll, controls.aim_yaw, controls.gun_pitch},
                            {std::sin(l.yaw) * l.speed, s.vertical_speed, std::cos(l.yaw) * l.speed}, l.speed);
    dr::selfPresentation(b, entity.get(), *access.driver, dt, dr::SliceTrace{}, false);
    auto vector = [&](double x, double y, double z) { return py::call(b.engine.math.get(), "Vector3", p::number(x).get(), p::number(y).get(), p::number(z).get()); };
    auto where = vector(l.position[0], l.position[1], l.position[2]);
    auto rotation = vector(0., 0., l.yaw);
    py::call(b.engine.avatar.get(), "updateOwnVehiclePosition", where.get(), rotation.get(), p::number(l.speed).get(),
             p::number(l.turn_speed).get());

    applyGunPose(b, sample);
    gun::applyReplayAmmo(b, left);
    if (!l.replay_applied || *l.replay_applied != left) {
        // Replay keeps the original observer's visibility decisions.
        const auto& rows = left.get("visibility").resolved();
        if (rows.kind == Value::Kind::Object)
            for (const auto& row : rows.object) {
                Actor* vehicle = b.vehicle(row.first);
                const auto& flags = row.second.resolved().array;
                if (vehicle && flags.size() == 2) vehicles::setSpotVisibility(b, *vehicle, flags[0].truth(), flags[1].truth());
            }
        l.replay_applied = left;
    }
    return true;
}
}
}
