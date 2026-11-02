// The player's own vehicle: presentation lifecycle, Siege, settings, contact hook and the per-frame warnings.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_local.hpp"
#include "stock_native_battle_feedback.hpp"
#include "stock_native_battle_visibility.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_remote.hpp"
#include "stock_native_battle_compat.hpp"
#include "stock_native_client_survival_python.hpp"
#include "stock_native_client_critical.hpp"
#include "stock_native_client_input_runtime.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_drive_publication.hpp"
#include "stock_native_world_pipeline.hpp"
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
namespace visibility {
// Provided by the visibility subsystem: re-evaluate spotting on the next frame.
void resetSpottingTimer(Battle& b);
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
// The copied pose and its matrix providers as the player Vehicle's native pose overlay.
void poseOverlay(Battle& b, PyObject* entity, PyObject* motion) {
    const auto& l = *b.local;
    auto position = vector(b, l.position);
    auto matrix = bodyPose(l);
    auto steady = obj(steadyRotation(l)), stabilised = obj(stabilisedPose(l));
    remote::OverlayPose pose{position.get(), l.yaw, matrix.get(), l.speed, l.turn_speed, motion, motion,
                             steady.get() == Py_None ? nullptr : steady.get(), stabilised.get() == Py_None ? nullptr : stabilised.get()};
    remote::publishOverlay(entity, pose);
}
void resetSuspension(LocalVehicle& l, bool disabled) {
    l.suspension_params.reset();
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
    if (l.physics) limits = "(" + text(p::number(l.physics->forward).get()) + "," + text(p::number(l.physics->reverse).get()) + ")";
    std::string pending = s.siege_pending ? (*s.siege_pending ? "True" : "False") : "None";
    log(std::string("SIEGE edge=") + stage + " state=" + text(state) + " remaining_ms=" + text(left) + " input_seq=" + text(sequence) +
        " pending=" + pending + " speed_mps=" + std::to_string(s.speed) + " limits_mps=" + limits + " input=(" + std::to_string(c.forward) +
        "," + std::to_string(c.turn) + "," + (c.handbrake ? "True" : "False") + ")");
}
// Rederives the drive physics after the active descriptor changed (Siege travel/hydraulic descriptors).
void derivePhysics(Battle& b, PyObject* descriptor) {
    auto factors = params::localFactors(b, descriptor);
    b.local->physics = deriveParams(descriptor, factors.get());
    ++b.local->physics_revision;
}
// One stopped-drivetrain input carrying the Siege mode request.
bool sendSiegeRequest(Battle& b, PyObject* entity, bool enabled) {
    if (!stock_native_client_input::sendCurrent(b, enabled)) return false;
    const auto sequence = inputSequence(b);
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

// PersistentSiegeHints: the stock indicator keeps choosing text, key, animation and visibility; for this battle's
// session provider its finite hint quota is bypassed and a stable mode shows the next switch duration.
enum SiegeHintKind { HintView, SiegeState, IndicatorView };
const char* const siege_hint_names[] = {"_SiegeModeIndicator__updateHintView", "_SiegeModeIndicator__updateSiegeState",
                                        "_SiegeModeIndicator__updateIndicatorView"};
constexpr long siege_disabled = 0, siege_enabled = 2;  // siege_mechanics.DISABLED / ENABLED
// One stock call with indicator.<name> = value (unchanged when null), restored afterwards even when the call raises.
PyObject* callWith(PyObject* original, PyObject* args, PyObject* kwargs, PyObject* indicator, const char* name, PyObject* value) {
    PyObject* previous = PyObject_GetAttrString(indicator, name);
    if (!previous) return nullptr;
    PyObject* result = nullptr;
    if (!value || PyObject_SetAttrString(indicator, name, value) == 0) result = PyObject_Call(original, args, kwargs);
    PyObject *type, *error, *trace;
    PyErr_Fetch(&type, &error, &trace);
    const int restored = PyObject_SetAttrString(indicator, name, previous);
    Py_DECREF(previous);
    if (restored < 0) {
        Py_XDECREF(type); Py_XDECREF(error); Py_XDECREF(trace); Py_XDECREF(result);
        return nullptr;
    }
    PyErr_Restore(type, error, trace);
    return result;
}
PyObject* siegeHint(PyObject* closure, PyObject* args, PyObject* kwargs) {
    PyObject* original = PyTuple_GET_ITEM(closure, 0);
    const long kind = PyInt_AsLong(PyTuple_GET_ITEM(closure, 1));
    try {
        const Battle* b = current();
        const LocalVehicle* l = b ? b->local.get() : nullptr;
        PyObject* indicator = PyTuple_GET_SIZE(args) ? PyTuple_GET_ITEM(args, 0) : nullptr;
        if (!indicator || !l || !l->siege_hints_installed) return PyObject_Call(original, args, kwargs);
        if (py::attr(indicator, "sessionProvider").get() != l->siege_hint_provider.get()) return PyObject_Call(original, args, kwargs);
        if (kind != IndicatorView) {
            auto one = p::integer(1);
            return callWith(original, args, kwargs, indicator, "_hintsLeft", kind == HintView ? one.get() : nullptr);
        }
        // Stable LAN states carry zero remaining time; Flash shows the next switch duration from the native table.
        auto state = py::attr(indicator, "_siegeState");
        auto stable = [&](long n) { const int same = PyObject_RichCompareBool(state.get(), p::integer(n).get(), Py_EQ); if (same < 0) throw p::Error("_siegeState"); return same == 1; };
        if (!stable(siege_disabled) && !stable(siege_enabled)) return PyObject_Call(original, args, kwargs);
        auto row = p::Ref(PyObject_GetItem(py::attr(indicator, "_switchTimeTable").get(), state.get()));
        auto engine = p::Ref(PyObject_GetItem(py::attr(indicator, "_devices").get(), p::text("engine").get()));
        auto duration = p::Ref(PyObject_GetItem(row.get(), engine.get()));
        return callWith(original, args, kwargs, indicator, "_switchTime", duration.get());
    } catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyMethodDef siegeHintMethod = {"persistent_siege_hint", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)(void)>(siegeHint)),
                               METH_VARARGS | METH_KEYWORDS, nullptr};
bool enableSiegeHints(Battle& b) {
    auto& l = *b.local;
    if (b.worker || !b.engine.avatar || l.siege_hint_type || !l.descriptor || !hasSiege(l.descriptor.get())) return false;
    auto type = py::attr(b.engine.modules.get(), "siege_mode_indicator_type");
    auto provider = py::attr(b.engine.avatar.get(), "guiSessionProvider");
    l.siege_hint_type = borrow(type.get());
    l.siege_hint_provider = own(std::move(provider));
    auto dict = py::attr(type.get(), "__dict__");
    std::vector<p::Ref> originals;
    for (const char* name : siege_hint_names) originals.push_back(p::Ref(PyObject_GetItem(dict.get(), p::text(name).get())));
    for (const auto& original : originals)
        if (!PyCallable_Check(original.get())) throw p::Error("stock Siege hint callback is unavailable");
    for (long kind = HintView; kind <= IndicatorView; ++kind) {
        PyObject* original = originals[kind].get();
        p::Ref closure(Py_BuildValue("(Ol)", original, kind));
        p::Ref function(PyCFunction_NewEx(&siegeHintMethod, closure.get(), nullptr));
        // An unbound method binds the indicator instance as the stock callback's first argument.
        p::Ref method(PyMethod_New(function.get(), nullptr, type.get()));
        py::set(type.get(), siege_hint_names[kind], method.get());
        l.siege_hints.push_back({siege_hint_names[kind], borrow(original), own(std::move(method))});
    }
    l.siege_hints_installed = true;
    return true;
}
}

