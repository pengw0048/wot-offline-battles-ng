// Visible-client projectile presentation: ownership, meta and descriptor lookups, ported from the pinned
// battle runtime. Module constants come from the runtime's globals().
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace {
using namespace py;

p::Ref recordFor(PyObject* battle, PyObject* meta) {
    return recordsGet(battle, formatKey(method(meta, "get", p::text("shooter_kind").get()).get(),
                                        method(meta, "get", p::text("shooter_id").get()).get()).get());
}
// BattleRuntime._descriptor_shot (staticmethod)
p::Ref descriptorShot(PyObject* descriptor, PyObject* shell_index) {
    auto raw = attr(attr(descriptor, "gun").get(), "shots");
    p::Ref shots(PySequence_Tuple(truthy(raw) ? raw.get() : p::Ref(PyTuple_New(0)).get()));
    p::Ref index = p::retain(shell_index);
    if (index.get() == Py_None) {
        PyObject* active = PyObject_GetAttrString(descriptor, "activeGunShotIndex");
        if (!active) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("active shot lookup failed");
            PyErr_Clear(); active = integer(0).release();
        }
        replace(index, p::Ref(active));
    }
    const Py_ssize_t count = PyTuple_GET_SIZE(shots.get());
    const auto chosen = std::max<std::int64_t>(0, std::min<std::int64_t>(strictInt(index.get()), std::max<Py_ssize_t>(0, count - 1)));
    return count ? p::retain(PyTuple_GET_ITEM(shots.get(), chosen)) : p::dict();
}
p::Ref field(PyObject* g, PyObject* owner, const char* name, PyObject* fallback) {
    return p::Ref(PyObject_CallFunctionObjArgs(global(g, "_field").get(), owner, p::text(name).get(), fallback, nullptr));
}

PyObject* projectileIsAuthority(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        if (truthy(attr(battle, "_replay_mode"))) Py_RETURN_FALSE;
        auto client = attr(battle, "client");
        if (!truthy(attr(battle, "_worker_mode"))) {
            auto player = optionalAttr(client.get(), "player_id");
            const int typed = PyObject_IsInstance(player.get(), global(g, "_INTEGER_TYPES").get());
            if (typed < 0) return nullptr;
            if (!typed || PyBool_Check(player.get())) Py_RETURN_FALSE;
            return PyObject_RichCompare(player.get(), integer(0).get(), Py_GT);
        }
        p::Ref checker = p::retain(Py_None);
        if (!callableAttr(client.get(), "is_bot_authority", checker)) Py_RETURN_FALSE;
        PyObject* result = PyObject_CallObject(checker.get(), nullptr);
        if (!result) {
            if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
            PyErr_Clear(); Py_RETURN_FALSE;
        }
        p::Ref held(result);
        const int value = PyObject_IsTrue(result);
        if (value < 0) { if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr; PyErr_Clear(); Py_RETURN_FALSE; }
        return p::boolean(value != 0).release();
    } catch (const std::exception& e) { return fail(e); }
}

// One evaluator owns each shell; other clients only present it.
PyObject* ownsProjectile(PyObject*, PyObject* args) {
    PyObject *battle, *meta, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &meta, &g)) return nullptr;
    try {
        if (!truthy(dispatch(battle, "_projectile_is_authority", "projectile_is_authority", {}, {g})) || !PyDict_Check(meta))
            Py_RETURN_FALSE;
        auto kind = method(meta, "get", p::text("shooter_kind").get());
        if (truthy(attr(battle, "_worker_mode"))) return PyObject_RichCompare(kind.get(), p::text("bot").get(), Py_EQ);
        p::Ref player(PyObject_RichCompare(kind.get(), p::text("player").get(), Py_EQ));
        if (!truthy(player)) return player.release();
        return PyObject_RichCompare(method(meta, "get", p::text("shooter_id").get()).get(),
                                    attr(attr(battle, "client").get(), "player_id").get(), Py_EQ);
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* projectileEstimatedServerTime(PyObject*, PyObject* args) {
    PyObject *battle, *now;
    if (!PyArg_ParseTuple(args, "OO", &battle, &now)) return nullptr;
    try {
        auto server = attr(battle, "_projectile_server_time_ms");
        auto local = attr(battle, "_projectile_server_local_time");
        if (server.get() == Py_None || local.get() == Py_None) Py_RETURN_NONE;
        p::Ref current(PyNumber_Float(now));
        p::Ref anchor(PyNumber_Float(local.get()));
        const double elapsed = std::max(0., PyFloat_AS_DOUBLE(current.get()) - PyFloat_AS_DOUBLE(anchor.get()));
        p::Ref total(PyNumber_Add(server.get(), p::number(elapsed * 1000.).get()));
        return PyNumber_Int(total.get());
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* newProjectileMeta(PyObject*, PyObject* args) {
    PyObject *battle, *normalized;
    if (!PyArg_ParseTuple(args, "OO", &battle, &normalized)) return nullptr;
    try {
        p::Ref projectile_id(PyObject_GetItem(normalized, p::text("projectile_id").get()));
        p::Ref meta(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), normalized, nullptr));
        p::Ref ricochets(PyObject_GetItem(normalized, p::text("ricochet_count").get()));
        put(meta.get(), "manager_key", equal(ricochets.get(), integer(0).get()) ? p::retain(projectile_id.get()) :
            p::Ref(PyTuple_Pack(2, projectile_id.get(), p::Ref(PyObject_GetItem(normalized, p::text("ricochet_count").get())).get())));
        put(meta.get(), "destructibles_pending", p::list());
        // Own-player collision samples already follow the displayed poses.
        put(meta.get(), "presentation_offsets", p::dict());
        return meta.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* projectileSourceDescriptor(PyObject*, PyObject* args) {
    PyObject *battle, *meta;
    if (!PyArg_ParseTuple(args, "OO", &battle, &meta)) return nullptr;
    try {
        auto descriptor = method(meta, "get", p::text("source_descriptor").get());
        if (descriptor.get() != Py_None) return descriptor.release();
        auto vehicle = method(meta, "get", p::text("source_vehicle").get());
        if (!truthy(vehicle)) Py_RETURN_NONE;
        PyObject* resolved = PyObject_CallMethod(battle, const_cast<char*>("_resolve_descriptor"), const_cast<char*>("(O)"), vehicle.get());
        if (!resolved) {
            if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
            PyErr_Clear(); Py_RETURN_NONE;
        }
        p::Ref held(resolved);
        if (PyObject_SetItem(meta, p::text("source_descriptor").get(), resolved) < 0) return nullptr;
        return held.release();
    } catch (const std::exception& e) { return fail(e); }
}

// The engine id #1513 hit sounds compare against the player.
PyObject* projectileAttackerEngineId(PyObject*, PyObject* args) {
    PyObject *battle, *meta;
    if (!PyArg_ParseTuple(args, "OO", &battle, &meta)) return nullptr;
    try {
        auto record = recordFor(battle, meta);
        if (record.get() == Py_None) return integer(0).release();
        auto engine = method(record.get(), "get", p::text("engine_id").get());
        return intOr(truthy(engine) ? engine.get() : integer(0).get(), 0).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* projectileSourceEntity(PyObject*, PyObject* args) {
    PyObject *battle, *meta;
    if (!PyArg_ParseTuple(args, "OO", &battle, &meta)) return nullptr;
    try {
        auto record = recordFor(battle, meta);
        if (record.get() == Py_None) Py_RETURN_NONE;
        return method(battle, "_server_entity", method(record.get(), "get", p::text("engine_id").get()).get()).release();
    } catch (const std::exception& e) { return fail(e); }
}

// Bounded monotonic timestamps for per-shot diagnostics.
PyObject* rememberFireTimeline(PyObject*, PyObject* args) {
    PyObject *battle, *key, *value;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &key, &value)) return nullptr;
    try {
        auto timeline = attr(battle, "_fire_timeline");
        method(timeline.get(), "pop", key, Py_None);
        if (PyObject_SetItem(timeline.get(), key, value) < 0) return nullptr;
        while (PyObject_Size(timeline.get()) > 64) callKw(timeline.get(), "popitem", {}, {{"last", Py_False}});
        if (PyErr_Occurred()) return nullptr;
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* projectileShot(PyObject*, PyObject* args) {
    PyObject *battle, *meta, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &meta, &g)) return nullptr;
    try {
        auto frozen = method(meta, "get", p::text("source_shot").get());
        auto source = dispatch(battle, "_projectile_source_entity", "projectile_source_entity", {meta});
        auto descriptor = source.get() != Py_None ? optionalAttr(source.get(), "typeDescriptor") : p::retain(Py_None);
        if (descriptor.get() == Py_None)
            replace(descriptor, dispatch(battle, "_projectile_source_descriptor", "projectile_source_descriptor", {meta}));
        auto shot = descriptor.get() != Py_None ? descriptorShot(descriptor.get(), method(meta, "get", p::text("shell_index").get()).get())
                                                : p::retain(Py_None);
        if (!PyDict_Check(frozen.get())) return (truthy(shot) ? std::move(shot) : p::dict()).release();
        // Physical values are immutable launch evidence; keep only descriptor-owned cosmetic keys.
        p::Ref result(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), frozen.get(), nullptr));
        auto frozen_shell = method(frozen.get(), "get", p::text("shell").get());
        p::Ref shell(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type),
                                                  truthy(frozen_shell) ? frozen_shell.get() : p::dict().get(), nullptr));
        auto descriptor_shell = field(g, shot.get(), "shell", Py_None);
        for (const char* name : {"compactDescr", "effectsIndex", "isTracer"}) {
            auto value = field(g, descriptor_shell.get(), name, Py_None);
            if (value.get() != Py_None) put(shell.get(), name, std::move(value));
        }
        put(result.get(), "shell", std::move(shell));
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// warn(name, error) for the Python exception currently set (or a RuntimeError for a C++ failure).
void warnCaught(PyObject* battle, const char* name, const std::exception& e, bool keep_enabled) {
    if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
    auto error = caught(e);
    if (keep_enabled) callKw(battle, "_warn_optional_failure", {p::text(name).get(), error.get()}, {{"disable", Py_False}});
    else method(battle, "_warn_optional_failure", p::text(name).get(), error.get());
}

// Ask the presenter for cosmetic capacity, never authority capacity.
PyObject* admitProjectileVisual(PyObject*, PyObject* args) {
    PyObject *battle, *attacker_id, *projectile_id, *now;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &attacker_id, &projectile_id, &now)) return nullptr;
    try {
        auto factory = attr(battle, "_remote_factory");
        if (projectile_id == Py_None || factory.get() == Py_None) Py_RETURN_TRUE;
        p::Ref callback = p::retain(Py_None);
        if (!callableAttr(factory.get(), "admit_projectile_visual", callback)) Py_RETURN_TRUE;
        try {
            return p::boolean(truthy(p::Ref(PyObject_CallFunctionObjArgs(callback.get(), attacker_id, projectile_id, now, nullptr)))).release();
        } catch (const std::exception& e) {
            warnCaught(battle, "projectile visual admission", e, false);
            Py_RETURN_FALSE;
        }
    } catch (const std::exception& e) { return fail(e); }
}

// Advance stock local shot state without starting another effect.
PyObject* showLocalShotWithoutExtra(PyObject*, PyObject* args) {
    PyObject *battle, *entity, *burst_count, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &entity, &burst_count, &g)) return nullptr;
    try {
        auto shoot = [&]() { return p::Ref(PyObject_CallMethod(entity, const_cast<char*>("showShooting"), const_cast<char*>("(OO)"), burst_count, Py_False)); };
        auto extras = optionalAttr(optionalAttr(entity, "typeDescriptor").get(), "extrasDict");
        p::Ref original = p::retain(Py_None);
        {
            PyObject* found = PyObject_CallMethod(extras.get(), const_cast<char*>("get"), const_cast<char*>("(s)"), "shoot");
            if (found) replace(original, p::Ref(found));
            else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
            else return nullptr;
        }
        // No shoot extra: the stock call has no cosmetic work to suppress, only its dispersion handshake.
        if (original.get() == Py_None) return shoot().release();
        try { method(original.get(), "stopFor", entity); }
        catch (const std::exception& e) { warnCaught(battle, "shot muzzle retirement", e, true); }
        auto muted = p::Ref(PyObject_CallFunctionObjArgs(global(g, "_MutedShootingExtra").get(), original.get(), nullptr));
        if (PyObject_SetItem(extras.get(), p::text("shoot").get(), muted.get()) < 0) {
            // A non-mutable descriptor cannot be intercepted; keep stock shot convergence.
            if (!PyErr_ExceptionMatches(PyExc_Exception)) return nullptr;
            PyErr_Clear();
            return shoot().release();
        }
        PyObject* result = PyObject_CallMethod(entity, const_cast<char*>("showShooting"), const_cast<char*>("(OO)"), burst_count, Py_False);
        PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
        if (!result) PyErr_Fetch(&type, &value, &trace);
        // finally: extras['shoot'] = original; its own failure replaces the call's outcome.
        if (PyObject_SetItem(extras.get(), p::text("shoot").get(), original.get()) < 0) {
            Py_XDECREF(result); Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
            return nullptr;
        }
        if (!result) PyErr_Restore(type, value, trace);
        return result;
    } catch (const std::exception& e) { return fail(e); }
}

// Whether the stock muzzle effect belongs to a drawable shooter (retail AoI delivery rule).
PyObject* shotMuzzleDrawn(PyObject*, PyObject* args) {
    PyObject *battle, *record;
    if (!PyArg_ParseTuple(args, "OO", &battle, &record)) return nullptr;
    try {
        if (truthy(method(record, "get", p::text("local").get()))) Py_RETURN_TRUE;
        return p::boolean(truthy(method(record, "get", p::text("spot_visible").get(), Py_True))).release();
    } catch (const std::exception& e) { return fail(e); }
}

// Play the native shooting extra without creating a new fire intent.
PyObject* showReplayLocalMuzzle(PyObject*, PyObject* args) {
    PyObject *battle, *entity, *burst_count;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &entity, &burst_count)) return nullptr;
    try {
        auto extras = attr(attr(entity, "typeDescriptor").get(), "extrasDict");
        p::Ref extra(PyObject_GetItem(extras.get(), p::text("shoot").get()));
        // An already-running native extra owns callbacks and must be retired before the next shot.
        method(extra.get(), "stopFor", entity);
        method(extra.get(), "startFor", entity, burst_count);
        PyObject* previous = PyObject_GetAttrString(battle, "_replay_muzzle_count");
        if (!previous) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) return nullptr;
            PyErr_Clear(); previous = integer(0).release();
        }
        p::Ref held(previous);
        p::Ref count(PyNumber_Add(held.get(), integer(1).get()));
        setAttr(battle, "_replay_muzzle_count", count.get());
        auto shown = p::Ref(PyObject_GetAttrString(battle, "_replay_muzzle_count"));
        const bool early = PyObject_RichCompareBool(shown.get(), integer(3).get(), Py_LE) == 1;
        p::Ref remainder(PyNumber_Remainder(attr(battle, "_replay_muzzle_count").get(), integer(50).get()));
        if (early || equal(remainder.get(), integer(0).get())) {
            p::Ref values(PyTuple_Pack(2, attr(battle, "_replay_muzzle_count").get(), burst_count));
            p::Ref text(PyString_Format(p::text("REPLAY_SOUND muzzle stop_start=True shown=%d burst=%d").get(), values.get()));
            method(module("gui.mods.offline_lan_0922.offline_replay").get(), "log", text.get());
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// Fence an epoch change without manufacturing terminal feedback.
PyObject* resetProjectileVisuals(PyObject*, PyObject* args) {
    PyObject* battle;
    if (!PyArg_ParseTuple(args, "O", &battle)) return nullptr;
    try {
        setAttr(battle, "_projectile_visual_meta", p::dict().get());
        p::Ref reset = p::retain(Py_None);
        if (!callableAttr(attr(battle, "_remote_factory").get(), "reset_projectile_visuals", reset)) Py_RETURN_NONE;
        bool complete;
        try {
            complete = truthy(p::Ref(PyObject_CallObject(reset.get(), nullptr)));
        } catch (const std::exception& e) {
            warnCaught(battle, "projectile visual epoch reset", e, true);
            Py_RETURN_NONE;
        }
        if (!complete) {
            p::Ref error(PyObject_CallFunction(PyExc_RuntimeError, const_cast<char*>("s"), "presenter retained old-epoch visual owners"));
            callKw(battle, "_warn_optional_failure", {p::text("projectile visual epoch reset").get(), error.get()}, {{"disable", Py_False}});
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* reportPlayerShotTiming(PyObject*, PyObject* args) {
    PyObject *battle, *meta;
    if (!PyArg_ParseTuple(args, "OO", &battle, &meta)) return nullptr;
    try {
        const int other = PyObject_RichCompareBool(method(meta, "get", p::text("shooter_kind").get()).get(), p::text("player").get(), Py_NE);
        if (other < 0) return nullptr;
        if (other) Py_RETURN_FALSE;
        try {
            auto get = [&](const char* name) { return method(meta, "get", p::text(name).get()); };
            p::Ref values(PyTuple_Pack(5, get("projectile_id").get(), get("shooter_id").get(), get("fire_intent_seq").get(),
                                       get("fire_input_seq").get(), toInt(method(meta, "get", p::text("launch_server_time_ms").get(), integer(0).get()).get()).get()));
            writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] PLAYER SHOT TIMING id=%s player=%s "
                                                       "intent=%s input=%s launch_ms=%d\n").get(), values.get())).get());
        } catch (const std::exception&) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            PyErr_Clear(); Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// Python `a <op> b` for two objects, raising on a failed comparison.
bool compare(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("comparison failed");
    return result != 0;
}
bool compareInt(PyObject* a, long b, int op) { return compare(a, integer(b).get(), op); }
// tuple(float(value) for value in sequence)
p::Ref floats(PyObject* sequence, std::vector<double>& out) {
    p::Ref iterator(PyObject_GetIter(sequence));
    out.clear();
    while (PyObject* raw = PyIter_Next(iterator.get())) {
        p::Ref item(raw);
        p::Ref value(PyNumber_Float(item.get()));
        out.push_back(PyFloat_AS_DOUBLE(value.get()));
    }
    if (PyErr_Occurred()) throw p::Error("float sequence iteration failed");
    p::Ref tuple(PyTuple_New(Py_ssize_t(out.size())));
    for (std::size_t i = 0; i < out.size(); ++i) PyTuple_SET_ITEM(tuple.get(), i, PyFloat_FromDouble(out[i]));
    return tuple;
}
// float(value) as a Python object, including NaN and infinity.
p::Ref pyFloat(double value) { return p::Ref(PyFloat_FromDouble(value)); }
double toFloat(PyObject* value) { p::Ref f(PyNumber_Float(value)); return PyFloat_AS_DOUBLE(f.get()); }
bool conversionError() {
    return PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError) ||
           PyErr_ExceptionMatches(PyExc_OverflowError);
}
p::Ref item(PyObject* mapping, const char* name) { return p::Ref(PyObject_GetItem(mapping, p::text(name).get())); }

