// Native remote Vehicle presentation: the factory and each Vehicle's pose, aim, Siege, motion and belt feed.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_remote_internal.hpp"
#include <algorithm>
#include <sstream>

namespace native_battle {
Remote::Remote() = default;
Remote::~Remote() = default;

namespace remote {
namespace {
constexpr double minimum_keyframe = 0.001;
constexpr std::int64_t siege_enabled = 2, siege_switching_off = 3;

bool flagOf(PyObject* o, const char* name, bool fallback = false) {
    auto v = py::optional(o, name);
    return v.get() == Py_None ? fallback : p::flag(v.get());
}
// _native_ypr: finite principal angles at the native matrix boundary.
Point principalYpr(double yaw, double pitch, double roll) {
    Point out{yaw, pitch, roll};
    for (auto& angle : out) {
        if (!std::isfinite(angle)) throw p::Error("remote pose angle must be finite");
        angle = principal(angle);
    }
    return out;
}
Object matrix(Battle& b) { return own(py::call(b.engine.math.get(), "Matrix")); }
void writePose(PyObject* m, const std::array<double, 6>& pose) {
    nativeMatrix(m, {pose[0], pose[1], pose[2]}, principalYpr(pose[3], pose[4], pose[5]), true);
}
std::array<double, 6> renderOf(const RemoteVehicle& rv) {
    return {rv.position[0], rv.position[1], rv.position[2], rv.yaw, rv.pitch, rv.roll};
}

// _write_changed_pose: write the changed parts of the persistent provider matrix, fenced by matrix identity.
void writeMatrix(RemoteVehicle& rv) {
    const Point ypr = principalYpr(rv.yaw, rv.pitch, rv.roll);
    const auto previous = rv.matrix_cache.get() == rv.matrix.get() ? rv.matrix_pose : std::nullopt;
    const bool rotated = !previous || (*previous)[3] != ypr[0] || (*previous)[4] != ypr[1] || (*previous)[5] != ypr[2];
    // A failed rotation/translation pair may have changed the provider; the next sample must repair it.
    rv.matrix_pose.reset();
    if (rotated || (*previous)[0] != rv.position[0] || (*previous)[1] != rv.position[1] || (*previous)[2] != rv.position[2])
        nativeMatrix(rv.matrix.get(), rv.position, ypr, rotated);
    rv.matrix_cache = borrow(rv.matrix.get());
    rv.matrix_pose = std::array<double, 6>{rv.position[0], rv.position[1], rv.position[2], ypr[0], ypr[1], ypr[2]};
}

void rebindProvider(Battle& b, RemoteVehicle& rv);

bool rekey(Battle& b, RemoteVehicle& rv, double relax_time) {
    if (!rv.animation) return false;
    try {
        p::Ref first(Py_BuildValue("(dO)", 0., rv.key_from.get()));
        p::Ref second(Py_BuildValue("(dO)", std::max(relax_time, minimum_keyframe), rv.key_to.get()));
        p::Ref frames(PyTuple_Pack(2, first.get(), second.get()));
        py::set(rv.animation.get(), "keyframes", frames.get());
        py::set(rv.animation.get(), "time", p::number(0.).get());
    } catch (const std::exception&) {
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
        PyErr_Clear();
        rv.animation = Object();
        rv.provider = rv.matrix;
        rebindProvider(b, rv);
        return false;
    }
    return true;
}

// The interpolated pose the animation shows at `now`.
std::array<double, 6> mirrorPose(const RemoteVehicle& rv, std::optional<double> now) {
    if (!rv.render_to) return rv.render_pose;
    if (!rv.render_from || !now || rv.render_duration <= 0.) return *rv.render_to;
    const double ratio = (*now - rv.render_started) / rv.render_duration;
    if (ratio >= 1.) return *rv.render_to;
    const double r = std::max(0., ratio);
    const auto& s = *rv.render_from;
    const auto& t = *rv.render_to;
    return {s[0] + (t[0] - s[0]) * r, s[1] + (t[1] - s[1]) * r, s[2] + (t[2] - s[2]) * r,
            blendAngle(s[3], t[3], r), blendAngle(s[4], t[4], r), blendAngle(s[5], t[5], r)};
}

bool retarget(Battle& b, RemoteVehicle& rv, std::optional<double> relax, std::optional<double> now) {
    const auto target = renderOf(rv);
    const double relax_time = relax.value_or(0.);
    if (!rv.animation) { rv.render_pose = target; return false; }
    const auto current_pose = mirrorPose(rv, now);
    if (relax_time <= 0.) {
        rv.render_from.reset();
        rv.render_to.reset();
        rv.render_pose = target;
        writePose(rv.key_from.get(), target);
        writePose(rv.key_to.get(), target);
        return rekey(b, rv, 0.);
    }
    if (current_pose == target) return false;
    writePose(rv.key_from.get(), current_pose);
    writePose(rv.key_to.get(), target);
    rv.render_from = current_pose;
    rv.render_to = target;
    rv.render_pose = current_pose;
    rv.render_started = now.value_or(0.);
    rv.render_duration = relax_time;
    return rekey(b, rv, relax_time);
}

void updateMotion(RemoteVehicle& rv, const Point& previous_position, double previous_yaw, std::optional<double> now) {
    Point velocity{}, acceleration{};
    double speed = 0., turn = 0.;
    if (now) {
        if (rv.last_pose_time && *now > *rv.last_pose_time) {
            const double elapsed = *now - *rv.last_pose_time;
            for (unsigned i = 0; i < 3; ++i) {
                velocity[i] = (rv.position[i] - previous_position[i]) / elapsed;
                acceleration[i] = (velocity[i] - rv.velocity[i]) / elapsed;
            }
            // BigWorld's yaw zero points down the model's +Z axis.
            speed = velocity[0] * std::sin(rv.yaw) + velocity[2] * std::cos(rv.yaw);
            turn = wrap(rv.yaw - previous_yaw) / elapsed;
        }
        rv.last_pose_time = *now;
    }
    rv.velocity = velocity;
    rv.acceleration = acceleration;
    rv.speed = speed;
    rv.turn_speed = turn;
}

Object relativeBody(Battle& b, PyObject* first, PyObject* second) {
    auto product = py::call(b.engine.math.get(), "MatrixProduct");
    py::set(product.get(), "a", first);
    if (second) py::set(product.get(), "b", second);
    return own(std::move(product));
}

// Relates native hydraulic body pitch to the canonical LAN ground.
bool prepareSiegePose(Battle& b, RemoteVehicle& rv) {
    auto descriptor = py::optional(rv.entity.get(), "typeDescriptor");
    if (!flagOf(descriptor.get(), "hasSiegeMode")) return false;
    if (rv.siege_relative_body) return true;
    auto inverse = py::optional(b.engine.math.get(), "MatrixInverse");
    if (!py::callable(inverse)) throw p::Error("#1513 Math.MatrixInverse is unavailable");
    auto filter = py::optional(rv.entity.get(), "filter");
    auto body = py::optional(filter.get(), "bodyMatrix");
    auto ground = py::optional(filter.get(), "groundPlacingMatrix");
    if (body.get() == Py_None || ground.get() == Py_None) throw p::Error("#1513 hydraulic vehicle matrices are unavailable");
    // Exact #1513 Vehicle.getComponents uses body * inverse(ground) for the hull, turret and gun.
    auto inverted = py::invoke(inverse.get(), ground.get());
    rv.siege_relative_body = relativeBody(b, body.get(), inverted.get());
    rv.siege_body = relativeBody(b, rv.siege_relative_body.get(), rv.matrix.get());
    return true;
}

std::pair<double, double> absolutePitchLimits(PyObject* descriptor) {
    auto gun = py::optional(descriptor, "gun");
    auto limits = py::optional(gun.get(), "pitchLimits");
    p::Ref absolute(p::retain(Py_None));
    if (PyDict_Check(limits.get())) {
        PyObject* raw = PyDict_GetItemString(limits.get(), "absolute");
        replace(absolute, p::retain(raw ? raw : Py_None));
    } else replace(absolute, py::optional(limits.get(), "absolute"));
    PyObject* low = absolute.get() != Py_None ? PySequence_GetItem(absolute.get(), 0) : nullptr;
    PyObject* high = low ? PySequence_GetItem(absolute.get(), 1) : nullptr;
    if (!high) {
        Py_XDECREF(low);
        PyErr_Clear();
        throw p::Error("#1513 hydraulic gun pitch limits are unavailable");
    }
    p::Ref l(low), h(high);
    const double minimum = PyFloat_AsDouble(l.get()), maximum = PyFloat_AsDouble(h.get());
    if (PyErr_Occurred()) { PyErr_Clear(); throw p::Error("#1513 hydraulic gun pitch limits are unavailable"); }
    if (minimum > maximum) throw p::Error("#1513 hydraulic gun pitch limits are invalid");
    return {minimum, maximum};
}

bool hasSiege(const RemoteVehicle& rv) {
    return flagOf(py::optional(rv.entity.get(), "typeDescriptor").get(), "hasSiegeMode");
}
bool siegeActive(const RemoteVehicle& rv) {
    auto state = py::optional(rv.entity.get(), "siegeState");
    const std::int64_t value = state.get() == Py_None ? 0 : p::wide(state.get());
    return value == siege_enabled || value == siege_switching_off;
}

// Feeds #1513 hydraulics while preserving its body/ground split (authority geometry only).
bool siegePose(Battle& b, RemoteVehicle& rv) {
    if (!rv.authority_geometry || !hasSiege(rv)) return false;
    prepareSiegePose(b, rv);
    const bool active = siegeActive(rv);
    auto desired = rv.aim_desired_gun_pitch;
    auto visible = desired;
    double delta = 0.;
    if (active && desired) {
        const double local_desired = *desired - rv.pitch;
        const auto limits = absolutePitchLimits(py::attr(rv.entity.get(), "typeDescriptor").get());
        visible = std::max(limits.first, std::min(limits.second, local_desired));
        delta = wrap(local_desired - *visible);
    }
    if (visible) writeAimPitch(rv, *visible);
    auto filter = py::optional(rv.entity.get(), "filter");
    auto get_physics = py::optional(filter.get(), "getVehiclePhysics");
    if (!py::callable(get_physics)) throw p::Error("#1513 Siege vehicle physics boundary is unavailable");
    auto physics = py::invoke(get_physics.get());
    if (physics.get() == Py_None) return false;
    auto set_delta = py::optional(physics.get(), "setHullAimingAnglesDelta");
    if (!py::callable(set_delta)) throw p::Error("#1513 hydraulic aiming input boundary is unavailable");
    // The exact x86 wrapper takes yaw first and pitch second.
    py::invoke(set_delta.get(), p::number(0.).get(), p::number(delta).get());
    return true;
}

void rebindProvider(Battle& b, RemoteVehicle& rv) {
    if (!rv.entity || py::optional(rv.entity.get(), "model").get() == Py_None) return;
    py::set(py::attr(rv.entity.get(), "model").get(), "matrix", rv.provider.get());
    rv.track_feed.reset();
    bindStockMotion(b, rv);
    publishPose(rv);
}

bool presentShotImpulse(Battle& b, PyObject* appearance, PyObject* direction, double impulse) {
    if (!appearance || appearance == Py_None || py::optional(appearance, "swingingAnimator").get() == Py_None) return false;
    auto receive = py::optional(appearance, "receiveShotImpulse");
    if (!py::callable(receive)) return false;
    Point values{};
    for (unsigned i = 0; i < 3; ++i) {
        PyObject* raw = PyObject_HasAttrString(direction, "x") ? PyObject_GetAttrString(direction, i == 0 ? "x" : i == 1 ? "y" : "z")
                                                               : PySequence_GetItem(direction, i);
        if (!raw) { PyErr_Clear(); return false; }
        p::Ref held(raw);
        values[i] = PyFloat_AsDouble(raw);
        if (PyErr_Occurred()) { PyErr_Clear(); return false; }
    }
    if (!(impulse > 0.) || !std::isfinite(impulse)) return false;
    for (double v : values) if (!std::isfinite(v)) return false;
    const double length = std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2]);
    if (length <= 1.e-6) return false;
    // Retail scales the hit axis by the impulse; normalise this runtime's contact direction.
    auto axis = py::call(b.engine.math.get(), "Vector3", p::number(values[0] / length).get(), p::number(values[1] / length).get(),
                         p::number(values[2] / length).get());
    py::invoke(receive.get(), axis.get(), p::number(impulse).get());
    return true;
}

void retireFailedCreates(Battle& b) {
    auto& r = *b.remote;
    std::optional<std::string> first;
    for (auto it = r.failed_creates.begin(); it != r.failed_creates.end();) {
        if (!engineOwns(b, *it)) { ++it; continue; }
        try {
            py::call(b.engine.binding.get(), "destroy_entity", p::integer(*it).get());
            it = r.failed_creates.erase(it);
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            if (!first) first = describe(e);
            ++it;
        }
    }
    if (first) throw p::Error(*first);
}
}

