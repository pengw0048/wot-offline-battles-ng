// Native stock-hook layer: hook objects, one-time installation into #1513 classes, and per-round battle state.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_compat_internal.hpp"
#include "stock_native_battle_subsystems.hpp"
#include <frameobject.h>
#include <deque>

namespace native_battle {
namespace compat {
const char* const init_complete = "_offlineLANInitComplete";
const char* const player_ready = "_offlineLANPlayerReady";
const char* const retire_pending = "_offlineLANRetirePending";

namespace {
Stock stock_state;
Scopes scope_state;

// ---- hook objects ---------------------------------------------------------------------------------------------
struct Hook { PyObject_HEAD Body body; const char* name; };
PyTypeObject hook_type = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.StockHook"};
PyObject* hookCall(PyObject* self, PyObject* args, PyObject* kwargs) { return reinterpret_cast<Hook*>(self)->body(args, kwargs); }
// Bind like a Python function so an installed hook receives the instance first.
PyObject* hookBind(PyObject* self, PyObject* instance, PyObject* owner) {
    if (!instance || instance == Py_None) { Py_INCREF(self); return self; }
    return PyMethod_New(self, instance, owner);
}
PyObject* hookRepr(PyObject* self) { return PyString_FromFormat("<native stock hook %s>", reinterpret_cast<Hook*>(self)->name); }

PyObject* constantCall(PyObject* self, PyObject*, PyObject*) { Py_INCREF(self); return self; }
PyMethodDef constant_def = {"constant", reinterpret_cast<PyCFunction>(constantCall), METH_VARARGS | METH_KEYWORDS, nullptr};

// ---- surrogates for late #1513 teardown owners ---------------------------------------------------------------
struct Surrogate { PyObject_HEAD int kind; };
PyTypeObject surrogate_type = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.TeardownOwner"};
PyObject* ignore(PyObject*, PyObject*, PyObject*) { Py_RETURN_NONE; }
PyMethodDef ignore_def = {"ignored", reinterpret_cast<PyCFunction>(ignore), METH_VARARGS | METH_KEYWORDS, nullptr};
PyObject* surrogateGet(PyObject* self, PyObject* name) {
    const std::string text = p::string(name);
    const int kind = reinterpret_cast<Surrogate*>(self)->kind;
    try {
        if (kind == SurrogateCollider) {
            if (text == "addVehicleToCameraCollider" || text == "removeVehicleFromCameraCollider") return PyCFunction_New(&ignore_def, nullptr);
            if (text == "onCameraChanged") return surrogate(SurrogateSink).release();
        }
        if (kind == SurrogateArena && text == "onPeriodChange") return surrogate(SurrogateSink).release();
        if (kind == SurrogateOwner) {
            if (text == "inputHandler") return surrogate(SurrogateCollider).release();
            if (text == "arena") return surrogate(SurrogateArena).release();
        }
    } catch (const std::exception&) { return nullptr; }
    return PyObject_GenericGetAttr(self, name);
}
// `owner.onCameraChanged -= callback` assigns the event back; the surrogates accept and drop it.
int surrogateSet(PyObject*, PyObject*, PyObject*) { return 0; }
PyObject* inplaceSelf(PyObject* self, PyObject*) { Py_INCREF(self); return self; }
PyNumberMethods surrogate_number = {};

// ---- the deferred Avatar server ------------------------------------------------------------------------------
// Exact early Avatar requests before the entity binding exists (compat.py _DeferredAvatarServer).
struct Deferred {
    PyObject_HEAD
    PyObject* target;
    std::deque<std::pair<std::string, PyObject*>>* pending;
};
PyTypeObject deferred_type = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.DeferredAvatarServer"};
void deferredDealloc(PyObject* self) {
    auto* d = reinterpret_cast<Deferred*>(self);
    Py_XDECREF(d->target);
    if (d->pending) { for (auto& row : *d->pending) Py_XDECREF(row.second); delete d->pending; }
    PyObject_Del(self);
}
PyObject* forward(Deferred* d, const char* name, PyObject* args) {
    p::Ref method(PyObject_GetAttrString(d->target, name));
    return PyObject_Call(method.get(), args, nullptr);
}
PyObject* defer(PyObject* self, const char* name, PyObject* args) {
    auto* d = reinterpret_cast<Deferred*>(self);
    if (d->target) {
        try { return forward(d, name, args); } catch (const std::exception&) { return nullptr; }
    }
    Py_INCREF(args);
    d->pending->emplace_back(name, args);
    Py_RETURN_NONE;
}
PyObject* immediate(PyObject* self, const char* name, PyObject* args) {
    auto* d = reinterpret_cast<Deferred*>(self);
    if (!d->target) Py_RETURN_NONE;
    try { return forward(d, name, args); } catch (const std::exception&) { return nullptr; }
}
PyObject* setClientReady(PyObject* self, PyObject* args) { return defer(self, "setClientReady", args); }
PyObject* autoAim(PyObject* self, PyObject* args) { return defer(self, "autoAim", args); }
PyObject* doCmdStr(PyObject* self, PyObject* args) { return defer(self, "doCmdStr", args); }
PyObject* doCmdIntArr(PyObject* self, PyObject* args) { return defer(self, "doCmdIntArr", args); }
PyObject* invalidateMicrophoneMute(PyObject* self, PyObject* args) { return immediate(self, "invalidateMicrophoneMute", args); }
PyObject* switchObserverFPV(PyObject* self, PyObject* args) { return immediate(self, "switchObserverFPV", args); }
PyObject* attachMethod(PyObject* self, PyObject* args) {
    PyObject* target;
    if (!PyArg_ParseTuple(args, "O", &target)) return nullptr;
    try { attachDeferredServer(self, target); } catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
    Py_RETURN_NONE;
}
PyMethodDef deferred_methods[] = {
    {"attach", attachMethod, METH_VARARGS, nullptr},
    {"setClientReady", setClientReady, METH_VARARGS, nullptr},
    {"autoAim", autoAim, METH_VARARGS, nullptr},
    {"doCmdStr", doCmdStr, METH_VARARGS, nullptr},
    {"doCmdIntArr", doCmdIntArr, METH_VARARGS, nullptr},
    {"invalidateMicrophoneMute", invalidateMicrophoneMute, METH_VARARGS, nullptr},
    {"switchObserverFPV", switchObserverFPV, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
PyObject* deferredGet(PyObject* self, PyObject* name) {
    const std::string text = p::string(name);
    if (text == "voipController") { Py_INCREF(self); return self; }
    PyObject* found = PyObject_GenericGetAttr(self, name);
    if (found || !PyErr_ExceptionMatches(PyExc_AttributeError)) return found;
    PyErr_Clear();
    auto* d = reinterpret_cast<Deferred*>(self);
    if (!d->target) {
        PyErr_Format(PyExc_AttributeError, "Avatar server is not attached for mailbox %s", text.c_str());
        return nullptr;
    }
    return PyObject_GetAttr(d->target, name);
}

void ready(PyTypeObject& type) {
    if (PyType_Ready(&type) < 0) throw p::Error("native stock hook type failed");
}
void readyTypes() {
    static bool done = false;
    if (done) return;
    hook_type.tp_basicsize = sizeof(Hook);
    hook_type.tp_flags = Py_TPFLAGS_DEFAULT;
    hook_type.tp_call = hookCall;
    hook_type.tp_descr_get = hookBind;
    hook_type.tp_repr = hookRepr;
    ready(hook_type);
    surrogate_number.nb_inplace_add = inplaceSelf;
    surrogate_number.nb_inplace_subtract = inplaceSelf;
    surrogate_type.tp_basicsize = sizeof(Surrogate);
    surrogate_type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES;
    surrogate_type.tp_getattro = surrogateGet;
    surrogate_type.tp_setattro = surrogateSet;
    surrogate_type.tp_as_number = &surrogate_number;
    ready(surrogate_type);
    deferred_type.tp_basicsize = sizeof(Deferred);
    deferred_type.tp_flags = Py_TPFLAGS_DEFAULT;
    deferred_type.tp_dealloc = deferredDealloc;
    deferred_type.tp_getattro = deferredGet;
    deferred_type.tp_methods = deferred_methods;
    ready(deferred_type);
    done = true;
}
}

Stock& stock() { return stock_state; }
Scopes& mutableScopes() { return scope_state; }
const Scopes& scopes() { return scope_state; }
PyObject* code(Code which) { return stock_state.codes[std::size_t(which)].get(); }

Battle* battle() {
    Battle* b = current();
    return b && b->compat && b->compat->active ? b : nullptr;
}
bool active() { return battle() != nullptr; }

p::Ref hook(Body body, const char* name) {
    readyTypes();
    auto* h = PyObject_New(Hook, &hook_type);
    if (!h) throw p::Error("native stock hook allocation failed");
    h->body = body;
    h->name = name;
    return p::Ref(reinterpret_cast<PyObject*>(h));
}
p::Ref constant(PyObject* value) { return p::Ref(PyCFunction_NewEx(&constant_def, value, nullptr)); }
p::Ref surrogate(int kind) {
    readyTypes();
    auto* s = PyObject_New(Surrogate, &surrogate_type);
    if (!s) throw p::Error("native teardown surrogate allocation failed");
    s->kind = kind;
    return p::Ref(reinterpret_cast<PyObject*>(s));
}
p::Ref deferredServer() {
    readyTypes();
    auto* d = PyObject_New(Deferred, &deferred_type);
    if (!d) throw p::Error("native deferred server allocation failed");
    d->target = nullptr;
    d->pending = new std::deque<std::pair<std::string, PyObject*>>();
    return p::Ref(reinterpret_cast<PyObject*>(d));
}
void attachDeferredServer(PyObject* proxy, PyObject* server) {
    if (Py_TYPE(proxy) != &deferred_type) throw p::Error("Avatar deferred server is unavailable");
    auto* d = reinterpret_cast<Deferred*>(proxy);
    if (d->target && d->target != server) throw p::Error("Avatar server is already attached");
    Py_INCREF(server);
    Py_XDECREF(d->target);
    d->target = server;
    std::deque<std::pair<std::string, PyObject*>> pending;
    pending.swap(*d->pending);
    for (auto& row : pending) {
        p::Ref args(row.second);
        p::Ref method(PyObject_GetAttrString(server, row.first.c_str()));
        p::Ref(PyObject_Call(method.get(), args.get(), nullptr));
    }
}

// ---- shared helpers --------------------------------------------------------------------------------------------
PyObject* callerCode() {
    PyFrameObject* frame = PyEval_GetFrame();
    return frame ? reinterpret_cast<PyObject*>(frame->f_code) : nullptr;
}
p::Ref call(const Object& fn, std::initializer_list<PyObject*> args, PyObject* kwargs) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    return p::Ref(PyObject_Call(fn.get(), tuple.get(), kwargs));
}
p::Ref argument(PyObject* args, Py_ssize_t index) {
    if (index >= PyTuple_GET_SIZE(args)) throw p::Error("stock hook received too few arguments");
    return p::retain(PyTuple_GET_ITEM(args, index));
}
bool flagAttribute(PyObject* o, const char* name, bool fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (!v) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
        PyErr_Clear();
        return fallback;
    }
    p::Ref held(v);
    return p::flag(v);
}
p::Ref player() {
    PyObject* v = PyObject_CallMethod(stock_state.bigworld.get(), const_cast<char*>("player"), nullptr);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_ReferenceError)) throw p::Error("BigWorld.player failed");
    PyErr_Clear();
    return p::retain(Py_None);
}
p::Ref nativeVehicleAttribute(PyObject* vehicle, const char* name) {
    if (!stock_state.vehicle_getattribute) throw p::Error("native Vehicle attribute boundary is unavailable");
    return call(stock_state.vehicle_getattribute, {vehicle, p::text(name).get()});
}