// BattleRuntime._projectile_wire_meta (staticmethod): normalize the canonical-event and snapshot launch spellings.
p::Ref projectileWireMeta(PyObject* raw, PyObject* g) {
    if (!PyDict_Check(raw)) return p::retain(Py_None);
    auto get = [&](const char* name, PyObject* fallback = Py_None) { return method(raw, "get", p::text(name).get(), fallback); };
    auto maximum_raw = get("max_distance", get("maxDistance").get());
    auto shooter_kind = get("shooter_kind");
    auto shooter_id = get("shooter_id");
    if (shooter_kind.get() == Py_None) {
        if (get("attacker_bot").get() != Py_None) { replace(shooter_kind, p::text("bot")); replace(shooter_id, get("attacker_bot")); }
        else if (get("attacker").get() != Py_None) { replace(shooter_kind, p::text("player")); replace(shooter_id, get("attacker")); }
    }
    static const char* const required[] = {
        "projectile_id", "source_vehicle", "shot_seq", "burst_group_seq", "burst_index", "burst_count", "shell_index", "origin",
        "velocity", "gravity", "max_time_ms", "is_he", "splash_radius", "penetration_factor", "launch_server_time_ms",
        "source_shot", "range_origin", "segment_origin", "segment_velocity", "segment_start_time_ms", "ricochet_count",
        "base_penetration_multiplier"};
    if (maximum_raw.get() == Py_None || shooter_kind.get() == Py_None || shooter_id.get() == Py_None) return p::retain(Py_None);
    for (const char* name : required) if (!member(raw, p::text(name).get())) return p::retain(Py_None);
    auto protocol = global(g, "lan_protocol");
    std::vector<double> origin, velocity, range_origin, segment_origin, segment_velocity;
    p::Ref origin_t = p::retain(Py_None), velocity_t = p::retain(Py_None), range_t = p::retain(Py_None),
           segment_origin_t = p::retain(Py_None), segment_velocity_t = p::retain(Py_None), max_time = p::retain(Py_None),
           projectile_id = p::retain(Py_None), source_vehicle = p::retain(Py_None), shooter = p::retain(Py_None),
           shot_seq = p::retain(Py_None), burst_group = p::retain(Py_None), shell_index = p::retain(Py_None),
           launch_time = p::retain(Py_None), segment_start = p::retain(Py_None), ricochets = p::retain(Py_None),
           source_shot = p::retain(Py_None);
    double gravity, maximum, base_multiplier, splash_radius, penetration_factor;
    try {
        replace(origin_t, floats(item(raw, "origin").get(), origin));
        replace(velocity_t, floats(item(raw, "velocity").get(), velocity));
        replace(range_t, floats(item(raw, "range_origin").get(), range_origin));
        replace(segment_origin_t, floats(item(raw, "segment_origin").get(), segment_origin));
        replace(segment_velocity_t, floats(item(raw, "segment_velocity").get(), segment_velocity));
        if (origin.size() != 3 || velocity.size() != 3 || range_origin.size() != 3 || segment_origin.size() != 3 ||
            segment_velocity.size() != 3) return p::retain(Py_None);
        gravity = toFloat(item(raw, "gravity").get());
        maximum = toFloat(maximum_raw.get());
        replace(max_time, toInt(item(raw, "max_time_ms").get()));
        replace(projectile_id, str(item(raw, "projectile_id").get()));
        replace(source_vehicle, str(item(raw, "source_vehicle").get()));
        replace(shooter_kind, str(shooter_kind.get()));
        replace(shooter, toInt(shooter_id.get()));
        replace(shot_seq, toInt(item(raw, "shot_seq").get()));
        replace(burst_group, method(protocol.get(), "_strict_burst_group", shot_seq.get(), item(raw, "burst_group_seq").get(),
                                    item(raw, "burst_index").get(), item(raw, "burst_count").get()));
        replace(shell_index, toInt(item(raw, "shell_index").get()));
        replace(launch_time, toInt(item(raw, "launch_server_time_ms").get()));
        replace(segment_start, toInt(item(raw, "segment_start_time_ms").get()));
        replace(ricochets, toInt(item(raw, "ricochet_count").get()));
        base_multiplier = toFloat(item(raw, "base_penetration_multiplier").get());
        splash_radius = toFloat(item(raw, "splash_radius").get());
        penetration_factor = toFloat(item(raw, "penetration_factor").get());
        replace(source_shot, method(protocol.get(), "_strict_projectile_source_shot", item(raw, "source_shot").get()));
    } catch (const std::exception&) {
        if (!PyErr_Occurred() || !conversionError()) throw;
        PyErr_Clear();
        return p::retain(Py_None);
    }
    std::vector<double> values;
    for (const auto* part : {&origin, &velocity, &range_origin, &segment_origin, &segment_velocity})
        values.insert(values.end(), part->begin(), part->end());
    for (double value : {gravity, maximum, splash_radius, penetration_factor, base_multiplier}) values.push_back(value);
    auto is_he = item(raw, "is_he");
    auto max_allowed = global(g, "PROJECTILE_MAX_TIME_MS");
    const bool rejected =
        !PyObject_IsTrue(projectile_id.get()) || !PyObject_IsTrue(source_vehicle.get()) ||
        PyObject_Size(source_vehicle.get()) > 128 || !textIn(shooter_kind.get(), {"player", "bot"}) ||
        compareInt(shooter.get(), 0, Py_LE) || compareInt(shot_seq.get(), 0, Py_LE) || burst_group.get() == Py_None ||
        compareInt(shell_index.get(), 0, Py_LT) || compareInt(shell_index.get(), 9, Py_GT) ||
        gravity <= 0. || maximum <= 0. ||
        compareInt(max_time.get(), 0, Py_LE) || compare(max_time.get(), max_allowed.get(), Py_GT) ||
        compareInt(launch_time.get(), 0, Py_LT) || compareInt(segment_start.get(), 0, Py_LT) ||
        compare(segment_start.get(), max_time.get(), Py_GT) ||
        (compareInt(ricochets.get(), 1, Py_EQ) && compare(segment_start.get(), max_time.get(), Py_GE)) ||
        !(compareInt(ricochets.get(), 0, Py_EQ) || compareInt(ricochets.get(), 1, Py_EQ)) ||
        splash_radius < 0. || penetration_factor < 0. || !PyBool_Check(is_he.get()) ||
        !truthy(method(protocol.get(), "_projectile_source_shot_matches_launch", source_shot.get(), velocity_t.get(),
                       pyFloat(gravity).get(), pyFloat(maximum).get(), is_he.get(), pyFloat(splash_radius).get())) ||
        std::any_of(values.begin(), values.end(), [](double v) { return std::isnan(v) || std::isinf(v); });
    if (rejected) return p::retain(Py_None);
    auto shell = method(source_shot.get(), "get", p::text("shell").get());
    auto kind = method(truthy(shell) ? shell.get() : p::dict().get(), "get", p::text("kind").get());
    const bool first_segment = compareInt(ricochets.get(), 0, Py_EQ);
    auto expected = first_segment ? pyFloat(1.) :
        method(global(g, "combat_rules").get(), "first_ricochet_penetration_multiplier", kind.get());
    if (expected.get() == Py_None || compare(pyFloat(base_multiplier).get(), expected.get(), Py_NE) ||
        (first_segment && (compareInt(segment_start.get(), 0, Py_NE) || segment_origin != origin || segment_velocity != velocity)))
        return p::retain(Py_None);
    p::Ref checked_ms = p::retain(Py_None);
    double checked_distance, piercing_loss;
    try {
        auto raw_checked = get("checked_through_ms", integer(0).get());
        replace(checked_ms, toInt(truthy(raw_checked) ? raw_checked.get() : integer(0).get()));
        checked_distance = toFloat(get("checked_distance", pyFloat(0.).get()).get());
        piercing_loss = toFloat(get("piercing_loss", pyFloat(0.).get()).get());
    } catch (const std::exception&) {
        if (!PyErr_Occurred() || !conversionError()) throw;
        PyErr_Clear();
        return p::retain(Py_None);
    }
    if (!(compare(segment_start.get(), checked_ms.get(), Py_LE) && compare(checked_ms.get(), max_time.get(), Py_LE)) ||
        !(0. <= checked_distance && checked_distance <= maximum + 0.1) || piercing_loss < 0. ||
        std::isnan(checked_distance) || std::isinf(checked_distance) || std::isnan(piercing_loss) || std::isinf(piercing_loss))
        return p::retain(Py_None);
    auto result = literal(28);
    put(result.get(), "projectile_id", p::retain(projectile_id.get()));
    put(result.get(), "shooter_kind", p::retain(shooter_kind.get()));
    put(result.get(), "shooter_id", p::retain(shooter.get()));
    put(result.get(), "source_vehicle", p::retain(source_vehicle.get()));
    put(result.get(), "source_shot", p::retain(source_shot.get()));
    put(result.get(), "shot_seq", p::retain(shot_seq.get()));
    for (int i = 0; i < 3; ++i) {
        static const char* const names[] = {"burst_group_seq", "burst_index", "burst_count"};
        put(result.get(), names[i], p::Ref(PySequence_GetItem(burst_group.get(), i)));
    }
    put(result.get(), "shell_index", p::retain(shell_index.get()));
    put(result.get(), "origin", p::retain(origin_t.get()));
    put(result.get(), "velocity", p::retain(velocity_t.get()));
    put(result.get(), "range_origin", p::retain(range_t.get()));
    put(result.get(), "segment_origin", p::retain(segment_origin_t.get()));
    put(result.get(), "segment_velocity", p::retain(segment_velocity_t.get()));
    put(result.get(), "segment_start_time_ms", p::retain(segment_start.get()));
    put(result.get(), "ricochet_count", p::retain(ricochets.get()));
    put(result.get(), "base_penetration_multiplier", pyFloat(base_multiplier));
    put(result.get(), "gravity", pyFloat(gravity));
    put(result.get(), "max_distance", pyFloat(maximum));
    put(result.get(), "max_time_ms", p::retain(max_time.get()));
    put(result.get(), "is_he", p::boolean(truthy(is_he)));
    put(result.get(), "splash_radius", pyFloat(splash_radius));
    put(result.get(), "penetration_factor", pyFloat(penetration_factor));
    put(result.get(), "launch_server_time_ms", p::retain(launch_time.get()));
    put(result.get(), "base_checked_ms", p::retain(checked_ms.get()));
    put(result.get(), "checked_distance", pyFloat(checked_distance));
    put(result.get(), "piercing_loss", pyFloat(piercing_loss));
    if (equal(shooter_kind.get(), p::text("player").get())) {
        p::Ref intent = p::retain(Py_None), input = p::retain(Py_None);
        try {
            replace(intent, toInt(item(raw, "fire_intent_seq").get()));
            replace(input, toInt(item(raw, "fire_input_seq").get()));
        } catch (const std::exception&) {
            if (!PyErr_Occurred() || !(conversionError() || PyErr_ExceptionMatches(PyExc_KeyError))) throw;
            PyErr_Clear();
            return p::retain(Py_None);
        }
        if (compareInt(intent.get(), 0, Py_LE) || compareInt(input.get(), 0, Py_LE)) return p::retain(Py_None);
        put(result.get(), "fire_intent_seq", std::move(intent));
        put(result.get(), "fire_input_seq", std::move(input));
    } else if (member(raw, p::text("fire_intent_seq").get()) || member(raw, p::text("fire_input_seq").get())) {
        return p::retain(Py_None);
    }
    if (get("bot_presentation_time_us").get() != Py_None) {
        auto stamp = item(raw, "bot_presentation_time_us");
        const int typed = PyObject_IsInstance(stamp.get(), global(g, "_INTEGER_TYPES").get());
        if (typed < 0) throw p::Error("integer type check failed");
        if (!equal(shooter_kind.get(), p::text("bot").get()) || PyBool_Check(stamp.get()) || !typed || compareInt(stamp.get(), 0, Py_LT))
            return p::retain(Py_None);
        put(result.get(), "bot_presentation_time_us", std::move(stamp));
    }
    return result;
}
PyObject* projectileWireMetaPort(PyObject*, PyObject* args) {
    PyObject *raw, *g;
    if (!PyArg_ParseTuple(args, "OO", &raw, &g)) return nullptr;
    try { return projectileWireMeta(raw, g).release(); } catch (const std::exception& e) { return fail(e); }
}

p::Ref callGlobal(PyObject* g, const char* name, std::initializer_list<PyObject*> args) {
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    return p::Ref(PyObject_Call(global(g, name).get(), tuple.get(), nullptr));
}
// int(state.get('siege_state', 0) or 0)
p::Ref siegeState(PyObject* state) {
    auto raw = method(state, "get", p::text("siege_state").get(), integer(0).get());
    return toInt(truthy(raw) ? raw.get() : integer(0).get());
}
// BattleRuntime._projectile_plain_pose (staticmethod): copy a pose whose numeric fields were canonicalized.
p::Ref plainPose(PyObject* position, PyObject* state_argument, PyObject* g) {
    auto state = PyDict_Check(state_argument) ? p::retain(state_argument) : p::dict();
    auto yaw = method(state.get(), "get", p::text("yaw").get(), p::number(0.).get());
    auto aim_yaw = method(state.get(), "get", p::text("aim_yaw").get(), yaw.get());
    auto turret_default = callGlobal(g, "_angle_delta", {yaw.get(), aim_yaw.get()});
    auto turret_yaw = method(state.get(), "get", p::text("turret_yaw").get(), turret_default.get());
    auto pose = literal(9);
    for (int i = 0; i < 3; ++i) {
        static const char* const axes[] = {"x", "y", "z"};
        put(pose.get(), axes[i], p::Ref(PyObject_GetItem(position, integer(i).get())));
    }
    put(pose.get(), "yaw", p::retain(yaw.get()));
    put(pose.get(), "pitch", method(state.get(), "get", p::text("pitch").get(), p::number(0.).get()));
    put(pose.get(), "roll", method(state.get(), "get", p::text("roll").get(), p::number(0.).get()));
    put(pose.get(), "turret_yaw", std::move(turret_yaw));
    put(pose.get(), "gun_pitch", method(state.get(), "get", p::text("gun_pitch").get(), p::number(0.).get()));
    put(pose.get(), "siege_state", siegeState(state.get()));
    return pose;
}
// BattleRuntime._projectile_boundary_pose (staticmethod): canonicalize one network/native pose.
p::Ref boundaryPose(PyObject* position, PyObject* state_argument, PyObject* g) {
    auto state = PyDict_Check(state_argument) ? p::retain(state_argument) : p::dict();
    auto number = [&](const char* name, PyObject* fallback) {
        auto value = method(state.get(), "get", p::text(name).get());
        return fallback ? callGlobal(g, "_number", {value.get(), fallback}) : callGlobal(g, "_number", {value.get()});
    };
    auto yaw = number("yaw", nullptr);
    auto aim_yaw = number("aim_yaw", yaw.get());
    auto canonical = literal(7);
    put(canonical.get(), "yaw", p::retain(yaw.get()));
    put(canonical.get(), "pitch", number("pitch", nullptr));
    put(canonical.get(), "roll", number("roll", nullptr));
    put(canonical.get(), "aim_yaw", p::retain(aim_yaw.get()));
    auto turret_value = method(state.get(), "get", p::text("turret_yaw").get());
    auto delta = callGlobal(g, "_angle_delta", {yaw.get(), aim_yaw.get()});
    put(canonical.get(), "turret_yaw", callGlobal(g, "_number", {turret_value.get(), delta.get()}));
    put(canonical.get(), "gun_pitch", number("gun_pitch", nullptr));
    put(canonical.get(), "siege_state", siegeState(state.get()));
    p::Ref xyz(PyTuple_New(3));
    for (int i = 0; i < 3; ++i)
        PyTuple_SET_ITEM(xyz.get(), i, callGlobal(g, "_number", {p::Ref(PyObject_GetItem(position, integer(i).get())).get()}).release());
    return plainPose(xyz.get(), canonical.get(), g);
}
PyObject* plainPosePort(PyObject*, PyObject* args) {
    PyObject *position, *state, *g;
    if (!PyArg_ParseTuple(args, "OOO", &position, &state, &g)) return nullptr;
    try { return plainPose(position, state, g).release(); } catch (const std::exception& e) { return fail(e); }
}
PyObject* boundaryPosePort(PyObject*, PyObject* args) {
    PyObject *position, *state, *g;
    if (!PyArg_ParseTuple(args, "OOO", &position, &state, &g)) return nullptr;
    try { return boundaryPose(position, state, g).release(); } catch (const std::exception& e) { return fail(e); }
}
// BattleRuntime._projectile_visual_age (staticmethod): the collision-confirmed age of one visual segment.
double visualAge(PyObject* raw, PyObject* g) {
        if (!PyDict_Check(raw)) return 0.;
        auto start = method(raw, "get", p::text("segment_start_time_ms").get(), integer(0).get());
        auto through_default = method(raw, "get", p::text("checked_through_ms").get(), start.get());
        auto through = method(raw, "get", p::text("base_checked_ms").get(), through_default.get());
        auto max_time = method(raw, "get", p::text("max_time_ms").get(), global(g, "PROJECTILE_MAX_TIME_MS").get());
        double maximum, age;
        try {
            auto start_int = toInt(start.get());
            auto through_int = toInt(through.get());
            p::Ref span(PyNumber_Subtract(toInt(max_time.get()).get(), start_int.get()));
            maximum = std::max(0., toFloat(span.get()) / 1000.);
            p::Ref checked(PyNumber_Subtract(through_int.get(), start_int.get()));
            age = toFloat(checked.get()) / 1000.;
        } catch (const std::exception&) {
            if (!PyErr_Occurred() || !conversionError()) throw;
            PyErr_Clear();
            return 0.;
        }
        // max(0.0, min(maximum, age)) keeps Python's first-argument ties.
        const double bounded = age < maximum ? age : maximum;
        return bounded > 0. ? bounded : 0.;
}
PyObject* projectileVisualAge(PyObject*, PyObject* args) {
    PyObject *raw, *g;
    if (!PyArg_ParseTuple(args, "OO", &raw, &g)) return nullptr;
    try { return PyFloat_FromDouble(visualAge(raw, g)); } catch (const std::exception& e) { return fail(e); }
}