RemoteVehicle* vehicle(const Battle& b, std::int64_t engine_id) {
    if (!b.remote) return nullptr;
    auto found = b.remote->vehicles.find(engine_id);
    return found == b.remote->vehicles.end() ? nullptr : found->second.get();
}

void recordCapability(RemoteVehicle& rv, const std::string& name, bool available, const std::string& error) {
    auto found = rv.capabilities.find(name);
    if (found == rv.capabilities.end() || found->second != available) rv.capabilities_changed = true;
    rv.capabilities[name] = available;
    if (error.empty()) rv.errors.erase(name);
    else rv.errors.emplace(name, error);
}

// One line per distinct set of stock binding outcomes.
void publishCapabilities(Battle& b, RemoteVehicle& rv) {
    if (!rv.capabilities_changed) return;
    rv.capabilities_changed = false;
    if (rv.capabilities.empty()) return;
    std::ostringstream signature;
    bool space = false;
    for (const auto& item : rv.capabilities) {
        signature << (space ? " " : "") << item.first << "=" << (item.second ? "True" : "False");
        space = true;
    }
    if (!b.remote->reported_capabilities.insert(signature.str()).second) return;
    std::ostringstream errors;
    if (!rv.errors.empty()) {
        errors << " errors={";
        bool comma = false;
        for (const auto& item : rv.errors) { errors << (comma ? ", " : "") << "'" << item.first << "': '" << item.second << "'"; comma = true; }
        errors << "}";
    }
    native_battle::log("PRESENTATION " + signature.str() + errors.str());
}

