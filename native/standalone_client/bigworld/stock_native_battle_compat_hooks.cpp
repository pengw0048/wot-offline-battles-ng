// Battle-time stock hook bodies: each takes its battle branch while a configured Battle is current.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_compat_internal.hpp"
#include "stock_native_battle_subsystems.hpp"
#include <algorithm>

namespace native_battle {
namespace compat {
namespace {
PyObject* fail(const std::exception& e) {
    if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
}
// The call through the original with the hook's own positional arguments and keywords.
PyObject* passThrough(const Object& original, PyObject* args, PyObject* kwargs) { return PyObject_Call(original.get(), args, kwargs); }
// One attribute scope restored on every exit, as compat.py's try/finally scoped fields.
struct Scope {
    PyObject*& slot;
    PyObject* previous;
    Scope(PyObject*& s, PyObject* value) : slot(s), previous(s) { slot = value; }
    ~Scope() { slot = previous; }
};
bool fakeConnected() { return p::flag(py::attr(stock().compatibility.get(), "_fake_connected").get()); }
bool marker(PyObject* o, const char* name) { return flagAttribute(o, name, false); }
void mark(PyObject* o, const char* name, bool value) { py::set(o, name, value ? Py_True : Py_False); }
}

// ---- Avatar lifecycle ------------------------------------------------------------------------------------------

// compat.py _prepare_avatar_properties: battle server properties seeded before PlayerAvatar.__init__.
void prepareAvatarProperties(PyObject* avatar) {
    auto& s = stock();
    auto server = deferredServer();
    py::set(avatar, "fakeServer", server.get());
    Battle* b = current();
    const Compat fallback;
    const Compat& c = b && b->compat ? *b->compat : fallback;
    auto zero = py::call(s.math.get(), "Vector3", p::number(0.).get(), p::number(0.).get(), p::number(0.).get());
    auto camera = p::dict();
    p::set(camera.get(), "time", p::number(0.));
    PyDict_SetItemString(camera.get(), "shotPoint", zero.get());
    p::set(camera.get(), "zoom", p::integer(0));
    const std::pair<const char*, p::Ref> values[] = {
        {"name", p::Ref(PyString_FromStringAndSize(c.player_name.data(), Py_ssize_t(c.player_name.size())))},
        {"team", p::integer(c.player_team)},
        {"playerVehicleID", p::integer(0)},
        {"ownVehicleAuxPhysicsData", p::integer(0)},
        {"ownVehicleGear", p::integer(0)},
        {"denunciationsLeft", p::integer(10)},
        {"tkillIsSuspected", p::boolean(false)},
        {"clientCtx", p::Ref(PyString_FromString(""))},
        {"isObserverBothTeams", p::boolean(false)},
        {"isGunLocked", p::boolean(false)},
        {"arenaUniqueID", p::integer(0)},
        {"arenaTypeID", p::integer(c.arena_type_id)},
        {"arenaBonusType", p::integer(0)},
        {"arenaGuiType", p::integer(0)},
        {"arenaExtraData", p::dict()},
        {"weatherPresetID", p::integer(0)},
        {"playLimits", p::dict()},
        {"remoteCamera", p::retain(camera.get())},
        {"isObserverFPV", p::boolean(false)},
        {"observerFPVControlMode", p::integer(0)},
        {"numOfObservers", p::integer(0)},
    };
    for (const auto& value : values)
        if (!PyObject_HasAttrString(avatar, value.first)) py::set(avatar, value.first, value.second.get());
}

PyObject* avatarInit(PyObject* args, PyObject* kwargs) {
    try {
        auto avatar = argument(args, 0);
        const bool offline = fakeConnected();
        if (offline) prepareAvatarProperties(avatar.get());
        p::Ref result(passThrough(stock().avatar_init, args, kwargs));
        if (offline) {
            auto filter = py::call(stock().bigworld.get(), "AvatarFilter");
            py::set(avatar.get(), "filter", filter.get());
            py::call(filter.get(), "enableLagDetection", Py_True);
            mark(avatar.get(), init_complete, true);
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* avatarBecomePlayer(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        if (!fakeConnected()) return passThrough(s.avatar_become_player, args, kwargs);
        auto avatar = argument(args, 0);
        if (!marker(avatar.get(), init_complete)) throw p::Error("offline Avatar initialization did not complete");
        const bool native_battle = active();
        if (!native_battle) {
            // An Avatar outside a configured battle keeps compat.py's inert input handler.
            auto module = PyImport_ImportModule(p::string(py::attr(reinterpret_cast<PyObject*>(Py_TYPE(s.compatibility.get())), "__module__").get()).c_str());
            p::Ref owner(module);
            auto handler = py::call(owner.get(), "_OfflineInputHandler");
            py::set(avatar.get(), "inputHandler", handler.get());
        }
        if (!PyObject_HasAttrString(avatar.get(), "playLimits")) py::set(avatar.get(), "playLimits", p::dict().get());
        auto offline_filter = py::attr(avatar.get(), "filter");
        auto original_factory = py::attr(s.bigworld.get(), "AvatarFilter");
        auto active = py::optional(s.offline_map_creator.get(), "Active");
        const bool map_was_active = py::callable(active) ? p::flag(py::invoke(active.get()).get())
                                                         : flagAttribute(s.offline_map_creator.get(), "active", false);
        Battle* b = current();
        if (b && b->compat && b->compat->gui_type) py::set(avatar.get(), "arenaGuiType", b->compat->gui_type.get());
        if (b && b->compat && b->compat->bonus_type) py::set(avatar.get(), "arenaBonusType", b->compat->bonus_type.get());
        auto reuse = constant(offline_filter.get());
        py::set(s.bigworld.get(), "AvatarFilter", reuse.get());
        // A playable LAN battle needs the normal #1513 initialization branches, not the offline viewer's.
        if (native_battle) py::call(s.offline_map_creator.get(), "SetActive", Py_False);
        // Open the retirement token first so a partial promotion is still torn down exactly once.
        mark(avatar.get(), retire_pending, true);
        PyObject* result = passThrough(s.avatar_become_player, args, kwargs);
        PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
        if (!result) PyErr_Fetch(&type, &value, &trace);
        if (py::attr(s.bigworld.get(), "AvatarFilter").get() == reuse.get()) py::set(s.bigworld.get(), "AvatarFilter", original_factory.get());
        py::call(s.offline_map_creator.get(), "SetActive", map_was_active ? Py_True : Py_False);
        if (!result) { PyErr_Restore(type, value, trace); return nullptr; }
        p::Ref held(result);
        auto arena = py::optional(avatar.get(), "arena");
        // Exact onBecomePlayer can return normally without an arena type.
        if (arena.get() == Py_None || py::optional(arena.get(), "arenaType").get() == Py_None)
            throw p::Error("offline Avatar has no initialized arena type");
        mark(avatar.get(), player_ready, true);
        return held.release();
    } catch (const std::exception& e) { return fail(e); }
}

// compat.py retire_offline_player for the Avatar: native retirement exactly once.
PyObject* avatarBecomeNonPlayer(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        auto avatar = argument(args, 0);
        if (!marker(avatar.get(), init_complete)) {
            if (fakeConnected()) Py_RETURN_NONE;
            return passThrough(s.avatar_become_non_player, args, kwargs);
        }
        if (!marker(avatar.get(), retire_pending)) Py_RETURN_NONE;
        mark(avatar.get(), retire_pending, false);
        mark(avatar.get(), player_ready, false);
        auto releaseChat = [&] {
            if (s.chat_manager && py::optional(s.chat_manager.get(), "playerProxy").get() != Py_None)
                py::call(s.chat_manager.get(), "switchPlayerProxy", Py_None);
        };
        PyObject* result = passThrough(s.avatar_become_non_player, args, kwargs);
        if (!result) {
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            try { releaseChat(); } catch (const std::exception&) { PyErr_Clear(); }
            PyErr_Restore(type, value, trace);
            return nullptr;
        }
        p::Ref held(result);
        releaseChat();
        return held.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* avatarEnterWorld(PyObject* args, PyObject* kwargs) {
    try {
        // A failed constructor still receives world callbacks; stock would hide the first error.
        if (fakeConnected() && !marker(argument(args, 0).get(), init_complete)) Py_RETURN_NONE;
        return passThrough(stock().avatar_enter_world, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* avatarLeaveWorld(PyObject* args, PyObject* kwargs) {
    try {
        if (fakeConnected() && !marker(argument(args, 0).get(), init_complete)) Py_RETURN_NONE;
        return passThrough(stock().avatar_leave_world, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* avatarPrereqsLoaded(PyObject* args, PyObject* kwargs) {
    try {
        if (fakeConnected()) {
            auto avatar = argument(args, 0);
            auto current_player = player();
            // BigWorld resource callbacks cannot be cancelled; drop one that outlived its player.
            if (current_player.get() != avatar.get() || !marker(avatar.get(), init_complete) || !marker(avatar.get(), player_ready)) Py_RETURN_NONE;
        }
        return passThrough(stock().avatar_prereqs_loaded, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* avatarAuxPhysics(PyObject* args, PyObject* kwargs) {
    try {
        if (!active()) return passThrough(stock().avatar_aux_physics, args, kwargs);
        Scope scope(mutableScopes().syncing_aux_physics, PyTuple_GET_ITEM(args, 0));
        return passThrough(stock().avatar_aux_physics, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

// compat.py prime_initial_remote_enemy: mark one enemy hidden before stock starts its visual.
bool primeInitialRemoteEnemy(PyObject* avatar, PyObject* vehicle) {
    if (!active()) return false;
    std::int64_t vehicle_team = 0, avatar_team = 0;
    try {
        auto info = py::attr(vehicle, "publicInfo");
        vehicle_team = p::wide(p::Ref(PyNumber_Int(p::Ref(PyObject_GetItem(info.get(), p::text("team").get())).get())).get());
        avatar_team = p::wide(p::Ref(PyNumber_Int(call(stock().avatar_getattribute, {avatar, p::text("team").get()}).get())).get());
    } catch (const p::Error&) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_KeyError) || PyErr_ExceptionMatches(PyExc_TypeError) ||
            PyErr_ExceptionMatches(PyExc_ValueError)) { PyErr_Clear(); return false; }
        throw;
    }
    if (vehicle_team == avatar_team) return false;
    py::set(vehicle, "_spot_visible", Py_False);
    py::set(vehicle, "_offlineNativeDrawVisible", Py_False);
    py::set(vehicle, "_offlineNativeMarkerVisible", Py_False);
    py::set(vehicle, "targetCaps", p::list().get());
    return true;
}

// compat.py hide_initial_remote_enemy: close the first native draw/marker window for one enemy.
bool hideInitialRemoteEnemy(PyObject* avatar, PyObject* vehicle) {
    if (!primeInitialRemoteEnemy(avatar, vehicle)) return false;
    auto show = py::optional(vehicle, "show");
    auto appearance = py::optional(vehicle, "appearance");
    p::Ref change_visibility(p::retain(Py_None));
    if (appearance.get() != Py_None) replace(change_visibility, py::optional(appearance.get(), "changeVisibility"));
    p::Ref provider(p::retain(Py_None));
    PyObject* raw = PyObject_CallFunctionObjArgs(stock().avatar_getattribute.get(), avatar, p::text("guiSessionProvider").get(), nullptr);
    if (raw) replace(provider, p::Ref(raw));
    else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
    else throw p::Error("guiSessionProvider");
    auto stop_visual = py::optional(provider.get(), "stopVehicleVisual");
    if (!py::callable(show) || !py::callable(stop_visual) || (appearance.get() != Py_None && !py::callable(change_visibility)))
        throw p::Error("#1513 initial enemy visibility gate is unavailable");
    // Vehicle.show(False) keeps the shadow pass; close the whole compound and its decals/selectors too.
    py::invoke(show.get(), Py_False);
    if (change_visibility.get() != Py_None) py::invoke(change_visibility.get(), Py_False);
    if (appearance.get() != Py_None) remote::closeStockPresentationExtras(require(), appearance.get(), false);
    py::invoke(stop_visual.get(), p::Ref(PyNumber_Int(py::attr(vehicle, "id").get())).get(), Py_False);
    return true;
}

// compat.py avatar_vehicle_enter: the server bridge's enter barrier around stock vehicle_onEnterWorld.
PyObject* avatarVehicleEnter(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        auto avatar = argument(args, 0), vehicle = argument(args, 1);
        auto id = py::attr(vehicle.get(), "id");
        p::Ref server(p::retain(Py_None));
        auto failEnter = [&] {
            auto fail_enter = py::optional(server.get(), "failVehicleEnter");
            if (!py::callable(fail_enter)) return;
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            PyErr_NormalizeException(&type, &value, &trace);
            PyObject* error = value ? value : Py_None;
            PyObject* done = PyObject_CallFunctionObjArgs(fail_enter.get(), id.get(), error, nullptr);
            if (done) { Py_DECREF(done); PyErr_Restore(type, value, trace); }
            else { Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace); }
        };
        if (active()) {
            PyObject* raw = PyObject_CallFunctionObjArgs(s.avatar_getattribute.get(), avatar.get(), p::text("fakeServer").get(), nullptr);
            if (raw) replace(server, p::Ref(raw));
            else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
            else return nullptr;
            auto prepare = py::optional(server.get(), "prepareVehicleEnter");
            auto accept = py::optional(server.get(), "acceptVehicleEnter");
            if (py::callable(accept)) {
                try {
                    if (py::callable(prepare)) py::invoke(prepare.get(), vehicle.get());
                    py::invoke(accept.get(), id.get());
                } catch (const p::Error&) {
                    failEnter();
                    return nullptr;
                }
            }
        }
        p::Ref result(p::retain(Py_None));
        {
            Scope scope(mutableScopes().entering_vehicle, vehicle.get());
            try {
                primeInitialRemoteEnemy(avatar.get(), vehicle.get());
                if (s.avatar_vehicle_enter.get() != Py_None) replace(result, p::Ref(passThrough(s.avatar_vehicle_enter, args, kwargs)));
                hideInitialRemoteEnemy(avatar.get(), vehicle.get());
            } catch (const std::exception&) {
                if (!PyErr_Occurred()) throw;
                failEnter();
                return nullptr;
            }
        }
        auto complete = py::optional(server.get(), "completeVehicleEnter");
        if (py::callable(complete)) py::invoke(complete.get(), id.get());
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// ---- Avatar target lock and input -------------------------------------------------------------------------------

// compat.py avatar_auto_aim: admit the private remote Vehicle to the stock lock lifecycle.
PyObject* avatarAutoAim(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        Battle* b = battle();
        if (!b) return passThrough(s.avatar_auto_aim, args, kwargs);
        auto avatar = argument(args, 0);
        auto target = argument(args, 1);
        auto& scope = mutableScopes();
        auto current_id = call(s.avatar_getattribute, {avatar.get(), p::text("_PlayerAvatar__autoAimVehID").get()});
        PyObject* candidate = b->compat->target_lock_candidate.get();
        PyObject* caller = callerCode();
        const bool lock_input = scope.target_lock_input_pending && avatar.get() == scope.target_lock_input_avatar &&
                                caller && (caller == code(Code::ArcadeHandleKeyEvent) || caller == code(Code::SniperHandleKeyEvent));
        if (lock_input) {
            scope.target_lock_input_pending = false;
            scope.target_lock_input_avatar = nullptr;
            if (target.get() == Py_None && candidate) replace(target, p::retain(candidate));
        }
        // Map a native selection back to the gameplay adapter that owns it.
        if (candidate && target.get() != Py_None && target.get() == py::optional(candidate, "bw_entity").get()) replace(target, p::retain(candidate));
        if (!flagAttribute(target.get(), "_offlineLANPresentation", false))
            return PyObject_CallFunctionObjArgs(s.avatar_auto_aim.get(), avatar.get(), target.get(), nullptr);
        auto alive = py::attr(target.get(), "isAlive");
        const bool is_alive = PyCallable_Check(alive.get()) ? p::flag(py::invoke(alive.get()).get()) : p::flag(alive.get());
        const std::int64_t target_id = p::wide(p::Ref(PyNumber_Int(py::attr(target.get(), "id").get())).get());
        const std::int64_t current = p::wide(p::Ref(PyNumber_Int(current_id.get())).get());
        const bool rejected = target_id == current ||
                              p::wide(p::Ref(PyNumber_Int(py::attr(target.get(), "team").get())).get()) ==
                                  p::wide(p::Ref(PyNumber_Int(py::attr(avatar.get(), "team").get())).get()) ||
                              !is_alive;
        if (rejected) {
            // Stock owns the full unlock transition, including its convergence bookkeeping.
            if (current) return PyObject_CallFunctionObjArgs(s.avatar_auto_aim.get(), avatar.get(), Py_None, nullptr);
            Py_RETURN_NONE;
        }
        auto id = p::integer(target_id);
        py::set(avatar.get(), "_PlayerAvatar__autoAimVehID", id.get());
        py::call(py::attr(avatar.get(), "cell").get(), "autoAim", id.get());
        auto mode = py::attr(py::attr(s.constants.get(), "AIMING_MODE").get(), "TARGET_LOCK");
        py::call(py::attr(avatar.get(), "inputHandler").get(), "setAimingMode", Py_True, mode.get());
        py::set(py::attr(avatar.get(), "gunRotator").get(), "clientMode", Py_False);
        auto sound = py::attr(s.avatar_module.get(), "AimSound");
        py::call(avatar.get(), "onLockTarget", py::attr(sound.get(), "TARGET_LOCKED").get(), Py_True);
        auto triggers = py::attr(py::attr(s.avatar_module.get(), "TriggersManager").get(), "g_manager");
        auto trigger = py::attr(py::attr(s.avatar_module.get(), "TRIGGER_TYPE").get(), "AUTO_AIM_AT_VEHICLE");
        auto activate = py::attr(triggers.get(), "activateTrigger");
        p::Ref positional(PyTuple_Pack(1, trigger.get()));
        auto keywords = p::dict();
        p::set(keywords.get(), "vehicleId", p::integer(target_id));
        p::Ref(PyObject_Call(activate.get(), positional.get(), keywords.get()));
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// compat.py ammo_change_setting: one offline CURRENT transaction owns its native edge order.
PyObject* ammoChangeSetting(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        if (!active()) return passThrough(s.ammo_change_setting, args, kwargs);
        auto controller = argument(args, 0), int_cd = argument(args, 1);
        p::Ref avatar(p::retain(PyTuple_GET_SIZE(args) > 2 ? PyTuple_GET_ITEM(args, 2) : Py_None));
        if (kwargs) if (PyObject* named = PyDict_GetItemString(kwargs, "avatar")) replace(avatar, p::retain(named));
        auto getter = s.avatar_getter.get();
        if (!p::flag(py::call(getter, "isVehicleAlive", avatar.get()).get())) Py_RETURN_FALSE;
        auto setting_code = py::call(controller.get(), "getNextSettingCode", int_cd.get());
        auto settings = py::optional(s.constants.get(), "VEHICLE_SETTING");
        auto current_shells = py::optional(settings.get(), "CURRENT_SHELLS");
        if (current_shells.get() == Py_None || PyObject_RichCompareBool(setting_code.get(), current_shells.get(), Py_NE) == 1)
            return passThrough(s.ammo_change_setting, args, kwargs);
        if (!p::flag(py::call(getter, "isPlayerOnArena", avatar.get()).get())) return passThrough(s.ammo_change_setting, args, kwargs);
        // The server bridge runs the whole reload/shell transaction before this input call returns.
        py::call(getter, "changeVehicleSetting", setting_code.get(), int_cd.get(), avatar.get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

namespace {
PyObject* targetLockInput(const Object& original, PyObject* args, PyObject* kwargs) {
    auto& s = stock();
    if (!active()) return passThrough(original, args, kwargs);
    auto is_down = argument(args, 1), key = argument(args, 2);
    auto mapping = py::attr(py::attr(s.runtime.get(), "control_modes").get(), "CommandMapping");
    const bool lock = p::flag(is_down.get()) &&
                      p::flag(py::call(py::attr(mapping.get(), "g_instance").get(), "isFired", py::attr(mapping.get(), "CMD_CM_LOCK_TARGET").get(), key.get()).get());
    auto& scope = mutableScopes();
    if (lock && scope.target_lock_input_pending) throw p::Error("nested target-lock input is not allowed");
    if (lock) {
        scope.target_lock_input_pending = true;
        scope.target_lock_input_avatar = player().get();
    }
    PyObject* result = passThrough(original, args, kwargs);
    if (lock) {
        scope.target_lock_input_pending = false;
        scope.target_lock_input_avatar = nullptr;
    }
    return result;
}
}
PyObject* arcadeKeyEvent(PyObject* args, PyObject* kwargs) {
    try { return targetLockInput(stock().arcade_key_event, args, kwargs); } catch (const std::exception& e) { return fail(e); }
}
PyObject* sniperKeyEvent(PyObject* args, PyObject* kwargs) {
    try { return targetLockInput(stock().sniper_key_event, args, kwargs); } catch (const std::exception& e) { return fail(e); }
}

// compat.py strategic_camera_update: one failed SPG camera tick must not stop the camera loop.
PyObject* strategicCameraUpdate(PyObject* args, PyObject* kwargs) {
    try {
        Battle* b = battle();
        PyObject* result = passThrough(stock().strategic_camera_update, args, kwargs);
        if (result || !b || !PyErr_ExceptionMatches(PyExc_Exception)) return result;
        if (!b->compat->strategic_camera_failure_reported) {
            b->compat->strategic_camera_failure_reported = true;
            log("strategic camera tick failed; the camera loop continues");
            PyErr_Print();
        } else PyErr_Clear();
        return PyFloat_FromDouble(0.);
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* controlModeChanged(PyObject* args, PyObject* kwargs) {
    try {
        p::Ref result(passThrough(stock().control_mode_changed, args, kwargs));
        Battle* b = current();
        if (b && b->compat && b->compat->control_mode_listener) {
            auto handler = argument(args, 0), mode = argument(args, 1);
            auto current_mode = py::optional(handler.get(), "_AvatarInputHandler__ctrlModeName");
            if (PyObject_RichCompareBool(current_mode.get(), mode.get(), Py_EQ) == 1)
                call(b->compat->control_mode_listener, {handler.get(), mode.get()});
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// ---- gun marker selection ----------------------------------------------------------------------------------------

// VehicleGunRotator.showServerMarker setter: the persisted preference stays, the battle shows the client marker only.
PyObject* setServerMarker(PyObject* args, PyObject*) {
    try {
        auto rotator = argument(args, 0);
        auto enabled = argument(args, 1);
        if (active()) replace(enabled, p::boolean(false));
        auto setter = py::attr(stock().server_marker_property.get(), "fset");
        return PyObject_CallFunctionObjArgs(setter.get(), rotator.get(), enabled.get(), nullptr);
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* useClientGunMarker(PyObject*, PyObject*) {
    try {
        if (active()) Py_RETURN_TRUE;
        return call(stock().use_client_marker, {}).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* useServerGunMarker(PyObject*, PyObject*) {
    try {
        if (active()) Py_RETURN_FALSE;
        return call(stock().use_server_marker, {}).release();
    } catch (const std::exception& e) { return fail(e); }
}

// ---- vehicle markers -------------------------------------------------------------------------------------------
namespace {
// compat.py rollback_vehicle_marker_start: detach a plugin whose start failed, keeping the first error.
void rollbackMarkerStart(PyObject* plugin, const std::string& primary) {
    std::vector<std::string> failures;
    auto provider = py::optional(plugin, "sessionProvider");
    auto remove = py::optional(provider.get(), "removeArenaCtrl");
    if (!py::callable(remove)) failures.push_back("removeArenaCtrl is unavailable");
    else {
        PyObject* done = PyObject_CallFunctionObjArgs(remove.get(), plugin, nullptr);
        if (done) Py_DECREF(done);
        else failures.push_back("removeArenaCtrl failed: " + describe(p::Error("removeArenaCtrl")));
    }
    PyObject* stopped = PyObject_CallFunctionObjArgs(stock().marker_stop.get(), plugin, nullptr);
    if (stopped) Py_DECREF(stopped);
    else failures.push_back("native stop failed: " + describe(p::Error("stop")));
    if (Battle* b = current()) if (b->compat) b->compat->marker_plugins.erase(plugin);
    if (!failures.empty()) {
        std::string joined;
        for (const auto& f : failures) joined += (joined.empty() ? "" : "; ") + f;
        PyErr_Clear();
        throw p::Error(primary + "; vehicle-marker start rollback failed: " + joined);
    }
}
}

// compat.py vehicle_marker_start: the stock plugin must start on the validated player identity.
PyObject* markerStart(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        auto plugin = argument(args, 0);
        Battle* b = current();
        const std::int64_t expected = b && b->compat ? b->compat->player_vehicle_id : 0;
        if (expected) {
            auto provider = py::optional(plugin.get(), "sessionProvider");
            auto get_arena_dp = py::optional(provider.get(), "getArenaDP");
            if (!py::callable(get_arena_dp)) throw p::Error("#1513 vehicle-marker ArenaDP provider is unavailable");
            auto arena_dp = py::invoke(get_arena_dp.get());
            auto required = py::optional(arena_dp.get(), "isRequiredDataExists");
            auto player_vehicle = py::optional(arena_dp.get(), "getPlayerVehicleID");
            if (!py::callable(required) || !py::callable(player_vehicle)) throw p::Error("#1513 vehicle-marker player identity API is unavailable");
            if (!p::flag(py::invoke(required.get()).get())) throw p::Error("#1513 vehicle-marker ArenaDP identity is incomplete before start");
            const std::int64_t arena_vehicle = p::wide(p::Ref(PyNumber_Int(py::invoke(player_vehicle.get(), Py_False).get())).get());
            if (arena_vehicle != expected)
                throw p::Error("#1513 vehicle-marker ArenaDP identity mismatch before start: expected=" + std::to_string(expected) +
                               " arenaDP=" + std::to_string(arena_vehicle));
        }
        PyObject* result = passThrough(s.marker_start, args, kwargs);
        if (!result) {
            // Roll back, then re-raise the stock error unless the rollback itself failed.
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            PyErr_NormalizeException(&type, &value, &trace);
            const std::string primary = value ? p::string(p::Ref(PyObject_Str(value)).get()) : std::string("vehicle-marker start failed");
            try { rollbackMarkerStart(plugin.get(), primary); }
            catch (...) { Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace); throw; }
            PyErr_Restore(type, value, trace);
            return nullptr;
        }
        p::Ref held(result);
        PyObject* cached = PyObject_GetAttrString(plugin.get(), "_VehicleMarkerPlugin__playerVehicleID");
        if (!cached) {
            PyErr_Clear();
            rollbackMarkerStart(plugin.get(), "#1513 vehicle-marker player identity cache is missing");
            throw p::Error("#1513 vehicle-marker player identity cache is missing");
        }
        p::Ref cached_id(cached);
        if (expected && PyObject_RichCompareBool(cached, p::integer(expected).get(), Py_NE) == 1) {
            const std::string error = "#1513 vehicle-marker plugin captured a stale player identity: expected=" + std::to_string(expected) +
                                      " cached=" + p::string(p::Ref(PyObject_Str(cached)).get());
            rollbackMarkerStart(plugin.get(), error);
            throw p::Error(error);
        }
        if (b && b->compat) b->compat->marker_plugins[plugin.get()] = borrow(plugin.get());
        return held.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* markerStop(PyObject* args, PyObject* kwargs) {
    try {
        auto plugin = argument(args, 0);
        PyObject* result = passThrough(stock().marker_stop, args, kwargs);
        if (Battle* b = current()) if (b->compat) b->compat->marker_plugins.erase(plugin.get());
        return result;
    } catch (const std::exception& e) { return fail(e); }
}

// ---- pose sources -------------------------------------------------------------------------------------------------

// ConsistentMatrices.__linkOwnVehicle: the player's live overlay owns the own-vehicle provider.
PyObject* linkOwnVehicle(PyObject* args, PyObject* kwargs) {
    try {
        Battle* b = battle();
        auto overlay = b ? remote::poseOverlay(*b, argument(args, 1).get()) : std::nullopt;
        if (!overlay) return passThrough(stock().link_own_vehicle, args, kwargs);
        auto provider = py::optional(argument(args, 0).get(), "_ConsistentMatrices__ownVehicleMProv");
        if (provider.get() == Py_None) throw p::Error("#1513 own-vehicle matrix provider is unavailable");
        py::set(provider.get(), "target", overlay->matrix.get());
        if (py::attr(provider.get(), "target").get() != overlay->matrix.get()) throw p::Error("#1513 own-vehicle matrix rejected live pose");
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// SteadyVehicleMatrixCalculator.relinkSources: steady and stabilised providers follow the live overlay.
PyObject* relinkSources(PyObject* args, PyObject* kwargs) {
    try {
        Battle* b = battle();
        if (b) {
            auto current_player = player();
            auto vehicle = current_player.get() != Py_None ? py::call(current_player.get(), "getVehicleAttached") : p::retain(Py_None);
            auto overlay = vehicle.get() != Py_None ? remote::poseOverlay(*b, vehicle.get()) : std::nullopt;
            if (overlay) {
                PyObject* matrix = overlay->matrix.get();
                PyObject* steady = overlay->steady_rotation ? overlay->steady_rotation.get() : matrix;
                PyObject* stabilised_matrix = overlay->stabilised ? overlay->stabilised.get() : matrix;
                auto calculator = argument(args, 0);
                auto output = py::optional(calculator.get(), "_SteadyVehicleMatrixCalculator__outputMProv");
                auto stabilised = py::optional(calculator.get(), "_SteadyVehicleMatrixCalculator__stabilisedMProv");
                if (output.get() == Py_None || stabilised.get() == Py_None) throw p::Error("#1513 steady vehicle providers are unavailable");
                py::set(output.get(), "rotationSrc", steady);
                py::set(output.get(), "translationSrc", stabilised_matrix);
                py::set(stabilised.get(), "target", stabilised_matrix);
                if (py::attr(output.get(), "rotationSrc").get() != steady || py::attr(output.get(), "translationSrc").get() != stabilised_matrix ||
                    py::attr(stabilised.get(), "target").get() != stabilised_matrix)
                    throw p::Error("#1513 steady vehicle providers rejected live pose");
                Py_RETURN_NONE;
            }
        }
        return passThrough(stock().relink_sources, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

// ---- Vehicle lifecycle --------------------------------------------------------------------------------------------

// Vehicle.onEnterWorld: stock initialises every controller; the enemy draw gate closes before and after.
PyObject* vehicleEnterWorld(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        if (!active()) return passThrough(s.vehicle_enter_world, args, kwargs);
        auto vehicle = argument(args, 0);
        auto avatar = player();
        if (avatar.get() != Py_None) primeInitialRemoteEnemy(avatar.get(), vehicle.get());
        p::Ref result(passThrough(s.vehicle_enter_world, args, kwargs));
        // Only the runtime's occluded current-pose ray may publish target focus.
        py::set(vehicle.get(), "targetCaps", p::list().get());
        replace(avatar, player());
        if (avatar.get() != Py_None) hideInitialRemoteEnemy(avatar.get(), vehicle.get());
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// Vehicle.startVisual: every stock controller runs; the scoped attribute gates keep an enemy unpublished.
PyObject* vehicleStartVisual(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        if (!active()) return passThrough(s.vehicle_start_visual, args, kwargs);
        auto vehicle = argument(args, 0);
        auto avatar = player();
        if (avatar.get() != Py_None) primeInitialRemoteEnemy(avatar.get(), vehicle.get());
        p::Ref result(p::retain(Py_None));
        {
            Scope scope(mutableScopes().starting_visual, vehicle.get());
            replace(result, p::Ref(passThrough(s.vehicle_start_visual, args, kwargs)));
        }
        py::set(vehicle.get(), "targetCaps", p::list().get());
        if (avatar.get() != Py_None) hideInitialRemoteEnemy(avatar.get(), vehicle.get());
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// Vehicle.onLeaveWorld after the player is gone: only the two Vehicle-owned stages remain.
PyObject* vehicleLeaveWorld(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        if (!active()) return passThrough(s.vehicle_leave_world, args, kwargs);
        auto current_player = player();
        if (py::callable(py::optional(current_player.get(), "vehicle_onLeaveWorld"))) return passThrough(s.vehicle_leave_world, args, kwargs);
        auto vehicle = argument(args, 0);
        auto stop_extras = py::optional(vehicle.get(), "_Vehicle__stopExtras");
        if (py::callable(stop_extras)) py::invoke(stop_extras.get());
        if (flagAttribute(vehicle.get(), "isStarted", false)) {
            auto stop_visual = py::optional(vehicle.get(), "stopVisual");
            if (!py::callable(stop_visual)) throw p::Error("retired offline Vehicle cannot stop its visual");
            py::invoke(stop_visual.get(), Py_False);
        }
        if (flagAttribute(vehicle.get(), "isStarted", false)) throw p::Error("retired offline Vehicle remained visually started");
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* vehicleStartWgPhysics(PyObject* args, PyObject* kwargs) {
    try {
        if (!active()) return passThrough(stock().vehicle_start_wg_physics, args, kwargs);
        Scope scope(mutableScopes().starting_wg_physics, PyTuple_GET_ITEM(args, 0));
        return passThrough(stock().vehicle_start_wg_physics, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* vehicleSetGunAngles(PyObject* args, PyObject* kwargs) {
    try {
        if (!active()) return passThrough(stock().vehicle_set_gun_angles, args, kwargs);
        Scope scope(mutableScopes().syncing_gun_angles, PyTuple_GET_ITEM(args, 0));
        return passThrough(stock().vehicle_set_gun_angles, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

// ---- collision against the drawn pose -----------------------------------------------------------------------------
namespace {
// compat.py visible_native_remote_collisions: None defers to stock collision.
p::Ref visibleCollisions(PyObject* vehicle, PyObject* start, PyObject* end) {
    Battle* b = battle();
    if (!b) return p::retain(Py_None);
    auto native = visibility::collisions(*b, vehicle, start, end);
    if (native.get() != Py_None) return native;
    return remote::visibleCollisions(*b, vehicle, start, end);
}
double collisionDistance(PyObject* item) { return p::scalar(py::attr(item, "dist").get()); }
}

PyObject* vehicleCollideSegment(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        auto vehicle = argument(args, 0), start = argument(args, 1), end = argument(args, 2);
        PyObject* skip_gun = PyTuple_GET_SIZE(args) > 3 ? PyTuple_GET_ITEM(args, 3) : Py_False;
        PyObject* optimized = PyTuple_GET_SIZE(args) > 4 ? PyTuple_GET_ITEM(args, 4) : Py_True;
        if (kwargs) {
            if (PyObject* v = PyDict_GetItemString(kwargs, "skipGun")) skip_gun = v;
            if (PyObject* v = PyDict_GetItemString(kwargs, "optimized")) optimized = v;
        }
        auto collisions = visibleCollisions(vehicle.get(), start.get(), end.get());
        if (collisions.get() == Py_None)
            return call(s.vehicle_collide_segment, {vehicle.get(), start.get(), end.get(), skip_gun, optimized}).release();
        p::Ref rows(PySequence_List(collisions.get()));
        PyObject* closest = nullptr;
        double best = 0.;
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            PyObject* item = PyList_GET_ITEM(rows.get(), i);
            if (p::flag(skip_gun) && p::string(py::attr(item, "compName").get()) == "vehicleGun") continue;
            const double distance = collisionDistance(item);
            if (!closest || distance < best) { closest = item; best = distance; }
        }
        if (!closest) Py_RETURN_NONE;
        auto material = py::attr(closest, "matInfo");
        p::Ref armor(p::integer(0));
        if (material.get() != Py_None) {
            PyObject* value = PyObject_GetAttrString(material.get(), "armor");
            if (value) replace(armor, p::Ref(value));
            else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
            else return nullptr;
        }
        return call(s.segment_collision_result, {py::attr(closest, "dist").get(), py::attr(closest, "hitAngleCos").get(), armor.get()}).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* vehicleCollideSegmentExt(PyObject* args, PyObject* kwargs) {
    try {
        auto vehicle = argument(args, 0), start = argument(args, 1), end = argument(args, 2);
        auto collisions = visibleCollisions(vehicle.get(), start.get(), end.get());
        if (collisions.get() == Py_None) return passThrough(stock().vehicle_collide_segment_ext, args, kwargs);
        if (!p::flag(collisions.get())) Py_RETURN_NONE;
        p::Ref rows(PySequence_List(collisions.get()));
        std::vector<std::pair<double, PyObject*>> keyed;
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) keyed.emplace_back(collisionDistance(PyList_GET_ITEM(rows.get(), i)), PyList_GET_ITEM(rows.get(), i));
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        p::Ref sorted(PyList_New(Py_ssize_t(keyed.size())));
        for (std::size_t i = 0; i < keyed.size(); ++i) { Py_INCREF(keyed[i].second); PyList_SET_ITEM(sorted.get(), Py_ssize_t(i), keyed[i].second); }
        return sorted.release();
    } catch (const std::exception& e) { return fail(e); }
}

// ProjectileMover.segmentMayHitEntity: the draw gate applies to stale imported stock queries too.
PyObject* projectileSegmentMayHit(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        Battle* b = battle();
        if (b) {
            auto entity = argument(args, 0), start = argument(args, 1), end = argument(args, 2);
            auto native = visibility::segmentMayHit(*b, entity.get(), start.get(), end.get());
            if (native.get() != Py_None) return native.release();
            if (undrawnLanRemote(entity.get())) Py_RETURN_FALSE;
        }
        return passThrough(s.segment_may_hit, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

// ProjectileMover.getCollidableEntities: an undrawn LAN remote is not in the stock dynamic collision set.
PyObject* projectileCollidableEntities(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        Battle* b = battle();
        if (b) {
            auto native = visibility::collidableEntities(*b, argument(args, 0).get(), argument(args, 1).get(), argument(args, 2).get());
            if (native.get() != Py_None) return native.release();
        }
        p::Ref entities(passThrough(s.collidable_entities, args, kwargs));
        if (!b || !p::flag(entities.get())) return entities.release();
        p::Ref rows(PySequence_List(entities.get()));
        p::Ref drawn(PyList_New(0));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i)
            if (!undrawnLanRemote(PyList_GET_ITEM(rows.get(), i))) p::append(drawn.get(), p::retain(PyList_GET_ITEM(rows.get(), i)));
        if (PyList_GET_SIZE(drawn.get()) == PyList_GET_SIZE(rows.get())) return entities.release();
        return drawn.release();
    } catch (const std::exception& e) { return fail(e); }
}

// ---- CompoundAppearance -------------------------------------------------------------------------------------------

// CompoundAppearance.deactivate: late #1513 teardown gets a no-op camera collider owner.
PyObject* compoundDeactivate(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        auto appearance = argument(args, 0);
        auto callOriginal = [&]() -> PyObject* {
            PyObject* result = passThrough(s.compound_deactivate, args, kwargs);
            // A nested model refresh keeps its generation token; final teardown clears the decal state.
            if (mutableScopes().refreshing_models != appearance.get()) {
                PyObject *type, *value, *trace;
                PyErr_Fetch(&type, &value, &trace);
                try { remote::clearGroundDecalVisibilityState(appearance.get()); }
                catch (const std::exception&) {
                    if (!type) { Py_XDECREF(result); return nullptr; }
                    PyErr_Clear();
                }
                if (type) PyErr_Restore(type, value, trace);
            }
            return result;
        };
        if (!active()) return callOriginal();
        auto current_player = player();
        p::Ref handler(p::retain(Py_None));
        if (current_player.get() != Py_None) {
            PyObject* raw = PyObject_GetAttrString(current_player.get(), "inputHandler");
            if (raw) replace(handler, p::Ref(raw));
            else if (PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_ReferenceError)) PyErr_Clear();
            else return nullptr;
        }
        if (handler.get() != Py_None) return callOriginal();
        // PlayerAvatar.__destroyGUI cleared inputHandler; deactivate still removes the camera collider.
        auto fallback = surrogate(SurrogateCollider);
        if (current_player.get() != Py_None) {
            if (PyObject_SetAttrString(current_player.get(), "inputHandler", fallback.get()) < 0) { PyErr_Clear(); return callOriginal(); }
            PyObject* result = callOriginal();
            PyObject *type, *value, *trace;
            PyErr_Fetch(&type, &value, &trace);
            PyObject* now = PyObject_GetAttrString(current_player.get(), "inputHandler");
            if (now == fallback.get()) PyObject_SetAttrString(current_player.get(), "inputHandler", Py_None);
            Py_XDECREF(now);
            PyErr_Clear();
            if (type) PyErr_Restore(type, value, trace);
            return result;
        }
        PyObject* module_dict = PyModule_Check(s.bigworld.get()) ? PyModule_GetDict(s.bigworld.get()) : nullptr;
        PyObject* raw_player = module_dict ? PyDict_GetItemString(module_dict, "player") : nullptr;
        p::Ref saved(p::retain(raw_player ? raw_player : Py_None));
        const bool had_player = raw_player != nullptr;
        auto owner = constant(surrogate(SurrogateOwner).get());
        py::set(s.bigworld.get(), "player", owner.get());
        PyObject* result = callOriginal();
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        PyObject* now = PyObject_GetAttrString(s.bigworld.get(), "player");
        if (now == owner.get()) {
            if (had_player) PyObject_SetAttrString(s.bigworld.get(), "player", saved.get());
            else PyObject_DelAttrString(s.bigworld.get(), "player");
        }
        Py_XDECREF(now);
        PyErr_Clear();
        if (type) PyErr_Restore(type, value, trace);
        return result;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* compoundModelsRefresh(PyObject* args, PyObject* kwargs) {
    try {
        if (!active()) return passThrough(stock().compound_models_refresh, args, kwargs);
        Scope scope(mutableScopes().refreshing_models, PyTuple_GET_ITEM(args, 0));
        return passThrough(stock().compound_models_refresh, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

// CompoundAppearance.__updateEffectsLOD: an undrawn LAN remote keeps its dust, exhaust and decals closed.
PyObject* compoundEffectsLod(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        if (!active()) return passThrough(s.compound_effects_lod, args, kwargs);
        auto appearance = argument(args, 0);
        PyObject* raw = PyObject_CallFunctionObjArgs(s.compound_getattribute.get(), appearance.get(), p::text("_CompoundAppearance__vehicle").get(), nullptr);
        p::Ref vehicle(p::retain(Py_None));
        if (raw) replace(vehicle, p::Ref(raw));
        else if (PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_ReferenceError)) PyErr_Clear();
        else return nullptr;
        if (vehicle.get() != Py_None && undrawnLanRemote(vehicle.get())) {
            remote::stopGroundEffects(appearance.get());
            remote::setGroundDecalVisibility(appearance.get(), false);
            remote::stopBoundVisualEffects(appearance.get());
            Py_RETURN_NONE;
        }
        return passThrough(s.compound_effects_lod, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}

// ---- BigWorld.serverTime ------------------------------------------------------------------------------------------

// The fake connection's serverTime stays frozen; a battle advances it with BigWorld.time from its origin.
PyObject* serverTime(PyObject* args, PyObject* kwargs) {
    try {
        auto& s = stock();
        Battle* b = battle();
        if (b && b->compat->server_time_origin && b->compat->clock_origin) {
            PyObject* clock = PyObject_CallMethod(s.bigworld.get(), const_cast<char*>("time"), nullptr);
            if (clock) {
                const double now = PyFloat_AsDouble(clock);
                Py_DECREF(clock);
                if (!PyErr_Occurred()) return PyFloat_FromDouble(*b->compat->server_time_origin + std::max(0., now - *b->compat->clock_origin));
            }
            if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
            PyErr_Clear();
        }
        return passThrough(s.server_time, args, kwargs);
    } catch (const std::exception& e) { return fail(e); }
}
}
}
