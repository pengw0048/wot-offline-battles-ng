// Native ports of the pinned BattleRuntime entity, gun, Expert, invisibility and per-frame presentation helpers.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace helper_steps {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref flt(double v) { return p::Ref(PyFloat_FromDouble(v)); }
bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("helper comparison failed");
    return result != 0;
}
p::Ref getattrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
p::Ref mget(PyObject* mapping, const char* name, PyObject* fallback = nullptr) {
    return fallback ? method(mapping, "get", p::text(name).get(), fallback) : method(mapping, "get", p::text(name).get());
}
p::Ref item(PyObject* mapping, const char* name) { return p::Ref(PyObject_GetItem(mapping, p::text(name).get())); }
void store(PyObject* mapping, const char* name, PyObject* value) {
    if (PyObject_SetItem(mapping, p::text(name).get(), value) < 0) throw p::Error(name);
}
// Module helper _field(value, name, default).
p::Ref field(PyObject* value, const char* name, PyObject* fallback) {
    return PyDict_Check(value) ? method(value, "get", p::text(name).get(), fallback) : getattrOr(value, name, fallback);
}
// `import name` for a top-level native module: sys.modules first, as the import statement does.
p::Ref imported(const char* name) {
    PyObject* loaded = PyDict_GetItemString(PyImport_GetModuleDict(), name);
    return loaded ? p::retain(loaded) : p::Ref(PyImport_ImportModule(name));
}
bool running(PyObject* battle) { return test(attr(battle, "state").get(), p::text("running").get(), Py_EQ); }
p::Ref call(PyObject* fn, std::initializer_list<PyObject*> args) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    return p::Ref(PyObject_Call(fn, tuple.get(), nullptr));
}

PyObject* serverEntityPort(PyObject*, PyObject* args) {
    PyObject *battle, *entity_id;
    if (!PyArg_ParseTuple(args, "OO", &battle, &entity_id)) return nullptr;
    try { return serverEntity(battle, entity_id).release(); } catch (const std::exception& e) { return fail(e); }
}