bool writeAimPitch(RemoteVehicle& rv, double gun_pitch) {
    if (rv.aim_gun_pitch && *rv.aim_gun_pitch == gun_pitch) return false;
    nativeMatrix(py::attr(rv.aim.get(), "gunMatrix").get(), {}, {0., gun_pitch, 0.}, true);
    rv.aim_gun_pitch = gun_pitch;
    return true;
}

void publishPose(RemoteVehicle& rv) {
    if (!rv.entity) return;
    p::Ref position(nativeVector(rv.position)), velocity(nativeVector(rv.velocity)), acceleration(nativeVector(rv.acceleration));
    OverlayPose pose{position.get(), rv.yaw, rv.provider.get(), rv.speed, rv.turn_speed, velocity.get(), acceleration.get()};
    publishOverlay(rv.entity.get(), pose);
}

// Rebinds stock #1513 presentation components to the copied LAN motion.
void bindStockMotion(Battle& b, RemoteVehicle& rv) {
    auto appearance = py::optional(rv.entity.get(), "appearance");
    if (appearance.get() == Py_None) return;
    bool engine_ready = false;
    if (py::optional(appearance.get(), "detailedEngineState").get() != Py_None) {
        try { engine_ready = bindEngineMotion(appearance.get(), rv.speed_link.get(), rv.rotation_link.get()); }
        catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            recordCapability(rv, "engine_audio_motion", false, describe(e));
        }
    }
    if (engine_ready) recordCapability(rv, "engine_audio_motion", true);
    else if (!rv.errors.count("engine_audio_motion")) recordCapability(rv, "engine_audio_motion", false, "stock DetailedEngineState is unavailable");

    auto swinging = py::optional(appearance.get(), "swingingAnimator");
    if (swinging.get() != Py_None) {
        p::Ref placing(p::retain(Py_None)), world(p::retain(Py_None));
        bool read = false;
        try {
            replace(placing, py::attr(swinging.get(), "placingCompensationMatrix"));
            replace(world, py::attr(swinging.get(), "worldMatrix"));
            read = true;
            // The copied provider already carries ground pitch and roll; apply placement once.
            py::set(swinging.get(), "placingCompensationMatrix", rv.identity_placing.get());
            py::set(swinging.get(), "worldMatrix", rv.provider.get());
            recordCapability(rv, "body_swinging", true);
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            std::string error = describe(e);
            if (read) {
                std::string rollback;
                for (auto item : {std::make_pair("placingCompensationMatrix", placing.get()), std::make_pair("worldMatrix", world.get())})
                    if (PyObject_SetAttrString(swinging.get(), item.first, item.second) < 0) {
                        rollback += (rollback.empty() ? "" : "; ") + std::string(item.first) + ": " + describe(p::Error("rollback"));
                    }
                if (!rollback.empty()) error += "; rollback failed: " + rollback;
            }
            recordCapability(rv, "body_swinging", false, error);
        }
    } else recordCapability(rv, "body_swinging", false, "stock SwingingAnimator is unavailable");

    // Stock binds the LodCalculator to the filter's spawn pose; repeat its public binding for our provider.
    auto lod = py::optional(appearance.get(), "lodCalculator");
    auto links = py::optional(b.engine.modules.get(), "data_links");
    auto link_translation = py::optional(links.get(), "linkMatrixTranslation");
    auto setup = py::optional(lod.get(), "setupPosition");
    bool lod_ready = false;
    if (py::callable(link_translation) && py::callable(setup)) {
        try {
            py::invoke(setup.get(), py::invoke(link_translation.get(), rv.provider.get()).get());
            lod_ready = true;
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            recordCapability(rv, "stock_motion_lod", false, describe(e));
        }
    }
    if (lod_ready) recordCapability(rv, "stock_motion_lod", true);
    else if (!rv.errors.count("stock_motion_lod")) recordCapability(rv, "stock_motion_lod", false, "stock LodCalculator/DataLinks are unavailable");

    const bool wheels = py::optional(appearance.get(), "wheelsAnimator").get() != Py_None;
    auto suspension = py::optional(appearance.get(), "suspension");
    if (suspension.get() == Py_None) replace(suspension, py::optional(appearance.get(), "leveredSuspension"));
    recordCapability(rv, "stock_wheels", wheels, wheels ? "" : "stock WheelsAnimator is unavailable");
    recordCapability(rv, "stock_suspension", suspension.get() != Py_None, suspension.get() != Py_None ? "" : "stock suspension is unavailable");
    publishCapabilities(b, rv);
}

