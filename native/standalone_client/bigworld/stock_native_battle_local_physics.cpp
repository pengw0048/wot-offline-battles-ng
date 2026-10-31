// vehicle_physics on stock descriptors: the drive parameters, the ten-spring suspension trial and its exclusion.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_local.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace local {
namespace {
namespace step = wot::offline_bot_step;
[[noreturn]] void invalid(const std::string& message) {
    PyErr_Clear();
    throw p::Error(message);
}
// float(value), including its TypeError/ValueError.
double toFloat(PyObject* value) {
    p::Ref out(PyNumber_Float(value));
    return PyFloat_AS_DOUBLE(out.get());
}
// `key in mapping` and `mapping[key]`.
bool has(PyObject* mapping, const char* key) {
    const int found = PySequence_Contains(mapping, p::text(key).get());
    if (found < 0) throw p::Error(key);
    return found != 0;
}
p::Ref item(PyObject* mapping, const char* key) { return p::Ref(PyObject_GetItem(mapping, p::text(key).get())); }
p::Ref at(PyObject* sequence, Py_ssize_t index) { return p::Ref(PySequence_GetItem(sequence, index)); }
// getattr(object, name, fallback).
p::Ref attribute(PyObject* object, const char* name, PyObject* fallback = Py_None) {
    PyObject* value = PyObject_GetAttrString(object, name);
    if (value) return p::Ref(value);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
// component.get(name, fallback) for a dict, else getattr(component, name, fallback).
p::Ref field(PyObject* component, const char* name, PyObject* fallback = Py_None) {
    if (!PyDict_Check(component)) return attribute(component, name, fallback);
    PyObject* value = PyDict_GetItemString(component, name);
    return p::retain(value ? value : fallback);
}

// track_pivot_descriptor_params: the pinned pivot flag and half track gauge, or nothing.
std::optional<std::pair<bool, double>> trackPivot(PyObject* td) {
    try {
        const bool mapping = PyDict_Check(td);
        auto chassis = mapping ? field(td, "chassis") : py::attr(td, "chassis");
        auto physics = mapping ? field(td, "physics") : py::attr(td, "physics");
        auto around = PyDict_Check(chassis.get()) ? field(chassis.get(), "rotationIsAroundCenter")
                                                   : py::attr(chassis.get(), "rotationIsAroundCenter");
        if (!PyBool_Check(around.get())) return {};
        const double gauge = std::abs(toFloat(item(physics.get(), "trackCenterOffset").get()));
        if (!(0. < gauge && gauge <= 100.)) return {};
        return std::make_pair(around.get() == Py_True, gauge);
    } catch (const std::exception&) {
        PyErr_Clear();
        return {};
    }
}
// _native_power_ratio: the selected detailed engine override over the generic power projection.
double nativePowerRatio(PyObject* td, double power) {
    try {
        auto empty = p::dict();
        auto physics = attribute(td, "physics", empty.get());
        auto projected = py::call(physics.get(), "get", p::text("nativePowerRatio").get());
        const double value = toFloat(projected.get());
        if (value > 0. && std::isfinite(value)) return value;
    } catch (const std::exception&) {
        PyErr_Clear();
    }
    try {
        auto engine = py::attr(py::attr(td, "engine").get(), "name");
        auto xphysics = py::attr(py::attr(td, "type").get(), "xphysics");
        auto detailed = item(xphysics.get(), "detailed");
        auto config = p::Ref(PyObject_GetItem(item(detailed.get(), "engines").get(), engine.get()));
        const double native = toFloat(item(config.get(), "smplEnginePower").get());
        const double base = power * 0.001 * law::GRAVITY_FACTOR;
        if (native > 0. && base > 0. && std::isfinite(native)) return native / base;
    } catch (const std::exception&) {
        PyErr_Clear();
    }
    return 1.;
}
// _factor_list / _factor over the attribute-factor mapping.
std::array<double, 3> factorList(PyObject* factors, const char* name) {
    try {
        auto values = item(factors, name);
        std::array<double, 3> out{};
        for (int i = 0; i < 3; ++i) out[i] = toFloat(at(values.get(), i).get());
        return out;
    } catch (const std::exception&) {
        PyErr_Clear();
        return {{1., 1., 1.}};
    }
}
double factor(PyObject* factors, const char* name) {
    try {
        return std::max(0., toFloat(item(factors, name).get()));
    } catch (const std::exception&) {
        PyErr_Clear();
        return 1.;
    }
}

// _required_value, _finite_number and _linear_interpolate of the suspension trial.
p::Ref required(PyObject* component, const char* name, const std::string& context) {
    auto value = field(component, name);
    if (value.get() == Py_None) invalid(context + " is required by the contrib suspension trial");
    return value;
}
double finite(PyObject* value, const std::string& context) {
    double out = 0.;
    try {
        out = toFloat(value);
    } catch (const std::exception&) {
        invalid(context + " is invalid");
    }
    if (!std::isfinite(out)) invalid(context + " is not finite");
    return out;
}
double interpolate(double value, double minimum, double maximum, double lower, double upper) {
    if (maximum == minimum) return lower;
    const double ratio = std::max(0., std::min(1., (value - minimum) / (maximum - minimum)));
    return lower + (upper - lower) * ratio;
}
// _bbox: the component's hit-tester box corners and its positive dimensions.
struct Box { step::Point minimum{}, maximum{}, size{}; };
Box box(PyObject* component, const std::string& context) {
    auto tester = required(component, "hitTester", context + " hitTester");
    auto bounds = required(tester.get(), "bbox", context + " hitTester bbox");
    // Index failures are an invalid box; a non-finite corner keeps its own message.
    auto index = [&](PyObject* sequence, Py_ssize_t i) {
        PyObject* value = sequence ? PySequence_GetItem(sequence, i) : nullptr;
        if (!value) invalid(context + " hitTester bbox is invalid");
        return p::Ref(value);
    };
    Box out;
    auto minimum = index(bounds.get(), 0), maximum = index(bounds.get(), 1);
    for (int i = 0; i < 3; ++i) {
        out.maximum[i] = finite(index(maximum.get(), i).get(), context + " bbox maximum");
        out.minimum[i] = finite(index(minimum.get(), i).get(), context + " bbox minimum");
        out.size[i] = out.maximum[i] - out.minimum[i];
    }
    for (double v : out.size)
        if (v <= 0.) invalid(context + " hitTester bbox has no volume");
    return out;
}
// _suspension_wheel_radius: the selected chassis wheel, else one spring spacing. #1513 client xphysics carries only
// `grounds` per chassis, so no detailed wheelRadius/roadWheelPositions/stiffness/damping/bodyHeight refinement exists.
double wheelRadius(PyObject* chassis, double spacing) {
    double radius = 0.;
    try {
        auto groups = py::attr(py::attr(chassis, "wheels").get(), "groups");
        radius = finite(py::attr(at(groups.get(), 0).get(), "radius").get(), "selected chassis wheel radius");
    } catch (const std::exception&) {
        PyErr_Clear();
        radius = 0.;
    }
    return radius > 0. ? radius : std::max(0.125, spacing * 0.5);
}
step::SuspensionContact contact(double x, double y, double z, int side, double penetration) {
    step::SuspensionContact out;
    out.x = x; out.y = y; out.z = z; out.side = side; out.penetration = penetration;
    return out;
}
}

DriveParams deriveParams(PyObject* td, PyObject* factors) {
    DriveParams p;
    auto tdp = [&] {
        try {
            auto value = attribute(td, "physics");
            if (p::flag(value.get())) return value;
        } catch (const std::exception&) {
            PyErr_Clear();
        }
        return p::dict();
    }();
    // The pinned law keeps every field read before the first failing one.
    try {
        PyObject* raw = tdp.get();
        if (has(raw, "weight")) p.mass = toFloat(item(raw, "weight").get());
        if (has(raw, "enginePower")) p.power = toFloat(item(raw, "enginePower").get());
        if (has(raw, "speedLimits")) {
            auto limits = item(raw, "speedLimits");
            p.forward = toFloat(at(limits.get(), 0).get());
            p.reverse = toFloat(at(limits.get(), 1).get());
        }
        if (has(raw, "terrainResistance")) {
            auto values = item(raw, "terrainResistance");
            std::array<double, 3> terrain{};
            for (int i = 0; i < 3; ++i) terrain[i] = toFloat(at(values.get(), i).get());
            p.terrain = terrain;
        }
        if (has(raw, "specificFriction")) p.friction = toFloat(item(raw, "specificFriction").get());
        if (has(raw, "brakeForce")) {
            // #1513 leaves this legacy field at zero: zero means unavailable, keep the track-grip fallback.
            const double brake = toFloat(item(raw, "brakeForce").get());
            if (brake > 0.) p.brake = std::min(law::COHESION * law::GRAVITY, brake / std::max(p.mass, 1.));
        }
        if (has(raw, "trackCenterOffset")) {
            const double center = std::abs(toFloat(item(raw, "trackCenterOffset").get()));
            if (0.3 <= center && center <= 3.0) p.track_center = center;
        }
        if (has(raw, "minPlaneNormalY")) p.min_plane_normal_y = toFloat(item(raw, "minPlaneNormalY").get());
    } catch (const std::exception&) {
        PyErr_Clear();
    }
    p.native_power_ratio = nativePowerRatio(td, p.power);
    try {
        auto chassis = attribute(td, "chassis");
        if (chassis.get() != Py_None) {
            p.rotation_around_center = p::flag(field(chassis.get(), "rotationIsAroundCenter", Py_True).get());
            if (!p.rotation_around_center) {
                const auto pivot = trackPivot(td);
                if (!pivot) invalid("#1513 track pivot geometry is unavailable");
                p.track_center = pivot->second;
            }
            auto raw = field(chassis.get(), "rotationSpeed");
            if (raw.get() == Py_None) invalid("#1513 chassis rotation speed is unavailable");
            const double rotation = toFloat(raw.get());
            // The reader stores radians; tolerate a raw-degrees dump.
            p.rotation = rotation > 6.3 ? law::radians(rotation) : rotation;
        }
    } catch (const p::Error& e) {
        const std::string message = e.what();
        if (!PyErr_Occurred() && message.rfind("#1513 ", 0) == 0) throw;
        invalid("#1513 chassis rotation speed is invalid: " + describe(e));
    }
    // VehicleParams composes terrain resistance as the crew factor times the descriptor's rolling-friction factors.
    std::array<double, 3> rolling{{1., 1., 1.}};
    try {
        if (has(tdp.get(), "rollingFrictionFactors")) {
            auto values = item(tdp.get(), "rollingFrictionFactors");
            for (int i = 0; i < 3; ++i) rolling[i] = toFloat(at(values.get(), i).get());
        }
    } catch (const std::exception&) {
        PyErr_Clear();
        rolling = {{1., 1., 1.}};
    }
    const bool scaled = factors && factors != Py_None && p::flag(factors);
    const auto crew = scaled ? factorList(factors, "chassis/terrainResistance") : std::array<double, 3>{{1., 1., 1.}};
    for (int i = 0; i < 3; ++i) p.terrain[i] = p.terrain[i] * crew[i] * rolling[i];
    if (scaled) {
        p.rotation *= factor(factors, "vehicle/rotationSpeed");
        p.power *= factor(factors, "engine/power");
    }
    return p;
}

Value physicsValue(const DriveParams& p) {
    auto number = [](double v) { Value out; out.kind = Value::Kind::Number; out.number = v; return out; };
    Value out;
    out.kind = Value::Kind::Object;
    out.object["mass"] = number(p.mass);
    out.object["powerW"] = number(p.power);
    out.object["speedFwd"] = number(p.forward);
    out.object["speedBwd"] = number(p.reverse);
    out.object["rotSpd"] = number(p.rotation);
    out.object["specificFriction"] = number(p.friction);
    out.object["brakeDecel"] = number(p.brake);
    out.object["trackCenter"] = number(p.track_center);
    out.object["minPlaneNormalY"] = number(p.min_plane_normal_y);
    out.object["nativePowerRatio"] = number(p.native_power_ratio);
    Value terrain;
    terrain.kind = Value::Kind::Array;
    for (double v : p.terrain) terrain.array.push_back(number(v));
    out.object["terrainResist"] = std::move(terrain);
    Value around;
    around.kind = Value::Kind::Boolean;
    around.boolean = p.rotation_around_center;
    out.object["rotationIsAroundCenter"] = std::move(around);
    return out;
}

DriveParams physicsFromValue(const Value& raw) {
    const auto& value = raw.resolved();
    if (value.kind != Value::Kind::Object) throw p::Error("Native effective physics are unavailable");
    auto number = [&](const char* key) {
        const auto& v = value.get(key).resolved();
        if (v.kind != Value::Kind::Number && v.kind != Value::Kind::Integer) throw p::Error(std::string("Native effective physics lack ") + key);
        const double n = v.numeric(std::nan(""));
        if (!std::isfinite(n)) throw p::Error(std::string("Native effective physics ") + key + " is invalid");
        return n;
    };
    DriveParams p;
    p.mass = number("mass");
    p.power = number("powerW");
    p.native_power_ratio = number("nativePowerRatio");
    p.forward = number("speedFwd");
    p.reverse = number("speedBwd");
    p.friction = number("specificFriction");
    p.brake = number("brakeDecel");
    p.rotation = number("rotSpd");
    p.track_center = number("trackCenter");
    if (value.get("minPlaneNormalY").resolved().kind != Value::Kind::Null) p.min_plane_normal_y = number("minPlaneNormalY");
    const auto& terrain = value.get("terrainResist").resolved();
    if (terrain.kind != Value::Kind::Array || terrain.array.size() != 3) throw p::Error("Native effective terrain resistance is invalid");
    for (int i = 0; i < 3; ++i) {
        const double n = terrain.array[i].resolved().numeric(std::nan(""));
        if (!std::isfinite(n)) throw p::Error("Native effective terrain resistance is invalid");
        p.terrain[i] = n;
    }
    p.rotation_around_center = value.get("rotationIsAroundCenter").truth(true);
    return p;
}

bool suspensionTrialExcluded(PyObject* descriptor) {
    return p::flag(field(descriptor, "isPitchHullAimingAvailable", Py_False).get());
}

step::SuspensionDescriptor deriveSuspension(PyObject* descriptor) {
    // #1513 initVehiclePhysicsClient gives hull-aiming tanks mode-specific spring caches this trial does not own.
    if (suspensionTrialExcluded(descriptor)) invalid("#1513 hydraulic suspension is outside the contrib trial");
    const auto physics = deriveParams(descriptor);
    auto descriptor_physics = required(descriptor, "physics", "vehicle physics descriptor");
    const double mass = finite(required(descriptor_physics.get(), "weight", "vehicle physics weight").get(), "vehicle physics weight");
    if (mass <= 0.) invalid("vehicle physics weight must be positive");
    if (std::abs(physics.mass - mass) > 1.0e-6) invalid("vehicle physics mass projection is inconsistent");
    auto chassis = required(descriptor, "chassis", "vehicle chassis descriptor");
    auto hull = required(descriptor, "hull", "vehicle hull descriptor");
    const auto chassis_box = box(chassis.get(), "chassis");
    const auto hull_box = box(hull.get(), "hull");
    const double width = chassis_box.size[0], chassis_height = chassis_box.size[1], chassis_length = chassis_box.size[2];
    auto mount = required(chassis.get(), "hullPosition", "selected chassis hullPosition");
    step::Point hull_position{};
    try {
        for (int i = 0; i < 3; ++i) hull_position[i] = finite(at(mount.get(), i).get(), "chassis hullPosition");
    } catch (const std::exception&) {
        invalid("selected chassis hullPosition is invalid");
    }
    const double clearance_raw = hull_position[1] + hull_box.minimum[1];
    if (clearance_raw <= 0.) invalid("descriptor ground clearance must be positive");
    const double clearance = std::max(law::CLEARANCE_MIN * chassis_height,
                                      std::min(law::CLEARANCE_MAX * chassis_height, clearance_raw * law::CLEARANCE_SCALE));
    const double compression = interpolate(mass * law::WEIGHT_SCALE, law::SUSP_COMPRESSION_MIN_MASS, law::SUSP_COMPRESSION_MAX_MASS,
                                           law::SUSP_COMPRESSION_MIN, law::SUSP_COMPRESSION_MAX);
    const double rest_length = clearance / std::max(compression, 0.01);
    const double static_compression = std::max(0.01, rest_length - clearance);
    const double length_ratio = clearance / std::max(chassis_length, 0.01);
    const double hard_ratio = interpolate(length_ratio, law::CLEARANCE_TO_LENGTH_MIN, law::CLEARANCE_TO_LENGTH_MAX,
                                          law::HARD_RATIO_MIN, law::HARD_RATIO_MAX);
    // physics_shared._computeTrackLength.
    const double track_length = interpolate(length_ratio, law::CLEARANCE_TO_LENGTH_MIN, law::CLEARANCE_TO_LENGTH_MAX,
                                            law::TRACK_LENGTH_MAX, law::TRACK_LENGTH_MIN) * chassis_length;
    const double step_z = track_length / double(law::NUM_SPRING_PAIRS - 1);
    // Five carrier pairs from begZ = -trackLen/2, uniform stiffness, no authored damping.
    std::array<double, law::NUM_SPRING_PAIRS> positions{};
    for (int i = 0; i < law::NUM_SPRING_PAIRS; ++i) positions[i] = -track_length * 0.5 + step_z * double(i);
    const double damping_ratio = std::max(0.55, std::min(1.25, law::SERVER_PHYSICS_DAMPING_RATIO_BASE + law::SERVER_PHYSICS_DAMPING_RATIO_SCALE * 0.));
    const double track_x = width * law::SPRING_TRACK_WIDTH_RATIO;
    const double weight_total = 2. * law::NUM_SPRING_PAIRS;

    step::SuspensionDescriptor out;
    for (int side : {-1, 1}) {
        for (double z : positions) {
            const double sprung_mass = mass * 1. / weight_total;
            const double stiffness = sprung_mass * law::GRAVITY / static_compression;
            auto c = contact(side * track_x, 0., z, side, 0.);
            c.spring = true;
            c.stiffness = stiffness;
            c.damping = 2. * damping_ratio * std::sqrt(stiffness * sprung_mass);
            c.compression = static_compression;
            c.maximum = std::max(static_compression, rest_length - clearance * hard_ratio);
            c.force = sprung_mass * law::GRAVITY * law::SERVER_PHYSICS_MAX_SPRING_FORCE_FACTOR;
            c.rest_length = rest_length;
            c.footprint_front = std::min(step_z * 0.5, chassis_length * 0.5 - z);
            c.footprint_rear = std::min(step_z * 0.5, chassis_length * 0.5 + z);
            out.springs.push_back(c);
        }
    }
    for (int side : {-1, 1})
        for (int i = 0; i + 1 < law::NUM_SPRING_PAIRS; ++i) {
            auto c = contact(side * track_x, 0., (positions[i] + positions[i + 1]) * 0.5, side, law::TRACKS_PENETRATION);
            c.track = true;
            out.pseudo.push_back(c);
        }
    // Centre-line belly points keep a ridge between both tracks from vanishing between the spring queries.
    for (int i = 0; i + 1 < law::NUM_SPRING_PAIRS; ++i)
        out.pseudo.push_back(contact(0., clearance, (positions[i] + positions[i + 1]) * 0.5, 0, law::ALLOWED_PENETRATION));
    const double wheel = wheelRadius(chassis.get(), step_z);
    // The mounted hull envelope keeps side/roof contacts after the tank tips over.
    for (double x : {hull_box.minimum[0], hull_box.maximum[0]})
        for (double y : {hull_box.minimum[1], hull_box.maximum[1]})
            for (double z : {hull_box.minimum[2], hull_box.maximum[2]}) {
                auto c = contact(x + hull_position[0], y + hull_position[1], z + hull_position[2], 0, law::ALLOWED_PENETRATION);
                c.rigid = true;
                out.rigid.push_back(c);
            }
    try {
        auto turret = field(descriptor, "turret");
        const auto turret_box = box(turret.get(), "turret");
        auto zero = p::integer(0);
        auto index = field(descriptor, "activeTurretPosition", zero.get());
        auto positions_raw = field(hull.get(), "turretPositions");
        auto turret_mount = p::Ref(PyObject_GetItem(positions_raw.get(), index.get()));
        step::Point origin{};
        for (int i = 0; i < 3; ++i) origin[i] = hull_position[i] + toFloat(at(turret_mount.get(), i).get());
        out.turret_origin = origin;
        for (double x : {turret_box.minimum[0], turret_box.maximum[0]})
            for (double y : {turret_box.minimum[1], turret_box.maximum[1]})
                for (double z : {turret_box.minimum[2], turret_box.maximum[2]}) out.turret_contacts.push_back({{x, y, z}});
    } catch (const std::exception&) {
        // Descriptor-only fixtures may omit the independently mounted turret; the hull envelope remains.
        PyErr_Clear();
    }
    const double body_height = chassis_height;
    const auto& inertia = law::TRIAL_HULL_INERTIA_FACTORS;
    const double pitch_inertia = std::max(1., mass * (body_height * body_height + chassis_length * chassis_length) / 12.0 * inertia[0]);
    const double roll_inertia = std::max(1., mass * (body_height * body_height + width * width) / 12.0 * inertia[2]);
    out.parameters = {{mass, pitch_inertia, roll_inertia, law::SERVER_PHYSICS_STEP, double(law::SERVER_PHYSICS_CONSTRAINT_ITERATIONS)}};
    out.tuning = {{law::GRAVITY, law::CONTACT_PENETRATION, law::AIRBORNE_ANGULAR_DAMPING, law::AIRBORNE_ANGULAR_SPEED_LIMIT,
                   law::FREEZE_ACCEL_EPSILON, law::FREEZE_VEL_EPSILON, law::FREEZE_ANG_ACCEL_EPSILON, law::FREEZE_ANG_VEL_EPSILON,
                   double(law::SERVER_PHYSICS_MAX_SUBSTEPS)}};
    out.maximum_steps = law::SERVER_PHYSICS_MAX_SUBSTEPS;
    out.width = width;
    out.clearance = clearance;
    out.rest_length = rest_length;
    out.contact_memory_distance = std::max(0.25, std::min(0.9, wheel * 2.0));
    out.footprint_half_width = std::max(0.1, width * 0.06);
    return out;
}
}
}
