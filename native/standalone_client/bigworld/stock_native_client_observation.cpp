// Native ports of the pinned BattleRuntime observation, battle-live, splash and spotting-input steps.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace observation_steps {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref flt(double v) { return p::Ref(PyFloat_FromDouble(v)); }
bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("observation comparison failed");
    return result != 0;
}
p::Ref getattrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
// mapping.get(name[, fallback]) through the object's own method.
p::Ref mget(PyObject* mapping, const char* name, PyObject* fallback = nullptr) {
    return fallback ? method(mapping, "get", p::text(name).get(), fallback) : method(mapping, "get", p::text(name).get());
}
p::Ref field(PyObject* value, const char* name, PyObject* fallback) {
    return PyDict_Check(value) ? method(value, "get", p::text(name).get(), fallback) : getattrOr(value, name, fallback);
}
double asDouble(PyObject* v) { return PyFloat_AsDouble(p::Ref(PyNumber_Float(v)).get()); }
// Module helper _number(value, default): float(value) when finite, else float(default).
double numberOr(PyObject* value, PyObject* fallback) {
    PyObject* f = PyNumber_Float(value);
    if (!f) {
        if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError)) throw p::Error("float");
        PyErr_Clear();
        return asDouble(fallback);
    }
    const double out = PyFloat_AS_DOUBLE(p::Ref(f).get());
    return std::isfinite(out) ? out : asDouble(fallback);
}
Py_ssize_t length(PyObject* v) {
    const Py_ssize_t n = PyObject_Size(v);
    if (n < 0) throw p::Error("len");
    return n;
}