void attachVehicle(Battle& b, RemoteVehicle& rv, PyObject* entity) {
    rv.entity = borrow(entity);
    py::set(entity, "_offlineNativeRemote", Py_True);
    py::set(entity, "targetCaps", p::list().get());
    py::set(entity, "_offlineNativeMarkerVisible", flagOf(entity, "_offlineNativeMarkerVisible", true) ? Py_True : Py_False);
    py::set(entity, "_offlineNativeDrawVisible", flagOf(entity, "_offlineNativeDrawVisible", true) ? Py_True : Py_False);
    py::set(entity, "_aim_yaw", p::number(rv.yaw).get());
    py::set(entity, "_gun_pitch", p::number(0.).get());
    auto info = py::attr(entity, "publicInfo");
    p::Ref team(PyObject_GetItem(info.get(), p::text("team").get()));
    py::set(entity, "team", p::integer(p::wide(p::Ref(PyNumber_Int(team.get())).get())).get());
    py::set(entity, "bw_entity_id", p::integer(p::wide(p::Ref(PyNumber_Int(py::attr(entity, "id").get())).get())).get());
    py::set(py::attr(entity, "model").get(), "matrix", rv.provider.get());
    publishPose(rv);
    auto appearance = py::attr(entity, "appearance");
    rv.track_scroll = own(py::optional(appearance.get(), "_CompoundAppearance__trackScrollCtl"));
    py::set(entity, "track_scroll", rv.track_scroll.get());
    auto setup = py::optional(appearance.get(), "setupGunMatrixTargets");
    if (!py::callable(setup)) throw p::Error("#1513 CompoundAppearance aim-target boundary is unavailable");
    py::invoke(setup.get(), rv.aim.get());
    bindStockMotion(b, rv);
    if (rv.authority_geometry && hasSiege(rv)) {
        prepareSiegePose(b, rv);
        siegePose(b, rv);
    }
    const std::int64_t id = rv.engine_id;
    rv.model_changed = callback([id](PyObject*, PyObject*) -> PyObject* {
        Battle* battle = current();
        RemoteVehicle* found = battle ? vehicle(*battle, id) : nullptr;
        if (!found || !found->entity) Py_RETURN_NONE;
        auto appearance = py::optional(found->entity.get(), "appearance");
        if (appearance.get() == Py_None) Py_RETURN_NONE;
        rebindProvider(*battle, *found);
        py::call(appearance.get(), "setupGunMatrixTargets", found->aim.get());
        // Stock may replace the scroll controller with the chassis fashion; replay engine mode and speed next feed.
        found->track_scroll = own(py::optional(appearance.get(), "_CompoundAppearance__trackScrollCtl"));
        py::set(found->entity.get(), "track_scroll", found->track_scroll.get());
        found->track_mode.reset();
        found->track_feed.reset();
        // A refreshed model restores stock visibility; reapply the spotting gates and drop the native picker.
        setDrawVisibility(*battle, found->entity.get(), flagOf(found->entity.get(), "_offlineNativeDrawVisible", true));
        py::set(found->entity.get(), "targetCaps", p::list().get());
        Py_RETURN_NONE;
    });
    auto changed = py::optional(appearance.get(), "onModelChanged");
    if (changed.get() != Py_None) p::Ref unused(PyNumber_InPlaceAdd(changed.get(), rv.model_changed.get()));
}