bool undrawnLanRemote(PyObject* entity) {
    auto& s = stock_state;
    const bool vehicle = s.vehicle_getattribute && s.vehicle_type && PyObject_IsInstance(entity, s.vehicle_type.get()) == 1;
    PyErr_Clear();
    auto read = [&](const char* name) -> p::Ref {
        if (vehicle) return call(s.vehicle_getattribute, {entity, p::text(name).get()});
        return p::Ref(PyObject_GetAttrString(entity, name));
    };
    try {
        if (!p::flag(read("_offlineNativeRemote").get())) return false;
        return !p::flag(read("_offlineNativeDrawVisible").get());
    } catch (const p::Error&) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_ReferenceError) || PyErr_ExceptionMatches(PyExc_TypeError)) {
            PyErr_Clear();
            return false;
        }
        throw;
    }
}

// ---- installation ---------------------------------------------------------------------------------------------
namespace {
Object captured(const char* name) { return own(py::attr(stock_state.compatibility.get(), name)); }
Object runtimeField(const char* name) { return own(py::optional(stock_state.runtime.get(), name)); }
void replaceAttribute(PyObject* owner, const char* name, PyObject* value) {
    auto& s = stock_state;
    PyObject* previous = PyObject_GetAttrString(owner, name);
    if (!previous) PyErr_Clear();
    s.patches.push_back({borrow(owner), name, Object(previous)});
    if (PyObject_SetAttrString(owner, name, value) < 0) throw p::Error(std::string("stock hook install failed: ") + name);
}
// The first definition of a member below a class in its MRO (the inherited stock method).
p::Ref inheritedMember(PyObject* type, const char* name) {
    PyObject* mro = reinterpret_cast<PyTypeObject*>(type)->tp_mro;
    for (Py_ssize_t i = 1; mro && i < PyTuple_GET_SIZE(mro); ++i) {
        PyObject* base = PyTuple_GET_ITEM(mro, i);
        PyObject* dict = PyType_Check(base) ? reinterpret_cast<PyTypeObject*>(base)->tp_dict : nullptr;
        PyObject* found = dict ? PyDict_GetItemString(dict, name) : nullptr;
        if (found) return p::retain(found);
    }
    throw p::Error(std::string("#1513 inherited stock member is unavailable: ") + name);
}
}