// BattleRuntime.on_battle_live(message): start the one server-owned countdown.
PyObject* onBattleLive(PyObject*, PyObject* args) {
    PyObject *battle, *message, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &message, &g)) return nullptr;
    try {
        if (test(attr(battle, "state").get(), p::text("running").get(), Py_NE) || truthy(attr(battle, "_battle_live")) ||
            attr(battle, "_prebattle_deadline").get() != Py_None)
            Py_RETURN_FALSE;
        method(battle, "_observe_destructibles_disabled", message);
        auto source = [&] { return p::flag(message) ? p::retain(message) : p::dict(); };
        auto countdown_raw = mget(source().get(), "countdown_seconds");
        double countdown = numberOr(countdown_raw.get(), method(battle, "_prebattle_seconds").get());
        countdown = countdown > 0.0 ? countdown : 0.0;
        auto duration_raw = mget(source().get(), "battle_duration_seconds");
        double duration = numberOr(duration_raw.get(), method(battle, "_battle_seconds").get());
        duration = duration > 1.0 ? duration : 1.0;
        auto client = attr(battle, "client");
        auto deadline = getattrOr(client.get(), "combat_deadline", Py_None);
        if (deadline.get() != Py_None) {
            const double until = asDouble(deadline.get());
            const double remaining = until - asDouble(p::Ref(PyObject_CallFunctionObjArgs(global(g, "_monotonic_time").get(), nullptr)).get());
            countdown = remaining > 0.0 ? remaining : 0.0;
        }
        auto network_duration = getattrOr(client.get(), "combat_duration", Py_None);
        if (network_duration.get() != Py_None) {
            const double value = asDouble(network_duration.get());
            duration = value > 1.0 ? value : 1.0;
        }
        auto countdown_value = flt(countdown);
        if (PyObject_SetItem(attr(battle, "_config").get(), p::text("battleDurationSeconds").get(), flt(duration).get()) < 0) throw p::Error("config");
        if (!truthy(attr(battle, "_worker_mode"))) {
            method(attr(battle, "_binding").get(), "arena_period", p::text("prebattle").get(), countdown_value.get());
            method(battle, "_reset_prebattle_native_visuals");
            method(battle, "_show_prebattle_crosshair");
        }
        setAttr(battle, "_prebattle_deadline", p::Ref(PyNumber_Add(method(battle, "_clock").get(), countdown_value.get())).get());
        setAttr(battle, "_last_frame_time", method(battle, "_clock").get());
        if (countdown <= 0.0 && truthy(method(battle, "_prebattle_transition_ready", method(battle, "_clock").get())))
            method(battle, "_begin_battle");
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime.on_bot_observation(message): consume one server-admitted observation.
PyObject* onBotObservation(PyObject*, PyObject* args) {
    PyObject *battle, *message_raw, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &message_raw, &g)) return nullptr;
    try {
        if (test(attr(battle, "state").get(), p::text("running").get(), Py_NE)) Py_RETURN_FALSE;
        auto message = PyDict_Check(message_raw) ? p::retain(message_raw) : p::dict();
        auto start = attr(battle, "_start_message");
        if (start.get() == Py_None || test(mget(message.get(), "round_id").get(), mget(start.get(), "round_id").get(), Py_NE)) Py_RETURN_FALSE;
        try {
            auto now = method(battle, "_clock");
            auto team = method(battle, "_apply_team_observation", message.get(), now.get());
            auto enemy = dispatch(battle, "_observe_local_vehicle", "observe_local_vehicle", {message.get(), now.get()}, {g});
            return p::retain(truthy(team) || truthy(enemy) ? Py_True : Py_False).release();
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            auto error = caught(e);
            method(battle, "_fail", error.get());
            Py_RETURN_FALSE;
        }
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._collection_counts(): per-round collection sizes for retention trends.
PyObject* collectionCounts(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto size = [&](const char* name) { return integer(length(attr(battle, name).get())); };
        auto counts = literal(10);
        put(counts.get(), "journal", size("_event_journal"));
        put(counts.get(), "accepted_ids", size("_accepted_event_ids"));
        put(counts.get(), "applied_ids", size("_applied_event_ids"));
        put(counts.get(), "records", size("_records"));
        std::int64_t dead = 0;
        auto values = method(attr(battle, "_records").get(), "values");
        p::Ref iterator(PyObject_GetIter(values.get()));
        while (PyObject* item = PyIter_Next(iterator.get())) {
            p::Ref record(item);
            auto state = mget(record.get(), "state");
            if (!truthy(mget(truthy(state) ? state.get() : p::dict().get(), "alive", Py_True))) ++dead;
        }
        if (PyErr_Occurred()) throw p::Error("records");
        put(counts.get(), "records_dead", integer(dead));
        put(counts.get(), "health", size("_last_health"));
        put(counts.get(), "pending_bots", size("_pending_bot_creates"));
        put(counts.get(), "grounded_bots", size("_grounded_bot_ids"));
        put(counts.get(), "bot_assignments", size("_bot_vehicle_assignments"));
        put(counts.get(), "bot_fire_seen", size("_bot_fire_seen"));
        PyObject* projectiles = PyObject_GetAttrString(battle, "_projectiles");
        Py_ssize_t projectile_count = projectiles ? PyObject_Size(projectiles) : -1;
        Py_XDECREF(projectiles);
        if (projectile_count < 0) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError) && !PyErr_ExceptionMatches(PyExc_TypeError)) throw p::Error("projectiles");
            PyErr_Clear();
            projectile_count = 0;
        }
        put(counts.get(), "projectiles", integer(projectile_count));
        auto registry_counts = getattrOr(attr(battle, "_destructibles").get(), "registry_counts", Py_None);
        if (PyCallable_Check(registry_counts.get())) {
            auto items = method(p::Ref(PyObject_CallFunctionObjArgs(registry_counts.get(), nullptr)).get(), "items");
            p::Ref rows(PyObject_GetIter(items.get()));
            while (PyObject* item = PyIter_Next(rows.get())) {
                p::Ref row(item);
                auto pair = unpack(row.get(), 2);
                p::Ref key(PyNumber_Add(p::text("destr_").get(), pair[0].get()));
                if (PyObject_SetItem(counts.get(), key.get(), pair[1].get()) < 0) throw p::Error("registry");
            }
            if (PyErr_Occurred()) throw p::Error("registry");
        }
        auto states = getattrOr(attr(battle, "_bots").get(), "states", Py_None);
        const Py_ssize_t state_count = PyObject_Size(states.get());
        if (state_count < 0) {
            if (!PyErr_ExceptionMatches(PyExc_TypeError)) throw p::Error("states");
            PyErr_Clear();
        }
        put(counts.get(), "bot_states", integer(state_count < 0 ? 0 : state_count));
        put(counts.get(), "pose_keyframes", p::Ref(PyObject_CallFunctionObjArgs(global(g, "pose_animation_writes").get(), nullptr)));
        return counts.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._present_splash_hit(...): one HE near miss, as showSplashHitEffect presents it.