void detachVehicle(Battle& b, RemoteVehicle& rv) {
    if (!rv.entity) return;
    const bool owned = engineOwnsEntity(rv.engine_id, rv.entity.get());
    p::Ref appearance(p::retain(Py_None));
    if (owned) {
        replace(appearance, py::optional(rv.entity.get(), "appearance"));
        auto changed = py::optional(appearance.get(), "onModelChanged");
        if (changed.get() != Py_None && rv.model_changed) {
            PyObject* done = PyNumber_InPlaceSubtract(changed.get(), rv.model_changed.get());
            if (done) Py_DECREF(done);
            else PyErr_Clear();
        }
    }
    // The overlay clear is the last native-facing operation; keep owners intact if it fails.
    clearOverlay(rv.entity.get());
    clearAppearanceState(appearance.get());
    rv.model_changed = Object();
    rv.entity = Object();
}

void install(Battle& b, bool interpolate_motion, bool authority_geometry) {
    b.remote.reset(new Remote());
    b.remote->interpolate_motion = interpolate_motion;
    b.remote->authority_geometry = authority_geometry;
}

// ---- factory ----------------------------------------------------------------------------------------------

p::Ref prepareDescriptor(Battle& b, PyObject* descriptor) {
    // Stock Vehicle prerequisites and CompoundAppearance own the BSP references; pin the descriptor.
    b.remote->descriptors[descriptor] = borrow(descriptor);
    return p::retain(descriptor);
}