PyObject* projectileRecordPositions(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto result = p::dict();
        const bool worker = truthy(attr(battle, "_worker_mode"));
        p::Ref items(PySequence_Tuple(method(attr(battle, "_records").get(), "items").get()));
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(items.get()); ++i) {
            PyObject* pair = PyTuple_GET_ITEM(items.get(), i);
            PyObject *key = PyTuple_GET_ITEM(pair, 0), *record = PyTuple_GET_ITEM(pair, 1);
            if (truthy(method(record, "get", p::text("tombstone").get()))) continue;
            const bool local = truthy(method(record, "get", p::text("local").get()));
            // player:-1 is a private native-space carrier, never a projectile target.
            if (worker && local) continue;
            auto entity = method(battle, "_server_entity", method(record, "get", p::text("engine_id").get()).get());
            if (entity.get() == Py_None || !truthy(optionalAttr(entity.get(), "isStarted"))) continue;
            if (truthy(method(record, "get", p::text("local").get()))) {
                if (PyObject_SetItem(result.get(), key, p::Ref(PySequence_Tuple(attr(battle, "_local_position").get())).get()) < 0)
                    throw p::Error("position store failed");
            } else {
                auto fallback = method(record, "get", p::text("state").get(), p::dict().get());
                PyObject* position = PyObject_GetAttrString(entity.get(), "position");
                if (!position) {
                    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error("entity position failed");
                    PyErr_Clear(); position = fallback.release();
                }
                p::Ref held(position);
                if (PyObject_SetItem(result.get(), key, callGlobal(g, "_xyz", {held.get()}).get()) < 0) throw p::Error("position store failed");
            }
        }
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// Body/chassis poses matching the projectile timeline (unblended worker matrices where available).
PyObject* projectileVehicleMatrices(PyObject*, PyObject* args) {
    PyObject *battle, *record, *vehicle, *ground = Py_None;
    if (!PyArg_ParseTuple(args, "OOO|O", &battle, &record, &vehicle, &ground)) return nullptr;
    try {
        auto canonical = ground == Py_None ? attr(vehicle, "matrix") : p::retain(ground);
        auto factory = attr(battle, "_remote_factory");
        if (truthy(attr(battle, "_worker_mode")) && truthy(method(record, "get", p::text("native_remote").get())) &&
            factory.get() != Py_None) {
            p::Ref getter = p::retain(Py_None);
            if (callableAttr(factory.get(), "projectile_collision_matrices", getter)) {
                p::Ref matrices(PyObject_CallFunctionObjArgs(getter.get(), method(record, "get", p::text("engine_id").get()).get(), ground, nullptr));
                if (matrices.get() != Py_None) return matrices.release();
            }
            if (callableAttr(factory.get(), "projectile_collision_matrix", getter)) {
                p::Ref body(PyObject_CallFunctionObjArgs(getter.get(), method(record, "get", p::text("engine_id").get()).get(), nullptr));
                if (body.get() != Py_None) return PyTuple_Pack(2, body.get(), canonical.get());
            }
        }
        return PyTuple_Pack(2, canonical.get(), canonical.get());
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* projectileDescriptorAtPose(PyObject*, PyObject* args) {
    PyObject *battle, *target, *pose;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &target, &pose)) return nullptr;
    try {
        auto descriptor = optionalAttr(target, "typeDescriptor");
        if (descriptor.get() == Py_None || !truthy(optionalAttr(descriptor.get(), "hasSiegeMode"))) return descriptor.release();
        auto plain = optionalAttr(descriptor.get(), "defaultVehicleDescr");
        auto siege = optionalAttr(descriptor.get(), "siegeVehicleDescr");
        if (plain.get() == Py_None || siege.get() == Py_None) return descriptor.release();
        auto states = optionalAttr(optionalAttr(attr(battle, "_runtime").get(), "constants").get(), "VEHICLE_SIEGE_STATE");
        PyObject* enabled = PyObject_GetAttrString(states.get(), "ENABLED");
        if (!enabled) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError)) return nullptr;
            PyErr_Clear(); enabled = integer(2).release();
        }
        p::Ref held(enabled);
        auto state = toInt(method(pose, "get", p::text("siege_state").get(), integer(0).get()).get());
        return (equal(state.get(), held.get()) ? siege : plain).release();
    } catch (const std::exception& e) { return fail(e); }
}

// One bounded diagnostic for a projectile-local failure; diagnostics are best effort.
PyObject* reportProjectileTerminalFailure(PyObject*, PyObject* args) {
    PyObject *battle, *meta, *state, *stage, *reason, *error = Py_None, *g;
    if (!PyArg_ParseTuple(args, "OOOOOOO", &battle, &meta, &state, &stage, &reason, &error, &g)) return nullptr;
    try {
        try {
            p::Ref signature(PyTuple_Pack(2, str(stage).get(), str(reason).get()));
            p::Ref reported = method(meta, "setdefault", p::text("terminal_failures_reported").get(), p::Ref(PySet_New(nullptr)).get());
            if (member(reported.get(), signature.get())) Py_RETURN_FALSE;
            method(reported.get(), "add", signature.get());
            auto boundary = method(meta, "get", p::text("terminal_failure_boundary").get());
            p::Ref detail = p::retain(Py_None);
            try {
                const int text = PyObject_IsInstance(error, global(g, "_STRING_TYPES").get());
                if (text < 0) throw p::Error("string type check failed");
                replace(detail, text ? p::retain(error) : error != Py_None ? p::Ref(PyObject_Repr(error)) : p::text("none"));
            } catch (const std::exception&) {
                if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                PyErr_Clear();
                replace(detail, p::text("<unprintable>"));
            }
            auto cleaned = method(method(detail.get(), "replace", p::text("\r").get(), p::text(" ").get()).get(), "replace",
                                  p::text("\n").get(), p::text(" ").get());
            replace(detail, p::Ref(PySequence_GetSlice(cleaned.get(), 0, 160)));
            auto elapsed = callGlobal(g, "_number", {method(state, "get", p::text("elapsed").get()).get()});
            p::Ref positive_elapsed = compare(elapsed.get(), p::number(0.).get(), Py_GT) ? p::retain(elapsed.get()) : p::number(0.);
            p::Ref scaled(PyNumber_Multiply(positive_elapsed.get(), p::number(1000.).get()));
            auto rounded = method(module("__builtin__").get(), "round", scaled.get());
            p::Ref values(PyTuple_Pack(6, method(meta, "get", p::text("projectile_id").get()).get(), stage, reason, boundary.get(),
                                       toInt(rounded.get()).get(), detail.get()));
            writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] PROJECTILE FAILURE id=%s stage=%s reason=%s boundary=%s "
                                                       "elapsed_ms=%d error=%s\n").get(), values.get())).get());
        } catch (const std::exception&) {
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            PyErr_Clear();
            Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// Ack only a server-admitted Bot launch, preserving Bot order.
PyObject* confirmBotProjectileLaunch(PyObject*, PyObject* args) {
    PyObject *battle, *normalized, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &normalized, &g)) return nullptr;
    try {
        auto bots = attr(battle, "_bots");
        if (!PyDict_Check(normalized) ||
            compare(method(normalized, "get", p::text("shooter_kind").get()).get(), p::text("bot").get(), Py_NE) ||
            bots.get() == Py_None || !truthy(method(bots.get(), "is_authority")))
            Py_RETURN_FALSE;
        p::Ref bot_id = p::retain(Py_None), shot_seq = p::retain(Py_None);
        try {
            replace(bot_id, toInt(p::Ref(PyObject_GetItem(normalized, p::text("shooter_id").get())).get()));
            replace(shot_seq, toInt(p::Ref(PyObject_GetItem(normalized, p::text("shot_seq").get())).get()));
        } catch (const std::exception&) {
            if (!PyErr_Occurred() || !(conversionError() || PyErr_ExceptionMatches(PyExc_KeyError))) throw;
            PyErr_Clear();
            raise("canonical bot projectile identity is invalid");
        }
        auto seen = attr(battle, "_bot_fire_seen");
        auto confirmed = method(seen.get(), "get", bot_id.get(), integer(0).get());
        auto previous = p::retain(confirmed.get());
        if (compare(shot_seq.get(), confirmed.get(), Py_LE)) Py_RETURN_TRUE;
        p::Ref timeline_key(PyTuple_Pack(3, p::text("bot_publish").get(), bot_id.get(), shot_seq.get()));
        auto timeline = method(attr(battle, "_fire_timeline").get(), "get", timeline_key.get());
        if (timeline.get() != Py_None && !truthy(method(timeline.get(), "get", p::text("echo_logged").get()))) {
            put(timeline.get(), "echo_logged", p::retain(Py_True));
            p::Ref clock(PyObject_CallObject(global(g, "_PROFILE_CLOCK").get(), nullptr));
            p::Ref waited(PyNumber_Subtract(clock.get(), p::Ref(PyObject_GetItem(timeline.get(), p::text("wall").get())).get()));
            p::Ref positive_wait = compare(waited.get(), p::number(0.).get(), Py_GT) ? p::retain(waited.get()) : p::number(0.);
            p::Ref values(PyTuple_Pack(3, bot_id.get(), shot_seq.get(), p::Ref(PyNumber_Multiply(positive_wait.get(), p::number(1000.).get())).get()));
            writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] BOT SHOT TIMING stage=echo bot=%d seq=%d echo_wait_ms=%.3f\n").get(),
                                               values.get())).get());
        }
        auto pending = method(attr(battle, "_bot_fire_confirmations").get(), "setdefault", bot_id.get(), p::Ref(PySet_New(nullptr)).get());
        method(pending.get(), "add", shot_seq.get());
        p::Ref acknowledge = p::retain(Py_None);
        if (!callableAttr(bots.get(), "ack_projectile_launch", acknowledge)) raise("bot projectile outbox acknowledgement is unavailable");
        auto payloads = attr(battle, "_bot_launch_payloads");
        while (true) {
            p::Ref next(PyNumber_Add(confirmed.get(), integer(1).get()));
            if (!member(pending.get(), next.get())) break;
            if (!truthy(p::Ref(PyObject_CallFunctionObjArgs(acknowledge.get(), bot_id.get(), next.get(), nullptr))))
                raise("canonical bot projectile acknowledgement is out of order");
            method(pending.get(), "remove", next.get());
            p::Ref payload_key(PyTuple_Pack(2, bot_id.get(), next.get()));
            method(payloads.get(), "pop", payload_key.get(), Py_None);
            replace(confirmed, std::move(next));
        }
        if (compare(confirmed.get(), previous.get(), Py_GT) && PyObject_SetItem(seen.get(), bot_id.get(), confirmed.get()) < 0)
            throw p::Error("confirmed sequence store failed");
        if (PyObject_Size(pending.get()) == 0) method(attr(battle, "_bot_fire_confirmations").get(), "pop", bot_id.get(), Py_None);
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// #1513's current on-screen HP_gunFire visual start, or None for the canonical launch point.
PyObject* projectileVisualStart(PyObject*, PyObject* args) {
    PyObject *battle, *entity, *normalized, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &entity, &normalized, &g)) return nullptr;
    try {
        if (entity == Py_None || !truthy(optionalAttr(entity, "isStarted")) || !PyDict_Check(normalized) ||
            compare(method(normalized, "get", p::text("ricochet_count").get()).get(), integer(0).get(), Py_NE) ||
            visualAge(normalized, g) > 0.)
            Py_RETURN_NONE;
        auto runtime = attr(battle, "_runtime");
        p::Ref on_screen = p::retain(Py_None);
        if (!callableAttr(optionalAttr(runtime.get(), "cameras").get(), "isPointOnScreen", on_screen)) Py_RETURN_NONE;
        try {
            auto compound = attr(attr(entity, "appearance").get(), "compoundModel");
            auto node = method(compound.get(), "node", p::text("HP_gunFire").get());
            auto matrix = method(attr(runtime.get(), "math").get(), "Matrix", node.get());
            auto native_point = attr(matrix.get(), "translation");
            double point[3];
            for (int i = 0; i < 3; ++i) point[i] = toFloat(p::Ref(PyObject_GetItem(native_point.get(), integer(i).get())).get());
            for (double value : point) if (std::isnan(value) || std::isinf(value)) Py_RETURN_NONE;
            if (!truthy(p::Ref(PyObject_CallFunctionObjArgs(on_screen.get(), native_point.get(), nullptr)))) Py_RETURN_NONE;
            std::vector<double> reference;
            floats(p::Ref(PyObject_GetItem(normalized, p::text("segment_origin").get())).get(), reference);
            // sum((point[i] - reference[i]) ** 2 for i in range(3)); reference must hold three values.
            double offset = 0.;
            for (std::size_t i = 0; i < 3; ++i) offset += std::pow(point[i] - reference.at(i), 2.);
            p::Ref limit_sq(PyNumber_Power(global(g, "PROJECTILE_VISUAL_START_MAX_DIFF").get(), integer(2).get(), Py_None));
            // Mirror ProjectileMover.add so the frontier bridge records the start the presenter accepts.
            if (compare(pyFloat(offset).get(), limit_sq.get(), Py_GT)) Py_RETURN_NONE;
            return Py_BuildValue("(ddd)", point[0], point[1], point[2]);
        } catch (const std::exception&) {
            // The stock visual start is optional; fall back to the immutable canonical launch point.
            if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
            PyErr_Clear();
            Py_RETURN_NONE;
        }
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* setProjectileEpoch(PyObject*, PyObject* args) {
    PyObject *battle, *value, *now, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &value, &now, &g)) return nullptr;
    try {
        PyObject* converted = PyNumber_Int(value);
        if (!converted) {
            if (!conversionError()) return nullptr;
            PyErr_Clear(); Py_RETURN_FALSE;
        }
        p::Ref epoch(converted);
        if (PyBool_Check(value) || compareInt(epoch.get(), 0, Py_LT)) Py_RETURN_FALSE;
        if (equal(attr(battle, "_projectile_epoch").get(), epoch.get())) Py_RETURN_TRUE;
        // An authority epoch fences presentation as well as simulation; old tracers retire without terminal feedback.
        dispatch(battle, "_reset_projectile_visuals", "reset_projectile_visuals", {});
        auto recent = global(g, "_RecentIdSet");
        setAttr(battle, "_projectile_visual_terminals", p::Ref(PyObject_CallObject(recent.get(), nullptr)).get());
        setAttr(battle, "_projectile_resolution_acks", p::Ref(PyObject_CallObject(recent.get(), nullptr)).get());
        // Deferred local muzzle callbacks use this identity map as their ownership fence.
        method(attr(battle, "_local_projectile_presentations").get(), "clear");
        setAttr(battle, "_local_projectile_presented", p::Ref(PyObject_CallObject(recent.get(), nullptr)).get());
        setAttr(battle, "_projectile_epoch", epoch.get());
        setAttr(battle, "_projectile_meta", p::dict().get());
        setAttr(battle, "_projectile_terminal_data", p::dict().get());
        setAttr(battle, "_projectile_scene_stop_reasons", p::dict().get());
        auto projectiles = attr(battle, "_projectiles");
        if (projectiles.get() != Py_None) {
            auto current = pyFloat(toFloat(now));
            auto owner_now = attr(projectiles.get(), "now");
            auto reset_at = compare(owner_now.get(), current.get(), Py_GT) ? std::move(owner_now) : std::move(current);
            if (!truthy(method(projectiles.get(), "reset", reset_at.get()))) raise("projectile authority reset failed");
        }
        setAttr(battle, "_next_projectile_progress_time", pyFloat(toFloat(now)).get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// Freeze one coherent hull, aim and mode sample for collision.
PyObject* projectileRecordPose(PyObject*, PyObject* args) {
    PyObject *battle, *key, *record, *position, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &key, &record, &position, &g)) return nullptr;
    try {
        auto state = method(record, "get", p::text("state").get());
        if (!truthy(state)) replace(state, p::dict());
        auto source = method(record, "get", p::text("projectile_collision_pose").get());
        auto all_axes = [](PyObject* mapping) {
            for (const char* axis : {"x", "y", "z"}) if (!member(mapping, p::text(axis).get())) return false;
            return true;
        };
        auto triple = [](PyObject* mapping) {
            return p::Ref(PyTuple_Pack(3, item(mapping, "x").get(), item(mapping, "y").get(), item(mapping, "z").get()));
        };
        p::Ref pose = p::retain(Py_None);
        if (PyDict_Check(source.get())) {
            auto source_position = all_axes(source.get()) ? triple(source.get()) : p::retain(position);
            replace(pose, plainPose(source_position.get(), source.get(), g));
        } else {
            auto source_position = all_axes(state.get()) ? triple(state.get()) : p::retain(position);
            replace(pose, boundaryPose(source_position.get(), state.get(), g));
        }
        auto entity = method(battle, "_server_entity", method(record, "get", p::text("engine_id").get()).get());
        if (entity.get() == Py_None) Py_RETURN_NONE;
        if (truthy(method(record, "get", p::text("local").get()))) {
            auto local = attr(battle, "_local_position");
            auto update = literal(6);
            for (int i = 0; i < 3; ++i) {
                static const char* const axes[] = {"x", "y", "z"};
                put(update.get(), axes[i], p::Ref(PyObject_GetItem(local.get(), integer(i).get())));
            }
            put(update.get(), "yaw", attr(battle, "_local_yaw"));
            put(update.get(), "pitch", attr(battle, "_local_pitch"));
            put(update.get(), "roll", attr(battle, "_local_roll"));
            if (PyDict_Update(pose.get(), update.get()) < 0) throw p::Error("pose update failed");
        }
        auto aim = optionalAttr(entity.get(), "getAimParams");
        if (!PyDict_Check(method(record, "get", p::text("projectile_collision_pose").get()).get()) && PyCallable_Check(aim.get())) {
            try {
                auto pair = unpack(p::Ref(PyObject_CallObject(aim.get(), nullptr)).get(), 2);
                put(pose.get(), "turret_yaw", callGlobal(g, "_number", {pair[0].get()}));
                put(pose.get(), "gun_pitch", callGlobal(g, "_number", {pair[1].get()}));
            } catch (const std::exception&) {
                if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_Exception)) throw;
                PyErr_Clear();
            }
        }
        return pose.release();
    } catch (const std::exception& e) { return fail(e); }
}