PyObject* presentSplashHit(PyObject*, PyObject* args) {
    PyObject *battle, *target_record, *effects_descr, *effects_index, *impact, *direction, *damage_factor, *g;
    if (!PyArg_ParseTuple(args, "OOOOOOOO", &battle, &target_record, &effects_descr, &effects_index, &impact, &direction, &damage_factor, &g))
        return nullptr;
    try {
        PyObject* raw = PyObject_GetItem(effects_descr, p::text("armorSplashHit").get());
        if (!raw) {
            if (PyErr_ExceptionMatches(PyExc_KeyError) || PyErr_ExceptionMatches(PyExc_TypeError)) { PyErr_Clear(); Py_RETURN_FALSE; }
            return nullptr;
        }
        p::Ref effects_list(raw);
        if (!truthy(effects_list)) Py_RETURN_FALSE;
        std::vector<p::Ref> parts;
        try {
            parts = unpack(effects_list.get(), 3);
        } catch (const std::exception&) {
            if (!PyErr_Occurred() || (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError))) throw;
            PyErr_Clear();
            Py_RETURN_FALSE;
        }
        auto target = serverEntity(battle, mget(target_record, "engine_id").get());
        if (target.get() == Py_None || !truthy(getattrOr(target.get(), "isStarted", Py_False)) ||
            !truthy(recordAlive(battle, target_record, target.get())))
            Py_RETURN_FALSE;
        if (!truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {p::text("projectile impact presentation").get()})))
            Py_RETURN_FALSE;
        auto hull = getattrOr(getattrOr(attr(battle, "_runtime").get(), "tank_part_names", Py_None).get(), "HULL", Py_None);
        if (hull.get() == Py_None) raise("#1513 HULL part name is unavailable");
        try {
            // The compound-only fallback builds its bound-effect owner lazily and raises without a compound.
            auto add_effect = getattrOr(attr(attr(target.get(), "appearance").get(), "boundEffects").get(), "addNewToNode", Py_None);
            if (!PyCallable_Check(add_effect.get())) Py_RETURN_FALSE;
            auto matrix = method(attr(attr(battle, "_runtime").get(), "math").get(), "Matrix");
            p::Ref origin(Py_BuildValue("(ddd)", 0.0, 0.0, 0.0));
            method(matrix.get(), "setTranslate", origin.get());
            dispatch(battle, "_report_effect", "report_effect",
                     {p::text("armour_splash").get(), p::text("armorSplashHit").get(), effects_index, impact, direction}, {},
                     {p::text("armour_splash").get(), p::text("armorSplashHit").get(), effects_index, impact, direction, Py_None, g});
            p::Ref positional(PyTuple_Pack(4, hull.get(), matrix.get(), parts[1].get(), parts[0].get()));
            callWithMapping(add_effect.get(), positional.get(), {{"entity", target.get()}, {"damageFactor", damage_factor}}, p::dict().get());
        } catch (const std::exception& e) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            auto error = caught(e);
            method(battle, "_warn_optional_failure", p::text("projectile impact presentation").get(), error.get());
            Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._log_bot_motion_probes(now): nearby replicas' confirmed cursor against the model provider.