std::int64_t create(Battle& b, PyObject* descriptor, PyObject* properties, const Point& position, double yaw) {
    auto& r = *b.remote;
    retireFailedCreates(b);
    prepareDescriptor(b, descriptor);
    p::Ref at(nativeVector(position));
    p::Ref rotation(Py_BuildValue("(ddd)", 0., 0., yaw));
    auto raw = py::call(b.engine.binding.get(), "create_vehicle", properties, at.get(), rotation.get());
    if (raw.get() == Py_None) throw p::Error("native remote createEntity returned no id");
    const std::int64_t id = p::wide(raw.get());
    // #1513 enters client-created Vehicles asynchronously; a re-entrant entry cannot be completed.
    if (py::call(b.engine.bigworld.get(), "entity", raw.get()).get() != Py_None) {
        py::call(b.engine.binding.get(), "destroy_entity", raw.get());
        throw p::Error("native remote Vehicle entered before createEntity returned");
    }
    auto rv = std::make_unique<RemoteVehicle>();
    rv->engine_id = id;
    rv->authority_geometry = r.authority_geometry;
    rv->position = position;
    const Point ypr = principalYpr(yaw, 0., 0.);
    rv->yaw = ypr[0];
    rv->matrix = matrix(b);
    writeMatrix(*rv);
    rv->identity_placing = matrix(b);
    py::call(rv->identity_placing.get(), "setIdentity");
    rv->key_from = own(py::call(b.engine.math.get(), "Matrix", rv->matrix.get()));
    rv->key_to = own(py::call(b.engine.math.get(), "Matrix", rv->matrix.get()));
    rv->render_pose = renderOf(*rv);
    auto factory = py::optional(b.engine.math.get(), "MatrixAnimation");
    if (r.interpolate_motion && py::callable(factory)) rv->animation = own(py::invoke(factory.get()));
    rv->provider = rv->animation ? rv->animation : rv->matrix;
    rv->aim = aimTarget(b);
    rv->speed_link = callback([id](PyObject*, PyObject*) -> PyObject* {
        Battle* battle = current();
        RemoteVehicle* found = battle ? vehicle(*battle, id) : nullptr;
        return PyFloat_FromDouble(found ? found->speed : 0.);
    });
    rv->rotation_link = callback([id](PyObject*, PyObject*) -> PyObject* {
        Battle* battle = current();
        RemoteVehicle* found = battle ? vehicle(*battle, id) : nullptr;
        return PyFloat_FromDouble(found ? found->turn_speed : 0.);
    });
    RemoteVehicle& created = *rv;
    r.vehicles[id] = std::move(rv);
    if (created.animation) rekey(b, created, minimum_keyframe);
    try {
        auto snapshot = p::dict();
        p::set(snapshot.get(), "properties", p::retain(properties));
        p::set(snapshot.get(), "team_killer", p::boolean(false));
        py::call(b.engine.binding.get(), "arena_vehicle_added", raw.get(), snapshot.get());
    } catch (const std::exception& e) {
        // The caller never receives this id; retire it once BigWorld owns it.
        const std::string error = describe(e);
        r.vehicles.erase(id);
        r.failed_creates.insert(id);
        try { retireFailedCreates(b); }
        catch (const std::exception& cleanup) {
            throw p::Error("native remote arena registration failed: " + error + "; entity cleanup failed: " + describe(cleanup));
        }
        throw p::Error(error);
    }
    return id;
}

p::Ref get(const Battle& b, std::int64_t engine_id) {
    if (RemoteVehicle* rv = vehicle(b, engine_id)) if (rv->entity) return p::retain(rv->entity.get());
    return py::call(b.engine.bigworld.get(), "entity", p::integer(engine_id).get());
}

bool isReady(Battle& b, std::int64_t engine_id) {
    auto entity = py::call(b.engine.bigworld.get(), "entity", p::integer(engine_id).get());
    if (entity.get() == Py_None || !flagOf(entity.get(), "inWorld") || !flagOf(entity.get(), "isStarted") ||
        py::optional(entity.get(), "model").get() == Py_None || py::optional(entity.get(), "appearance").get() == Py_None ||
        py::optional(entity.get(), "typeDescriptor").get() == Py_None)
        return false;
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv) return false;
    if (!rv->entity) attachVehicle(b, *rv, entity.get());
    return true;
}

bool engineOwns(const Battle& b, std::int64_t engine_id) {
    auto entities = py::optional(b.engine.bigworld.get(), "entities");
    auto lookup = py::optional(entities.get(), "get");
    return py::callable(lookup) && py::invoke(lookup.get(), p::integer(engine_id).get()).get() != Py_None;
}

bool engineActive(const Battle& b) {
    if (!b.remote) return false;
    for (const auto& item : b.remote->vehicles) if (engineOwns(b, item.first)) return true;
    for (auto id : b.remote->failed_creates) if (engineOwns(b, id)) return true;
    return false;
}

void destroy(Battle& b, std::int64_t engine_id) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv) return;
    detachVehicle(b, *rv);
    if (engineOwns(b, engine_id)) py::call(b.engine.binding.get(), "destroy_entity", p::integer(engine_id).get());
    // Native entity destruction is the ownership commit boundary.
    b.remote->vehicles.erase(engine_id);
}

void destroyAll(Battle& b) {
    if (!b.remote) return;
    std::optional<std::string> first;
    try { retireFailedCreates(b); }
    catch (const std::exception& e) { first = describe(e); }
    std::vector<std::int64_t> ids;
    for (const auto& item : b.remote->vehicles) ids.push_back(item.first);
    for (auto id : ids) {
        try { destroy(b, id); }
        catch (const std::exception& e) { if (!first) first = describe(e); }
    }
    b.remote->descriptors.clear();
    clearOverlays();
    if (first) throw p::Error(*first);
}

// Vehicle.onHealthChanged owns the stock damaged-model replacement.
void requestWreck(Battle&, std::int64_t) {}

bool setInterpolateMotion(Battle& b, std::int64_t engine_id, bool enabled) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv || enabled == bool(rv->animation)) return false;
    const Object previous = rv->provider;
    const auto pose = renderOf(*rv);
    rv->render_pose = pose;
    rv->render_from.reset();
    rv->render_to.reset();
    rv->render_duration = 0.;
    writePose(rv->key_from.get(), pose);
    writePose(rv->key_to.get(), pose);
    if (enabled) {
        auto factory = py::optional(b.engine.math.get(), "MatrixAnimation");
        if (py::callable(factory)) {
            rv->animation = own(py::invoke(factory.get()));
            rv->provider = rv->animation;
            rekey(b, *rv, minimum_keyframe);
        }
    } else {
        rv->animation = Object();
        rv->provider = rv->matrix;
    }
    if (rv->provider.get() == previous.get()) return false;
    rebindProvider(b, *rv);
    return true;
}

