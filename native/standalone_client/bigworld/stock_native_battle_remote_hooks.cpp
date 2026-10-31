// Native stock Vehicle/Avatar/CompoundAppearance attribute hooks; copied poses come from the C++ pose overlays.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script.hpp"
#endif
#include "stock_native_battle_remote.hpp"
#include "stock_native_battle_compat.hpp"
#include <map>
#include <frameobject.h>
#include <algorithm>
#include <cstring>

namespace native_battle {
namespace remote_hooks {
namespace {
using native_battle::Overlay;
struct Hook { PyObject_HEAD PyObject* compatibility; PyObject* instance; int kind; };
struct Proxy { PyObject_HEAD PyObject* fields; PyObject* delegate; int kind; };
PyTypeObject hook_type = { PyVarObject_HEAD_INIT(nullptr, 0) };
PyTypeObject proxy_type = { PyVarObject_HEAD_INIT(nullptr, 0) };

PyObject* fail(const std::exception& e) {
    if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
}
p::Ref attr(PyObject* object, const char* name) { return p::Ref(PyObject_GetAttrString(object, name)); }
p::Ref call(PyObject* callable, PyObject* first) {
    return p::Ref(PyObject_CallFunctionObjArgs(callable, first, nullptr));
}
p::Ref original(PyObject* compatibility, PyObject* vehicle, PyObject* name) {
    auto get = attr(compatibility, "_original_vehicle_getattribute");
    return p::Ref(PyObject_CallFunctionObjArgs(get.get(), vehicle, name, nullptr));
}
p::Ref original(PyObject* compatibility, PyObject* vehicle, const char* name) {
    auto text = p::text(name); return original(compatibility, vehicle, text.get());
}
bool originalFlag(PyObject* compatibility, PyObject* vehicle, const char* name, bool fallback) {
    try { return p::flag(original(compatibility, vehicle, name).get()); }
    catch (const std::exception&) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw;
        PyErr_Clear(); return fallback;
    }
}
Overlay* overlay(PyObject*, PyObject* vehicle, bool create = false) { return native_battle::remote::overlay(vehicle, create); }
PyObject* field(const Overlay* row, const char* name) { return row ? row->field(name) : nullptr; }
// A proxy's own field dict.
PyObject* field(PyObject* fields, const char* name) {
    return fields && fields != Py_None && PyDict_Check(fields) ? PyDict_GetItemString(fields, name) : nullptr;
}
bool activePose(const Overlay* row) { return row && row->pose_active; }
// The native compat scope or stock code object a hook branch compares against (compat.py kept them as attributes).
PyObject* scoped(const char* name) {
    namespace c = native_battle::compat;
    const auto& s = c::scopes();
    static const std::map<std::string, c::Code> codes = {
        {"_vehicle_start_wg_physics_code", c::Code::VehicleStartWgPhysics}, {"_vehicle_start_visual_code", c::Code::VehicleStartVisual},
        {"_vehicle_set_gun_angles_code", c::Code::VehicleSetGunAngles}, {"_avatar_aux_physics_code", c::Code::AvatarAuxPhysics},
        {"_avatar_vehicle_enter_code", c::Code::AvatarVehicleEnter}, {"_avatar_start_vehicle_visual_code", c::Code::AvatarStartVehicleVisual},
        {"_gun_rotator_stabilised_code", c::Code::GunRotatorStabilised},
        {"_gun_rotator_predict_locked_target_code", c::Code::GunRotatorPredictLockedTarget},
        {"_camera_acceleration_update_code", c::Code::CameraAccelerationUpdate},
        {"_arcade_oscillator_acceleration_code", c::Code::ArcadeOscillatorAcceleration},
        {"_sniper_oscillator_acceleration_code", c::Code::SniperOscillatorAcceleration},
        {"_custom_effect_update_code", c::Code::CustomEffectUpdate},
        {"_crashed_track_setup_assembler_code", c::Code::CrashedTrackSetupAssembler},
        {"_crashed_track_model_loaded_code", c::Code::CrashedTrackModelLoaded},
        {"_projectile_segment_may_hit_code", c::Code::ProjectileSegmentMayHit},
        {"_compound_models_refresh_code", c::Code::CompoundModelsRefresh}};
    const auto found = codes.find(name);
    if (found != codes.end()) return c::code(found->second);
    const std::string key = name;
    if (key == "_vehicle_starting_wg_physics") return s.starting_wg_physics;
    if (key == "_vehicle_starting_visual") return s.starting_visual;
    if (key == "_vehicle_syncing_gun_angles") return s.syncing_gun_angles;
    if (key == "_avatar_entering_vehicle") return s.entering_vehicle;
    if (key == "_avatar_syncing_aux_physics") return s.syncing_aux_physics;
    if (key == "_compound_refreshing_models") return s.refreshing_models;
    throw p::Error(std::string("unknown native compat scope: ") + name);
}
bool same(PyObject*, const char* name, PyObject* expected) {
    PyObject* value = scoped(name);
    return value ? value == expected : expected == Py_None;
}
bool named(const std::string& value, std::initializer_list<const char*> names) {
    for (const auto* name : names) if (value == name) return true;
    return false;
}
p::Ref mathValue(PyObject* compatibility, const char* name, PyObject* value) {
    auto runtime = attr(compatibility, "_runtime"); auto math = attr(runtime.get(), "math");
    auto constructor = attr(math.get(), name); return call(constructor.get(), value);
}
p::Ref visiblePosition(PyObject* compatibility, const Overlay* row) {
    auto matrix = mathValue(compatibility, "Matrix", p::checked(field(row, "matrix")));
    return attr(matrix.get(), "translation");
}
int proxyTraverse(PyObject* object, visitproc visit, void* arg) {
    auto* proxy = reinterpret_cast<Proxy*>(object); Py_VISIT(proxy->fields); Py_VISIT(proxy->delegate); return 0;
}
int proxyClear(PyObject* object) {
    auto* proxy = reinterpret_cast<Proxy*>(object); Py_CLEAR(proxy->fields); Py_CLEAR(proxy->delegate); return 0;
}
void proxyDestroy(PyObject* object) {
    auto* proxy = reinterpret_cast<Proxy*>(object);
    PyObject_GC_UnTrack(object); Py_XDECREF(proxy->fields); Py_XDECREF(proxy->delegate); Py_TYPE(object)->tp_free(object);
}
PyObject* newProxy(int kind, PyObject* delegate, PyObject* fields) {
    auto* value = reinterpret_cast<Proxy*>(proxy_type.tp_alloc(&proxy_type, 0));
    if (!value) return nullptr;
    value->kind = kind; value->delegate = delegate; value->fields = fields;
    Py_INCREF(delegate); Py_INCREF(fields); return reinterpret_cast<PyObject*>(value);
}
PyObject* proxySync(PyObject*, PyObject* args) {
    PyObject *first, *second;
    if (!PyArg_ParseTuple(args, "OO", &first, &second)) return nullptr;
    Py_RETURN_NONE;
}
PyObject* proxySyncPose(PyObject*, PyObject* args) {
    PyObject *first, *second, *third;
    if (!PyArg_ParseTuple(args, "OOO", &first, &second, &third)) return nullptr;
    Py_RETURN_NONE;
}
PyObject* proxyInterpolate(PyObject* self, PyObject* args) {
    try {
        PyObject* timestamp; if (!PyArg_ParseTuple(args, "O", &timestamp)) return nullptr;
        auto* proxy = reinterpret_cast<Proxy*>(self); auto* pose = field(proxy->fields, "_pose_matrix");
        if (pose && pose != Py_None) return p::retain(pose).release();
        auto method = attr(proxy->delegate, "interpolateStabilisedMatrix");
        return call(method.get(), timestamp).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* proxySegment(PyObject* self, PyObject* args) {
    try {
        PyObject *start, *end, *skip; if (!PyArg_ParseTuple(args, "OOO", &start, &end, &skip)) return nullptr;
        auto* proxy = reinterpret_cast<Proxy*>(self); auto* position = p::checked(field(proxy->fields, "position"));
        double delta[3], offset[3]; const char* names[] = {"x", "y", "z"};
        for (unsigned i = 0; i != 3; ++i) {
            const double first = p::scalar(attr(start, names[i]).get());
            delta[i] = p::scalar(attr(end, names[i]).get()) - first;
            offset[i] = p::scalar(attr(position, names[i]).get()) - first;
        }
        const double length = delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2];
        const double fraction = length <= 1e-9 ? 0. : std::clamp(
            (offset[0]*delta[0] + offset[1]*delta[1] + offset[2]*delta[2])/length, 0., 1.);
        for (unsigned i = 0; i != 3; ++i) offset[i] -= delta[i]*fraction;
        return p::boolean(offset[0]*offset[0] + offset[1]*offset[1] + offset[2]*offset[2] <= 400.).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyMethodDef proxy_methods[] = {
    {"syncGunAngles", proxySync, METH_VARARGS, nullptr},
    {"syncStabilisedYPR", proxySyncPose, METH_VARARGS, nullptr},
    {"interpolateStabilisedMatrix", proxyInterpolate, METH_VARARGS, nullptr},
    {"startVehicleVisual", proxySync, METH_VARARGS, nullptr},
    {"segmentMayHitEntity", proxySegment, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}
};
PyObject* proxyGet(PyObject* self, PyObject* key) {
    try {
        auto* proxy = reinterpret_cast<Proxy*>(self); const auto name = p::string(key);
        if ((proxy->kind == 0 && name == "_vehicle_filter") || (proxy->kind == 1 && name == "_provider"))
            return p::retain(proxy->delegate).release();
        auto* value = PyDict_GetItem(proxy->fields, key);
        if (value) return p::retain(value).release();
        if (proxy->kind == 0) {
            const char* replacement = name == "groundPlacingMatrix" ? "_pose_matrix" :
                name == "velocity" ? "_velocity" : name == "acceleration" ? "_acceleration" : nullptr;
            if (replacement) {
                value = field(proxy->fields, replacement);
                if (value && value != Py_None) return p::retain(value).release();
            }
        }
        const bool own = (proxy->kind == 0 && named(name, {"syncGunAngles", "syncStabilisedYPR", "interpolateStabilisedMatrix"})) ||
            (proxy->kind == 1 && name == "startVehicleVisual") ||
            (proxy->kind == 3 && name == "segmentMayHitEntity") || name == "__class__";
        return own ? PyObject_GenericGetAttr(self, key) : PyObject_GetAttr(proxy->delegate, key);
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* proxyCall(PyObject* self, PyObject* args, PyObject* kwargs) {
    try {
        auto* proxy = reinterpret_cast<Proxy*>(self);
        if (proxy->kind != 2) throw p::Error("Vehicle provider is not callable");
        if (kwargs && PyDict_Size(kwargs)) throw p::Error("Initial visibility accepts one positional argument");
        PyObject* ignored; if (!PyArg_ParseTuple(args, "O", &ignored)) return nullptr;
        return call(proxy->delegate, Py_False).release();
    } catch (const std::exception& e) { return fail(e); }
}
p::Ref collisionFilter(PyObject* compatibility, Overlay* row) {
    auto position = visiblePosition(compatibility, row);
    auto* retained = field(row, "_collision_filter");
    p::Ref result = [&]() -> p::Ref {
        if (retained && retained != Py_None) return p::retain(retained);
        auto fields = p::dict();
        p::set(fields.get(), "stabilisedMatrix", p::retain(p::checked(field(row, "matrix"))));
        p::set(fields.get(), "groundPlacingMatrixFiltered", p::retain(p::checked(field(row, "matrix"))));
        p::Ref created(newProxy(3, Py_None, fields.get()));
        row->fields["_collision_filter"] = native_battle::borrow(created.get());
        return created;
    }();
    if (Py_TYPE(result.get()) != &proxy_type) throw p::Error("Vehicle collision filter has a different owner");
    auto* proxy = reinterpret_cast<Proxy*>(result.get());
    p::Ref zero(Py_BuildValue("(ddd)", 0., 0., 0.)); auto* velocity = field(row, "velocity");
    auto copied_velocity = mathValue(compatibility, "Vector3", velocity && velocity != Py_None ? velocity : zero.get());
    p::set(proxy->fields, "position", mathValue(compatibility, "Vector3", position.get()));
    p::set(proxy->fields, "velocity", copied_velocity);
    double speed = 0.; try { speed = p::scalar(attr(copied_velocity.get(), "length").get()); }
    catch (const std::exception&) { PyErr_Clear(); }
    p::set(proxy->fields, "speed", p::number(speed)); return result;
}
p::Ref get(PyObject* compatibility, PyObject* vehicle, PyObject* key, PyObject* code) {
    const auto name = p::string(key);
    const bool overlays = named(name, {"health", "isCrewActive", "position", "yaw", "matrix", "speedInfo"});
    if ((!overlays && !named(name, {"filter", "cell", "show", "guiSessionProvider"})) ||
        !p::truth(compatibility, "_battle_active")) return original(compatibility, vehicle, key);
    if (overlays) {
        auto* row = overlay(compatibility, vehicle); auto* value = field(row, name.c_str());
        if (!value) return original(compatibility, vehicle, key);
        if (name == "position" && same(compatibility, "_gun_rotator_predict_locked_target_code", code) &&
            originalFlag(compatibility, vehicle, "_offlineNativeRemote", false)) return visiblePosition(compatibility, row);
        return p::retain(value);
    }
    if (name == "cell") {
        try { return original(compatibility, vehicle, "fakeCell"); }
        catch (const std::exception&) { if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw; PyErr_Clear(); }
        auto runtime = attr(compatibility, "_runtime"); auto bigworld = attr(runtime.get(), "bigworld"); auto player_fn = attr(bigworld.get(), "player");
        p::Ref player(PyObject_CallObject(player_fn.get(), nullptr)); auto avatar_module = attr(runtime.get(), "avatar_module"); auto avatar_type = attr(avatar_module.get(), "PlayerAvatar");
        const int is_avatar = PyObject_IsInstance(player.get(), avatar_type.get()); if (is_avatar < 0) throw p::Error("Vehicle avatar type lookup failed");
        if (is_avatar) {
            auto getter = attr(compatibility, "_original_avatar_getattribute"); auto fake = p::text("fakeServer");
            auto* value = PyObject_CallFunctionObjArgs(getter.get(), player.get(), fake.get(), nullptr);
            if (value) return p::Ref(value);
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("Vehicle server lookup failed");
            PyErr_Clear();
        }
        return original(compatibility, vehicle, key);
    }
    const bool visual = same(compatibility, "_vehicle_starting_visual", vehicle) && same(compatibility, "_vehicle_start_visual_code", code);
    if (visual && named(name, {"show", "guiSessionProvider"})) {
        const bool marker = originalFlag(compatibility, vehicle, "_offlineNativeMarkerVisible", true);
        const bool drawn = originalFlag(compatibility, vehicle, "_offlineNativeDrawVisible", true);
        if ((name == "show" && !drawn) || (name == "guiSessionProvider" && !marker)) {
            auto delegate = original(compatibility, vehicle, key); auto fields = p::dict();
            return p::Ref(newProxy(name == "show" ? 2 : 1, delegate.get(), fields.get()));
        }
    }
    if (name != "filter") return original(compatibility, vehicle, key);
    const bool start = same(compatibility, "_vehicle_starting_wg_physics", vehicle) && same(compatibility, "_vehicle_start_wg_physics_code", code);
    const bool gun = same(compatibility, "_vehicle_syncing_gun_angles", vehicle) && same(compatibility, "_vehicle_set_gun_angles_code", code);
    const bool auxiliary = scoped("_avatar_syncing_aux_physics") != nullptr && same(compatibility, "_avatar_aux_physics_code", code);
    const bool entering = same(compatibility, "_avatar_entering_vehicle", vehicle) &&
        (same(compatibility, "_avatar_vehicle_enter_code", code) || same(compatibility, "_avatar_start_vehicle_visual_code", code));
    auto* row = overlay(compatibility, vehicle); const bool pose = activePose(row);
    const bool fixed = pose && same(compatibility, "_gun_rotator_stabilised_code", code);
    const bool camera = pose && (same(compatibility, "_camera_acceleration_update_code", code) ||
        same(compatibility, "_arcade_oscillator_acceleration_code", code) || same(compatibility, "_sniper_oscillator_acceleration_code", code));
    const bool effect = pose && code != Py_None && same(compatibility, "_custom_effect_update_code", code);
    const bool crashed = pose && (same(compatibility, "_crashed_track_setup_assembler_code", code) || same(compatibility, "_crashed_track_model_loaded_code", code));
    if (pose && same(compatibility, "_projectile_segment_may_hit_code", code) && originalFlag(compatibility, vehicle, "_offlineNativeRemote", false))
        return collisionFilter(compatibility, row);
    if (start || gun || auxiliary || entering || fixed || camera || effect || crashed) {
        auto delegate = original(compatibility, vehicle, key); auto fields = p::dict();
        auto* matrix = fixed ? field(row, "stabilised_matrix") : nullptr;
        if ((fixed && !matrix) || crashed) matrix = p::checked(field(row, "matrix"));
        p::set(fields.get(), "_pose_matrix", p::retain(matrix ? matrix : Py_None));
        auto* velocity = camera || effect ? field(row, "velocity") : nullptr;
        auto* acceleration = camera || effect ? field(row, "acceleration") : nullptr;
        p::set(fields.get(), "_velocity", p::retain(velocity ? velocity : Py_None));
        p::set(fields.get(), "_acceleration", p::retain(acceleration ? acceleration : Py_None));
        return p::Ref(newProxy(0, delegate.get(), fields.get()));
    }
    return original(compatibility, vehicle, key);
}
PyObject* interned(PyObject*& slot, const char* text) {
    if (!slot) slot = PyString_InternFromString(text);
    return slot;
}
// The original getter for the common path, without C++ exceptions: stock
// getattr(..., default) probes make AttributeError a routine result.
PyObject* callOriginal(PyObject* compatibility, PyObject* getter, PyObject* self, PyObject* key) {
    PyObject* get = getter ? PyObject_GetAttr(compatibility, getter) : nullptr;
    if (!get) return nullptr;
    PyObject* value = PyObject_CallFunctionObjArgs(get, self, key, nullptr);
    Py_DECREF(get); return value;
}
int battleActive(PyObject* compatibility) {
    static PyObject* key;
    PyObject* value = interned(key, "_battle_active") ? PyObject_GetAttr(compatibility, key) : nullptr;
    if (!value) return -1;
    const int active = PyObject_IsTrue(value); Py_DECREF(value); return active;
}
bool isName(PyObject* key, const char* name) {
    if (PyString_Check(key)) return std::strcmp(PyString_AS_STRING(key), name) == 0 &&
        std::size_t(PyString_GET_SIZE(key)) == std::strlen(name);
    return p::string(key) == name;
}
PyObject* callerCode() {
    auto* frame = PyEval_GetFrame(); return frame ? reinterpret_cast<PyObject*>(frame->f_code) : Py_None;
}
PyObject* avatarGet(PyObject* compatibility, PyObject* avatar, PyObject* key) {
    static PyObject* original_name;
    PyObject* original_key = interned(original_name, "_original_avatar_getattribute");
    const bool server = isName(key, "base") || isName(key, "cell") || isName(key, "server") || isName(key, "bwProto");
    const bool vehicle = !server && isName(key, "vehicle");
    if (!server && !vehicle) return callOriginal(compatibility, original_key, avatar, key);
    const int active = battleActive(compatibility);
    if (active < 0) return nullptr;
    if (active && server) {
        auto fake = p::text("fakeServer");
        if (PyObject* value = callOriginal(compatibility, original_key, avatar, fake.get())) return value;
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) return nullptr;
        PyErr_Clear();
    }
    if (active && vehicle) {
        auto lookup = [&]() { auto runtime = attr(compatibility, "_runtime"); auto bigworld = attr(runtime.get(), "bigworld"); return attr(bigworld.get(), "entity"); };
        native_battle::Battle* battle = native_battle::current();
        auto postmortem = p::integer(battle && battle->compat ? battle->compat->postmortem_vehicle_id : 0);
        if (p::flag(postmortem.get())) {
            auto entity = call(lookup().get(), postmortem.get());
            // Retail changes Avatar.vehicle before it publishes onSwitchViewpoint.
            if (entity.get() != Py_None) return entity.release();
        }
        if (same(compatibility, "_sniper_oscillator_acceleration_code", callerCode())) {
            p::Ref selected = [&]() -> p::Ref {
                try {
                    auto getter = attr(compatibility, "_original_avatar_getattribute"); auto name = p::text("playerVehicleID");
                    p::Ref vehicle_id(PyObject_CallFunctionObjArgs(getter.get(), avatar, name.get(), nullptr));
                    auto entity = call(lookup().get(), vehicle_id.get());
                    auto* row = overlay(compatibility, entity.get());
                    return entity.get() != Py_None && activePose(row) ? std::move(entity) : p::retain(Py_None);
                } catch (const std::exception&) {
                    if (!PyErr_ExceptionMatches(PyExc_AttributeError) && !PyErr_ExceptionMatches(PyExc_ReferenceError) && !PyErr_ExceptionMatches(PyExc_TypeError)) throw;
                    PyErr_Clear(); return p::retain(Py_None);
                }
            }();
            // Exact SniperCamera reads player().vehicle without an engine attachment.
            if (selected.get() != Py_None) return selected.release();
        }
    }
    return callOriginal(compatibility, original_key, avatar, key);
}
PyObject* compoundGet(PyObject* compatibility, PyObject* appearance, PyObject* key) {
    static PyObject* original_name;
    PyObject* value = callOriginal(compatibility, interned(original_name, "_original_compound_getattribute"), appearance, key);
    if (!value || !isName(key, "_CompoundAppearance__filter")) return value;
    p::Ref held(value);
    const int active = battleActive(compatibility);
    if (active < 0) return nullptr;
    // Only the direct LOAD_ATTR in the original __onModelsRefresh gets the proxy.
    if (!active || !same(compatibility, "_compound_refreshing_models", appearance) ||
        !same(compatibility, "_compound_models_refresh_code", callerCode())) return held.release();
    auto fields = p::dict();
    for (const char* name : {"_pose_matrix", "_velocity", "_acceleration"}) p::set(fields.get(), name, p::retain(Py_None));
    return newProxy(0, held.get(), fields.get());
}
int hookTraverse(PyObject* object, visitproc visit, void* arg) {
    auto* value = reinterpret_cast<Hook*>(object); Py_VISIT(value->compatibility); Py_VISIT(value->instance); return 0;
}
int hookClear(PyObject* object) {
    auto* value = reinterpret_cast<Hook*>(object); Py_CLEAR(value->compatibility); Py_CLEAR(value->instance); return 0;
}
void hookDestroy(PyObject* object) {
    auto* value = reinterpret_cast<Hook*>(object); PyObject_GC_UnTrack(object); Py_XDECREF(value->compatibility); Py_XDECREF(value->instance); Py_TYPE(object)->tp_free(object);
}
PyObject* newHook(PyObject* compatibility, PyObject* instance, int kind) {
    auto* value = reinterpret_cast<Hook*>(hook_type.tp_alloc(&hook_type, 0)); if (!value) return nullptr;
    value->compatibility = compatibility; value->instance = instance; value->kind = kind;
    Py_INCREF(compatibility); Py_XINCREF(instance); return reinterpret_cast<PyObject*>(value);
}
PyObject* hookBind(PyObject* self, PyObject* instance, PyObject*) {
    if (!instance || instance == Py_None) return p::retain(self).release();
    auto* value = reinterpret_cast<Hook*>(self); return newHook(value->compatibility, instance, value->kind);
}
PyObject* hookCall(PyObject* self, PyObject* args, PyObject* kwargs) {
    try {
        auto* hook = reinterpret_cast<Hook*>(self);
        PyObject* compatibility = PyWeakref_GetObject(hook->compatibility);
        if (compatibility == Py_None) throw p::Error("Vehicle compatibility owner is retired");
        auto lease = p::retain(compatibility);
        if (kwargs && PyDict_Size(kwargs) && (hook->kind != 3 || PyDict_Size(kwargs) != 1 || !PyDict_GetItemString(kwargs, "get_instantaneous")))
            throw p::Error("Vehicle hook takes positional arguments");
        const Py_ssize_t count = PyTuple_GET_SIZE(args), offset = hook->instance ? 0 : 1;
        const Py_ssize_t wanted = hook->kind == 0 || hook->kind >= 4 ? 1 : hook->kind == 1 ? 2 : 0;
        if (count - offset < 0 || (hook->kind == 3 ? count - offset > 1 : count - offset != wanted)) {
            PyErr_SetString(PyExc_TypeError, "Vehicle hook argument count differs"); return nullptr;
        }
        PyObject* vehicle = hook->instance ? hook->instance : PyTuple_GET_ITEM(args, 0);
        if (hook->kind == 4) return avatarGet(compatibility, vehicle, PyTuple_GET_ITEM(args, offset));
        if (hook->kind == 5) return compoundGet(compatibility, vehicle, PyTuple_GET_ITEM(args, offset));
        if (hook->kind == 3) {
            PyObject* instantaneous = count - offset ? PyTuple_GET_ITEM(args, offset) : Py_False;
            if (kwargs && PyDict_Size(kwargs)) {
                if (count - offset) { PyErr_SetString(PyExc_TypeError, "Duplicate instantaneous argument"); return nullptr; }
                instantaneous = PyDict_GetItemString(kwargs, "get_instantaneous");
            }
            if (p::truth(compatibility, "_battle_active")) {
                Overlay* row = [&]() -> Overlay* {
                    try {
                        auto getter = attr(compatibility, "_original_avatar_getattribute"); auto name = p::text("playerVehicleID");
                        p::Ref id(PyObject_CallFunctionObjArgs(getter.get(), vehicle, name.get(), nullptr));
                        auto runtime = attr(compatibility, "_runtime"); auto bw = attr(runtime.get(), "bigworld"); auto lookup = attr(bw.get(), "entity");
                        auto entity = call(lookup.get(), id.get()); return overlay(compatibility, entity.get());
                    } catch (const std::exception&) {
                        if (!PyErr_ExceptionMatches(PyExc_AttributeError) && !PyErr_ExceptionMatches(PyExc_ReferenceError) && !PyErr_ExceptionMatches(PyExc_TypeError)) throw;
                        PyErr_Clear(); return nullptr;
                    }
                }();
                if (activePose(row) && field(row, "speed") && field(row, "turn_speed"))
                    return Py_BuildValue("(dd)", row->speed, row->turn_speed);
            }
            auto getter = attr(compatibility, "_original_avatar_get_speeds");
            return PyObject_CallFunctionObjArgs(getter.get(), vehicle, instantaneous, nullptr);
        }
        if (hook->kind == 0) {
            auto* frame = PyEval_GetFrame(); PyObject* code = frame ? reinterpret_cast<PyObject*>(frame->f_code) : Py_None;
            return get(compatibility, vehicle, PyTuple_GET_ITEM(args, offset), code).release();
        }
        if (hook->kind == 1) {
            auto* key = PyTuple_GET_ITEM(args, offset); auto* value = PyTuple_GET_ITEM(args, offset + 1); const auto name = p::string(key);
            if (p::truth(compatibility, "_battle_active") && named(name, {"health", "isCrewActive", "position", "yaw", "matrix"})) {
                const bool mandatory = name == "health" || name == "isCrewActive"; auto* row = overlay(compatibility, vehicle, mandatory);
                if (mandatory || activePose(row)) {
                    row->fields[name] = native_battle::borrow(value);
                    Py_RETURN_NONE;
                }
            }
            auto setter = attr(compatibility, "_original_vehicle_setattr");
            return PyObject_CallFunctionObjArgs(setter.get(), vehicle, key, value, nullptr);
        }
        if (p::truth(compatibility, "_battle_active")) {
            auto* row = overlay(compatibility, vehicle); auto* value = field(row, "speed");
            if (activePose(row) && value) return p::retain(value).release();
        }
        auto getter = attr(compatibility, "_original_vehicle_get_speed"); return call(getter.get(), vehicle).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* create(PyObject*, PyObject* args) {
    try {
        PyObject* compatibility; int kind;
        if (!PyArg_ParseTuple(args, "Oi", &compatibility, &kind)) return nullptr;
        if (kind < 0 || kind > 5) throw p::Error("Unknown stock hook kind");
        p::Ref weak(PyWeakref_NewRef(compatibility, nullptr)); return newHook(weak.get(), nullptr, kind);
    } catch (const std::exception& e) { return fail(e); }
}
PyMethodDef methods[] = {{"create", create, METH_VARARGS, "Create a native stock Vehicle descriptor."}, {nullptr, nullptr, 0, nullptr}};
}
PyObject* initialize() {
    hook_type.tp_name = const_cast<char*>("offline_client_vehicle_hooks_native.Descriptor");
    hook_type.tp_basicsize = sizeof(Hook); hook_type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    hook_type.tp_dealloc = hookDestroy; hook_type.tp_descr_get = hookBind; hook_type.tp_call = hookCall;
    hook_type.tp_traverse = hookTraverse; hook_type.tp_clear = hookClear;
    proxy_type.tp_name = const_cast<char*>("offline_client_vehicle_hooks_native.Provider");
    proxy_type.tp_basicsize = sizeof(Proxy); proxy_type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    proxy_type.tp_dealloc = proxyDestroy; proxy_type.tp_getattro = proxyGet; proxy_type.tp_call = proxyCall; proxy_type.tp_methods = proxy_methods;
    proxy_type.tp_traverse = proxyTraverse; proxy_type.tp_clear = proxyClear;
    if (PyType_Ready(&hook_type) < 0 || PyType_Ready(&proxy_type) < 0) return nullptr;
    return Py_InitModule("offline_client_vehicle_hooks_native", methods);
}
}
}
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
BW_BEGIN_NAMESPACE
namespace {
class NativeVehicleHooksInit : public Script::InitTimeJob {
public: NativeVehicleHooksInit() : Script::InitTimeJob(1) {}
    void init() { if (!native_battle::remote_hooks::initialize()) PyErr_Print(); }
} native_vehicle_hooks_init;
}
BW_END_NAMESPACE
#endif
