// The player's own vehicle: presentation lifecycle, Siege, settings, contact hook and the per-frame warnings.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_local.hpp"
#include "stock_native_client_survival_python.hpp"
#include "stock_native_client_critical.hpp"
#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include <cmath>

namespace native_battle {
namespace params {
// Provided by the params subsystem: the player's own attribute factors, and the replay's accepted effective params.
p::Ref localFactors(Battle&, PyObject* descriptor);
Value acceptedLocalEffectiveParams(Battle&);
}
namespace gun {
// Provided by the projectiles/gun subsystem: shell, partial-clip and equipment vehicle settings.
bool changeSetting(Battle&, int code, PyObject* value);
}
namespace vehicles {
// Provided by the vehicles subsystem: publishes the native rotator angle as the server gun echo.
bool echoLocalGunAngles(Battle&, std::optional<double> turret_yaw, std::optional<double> gun_pitch);
}
namespace local {
namespace dr = stock_native_client_drive;
namespace {
p::Ref obj(const Object& o) { return p::retain(o ? o.get() : Py_None); }
p::Ref construct(Battle& b, const char* type, PyObject* argument = nullptr) {
    auto ctor = py::attr(b.engine.math.get(), type);
    return argument ? py::invoke(ctor.get(), argument) : py::invoke(ctor.get());
}
p::Ref vector(Battle& b, const Point& v) {
    p::Ref args(Py_BuildValue("(ddd)", v[0], v[1], v[2]));
    auto ctor = py::attr(b.engine.math.get(), "Vector3");
    return p::Ref(PyObject_CallObject(ctor.get(), args.get()));
}
// Math.MatrixProduct over the given providers.
Object product(Battle& b, PyObject* first, PyObject* second = nullptr) {
    auto value = construct(b, "MatrixProduct");
    py::set(value.get(), "a", first);
    if (second) py::set(value.get(), "b", second);
    return own(std::move(value));
}
const Object& bodyPose(const LocalVehicle& l) { return l.pose_matrix ? l.pose_matrix : l.matrix; }
const Object& stabilisedPose(const LocalVehicle& l) { return l.stabilised_snapshot ? l.stabilised_snapshot : bodyPose(l); }
const Object& steadyRotation(const LocalVehicle& l) { return l.steady_rotation_matrix ? l.steady_rotation_matrix : bodyPose(l); }
bool hasSiege(PyObject* descriptor) { return p::flag(py::optional(descriptor, "hasSiegeMode").get()); }
p::Ref playerEntity(Battle& b) {
    if (!b.engine.server) return p::retain(Py_None);
    return b.entity(p::wide(py::attr(b.engine.server.get(), "vehicle_id").get()));
}
// compatibility.set_vehicle_pose_overlay with the copied pose and its matrix providers.
void poseOverlay(Battle& b, PyObject* entity, PyObject* motion) {
    const auto& l = *b.local;
    auto position = vector(b, l.position);
    p::Ref args(Py_BuildValue("(OOdOddOO)", entity, position.get(), l.yaw, bodyPose(l).get(), l.speed, l.turn_speed, motion, motion));
    auto kwargs = p::dict();
    p::set(kwargs.get(), "steady_rotation_matrix", obj(steadyRotation(l)));
    p::set(kwargs.get(), "stabilised_matrix", obj(stabilisedPose(l)));
    auto overlay = py::attr(b.engine.compatibility.get(), "set_vehicle_pose_overlay");
    p::Ref done(PyObject_Call(overlay.get(), args.get(), kwargs.get()));
}
void resetSuspension(LocalVehicle& l, bool disabled) {
    l.suspension_params = Object();
    l.suspension_disabled = disabled;
}
void reportSuspension(LocalVehicle& l, const std::string& outcome) {
    const std::pair<bool, std::string> record{true, outcome};
    if (l.suspension_report == record) return;
    l.suspension_report = record;
    log("SUSPENSION player trial " + outcome);
}
void reportSiegeEdge(Battle& b, const char* stage, PyObject* state, PyObject* left = Py_None, PyObject* sequence = Py_None) {
    auto& l = *b.local;
    if (l.siege_edge_reports >= 128) return;
    ++l.siege_edge_reports;
    auto text = [](PyObject* v) { p::Ref r(PyObject_Repr(v ? v : Py_None)); return p::string(r.get()); };
    auto access = dr::driverAccess(b);
    const auto& s = access.driver->state();
    const auto& c = access.driver->controls();
    std::string limits = "(None,None)";
    if (l.physics && PyDict_Check(l.physics.get())) {
        auto fwd = p::value(l.physics.get(), "speedFwd"), bwd = p::value(l.physics.get(), "speedBwd");
        limits = "(" + text(fwd.get()) + "," + text(bwd.get()) + ")";
    }
    std::string pending = s.siege_pending ? (*s.siege_pending ? "True" : "False") : "None";
    log(std::string("SIEGE edge=") + stage + " state=" + text(state) + " remaining_ms=" + text(left) + " input_seq=" + text(sequence) +
        " pending=" + pending + " speed_mps=" + std::to_string(s.speed) + " limits_mps=" + limits + " input=(" + std::to_string(c.forward) +
        "," + std::to_string(c.turn) + "," + (c.handbrake ? "True" : "False") + ")");
}
// Rederives the drive physics after the active descriptor changed (Siege travel/hydraulic descriptors).
void derivePhysics(Battle& b, PyObject* descriptor) {
    auto factors = params::localFactors(b, descriptor);
    auto physics = py::call(p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics")).get(), "derive_params", descriptor, factors.get());
    b.local->physics = own(std::move(physics));
}
// One stopped-drivetrain input carrying the Siege mode request.
bool sendSiegeRequest(Battle& b, PyObject* entity, bool enabled) {
    if (!stock_native_client_input::sendCurrent(b, enabled)) return false;
    auto raw = py::optional(b.engine.client.get(), "_input_seq");
    std::optional<std::int64_t> sequence;
    if (!PyBool_Check(raw.get()) && (PyInt_Check(raw.get()) || PyLong_Check(raw.get())) && p::wide(raw.get()) > 0) sequence = p::wide(raw.get());
    dr::driverAccess(b).driver->siege_admitted(enabled, sequence);
    auto seq = sequence ? p::integer(*sequence) : p::retain(Py_None);
    reportSiegeEdge(b, "request", py::optional(entity, "siegeState").get(), Py_None, seq.get());
    return true;
}
// Hydraulic body providers on the copied chassis authority (the pinned _prepare_local_siege_pose).
void prepareSiegePose(Battle& b, PyObject* entity) {
    auto& l = *b.local;
    l.pose_matrix = l.stabilised_matrix = l.steady_rotation_matrix = l.matrix;
    if (!hasSiege(py::optional(entity, "typeDescriptor").get())) return;
    auto aim = construct(b, "Matrix");
    py::call(aim.get(), "setIdentity");
    l.siege_aim_matrix = own(std::move(aim));
    l.siege_aim_world_matrix = product(b, l.siege_aim_matrix.get(), l.matrix.get());
    l.siege_aim_pitch = l.siege_aim_center_z = 0.;
    auto relative = construct(b, "Matrix");
    py::call(relative.get(), "setIdentity");
    l.siege_flat_body_matrix = product(b, relative.get(), l.matrix.get());
    l.siege_body_matrix = product(b, relative.get(), l.siege_aim_world_matrix.get());
    // The fixed-turret gun marker, shot ray and rendered barrel share the copied hydraulic body.
    l.siege_stabilised_matrix = l.siege_body_matrix;
    l.siege_ground_matrix = product(b, l.matrix.get());
    l.pose_matrix = product(b, l.matrix.get());
    l.stabilised_matrix = product(b, l.matrix.get());
    l.stabilised_snapshot = own(construct(b, "Matrix", l.stabilised_matrix.get()));
    l.steady_rotation_matrix = product(b, l.matrix.get());
}
// Selects the hydraulic or plain copied pose for every consumer (the pinned _select_local_siege_pose).
bool selectSiegePose(Battle& b, bool enabled) {
    auto& l = *b.local;
    if (!l.siege_body_matrix || !l.siege_stabilised_matrix || !l.siege_ground_matrix) return false;
    PyObject* body = enabled ? l.siege_body_matrix.get() : l.matrix.get();
    PyObject* ground = enabled ? l.siege_ground_matrix.get() : l.matrix.get();
    py::set(l.pose_matrix.get(), "a", body);
    py::set(l.stabilised_matrix.get(), "a", body);
    py::set(l.steady_rotation_matrix.get(), "a", ground);
    if (py::attr(l.pose_matrix.get(), "a").get() != body || py::attr(l.stabilised_matrix.get(), "a").get() != body ||
        py::attr(l.steady_rotation_matrix.get(), "a").get() != ground)
        throw p::Error("#1513 hydraulic pose selector was rejected");
    if (l.stabilised_snapshot) py::call(l.stabilised_snapshot.get(), "set", l.stabilised_matrix.get());
    return true;
}
// Re-seats the hydraulic aim provider at its current correction for the new descriptor (zero elapsed time).
void hullAiming(Battle& b, PyObject* entity) {
    auto& l = *b.local;
    auto descriptor = py::optional(entity, "typeDescriptor");
    if (!l.siege_aim_matrix || !hasSiege(descriptor.get())) return;
    double correction = l.siege_aim_pitch;
    try {
        auto type = py::optional(descriptor.get(), "type");
        auto params = [&] { auto own_params = py::optional(type.get(), "hullAimingParams"); return own_params.get() != Py_None ? std::move(own_params) : py::optional(descriptor.get(), "hullAimingParams"); }();
        auto pitch = py::optional(params.get(), "pitch");
        if (pitch.get() == Py_None) throw p::Error("hydraulic pitch parameters are unavailable");
        l.siege_aim_center_z = p::scalar(py::attr(pitch.get(), "wheelCorrectionCenterZ").get());
        const auto states = py::attr(b.engine.constants.get(), "VEHICLE_SIEGE_STATE");
        auto siege = py::optional(entity, "siegeState");
        const bool active = PyObject_RichCompareBool(siege.get(), py::attr(states.get(), "ENABLED").get(), Py_EQ) == 1;
        if (!p::flag(py::attr(pitch.get(), "isAvailable").get()) || (active && !p::flag(py::attr(pitch.get(), "isEnabled").get())))
            throw p::Error("hydraulic pitch is not enabled");
    } catch (const std::exception&) {
        PyErr_Clear();
        correction = 0.;
    }
    l.siege_aim_pitch = correction;
    p::Ref ypr(Py_BuildValue("(ddd)", 0., correction, 0.));
    py::call(l.siege_aim_matrix.get(), "setRotateYPR", ypr.get());
    py::set(l.siege_aim_matrix.get(), "translation",
            vector(b, {0., l.siege_aim_center_z * std::sin(correction), l.siege_aim_center_z * (1. - std::cos(correction))}).get());
}
// PlayerAvatar.handleVehicleCollidedVehicle replacement: the stock callback, then the native contact proofs.
PyObject* observeAfterNative(PyObject* original, PyObject* args) {
    PyObject* result = PyObject_CallObject(original, args);
    if (!result) return nullptr;
    Battle* b = current();
    PyObject *avatar = nullptr, *first = nullptr, *second = nullptr, *hit = nullptr, *time = nullptr;
    if (b && b->local && PyArg_ParseTuple(args, "OOOOO", &avatar, &first, &second, &hit, &time) && avatar == b->engine.avatar.get()) {
        try {
            const double contact = PyFloat_AsDouble(time);
            observeFeedback(*b, first, second, contact);
            observeContact(*b, first, second, hit, contact);
        } catch (const std::exception& e) {
            b->warn("native ram contact proof", describe(e));
        }
    }
    PyErr_Clear();
    return result;
}
PyMethodDef observeMethod = {"handleVehicleCollidedVehicle", observeAfterNative, METH_VARARGS, nullptr};
}

void preparePresentation(Battle& b, PyObject* entity) {
    auto& l = *b.local;
    if (l.matrix) throw p::Error("player pose was prepared more than once");
    auto native_attribute = py::attr(b.engine.compatibility.get(), "native_vehicle_attribute");
    auto filter = py::invoke(native_attribute.get(), entity, p::text("filter").get());
    (void)filter;
    auto matrix = construct(b, "Matrix");
    p::Ref ypr(Py_BuildValue("(ddd)", l.yaw, 0., 0.));
    py::call(matrix.get(), "setRotateYPR", ypr.get());
    py::set(matrix.get(), "translation", vector(b, l.position).get());
    l.matrix = own(std::move(matrix));
    // Hydraulic providers are transplanted after the native lifecycle completes (attachPresentation).
    l.pose_matrix = l.stabilised_matrix = l.steady_rotation_matrix = l.matrix;
    auto zero = vector(b, {0., 0., 0.});
    poseOverlay(b, entity, zero.get());
    l.camera_velocity = {};
    auto descriptor = py::attr(entity, "typeDescriptor");
    if (!l.descriptor) l.descriptor = borrow(descriptor.get());
    derivePhysics(b, descriptor.get());
    if (b.replay) {
        const auto snapshot = params::acceptedLocalEffectiveParams(b);
        if (snapshot.resolved().kind != Value::Kind::Null) l.physics = own(p::Ref(cs::write(snapshot.get("physics"))));
    }
    resetSuspension(l, false);
}

void attachPresentation(Battle& b) {
    auto& l = *b.local;
    auto entity = playerEntity(b);
    if (entity.get() == Py_None) throw p::Error("player Vehicle is unavailable for presentation");
    if (!l.matrix) preparePresentation(b, entity.get());
    if (hasSiege(py::optional(entity.get(), "typeDescriptor").get()) && !l.siege_body_matrix) {
        prepareSiegePose(b, entity.get());
        auto motion = vector(b, l.camera_velocity);
        poseOverlay(b, entity.get(), motion.get());
    }
    auto model = py::optional(entity.get(), "model");
    if (model.get() == Py_None) throw p::Error("player compound model is unavailable");
    py::set(model.get(), "matrix", bodyPose(l).get());
    // Body swinging and engine audio motion bind natively on the first drive slice (self presentation).
    py::call(b.engine.compatibility.get(), "bind_vehicle_pose_sources", b.engine.avatar.get(), entity.get());
    l.model = own(std::move(model));
}

void bindControlSources(Battle& b, PyObject* handler, PyObject* mode) {
    const auto& l = *b.local;
    if (!l.matrix) throw p::Error("player control bind requires a live pose");
    auto calculator = py::optional(handler, "steadyVehicleMatrixCalculator");
    if (calculator.get() == Py_None) throw p::Error("#1513 steady vehicle matrix calculator is unavailable");
    auto output = py::optional(calculator.get(), "_SteadyVehicleMatrixCalculator__outputMProv");
    auto stabilised = py::optional(calculator.get(), "_SteadyVehicleMatrixCalculator__stabilisedMProv");
    if (output.get() == Py_None || stabilised.get() == Py_None) throw p::Error("#1513 steady vehicle matrix providers are unavailable");
    PyObject* steady = steadyRotation(l).get();
    PyObject* pose = stabilisedPose(l).get();
    py::set(output.get(), "rotationSrc", steady);
    py::set(output.get(), "translationSrc", pose);
    py::set(stabilised.get(), "target", pose);
    if (py::attr(output.get(), "rotationSrc").get() != steady || py::attr(output.get(), "translationSrc").get() != pose ||
        py::attr(stabilised.get(), "target").get() != pose)
        throw p::Error("#1513 steady vehicle matrix providers rejected live pose");
    auto modes = py::optional(py::attr(b.engine.modules.get(), "avatar_input_handler").get(), "_CTRL_MODE");
    if (modes.get() == Py_None) throw p::Error("#1513 control-mode constants are unavailable");
    // Sniper aiming consumes the steady calculator above; other stock modes own different cameras.
    if (PyObject_RichCompareBool(mode, py::attr(modes.get(), "ARCADE").get(), Py_EQ) != 1) return;
    auto control = py::optional(handler, "_AvatarInputHandler__curCtrl");
    if (control.get() == Py_None) throw p::Error("native current control mode is unavailable");
    auto camera = py::optional(control.get(), "camera");
    if (camera.get() == Py_None) throw p::Error("native current camera is unavailable");
    PyObject* raw_previous = PyObject_GetAttrString(camera.get(), "vehicleMProv");
    if (!raw_previous) { PyErr_Clear(); throw p::Error("initial #1513 camera has no vehicle matrix provider"); }
    p::Ref previous(raw_previous);
    PyObject* body = bodyPose(l).get();
    py::set(camera.get(), "vehicleMProv", body);
    // The exact #1513 getter unwraps the translation-only provider; a mismatch means the setter rejected the pose.
    if (py::attr(camera.get(), "vehicleMProv").get() != body) {
        py::set(camera.get(), "vehicleMProv", previous.get());
        throw p::Error("native arcade camera rejected the player pose provider");
    }
}

void bindArcadeCamera(Battle& b) {
    auto handler = py::optional(b.engine.avatar.get(), "inputHandler");
    if (handler.get() == Py_None) throw p::Error("native input handler is unavailable");
    auto modes = py::optional(py::attr(b.engine.modules.get(), "avatar_input_handler").get(), "_CTRL_MODE");
    if (modes.get() == Py_None) throw p::Error("#1513 control-mode constants are unavailable");
    auto mode = py::optional(handler.get(), "_AvatarInputHandler__ctrlModeName");
    if (PyObject_RichCompareBool(mode.get(), py::attr(modes.get(), "ARCADE").get(), Py_EQ) != 1) throw p::Error("initial #1513 control mode is not arcade");
    bindControlSources(b, handler.get(), mode.get());
    auto control = py::optional(handler.get(), "_AvatarInputHandler__curCtrl");
    auto camera = py::optional(control.get(), "camera");
    auto align = py::optional(camera.get(), "setToVehicleDirection");
    if (!py::callable(align)) throw p::Error("#1513 arcade camera direction boundary is unavailable");
    // ArcadeCamera was created before the client-only Vehicle; reset its yaw now the live matrix is attached.
    py::invoke(align.get());
    auto rotator = py::optional(b.engine.avatar.get(), "gunRotator");
    auto reset = py::optional(rotator.get(), "reset");
    if (!py::callable(reset)) throw p::Error("#1513 gun-direction reset boundary is unavailable");
    py::invoke(reset.get());
    vehicles::echoLocalGunAngles(b, 0., 0.);
    alignAim(b, 0., 0.);
}

void siegeChanged(Battle& b, PyObject* entity, bool raised) {
    auto& l = *b.local;
    if (l.matrix) selectSiegePose(b, raised);
    if (l.physics) {
        derivePhysics(b, py::attr(entity, "typeDescriptor").get());
        resetSuspension(l, false);
    }
    if (l.matrix) hullAiming(b, entity);
}

bool changeVehicleSetting(Battle& b, int code, PyObject* value) {
    auto settings = py::attr(b.engine.constants.get(), "VEHICLE_SETTING");
    auto siege_code = py::optional(settings.get(), "SIEGE_MODE_ENABLED");
    if (siege_code.get() == Py_None || PyObject_RichCompareBool(p::integer(code).get(), siege_code.get(), Py_EQ) != 1)
        return gun::changeSetting(b, code, value);
    if (PyBool_Check(value) || (!PyInt_Check(value) && !PyLong_Check(value))) return false;
    const auto requested = p::wide(value);
    if ((requested != 0 && requested != 1) || !b.engine.server) return false;
    auto entity = playerEntity(b);
    if (!hasSiege(py::optional(entity.get(), "typeDescriptor").get()) || p::flag(py::optional(entity.get(), "is_engine_dead").get())) return false;
    const bool enabled = requested == 1;
    auto states = py::attr(b.engine.constants.get(), "VEHICLE_SIEGE_STATE");
    auto desired = py::attr(states.get(), enabled ? "ENABLED" : "DISABLED");
    auto siege = [&] { auto value = py::optional(entity.get(), "siegeState"); return value.get() != Py_None ? std::move(value) : py::attr(states.get(), "DISABLED"); }();
    auto access = dr::driverAccess(b);
    auto& state = access.driver->state();
    if (state.siege_braking && !state.siege_pending && PyObject_RichCompareBool(siege.get(), desired.get(), Py_EQ) == 1) {
        state.siege_braking.reset();
        reportSiegeEdge(b, "cancel_brake", desired.get());
        return true;
    }
    if (state.speed != 0. || state.turn_speed != 0.) {
        state.siege_braking = enabled;
        reportSiegeEdge(b, "brake", py::optional(entity.get(), "siegeState").get());
        return true;
    }
    return sendSiegeRequest(b, entity.get(), enabled);
}

PyObject* ensureSuspensionParams(Battle& b, PyObject* descriptor) {
    auto& l = *b.local;
    p::Ref physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));
    if (p::flag(py::call(physics.get(), "suspension_trial_excluded", descriptor).get())) {
        resetSuspension(l, true);
        reportSuspension(l, "excluded: #1513 hydraulic suspension");
        Py_RETURN_NONE;
    }
    if (l.suspension_disabled) Py_RETURN_NONE;
    if (!l.suspension_params) {
        try {
            l.suspension_params = own(py::call(physics.get(), "derive_suspension_params", descriptor));
        } catch (const std::exception& e) {
            const auto error = describe(e);
            resetSuspension(l, true);
            reportSuspension(l, "inactive: " + error);
            b.warn("ten-spring suspension trial", error);
            Py_RETURN_NONE;
        }
        reportSuspension(l, "active");
    }
    Py_INCREF(l.suspension_params.get());
    return l.suspension_params.get();
}

bool installRamContactHook(Battle& b) {
    auto& l = *b.local;
    if (b.worker || l.ram_contact_hook) return false;
    auto type = p::retain(reinterpret_cast<PyObject*>(Py_TYPE(b.engine.avatar.get())));
    PyObject* dict = reinterpret_cast<PyTypeObject*>(type.get())->tp_dict;
    PyObject* original = dict ? PyDict_GetItemString(dict, "handleVehicleCollidedVehicle") : nullptr;
    if (!original || !PyCallable_Check(original)) throw p::Error("#1513 PlayerAvatar collision callback is unavailable");
    p::Ref held(p::retain(original));
    p::Ref function(PyCFunction_New(&observeMethod, original));
    // An unbound method binds the Avatar instance as the stock callback's first argument.
    p::Ref method(PyMethod_New(function.get(), nullptr, type.get()));
    py::set(type.get(), "handleVehicleCollidedVehicle", method.get());
    l.ram_contact_hook = own(std::move(held));
    l.ram_contact_type = own(std::move(type));
    return true;
}

void restoreRamContactHook(Battle& b) {
    if (!b.local || !b.local->ram_contact_hook) return;
    py::set(b.local->ram_contact_type.get(), "handleVehicleCollidedVehicle", b.local->ram_contact_hook.get());
    b.local->ram_contact_hook = Object();
    b.local->ram_contact_type = Object();
}

bool seedSiegeHud(Battle& b, Actor& local) {
    if (b.worker || !b.engine.avatar || !local.local || !local.ready || b.local->siege_hud_seeded) return false;
    if (local.engine_id != p::wide(py::attr(b.engine.avatar.get(), "playerVehicleID").get())) return false;
    auto entity = b.entity(local.engine_id);
    if (!hasSiege(py::optional(entity.get(), "typeDescriptor").get()) || !p::flag(py::call(entity.get(), "isAlive").get())) return false;
    // Vehicle construction seeds DISABLED without a property event; the stock indicator reads this cache.
    auto disabled = py::attr(py::attr(b.engine.constants.get(), "VEHICLE_SIEGE_STATE").get(), "DISABLED");
    if (PyObject_RichCompareBool(py::attr(entity.get(), "siegeState").get(), disabled.get(), Py_EQ) != 1) return false;
    auto view = py::attr(b.engine.modules.get(), "vehicle_view_state");
    p::Ref value(Py_BuildValue("(Od)", disabled.get(), 0.));
    py::call(py::attr(b.engine.avatar.get(), "guiSessionProvider").get(), "invalidateVehicleState", py::attr(view.get(), "SIEGE_MODE").get(), value.get());
    b.local->siege_hud_seeded = true;
    return true;
}

void tickCritical(Battle& b, double dt) {
    if (!b.running() || b.worker || b.replay) return;
    try { stock_native_client_critical::tick(b, dt); }
    catch (const std::exception& e) { b.warn("native local track repair", describe(e), false); }
}
void tickDrowning(Battle& b, double dt, double now) { stock_native_client_survival::drowning(b, dt, now); }
void tickOverturn(Battle& b, double dt, double now) { stock_native_client_survival::overturn(b, dt, now); }
std::array<int, 2> warningLevels(const Battle& b) { return stock_native_client_survival::levels(b); }
std::optional<bool> reconcileCritical(Battle& b, Actor& vehicle, const Value& source, Value* canonical) {
    if (!b.running() || b.worker || b.replay || !vehicle.local) return {};
    return stock_native_client_critical::reconcile(b, vehicle, source, canonical);
}
const Value& damageReport(const Battle& b) { return stock_native_client_critical::report(b); }
double vehicleSpeed(const Battle& b) { return b.local ? b.local->speed : 0.; }
double vehicleRotationSpeed(const Battle& b) { return b.local ? b.local->turn_speed : 0.; }
}
}