void preparePresentation(Battle& b, PyObject* entity) {
    auto& l = *b.local;
    if (l.matrix) throw p::Error("player pose was prepared more than once");
    auto native_matrix = compat::nativeVehicleAttribute(entity, "matrix");
    auto filter = compat::nativeVehicleAttribute(entity, "filter");
    PyObject* raw_stabilised = PyObject_GetAttrString(filter.get(), "stabilisedMatrix");
    if (!raw_stabilised) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("native Vehicle filter stabilisedMatrix");
        PyErr_Clear();
    }
    l.native_matrix = own(std::move(native_matrix));
    l.native_stabilised_matrix = raw_stabilised ? Object(raw_stabilised) : l.native_matrix;
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
        if (snapshot.resolved().kind != Value::Kind::Null) {
            l.physics = physicsFromValue(snapshot.get("physics"));
            ++l.physics_revision;
        }
    }
    resetSuspension(l, false);
}

void attachPresentation(Battle& b) {
    auto& l = *b.local;
    auto entity = playerEntity(b);
    if (entity.get() == Py_None) throw p::Error("player Vehicle is unavailable for presentation");
    if (!l.matrix) preparePresentation(b, entity.get());
    if (hasSiege(py::optional(entity.get(), "typeDescriptor").get()) && !l.siege_body_matrix) {
        auto filter = compat::nativeVehicleAttribute(entity.get(), "filter");
        l.native_stabilised_matrix = own(py::optional(filter.get(), "stabilisedMatrix"));
        prepareSiegePose(b, entity.get());
        auto motion = vector(b, l.camera_velocity);
        poseOverlay(b, entity.get(), motion.get());
    }
    auto model = py::optional(entity.get(), "model");
    if (model.get() == Py_None) throw p::Error("player compound model is unavailable");
    py::set(model.get(), "matrix", bodyPose(l).get());
    // Body swinging and engine audio motion bind natively on the first drive slice (self presentation).
    compat::bindVehiclePoseSources(b, b.engine.avatar.get(), entity.get());
    l.model = own(std::move(model));
    b.feature("persistent Siege mode hint", [&] { enableSiegeHints(b); });
}