bool setPose(Battle& b, std::int64_t engine_id, const Point& position, double yaw, double pitch, double roll,
             std::optional<double> relax_time, std::optional<double> now) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv) return false;
    const Point ypr = principalYpr(yaw, pitch, roll);
    const Point previous_position = rv->position;
    const double previous_yaw = rv->yaw;
    rv->position = position;
    rv->yaw = ypr[0];
    rv->pitch = ypr[1];
    rv->roll = ypr[2];
    writeMatrix(*rv);
    retarget(b, *rv, relax_time, now);
    updateMotion(*rv, previous_position, previous_yaw, now);
    if (rv->entity) {
        if (rv->authority_geometry && hasSiege(*rv)) siegePose(b, *rv);
        publishPose(*rv);
    }
    return true;
}

bool setAim(Battle& b, std::int64_t engine_id, double hull_yaw, double aim_yaw, double gun_pitch) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv) return false;
    double turret_yaw = wrap(aim_yaw - hull_yaw), component_pitch = gun_pitch;
    auto descriptor = py::optional(rv->entity.get(), "typeDescriptor");
    auto gun = PyDict_Check(descriptor.get()) ? p::value(descriptor.get(), "gun") : py::optional(descriptor.get(), "gun");
    auto static_yaw = PyDict_Check(gun.get()) ? p::value(gun.get(), "staticTurretYaw") : py::optional(gun.get(), "staticTurretYaw");
    auto static_pitch = PyDict_Check(gun.get()) ? p::value(gun.get(), "staticPitch") : py::optional(gun.get(), "staticPitch");
    if (static_yaw.get() != Py_None) turret_yaw = p::scalar(static_yaw.get());
    if (static_pitch.get() != Py_None) component_pitch = p::scalar(static_pitch.get());
    if (!rv->aim_relative_yaw || *rv->aim_relative_yaw != turret_yaw) {
        nativeMatrix(py::attr(rv->aim.get(), "turretMatrix").get(), {}, {turret_yaw, 0., 0.}, true);
        rv->aim_relative_yaw = turret_yaw;
    }
    rv->aim_desired_gun_pitch = component_pitch;
    if (rv->authority_geometry && rv->entity && hasSiege(*rv)) siegePose(b, *rv);
    else writeAimPitch(*rv, component_pitch);
    if (rv->entity) {
        py::set(rv->entity.get(), "_aim_yaw", p::number(aim_yaw).get());
        py::set(rv->entity.get(), "_gun_pitch", p::number(gun_pitch).get());
    }
    return true;
}

bool settleMotion(Battle& b, std::int64_t engine_id, std::optional<double> now) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv) return false;
    if (now) rv->last_pose_time = *now;
    rv->velocity = rv->acceleration = Point{};
    rv->speed = rv->turn_speed = 0.;
    publishPose(*rv);
    return true;
}

bool updateSiegePose(Battle& b, std::int64_t engine_id) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    return rv && rv->entity && siegePose(b, *rv);
}

std::optional<std::pair<Object, Object>> collisionMatrices(Battle& b, std::int64_t engine_id, PyObject* ground) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv || !rv->entity) return std::nullopt;
    const Object canonical = ground ? borrow(ground) : rv->matrix;
    if (!rv->authority_geometry || !hasSiege(*rv) || !siegeActive(*rv)) return std::make_pair(canonical, canonical);
    prepareSiegePose(b, *rv);
    const Object body = ground ? relativeBody(b, rv->siege_relative_body.get(), ground) : rv->siege_body;
    return std::make_pair(body, canonical);
}

bool presentHitImpulse(Battle& b, std::int64_t engine_id, PyObject* direction, double impulse) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv || !rv->entity) return false;
    // Once BigWorld releases the PyEntity, reading its appearance can touch freed native memory.
    if (!engineOwnsEntity(rv->engine_id, rv->entity.get())) return false;
    auto swinging = rv->capabilities.find("body_swinging");
    if (swinging == rv->capabilities.end() || !swinging->second) return false;
    return presentShotImpulse(b, py::optional(rv->entity.get(), "appearance").get(), direction, impulse);
}