PyObject* recordAlivePort(PyObject*, PyObject* args) {
    PyObject *record, *entity;
    if (!PyArg_ParseTuple(args, "OO", &record, &entity)) return nullptr;
    // A staticmethod route has no instance to override it.
    try {
        auto raw = method(record, "get", p::text("state").get());
        auto state = truthy(raw) ? std::move(raw) : p::dict();
        const int has = PySequence_Contains(state.get(), p::text("alive").get());
        if (has < 0) throw p::Error("alive");
        if (has) {
            if (!truthy(method(state.get(), "get", p::text("alive").get()))) Py_RETURN_FALSE;
            auto health = method(state.get(), "get", p::text("health").get(), integer(1).get());
            return p::boolean(greaterThanZero(toInt(truthy(health) ? health.get() : integer(0).get()).get())).release();
        }
        auto alive = getattrOr(entity, "isAlive", Py_None);
        if (PyCallable_Check(alive.get())) replace(alive, p::Ref(PyObject_CallFunctionObjArgs(alive.get(), nullptr)));
        return p::boolean(truthy(alive)).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._server_clock(): BigWorld.serverTime(), else the battle clock.
PyObject* serverClock(PyObject*, PyObject* args) {
    PyObject* battle;
    if (!PyArg_ParseTuple(args, "O", &battle)) return nullptr;
    try {
        auto function = getattrOr(attr(attr(battle, "_runtime").get(), "bigworld").get(), "serverTime", Py_None);
        if (PyCallable_Check(function.get())) {
            PyObject* value = PyObject_CallFunctionObjArgs(function.get(), nullptr);
            PyObject* number = value ? PyNumber_Float(value) : nullptr;
            Py_XDECREF(value);
            if (number) return number;
            if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
            PyErr_Clear();
        }
        return method(battle, "_clock").release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._critical_state(payload) (staticmethod)
PyObject* criticalState(PyObject*, PyObject* args) {
    PyObject* payload;
    if (!PyArg_ParseTuple(args, "O", &payload)) return nullptr;
    try {
        if (!PyDict_Check(payload)) Py_RETURN_NONE;
        p::Ref result(PyDict_Copy(payload));
        store(result.get(), "events", p::Ref(PyList_New(0)).get());
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._combat_target_is_spotted(record) (staticmethod)
PyObject* combatTargetIsSpotted(PyObject*, PyObject* args) {
    PyObject* record;
    if (!PyArg_ParseTuple(args, "O", &record)) return nullptr;
    try {
        auto visible = mget(record, "spot_visible", Py_True);
        return p::boolean(truthy(mget(record, "spot_marker_visible", visible.get()))).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._store_remote_visual_components(record, world_marker, minimap) (staticmethod)
PyObject* storeRemoteVisualComponents(PyObject*, PyObject* args) {
    PyObject *record, *world_marker, *minimap;
    if (!PyArg_ParseTuple(args, "OOO", &record, &world_marker, &minimap)) return nullptr;
    try {
        const bool marker = truthy(p::retain(world_marker));
        store(record, "world_marker_started", p::boolean(marker).get());
        const bool map = truthy(p::retain(minimap));
        store(record, "minimap_started", p::boolean(map).get());
        store(record, "visual_started", p::boolean(marker || map).get());
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._record_position(record)
PyObject* recordPosition(PyObject*, PyObject* args) {
    PyObject *battle, *record, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &record, &g)) return nullptr;
    try {
        if (truthy(mget(record, "local"))) return PySequence_Tuple(attr(battle, "_local_position").get());
        auto raw = mget(record, "state");
        auto state = truthy(raw) ? std::move(raw) : p::dict();
        bool complete = true;
        for (const char* name : {"x", "y", "z"}) {
            const int has = PySequence_Contains(state.get(), p::text(name).get());
            if (has < 0) throw p::Error("position membership");
            if (!has) { complete = false; break; }
        }
        if (complete) {
            const double x = number(item(state.get(), "x").get()), y = number(item(state.get(), "y").get()), z = number(item(state.get(), "z").get());
            return Py_BuildValue("(ddd)", x, y, z);
        }
        auto entity = serverEntity(battle, item(record, "engine_id").get());
        if (entity.get() == Py_None) raise("combat presentation entity is unavailable");
        return PyObject_CallFunctionObjArgs(global(g, "_xyz").get(), attr(entity.get(), "position").get(), nullptr);
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._bot_authority_is_local(player_id)
PyObject* botAuthorityIsLocal(PyObject*, PyObject* args) {
    PyObject *battle, *player_id;
    if (!PyArg_ParseTuple(args, "OO", &battle, &player_id)) return nullptr;
    try {
        auto local_id = getattrOr(attr(battle, "client").get(), "player_id", Py_None);
        if (player_id == Py_None || local_id.get() == Py_None) Py_RETURN_FALSE;
        PyObject* left = PyNumber_Int(player_id);
        PyObject* right = left ? PyNumber_Int(local_id.get()) : nullptr;
        if (!right) {
            Py_XDECREF(left);
            if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError) || PyErr_ExceptionMatches(PyExc_OverflowError)) {
                PyErr_Clear();
                raise("bot authority identity is invalid");
            }
            return nullptr;
        }
        p::Ref a(left), b(right);
        return PyObject_RichCompare(a.get(), b.get(), Py_EQ);
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._shot_invisibility_factor(descriptor) (staticmethod)
PyObject* shotInvisibilityFactor(PyObject*, PyObject* args) {
    PyObject *descriptor, *g;
    if (!PyArg_ParseTuple(args, "OO", &descriptor, &g)) return nullptr;
    try {
        auto gun = field(descriptor, "gun", p::dict().get());
        auto factor = field(gun.get(), "invisibilityFactorAtShot", flt(1.0).get());
        return method(global(g, "spotting").get(), "clamp", factor.get(), flt(0.0).get(), flt(1.0).get()).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._base_invisibility(descriptor, profile, camouflage_id=None) (staticmethod): #1513 (moving, still).
PyObject* baseInvisibility(PyObject*, PyObject* args) {
    PyObject *descriptor, *profile, *camouflage_id, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &descriptor, &profile, &camouflage_id, &g)) return nullptr;
    try {
        auto crew_factor = item(profile, "camouflage_factor");
        auto calculator = getattrOr(descriptor, "computeBaseInvisibility", Py_None);
        if (PyCallable_Check(calculator.get())) {
            try {
                auto values = call(calculator.get(), {crew_factor.get(), camouflage_id});
                if ((PyList_Check(values.get()) || PyTuple_Check(values.get())) && PySequence_Size(values.get()) >= 2) {
                    const double moving = number(p::Ref(PySequence_GetItem(values.get(), 0)).get());
                    const double still = number(p::Ref(PySequence_GetItem(values.get(), 1)).get());
                    return Py_BuildValue("(dd)", moving, still);
                }
            } catch (const std::exception&) {
                if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                PyErr_Clear();
            }
        }
        auto vehicle_type = field(descriptor, "type", p::dict().get());
        auto values = field(vehicle_type.get(), "invisibility", p::Ref(Py_BuildValue("(dd)", 0.0, 0.0)).get());
        if ((!PyList_Check(values.get()) && !PyTuple_Check(values.get())) || PySequence_Size(values.get()) < 2)
            replace(values, p::Ref(Py_BuildValue("(dd)", 0.0, 0.0)));
        auto misc = field(descriptor, "miscAttrs", p::dict().get());
        auto moving = p::Ref(PySequence_GetItem(values.get(), 0));
        auto still = p::Ref(PySequence_GetItem(values.get(), 1));
        auto factor = field(misc.get(), "invisibilityFactor", flt(1.0).get());
        return callKw(global(g, "spotting").get(), "base_camouflage", {moving.get(), still.get()},
                      {{"crew_factor", crew_factor.get()}, {"invisibility_factor", factor.get()}}).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._local_factors(descriptor): the player's own #1513 attribute factors for this round.
PyObject* localFactors(PyObject*, PyObject* args) {
    PyObject *battle, *descriptor, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &descriptor, &g)) return nullptr;
    try {
        if (attr(battle, "_local_factors_cache").get() == Py_None) {
            auto law = global(g, "loadout_law");
            auto snapshot = method(battle, "_garage_loadout_snapshot");
            p::Ref equipments_tuple(PySequence_Tuple(item(snapshot.get(), "equipments").get()));
            p::Ref boosters(PySequence_Tuple(mget(snapshot.get(), "battle_boosters", p::Ref(PyTuple_New(0)).get()).get()));
            p::Ref factor_equipments(PySequence_Concat(equipments_tuple.get(), boosters.get()));
            // The trigger-only Removed RPM Limiter would otherwise grant a permanent power buff.
            p::Ref kept(PyList_New(0));
            auto marker = p::text("removedrpmlimiter");
            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(factor_equipments.get()); ++i) {
                PyObject* equipment = PyTuple_GET_ITEM(factor_equipments.get(), i);
                p::Ref single(PyTuple_Pack(1, equipment));
                p::Ref names(PyObject_GetIter(method(law.get(), "equipment_names", single.get()).get()));
                bool limiter = false;
                while (!limiter) {
                    PyObject* name = PyIter_Next(names.get());
                    if (!name) { if (PyErr_Occurred()) throw p::Error("equipment names"); break; }
                    p::Ref held(name);
                    const int found = PySequence_Contains(name, marker.get());
                    if (found < 0) throw p::Error("equipment name");
                    limiter = found != 0;
                }
                if (!limiter) p::append(kept.get(), p::retain(equipment));
            }
            p::Ref equipments(PyList_AsTuple(kept.get()));
            auto crew = item(snapshot.get(), "crew");
            auto factors = method(law.get(), "attribute_factors", descriptor, truthy(crew) ? crew.get() : Py_None, equipments.get());
            setAttr(battle, "_local_factors_cache", truthy(factors) ? factors.get() : Py_False);
        }
        auto cache = attr(battle, "_local_factors_cache");
        return (truthy(cache) ? std::move(cache) : none()).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._local_state(): the player's start row, else one built from the LAN client.
PyObject* localState(PyObject*, PyObject* args) {
    PyObject* battle;
    if (!PyArg_ParseTuple(args, "O", &battle)) return nullptr;
    try {
        auto players = mget(attr(battle, "_start_message").get(), "players");
        p::Ref rows(PySequence_List(truthy(players) ? players.get() : p::Ref(PyTuple_New(0)).get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            PyObject* value = PyList_GET_ITEM(rows.get(), i);
            auto id = mget(value, "id");
            if (test(id.get(), attr(attr(battle, "client").get(), "player_id").get(), Py_EQ))
                return PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), value, nullptr);
        }
        auto client = [&] { return attr(battle, "client"); };
        auto result = literal(9);
        put(result.get(), "id", attr(client().get(), "player_id"));
        put(result.get(), "name", attr(client().get(), "name"));
        put(result.get(), "vehicle", attr(client().get(), "vehicle"));
        put(result.get(), "team", attr(client().get(), "team"));
        put(result.get(), "slot", attr(client().get(), "slot"));
        put(result.get(), "health", attr(client().get(), "max_health"));
        put(result.get(), "max_health", attr(client().get(), "max_health"));
        put(result.get(), "alive", p::boolean(true));
        put(result.get(), "marks_on_gun", getattrOr(client().get(), "marks_on_gun", integer(0).get()));
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._set_gun_locked(locked): freeze or release gun laying without changing the camera.
PyObject* setGunLocked(PyObject*, PyObject* args) {
    PyObject *battle, *locked_raw;
    if (!PyArg_ParseTuple(args, "OO", &battle, &locked_raw)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        auto avatar = attr(battle, "_avatar");
        if (avatar.get() == Py_None) Py_RETURN_FALSE;
        auto rotator = getattrOr(avatar.get(), "gunRotator", Py_None);
        auto lock = getattrOr(rotator.get(), "lock", Py_None);
        if (!PyCallable_Check(lock.get())) raise("#1513 gun lock boundary is unavailable");
        // A replay's saved pose owns the gun; native period callbacks must not release it.
        auto locked = p::boolean(truthy(p::retain(locked_raw)) || truthy(attr(battle, "_replay_mode")));
        setAttr(avatar.get(), "isGunLocked", locked.get());
        call(lock.get(), {locked.get()});
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// float(rotator.<name>), or the pinned RuntimeError for AttributeError/TypeError/ValueError.
p::Ref rotatorAngle(PyObject* rotator, const char* name, const char* message) {
    PyObject* raw = PyObject_GetAttrString(rotator, name);
    PyObject* value = raw ? PyNumber_Float(raw) : nullptr;
    Py_XDECREF(raw);
    if (value) return p::Ref(value);
    if (PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError)) {
        PyErr_Clear();
        raise(message);
    }
    throw p::Error(name);
}

// BattleRuntime._echo_local_gun_angles(turret_yaw=None, gun_pitch=None): the native rotator angle as the server echo.
PyObject* echoLocalGunAngles(PyObject*, PyObject* args) {
    PyObject *battle, *turret_raw, *pitch_raw;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &turret_raw, &pitch_raw)) return nullptr;
    try {
        if (attr(battle, "_server").get() == Py_None || attr(battle, "_binding").get() == Py_None) raise("player gun-angle echo is not attached");
        auto rotator = getattrOr(attr(battle, "_avatar").get(), "gunRotator", Py_None);
        if (rotator.get() == Py_None) raise("#1513 gun rotator is unavailable");
        auto turret_yaw = turret_raw == Py_None ? rotatorAngle(rotator.get(), "turretYaw", "#1513 turret yaw is unavailable for server echo")
                                                : p::retain(turret_raw);
        auto gun_pitch = pitch_raw == Py_None ? rotatorAngle(rotator.get(), "gunPitch", "#1513 gun pitch is unavailable for server echo")
                                              : p::retain(pitch_raw);
        auto hull_yaw = p::Ref(PyNumber_Float(attr(battle, "_local_yaw").get()));
        auto update = attr(attr(battle, "_binding").get(), "update_vehicle_aim");
        auto vehicle_id = attr(attr(battle, "_server").get(), "vehicle_id");
        auto turret = p::Ref(PyNumber_Add(hull_yaw.get(), p::Ref(PyNumber_Float(turret_yaw.get())).get()));
        auto pitch = p::Ref(PyNumber_Float(gun_pitch.get()));
        call(update.get(), {vehicle_id.get(), hull_yaw.get(), turret.get(), pitch.get()});
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._enable_expert_visibility(): give the offline silhouette target sole ownership of Expert.
PyObject* enableExpertVisibility(PyObject*, PyObject* args) {
    PyObject* battle;
    if (!PyArg_ParseTuple(args, "O", &battle)) return nullptr;
    try {
        if (!truthy(attr(battle, "_has_expert")) || truthy(attr(battle, "_expert_visibility_enabled")) || truthy(attr(battle, "_worker_mode")) ||
            attr(battle, "_avatar").get() == Py_None || attr(battle, "_server").get() == Py_None)
            Py_RETURN_FALSE;
        auto statuses = getattrOr(attr(attr(battle, "_runtime").get(), "constants").get(), "VEHICLE_MISC_STATUS", Py_None);
        auto status = getattrOr(statuses.get(), "OTHER_VEHICLE_DAMAGED_DEVICES_VISIBLE", Py_None);
        auto callback = getattrOr(attr(battle, "_avatar").get(), "updateVehicleMiscStatus", Py_None);
        if (status.get() == Py_None || !PyCallable_Check(callback.get())) raise("#1513 Expert visibility boundary is unavailable");
        // PlayerAvatar reads floatArgs[0] even for this integer-only status.
        p::Ref float_args(Py_BuildValue("(d)", 0.0));
        call(callback.get(), {attr(attr(battle, "_server").get(), "vehicle_id").get(), toInt(status.get()).get(), integer(0).get(), float_args.get()});
        setAttr(battle, "_expert_visibility_enabled", Py_True);
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._advance_local_gun_edge(state, now=None): close the old gun timeline before one state mutation.
PyObject* advanceLocalGunEdge(PyObject*, PyObject* args) {
    PyObject *battle, *state, *now_raw;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &state, &now_raw)) return nullptr;
    try {
        if (truthy(getattrOr(battle, "_replay_mode", Py_False))) Py_RETURN_NONE;
        if (state == Py_None || truthy(attr(battle, "_worker_mode")) || attr(battle, "_gun_last_tick").get() == Py_None) Py_RETURN_NONE;
        if (state != attr(battle, "_gun_state").get()) raise("local gun edge does not own the active state");
        if (attr(battle, "_server").get() == Py_None) raise("player gun edge has no server identity");
        auto entity = serverEntity(battle, attr(attr(battle, "_server").get(), "vehicle_id").get());
        if (entity.get() == Py_None || attr(entity.get(), "typeDescriptor").get() == Py_None) raise("player gun edge has no vehicle descriptor");
        auto now = now_raw == Py_None ? method(battle, "_clock") : p::Ref(PyNumber_Float(now_raw));
        auto advanced = method(battle, "_advance_local_gun_to", entity.get(), now.get());
        if (advanced.get() != state) raise("player gun edge replaced the active state");
        return PyTuple_Pack(2, entity.get(), now.get());
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._apply_authority_bot_poses(states): the worker's native Bot runtime presents them.
PyObject* applyAuthorityBotPoses(PyObject*, PyObject* args) {
    PyObject *battle, *states, *bot_module;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &states, &bot_module)) return nullptr;
    try {
        auto bots = attr(battle, "_bots");
        if (bots.get() == Py_None || !truthy(method(bot_module, "enabled", bots.get())))
            raise("authority Bot poses are presented only by the worker's native Bot runtime");
        return method(bot_module, "apply_presentation", bots.get(), states, method(battle, "_clock").get()).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._present_authority_bot_poses(presentation_states, now): one accepted pose set, as a one-tuple count.
PyObject* presentAuthorityBotPoses(PyObject*, PyObject* args) {
    PyObject *battle, *presentation_states, *now;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &presentation_states, &now)) return nullptr;
    try {
        auto states = call(presentation_states, {now});
        if (overridden(battle, "_apply_authority_bot_poses")) method(battle, "_apply_authority_bot_poses", states.get());
        else {
            auto bots = imported("gui.mods.offline_lan_0922.native_bot_runtime");
            p::Ref(applyAuthorityBotPoses(nullptr, p::Ref(PyTuple_Pack(3, battle, states.get(), bots.get())).get()));
        }
        p::Ref count(PyInt_FromSsize_t(PyObject_Size(states.get())));
        if (PyErr_Occurred()) throw p::Error("states");
        return PyTuple_Pack(1, count.get());
    } catch (const std::exception& e) { return fail(e); }
}

// The visible client's drowning, overturn, Expert and outline presentation is native; the worker never runs it.
PyObject* tickDrowning(PyObject*, PyObject* args) {
    PyObject *battle, *dt, *now;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &dt, &now)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode")) || !running(battle)) raise("drowning presentation runs only on the visible client in battle");
        return method(imported("offline_client_environment_native").get(), "drowning", battle, dt, now).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* tickOverturn(PyObject*, PyObject* args) {
    PyObject *battle, *dt, *now;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &dt, &now)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode")) || !running(battle)) raise("overturn presentation runs only on the visible client in battle");
        return method(imported("offline_client_environment_native").get(), "overturn", battle, dt, now).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* tickExpertTarget(PyObject*, PyObject* args) {
    PyObject *battle, *now;
    if (!PyArg_ParseTuple(args, "OO", &battle, &now)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) raise("Expert presentation runs only on the visible client");
        return method(imported("offline_client_query_native").get(), "tick_expert", battle, now).release();
    } catch (const std::exception& e) { return fail(e); }
}
PyObject* updateTargetOutline(PyObject*, PyObject* args) {
    PyObject *battle, *now;
    if (!PyArg_ParseTuple(args, "OO", &battle, &now)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) raise("target outline runs only on the visible client");
        return method(imported("offline_client_query_native").get(), "target_outline", battle, now).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._refresh_fallen_tree_foliage(now, force=False): the native environment owns foliage.
PyObject* refreshFallenTreeFoliage(PyObject*, PyObject* args) {
    PyObject *battle, *now, *force, *environment;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &now, &force, &environment)) return nullptr;
    try {
        auto native = method(environment, "foliage", battle, now, force);
        if (native.get() == Py_None) raise("#1513 native environment owner is unavailable for foliage");
        return native.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._advance_detached_turrets(now): presentation on the visible client, the native Bot runtime on the worker.
PyObject* advanceDetachedTurrets(PyObject*, PyObject* args) {
    PyObject *battle, *now, *turret_module, *bot_module;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &now, &turret_module, &bot_module)) return nullptr;
    try {
        if (!truthy(attr(battle, "_worker_mode"))) return method(turret_module, "frame", battle, now).release();
        auto bots = attr(battle, "_bots");
        if (bots.get() == Py_None || !truthy(method(bot_module, "enabled", bots.get())))
            raise("worker detached turrets need the native Bot runtime");
        return method(bot_module, "detached_advance", bots.get(), now).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"server_entity", serverEntityPort, METH_VARARGS, nullptr},
    {"record_alive", recordAlivePort, METH_VARARGS, nullptr},
    {"server_clock", serverClock, METH_VARARGS, nullptr},
    {"critical_state_copy", criticalState, METH_VARARGS, nullptr},
    {"combat_target_is_spotted", combatTargetIsSpotted, METH_VARARGS, nullptr},
    {"store_remote_visual_components", storeRemoteVisualComponents, METH_VARARGS, nullptr},
    {"record_position", recordPosition, METH_VARARGS, nullptr},
    {"bot_authority_is_local", botAuthorityIsLocal, METH_VARARGS, nullptr},
    {"shot_invisibility_factor", shotInvisibilityFactor, METH_VARARGS, nullptr},
    {"base_invisibility", baseInvisibility, METH_VARARGS, nullptr},
    {"local_factors", localFactors, METH_VARARGS, nullptr},
    {"battle_local_state", localState, METH_VARARGS, nullptr},
    {"set_gun_locked", setGunLocked, METH_VARARGS, nullptr},
    {"echo_local_gun_angles", echoLocalGunAngles, METH_VARARGS, nullptr},
    {"enable_expert_visibility", enableExpertVisibility, METH_VARARGS, nullptr},
    {"advance_local_gun_edge", advanceLocalGunEdge, METH_VARARGS, nullptr},
    {"apply_authority_bot_poses", applyAuthorityBotPoses, METH_VARARGS, nullptr},
    {"present_authority_bot_poses", presentAuthorityBotPoses, METH_VARARGS, nullptr},
    {"tick_drowning", tickDrowning, METH_VARARGS, nullptr},
    {"tick_overturn", tickOverturn, METH_VARARGS, nullptr},
    {"tick_expert_target", tickExpertTarget, METH_VARARGS, nullptr},
    {"update_target_outline", updateTargetOutline, METH_VARARGS, nullptr},
    {"refresh_fallen_tree_foliage", refreshFallenTreeFoliage, METH_VARARGS, nullptr},
    {"advance_detached_turrets", advanceDetachedTurrets, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* helper_steps_methods() { return helper_steps::methods; }
}