// Atomic collision-pose history for projectile catch-up.
PyObject* sampleProjectilePositions(PyObject*, PyObject* args) {
    PyObject *battle, *now, *poses, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &now, &poses, &g)) return nullptr;
    try {
        auto frozen = p::dict();
        auto source = PyObject_IsTrue(poses) > 0 ? p::retain(poses) : p::dict();
        p::Ref iterator(PyObject_GetIter(method(source.get(), "items").get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref pair(raw);
            PyObject *key = PyTuple_GET_ITEM(pair.get(), 0), *pose = PyTuple_GET_ITEM(pair.get(), 1);
            auto copy = PyDict_Check(pose) ? p::Ref(PyObject_CallFunctionObjArgs(reinterpret_cast<PyObject*>(&PyDict_Type), pose, nullptr))
                                           : plainPose(pose, Py_None, g);
            if (PyObject_SetItem(frozen.get(), key, copy.get()) < 0) throw p::Error("pose store failed");
        }
        if (PyErr_Occurred()) throw p::Error("pose iteration failed");
        const double at = toFloat(now);
        p::Ref sample(Py_BuildValue("(dO)", at, frozen.get()));
        auto history = attr(battle, "_projectile_position_history");
        const Py_ssize_t size = PyObject_Size(history.get());
        if (size < 0) throw p::Error("history size failed");
        bool replaced = false;
        if (size) {
            p::Ref last(PySequence_GetItem(history.get(), size - 1));
            const double previous = toFloat(p::Ref(PySequence_GetItem(last.get(), 0)).get());
            if (std::fabs(previous - at) <= 1.0e-9) {
                if (PySequence_SetItem(history.get(), size - 1, sample.get()) < 0) throw p::Error("history replace failed");
                replaced = true;
            }
        }
        if (!replaced) method(history.get(), "append", sample.get());
        // The protocol lifetime plus a margin keeps the left interpolation endpoint during a delayed frame.
        const double floor = at - toFloat(global(g, "PROJECTILE_MAX_TIME_MS").get()) / 1000. - 1.;
        while (PyObject_Size(history.get()) > 2) {
            p::Ref second(PySequence_GetItem(history.get(), 1));
            if (!(toFloat(p::Ref(PySequence_GetItem(second.get(), 0)).get()) < floor)) break;
            method(history.get(), "pop", integer(0).get());
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// One target view whose outer and inner geometry agree.
PyObject* projectileFrozenTarget(PyObject*, PyObject* args) {
    PyObject *battle, *target, *pose, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &target, &pose, &g)) return nullptr;
    try {
        auto descriptor = dispatch(battle, "_projectile_descriptor_at_pose", "projectile_descriptor_at_pose", {target, pose});
        auto math = attr(attr(battle, "_runtime").get(), "math");
        auto factory = optionalAttr(math.get(), "Matrix");
        if (descriptor.get() == Py_None || !PyCallable_Check(factory.get())) Py_RETURN_NONE;
        p::Ref matrix(PyObject_CallObject(factory.get(), nullptr));
        auto angle = [&](const char* name) { return method(pose, "get", p::text(name).get(), p::number(0.).get()); };
        p::Ref ypr(PyTuple_Pack(3, angle("yaw").get(), angle("pitch").get(), angle("roll").get()));
        method(matrix.get(), "setRotateYPR", ypr.get());
        p::Ref xyz(PyTuple_Pack(3, item(pose, "x").get(), item(pose, "y").get(), item(pose, "z").get()));
        auto position = method(battle, "_vector", xyz.get());
        setAttr(matrix.get(), "translation", position.get());
        auto appearance = callGlobal(g, "_ProjectileCollisionAppearance", {math.get(), descriptor.get(), angle("turret_yaw").get(),
                                                                          angle("gun_pitch").get()});
        return callGlobal(g, "_ProjectileCollisionTarget", {target, descriptor.get(), matrix.get(), position.get(), appearance.get(),
                                                             math.get()}).release();
    } catch (const std::exception& e) { return fail(e); }
}

// Pair live interior components with the exterior collision owner.
PyObject* projectileLiveCriticalTarget(PyObject*, PyObject* args) {
    PyObject *battle, *record, *target, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &record, &target, &g)) return nullptr;
    try {
        const bool local = truthy(method(record, "get", p::text("local").get()));
        if (!(local || truthy(method(record, "get", p::text("native_remote").get())))) return p::retain(target).release();
        p::Ref body = p::retain(Py_None), chassis = p::retain(Py_None);
        if (local) {
            auto local_matrix = attr(battle, "_local_matrix");
            if (local_matrix.get() != Py_None) {
                replace(body, method(battle, "_local_body_pose"));
                replace(chassis, attr(battle, "_local_matrix"));
            }
        } else {
            auto pair = unpack(dispatch(battle, "_projectile_vehicle_matrices", "projectile_vehicle_matrices", {record, target}, {Py_None}).get(), 2);
            replace(body, p::retain(pair[0].get()));
            replace(chassis, p::retain(pair[1].get()));
        }
        auto math = attr(attr(battle, "_runtime").get(), "math");
        auto matrix = attr(math.get(), "Matrix");
        auto descriptor = attr(target, "typeDescriptor");
        auto body_matrix = body.get() != Py_None ? p::Ref(PyObject_CallFunctionObjArgs(matrix.get(), body.get(), nullptr)) : p::retain(Py_None);
        auto position = attr(target, "position");
        auto appearance = optionalAttr(target, "appearance");
        auto chassis_matrix = chassis.get() != Py_None ? p::Ref(PyObject_CallFunctionObjArgs(matrix.get(), chassis.get(), nullptr)) : p::retain(Py_None);
        return callGlobal(g, "_ProjectileCollisionTarget", {target, descriptor.get(), body_matrix.get(), position.get(), appearance.get(),
                                                             math.get(), chassis_matrix.get()}).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* discardProvisionalProjectile(PyObject*, PyObject* args) {
    PyObject *battle, *projectile_id, *projectile_module, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &projectile_id, &projectile_module, &g)) return nullptr;
    try {
        if (truthy(method(projectile_module, "enabled", battle))) method(projectile_module, "discard", battle, projectile_id);
        auto metas = attr(battle, "_projectile_meta");
        auto meta = method(metas.get(), "get", projectile_id);
        if (meta.get() == Py_None || !truthy(dispatch(battle, "_owns_projectile", "owns_projectile", {meta.get()}, {g}))) Py_RETURN_FALSE;
        auto projectiles = attr(battle, "_projectiles");
        if (projectiles.get() != Py_None)
            method(projectiles.get(), "rollback_provisional", method(meta.get(), "get", p::text("manager_key").get(), projectile_id).get());
        method(attr(battle, "_projectile_meta").get(), "pop", projectile_id, Py_None);
        method(attr(battle, "_projectile_terminal_data").get(), "pop", projectile_id, Py_None);
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// max(0.0, min(0.25, float(rtt_ms) / 2000.0)) from the client's minimum (or current) round trip.
double oneWay(PyObject* battle) {
    try {
        auto client = attr(battle, "client");
        auto rtt = optionalAttr(client.get(), "minimum_rtt_ms");
        if (rtt.get() == Py_None) replace(rtt, optionalAttr(client.get(), "rtt_ms"));
        if (rtt.get() == Py_None) return 0.;
        const double half = toFloat(rtt.get()) / 2000.;
        const double bounded = half < 0.25 ? half : 0.25;
        return bounded > 0. ? bounded : 0.;
    } catch (const std::exception&) {
        if (!PyErr_Occurred() || !conversionError()) throw;
        PyErr_Clear();
        return 0.;
    }
}
// Anchor round-relative server time in this process clock domain.
PyObject* observeProjectileMessage(PyObject*, PyObject* args) {
    PyObject *battle, *message, *projectile_module, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &message, &projectile_module, &bot_module, &g)) return nullptr;
    try {
        if (truthy(method(projectile_module, "enabled", battle))) return method(projectile_module, "observe", battle, message).release();
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get()))) {
            auto accepted = method(bot_module, "projectile_observe", bots.get(), message, method(battle, "_clock").get());
            if (accepted.get() != Py_None) return accepted.release();
        }
        if (!PyDict_Check(message)) Py_RETURN_FALSE;
        auto now = method(battle, "_clock");
        auto received = method(message, "get", p::text("_client_received_time").get());
        auto local_anchor = p::retain(now.get());
        if (received.get() != Py_None) {
            try {
                const double at = toFloat(received.get());
                if (!std::isnan(at) && !std::isinf(at)) {
                    // Project the network thread's queueing delay into BigWorld time.
                    auto clock_module = module("time");
                    auto monotonic = optionalAttr(clock_module.get(), "monotonic");
                    const double process_now = PyCallable_Check(monotonic.get()) ?
                        toFloat(p::Ref(PyObject_CallObject(monotonic.get(), nullptr)).get()) :
                        toFloat(method(clock_module.get(), "clock").get());
                    const double raw_lag = process_now - at;
                    const double capped = raw_lag < 60. ? raw_lag : 60.;
                    const double lag = capped > 0. ? capped : 0.;
                    p::Ref shifted(PyNumber_Subtract(now.get(), pyFloat(lag).get()));
                    replace(local_anchor, compare(shifted.get(), pyFloat(0.).get(), Py_GT) ? std::move(shifted) : pyFloat(0.));
                }
            } catch (const std::exception&) {
                if (!PyErr_Occurred() || !conversionError()) throw;
                PyErr_Clear(); Py_RETURN_FALSE;
            }
        }
        auto anchored = [&](double one_way) {
            p::Ref shifted(PyNumber_Subtract(local_anchor.get(), pyFloat(one_way).get()));
            return compare(shifted.get(), pyFloat(0.).get(), Py_GT) ? std::move(shifted) : pyFloat(0.);
        };
        auto integerField = [&](const char* name, p::Ref& out) {
            PyObject* converted = PyNumber_Int(method(message, "get", p::text(name).get()).get());
            if (converted) { replace(out, p::Ref(converted)); return true; }
            if (!conversionError()) throw p::Error("integer conversion failed");
            PyErr_Clear();
            return false;
        };
        if (member(message, p::text("server_time_ms").get())) {
            p::Ref server_time = p::retain(Py_None);
            if (!integerField("server_time_ms", server_time) || compareInt(server_time.get(), 0, Py_LT)) Py_RETURN_FALSE;
            auto previous = attr(battle, "_projectile_server_time_ms");
            if (previous.get() == Py_None || compare(server_time.get(), previous.get(), Py_GE)) {
                const double one_way = oneWay(battle);
                setAttr(battle, "_projectile_server_time_ms", server_time.get());
                setAttr(battle, "_projectile_server_local_time", anchored(one_way).get());
            }
        }
        if (member(message, p::text("motion_time_us").get())) {
            p::Ref motion_time = p::retain(Py_None);
            if (!integerField("motion_time_us", motion_time) || compareInt(motion_time.get(), 0, Py_LT)) Py_RETURN_FALSE;
            const double one_way = oneWay(battle);
            auto previous = attr(battle, "_pose_motion_time_us");
            if (previous.get() == Py_None || compare(motion_time.get(), previous.get(), Py_GE)) {
                setAttr(battle, "_pose_motion_time_us", motion_time.get());
                setAttr(battle, "_pose_motion_local_time", anchored(one_way).get());
            }
        }
        auto client_epoch = optionalAttr(attr(battle, "client").get(), "authority_epoch");
        auto epoch = method(message, "get", p::text("authority_epoch").get(), client_epoch.get());
        if (epoch.get() != Py_None) dispatch(battle, "_set_projectile_epoch", "set_projectile_epoch", {epoch.get(), now.get()}, {g});
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* installProjectileMeta(PyObject*, PyObject* args) {
    PyObject *battle, *normalized, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &normalized, &g)) return nullptr;
    try {
        auto projectile_id = item(normalized, "projectile_id");
        auto metas = attr(battle, "_projectile_meta");
        auto meta = method(metas.get(), "get", projectile_id.get());
        if (meta.get() == Py_None) {
            replace(meta, dispatch(battle, "_new_projectile_meta", "new_projectile_meta", {normalized}));
            if (PyObject_SetItem(attr(battle, "_projectile_meta").get(), projectile_id.get(), meta.get()) < 0) throw p::Error("meta store failed");
            dispatch(battle, "_report_player_shot_timing", "report_player_shot_timing", {meta.get()});
        } else {
            // Launch fields are immutable; only the acknowledged cursor and penetration state may advance.
            auto current = toInt(method(meta.get(), "get", p::text("ricochet_count").get(), integer(0).get()).get());
            auto default_key = compareInt(current.get(), 0, Py_EQ) ? p::retain(projectile_id.get())
                                                                   : p::Ref(PyTuple_Pack(2, projectile_id.get(), current.get()));
            method(meta.get(), "setdefault", p::text("manager_key").get(), default_key.get());
            p::Ref iterator(PyObject_GetIter(global(g, "_PROJECTILE_IMMUTABLE_FIELDS").get()));
            while (PyObject* raw = PyIter_Next(iterator.get())) {
                p::Ref field_name(raw);
                if (compare(method(meta.get(), "get", field_name.get()).get(), method(normalized, "get", field_name.get()).get(), Py_NE))
                    raise("canonical projectile launch changed");
            }
            if (PyErr_Occurred()) throw p::Error("immutable fields iteration failed");
            auto incoming = item(normalized, "ricochet_count");
            if (!truthy(attr(battle, "_worker_mode")) && truthy(dispatch(battle, "_owns_projectile", "owns_projectile", {meta.get()}, {g})) &&
                compare(incoming.get(), current.get(), Py_LT))
                // An older echo can follow a locally queued ricochet; it does not rewind the second segment.
                return meta.release();
            if (compare(incoming.get(), current.get(), Py_LT) ||
                compare(incoming.get(), p::Ref(PyNumber_Add(current.get(), integer(1).get())).get(), Py_GT))
                raise("canonical projectile segment regressed");
            const char* segment_fields[] = {"segment_origin", "segment_velocity", "segment_start_time_ms", "base_penetration_multiplier"};
            if (compare(incoming.get(), current.get(), Py_EQ)) {
                for (const char* name : segment_fields)
                    if (compare(method(meta.get(), "get", p::text(name).get()).get(), method(normalized, "get", p::text(name).get()).get(), Py_NE))
                        raise("canonical projectile segment changed");
            } else {
                auto old_key = method(meta.get(), "get", p::text("manager_key").get(), projectile_id.get());
                auto projectiles = attr(battle, "_projectiles");
                if (projectiles.get() != Py_None) method(projectiles.get(), "remove", old_key.get());
                for (const char* name : segment_fields) put(meta.get(), name, item(normalized, name));
                put(meta.get(), "ricochet_count", p::retain(incoming.get()));
                put(meta.get(), "manager_key", p::Ref(PyTuple_Pack(2, projectile_id.get(), incoming.get())));
                put(meta.get(), "awaiting_ricochet", p::retain(Py_False));
                put(meta.get(), "pending_ricochet", p::retain(Py_None));
                put(meta.get(), "destructibles_pending", p::list());
            }
            auto checked = item(normalized, "base_checked_ms");
            if (compare(checked.get(), method(meta.get(), "get", p::text("base_checked_ms").get(), integer(0).get()).get(), Py_GE)) {
                put(meta.get(), "base_checked_ms", item(normalized, "base_checked_ms"));
                put(meta.get(), "acked_distance", item(normalized, "checked_distance"));
                put(meta.get(), "acked_piercing_loss", item(normalized, "piercing_loss"));
                // Preserve the server's six-decimal monotonic floor: an acknowledgement never regresses a frontier.
                for (const char* name : {"checked_distance", "piercing_loss"}) {
                    auto kept = callGlobal(g, "_number", {method(meta.get(), "get", p::text(name).get()).get(), pyFloat(0.).get()});
                    auto incoming_value = item(normalized, name);
                    put(meta.get(), name, compare(incoming_value.get(), kept.get(), Py_GT) ? std::move(incoming_value) : std::move(kept));
                }
                auto pending = method(meta.get(), "get", p::text("progress_pending").get());
                if (pending.get() != Py_None && compare(item(normalized, "base_checked_ms").get(), item(pending.get(), "checked_through_ms").get(), Py_GE))
                    put(meta.get(), "progress_pending", p::retain(Py_None));
                auto projectiles = attr(battle, "_projectiles");
                const bool active = projectiles.get() != Py_None &&
                    truthy(method(projectiles.get(), "contains", item(meta.get(), "manager_key").get()));
                auto absent = [&](const char* name) { return method(meta.get(), "get", p::text(name).get()).get() == Py_None; };
                if (!active && absent("pending_resolution") && absent("pending_ricochet") &&
                    !truthy(method(meta.get(), "get", p::text("awaiting_ricochet").get())) &&
                    !truthy(method(meta.get(), "get", p::text("awaiting_resolution").get()))) {
                    put(meta.get(), "checked_distance", item(normalized, "checked_distance"));
                    put(meta.get(), "piercing_loss", item(normalized, "piercing_loss"));
                }
            }
        }
        dispatch(battle, "_confirm_bot_projectile_launch", "confirm_bot_projectile_launch", {normalized}, {g});
        return meta.release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* projectileRecordPoses(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        auto positions = dispatch(battle, "_projectile_record_positions", "projectile_record_positions", {}, {g});
        auto result = p::dict();
        p::Ref iterator(PyObject_GetIter(method(positions.get(), "items").get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref pair(raw);
            PyObject *key = PyTuple_GET_ITEM(pair.get(), 0), *position = PyTuple_GET_ITEM(pair.get(), 1);
            auto record = recordsGet(battle, key);
            if (record.get() == Py_None) continue;
            auto pose = dispatch(battle, "_projectile_record_pose", "projectile_record_pose", {key, record.get(), position}, {g});
            if (pose.get() != Py_None && PyObject_SetItem(result.get(), key, pose.get()) < 0) throw p::Error("pose store failed");
        }
        if (PyErr_Occurred()) throw p::Error("positions iteration failed");
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

bool admittedVisual(PyObject* battle, PyObject* projectile_id) {
    auto visual = method(attr(battle, "_projectile_visual_meta").get(), "get", projectile_id);
    return visual.get() == Py_None || truthy(method(visual.get(), "get", p::text("admitted").get(), Py_True));
}
// shotEffects[int(effects_index)] for the meta's shot, or None when the shot has no effects index.
p::Ref shotEffectsIndex(PyObject* battle, PyObject* meta, PyObject* g) {
    auto shot = dispatch(battle, "_projectile_shot", "projectile_shot", {meta}, {g});
    auto shell = field(g, shot.get(), "shell", Py_None);
    return field(g, shell.get(), "effectsIndex", Py_None);
}
bool effectsLookupError() {
    return PyErr_ExceptionMatches(PyExc_AttributeError) || PyErr_ExceptionMatches(PyExc_IndexError) ||
           PyErr_ExceptionMatches(PyExc_KeyError) || PyErr_ExceptionMatches(PyExc_TypeError) ||
           PyErr_ExceptionMatches(PyExc_ValueError);
}
p::Ref shotEffects(PyObject* battle, PyObject* effects_index) {
    auto cache = attr(attr(attr(battle, "_runtime").get(), "vehicles").get(), "g_cache");
    return p::Ref(PyObject_GetItem(attr(cache.get(), "shotEffects").get(), toInt(effects_index).get()));
}
// The terminal or visual velocity, else the shooter-to-impact direction for a wreck hit.
p::Ref normalisedDirection(PyObject* battle, PyObject* value, PyObject* g) {
    auto direction = method(battle, "_vector", callGlobal(g, "_xyz", {value}).get());
    if (compare(attr(direction.get(), "length").get(), pyFloat(0.).get(), Py_LE)) return p::retain(Py_None);
    method(direction.get(), "normalise");
    return direction;
}

// (effectsDescr, effectMaterial, velocity) for a world hit, or None when the verdict is not ours to make.
PyObject* projectileExplosion(PyObject*, PyObject* args) {
    PyObject *battle, *projectile_id, *impact, *outcome = nullptr, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &projectile_id, &impact, &outcome, &g)) return nullptr;
    try {
        if (!equal(outcome, p::text("impact").get())) Py_RETURN_NONE;
        auto meta = method(attr(battle, "_projectile_meta").get(), "get", projectile_id);
        if (meta.get() == Py_None || method(meta.get(), "get", p::text("hit_vehicle").get()).get() != Py_False) Py_RETURN_NONE;
        if (!admittedVisual(battle, projectile_id)) Py_RETURN_NONE;
        auto effects_index = shotEffectsIndex(battle, meta.get(), g);
        if (effects_index.get() == Py_None) Py_RETURN_NONE;
        p::Ref effects_descr = p::retain(Py_None);
        try { replace(effects_descr, shotEffects(battle, effects_index.get())); }
        catch (const std::exception&) {
            if (!PyErr_Occurred() || !effectsLookupError()) throw;
            PyErr_Clear(); Py_RETURN_NONE;
        }
        auto material = method(battle, "_surface_effect_material", impact);
        if (material.get() == Py_None) Py_RETURN_NONE;
        auto velocity = method(meta.get(), "get", p::text("terminal_velocity").get());
        if (!truthy(velocity) || PyObject_Size(p::Ref(PySequence_Tuple(velocity.get())).get()) < 3) {
            auto visual = method(attr(battle, "_projectile_visual_meta").get(), "get", projectile_id);
            replace(velocity, truthy(visual) ? method(visual.get(), "get", p::text("velocity").get()) : p::retain(Py_None));
        }
        if (!truthy(velocity)) Py_RETURN_NONE;
        // __addExplosionEffect keys the effect at position +/- velocityDir; use a unit direction.
        auto direction = normalisedDirection(battle, velocity.get(), g);
        if (direction.get() == Py_None) Py_RETURN_NONE;
        auto event = p::dict();
        put(event.get(), "projectile_id", p::retain(projectile_id));
        callKw(battle, "_report_effect", {p::text("world_explosion").get(), material.get(), effects_index.get(), impact, direction.get()},
               {{"event", event.get()}});
        return PyTuple_Pack(3, effects_descr.get(), material.get(), direction.get());
    } catch (const std::exception& e) { return fail(e); }
}

// Play the stock armour-hit family on one visible retained wreck.
PyObject* presentProjectileWreckHit(PyObject*, PyObject* args) {
    PyObject *battle, *projectile_id, *event, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &projectile_id, &event, &g)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        if (!admittedVisual(battle, projectile_id)) Py_RETURN_FALSE;
        auto wreck_hit = method(event, "get", p::text("wreck_hit").get());
        if (!PyDict_Check(wreck_hit.get()) || PyDict_Size(wreck_hit.get()) != 2 ||
            !PyDict_GetItemString(wreck_hit.get(), "target_kind") || !PyDict_GetItemString(wreck_hit.get(), "target_id"))
            Py_RETURN_FALSE;
        auto target_kind = method(wreck_hit.get(), "get", p::text("target_kind").get());
        PyObject* converted = PyNumber_Int(method(wreck_hit.get(), "get", p::text("target_id").get()).get());
        if (!converted) {
            if (!conversionError()) return nullptr;
            PyErr_Clear(); Py_RETURN_FALSE;
        }
        p::Ref target_id(converted);
        if (!textIn(target_kind.get(), {"player", "bot"}) || compareInt(target_id.get(), 0, Py_LE)) Py_RETURN_FALSE;
        auto target_record = recordsGet(battle, formatKey(target_kind.get(), target_id.get()).get());
        if (target_record.get() == Py_None) Py_RETURN_FALSE;
        auto target = method(battle, "_server_entity", method(target_record.get(), "get", p::text("engine_id").get()).get());
        if (target.get() == Py_None || !truthy(optionalAttr(target.get(), "isStarted")) ||
            truthy(method(battle, "_record_alive", target_record.get(), target.get())) ||
            (!truthy(method(target_record.get(), "get", p::text("local").get())) &&
             !truthy(method(target_record.get(), "get", p::text("spot_visible").get(), Py_True))))
            Py_RETURN_FALSE;
        auto meta = method(attr(battle, "_projectile_meta").get(), "get", projectile_id);
        if (meta.get() == Py_None) Py_RETURN_FALSE;
        auto effects_index = shotEffectsIndex(battle, meta.get(), g);
        if (effects_index.get() == Py_None) Py_RETURN_FALSE;
        std::vector<p::Ref> parts;
        try {
            auto effects_descr = shotEffects(battle, effects_index.get());
            parts = unpack(p::Ref(PyObject_GetItem(effects_descr.get(), p::text("armorHit").get())).get(), 3);
        } catch (const std::exception&) {
            if (!PyErr_Occurred() || !effectsLookupError()) throw;
            PyErr_Clear(); Py_RETURN_FALSE;
        }
        auto impact = method(event, "get", p::text("impact").get());
        if (!(PyList_Check(impact.get()) || PyTuple_Check(impact.get())) || PyObject_Size(impact.get()) < 3) Py_RETURN_FALSE;
        auto direction_value = method(meta.get(), "get", p::text("terminal_velocity").get());
        if (!truthy(direction_value)) {
            auto visual = method(attr(battle, "_projectile_visual_meta").get(), "get", projectile_id);
            replace(direction_value, truthy(visual) ? method(visual.get(), "get", p::text("velocity").get()) : p::retain(Py_None));
        }
        if (!truthy(direction_value)) {
            auto source = dispatch(battle, "_projectile_source_entity", "projectile_source_entity", {meta.get()});
            auto source_position = optionalAttr(source.get(), "position");
            if (source_position.get() != Py_None) {
                p::Ref delta(PyTuple_New(3));
                for (int i = 0; i < 3; ++i) {
                    auto a = callGlobal(g, "_number", {p::Ref(PyObject_GetItem(impact.get(), integer(i).get())).get()});
                    auto b = callGlobal(g, "_number", {p::Ref(PyObject_GetItem(source_position.get(), integer(i).get())).get()});
                    PyTuple_SET_ITEM(delta.get(), i, PyNumber_Subtract(a.get(), b.get()));
                    if (!PyTuple_GET_ITEM(delta.get(), i)) throw p::Error("direction subtraction failed");
                }
                replace(direction_value, std::move(delta));
            }
        }
        if (!truthy(direction_value)) Py_RETURN_FALSE;
        auto direction = normalisedDirection(battle, direction_value.get(), g);
        if (direction.get() == Py_None) Py_RETURN_FALSE;
        auto hit_position = method(battle, "_vector", callGlobal(g, "_xyz", {impact.get()}).get());
        auto terrain = optionalAttr(attr(battle, "_avatar").get(), "terrainEffects");
        auto add_effect = optionalAttr(terrain.get(), "addNew");
        if (!PyCallable_Check(add_effect.get()) ||
            !truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {p::text("projectile impact presentation").get()})))
            Py_RETURN_FALSE;
        method(battle, "_report_effect", p::text("wreck_hit").get(), p::text("armorHit").get(), effects_index.get(), impact.get(), direction.get());
        try {
            // The #1513 impact trio needs the shooter and target identities, as for a live vehicle hit.
            auto offset = method(direction.get(), "scale", p::number(0.4).get());
            p::Ref start(PyNumber_Subtract(hit_position.get(), offset.get()));
            auto far_offset = method(direction.get(), "scale", p::number(0.4).get());
            p::Ref end(PyNumber_Add(hit_position.get(), far_offset.get()));
            auto attacker = dispatch(battle, "_projectile_attacker_engine_id", "projectile_attacker_engine_id", {meta.get()});
            auto identity = method(battle, "_impact_sound_identity", target_record.get(), attacker.get(), direction.get(), pyFloat(0.).get());
            p::Ref positional(PyTuple_Pack(4, hit_position.get(), parts[1].get(), parts[0].get(), Py_None));
            callWithMapping(add_effect.get(), positional.get(), {{"dir", direction.get()}, {"start", start.get()}, {"end", end.get()},
                            {"showShockWave", Py_False}, {"showFlashBang", Py_False}}, identity.get());
        } catch (const std::exception& e) {
            warnCaught(battle, "projectile impact presentation", e, false);
            Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// (0.0, -gravity, 0.0)
p::Ref gravityVector(PyObject* gravity) {
    return p::Ref(PyTuple_Pack(3, pyFloat(0.).get(), p::Ref(PyNumber_Negative(gravity)).get(), pyFloat(0.).get()));
}
// max(floor, value) with Python's first-argument tie.
p::Ref atLeast(double floor, p::Ref value) {
    return compare(value.get(), pyFloat(floor).get(), Py_GT) ? std::move(value) : pyFloat(floor);
}
double sinceTriggerMs(PyObject* timeline, PyObject* g) {
    const double waited = toFloat(p::Ref(PyObject_CallObject(global(g, "_PROFILE_CLOCK").get(), nullptr)).get()) -
                          toFloat(item(timeline, "trigger").get());
    return (waited > 0. ? waited : 0.) * 1000.;
}

PyObject* stopProjectileVisual(PyObject*, PyObject* args) {
    PyObject *battle, *projectile_id, *event, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &projectile_id, &event, &g)) return nullptr;
    try {
        auto visuals = attr(battle, "_projectile_visual_meta");
        auto factory = attr(battle, "_remote_factory");
        if (truthy(attr(battle, "_worker_mode")) || factory.get() == Py_None) {
            method(visuals.get(), "pop", projectile_id, Py_None);
            Py_RETURN_FALSE;
        }
        const bool mapping = PyDict_Check(event);
        auto impact = mapping ? method(event, "get", p::text("impact").get()) : p::retain(Py_None);
        if (impact.get() == Py_None) {
            auto meta = method(attr(battle, "_projectile_meta").get(), "get", projectile_id);
            auto key = meta.get() != Py_None ? method(meta.get(), "get", p::text("manager_key").get()) : p::retain(projectile_id);
            auto projectiles = attr(battle, "_projectiles");
            auto state = projectiles.get() != Py_None ? method(projectiles.get(), "get", key.get()) : p::retain(Py_None);
            replace(impact, state.get() != Py_None ? method(state.get(), "get", p::text("position").get()) : p::retain(Py_None));
        }
        if (impact.get() == Py_None) {
            auto visual = method(attr(battle, "_projectile_visual_meta").get(), "get", projectile_id);
            p::Ref elapsed = p::retain(Py_None);
            try {
                const double resolved = toFloat(method(event, "get", p::text("resolved_time_ms").get()).get());
                const double start = toFloat(method(visual.get(), "get", p::text("segment_start_time_ms").get(), integer(0).get()).get());
                const double seconds = (resolved - start) / 1000.;
                replace(elapsed, pyFloat(seconds > 0. ? seconds : 0.));
            } catch (const std::exception&) {
                if (!PyErr_Occurred() || !(conversionError() || PyErr_ExceptionMatches(PyExc_AttributeError))) throw;
                PyErr_Clear();
            }
            if (visual.get() != Py_None && elapsed.get() != Py_None)
                replace(impact, callGlobal(g, "trajectory_position", {item(visual.get(), "origin").get(), item(visual.get(), "velocity").get(),
                                                                       gravityVector(item(visual.get(), "gravity").get()).get(), elapsed.get()}));
        }
        if (impact.get() == Py_None) Py_RETURN_FALSE;
        bool stopped;
        try {
            auto outcome = mapping ? method(event, "get", p::text("outcome").get()) : p::retain(Py_None);
            auto stop = attr(attr(battle, "_remote_factory").get(), "stop_projectile_tracer");
            auto explosion = dispatch(battle, "_projectile_explosion", "projectile_explosion", {projectile_id, impact.get(), outcome.get()}, {g});
            const bool missed = mapping && method(event, "get", p::text("hit_vehicle").get()).get() == Py_False;
            p::Ref positional(PyTuple_Pack(2, projectile_id, impact.get()));
            stopped = truthy(callWithMapping(stop.get(), positional.get(), {{"explosion", explosion.get()}, {"missed", missed ? Py_True : Py_False}},
                                             p::dict().get()));
        } catch (const std::exception& e) {
            // Terminal authority has already been applied; a cosmetic retirement failure must not poison the journal.
            warnCaught(battle, "projectile visual retirement", e, true);
            stopped = false;
        }
        method(attr(battle, "_projectile_visual_meta").get(), "pop", projectile_id, Py_None);
        return p::boolean(stopped).release();
    } catch (const std::exception& e) { return fail(e); }
}