void closeSiegeHints(Battle& b) {
    if (!b.local) return;
    auto& l = *b.local;
    l.siege_hints_installed = false;
    if (l.siege_hint_type) {
        auto dict = py::attr(l.siege_hint_type.get(), "__dict__");
        // A later mod owns its own hook; restore only callbacks that are still ours.
        for (auto hint = l.siege_hints.rbegin(); hint != l.siege_hints.rend(); ++hint) {
            auto installed = py::call(dict.get(), "get", p::text(hint->name).get());
            if (installed.get() == hint->replacement.get()) py::set(l.siege_hint_type.get(), hint->name.c_str(), hint->original.get());
        }
    }
    l.siege_hints.clear();
    l.siege_hint_provider = Object();
    l.siege_hint_type = Object();
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

const wot::offline_bot_step::SuspensionDescriptor* ensureSuspensionParams(Battle& b, PyObject* descriptor) {
    auto& l = *b.local;
    if (suspensionTrialExcluded(descriptor)) {
        resetSuspension(l, true);
        reportSuspension(l, "excluded: #1513 hydraulic suspension");
        return nullptr;
    }
    if (l.suspension_disabled) return nullptr;
    if (!l.suspension_params) {
        try {
            l.suspension_params = deriveSuspension(descriptor);
            ++l.suspension_revision;
        } catch (const std::exception& e) {
            const auto error = describe(e);
            resetSuspension(l, true);
            reportSuspension(l, "inactive: " + error);
            b.warn("ten-spring suspension trial", error);
            return nullptr;
        }
        reportSuspension(l, "active");
    }
    return &*l.suspension_params;
}

std::optional<std::int64_t> inputSequence(const Battle& b) {
    if (!b.local || b.local->input_seq <= 0) return {};
    return b.local->input_seq;
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

namespace {
// Siege edges read the player driver; it is installed by the first live drive slice.
bool driving(const Battle& b) { return !b.worker && !b.replay && b.live && b.local && b.local->drive; }
p::Ref wire(const Value& value) { return p::Ref(cs::write(value.resolved())); }
std::int64_t localEngineId(const Battle& b) {
    if (!b.engine.server) return 0;
    auto id = py::optional(b.engine.server.get(), "vehicle_id");
    return id.get() == Py_None ? 0 : p::wide(id.get());
}
// BattleRuntime._assert_postmortem_control_sources: every death-camera provider stock selected is the copied pose.
bool assertPostmortemSources(Battle& b, PyObject* handler) {
    const auto& l = *b.local;
    auto matrices = py::optional(b.engine.avatar.get(), "consistentMatrices");
    auto attached = py::optional(matrices.get(), "attachedVehicleMatrix");
    if (attached.get() == Py_None) throw p::Error("#1513 attached vehicle matrix provider is unavailable");
    auto attached_target = py::optional(attached.get(), "target");
    p::Ref expected_target = obj(bodyPose(l));
    const auto spectated = feedback::spectated(b);
    if (spectated && *spectated != localEngineId(b)) {
        auto vehicle = feedback::spectatorEntity(b, *spectated);
        if (vehicle.get() != Py_None) replace(expected_target, py::attr(vehicle.get(), "matrix"));
    }
    if (attached_target.get() != expected_target.get()) throw p::Error("#1513 postmortem attached provider captured a stale vehicle pose");
    auto control = py::optional(handler, "_AvatarInputHandler__curCtrl");
    auto camera = py::optional(control.get(), "_PostMortemControlMode__cam");
    if (camera.get() == Py_None) throw p::Error("#1513 postmortem camera is unavailable");
    p::Ref expected_camera = p::retain(attached.get());
    if (py::optional(control.get(), "curPostmortemDelay").get() != Py_None) {
        auto lookup = py::optional(b.engine.bigworld.get(), "entity");
        if (!py::callable(lookup)) throw p::Error("#1513 postmortem vehicle lookup is unavailable");
        auto vehicle = py::invoke(lookup.get(), py::attr(b.engine.avatar.get(), "playerVehicleID").get());
        if (vehicle.get() != Py_None) {
            replace(expected_camera, py::optional(vehicle.get(), "matrix"));
            if (expected_camera.get() != bodyPose(l).get()) throw p::Error("#1513 postmortem vehicle captured a stale vehicle pose");
        } else {
            auto calculator = py::optional(handler, "steadyVehicleMatrixCalculator");
            replace(expected_camera, py::optional(calculator.get(), "outputMProv"));
            if (expected_camera.get() == Py_None) throw p::Error("#1513 postmortem delay matrix provider is unavailable");
            if (py::optional(expected_camera.get(), "rotationSrc").get() != steadyRotation(l).get() ||
                py::optional(expected_camera.get(), "translationSrc").get() != stabilisedPose(l).get())
                throw p::Error("#1513 postmortem delay captured a stale vehicle pose");
        }
    }
    PyObject* camera_matrix = PyObject_GetAttrString(camera.get(), "vehicleMProv");
    if (!camera_matrix) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
        throw p::Error("#1513 postmortem camera has no vehicle matrix provider");
    }
    p::Ref held(camera_matrix);
    if (camera_matrix != expected_camera.get()) throw p::Error("#1513 postmortem camera captured a stale vehicle pose");
    return true;
}
// BattleRuntime._restore_local_body_swinging: the animator gets back the stock providers the rebind replaced.
bool restoreSwinging(Battle& b, PyObject* entity) {
    auto& l = *b.local;
    if (!l.swinging_restore) return false;
    PyObject* restore = l.swinging_restore.get();
    if (!PyTuple_Check(restore) || PyTuple_GET_SIZE(restore) != 3) throw p::Error("local swinging restore record is invalid");
    PyObject* swinging = PyTuple_GET_ITEM(restore, 0);
    l.swinging_probe = Object();
    l.swinging_move_flags = 0;
    auto appearance = py::optional(entity, "appearance");
    if (!p::flag(py::optional(entity, "isStarted").get()) || py::optional(appearance.get(), "swingingAnimator").get() != swinging) {
        // A stock appearance refresh retired this animator; its replacement owns the model now.
        l.swinging_animator = l.swinging_restore = Object();
        return false;
    }
    try {
        py::set(swinging, "accelSwingingPeriod", p::number(0.).get());
        py::set(swinging, "accelSwingingDirection", p::number(0.).get());
    } catch (const std::exception& e) {
        log("PRESENTATION local_body_swinging=False error=" + describe(e));
    }
    py::set(swinging, "placingCompensationMatrix", PyTuple_GET_ITEM(restore, 1));
    py::set(swinging, "worldMatrix", PyTuple_GET_ITEM(restore, 2));
    l.swinging_animator = l.swinging_restore = Object();
    return true;
}
}

void siegeAcknowledged(Battle& b, std::int64_t siege, std::int64_t left_ms, const Value& input_seq, bool switching) {
    // A switching echo keeps the pending edge latched.
    if (switching || !driving(b)) return;
    auto access = dr::driverAccess(b);
    auto& state = access.driver->state();
    if (!state.siege_pending) return;
    const auto& sequence = input_seq.resolved();
    access.driver->acknowledge_siege(siege, sequence.kind == Value::Kind::Integer ? std::optional<std::int64_t>(sequence.integer) : std::nullopt);
    if (state.siege_pending) return;
    reportSiegeEdge(b, "ack", p::integer(siege).get(), p::integer(left_ms).get(), wire(sequence).get());
}

void siegeReported(Battle& b, const char* stage, std::int64_t siege, std::int64_t left_ms, const Value& input_seq) {
    if (!driving(b)) return;
    reportSiegeEdge(b, stage, p::integer(siege).get(), p::integer(left_ms).get(), wire(input_seq).get());
}

bool controlModeChanged(Battle& b, PyObject* handler, PyObject* mode) {
    // onControlModeChanged recreated the targeting device, which cleared BigWorld.target.
    visibility::clearOutline(b);
    if (!b.running() || !b.local || !b.local->matrix) return false;
    auto modes = py::optional(py::attr(b.engine.modules.get(), "avatar_input_handler").get(), "_CTRL_MODE");
    if (modes.get() == Py_None) throw p::Error("#1513 control-mode constants are unavailable");
    const int postmortem = PyObject_RichCompareBool(mode, py::attr(modes.get(), "POSTMORTEM").get(), Py_EQ);
    if (postmortem < 0) throw p::Error("control mode comparison failed");
    if (postmortem) {
        const bool result = assertPostmortemSources(b, handler);
        if (b.engine.server) feedback::setSpectated(b, localEngineId(b));
        return result;
    }
    bindControlSources(b, handler, mode);
    // Strategic view may draw team-spotted targets beyond the vehicle AOI: re-evaluate on the next frame.
    visibility::resetSpottingTimer(b);
    return true;
}

bool detachPresentation(Battle& b) {
    if (!b.local || !b.engine.server || (!b.local->matrix && !b.local->model)) return false;
    auto& l = *b.local;
    auto entity = playerEntity(b);
    if (entity.get() == Py_None) return false;
    vehicles::syncFireEffect(b, entity.get(), false);
    restoreSwinging(b, entity.get());
    if (l.model && l.native_matrix) py::set(l.model.get(), "matrix", l.native_matrix.get());
    remote::clearOverlay(entity.get());
    compat::restoreVehiclePoseSources(b, b.engine.avatar.get(), entity.get(), obj(l.native_matrix).get(), obj(l.native_stabilised_matrix).get());
    l.matrix = l.pose_matrix = l.stabilised_matrix = l.stabilised_snapshot = l.steady_rotation_matrix = Object();
    l.siege_body_matrix = l.siege_stabilised_matrix = l.siege_ground_matrix = l.siege_flat_body_matrix = Object();
    l.siege_aim_matrix = l.siege_aim_world_matrix = Object();
    l.siege_aim_pitch = l.siege_aim_center_z = 0.;
    l.model = l.swinging_animator = l.swinging_restore = l.placing_compensation = l.swinging_probe = Object();
    l.swinging_move_flags = l.swinging_event_seq = l.swinging_events_reported = 0;
    l.native_matrix = l.native_stabilised_matrix = Object();
    l.camera_velocity = {};
    l.engine_mode = l.engine_state = Object();
    l.engine_audio_report_at = 0.;
    return true;
}

bool destructibleRejection(Battle& b, std::int64_t sequence, const std::optional<std::pair<Point, double>>&) {
    // The visible endpoint already committed its exact crush; a late disagreement never resurrects collision or rewinds.
    if (!driving(b)) return false;
    auto access = dr::driverAccess(b);
    const Value* pending = access.publication->pendingDestructible(sequence);
    if (!pending || !b.local->destructible_rejected.insert(sequence).second) return false;
    auto scene = dr::sceneAccess(b);
    std::string requested;
    bool changed = false;
    for (const auto& raw : pending->get("token").resolved().array) {
        const auto& fields = raw.resolved().array;
        if (fields.size() != 3) continue;
        const auto& material = fields[2].resolved();
        const std::int64_t chunk = std::int64_t(fields[0].numeric()), item = std::int64_t(fields[1].numeric());
        const std::int64_t kind = material.kind == Value::Kind::Null ? -1 : std::int64_t(material.numeric());
        changed |= scene.registry->skin.speculative.erase({std::uint32_t(chunk), std::uint32_t(item), kind}) > 0;
        requested += (requested.empty() ? "(" : ", ") + std::string("(") + std::to_string(chunk) + ", " + std::to_string(item) + ", " +
                     (kind < 0 ? std::string("None") : std::to_string(kind)) + ")";
    }
    if (changed) ++scene.registry->generation;
    if (b.local->destructible_verdict_reports < 24) {
        ++b.local->destructible_verdict_reports;
        log("DESTRUCTIBLE stage=visible_kept seq=" + std::to_string(sequence) + " accepted=0 requested=" +
            (requested.empty() ? std::string("None") : requested + (requested.find("), (") == std::string::npos ? ",)" : ")")) +
            " actual=None world=- commit=-");
    }
    return true;
}

p::Ref bodyPose(Battle& b) { return b.local ? obj(bodyPose(*b.local)) : p::retain(Py_None); }
p::Ref swingingAnimator(Battle& b) { return b.local ? obj(b.local->swinging_animator) : p::retain(Py_None); }
}
}