PyObject* logBotMotionProbes(PyObject*, PyObject* args) {
    PyObject *battle, *now, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &now, &g)) return nullptr;
    try {
        auto start = attr(battle, "_start_message");
        auto round_id = mget(truthy(start) ? start.get() : p::dict().get(), "round_id");
        p::Ref epoch(PyTuple_Pack(2, attr(battle, "_generation").get(), round_id.get()));
        if (test(epoch.get(), getattrOr(battle, "_motion_probe_epoch", Py_None).get(), Py_NE)) {
            setAttr(battle, "_motion_probe_epoch", epoch.get());
            setAttr(battle, "_motion_probe_next", flt(0.0).get());
            setAttr(battle, "_motion_probe_count", integer(0).get());
        }
        if (test(now, attr(battle, "_motion_probe_next").get(), Py_LT) || test(attr(battle, "_motion_probe_count").get(), integer(900).get(), Py_GE))
            Py_RETURN_FALSE;
        setAttr(battle, "_motion_probe_next", p::Ref(PyNumber_Add(now, flt(1.0).get())).get());
        auto rows = method(attr(battle, "_sync").get(), "take_motion_probes", now);
        p::Ref candidates(PyList_New(0));
        auto origin = getattrOr(attr(battle, "_avatar").get(), "position", Py_None);
        auto items = method(rows.get(), "items");
        p::Ref iterator(PyObject_GetIter(items.get()));
        while (PyObject* item = PyIter_Next(iterator.get())) {
            p::Ref pair_raw(item);
            auto pair = unpack(pair_raw.get(), 2);
            auto record = method(attr(battle, "_records").get(), "get", pair[0].get());
            if (!truthy(record) || !truthy(mget(record.get(), "ready")) || !truthy(mget(record.get(), "spot_visible"))) continue;
            auto pose = p::Ref(PyObject_GetItem(pair[1].get(), p::text("presented_pose").get()));
            p::Ref distance = flt(0.0);
            if (origin.get() != Py_None) {
                auto axis = [&](const char* name, const char* attribute) {
                    p::Ref value(PyObject_GetItem(pose.get(), p::text(name).get()));
                    p::Ref base(PyNumber_Float(attr(origin.get(), attribute).get()));
                    p::Ref delta(PyNumber_Subtract(value.get(), base.get()));
                    return p::Ref(PyNumber_Power(delta.get(), integer(2).get(), Py_None));
                };
                auto dx = axis("x", "x");
                auto dz = axis("z", "z");
                replace(distance, p::Ref(PyNumber_Add(dx.get(), dz.get())));
            }
            p::append(candidates.get(), p::Ref(PyTuple_Pack(4, distance.get(), pair[0].get(), record.get(), pair[1].get())));
        }
        if (PyErr_Occurred()) throw p::Error("probes");
        // sorted(candidates, key=lambda item: item[:2])[:4]
        p::Ref keyed(PyList_New(0));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(candidates.get()); ++i) {
            PyObject* candidate = PyList_GET_ITEM(candidates.get(), i);
            p::Ref key(PyTuple_GetSlice(candidate, 0, 2));
            p::append(keyed.get(), p::Ref(PyTuple_Pack(3, key.get(), integer(i).get(), candidate)));
        }
        if (PyList_Sort(keyed.get()) < 0) throw p::Error("sort");
        p::Ref output(PyList_New(0));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(keyed.get()) && i < 4; ++i) {
            PyObject* candidate = PyTuple_GET_ITEM(PyList_GET_ITEM(keyed.get(), i), 2);
            PyObject* distance = PyTuple_GET_ITEM(candidate, 0);
            PyObject* key = PyTuple_GET_ITEM(candidate, 1);
            PyObject* record = PyTuple_GET_ITEM(candidate, 2);
            PyObject* row = PyTuple_GET_ITEM(candidate, 3);
            auto engine_id = p::Ref(PyObject_GetItem(record, p::text("engine_id").get()));
            auto sqrt = p::Ref(PyObject_CallFunctionObjArgs(attr(global(g, "math").get(), "sqrt").get(), distance, nullptr));
            // CPython 2.7 fills a C callee's keyword dict last keyword first.
            auto update = p::dict();
            put(update.get(), "distance_m", sqrt);
            put(update.get(), "engine_id", p::retain(engine_id.get()));
            put(update.get(), "entity", p::retain(key));
            p::Ref(PyObject_Call(attr(row, "update").get(), p::Ref(PyTuple_New(0)).get(), update.get()));
            auto vehicle = method(attr(battle, "_remote_factory").get(), "get", engine_id.get());
            auto model = getattrOr(vehicle.get(), "model", Py_None);
            if (model.get() != Py_None) {
                auto matrix = method(attr(attr(battle, "_runtime").get(), "math").get(), "Matrix", attr(model.get(), "matrix").get());
                auto translation = attr(matrix.get(), "translation");
                p::Ref position(PyList_New(3));
                const char* axes[] = {"x", "y", "z"};
                for (Py_ssize_t a = 0; a < 3; ++a) PyList_SET_ITEM(position.get(), a, p::Ref(PyNumber_Float(attr(translation.get(), axes[a]).get())).release());
                if (PyObject_SetItem(row, p::text("model_position").get(), position.get()) < 0) throw p::Error("model_position");
                auto provider = attr(reinterpret_cast<PyObject*>(Py_TYPE(attr(model.get(), "matrix").get())), "__name__");
                if (PyObject_SetItem(row, p::text("provider_type").get(), provider.get()) < 0) throw p::Error("provider_type");
            }
            p::append(output.get(), p::retain(row));
        }
        if (PyList_GET_SIZE(output.get())) {
            auto writer = attr(p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.native_input_diagnostics")).get(), "write_json_record");
            auto keywords = p::dict();
            put(keywords.get(), "bots", p::retain(output.get()));
            put(keywords.get(), "time", p::retain(now));
            put(keywords.get(), "round", p::retain(round_id.get()));
            p::Ref payload(PyDict_Copy(keywords.get()));
            p::Ref(PyObject_CallFunctionObjArgs(writer.get(), p::text("BOT MOTION PRESENT").get(), payload.get(), nullptr));
            setAttr(battle, "_motion_probe_count", p::Ref(PyNumber_InPlaceAdd(attr(battle, "_motion_probe_count").get(), integer(1).get())).get());
        }
        return p::retain(PyList_GET_SIZE(output.get()) ? Py_True : Py_False).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._spotting_profile(descriptor, local=False): device and crew spotting inputs.
PyObject* spottingProfile(PyObject*, PyObject* args) {
    PyObject *battle, *descriptor, *local_raw, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &descriptor, &local_raw, &g)) return nullptr;
    try {
        const bool local = truthy(p::retain(local_raw));
        auto law = global(g, "loadout_law");
        if (local && truthy(getattrOr(battle, "_replay_mode", Py_False))) {
            auto snapshot = method(battle, "_accepted_local_effective_params");
            if (snapshot.get() != Py_None) return PyObject_GetItem(snapshot.get(), p::text("spotting").get());
        }
        if (local) {
            if (attr(battle, "_local_spotting_cache").get() == Py_None) {
                auto snapshot = method(battle, "_garage_loadout_snapshot");
                auto crew = p::Ref(PyObject_GetItem(snapshot.get(), p::text("crew").get()));
                if (!truthy(crew)) replace(crew, none());
                p::Ref equipments(PyObject_GetItem(snapshot.get(), p::text("equipments").get()));
                auto skills = truthy(crew) ? method(law.get(), "crew_skill_names", crew.get()) : none();
                auto level = method(law.get(), "crew_level_increase", descriptor, equipments.get(), skills.get());
                auto factors = method(battle, "_local_factors", descriptor);
                auto profile = callWithMapping(attr(law.get(), "spotting_profile").get(), p::Ref(PyTuple_Pack(2, descriptor, crew.get())).get(),
                                               {{"level_increase", level.get()}, {"factors", factors.get()}}, p::dict().get());
                setAttr(battle, "_local_spotting_cache", profile.get());
            }
            return attr(battle, "_local_spotting_cache").release();
        }
        // Every non-local vehicle carries the default crew; its profile depends only on type and devices.
        auto empty = p::text("");
        p::Ref key(PyTuple_Pack(2, field(descriptor, "name", empty.get()).get(), method(law.get(), "device_names", descriptor).get()));
        auto cache = attr(battle, "_remote_spotting_cache");
        auto profile = method(cache.get(), "get", key.get());
        if (profile.get() == Py_None) {
            auto level = method(law.get(), "crew_level_increase", descriptor);
            auto factors = method(law.get(), "attribute_factors", descriptor);
            replace(profile, callWithMapping(attr(law.get(), "spotting_profile").get(), p::Ref(PyTuple_Pack(2, descriptor, Py_None)).get(),
                                             {{"level_increase", level.get()}, {"factors", factors.get()}}, p::dict().get()));
            if (PyObject_SetItem(cache.get(), key.get(), profile.get()) < 0) throw p::Error("cache");
        }
        return profile.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._vision_radius(descriptor, entity=None, still_seconds=0.0, local=False)
PyObject* visionRadius(PyObject*, PyObject* args) {
    PyObject *battle, *descriptor, *entity, *still_seconds, *local, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &descriptor, &entity, &still_seconds, &local, &g)) return nullptr;
    try {
        auto turret = field(descriptor, "turret", p::dict().get());
        auto misc = field(descriptor, "miscAttrs", p::dict().get());
        p::Ref damage_factor = flt(1.0);
        if (entity != Py_None) {
            auto critical = global(g, "critical_damage");
            auto factor = method(critical.get(), "stat_factor", entity, p::text("vision").get());
            replace(damage_factor, method(attr(critical.get(), "_device_damage").get(), "clamp_vision_factor", factor.get()));
        }
        auto profile = dispatch(battle, "_spotting_profile", "spotting_profile", {descriptor, local}, {g});
        auto law = global(g, "loadout_law");
        auto radius = field(turret.get(), "circularVisionRadius", flt(400.0).get());
        auto misc_factor = p::Ref(PyNumber_Multiply(field(misc.get(), "circularVisionRadiusFactor", flt(1.0).get()).get(), damage_factor.get()));
        auto crew = p::Ref(PyObject_GetItem(profile.get(), p::text("vision_factor").get()));
        auto binocular = p::Ref(PyObject_GetItem(profile.get(), p::text("binocular_factor").get()));
        auto has = p::Ref(PyObject_GetItem(profile.get(), p::text("has_binoculars").get()));
        auto active = truthy(has) ? method(law.get(), "still_device_active", still_seconds,
                                           p::Ref(PyObject_GetItem(profile.get(), p::text("binocular_delay").get())).get())
                                  : std::move(has);
        return callWithMapping(attr(global(g, "spotting").get(), "effective_view_range").get(), p::Ref(PyTuple_Pack(1, radius.get())).get(),
                               {{"misc_factor", misc_factor.get()}, {"crew_factor", crew.get()}, {"binocular_factor", binocular.get()},
                                {"binocular_active", active.get()}},
                               p::dict().get()).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"on_battle_live", onBattleLive, METH_VARARGS, "Start the server-owned countdown."},
    {"on_bot_observation", onBotObservation, METH_VARARGS, "Consume one server-admitted observation."},
    {"collection_counts", collectionCounts, METH_VARARGS, "Per-round collection sizes."},
    {"present_splash_hit", presentSplashHit, METH_VARARGS, "Present one HE near miss."},
    {"log_bot_motion_probes", logBotMotionProbes, METH_VARARGS, "Report nearby replicas' presentation cursors."},
    {"spotting_profile", spottingProfile, METH_VARARGS, "Device and crew spotting inputs for one descriptor."},
    {"vision_radius", visionRadius, METH_VARARGS, "One vehicle's effective view range."},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* observation_steps_methods() { return observation_steps::methods; }
}