// Start one native visual per segment, independent of cursor cadence.
PyObject* ensureProjectileVisualNativePresentation(PyObject*, PyObject* args) {
    PyObject *battle, *normalized, *now, *visual_start_argument, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &normalized, &now, &visual_start_argument, &g)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode"))) Py_RETURN_FALSE;
        if (attr(battle, "_remote_factory").get() == Py_None || !PyDict_Check(normalized)) Py_RETURN_FALSE;
        auto projectile_id = item(normalized, "projectile_id");
        if (member(attr(battle, "_projectile_visual_terminals").get(), projectile_id.get())) Py_RETURN_FALSE;
        auto owner = method(attr(battle, "_projectile_meta").get(), "get", projectile_id.get());
        auto owns = [&]() { return truthy(dispatch(battle, "_owns_projectile", "owns_projectile", {owner.get()}, {g})); };
        auto ricochets = [&]() { return item(normalized, "ricochet_count"); };
        if (owner.get() != Py_None && member(owner.get(), p::text("local_launch_time").get()) && owns() &&
            compare(ricochets().get(), method(owner.get(), "get", p::text("presented_ricochet_count").get(), integer(0).get()).get(), Py_GT))
            // Physical continuation may run ahead of the echo; its tracer follows the committed armour response.
            Py_RETURN_FALSE;
        if (owner.get() != Py_None && owns() &&
            (method(owner.get(), "get", p::text("local_ricochet_admission").get()).get() != Py_None ||
             method(owner.get(), "get", p::text("pending_resolution").get()).get() != Py_None ||
             truthy(method(owner.get(), "get", p::text("awaiting_resolution").get()))))
            // An echo cannot restart a deferred or locally retired segment.
            Py_RETURN_FALSE;
        double confirmed = visualAge(normalized, g);
        auto bot_stamp = method(normalized, "get", p::text("bot_presentation_time_us").get());
        // (owner or normalized)
        auto holder = [&]() { return truthy(owner) ? p::retain(owner.get()) : p::retain(normalized); };
        auto segment_start_s = [&]() { return toFloat(item(normalized, "segment_start_time_ms").get()) / 1000.; };
        if (!truthy(attr(battle, "_replay_mode")) && bot_stamp.get() != Py_None) {
            auto start_message = attr(battle, "_start_message");
            auto server_time = method(truthy(start_message) ? start_message.get() : p::dict().get(), "get", p::text("server_time_ms").get(),
                                      integer(0).get());
            const bool historical = compare(toInt(server_time.get()).get(), toInt(item(normalized, "launch_server_time_ms").get()).get(), Py_GT);
            if (!truthy(method(normalized, "get", p::text("muzzle_presented").get())) && !historical) Py_RETURN_FALSE;
            if (!historical &&
                compare(ricochets().get(), method(holder().get(), "get", p::text("presented_ricochet_count").get(), integer(0).get()).get(), Py_GT))
                Py_RETURN_FALSE;
            auto actor = recordsGet(battle, formatKey(p::text("bot").get(), item(normalized, "shooter_id").get()).get());
            if (!truthy(actor)) replace(actor, p::dict());
            auto stamp = method(actor.get(), "get", p::text("presentation_time_us").get());
            if (historical && stamp.get() != Py_None) {
                p::Ref micros(PyNumber_Subtract(toInt(stamp.get()).get(), toInt(item(normalized, "bot_presentation_time_us").get()).get()));
                const double value = toFloat(micros.get()) / 1000000.0 - segment_start_s();
                confirmed = value > 0. ? value : 0.;
            } else if (!member(holder().get(), p::text("bot_visual_launch_time").get())) {
                // A newly displayed muzzle owns a new visual flight, not a mid-flight tracer.
                confirmed = 0.;
            }
            const double default_launch = toFloat(now) - confirmed - segment_start_s();
            auto launch = method(holder().get(), "setdefault", p::text("bot_visual_launch_time").get(), pyFloat(default_launch).get());
            const double value = toFloat(now) - toFloat(launch.get()) - segment_start_s();
            confirmed = value > 0. ? value : 0.;
        }
        auto visuals = attr(battle, "_projectile_visual_meta");
        auto visual = method(visuals.get(), "get", projectile_id.get());
        if (visual.get() != Py_None &&
            compare(toInt(method(visual.get(), "get", p::text("ricochet_count").get(), integer(0).get()).get()).get(), ricochets().get(), Py_GT))
            // A delayed launch callback must not replace the current bounce segment.
            return p::boolean(truthy(method(visual.get(), "get", p::text("active").get(), Py_False))).release();
        if (visual.get() != Py_None &&
            compare(toInt(method(visual.get(), "get", p::text("ricochet_count").get(), integer(0).get()).get()).get(), ricochets().get(), Py_NE)) {
            auto meta = method(attr(battle, "_projectile_meta").get(), "get", projectile_id.get());
            if (meta.get() != Py_None) put(meta.get(), "hit_vehicle", p::retain(Py_True));
            auto stop_event = literal(2);
            put(stop_event.get(), "impact", p::Ref(PySequence_List(item(normalized, "segment_origin").get())));
            put(stop_event.get(), "resolved_time_ms", item(normalized, "segment_start_time_ms"));
            dispatch(battle, "_stop_projectile_visual", "stop_projectile_visual", {projectile_id.get(), stop_event.get()}, {g});
            replace(visual, p::retain(Py_None));
        } else if (visual.get() != Py_None) {
            // Only a failed initial creation may try the newest checked pose.
            auto timeline = method(attr(battle, "_fire_timeline").get(), "get", str(projectile_id.get()).get());
            if (timeline.get() != Py_None && method(timeline.get(), "get", p::text("shown").get()).get() != Py_None &&
                compareInt(toInt(method(timeline.get(), "get", p::text("cursor_logs").get(), integer(0).get()).get()).get(), 8, Py_LT)) {
                put(timeline.get(), "cursor_logs",
                    p::Ref(PyNumber_Add(toInt(method(timeline.get(), "get", p::text("cursor_logs").get(), integer(0).get()).get()).get(),
                                        integer(1).get())));
                auto active = integer(truthy(method(visual.get(), "get", p::text("active").get())) ? 1 : 0);
                p::Ref values(PyTuple_Pack(4, projectile_id.get(), pyFloat(confirmed * 1000.).get(), active.get(),
                                           pyFloat(sinceTriggerMs(timeline.get(), g)).get()));
                writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] FIRE CURSOR projectile=%s confirmed_ms=%.0f "
                                                           "native_started=%d since_trigger_ms=%.0f\n").get(), values.get())).get());
            }
            if (truthy(method(visual.get(), "get", p::text("active").get(), Py_False)) ||
                !truthy(method(visual.get(), "get", p::text("admitted").get(), Py_True)))
                return p::boolean(truthy(method(visual.get(), "get", p::text("active").get(), Py_False))).release();
        }
        auto descriptor = dispatch(battle, "_projectile_source_descriptor", "projectile_source_descriptor", {normalized});
        if (descriptor.get() == Py_None) Py_RETURN_FALSE;
        if (visual.get() == Py_None) {
            replace(visual, literal(6));
            put(visual.get(), "origin", p::Ref(PySequence_Tuple(item(normalized, "segment_origin").get())));
            put(visual.get(), "velocity", p::Ref(PySequence_Tuple(item(normalized, "segment_velocity").get())));
            put(visual.get(), "gravity", item(normalized, "gravity"));
            put(visual.get(), "segment_start_time_ms", item(normalized, "segment_start_time_ms"));
            put(visual.get(), "ricochet_count", item(normalized, "ricochet_count"));
            put(visual.get(), "active", p::retain(Py_False));
            if (PyObject_SetItem(attr(battle, "_projectile_visual_meta").get(), projectile_id.get(), visual.get()) < 0)
                throw p::Error("visual store failed");
            auto record = recordsGet(battle, formatKey(item(normalized, "shooter_kind").get(), item(normalized, "shooter_id").get()).get());
            p::Ref attacker_id = integer(0);
            if (record.get() != Py_None) {
                auto engine = method(record.get(), "get", p::text("engine_id").get(), integer(0).get());
                replace(attacker_id, toInt(truthy(engine) ? engine.get() : integer(0).get()));
            }
            if (compareInt(attacker_id.get(), 0, Py_LE)) replace(attacker_id, toInt(item(normalized, "shooter_id").get()));
            put(visual.get(), "attacker_id", p::retain(attacker_id.get()));
            put(visual.get(), "admitted", dispatch(battle, "_admit_projectile_visual", "admit_projectile_visual",
                                                   {attacker_id.get(), projectile_id.get(), now}));
        }
        if (!truthy(method(visual.get(), "get", p::text("admitted").get(), Py_True)) ||
            !truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {p::text("projectile visual launch").get()})))
            Py_RETURN_FALSE;
        // A late first snapshot starts at its checked trajectory pose; the stock simulator then advances locally.
        auto gravity = item(normalized, "gravity");
        auto origin = item(normalized, "segment_origin");
        auto velocity = item(normalized, "segment_velocity");
        auto elapsed = pyFloat(confirmed);
        auto reference_origin = callGlobal(g, "trajectory_position", {origin.get(), velocity.get(), gravityVector(gravity.get()).get(), elapsed.get()});
        p::Ref fall(PyNumber_Multiply(gravity.get(), elapsed.get()));
        p::Ref reference_velocity(PyTuple_Pack(3, p::Ref(PySequence_GetItem(velocity.get(), 0)).get(),
                                               p::Ref(PyNumber_Subtract(p::Ref(PySequence_GetItem(velocity.get(), 1)).get(), fall.get())).get(),
                                               p::Ref(PySequence_GetItem(velocity.get(), 2)).get()));
        p::Ref visual_start = p::retain(visual_start_argument);
        if (confirmed > 0. || truthy(ricochets())) replace(visual_start, p::retain(Py_None));
        try {
            auto max_distance = item(normalized, "max_distance");
            auto remaining = atLeast(0.001, p::Ref(PyNumber_Subtract(max_distance.get(), item(normalized, "checked_distance").get())));
            if (method(normalized, "get", p::text("bot_presentation_time_us").get()).get() != Py_None) {
                // The authority cursor may be ahead of this displayed flight; only displayed segments spend its range.
                auto spent = compareInt(ricochets().get(), 0, Py_EQ) ? pyFloat(0.) :
                    method(holder().get(), "get", p::text("visual_segment_distance").get(), item(normalized, "checked_distance").get());
                replace(remaining, atLeast(0.001, p::Ref(PyNumber_Subtract(item(normalized, "max_distance").get(), spent.get()))));
            }
            auto play = attr(attr(battle, "_remote_factory").get(), "play_projectile_tracer");
            p::Ref positional(PyTuple_Pack(10, descriptor.get(), item(normalized, "shell_index").get(), origin.get(), velocity.get(),
                                           gravity.get(), remaining.get(), item(visual.get(), "attacker_id").get(), projectile_id.get(),
                                           reference_origin.get(), reference_velocity.get()));
            const bool active = truthy(callWithMapping(play.get(), positional.get(),
                {{"is_ricochet", truthy(ricochets()) ? Py_True : Py_False}, {"visual_start", visual_start.get()}}, p::dict().get()));
            put(visual.get(), "active", p::boolean(active));
            if (active && method(normalized, "get", p::text("bot_presentation_time_us").get()).get() != Py_None) {
                p::Ref values(PyTuple_Pack(4, projectile_id.get(), pyFloat(confirmed * 1000.).get(),
                                           pyFloat(visualAge(normalized, g) * 1000.).get(),
                                           p::boolean(truthy(method(normalized, "get", p::text("muzzle_presented").get()))).get()));
                writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] BOT TRACER STARTED projectile=%s pose_age_ms=%.3f "
                                                           "checked_age_ms=%.3f muzzle=%s\n").get(), values.get())).get());
            }
            auto timeline = method(attr(battle, "_fire_timeline").get(), "get", str(projectile_id.get()).get());
            if (active && timeline.get() != Py_None) {
                // This records the hand-off, not the native frame where a particle first becomes visible.
                p::Ref values(PyTuple_Pack(2, projectile_id.get(), pyFloat(sinceTriggerMs(timeline.get(), g)).get()));
                writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] FIRE TRACER STARTED projectile=%s "
                                                           "since_trigger_ms=%.0f path=native\n").get(), values.get())).get());
            }
            return item(visual.get(), "active").release();
        } catch (const std::exception& e) {
            warnCaught(battle, "projectile visual launch", e, false);
            Py_RETURN_FALSE;
        }
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* ensureProjectileVisual(PyObject*, PyObject* args) {
    PyObject *battle, *normalized, *now, *visual_start, *projectile_module, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &normalized, &now, &visual_start, &projectile_module, &g)) return nullptr;
    try {
        if (truthy(method(projectile_module, "enabled", battle)))
            return method(projectile_module, "visual", battle, normalized, now, visual_start).release();
        return dispatch(battle, "_ensure_projectile_visual_native_presentation", "ensure_projectile_visual_native_presentation",
                        {normalized, now, visual_start}, {g}).release();
    } catch (const std::exception& e) { return fail(e); }
}