// Hook bodies (stock_native_battle_compat_hooks.cpp).
PyObject* avatarInit(PyObject*, PyObject*);
PyObject* avatarBecomePlayer(PyObject*, PyObject*);
PyObject* avatarBecomeNonPlayer(PyObject*, PyObject*);
PyObject* avatarEnterWorld(PyObject*, PyObject*);
PyObject* avatarLeaveWorld(PyObject*, PyObject*);
PyObject* avatarVehicleEnter(PyObject*, PyObject*);
PyObject* avatarPrereqsLoaded(PyObject*, PyObject*);
PyObject* avatarAuxPhysics(PyObject*, PyObject*);
PyObject* avatarAutoAim(PyObject*, PyObject*);
PyObject* ammoChangeSetting(PyObject*, PyObject*);
PyObject* arcadeKeyEvent(PyObject*, PyObject*);
PyObject* sniperKeyEvent(PyObject*, PyObject*);
PyObject* strategicCameraUpdate(PyObject*, PyObject*);
PyObject* controlModeChanged(PyObject*, PyObject*);
PyObject* setServerMarker(PyObject*, PyObject*);
PyObject* useClientGunMarker(PyObject*, PyObject*);
PyObject* useServerGunMarker(PyObject*, PyObject*);
PyObject* markerStart(PyObject*, PyObject*);
PyObject* markerStop(PyObject*, PyObject*);
PyObject* linkOwnVehicle(PyObject*, PyObject*);
PyObject* relinkSources(PyObject*, PyObject*);
PyObject* vehicleEnterWorld(PyObject*, PyObject*);
PyObject* vehicleStartVisual(PyObject*, PyObject*);
PyObject* vehicleLeaveWorld(PyObject*, PyObject*);
PyObject* vehicleStartWgPhysics(PyObject*, PyObject*);
PyObject* vehicleSetGunAngles(PyObject*, PyObject*);
PyObject* vehicleCollideSegment(PyObject*, PyObject*);
PyObject* vehicleCollideSegmentExt(PyObject*, PyObject*);
PyObject* projectileSegmentMayHit(PyObject*, PyObject*);
PyObject* projectileCollidableEntities(PyObject*, PyObject*);
PyObject* compoundDeactivate(PyObject*, PyObject*);
PyObject* compoundModelsRefresh(PyObject*, PyObject*);
PyObject* compoundEffectsLod(PyObject*, PyObject*);
PyObject* serverTime(PyObject*, PyObject*);
namespace {
// The team-command readers' target: the native cursor owner of the current battle.
PyObject* teamCommandTargetHook(PyObject*, PyObject*) {
    try {
        Battle* b = current();
        if (!b) Py_RETURN_NONE;
        return teamCommandTarget(*b).release();
    } catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
}

// Installs the native battle hooks once. compat.py's lobby-wide install has captured every original.
bool install(Battle& b) {
    auto& s = stock_state;
    if (s.installed) return true;
    if (!b.engine.compatibility) throw p::Error("offline compatibility is not installed");
    readyTypes();
    s.compatibility = b.engine.compatibility;
    if (!p::flag(py::attr(s.compatibility.get(), "_installed").get())) throw p::Error("offline compatibility is not installed");
    s.runtime = captured("_runtime");
    s.bigworld = runtimeField("bigworld");
    s.math = runtimeField("math");
    s.constants = runtimeField("constants");
    s.avatar_module = runtimeField("avatar_module");
    s.avatar_type = own(py::attr(s.avatar_module.get(), "PlayerAvatar"));
    s.vehicle_type = own(py::optional(py::optional(s.runtime.get(), "vehicle_module").get(), "Vehicle"));
    s.compound_type = own(py::optional(py::optional(s.runtime.get(), "compound_appearance_module").get(), "CompoundAppearance"));
    s.marker_type = runtimeField("vehicle_marker_plugin_type");
    s.ammo_controller_type = runtimeField("ammo_controller_type");
    s.input_handler_type = own(py::optional(py::optional(s.runtime.get(), "avatar_input_handler").get(), "AvatarInputHandler"));
    auto modes = py::optional(s.runtime.get(), "control_modes");
    s.arcade_type = own(py::optional(modes.get(), "ArcadeControlMode"));
    s.sniper_type = own(py::optional(modes.get(), "SniperControlMode"));
    s.strategic_camera_type = runtimeField("strategic_camera_type");
    s.consistent_type = own(py::optional(py::optional(s.runtime.get(), "avatar_position_control").get(), "ConsistentMatrices"));
    s.steady_type = own(py::optional(py::optional(s.runtime.get(), "steady_vehicle_matrix").get(), "SteadyVehicleMatrixCalculator"));
    s.gun_rotator_type = own(py::optional(py::optional(s.runtime.get(), "vehicle_gun_rotator").get(), "VehicleGunRotator"));
    s.gun_marker_module = runtimeField("gun_marker_ctrl");
    s.projectile_mover = runtimeField("projectile_mover_module");
    s.debug_panel_type = runtimeField("debug_panel_type");
    s.consumables_panel_type = runtimeField("consumables_panel_type");
    s.player_messages_type = runtimeField("player_messages_type");
    s.postmortem_panel_type = runtimeField("postmortem_panel_type");
    s.effect_manager_type = runtimeField("custom_effect_manager_type");
    s.offline_map_creator = runtimeField("offline_map_creator");
    s.chat_manager = runtimeField("chat_manager");
    s.avatar_getter = runtimeField("avatar_getter");
    s.segment_collision_result = runtimeField("segment_collision_result_type");
    s.message_color_format = runtimeField("battle_message_color_format");
    s.nice_number_format = own(py::optional(s.bigworld.get(), "wg_getNiceNumberFormat"));
    // Originals the lobby-wide install captured before it wrapped each stock method.
    s.avatar_init = captured("_original_avatar_init");
    s.avatar_getattribute = captured("_original_avatar_getattribute");
    s.avatar_become_player = captured("_original_avatar_become_player");
    s.avatar_become_non_player = captured("_original_avatar_become_non_player");
    s.avatar_enter_world = captured("_original_avatar_enter_world");
    s.avatar_leave_world = captured("_original_avatar_leave_world");
    s.avatar_vehicle_enter = captured("_original_avatar_vehicle_enter");
    s.avatar_prereqs_loaded = captured("_original_avatar_prereqs_loaded");
    s.avatar_aux_physics = captured("_original_avatar_aux_physics");
    s.avatar_auto_aim = captured("_original_avatar_auto_aim");
    s.ammo_change_setting = own(py::optional(s.compatibility.get(), "_original_ammo_change_setting"));
    s.arcade_key_event = captured("_original_arcade_handle_key_event");
    s.sniper_key_event = captured("_original_sniper_handle_key_event");
    s.strategic_camera_update = captured("_original_strategic_camera_update");
    s.control_mode_changed = captured("_original_control_mode_changed");
    s.server_marker_property = captured("_original_gun_server_marker");
    s.use_client_marker = captured("_original_use_client_gun_marker");
    s.use_server_marker = captured("_original_use_server_gun_marker");
    s.marker_start = captured("_original_vehicle_marker_start");
    s.marker_stop = captured("_original_vehicle_marker_stop");
    s.link_own_vehicle = captured("_original_consistent_link_own_vehicle");
    s.relink_sources = captured("_original_steady_relink_sources");
    s.segment_may_hit = captured("_original_projectile_segment_may_hit");
    s.collidable_entities = captured("_original_projectile_collidable_entities");
    s.python_collidable_entities = own(py::optional(s.compatibility.get(), "_projectile_collidable_entities_wrapper"));
    s.server_time = captured("_original_server_time");
    s.debug_update = own(py::optional(s.compatibility.get(), "_original_debug_update"));
    s.shell_tooltip = own(py::optional(s.compatibility.get(), "_original_shell_tooltip"));
    s.postmortem_populate = own(py::optional(s.compatibility.get(), "_original_postmortem_populate"));
    s.effect_update = own(py::optional(s.compatibility.get(), "_original_effect_update"));
    if (s.vehicle_type) {
        s.vehicle_getattribute = captured("_original_vehicle_getattribute");
        s.vehicle_enter_world = captured("_original_vehicle_enter_world");
        s.vehicle_start_visual = captured("_original_vehicle_start_visual");
        s.vehicle_leave_world = captured("_original_vehicle_leave_world");
        s.vehicle_start_wg_physics = captured("_original_vehicle_start_wg_physics");
        s.vehicle_set_gun_angles = captured("_original_vehicle_set_gun_angles");
        s.vehicle_collide_segment = captured("_original_vehicle_collide_segment");
        s.vehicle_collide_segment_ext = captured("_original_vehicle_collide_segment_ext");
    }
    if (s.compound_type) {
        s.compound_getattribute = captured("_original_compound_getattribute");
        s.compound_deactivate = captured("_original_compound_deactivate");
        s.compound_models_refresh = captured("_original_compound_models_refresh");
        s.compound_effects_lod = captured("_original_compound_effects_lod");
    }
    // PlayerMessages.showMessage before the lobby-wide wrapper (its stored original may be the inherited one).
    if (s.player_messages_type) {
        auto stored = py::optional(s.compatibility.get(), "_original_player_message_show");
        s.player_message_show = stored.get() != Py_None ? own(std::move(stored))
                                                        : own(inheritedMember(s.player_messages_type.get(), "showMessage"));
    }
    // Stock code objects the scoped attribute hooks compare against.
    auto set = [&](Code which, const char* field) { s.codes[std::size_t(which)] = own(py::optional(s.compatibility.get(), field)); };
    set(Code::AvatarVehicleEnter, "_avatar_vehicle_enter_code");
    set(Code::AvatarStartVehicleVisual, "_avatar_start_vehicle_visual_code");
    set(Code::VehicleStartVisual, "_vehicle_start_visual_code");
    set(Code::VehicleStartWgPhysics, "_vehicle_start_wg_physics_code");
    set(Code::VehicleSetGunAngles, "_vehicle_set_gun_angles_code");
    set(Code::AvatarAuxPhysics, "_avatar_aux_physics_code");
    set(Code::GunRotatorStabilised, "_gun_rotator_stabilised_code");
    set(Code::GunRotatorPredictLockedTarget, "_gun_rotator_predict_locked_target_code");
    set(Code::ProjectileSegmentMayHit, "_projectile_segment_may_hit_code");
    set(Code::CameraAccelerationUpdate, "_camera_acceleration_update_code");
    set(Code::ArcadeOscillatorAcceleration, "_arcade_oscillator_acceleration_code");
    set(Code::SniperOscillatorAcceleration, "_sniper_oscillator_acceleration_code");
    set(Code::CustomEffectUpdate, "_custom_effect_update_code");
    set(Code::CrashedTrackSetupAssembler, "_crashed_track_setup_assembler_code");
    set(Code::CrashedTrackModelLoaded, "_crashed_track_model_loaded_code");
    set(Code::CompoundModelsRefresh, "_compound_models_refresh_code");
    set(Code::ArcadeHandleKeyEvent, "_arcade_handle_key_event_code");
    set(Code::SniperHandleKeyEvent, "_sniper_handle_key_event_code");

    auto install_hook = [&](const Object& owner, const char* name, Body body, bool when = true) {
        if (!owner || !when) return;
        auto value = hook(body, name);
        replaceAttribute(owner.get(), name, value.get());
    };
    const auto& avatar = s.avatar_type;
    install_hook(avatar, "__init__", avatarInit);
    install_hook(avatar, "onBecomePlayer", avatarBecomePlayer);
    install_hook(avatar, "onBecomeNonPlayer", avatarBecomeNonPlayer, bool(s.avatar_become_non_player) && s.avatar_become_non_player.get() != Py_None);
    install_hook(avatar, "onEnterWorld", avatarEnterWorld, s.avatar_enter_world.get() != Py_None);
    install_hook(avatar, "onLeaveWorld", avatarLeaveWorld, s.avatar_leave_world.get() != Py_None);
    install_hook(avatar, "vehicle_onEnterWorld", avatarVehicleEnter, s.avatar_vehicle_enter.get() != Py_None);
    install_hook(avatar, "onPrereqsLoaded", avatarPrereqsLoaded, s.avatar_prereqs_loaded.get() != Py_None);
    install_hook(avatar, "_PlayerAvatar__onSetOwnVehicleAuxPhysicsData", avatarAuxPhysics, s.avatar_aux_physics.get() != Py_None);
    install_hook(avatar, "autoAim", avatarAutoAim);
    install_hook(s.ammo_controller_type, "changeSetting", ammoChangeSetting, bool(s.ammo_change_setting) && s.ammo_change_setting.get() != Py_None);
    install_hook(s.arcade_type, "handleKeyEvent", arcadeKeyEvent);
    install_hook(s.sniper_type, "handleKeyEvent", sniperKeyEvent);
    install_hook(s.strategic_camera_type, "_StrategicCamera__cameraUpdate", strategicCameraUpdate);
    install_hook(s.input_handler_type, "onControlModeChanged", controlModeChanged);
    {
        // VehicleGunRotator.showServerMarker: stock getter/deleter, the battle-owned setter.
        auto prop = s.server_marker_property.get();
        auto setter = hook(setServerMarker, "showServerMarker.setter");
        p::Ref args(PyTuple_Pack(4, py::attr(prop, "fget").get(), setter.get(), py::attr(prop, "fdel").get(), py::attr(prop, "__doc__").get()));
        p::Ref replaced(PyObject_Call(reinterpret_cast<PyObject*>(&PyProperty_Type), args.get(), nullptr));
        replaceAttribute(s.gun_rotator_type.get(), "showServerMarker", replaced.get());
    }
    install_hook(s.gun_marker_module, "useClientGunMarker", useClientGunMarker);
    install_hook(s.gun_marker_module, "useServerGunMarker", useServerGunMarker);
    install_hook(s.marker_type, "start", markerStart);
    install_hook(s.marker_type, "stop", markerStop);
    install_hook(s.consistent_type, "_ConsistentMatrices__linkOwnVehicle", linkOwnVehicle);
    install_hook(s.steady_type, "relinkSources", relinkSources);
    if (s.vehicle_type) {
        install_hook(s.vehicle_type, "onEnterWorld", vehicleEnterWorld, s.vehicle_enter_world.get() != Py_None);
        install_hook(s.vehicle_type, "startVisual", vehicleStartVisual, s.vehicle_start_visual.get() != Py_None);
        install_hook(s.vehicle_type, "onLeaveWorld", vehicleLeaveWorld, s.vehicle_leave_world.get() != Py_None);
        install_hook(s.vehicle_type, "_Vehicle__startWGPhysics", vehicleStartWgPhysics, s.vehicle_start_wg_physics.get() != Py_None);
        install_hook(s.vehicle_type, "set_gunAnglesPacked", vehicleSetGunAngles, s.vehicle_set_gun_angles.get() != Py_None);
        install_hook(s.vehicle_type, "collideSegment", vehicleCollideSegment);
        install_hook(s.vehicle_type, "collideSegmentExt", vehicleCollideSegmentExt);
    }
    install_hook(s.projectile_mover, "segmentMayHitEntity", projectileSegmentMayHit);
    {
        // ProjectileMover.getCollidableEntities and every module that imported compat.py's wrapper.
        auto replacement = hook(projectileCollidableEntities, "getCollidableEntities");
        replaceAttribute(s.projectile_mover.get(), "getCollidableEntities", replacement.get());
        PyObject* modules = PyImport_GetModuleDict();
        Py_ssize_t cursor = 0;
        PyObject *name, *module;
        std::vector<PyObject*> owners;
        while (PyDict_Next(modules, &cursor, &name, &module)) {
            if (!module || module == Py_None || module == s.projectile_mover.get() || !PyModule_Check(module)) continue;
            PyObject* dict = PyModule_GetDict(module);
            PyObject* bound = dict ? PyDict_GetItemString(dict, "getCollidableEntities") : nullptr;
            if (bound && s.python_collidable_entities && bound == s.python_collidable_entities.get()) owners.push_back(module);
        }
        for (PyObject* owner : owners) replaceAttribute(owner, "getCollidableEntities", replacement.get());
    }
    if (s.compound_type) {
        install_hook(s.compound_type, "deactivate", compoundDeactivate, s.compound_deactivate.get() != Py_None);
        install_hook(s.compound_type, "_CompoundAppearance__onModelsRefresh", compoundModelsRefresh, s.compound_models_refresh.get() != Py_None);
        install_hook(s.compound_type, "_CompoundAppearance__updateEffectsLOD", compoundEffectsLod, s.compound_effects_lod.get() != Py_None);
    }
    install_hook(s.bigworld, "serverTime", serverTime);
    install_hook(s.debug_panel_type, "updateDebugInfo", debugUpdate, bool(s.debug_update) && s.debug_update.get() != Py_None);
    install_hook(s.consumables_panel_type, "_ConsumablesPanel__makeShellTooltip", shellTooltip, bool(s.shell_tooltip) && s.shell_tooltip.get() != Py_None);
    install_hook(s.player_messages_type, "showMessage", showPlayerMessage);
    install_hook(s.postmortem_panel_type, "_populate", postmortemPopulate, bool(s.postmortem_populate) && s.postmortem_populate.get() != Py_None);
    install_hook(s.effect_manager_type, "update", effectUpdate, bool(s.effect_update) && s.effect_update.get() != Py_None);
    {
        // Each stock chat/radial reader resolves BigWorld.target through a picker that asks for the cursor owner.
        auto readers = py::optional(s.compatibility.get(), "_team_command_readers");
        auto getter = hook(teamCommandTargetHook, "team_command_target");
        p::Ref rows(PySequence_List(readers.get() != Py_None ? readers.get() : p::list().get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            auto patched = p::item(PyList_GET_ITEM(rows.get(), i), 3);
            auto globals = py::attr(patched.get(), "func_globals");
            PyObject* world = PyDict_GetItemString(globals.get(), "BigWorld");
            if (!world) throw p::Error("#1513 team-command reader has no BigWorld binding");
            auto picker = py::attr(world, "target");
            replaceAttribute(picker.get(), "_target_getter", getter.get());
        }
    }
    s.installed = true;
    return true;
}

// Restores what each attribute held before the native install, newest first.
void uninstall(Battle&) {
    auto& s = stock_state;
    while (!s.patches.empty()) {
        auto patch = std::move(s.patches.back());
        s.patches.pop_back();
        if (patch.previous) PyObject_SetAttrString(patch.owner.get(), patch.name.c_str(), patch.previous.get());
        else PyObject_DelAttrString(patch.owner.get(), patch.name.c_str());
        PyErr_Clear();
    }
    s.installed = false;
}

// ---- per-round battle API --------------------------------------------------------------------------------------
namespace {
Compat& state(Battle& b) {
    if (!b.compat) b.compat = std::make_unique<Compat>();
    return *b.compat;
}
std::string entityBytes(PyObject* value, const char* fallback) {
    if (!value || value == Py_None) return fallback;
    if (PyString_Check(value)) return std::string(PyString_AS_STRING(value), std::size_t(PyString_GET_SIZE(value)));
    if (PyUnicode_Check(value)) {
        p::Ref encoded(PyUnicode_AsUTF8String(value));
        return std::string(PyString_AS_STRING(encoded.get()), std::size_t(PyString_GET_SIZE(encoded.get())));
    }
    return p::string(p::Ref(PyObject_Str(value)).get());
}
void requireActive(Battle& b, const char* message) {
    if (!b.compat || !b.compat->active) throw p::Error(message);
}
}

void activateMap(Battle&) {
    py::call(stock_state.offline_map_creator.get(), "SetActive", Py_True);
}

// compat.py configure_battle: the normal battle UI/input path for the next native Avatar.
void configure(Battle& b, PyObject* gui_type, PyObject* bonus_type, PyObject* player_name, PyObject* player_team, PyObject* arena_type_id) {
    auto& c = state(b);
    if (player_name && player_name != Py_None) c.player_name = entityBytes(player_name, "OfflinePlayer");
    if (player_team && player_team != Py_None) {
        const std::int64_t team = p::wide(p::Ref(PyNumber_Int(player_team)).get());
        if (team != 1 && team != 2) {
            PyErr_SetString(PyExc_ValueError, "Avatar team must be 1 or 2");
            throw p::Error("Avatar team must be 1 or 2");
        }
        c.player_team = team;
    }
    if (arena_type_id && arena_type_id != Py_None) c.arena_type_id = p::wide(p::Ref(PyNumber_Int(arena_type_id)).get());
    install(b);
    c.active = true;
    c.strategic_camera_failure_reported = false;
    c.player_vehicle_id = 0;
    c.postmortem_vehicle_id = 0;
    c.target_lock_candidate = Object();
    c.target_focus_clear_attempted = false;
    c.gui_type = borrow(gui_type && gui_type != Py_None ? gui_type : nullptr);
    c.bonus_type = borrow(bonus_type && bonus_type != Py_None ? bonus_type : nullptr);
    c.server_time_origin = p::scalar(call(stock_state.server_time, {}).get());
    c.clock_origin = p::scalar(py::call(stock_state.bigworld.get(), "time").get());
    // The lobby-wide layer keeps its own battle flag for the hooks that stay Python.
    py::set(stock_state.compatibility.get(), "_battle_active", Py_True);
    activateMap(b);
}

// compat.py deactivate_map: the hooks stay installed and behave as stock from here on.
void deactivate(Battle& b) {
    auto reset = [&] {
        if (b.compat) *b.compat = Compat();
        if (stock_state.compatibility && PyObject_SetAttrString(stock_state.compatibility.get(), "_battle_active", Py_False) < 0) PyErr_Clear();
    };
    PyObject* done = stock_state.offline_map_creator ? PyObject_CallMethod(stock_state.offline_map_creator.get(), const_cast<char*>("SetActive"),
                                                                           const_cast<char*>("O"), Py_False)
                                                     : (Py_INCREF(Py_None), Py_None);
    if (!done) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        reset();
        PyErr_Restore(type, value, trace);
        throw p::Error("offline map deactivation failed");
    }
    Py_DECREF(done);
    reset();
}

bool synchroniseMarkerIdentity(Battle& b, std::int64_t expected) {
    requireActive(b, "#1513 vehicle-marker identity requires an active battle");
    if (expected <= 0) throw p::Error("#1513 vehicle-marker identity is invalid");
    auto& c = *b.compat;
    for (auto& plugin : c.marker_plugins)
        if (!PyObject_HasAttrString(plugin.second.get(), "_VehicleMarkerPlugin__playerVehicleID"))
            throw p::Error("#1513 vehicle-marker player identity cache is missing");
    c.player_vehicle_id = expected;
    for (auto& plugin : c.marker_plugins) py::set(plugin.second.get(), "_VehicleMarkerPlugin__playerVehicleID", p::integer(expected).get());
    return assertMarkerIdentity(b, expected);
}

bool assertMarkerIdentity(Battle& b, std::int64_t expected) {
    const std::int64_t runtime_id = b.compat ? b.compat->player_vehicle_id : 0;
    if (runtime_id != expected)
        throw p::Error("#1513 vehicle-marker player identity mismatch: expected=" + std::to_string(expected) + " runtime=" + std::to_string(runtime_id));
    for (auto& plugin : b.compat->marker_plugins) {
        PyObject* cached = PyObject_GetAttrString(plugin.second.get(), "_VehicleMarkerPlugin__playerVehicleID");
        if (!cached) { PyErr_Clear(); throw p::Error("#1513 vehicle-marker player identity cache is missing"); }
        p::Ref held(cached);
        p::Ref expected_value(p::integer(expected));
        if (PyObject_RichCompareBool(cached, expected_value.get(), Py_EQ) != 1)
            throw p::Error("#1513 vehicle-marker player identity mismatch: expected=" + std::to_string(expected) + " cached=" +
                           p::string(p::Ref(PyObject_Str(cached)).get()));
    }
    return true;
}

// compat.py assert_vehicle_marker_damage_type: the active stock plugin's exact attacker classifier.
bool assertMarkerDamageType(Battle& b, PyObject* avatar, std::int64_t expected) {
    assertMarkerIdentity(b, expected);
    auto& c = *b.compat;
    if (c.marker_plugins.empty()) throw p::Error("#1513 active vehicle-marker plugin is unavailable");
    auto provider = py::optional(avatar, "guiSessionProvider");
    if (!py::callable(py::optional(provider.get(), "setVehicleHealth"))) throw p::Error("#1513 remote vehicle health presenter is unavailable");
    auto get_arena_dp = py::optional(provider.get(), "getArenaDP");
    if (!py::callable(get_arena_dp)) throw p::Error("#1513 ArenaDP provider is unavailable");
    auto arena_dp = py::invoke(get_arena_dp.get());
    auto get_info = py::optional(arena_dp.get(), "getVehicleInfo");
    if (!py::callable(get_info)) throw p::Error("#1513 ArenaDP vehicle-info API is unavailable");
    auto info = py::invoke(get_info.get(), p::integer(expected).get());
    std::int64_t attacker = 0;
    try { attacker = p::wide(p::Ref(PyNumber_Int(py::attr(info.get(), "vehicleID").get())).get()); }
    catch (const p::Error&) { PyErr_Clear(); throw p::Error("#1513 ArenaDP attacker vehicle info is invalid"); }
    if (attacker != expected)
        throw p::Error("#1513 ArenaDP attacker identity mismatch: expected=" + std::to_string(expected) + " arenaDP=" + std::to_string(attacker));
    if (py::optional(py::optional(provider.get(), "shared").get(), "feedback").get() == Py_None)
        throw p::Error("#1513 active battle feedback adaptor is unavailable");
    auto types = py::optional(stock_state.runtime.get(), "vehicle_marker_damage_type");
    auto expected_type = py::optional(types.get(), "FROM_PLAYER");
    if (expected_type.get() == Py_None) throw p::Error("#1513 vehicle-marker FROM_PLAYER type is unavailable");
    for (auto& entry : c.marker_plugins) {
        PyObject* plugin = entry.second.get();
        auto plugin_provider = py::optional(plugin, "sessionProvider");
        if (plugin_provider.get() != provider.get()) throw p::Error("#1513 active vehicle-marker provider mismatch");
        auto plugin_dp = py::optional(plugin_provider.get(), "getArenaDP");
        if (!py::callable(plugin_dp) || py::invoke(plugin_dp.get()).get() != arena_dp.get()) throw p::Error("#1513 active vehicle-marker ArenaDP mismatch");
        auto classifier = py::optional(plugin, "_VehicleMarkerPlugin__getVehicleDamageType");
        if (!py::callable(classifier)) throw p::Error("#1513 vehicle-marker damage classifier is unavailable");
        auto actual = py::invoke(classifier.get(), info.get());
        if (PyObject_RichCompareBool(actual.get(), expected_type.get(), Py_EQ) != 1)
            throw p::Error("#1513 vehicle-marker damage classification mismatch: expected=" + p::string(p::Ref(PyObject_Str(expected_type.get())).get()) +
                           " actual=" + p::string(p::Ref(PyObject_Str(actual.get())).get()) + " attacker=" + std::to_string(attacker));
    }
    return true;
}

void setNetworkClient(Battle& b, PyObject* client) { state(b).network_client = borrow(client && client != Py_None ? client : nullptr); }

void setControlModeListener(Battle& b, PyObject* listener) {
    if (listener && listener != Py_None && !PyCallable_Check(listener)) {
        PyErr_SetString(PyExc_TypeError, "control-mode listener must be callable");
        throw p::Error("control-mode listener must be callable");
    }
    state(b).control_mode_listener = borrow(listener && listener != Py_None ? listener : nullptr);
}

bool setTargetLockCandidate(Battle& b, PyObject* vehicle) {
    if (vehicle && vehicle != Py_None) {
        if (!b.compat || !b.compat->active) throw p::Error("target-lock candidate requires a battle");
        const bool presentation = flagAttribute(vehicle, "_offlineLANPresentation", false);
        if (!presentation && !flagAttribute(vehicle, "_offlineNativeRemote", false)) {
            PyErr_SetString(PyExc_TypeError, "target-lock candidate is not a remote Vehicle");
            throw p::Error("target-lock candidate is not a remote Vehicle");
        }
        if (presentation && py::optional(vehicle, "bw_entity").get() == Py_None) {
            PyErr_SetString(PyExc_ValueError, "target-lock candidate has no visual entity");
            throw p::Error("target-lock candidate has no visual entity");
        }
    }
    state(b).target_lock_candidate = borrow(vehicle && vehicle != Py_None ? vehicle : nullptr);
    return true;
}

// compat.py clear_target_focus: release the native mouse target while its battle owners are live.
bool clearTargetFocus(Battle& b) {
    if (!b.compat || !b.compat->active || b.compat->target_focus_clear_attempted) return false;
    b.compat->target_lock_candidate = Object();
    auto target = py::optional(stock_state.compatibility.get(), "_original_target");
    if (!py::callable(target)) throw p::Error("#1513 BigWorld.target is unavailable");
    auto clear = py::optional(target.get(), "clear");
    if (!py::callable(clear)) throw p::Error("#1513 BigWorld target-clear boundary is unavailable");
    // clear re-enters PlayerAvatar.targetBlur synchronously; the token goes first.
    b.compat->target_focus_clear_attempted = true;
    py::invoke(clear.get());
    return true;
}

bool releaseTargetLock(Battle& b, PyObject* avatar, std::int64_t vehicle_id) {
    if (!b.compat || !b.compat->active) return false;
    auto current_id = call(stock_state.avatar_getattribute, {avatar, p::text("_PlayerAvatar__autoAimVehID").get()});
    if (!p::flag(current_id.get()) || p::wide(p::Ref(PyNumber_Int(current_id.get())).get()) != vehicle_id) return false;
    call(stock_state.avatar_auto_aim, {avatar, Py_None});
    return true;
}

// compat.py _team_command_target: the occlusion-checked cursor owner, never a lock target.
p::Ref teamCommandTarget(Battle& b) {
    if (!b.compat || !b.compat->active || !b.compat->target_lock_candidate) return p::retain(Py_None);
    PyObject* vehicle = b.compat->target_lock_candidate.get();
    auto alive = py::optional(vehicle, "isAlive");
    if (py::callable(alive) ? !p::flag(py::invoke(alive.get()).get()) : !p::flag(alive.get())) return p::retain(Py_None);
    if (!flagAttribute(vehicle, "_spot_visible", true)) return p::retain(Py_None);
    auto entity = py::optional(vehicle, "bw_entity");
    if (!p::flag(entity.get())) replace(entity, p::retain(vehicle));
    if (py::optional(entity.get(), "model").get() == Py_None || !flagAttribute(entity.get(), "inWorld", false)) return p::retain(Py_None);
    return entity;
}

std::int64_t setPostmortemVehicle(Battle& b, std::int64_t vehicle_id) {
    requireActive(b, "postmortem attachment requires a battle");
    if (vehicle_id < 0) {
        PyErr_SetString(PyExc_ValueError, "postmortem vehicle id is invalid");
        throw p::Error("postmortem vehicle id is invalid");
    }
    if (vehicle_id && py::call(stock_state.bigworld.get(), "entity", p::integer(vehicle_id).get()).get() == Py_None)
        throw p::Error("postmortem vehicle is unavailable");
    const std::int64_t previous = b.compat->postmortem_vehicle_id;
    b.compat->postmortem_vehicle_id = vehicle_id;
    return previous;
}

std::int64_t clearPostmortemVehicle(Battle& b) {
    if (!b.compat) return 0;
    const std::int64_t previous = b.compat->postmortem_vehicle_id;
    b.compat->postmortem_vehicle_id = 0;
    return previous;
}

// compat.py clear_postmortem_killer: the delayed death camera stays on the local wreck.
bool clearPostmortemKiller(Battle& b, PyObject* avatar) {
    requireActive(b, "postmortem killer requires an active battle");
    auto handler = py::optional(avatar, "inputHandler");
    auto setter = py::optional(handler.get(), "setKillerVehicleID");
    if (!py::callable(setter)) throw p::Error("#1513 postmortem killer boundary is unavailable");
    py::invoke(setter.get(), Py_None);
    return true;
}

// compat.py bind_vehicle_pose_sources: every stock pose provider bound to the live overlay matrix.
bool bindVehiclePoseSources(Battle& b, PyObject* avatar, PyObject* vehicle) {
    auto overlay = b.compat && b.compat->active ? remote::poseOverlay(b, vehicle) : std::nullopt;
    if (!overlay) throw p::Error("player pose source requires a live overlay");
    PyObject* matrix = overlay->matrix.get();
    PyObject* stabilised_matrix = overlay->stabilised ? overlay->stabilised.get() : matrix;
    auto matrices = py::optional(avatar, "consistentMatrices");
    if (matrices.get() == Py_None) throw p::Error("#1513 ConsistentMatrices is unavailable");
    auto link = py::optional(matrices.get(), "_ConsistentMatrices__linkOwnVehicle");
    auto attached = py::optional(matrices.get(), "_ConsistentMatrices__setTarget");
    if (!py::callable(link) || !py::callable(attached)) throw p::Error("#1513 vehicle matrix binding methods are unavailable");
    py::invoke(link.get(), vehicle);
    py::invoke(attached.get(), matrix, Py_False);
    auto stabilised = py::optional(avatar, "_PlayerAvatar__ownVehicleStabMProv");
    if (stabilised.get() == Py_None) throw p::Error("#1513 player stabilised matrix provider is unavailable");
    py::set(stabilised.get(), "target", stabilised_matrix);
    if (py::attr(stabilised.get(), "target").get() != stabilised_matrix) throw p::Error("#1513 player stabilised matrix rejected live pose");
    auto handler = py::optional(avatar, "inputHandler");
    auto calculator = py::optional(handler.get(), "steadyVehicleMatrixCalculator");
    auto relink = py::optional(calculator.get(), "relinkSources");
    if (!py::callable(relink)) throw p::Error("#1513 steady vehicle matrix relink is unavailable");
    py::invoke(relink.get());
    return true;
}

// compat.py restore_vehicle_pose_sources: the stock providers after the live overlay is cleared.
bool restoreVehiclePoseSources(Battle& b, PyObject* avatar, PyObject* vehicle, PyObject* native_matrix, PyObject* native_stabilised) {
    if (remote::poseOverlay(b, vehicle)) throw p::Error("player pose overlay must be cleared before source restore");
    auto matrices = py::optional(avatar, "consistentMatrices");
    if (matrices.get() == Py_None) throw p::Error("#1513 ConsistentMatrices is unavailable");
    auto attached = py::optional(matrices.get(), "_ConsistentMatrices__setTarget");
    if (!py::callable(attached)) throw p::Error("#1513 attached vehicle matrix boundary is unavailable");
    call(stock_state.link_own_vehicle, {matrices.get(), vehicle});
    py::invoke(attached.get(), native_matrix, Py_False);
    auto stabilised = py::optional(avatar, "_PlayerAvatar__ownVehicleStabMProv");
    if (stabilised.get() == Py_None) throw p::Error("#1513 player stabilised matrix provider is unavailable");
    py::set(stabilised.get(), "target", native_stabilised);
    auto handler = py::optional(avatar, "inputHandler");
    auto calculator = py::optional(handler.get(), "steadyVehicleMatrixCalculator");
    if (calculator.get() == Py_None) throw p::Error("#1513 steady vehicle matrix calculator is unavailable");
    call(stock_state.relink_sources, {calculator.get()});
    return true;
}

// compat.py attach_avatar_server: the deferred proxy binds its bridge; every other entity's cell uses it too.
void attachAvatarServer(Battle&, PyObject* avatar, PyObject* server) {
    auto proxy = py::optional(avatar, "fakeServer");
    if (proxy.get() == Py_None) throw p::Error("Avatar deferred server is unavailable");
    attachDeferredServer(proxy.get(), server);
    auto entities = py::optional(stock_state.bigworld.get(), "entities");
    if (entities.get() == Py_None) return;
    p::Ref values(PyObject_CallMethod(entities.get(), const_cast<char*>("values"), nullptr));
    p::Ref rows(PySequence_List(values.get()));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
        PyObject* entity = PyList_GET_ITEM(rows.get(), i);
        if (entity == avatar) continue;
        if (PyObject_SetAttrString(entity, "fakeCell", proxy.get()) < 0) PyErr_Clear();
    }
}
}
}