// Belt speed and engine mode for a vehicle the replica does not own.
bool updateTracks(Battle& b, std::int64_t engine_id, double left, double right, std::array<int, 2> mode) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv) return false;
    if (!rv->entity) { rv->track_outcome = "detached"; return false; }
    if (!engineOwnsEntity(rv->engine_id, rv->entity.get())) {
        rv->capabilities["engine_owned_track_motion"] = false;
        rv->errors.emplace("engine_owned_track_motion", "BigWorld no longer owns the Vehicle entity");
        rv->track_outcome = "not engine owned";
        return false;
    }
    auto appearance = py::optional(rv->entity.get(), "appearance");
    if (appearance.get() == Py_None) { rv->track_outcome = "no appearance"; return false; }
    // An unseen vehicle has no presentation writer in retail; settle the belts once on the hide edge.
    const bool hidden = !flagOf(rv->entity.get(), "_offlineNativeDrawVisible", true);
    if (hidden) {
        if (rv->track_feed_hidden) { rv->track_outcome = "hidden and settled"; return false; }
        rv->track_feed_hidden = true;
        left = right = 0.;
        rv->track_feed.reset();
    } else if (rv->track_feed_hidden) {
        rv->track_feed_hidden = false;
        rv->track_feed.reset();
        rv->track_mode.reset();
    }
    auto flying = py::optional(appearance.get(), "flyingInfoProvider");
    const bool left_contact = !flagOf(flying.get(), "isLeftSideFlying"), right_contact = !flagOf(flying.get(), "isRightSideFlying");
    const std::array<double, 4> feed{std::round(left * 1000.) / 1000., std::round(right * 1000.) / 1000., double(mode[0]), double(mode[1])};
    const std::array<bool, 2> contacts{left_contact, right_contact};
    if (rv->track_feed == feed && rv->track_feed_contacts == contacts) {
        rv->track_outcome = "deduplicated";
        auto owned = rv->capabilities.find("engine_owned_track_motion");
        return owned != rv->capabilities.end() && owned->second;
    }
    bool native_updated = false;
    auto filter = py::optional(rv->entity.get(), "filter");
    auto appearance_filter = py::optional(appearance.get(), "filter");
    auto setter = py::optional(filter.get(), "setTracksSpeed");
    auto movement = py::optional(filter.get(), "movementInfo");
    if (appearance_filter.get() == filter.get() && py::callable(setter) && movement.get() != Py_None) {
        try {
            // Fatal on an unattached WGVehicleFilter; the ownership checks above are part of the ABI boundary.
            py::invoke(setter.get(), p::number(left).get(), left_contact ? Py_True : Py_False, p::number(right).get(),
                       right_contact ? Py_True : Py_False);
            native_updated = true;
            recordCapability(*rv, "engine_owned_track_motion", true);
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            recordCapability(*rv, "engine_owned_track_motion", false, describe(e));
        }
    } else recordCapability(*rv, "engine_owned_track_motion", false, "Appearance/filter ownership or movementInfo is unavailable");
    publishCapabilities(b, *rv);
    if (!hidden && rv->track_mode != mode) {
        applyTrackMode(appearance.get(), mode);
        rv->track_mode = mode;
    }
    applyTrackScroll(appearance.get(), left, right);
    if (native_updated) { rv->track_feed = feed; rv->track_feed_contacts = contacts; }
    rv->track_written = std::make_pair(left, right);
    rv->track_outcome = hidden ? "hidden and settling" : native_updated ? "engine and python" : "python only";
    return native_updated;
}

std::optional<TrackFeedState> trackFeedState(const Battle& b, std::int64_t engine_id) {
    RemoteVehicle* rv = vehicle(b, engine_id);
    if (!rv) return std::nullopt;
    TrackFeedState out;
    out.written = rv->track_written;
    out.outcome = rv->track_outcome;
    out.hidden = rv->track_feed_hidden;
    auto owned = rv->capabilities.find("engine_owned_track_motion");
    if (owned != rv->capabilities.end()) out.engine_owned = owned->second;
    return out;
}

// The three belt inputs of vehicle_physics.derive_params a remote track feed reads.
TrackParams trackParams(PyObject* descriptor) {
    TrackParams out;
    out.speed_cap = 32. / 3.6;
    auto physics = py::optional(descriptor, "physics");
    auto item = [&](const char* name) -> p::Ref {
        if (physics.get() == Py_None) return p::retain(Py_None);
        PyObject* v = PyObject_GetItem(physics.get(), p::text(name).get());
        if (!v) { PyErr_Clear(); return p::retain(Py_None); }
        return p::Ref(v);
    };
    auto limits = item("speedLimits");
    if (limits.get() != Py_None) {
        PyObject* first = PySequence_GetItem(limits.get(), 0);
        if (first) { out.speed_cap = PyFloat_AsDouble(first); Py_DECREF(first); }
        PyErr_Clear();
    }
    auto offset = item("trackCenterOffset");
    double gauge = 0.;
    if (offset.get() != Py_None) {
        gauge = std::fabs(PyFloat_AsDouble(offset.get()));
        if (PyErr_Occurred()) { PyErr_Clear(); gauge = 0.; }
        if (gauge >= .3 && gauge <= 3.) out.center = gauge;
    }
    auto chassis = py::optional(descriptor, "chassis");
    if (chassis.get() != Py_None) {
        auto around = PyDict_Check(chassis.get()) ? p::value(chassis.get(), "rotationIsAroundCenter")
                                                  : py::optional(chassis.get(), "rotationIsAroundCenter");
        out.around_center = around.get() == Py_None ? true : p::flag(around.get());
        if (!out.around_center) {
            if (!PyBool_Check(around.get()) || !(gauge > 0. && gauge <= 100.)) throw p::Error("#1513 track pivot geometry is unavailable");
            out.center = gauge;
        }
        auto rotation = PyDict_Check(chassis.get()) ? p::value(chassis.get(), "rotationSpeed") : py::optional(chassis.get(), "rotationSpeed");
        if (rotation.get() == Py_None) throw p::Error("#1513 chassis rotation speed is unavailable");
    }
    out.speed_cap *= .995;
    return out;
}
}
}