bool isAuthority(PyObject* battle, PyObject* g) {
    return truthy(dispatch(battle, "_projectile_is_authority", "projectile_is_authority", {}, {g}));
}

PyObject* flushPendingProjectileResolutions(PyObject*, PyObject* args) {
    PyObject *battle, *g;
    if (!PyArg_ParseTuple(args, "OO", &battle, &g)) return nullptr;
    try {
        if (!isAuthority(battle, g)) Py_RETURN_FALSE;
        bool changed = false;
        p::Ref metas(PySequence_Tuple(method(attr(battle, "_projectile_meta").get(), "values").get()));
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(metas.get()); ++i) {
            PyObject* meta = PyTuple_GET_ITEM(metas.get(), i);
            auto absent = [&](const char* name) { return method(meta, "get", p::text(name).get()).get() == Py_None; };
            if (!absent("local_ricochet_admission")) changed = truthy(method(battle, "_admit_local_projectile_ricochet", meta)) || changed;
            if (!absent("pending_resolution") && !truthy(method(meta, "get", p::text("awaiting_resolution").get())))
                changed = truthy(method(battle, "_submit_projectile_resolution", meta)) || changed;
            if (!absent("pending_ricochet") && !truthy(method(meta, "get", p::text("awaiting_ricochet").get())))
                changed = truthy(method(battle, "_submit_projectile_ricochet", meta)) || changed;
        }
        return p::boolean(changed).release();
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* pruneProjectilePositionHistory(PyObject*, PyObject* args) {
    PyObject *battle, *states_argument = Py_None, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &states_argument, &g)) return nullptr;
    try {
        auto history = attr(battle, "_projectile_position_history");
        auto projectiles = attr(battle, "_projectiles");
        if (!truthy(history) || projectiles.get() == Py_None) Py_RETURN_NONE;
        // The authority keeps the left pose endpoint a delayed next launch needs.
        if (truthy(attr(battle, "_worker_mode")) && isAuthority(battle, g)) Py_RETURN_NONE;
        auto states = states_argument == Py_None ? method(projectiles.get(), "snapshot") : p::retain(states_argument);
        if (!truthy(states)) {
            // Collapse only a truly inactive observer.
            if (!isAuthority(battle, g) && !truthy(attr(battle, "_projectile_visual_meta")))
                setAttr(battle, "_projectile_position_history",
                        p::Ref(PySequence_GetSlice(attr(battle, "_projectile_position_history").get(), -1, PY_SSIZE_T_MAX)).get());
            Py_RETURN_NONE;
        }
        bool first = true;
        double floor = 0.;
        p::Ref iterator(PyObject_GetIter(states.get()));
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref state(raw);
            const double cursor = toFloat(item(state.get(), "cursor_time").get());
            // min() keeps the first of equal values.
            if (first || cursor < floor) floor = cursor;
            first = false;
        }
        if (PyErr_Occurred()) throw p::Error("states iteration failed");
        auto live = attr(battle, "_projectile_position_history");
        while (PyObject_Size(live.get()) > 2) {
            p::Ref second(PySequence_GetItem(live.get(), 1));
            if (!(toFloat(p::Ref(PySequence_GetItem(second.get(), 0)).get()) <= floor)) break;
            method(live.get(), "pop", integer(0).get());
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// max(*values) / min(*values) with Python's first-of-equals result.
p::Ref extreme(std::initializer_list<PyObject*> values, int op) {
    p::Ref best = p::retain(*values.begin());
    for (auto it = values.begin() + 1; it != values.end(); ++it) if (compare(*it, best.get(), op)) replace(best, p::retain(*it));
    return best;
}

PyObject* advanceProjectiles(PyObject*, PyObject* args) {
    PyObject *battle, *now, *force_progress, *projectile_module, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &now, &force_progress, &projectile_module, &bot_module, &g)) return nullptr;
    try {
        if (truthy(method(projectile_module, "enabled", battle)))
            return method(projectile_module, "frame", battle, now, force_progress).release();
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get())))
            return method(bot_module, "projectile_advance", bots.get(), now, force_progress).release();
        setAttr(battle, "_projectile_perf", p::dict().get());
        setAttr(battle, "_projectile_scan_count", integer(0).get());
        setAttr(battle, "_projectile_candidate_count", integer(0).get());
        auto projectiles = attr(battle, "_projectiles");
        if (projectiles.get() == Py_None) Py_RETURN_FALSE;
        dispatch(battle, "_flush_pending_projectile_resolutions", "flush_pending_projectile_resolutions", {}, {g});
        auto previous = attr(battle, "_projectile_target_positions");
        auto states = method(attr(battle, "_projectiles").get(), "snapshot");
        if (!truthy(attr(battle, "_worker_mode")) && !truthy(states)) {
            p::Ref metas(PySequence_Tuple(method(attr(battle, "_projectile_meta").get(), "values").get()));
            for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(metas.get()); ++i)
                // Retain pose history while the sole reflected shell waits.
                if (method(PyTuple_GET_ITEM(metas.get(), i), "get", p::text("local_ricochet_admission").get()).get() != Py_None) Py_RETURN_FALSE;
            // A player's next shot begins locally and needs no history for someone else's delayed projectile.
            setAttr(battle, "_projectile_position_history", p::list().get());
            setAttr(battle, "_projectile_target_positions", p::dict().get());
            Py_RETURN_FALSE;
        }
        auto current_poses = dispatch(battle, "_projectile_record_poses", "projectile_record_poses", {}, {g});
        auto current = p::dict();
        {
            p::Ref iterator(PyObject_GetIter(method(current_poses.get(), "items").get()));
            while (PyObject* raw = PyIter_Next(iterator.get())) {
                p::Ref pair(raw);
                PyObject* pose = PyTuple_GET_ITEM(pair.get(), 1);
                p::Ref xyz(PyTuple_Pack(3, item(pose, "x").get(), item(pose, "y").get(), item(pose, "z").get()));
                if (PyObject_SetItem(current.get(), PyTuple_GET_ITEM(pair.get(), 0), xyz.get()) < 0) throw p::Error("position store failed");
            }
            if (PyErr_Occurred()) throw p::Error("poses iteration failed");
        }
        auto manager = attr(battle, "_projectiles");
        const Py_ssize_t pending = PyObject_Size(manager.get());
        if (pending < 0) throw p::Error("projectile manager size failed");
        if (pending > 0 || truthy(attr(battle, "_projectile_visual_meta")) || isAuthority(battle, g)) {
            dispatch(battle, "_sample_projectile_positions", "sample_projectile_positions", {now, current_poses.get()}, {g});
            dispatch(battle, "_prune_projectile_position_history", "prune_projectile_position_history", {states.get()}, {g});
        }
        if (!isAuthority(battle, g)) {
            setAttr(battle, "_projectile_target_positions", current.get());
            Py_RETURN_FALSE;
        }
        // The retired Python authority path: its chord and terminal callbacks stay the pinned methods.
        setAttr(battle, "_projectile_previous_positions", previous.get());
        setAttr(battle, "_projectile_current_positions", current.get());
        auto start = attr(manager.get(), "now");
        setAttr(battle, "_projectile_frame_start", start.get());
        setAttr(battle, "_projectile_frame_end", extreme({start.get(), pyFloat(toFloat(now)).get()}, Py_GT).get());
        auto active = integer(PyObject_Size(states.get()));
        auto sustainable = method(manager.get(), "sustainable_chord_budget", global(g, "PROJECTILE_SUSTAIN_SECONDS").get());
        p::Ref doubled(PyNumber_Multiply(active.get(), integer(2).get()));
        auto most = extreme({global(g, "PROJECTILE_CHORDS_PER_FRAME").get(), doubled.get(), sustainable.get()}, Py_GT);
        auto budget = extreme({global(g, "PROJECTILE_MAX_CHORDS_PER_FRAME").get(), most.get()}, Py_LT);
        auto clock = global(g, "_PROFILE_CLOCK");
        const double advance_start = toFloat(p::Ref(PyObject_CallObject(clock.get(), nullptr)).get());
        setAttr(battle, "_projectile_historic_pose_cache", p::dict().get());
        p::Ref advanced = p::retain(Py_None);
        {
            PyObject *type = nullptr, *value = nullptr, *trace = nullptr;
            bool failed = false;
            try {
                callKw(battle, "_build_projectile_spatial_bins", {states.get(), now}, {{"maximum_chords", budget.get()}});
                setAttr(battle, "_projectile_advance_active", Py_True);
                replace(advanced, callKw(manager.get(), "advance", {now, attr(battle, "_projectile_chord").get(),
                                                                    attr(battle, "_projectile_terminal").get()},
                                         {{"maximum_chords", budget.get()}}));
            } catch (const std::exception&) { failed = true; PyErr_Fetch(&type, &value, &trace); }
            // finally: the spatial index and pose cache never outlive this advance.
            setAttr(battle, "_projectile_advance_active", Py_False);
            method(battle, "_clear_projectile_spatial_index");
            setAttr(battle, "_projectile_historic_pose_cache", Py_None);
            if (failed) { PyErr_Restore(type, value, trace); throw p::Error("projectile advance failed"); }
        }
        // Manager restore is forbidden inside terminal callbacks; install queued continuations now.
        p::Ref metas(PySequence_Tuple(method(attr(battle, "_projectile_meta").get(), "values").get()));
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(metas.get()); ++i) {
            PyObject* meta = PyTuple_GET_ITEM(metas.get(), i);
            if (method(meta, "get", p::text("local_ricochet_admission").get()).get() != Py_None)
                method(battle, "_admit_local_projectile_ricochet", meta);
        }
        const double spent = toFloat(p::Ref(PyObject_CallObject(clock.get(), nullptr)).get()) - advance_start;
        auto metrics = method(manager.get(), "last_advance_metrics");
        auto perf = literal(7);
        auto metric = [&](const char* name, PyObject* fallback) { return method(metrics.get(), "get", p::text(name).get(), fallback); };
        put(perf.get(), "active", metric("active", active.get()));
        put(perf.get(), "chords", metric("chords", integer(0).get()));
        put(perf.get(), "debt", metric("debt_after", pyFloat(0.).get()));
        put(perf.get(), "advance", pyFloat(spent > 0. ? spent : 0.));
        put(perf.get(), "terminals", metric("terminals", integer(0).get()));
        put(perf.get(), "scans", attr(battle, "_projectile_scan_count"));
        put(perf.get(), "candidates", attr(battle, "_projectile_candidate_count"));
        setAttr(battle, "_projectile_perf", perf.get());
        dispatch(battle, "_prune_projectile_position_history", "prune_projectile_position_history", {}, {Py_None, g});
        setAttr(battle, "_projectile_target_positions", current.get());
        auto due = [&]() { return compare(now, attr(battle, "_next_projectile_progress_time").get(), Py_GE); };
        if (truthy(p::retain(force_progress)) || due()) {
            if (due()) {
                // Advance the deadline along the fixed cadence grid instead of restarting an interval.
                if (compare(attr(battle, "_next_projectile_progress_time").get(), pyFloat(0.).get(), Py_LE))
                    setAttr(battle, "_next_projectile_progress_time", pyFloat(toFloat(now)).get());
                const double overshoot_raw = toFloat(now) - toFloat(attr(battle, "_next_projectile_progress_time").get());
                const double overshoot = overshoot_raw > 0. ? overshoot_raw : 0.;
                auto cadence = global(g, "PROJECTILE_PROGRESS_SECONDS");
                const auto intervals = std::int64_t(std::floor((overshoot + 1e-9) / toFloat(cadence.get()))) + 1;
                p::Ref step(PyNumber_Multiply(integer(intervals).get(), cadence.get()));
                setAttr(battle, "_next_projectile_progress_time",
                        p::Ref(PyNumber_InPlaceAdd(attr(battle, "_next_projectile_progress_time").get(), step.get())).get());
            }
            method(battle, "_publish_projectile_progress");
        }
        return advanced.release();
    } catch (const std::exception& e) { return fail(e); }
}

bool overridden(PyObject* battle, const char* name) {
    PyObject** dict = _PyObject_GetDictPtr(battle);
    return dict && *dict && PyDict_GetItemString(*dict, name);
}
// self._projectile_wire_meta(raw): the instance override, else the native normalization.
p::Ref wireMeta(PyObject* battle, PyObject* raw, PyObject* g) {
    return overridden(battle, "_projectile_wire_meta") ? method(battle, "_projectile_wire_meta", raw) : projectileWireMeta(raw, g);
}
// BattleRuntime._projectile_launch_mismatches (staticmethod)
p::Ref launchMismatches(PyObject* battle, PyObject* current, PyObject* canonical, PyObject* g) {
    if (overridden(battle, "_projectile_launch_mismatches")) return method(battle, "_projectile_launch_mismatches", current, canonical);
    auto names = p::list();
    p::Ref iterator(PyObject_GetIter(global(g, "_PROJECTILE_IMMUTABLE_FIELDS").get()));
    while (PyObject* raw = PyIter_Next(iterator.get())) {
        p::Ref name(raw);
        if (compare(method(current, "get", name.get()).get(), method(canonical, "get", name.get()).get(), Py_NE)) p::append(names.get(), name);
    }
    if (PyErr_Occurred()) throw p::Error("immutable fields iteration failed");
    return p::Ref(PySequence_Tuple(names.get()));
}
p::Ref ensureVisual(PyObject* battle, PyObject* meta, PyObject* now, PyObject* g) {
    return dispatch(battle, "_ensure_projectile_visual", "ensure_projectile_visual", {meta, now}, {},
                    {meta, now, Py_None, module("gui.mods.offline_lan_0922.native_client_projectile").get(), g});
}
p::Ref owns(PyObject* battle, PyObject* meta, PyObject* g) { return dispatch(battle, "_owns_projectile", "owns_projectile", {meta}, {g}); }
p::Ref install(PyObject* battle, PyObject* normalized, PyObject* g) {
    return dispatch(battle, "_install_projectile_meta", "install_projectile_meta", {normalized}, {g});
}
p::Ref reportFailure(PyObject* battle, PyObject* meta, PyObject* stage, PyObject* reason, PyObject* error, PyObject* g) {
    auto state = p::dict();
    if (overridden(battle, "_report_projectile_terminal_failure")) {
        if (error) return method(battle, "_report_projectile_terminal_failure", meta, state.get(), stage, reason, error);
        return method(battle, "_report_projectile_terminal_failure", meta, state.get(), stage, reason);
    }
    return dispatch(battle, "_report_projectile_terminal_failure", "report_projectile_terminal_failure", {}, {},
                    {meta, state.get(), stage, reason, error ? error : Py_None, g});
}

PyObject* acceptProjectileEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event, *projectile_module, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &event, &projectile_module, &bot_module, &g)) return nullptr;
    try {
        if (truthy(method(projectile_module, "enabled", battle))) return method(projectile_module, "event", battle, event).release();
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get())))
            return method(bot_module, "projectile_event", bots.get(), event, method(battle, "_clock").get()).release();
        // Register one server-admitted launch on its projectile owner.
        if (!isAuthority(battle, g) || attr(battle, "_projectiles").get() == Py_None) Py_RETURN_FALSE;
        auto client_epoch = optionalAttr(attr(battle, "client").get(), "authority_epoch");
        auto epoch = method(event, "get", p::text("authority_epoch").get(), client_epoch.get());
        if (!truthy(dispatch(battle, "_set_projectile_epoch", "set_projectile_epoch", {epoch.get(), method(battle, "_clock").get()}, {g})))
            Py_RETURN_FALSE;
        auto normalized = wireMeta(battle, event, g);
        if (normalized.get() == Py_None) {
            auto stub_meta = literal(1);
            put(stub_meta.get(), "projectile_id", method(event, "get", p::text("projectile_id").get(), p::text("unknown").get()));
            reportFailure(battle, stub_meta.get(), p::text("canonical_echo").get(), p::text("invalid_cursor").get(), nullptr, g);
            Py_RETURN_FALSE;
        }
        if (!truthy(owns(battle, normalized.get(), g)) ||
            member(attr(battle, "_projectile_visual_terminals").get(), item(normalized.get(), "projectile_id").get()))
            Py_RETURN_FALSE;
        if (member(attr(battle, "_projectile_resolution_acks").get(), item(normalized.get(), "projectile_id").get())) {
            // A full snapshot can acknowledge our terminal while this launch waits in the ordered journal.
            p::Ref values(PyTuple_Pack(1, item(normalized.get(), "projectile_id").get()));
            writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] PROJECTILE LATE LAUNCH ACK id=%s\n").get(), values.get())).get());
            Py_RETURN_TRUE;
        }
        auto now = method(battle, "_clock");
        auto meta = method(battle, "_reconcile_provisional_projectile", normalized.get(), now.get());
        if (meta.get() == Py_False) Py_RETURN_FALSE;
        if (meta.get() == Py_None) replace(meta, install(battle, normalized.get(), g));
        auto projectile_id = item(normalized.get(), "projectile_id");
        auto manager_key = item(meta.get(), "manager_key");
        if (truthy(method(attr(battle, "_projectiles").get(), "contains", manager_key.get()))) Py_RETURN_TRUE;
        if (method(meta.get(), "get", p::text("pending_resolution").get()).get() != Py_None ||
            method(meta.get(), "get", p::text("pending_ricochet").get()).get() != Py_None ||
            truthy(method(meta.get(), "get", p::text("awaiting_resolution").get())) ||
            truthy(method(meta.get(), "get", p::text("awaiting_ricochet").get()))) {
            // Never manufacture a second launch for a retired manager identity awaiting settlement.
            method(battle, "_submit_projectile_ricochet", meta.get());
            method(battle, "_submit_projectile_resolution", meta.get());
            Py_RETURN_TRUE;
        }
        if (method(meta.get(), "get", p::text("local_ricochet_admission").get()).get() != Py_None) {
            // Keep the queued owner instead of restoring twice.
            method(battle, "_admit_local_projectile_ricochet", meta.get());
            Py_RETURN_TRUE;
        }
        if (!truthy(method(battle, "_admit_projectile_manager_state", normalized.get(), now.get()))) {
            method(attr(battle, "_projectile_meta").get(), "pop", projectile_id.get(), Py_None);
            raise("canonical projectile launch was not admitted");
        }
        if (!truthy(attr(battle, "_projectile_position_history"))) {
            auto poses = dispatch(battle, "_projectile_record_poses", "projectile_record_poses", {}, {g});
            dispatch(battle, "_sample_projectile_positions", "sample_projectile_positions", {now.get(), poses.get()}, {g});
            auto targets = p::dict();
            p::Ref iterator(PyObject_GetIter(method(poses.get(), "items").get()));
            while (PyObject* raw = PyIter_Next(iterator.get())) {
                p::Ref pair(raw);
                PyObject* pose = PyTuple_GET_ITEM(pair.get(), 1);
                p::Ref xyz(PyTuple_Pack(3, item(pose, "x").get(), item(pose, "y").get(), item(pose, "z").get()));
                if (PyObject_SetItem(targets.get(), PyTuple_GET_ITEM(pair.get(), 0), xyz.get()) < 0) throw p::Error("target store failed");
            }
            if (PyErr_Occurred()) throw p::Error("poses iteration failed");
            setAttr(battle, "_projectile_target_positions", targets.get());
        }
        put(meta.get(), "awaiting_resolution", p::retain(Py_False));
        put(meta.get(), "awaiting_ricochet", p::retain(Py_False));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// Replace the first segment only after the server commits its CAS.
PyObject* applyProjectileRicochetEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &event, &bot_module, &g)) return nullptr;
    try {
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get())))
            return method(bot_module, "projectile_event", bots.get(), event, method(battle, "_clock").get()).release();
        auto normalized = wireMeta(battle, event, g);
        if (normalized.get() == Py_None || compareInt(item(normalized.get(), "ricochet_count").get(), 1, Py_NE))
            raise("canonical projectile ricochet is malformed");
        auto projectile_id = item(normalized.get(), "projectile_id");
        auto meta = method(attr(battle, "_projectile_meta").get(), "get", projectile_id.get());
        if (member(attr(battle, "_projectile_visual_terminals").get(), projectile_id.get()) ||
            member(attr(battle, "_projectile_resolution_acks").get(), projectile_id.get()))
            Py_RETURN_FALSE;
        auto event_epoch = method(event, "get", p::text("authority_epoch").get());
        auto epoch = attr(battle, "_projectile_epoch");
        if (epoch.get() != Py_None && event_epoch.get() != Py_None && compare(event_epoch.get(), epoch.get(), Py_NE)) Py_RETURN_FALSE;
        if (meta.get() == Py_None) {
            // A non-owner may see a whole short flight between snapshots; restore its validated segment.
            replace(meta, install(battle, normalized.get(), g));
            p::Ref values(PyTuple_Pack(2, projectile_id.get(), owns(battle, meta.get(), g).get()));
            writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] PROJECTILE RICOCHET RESTORE id=%s owner=%s\n").get(),
                                               values.get())).get());
        }
        if (member(meta.get(), p::text("local_launch_time").get()) && truthy(owns(battle, meta.get(), g))) {
            auto mismatch = launchMismatches(battle, meta.get(), normalized.get(), g);
            if (truthy(mismatch)) {
                auto joined = method(p::text(",").get(), "join", mismatch.get());
                reportFailure(battle, meta.get(), p::text("canonical_echo").get(), p::text("launch_mismatch").get(), joined.get(), g);
                Py_RETURN_FALSE;
            }
        }
        if (compareInt(toInt(method(meta.get(), "get", p::text("ricochet_count").get(), integer(0).get()).get()).get(), 0, Py_EQ)) {
            put(meta.get(), "hit_vehicle", p::retain(Py_True));
            dispatch(battle, "_stop_projectile_visual", "stop_projectile_visual", {projectile_id.get(), event}, {g});
        }
        replace(meta, install(battle, normalized.get(), g));
        put(meta.get(), "awaiting_ricochet", p::retain(Py_False));
        put(meta.get(), "pending_ricochet", p::retain(Py_None));
        put(meta.get(), "presented_ricochet_count", item(normalized.get(), "ricochet_count"));
        put(meta.get(), "visual_segment_distance", item(normalized.get(), "checked_distance"));
        auto now = method(battle, "_clock");
        ensureVisual(battle, meta.get(), now.get(), g);
        if (isAuthority(battle, g))
            dispatch(battle, "_accept_projectile_event", "accept_projectile_event", {event}, {},
                     {event, module("gui.mods.offline_lan_0922.native_client_projectile").get(),
                      module("gui.mods.offline_lan_0922.native_bot_runtime").get(), g});
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* applyProjectileTerminalEvent(PyObject*, PyObject* args) {
    PyObject *battle, *event, *projectile_module, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &event, &projectile_module, &bot_module, &g)) return nullptr;
    try {
        if (truthy(method(projectile_module, "enabled", battle))) method(projectile_module, "event", battle, event);
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get())))
            return method(bot_module, "projectile_event", bots.get(), event, method(battle, "_clock").get()).release();
        auto raw_id = method(event, "get", p::text("projectile_id").get());
        if (raw_id.get() == Py_None) raise("projectile terminal event has no id");
        auto projectile_id = str(raw_id.get());
        auto meta = method(attr(battle, "_projectile_meta").get(), "get", projectile_id.get());
        if (meta.get() != Py_None && PyBool_Check(method(event, "get", p::text("hit_vehicle").get()).get())) {
            put(meta.get(), "hit_vehicle", item(event, "hit_vehicle"));
            auto visual = method(attr(battle, "_projectile_visual_meta").get(), "get", projectile_id.get());
            p::Ref elapsed = p::retain(Py_None);
            try {
                const double seconds = (toFloat(method(event, "get", p::text("resolved_time_ms").get()).get()) -
                                        toFloat(method(meta.get(), "get", p::text("segment_start_time_ms").get(), integer(0).get()).get())) / 1000.;
                replace(elapsed, pyFloat(seconds > 0. ? seconds : 0.));
            } catch (const std::exception&) {
                if (!PyErr_Occurred() || !conversionError()) throw;
                PyErr_Clear();
            }
            if (visual.get() != Py_None && elapsed.get() != Py_None) {
                auto velocity = item(visual.get(), "velocity");
                p::Ref fall(PyNumber_Multiply(item(visual.get(), "gravity").get(), elapsed.get()));
                p::Ref terminal(PyTuple_Pack(3, p::Ref(PyObject_GetItem(velocity.get(), integer(0).get())).get(),
                    p::Ref(PyNumber_Subtract(p::Ref(PyObject_GetItem(velocity.get(), integer(1).get())).get(), fall.get())).get(),
                    p::Ref(PyObject_GetItem(velocity.get(), integer(2).get())).get()));
                put(meta.get(), "terminal_velocity", std::move(terminal));
            }
        }
        if (PyDict_Check(method(event, "get", p::text("wreck_hit").get()).get()))
            dispatch(battle, "_present_projectile_wreck_hit", "present_projectile_wreck_hit", {projectile_id.get(), event}, {g});
        dispatch(battle, "_stop_projectile_visual", "stop_projectile_visual", {projectile_id.get(), event}, {g});
        // A same-epoch terminal fence keeps a duplicated snapshot row from resurrecting the tracer.
        method(attr(battle, "_projectile_visual_terminals").get(), "add", projectile_id.get());
        auto projectiles = attr(battle, "_projectiles");
        if (projectiles.get() != Py_None) {
            auto key = meta.get() != Py_None ? method(meta.get(), "get", p::text("manager_key").get()) : p::retain(projectile_id.get());
            method(projectiles.get(), "remove", key.get());
        }
        method(attr(battle, "_projectile_meta").get(), "pop", projectile_id.get(), Py_None);
        method(attr(battle, "_projectile_terminal_data").get(), "pop", projectile_id.get(), Py_None);
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// Retire one rejected own launch without refunding a fired round.
PyObject* onFireIntentResult(PyObject*, PyObject* args) {
    PyObject *battle, *message, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &message, &g)) return nullptr;
    try {
        if (truthy(attr(battle, "_worker_mode")) || !PyDict_Check(message)) Py_RETURN_FALSE;
        auto start = attr(battle, "_start_message");
        auto round = method(truthy(start) ? start.get() : p::dict().get(), "get", p::text("round_id").get());
        if (compare(method(message, "get", p::text("round_id").get()).get(), round.get(), Py_NE) ||
            method(message, "get", p::text("accepted").get()).get() != Py_False)
            Py_RETURN_FALSE;
        auto projectile_id = method(message, "get", p::text("projectile_id").get());
        auto meta = method(attr(battle, "_projectile_meta").get(), "get", projectile_id.get());
        if (meta.get() == Py_None || !truthy(owns(battle, meta.get(), g))) Py_RETURN_FALSE;
        PyObject* converted = PyNumber_Int(method(message, "get", p::text("intent_seq").get()).get());
        if (!converted) {
            if (!conversionError()) return nullptr;
            PyErr_Clear(); Py_RETURN_FALSE;
        }
        p::Ref sequence(converted);
        if (compare(sequence.get(), toInt(method(meta.get(), "get", p::text("fire_intent_seq").get(), integer(0).get()).get()).get(), Py_NE))
            Py_RETURN_FALSE;
        auto reason_raw = method(message, "get", p::text("reason").get(), p::text("rejected").get());
        auto reason = str(truthy(reason_raw) ? reason_raw.get() : p::text("rejected").get());
        reportFailure(battle, meta.get(), p::text("launch_rejected").get(), reason.get(), nullptr, g);
        dispatch(battle, "_discard_provisional_projectile", "discard_provisional_projectile", {projectile_id.get()}, {},
                 {projectile_id.get(), module("gui.mods.offline_lan_0922.native_client_projectile").get(), g});
        dispatch(battle, "_stop_projectile_visual", "stop_projectile_visual", {projectile_id.get(), p::dict().get()}, {g});
        method(attr(battle, "_projectile_visual_terminals").get(), "add", projectile_id.get());
        // Local gun state was committed at transport acceptance; a delayed rejection cannot rewind it.
        auto burst = attr(battle, "_local_player_burst");
        if (burst.get() != Py_None &&
            equal(method(burst.get(), "get", p::text("group_seq").get()).get(), method(meta.get(), "get", p::text("burst_group_seq").get()).get()))
            callKw(battle, "_cancel_local_player_burst", {}, {{"apply_settings", Py_True}});
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// The deferred local presentation; self is (battle, generation, round_id, projectile_id, frozen, globals).
PyObject* presentAfterMailboxReturns(PyObject* captured, PyObject*) {
    try {
        PyObject *battle = PyTuple_GET_ITEM(captured, 0), *generation = PyTuple_GET_ITEM(captured, 1), *round = PyTuple_GET_ITEM(captured, 2),
                 *projectile_id = PyTuple_GET_ITEM(captured, 3), *frozen = PyTuple_GET_ITEM(captured, 4), *g = PyTuple_GET_ITEM(captured, 5);
        auto start = attr(battle, "_start_message");
        auto current_round = method(truthy(start) ? start.get() : p::dict().get(), "get", p::text("round_id").get());
        auto presentations = attr(battle, "_local_projectile_presentations");
        if (compare(generation, attr(battle, "_generation").get(), Py_NE) || compare(round, current_round.get(), Py_NE) ||
            method(presentations.get(), "get", projectile_id).get() != frozen)
            Py_RETURN_NONE;
        method(attr(battle, "_local_projectile_presentations").get(), "pop", projectile_id, Py_None);
        if (!equal(attr(battle, "state").get(), p::text("running").get()) || attr(battle, "_server").get() == Py_None) Py_RETURN_NONE;
        auto record = recordsGet(battle, formatKey(p::text("player").get(), item(frozen, "shooter_id").get()).get());
        if (record.get() == Py_None || truthy(method(record.get(), "get", p::text("tombstone").get()))) Py_RETURN_NONE;
        try {
            dispatch(battle, "_show_shot", "show_shot", {frozen}, {}, {frozen, Py_True, g});
        } catch (const std::exception& e) {
            warnCaught(battle, "local shot presentation", e, true);
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}
PyMethodDef present_after_mailbox_returns = {"present_after_mailbox_returns", presentAfterMailboxReturns, METH_NOARGS,
                                             "Present one local shot after Avatar.shoot installs its waiting token."};

// Wait for Avatar.shoot to install its native waiting-for-shot token.
PyObject* deferLocalProjectilePresentation(PyObject*, PyObject* args) {
    PyObject *battle, *event, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &event, &g)) return nullptr;
    try {
        auto projectile_id = item(event, "projectile_id");
        auto generation = attr(battle, "_generation");
        auto start = attr(battle, "_start_message");
        auto round = method(truthy(start) ? start.get() : p::dict().get(), "get", p::text("round_id").get());
        auto frozen = method(module("copy").get(), "deepcopy", event);
        if (PyObject_SetItem(attr(battle, "_local_projectile_presentations").get(), projectile_id.get(), frozen.get()) < 0)
            throw p::Error("presentation store failed");
        p::Ref captured(PyTuple_Pack(6, battle, generation.get(), round.get(), projectile_id.get(), frozen.get(), g));
        p::Ref callback(PyCFunction_NewEx(&present_after_mailbox_returns, captured.get(), nullptr));
        method(attr(attr(battle, "_runtime").get(), "bigworld").get(), "callback", pyFloat(0.).get(), callback.get());
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// Restore the authoritative cursor without rescanning elapsed time.
PyObject* reconcileProjectileSnapshot(PyObject*, PyObject* args) {
    PyObject *battle, *message, *projectile_module, *bot_module, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &message, &projectile_module, &bot_module, &g)) return nullptr;
    try {
        if (truthy(method(projectile_module, "enabled", battle))) return method(projectile_module, "ingress", battle, message).release();
        auto bots = attr(battle, "_bots");
        if (bots.get() != Py_None && truthy(method(bot_module, "enabled", bots.get()))) {
            auto accepted = method(bot_module, "projectile_ingress", bots.get(), message, method(battle, "_clock").get());
            if (accepted.get() != Py_None) return accepted.release();
        }
        if (attr(battle, "_projectiles").get() == Py_None || !PyDict_Check(message)) Py_RETURN_FALSE;
        auto rows = method(message, "get", p::text("projectiles").get());
        if (!(PyList_Check(rows.get()) || PyTuple_Check(rows.get()))) Py_RETURN_FALSE;
        p::Ref reported(PySet_New(nullptr));
        const Py_ssize_t count = PySequence_Size(rows.get());
        for (Py_ssize_t i = 0; i < count; ++i) {
            p::Ref raw(PySequence_GetItem(rows.get(), i));
            if (PyDict_Check(raw.get()) && method(raw.get(), "get", p::text("projectile_id").get()).get() != Py_None)
                if (PySet_Add(reported.get(), str(item(raw.get(), "projectile_id").get()).get()) < 0) throw p::Error("id set failed");
        }
        auto now = method(battle, "_clock");
        p::Ref active(PySet_New(nullptr));
        auto normalized_rows = p::list();
        for (Py_ssize_t i = 0; i < count; ++i) {
            p::Ref raw(PySequence_GetItem(rows.get(), i));
            auto normalized = wireMeta(battle, raw.get(), g);
            if (normalized.get() == Py_None) {
                auto diagnostic = literal(1);
                put(diagnostic.get(), "projectile_id", PyDict_Check(raw.get()) ?
                    method(raw.get(), "get", p::text("projectile_id").get(), p::text("unknown").get()) : p::text("unknown"));
                reportFailure(battle, diagnostic.get(), p::text("canonical_snapshot").get(), p::text("invalid_cursor").get(), nullptr, g);
                continue;
            }
            auto projectile_id = item(normalized.get(), "projectile_id");
            // The ordered terminal can overtake an older snapshot; fence the row.
            if (member(attr(battle, "_projectile_visual_terminals").get(), projectile_id.get()) ||
                member(attr(battle, "_projectile_resolution_acks").get(), projectile_id.get()))
                continue;
            if (PySet_Add(active.get(), projectile_id.get()) < 0) throw p::Error("active set failed");
            auto installed = callKw(battle, "_reconcile_provisional_projectile", {normalized.get(), now.get()},
                                    {{"authoritative_snapshot", Py_True}});
            if (installed.get() == Py_False) continue;
            if (installed.get() == Py_None) replace(installed, install(battle, normalized.get(), g));
            p::append(normalized_rows.get(), normalized);
            ensureVisual(battle, installed.get(), now.get(), g);
        }
        if (!isAuthority(battle, g)) Py_RETURN_TRUE;
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(normalized_rows.get()); ++i) {
            PyObject* normalized = PyList_GET_ITEM(normalized_rows.get(), i);
            if (!truthy(owns(battle, normalized, g))) continue;
            auto meta = install(battle, normalized, g);
            if (truthy(method(attr(battle, "_projectiles").get(), "contains", item(meta.get(), "manager_key").get()))) continue;
            if (method(meta.get(), "get", p::text("pending_ricochet").get()).get() != Py_None) {
                // The unchanged first segment proves the server has not committed this ricochet; retry its CAS.
                put(meta.get(), "awaiting_ricochet", p::retain(Py_False));
                method(battle, "_submit_projectile_ricochet", meta.get());
                continue;
            }
            if (method(meta.get(), "get", p::text("pending_resolution").get()).get() != Py_None) {
                // Presence in the next snapshot means the terminal is uncommitted; retry once.
                put(meta.get(), "awaiting_resolution", p::retain(Py_False));
                method(battle, "_submit_projectile_resolution", meta.get());
                continue;
            }
            auto source = dispatch(battle, "_projectile_source_entity", "projectile_source_entity", {meta.get()});
            auto descriptor = source.get() != Py_None ? optionalAttr(source.get(), "typeDescriptor") : p::retain(Py_None);
            if (descriptor.get() == Py_None)
                replace(descriptor, dispatch(battle, "_projectile_source_descriptor", "projectile_source_descriptor", {meta.get()}));
            // Wait only when even the frozen canonical descriptor is unavailable.
            if (descriptor.get() == Py_None) continue;
            auto snapshot = method(battle, "_projectile_manager_snapshot", normalized, now.get());
            if (!truthy(callKw(attr(battle, "_projectiles").get(), "restore", {snapshot.get()}, {{"admission_time", now.get()}})))
                raise("active projectile restore failed");
        }
        p::Ref items(PySequence_Tuple(method(attr(battle, "_projectile_meta").get(), "items").get()));
        for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(items.get()); ++i) {
            PyObject* pair = PyTuple_GET_ITEM(items.get(), i);
            PyObject *projectile_id = PyTuple_GET_ITEM(pair, 0), *meta = PyTuple_GET_ITEM(pair, 1);
            auto get = [&](const char* name) { return method(meta, "get", p::text(name).get()); };
            const int present = PySet_Contains(active.get(), projectile_id);
            if (present < 0) throw p::Error("active lookup failed");
            if (present || truthy(get("local_launch_pending"))) continue;
            if (!(truthy(get("awaiting_resolution")) || get("pending_resolution").get() != Py_None ||
                  truthy(get("awaiting_ricochet")) || get("pending_ricochet").get() != Py_None))
                continue;
            const int was_reported = PySet_Contains(reported.get(), projectile_id);
            if (was_reported < 0) throw p::Error("reported lookup failed");
            // Missing from a full authoritative ledger acknowledges a sent terminal.
            if (truthy(get("awaiting_resolution")) && !was_reported &&
                truthy(method(attr(battle, "_projectile_resolution_acks").get(), "add", projectile_id))) {
                bool queued = false;
                p::Ref iterator(PyObject_GetIter(attr(battle, "_event_journal").get()));
                while (PyObject* raw = PyIter_Next(iterator.get())) {
                    p::Ref journal_item(raw);
                    if (member(global(g, "_SHOT_EVENT_KINDS").get(), method(journal_item.get(), "get", p::text("kind").get()).get()) &&
                        equal(str(method(journal_item.get(), "get", p::text("projectile_id").get()).get()).get(), projectile_id)) {
                        queued = true;
                        break;
                    }
                }
                if (PyErr_Occurred()) throw p::Error("journal iteration failed");
                p::Ref values(PyTuple_Pack(2, projectile_id, queued ? Py_True : Py_False));
                writeStdout(p::Ref(PyString_Format(p::text("[Offline LAN 0.9.22] PROJECTILE RESOLUTION ACK id=%s queued_launch=%s\n").get(),
                                                   values.get())).get());
            }
            method(attr(battle, "_projectile_meta").get(), "pop", projectile_id, Py_None);
            method(attr(battle, "_projectile_terminal_data").get(), "pop", projectile_id, Py_None);
        }
        PyObject* revision_raw = PyNumber_Int(method(message, "get", p::text("projectile_revision").get(), integer(-1).get()).get());
        p::Ref revision = integer(-1);
        if (revision_raw) replace(revision, p::Ref(revision_raw));
        else if (conversionError()) PyErr_Clear();
        else throw p::Error("revision conversion failed");
        auto current = attr(battle, "_projectile_revision");
        setAttr(battle, "_projectile_revision", (compare(revision.get(), current.get(), Py_GT) ? revision : current).get());
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef shot_methods[] = {
    {"on_fire_intent_result", onFireIntentResult, METH_VARARGS, "Retire one rejected own launch."},
    {"defer_local_projectile_presentation", deferLocalProjectilePresentation, METH_VARARGS, "Present a local shot after Avatar.shoot."},
    {"reconcile_projectile_snapshot", reconcileProjectileSnapshot, METH_VARARGS, "Restore authoritative projectile cursors."},
    {"accept_projectile_event", acceptProjectileEvent, METH_VARARGS, "Register one server-admitted launch on its owner."},
    {"apply_projectile_ricochet_event", applyProjectileRicochetEvent, METH_VARARGS, "Replace the first segment after the server CAS."},
    {"apply_projectile_terminal_event", applyProjectileTerminalEvent, METH_VARARGS, "Retire one projectile at its terminal."},
    {"flush_pending_projectile_resolutions", flushPendingProjectileResolutions, METH_VARARGS, "Submit queued authority resolutions."},
    {"prune_projectile_position_history", pruneProjectilePositionHistory, METH_VARARGS, "Drop collision poses no cursor can reach."},
    {"advance_projectiles", advanceProjectiles, METH_VARARGS, "Advance projectile presentation or the retired authority."},
    {"stop_projectile_visual", stopProjectileVisual, METH_VARARGS, "Retire one projectile visual at its impact."},
    {"ensure_projectile_visual_native_presentation", ensureProjectileVisualNativePresentation, METH_VARARGS,
     "Start one native visual per projectile segment."},
    {"ensure_projectile_visual", ensureProjectileVisual, METH_VARARGS, "Start one projectile visual on its owner."},
    {"projectile_explosion", projectileExplosion, METH_VARARGS, "The world explosion for one terminal impact."},
    {"present_projectile_wreck_hit", presentProjectileWreckHit, METH_VARARGS, "Play the armour-hit family on a retained wreck."},
    {"discard_provisional_projectile", discardProvisionalProjectile, METH_VARARGS, "Retract one provisional local launch."},
    {"observe_projectile_message", observeProjectileMessage, METH_VARARGS, "Anchor round-relative server time locally."},
    {"install_projectile_meta", installProjectileMeta, METH_VARARGS, "Install or advance one projectile meta."},
    {"projectile_record_poses", projectileRecordPoses, METH_VARARGS, "Collision poses of every projectile target."},
    {"projectile_visual_start", projectileVisualStart, METH_VARARGS, "The on-screen HP_gunFire start of a visual."},
    {"set_projectile_epoch", setProjectileEpoch, METH_VARARGS, "Fence one projectile authority epoch."},
    {"projectile_record_pose", projectileRecordPose, METH_VARARGS, "Freeze one hull, aim and mode sample."},
    {"sample_projectile_positions", sampleProjectilePositions, METH_VARARGS, "Retain one collision-pose history sample."},
    {"projectile_frozen_target", projectileFrozenTarget, METH_VARARGS, "A target view with agreeing outer and inner geometry."},
    {"projectile_live_critical_target", projectileLiveCriticalTarget, METH_VARARGS, "Pair live interior components with the exterior."},
    {"projectile_plain_pose", plainPosePort, METH_VARARGS, "Copy one canonical projectile pose."},
    {"projectile_boundary_pose", boundaryPosePort, METH_VARARGS, "Canonicalize one network or native pose."},
    {"projectile_visual_age", projectileVisualAge, METH_VARARGS, "Collision-confirmed age of one visual segment."},
    {"projectile_record_positions", projectileRecordPositions, METH_VARARGS, "Current positions of projectile targets."},
    {"projectile_vehicle_matrices", projectileVehicleMatrices, METH_VARARGS, "Body and chassis poses on the projectile timeline."},
    {"projectile_descriptor_at_pose", projectileDescriptorAtPose, METH_VARARGS, "The siege or default descriptor at a pose."},
    {"report_projectile_terminal_failure", reportProjectileTerminalFailure, METH_VARARGS, "Log one projectile-local failure."},
    {"confirm_bot_projectile_launch", confirmBotProjectileLaunch, METH_VARARGS, "Acknowledge admitted Bot launches in order."},
    {"projectile_wire_meta", projectileWireMetaPort, METH_VARARGS, "Normalize one projectile launch spelling."},
    {"admit_projectile_visual", admitProjectileVisual, METH_VARARGS, "Ask the presenter for cosmetic capacity."},
    {"show_local_shot_without_extra", showLocalShotWithoutExtra, METH_VARARGS, "Stock local shot state without another effect."},
    {"shot_muzzle_drawn", shotMuzzleDrawn, METH_VARARGS, "Whether the stock muzzle belongs to a drawable shooter."},
    {"show_replay_local_muzzle", showReplayLocalMuzzle, METH_VARARGS, "Replay the native shooting extra."},
    {"reset_projectile_visuals", resetProjectileVisuals, METH_VARARGS, "Fence a projectile epoch change."},
    {"report_player_shot_timing", reportPlayerShotTiming, METH_VARARGS, "Log one player shot's timing."},
    {"projectile_is_authority", projectileIsAuthority, METH_VARARGS, "Whether this runtime evaluates projectiles."},
    {"owns_projectile", ownsProjectile, METH_VARARGS, "Whether this runtime is the one evaluator of a shell."},
    {"projectile_estimated_server_time", projectileEstimatedServerTime, METH_VARARGS, "Server time estimated from the last anchor."},
    {"new_projectile_meta", newProjectileMeta, METH_VARARGS, "A projectile meta for one normalized launch."},
    {"projectile_source_descriptor", projectileSourceDescriptor, METH_VARARGS, "The shooter's descriptor, cached on the meta."},
    {"projectile_attacker_engine_id", projectileAttackerEngineId, METH_VARARGS, "The shooter's engine id for hit sounds."},
    {"projectile_source_entity", projectileSourceEntity, METH_VARARGS, "The shooter's live entity."},
    {"remember_fire_timeline", rememberFireTimeline, METH_VARARGS, "Record one bounded per-shot timestamp."},
    {"projectile_shot", projectileShot, METH_VARARGS, "The frozen shot with descriptor-owned cosmetic keys."},
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* shots_methods() { return shot_methods; }
}
