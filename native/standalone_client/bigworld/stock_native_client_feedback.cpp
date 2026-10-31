// Native ports of the pinned BattleRuntime hit, shell and kill feedback helpers. `g` is the
// runtime's globals(); calls the pinned code makes through `self` keep their instance overrides.
#if !defined(WOT_CLIENT_LIFECYCLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_lifecycle_python.hpp"

namespace stock_native_client_lifecycle {
namespace feedback {
namespace {
using namespace py;

p::Ref none() { return p::retain(Py_None); }
p::Ref flt(double v) { return p::Ref(PyFloat_FromDouble(v)); }
p::Ref boolean(bool v) { return p::retain(v ? Py_True : Py_False); }
bool test(PyObject* a, PyObject* b, int op) {
    const int result = PyObject_RichCompareBool(a, b, op);
    if (result < 0) throw p::Error("feedback comparison failed");
    return result != 0;
}
// getattr(o, name, fallback)
p::Ref attrOr(PyObject* o, const char* name, PyObject* fallback) {
    PyObject* v = PyObject_GetAttrString(o, name);
    if (v) return p::Ref(v);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
// Module helper _field: dict.get or getattr with a default.
p::Ref field(PyObject* value, const char* name, PyObject* fallback = Py_None) {
    if (PyDict_Check(value)) return method(value, "get", p::text(name).get(), fallback);
    return attrOr(value, name, fallback);
}
// value.get(name[, fallback]) through the mapping's own get.
p::Ref valueGet(PyObject* mapping, const char* name, PyObject* fallback = nullptr) {
    return fallback ? method(mapping, "get", p::text(name).get(), fallback) : method(mapping, "get", p::text(name).get());
}
// `x or {}`
p::Ref orDict(p::Ref v) { return truthy(v) ? std::move(v) : p::dict(); }
// int(x), with the conversion errors Python raises.
p::Ref asInt(PyObject* v) { return p::Ref(PyNumber_Int(v)); }
// int(x or 0)
p::Ref intOrZero(PyObject* v) { return asInt(p::flag(v) ? v : integer(0).get()); }
void write(const char* format, p::Ref args) {
    writeStdout(p::Ref(PyString_Format(p::text(format).get(), args.get())).get());
}
// `except <type> as error:` runner; leaves the error set when `type` matches, rethrows otherwise.
template<class Body> bool contained(PyObject* type, Body body) {
    try { body(); return true; }
    catch (const std::exception& e) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what());
        if (!PyErr_ExceptionMatches(type)) throw;
        return false;
    }
}
// Module helper _xyz, through the module's own semantics.
p::Ref xyz(PyObject* g, PyObject* value) { return p::Ref(PyObject_CallFunctionObjArgs(global(g, "_xyz").get(), value, nullptr)); }
p::Ref formatXyz(PyObject* g, PyObject* value) {
    return p::Ref(PyObject_CallFunctionObjArgs(global(g, "_format_xyz").get(), value, nullptr));
}

// BattleRuntime._projectile_cosmetic_allowed(event, visual_meta) (staticmethod)
PyObject* projectileCosmeticAllowed(PyObject*, PyObject* args) {
    PyObject *event, *visual_meta, *g;
    if (!PyArg_ParseTuple(args, "OOO", &event, &visual_meta, &g)) return nullptr;
    try {
        auto projectile_id = PyDict_Check(event) ? valueGet(event, "projectile_id") : none();
        if (projectile_id.get() == Py_None) Py_RETURN_TRUE;
        auto visual = method(visual_meta, "get", p::Ref(PyObject_Str(projectile_id.get())).get());
        return boolean(visual.get() == Py_None || truthy(valueGet(visual.get(), "admitted", Py_True))).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._hit_damage_factor(event, target_record): hull damage as a percent of maximum health.
PyObject* hitDamageFactor(PyObject*, PyObject* args) {
    PyObject *battle, *event, *target_record, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &event, &target_record, &g)) return nullptr;
    try {
        auto damage = positive(intOrZero(valueGet(event, "damage", integer(0).get()).get()));
        if (!greaterThanZero(damage.get())) return flt(0.0).release();
        auto state = orDict(valueGet(target_record, "state"));
        double maximum = number(valueGet(state.get(), "max_health").get(), 0.0);
        if (maximum <= 0.0) {
            auto target = serverEntity(battle, valueGet(target_record, "engine_id").get());
            auto zero = flt(0.0);
            maximum = number(field(attrOr(target.get(), "typeDescriptor", Py_None).get(), "maxHealth", zero.get()).get(), 0.0);
        }
        if (maximum <= 0.0) return flt(0.0).release();
        const double scaled = 100.0 * PyFloat_AsDouble(damage.get()) / maximum;
        if (PyErr_Occurred()) throw p::Error("damage");
        const double capped = scaled < 100.0 ? scaled : 100.0;
        return flt(capped > 0.0 ? capped : 0.0).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._impact_sound_identity(target_record, attacker_id, direction, damage_factor)
PyObject* impactSoundIdentity(PyObject*, PyObject* args) {
    PyObject *battle, *target_record, *attacker_id, *direction, *damage_factor, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &target_record, &attacker_id, &direction, &damage_factor, &g)) return nullptr;
    try {
        auto player_id = intOrZero(attrOr(attr(battle, "_avatar").get(), "playerVehicleID", integer(0).get()).get());
        auto lookup = attrOr(attr(attr(battle, "_runtime").get(), "bigworld").get(), "entity", Py_None);
        if (!greaterThanZero(player_id.get()) || !PyCallable_Check(lookup.get()) ||
            p::Ref(PyObject_CallFunctionObjArgs(lookup.get(), player_id.get(), nullptr)).get() == Py_None)
            return PyDict_New();
        auto attacker = intOr(p::flag(attacker_id) ? attacker_id : integer(0).get(), 0);
        auto out = literal(5);
        put(out.get(), "isPlayerVehicle", boolean(truthy(valueGet(target_record, "local"))));
        put(out.get(), "entity_id", intOrZero(valueGet(target_record, "engine_id").get()));
        put(out.get(), "attackerID", attacker);
        put(out.get(), "damageFactor", p::retain(damage_factor));
        put(out.get(), "hitdir", p::retain(direction));
        return out.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._surface_effect_material(impact): one EFFECT_MATERIALS name, or None.
PyObject* surfaceEffectMaterial(PyObject*, PyObject* args) {
    PyObject *battle, *impact, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &impact, &g)) return nullptr;
    try {
        auto runtime = attr(battle, "_runtime");
        auto calculation = attrOr(runtime.get(), "effect_material_calculation", Py_None);
        auto materials = attrOr(runtime.get(), "material_kinds", Py_None);
        if (calculation.get() == Py_None || materials.get() == Py_None) Py_RETURN_NONE;
        p::Ref result = none();
        if (!contained(PyExc_Exception, [&] {
                auto at = method(battle, "_vector", xyz(g, impact).get());
                p::Ref up(Py_BuildValue("(ddd)", 0.0, 1.0, 0.0));
                auto normal = method(battle, "_vector", up.get());
                auto surface = method(calculation.get(), "calcSurfaceMaterialNearPoint", at.get(), normal.get(),
                                      attr(attr(battle, "_avatar").get(), "spaceID").get());
                auto index = attr(surface.get(), "effectIdx");
                if (index.get() == Py_None) return;
                replace(result, p::Ref(PyObject_GetItem(attr(materials.get(), "EFFECT_MATERIALS").get(), asInt(index.get()).get())));
            }))
            PyErr_Clear();
        return result.release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._release_target_lock(engine_id)
PyObject* releaseTargetLock(PyObject*, PyObject* args) {
    PyObject *battle, *engine_id, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &engine_id, &g)) return nullptr;
    try {
        auto release = attrOr(attr(attr(battle, "_runtime").get(), "compatibility").get(), "release_target_lock", Py_None);
        if (!PyCallable_Check(release.get()) || attr(battle, "_avatar").get() == Py_None) Py_RETURN_FALSE;
        p::Ref result(PyObject_CallFunctionObjArgs(release.get(), attr(battle, "_avatar").get(), engine_id, nullptr));
        return boolean(truthy(result)).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._event_entity_key(event, role) (staticmethod)
PyObject* eventEntityKeyPort(PyObject*, PyObject* args) {
    PyObject *event, *role, *g;
    if (!PyArg_ParseTuple(args, "OOO", &event, &role, &g)) return nullptr;
    try {
        const bool attacker = test(role, p::text("attacker").get(), Py_EQ);
        for (const auto& pair : {std::make_pair(attacker ? "attacker_bot" : "target_bot", "bot:%s"),
                                 std::make_pair(attacker ? "attacker" : "target", "player:%s")}) {
            auto value = valueGet(event, pair.first);
            if (value.get() != Py_None)
                return PyString_Format(p::text(pair.second).get(), valueGet(event, pair.first).get());
        }
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}
// self.<static method>(*args): an instance override, else the static port with `trailing` appended.
p::Ref staticPort(PyObject* battle, const char* name, PyCFunction port, std::initializer_list<PyObject*> args,
                  std::initializer_list<PyObject*> trailing) {
    PyObject** dict = _PyObject_GetDictPtr(battle);
    if (dict && *dict && PyDict_GetItemString(*dict, name)) return callKw(battle, name, args, {});
    p::Ref tuple(PyTuple_New(Py_ssize_t(args.size() + trailing.size())));
    Py_ssize_t i = 0;
    for (PyObject* v : args) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    for (PyObject* v : trailing) { Py_INCREF(v); PyTuple_SET_ITEM(tuple.get(), i++, v); }
    return p::Ref(port(nullptr, tuple.get()));
}
p::Ref eventKey(PyObject* battle, PyObject* event, const char* role, PyObject* g) {
    return staticPort(battle, "_event_entity_key", eventEntityKeyPort, {event, p::text(role).get()}, {g});
}

// BattleRuntime._report_effect(kind, material, effects_index, where, direction, event=None)
PyObject* reportEffect(PyObject*, PyObject* args) {
    PyObject *battle, *kind, *material, *index, *where, *direction, *event, *g;
    if (!PyArg_ParseTuple(args, "OOOOOOOO", &battle, &kind, &material, &index, &where, &direction, &event, &g)) return nullptr;
    try {
        if (test(attr(battle, "_effect_reports").get(), attr(battle, "_EFFECT_REPORT_LIMIT").get(), Py_GE)) Py_RETURN_FALSE;
        setAttr(battle, "_effect_reports", p::Ref(PyNumber_InPlaceAdd(attr(battle, "_effect_reports").get(), integer(1).get())).get());
        auto row = [&] { return p::flag(event) ? p::retain(event) : p::dict(); };
        p::Ref values[] = {p::retain(kind), p::Ref(PyObject_Repr(material)), p::Ref(PyObject_Repr(index)), formatXyz(g, where),
                           formatXyz(g, direction), valueGet(row().get(), "event_id"), valueGet(row().get(), "projectile_id"),
                           eventKey(battle, row().get(), "target", g), valueGet(row().get(), "damage"),
                           valueGet(row().get(), "shot_result")};
        p::Ref tuple(PyTuple_New(10));
        for (Py_ssize_t i = 0; i < 10; ++i) PyTuple_SET_ITEM(tuple.get(), i, values[i].release());
        write("[Offline LAN 0.9.22] EFFECT %s material=%s index=%s at=%s "
              "dir=%s event=%s projectile=%s target=%s damage=%s result=%s\n", std::move(tuple));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._report_hit_impulse(target_record, direction, impulse, presented)
PyObject* reportHitImpulse(PyObject*, PyObject* args) {
    PyObject *battle, *target_record, *direction, *impulse, *presented, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &target_record, &direction, &impulse, &presented, &g)) return nullptr;
    try {
        if (test(attr(battle, "_hit_impulse_reports").get(), attr(battle, "_HIT_IMPULSE_REPORT_LIMIT").get(), Py_GE)) Py_RETURN_FALSE;
        setAttr(battle, "_hit_impulse_reports",
                p::Ref(PyNumber_InPlaceAdd(attr(battle, "_hit_impulse_reports").get(), integer(1).get())).get());
        auto zero = flt(0.0);
        p::Ref values[] = {valueGet(target_record, "engine_id"), boolean(truthy(valueGet(target_record, "local"))),
                           flt(number(attrOr(direction, "x", zero.get()).get())), flt(number(attrOr(direction, "y", zero.get()).get())),
                           flt(number(attrOr(direction, "z", zero.get()).get())), p::retain(impulse), boolean(p::flag(presented))};
        p::Ref tuple(PyTuple_New(7));
        for (Py_ssize_t i = 0; i < 7; ++i) PyTuple_SET_ITEM(tuple.get(), i, values[i].release());
        write("[Offline LAN 0.9.22] IMPULSE id=%s local=%s dir=(%.3f, %.3f, "
              "%.3f) impulse=%.1f presented=%s\n", std::move(tuple));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._decoded_hit_impulse_direction(event, target_record): retail's armour-segment axis.
PyObject* decodedHitImpulseDirection(PyObject*, PyObject* args) {
    PyObject *battle, *event, *target_record, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &event, &target_record, &g)) return nullptr;
    try {
        if (!member(event, p::text("damage_sticker").get())) Py_RETURN_NONE;
        auto target = serverEntity(battle, valueGet(target_record, "engine_id").get());
        auto appearance = attrOr(target.get(), "appearance", Py_None);
        auto descriptor = attrOr(target.get(), "typeDescriptor", Py_None);
        auto compound = attrOr(appearance.get(), "compoundModel", Py_None);
        if (target.get() == Py_None || !truthy(attrOr(target.get(), "isStarted", Py_False)) || descriptor.get() == Py_None ||
            compound.get() == Py_None)
            Py_RETURN_NONE;
        p::Ref result = none();
        if (contained(PyExc_Exception, [&] {
                auto decoder = attr(p::Ref(PyImport_ImportModule("VehicleEffects")).get(), "DamageFromShotDecoder");
                auto decoded = unpack(method(decoder.get(), "decodeSegment",
                                             p::Ref(PyObject_GetItem(event, p::text("damage_sticker").get())).get(),
                                             descriptor.get()).get(), 4);
                if (!truthy(decoded[0]) || decoded[2].get() == Py_None || decoded[3].get() == Py_None ||
                    test(decoded[2].get(), decoded[3].get(), Py_EQ))
                    return;
                auto node = method(compound.get(), "node", decoded[0].get());
                if (node.get() == Py_None) return;
                auto local = p::Ref(PyNumber_Subtract(decoded[3].get(), decoded[2].get()));
                auto threshold = flt(0.001);
                if (test(attr(local.get(), "length").get(), threshold.get(), Py_LE)) return;
                auto math = attr(attr(battle, "_runtime").get(), "math");
                auto matrix = method(math.get(), "Matrix", node.get());
                auto applied = method(matrix.get(), "applyVector", local.get());
                auto direction = method(math.get(), "Vector3", applied.get());
                if (test(attr(direction.get(), "length").get(), threshold.get(), Py_LE)) return;
                method(direction.get(), "normalise");
                replace(result, std::move(direction));
            }))
            return result.release();
        auto error = caught(std::runtime_error("hit impulse"));
        callKw(battle, "_warn_optional_failure", {p::text("vehicle hit impulse").get(), error.get()}, {{"disable", Py_False}});
        Py_RETURN_NONE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._present_hit_impulse(event, target_record, effects_descr, splash_direction=None)
PyObject* presentHitImpulse(PyObject*, PyObject* args) {
    PyObject *battle, *event, *target_record, *effects, *splash_direction, *g;
    if (!PyArg_ParseTuple(args, "OOOOOO", &battle, &event, &target_record, &effects, &splash_direction, &g)) return nullptr;
    try {
        auto state = orDict(valueGet(target_record, "state"));
        const bool splash = truthy(valueGet(event, "splash", Py_False));
        if (truthy(valueGet(event, "dead", Py_False))) Py_RETURN_FALSE;
        if (!truthy(valueGet(state.get(), "alive", Py_True)) ||
            !greaterThanZero(intOrZero(valueGet(state.get(), "health", integer(1).get()).get()).get()))
            Py_RETURN_FALSE;
        auto zero = flt(0.0);
        double impulse = number(field(effects, "targetImpulse", zero.get()).get());
        if (impulse <= 0.0) Py_RETURN_FALSE;
        p::Ref direction = none();
        if (splash) {
            impulse = impulse / 4.0;
            replace(direction, p::retain(splash_direction));
        } else {
            replace(direction, dispatch(battle, "_decoded_hit_impulse_direction", "decoded_hit_impulse_direction",
                                        {event, target_record}, {g}));
        }
        if (direction.get() == Py_None) Py_RETURN_FALSE;
        auto impulse_value = flt(impulse);
        p::Ref presented = none();
        if (truthy(valueGet(target_record, "local"))) {
            // The matching local stock-animator rebind owns this capability.
            auto appearance = method(battle, "_local_vehicle_appearance");
            auto animator = attrOr(battle, "_local_swinging_animator", Py_None);
            if (animator.get() == Py_None ||
                !truthy(dispatch(battle, "_optional_feature_enabled", "optional_feature_enabled", {p::text("local body swinging").get()})) ||
                attrOr(appearance.get(), "swingingAnimator", Py_None).get() != animator.get())
                Py_RETURN_FALSE;
            replace(presented, p::Ref(PyObject_CallFunctionObjArgs(global(g, "present_shot_impulse").get(),
                attr(attr(battle, "_runtime").get(), "math").get(), appearance.get(), direction.get(), impulse_value.get(), nullptr)));
        } else if (truthy(valueGet(target_record, "native_remote"))) {
            auto present = attrOr(attr(battle, "_remote_factory").get(), "present_hit_impulse", Py_None);
            if (!PyCallable_Check(present.get())) Py_RETURN_FALSE;
            replace(presented, p::Ref(PyObject_CallFunctionObjArgs(present.get(),
                asInt(p::Ref(PyObject_GetItem(target_record, p::text("engine_id").get())).get()).get(), direction.get(),
                impulse_value.get(), nullptr)));
        } else {
            Py_RETURN_FALSE;
        }
        dispatch(battle, "_report_hit_impulse", "report_hit_impulse", {target_record, direction.get(), impulse_value.get(), presented.get()},
                 {g});
        return boolean(truthy(presented)).release();
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._event_shell(attacker_record, event): the attacker's (shot, shell) for this event.
PyObject* eventShell(PyObject*, PyObject* args) {
    PyObject *battle, *attacker_record, *event, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &attacker_record, &event, &g)) return nullptr;
    try {
        auto entity = serverEntity(battle, p::Ref(PyObject_GetItem(attacker_record, p::text("engine_id").get())).get());
        if (entity.get() == Py_None || attr(entity.get(), "typeDescriptor").get() == Py_None)
            raise("combat attacker descriptor is unavailable");
        auto empty = p::dict();
        auto gun = field(attr(entity.get(), "typeDescriptor").get(), "gun", empty.get());
        auto no_shots = p::Ref(PyTuple_New(0));
        auto raw = field(gun.get(), "shots", no_shots.get());
        auto shots = p::Ref(PySequence_Tuple(truthy(raw) ? raw.get() : no_shots.get()));
        const Py_ssize_t count = PyTuple_GET_SIZE(shots.get());
        if (!count) raise("combat attacker has no shell descriptors");
        auto requested = intOrZero(valueGet(event, "shell_index", integer(0).get()).get());
        auto last = integer(count - 1);
        // max(0, min(index, len(shots) - 1))
        auto bounded = test(last.get(), requested.get(), Py_LT) ? p::retain(last.get()) : p::retain(requested.get());
        auto index = greaterThanZero(bounded.get()) ? std::move(bounded) : integer(0);
        auto shot = p::Ref(PyObject_GetItem(shots.get(), index.get()));
        auto shell = field(shot.get(), "shell", Py_None);
        if (shell.get() == Py_None) raise("combat attacker shell is unavailable");
        return PyTuple_Pack(2, shot.get(), shell.get());
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._shell_is_gold(shell): the raw item definitions' gold price flag.
PyObject* shellIsGold(PyObject*, PyObject* args) {
    PyObject *battle, *shell, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &shell, &g)) return nullptr;
    try {
        auto identity = field(shell, "id", Py_None);
        auto name = field(shell, "name", Py_None);
        if (name.get() == Py_None || !(PyTuple_Check(identity.get()) || PyList_Check(identity.get())) ||
            PyObject_Size(identity.get()) < 1)
            Py_RETURN_FALSE;
        auto names = attrOr(attrOr(attr(battle, "_runtime").get(), "nations", Py_None).get(), "NAMES", Py_None);
        if (!truthy(names)) Py_RETURN_FALSE;
        p::Ref nation = none();
        PyObject* key = nullptr;
        PyObject* first = PySequence_GetItem(identity.get(), 0);
        if (first) { key = PyNumber_Int(first); Py_DECREF(first); }
        PyObject* found = key ? PyObject_GetItem(names.get(), key) : nullptr;
        Py_XDECREF(key);
        if (!found) {
            if (PyErr_ExceptionMatches(PyExc_IndexError) || PyErr_ExceptionMatches(PyExc_KeyError) ||
                PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError)) {
                PyErr_Clear();
                Py_RETURN_FALSE;
            }
            return nullptr;
        }
        replace(nation, p::Ref(found));
        return PyObject_CallFunctionObjArgs(global(g, "is_gold_shell").get(), nation.get(), name.get(), nullptr);
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._report_feedback_shell_failure(reason): one bounded diagnostic per distinct shell.
PyObject* reportFeedbackShellFailure(PyObject*, PyObject* args) {
    PyObject *battle, *reason, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &reason, &g)) return nullptr;
    try {
        auto failures = attr(battle, "_feedback_shell_failures");
        if (member(failures.get(), reason)) Py_RETURN_FALSE;
        const Py_ssize_t size = PyObject_Size(failures.get());
        if (size < 0) throw p::Error("len");
        if (size >= 32) Py_RETURN_FALSE;
        method(failures.get(), "add", reason);
        write("[Offline LAN 0.9.22] FEEDBACK damage-log shell unresolved: %s\n", p::Ref(PyTuple_Pack(1, reason)));
        Py_RETURN_TRUE;
    } catch (const std::exception& e) { return fail(e); }
}

PyObject* combatEventSourcePort(PyObject*, PyObject* args);

// BattleRuntime._feedback_shell_fields(event, attacker_record): (#1513 shell type index, gold).
PyObject* feedbackShellFields(PyObject*, PyObject* args) {
    PyObject *battle, *event, *attacker_record, *g;
    if (!PyArg_ParseTuple(args, "OOOO", &battle, &event, &attacker_record, &g)) return nullptr;
    try {
        auto runtime = attr(battle, "_runtime");
        auto common = attrOr(runtime.get(), "battle_feedback_common", Py_None);
        auto sentinel = attrOr(common.get(), "NONE_SHELL_TYPE", Py_None);
        if (sentinel.get() == Py_None) raise("#1513 shell-type feedback sentinel is unavailable");
        auto none_shell = asInt(sentinel.get());
        auto fallback = [&] { return PyTuple_Pack(2, none_shell.get(), Py_False); };
        if (attacker_record == Py_None ||
            !test(staticPort(battle, "_combat_event_source", combatEventSourcePort, {event}, {g}).get(), p::text("shot").get(), Py_EQ) ||
            !textIn(valueGet(event, "kind").get(), {"hit", "bot_hit", "bot_human_hit", "bot_bot_hit"}))
            return fallback();
        auto indices = attrOr(attr(runtime.get(), "constants").get(), "SHELL_TYPES_INDICES", Py_None);
        if (!truthy(indices)) raise("#1513 shell type indices are unavailable");
        p::Ref shell = none();
        if (!contained(PyExc_RuntimeError, [&] {
                auto pair = unpack(dispatch(battle, "_event_shell", "event_shell", {attacker_record, event}, {g}).get(), 2);
                replace(shell, std::move(pair[1]));
            })) {
            // _present_combat_hit owns the fatal shell contract; the log column must not cost the hit.
            auto error = caught(std::runtime_error("event shell"));
            dispatch(battle, "_report_feedback_shell_failure", "report_feedback_shell_failure", {str(error.get()).get()}, {g});
            return fallback();
        }
        auto kind = field(shell.get(), "kind", Py_None);
        if (kind.get() == Py_None) replace(kind, field(field(shell.get(), "type", Py_None).get(), "name", Py_None));
        if (!member(indices.get(), kind.get())) {
            p::Ref reason(PyString_Format(p::text("shell kind is not a #1513 shell type: %r").get(),
                                          p::Ref(PyTuple_Pack(1, kind.get())).get()));
            dispatch(battle, "_report_feedback_shell_failure", "report_feedback_shell_failure", {reason.get()}, {g});
            return fallback();
        }
        auto index = asInt(p::Ref(PyObject_GetItem(indices.get(), kind.get())).get());
        auto gold = dispatch(battle, "_shell_is_gold", "shell_is_gold", {shell.get()}, {g});
        return PyTuple_Pack(2, index.get(), gold.get());
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._fallback_postmortem_viewpoint(excluded_engine_id): move off a dead observed ally.
PyObject* fallbackPostmortemViewpoint(PyObject*, PyObject* args) {
    PyObject *battle, *excluded, *g;
    if (!PyArg_ParseTuple(args, "OOO", &battle, &excluded, &g)) return nullptr;
    try {
        if (test(attr(battle, "_spectated_engine_id").get(), asInt(excluded).get(), Py_NE)) Py_RETURN_FALSE;
        auto origin = attr(battle, "_local_position");
        p::Ref candidates(PyList_New(0));
        auto values = method(attr(battle, "_records").get(), "values");
        p::Ref iterator(PyObject_GetIter(values.get()));
        auto two = integer(2);
        while (PyObject* raw = PyIter_Next(iterator.get())) {
            p::Ref record(raw);
            auto engine_id = intOrZero(valueGet(record.get(), "engine_id", integer(0).get()).get());
            if (!truthy(engine_id) || test(engine_id.get(), asInt(excluded).get(), Py_EQ)) continue;
            auto pair = unpack(method(battle, "_spectator_record", engine_id.get()).get(), 2);
            if (pair[0].get() == Py_None) continue;
            auto position = xyz(g, attr(pair[1].get(), "position").get());
            auto axis = [&](Py_ssize_t i) {
                p::Ref delta(PyNumber_Subtract(p::Ref(PyObject_GetItem(position.get(), integer(i).get())).get(),
                                               p::Ref(PyObject_GetItem(origin.get(), integer(i).get())).get()));
                return p::Ref(PyNumber_Power(delta.get(), two.get(), Py_None));
            };
            auto dx = axis(0);
            auto dz = axis(2);
            p::Ref distance(PyNumber_Add(dx.get(), dz.get()));
            p::append(candidates.get(), p::Ref(PyTuple_Pack(2, distance.get(), engine_id.get())));
        }
        if (PyErr_Occurred()) throw p::Error("records");
        if (PyList_Sort(candidates.get()) < 0) throw p::Error("sort");
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(candidates.get()); ++i) {
            PyObject* engine_id = PyTuple_GET_ITEM(PyList_GET_ITEM(candidates.get(), i), 1);
            if (truthy(method(battle, "_switch_postmortem_viewpoint", Py_False, engine_id))) Py_RETURN_TRUE;
        }
        auto server = attr(battle, "_server");
        auto local_id = server.get() != Py_None ? asInt(attr(server.get(), "vehicle_id").get()) : integer(0);
        if (truthy(local_id) && test(local_id.get(), asInt(excluded).get(), Py_NE))
            return method(battle, "_switch_postmortem_viewpoint", Py_False, local_id.get()).release();
        method(battle, "_release_postmortem_visibility");
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._diagnostic_int / _diagnostic_vehicle_info / _diagnostic_value
p::Ref diagnosticInt(PyObject* value) {
    PyObject* converted = PyNumber_Int(value);
    if (converted) return p::Ref(converted);
    if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError) &&
        !PyErr_ExceptionMatches(PyExc_OverflowError))
        throw p::Error("diagnostic int");
    PyErr_Clear();
    return none();
}
std::pair<p::Ref, p::Ref> diagnosticVehicleInfo(PyObject* info) {
    if (PyDict_Check(info)) {
        auto id = method(info, "get", p::text("vehicleID").get(), valueGet(info, "id").get());
        auto team = valueGet(info, "team");
        return {diagnosticInt(id.get()), diagnosticInt(team.get())};
    }
    auto id = attrOr(info, "vehicleID", Py_None);
    auto team = attrOr(info, "team", Py_None);
    return {diagnosticInt(id.get()), diagnosticInt(team.get())};
}
p::Ref diagnosticValue(PyObject* value) { return value == Py_None ? p::text("-") : p::Ref(PyObject_Str(value)); }
bool sameOrUnknown(PyObject* observed, PyObject* expected) {
    return observed == Py_None || expected == Py_None || !test(observed, expected, Py_NE);
}

// BattleRuntime._report_local_kill_contract(target_record, attacker_id, reason_id): diagnostic only.
PyObject* reportLocalKillContract(PyObject*, PyObject* args) {
    PyObject *battle, *target_record, *attacker_id, *reason_id, *g;
    if (!PyArg_ParseTuple(args, "OOOOO", &battle, &target_record, &attacker_id, &reason_id, &g)) return nullptr;
    try {
        bool reported = false;
        const bool ok = contained(PyExc_Exception, [&] {
            auto avatar = attr(battle, "_avatar");
            if (truthy(attr(battle, "_worker_mode")) || truthy(valueGet(target_record, "local")) || avatar.get() == Py_None) return;
            auto target_id = diagnosticInt(valueGet(target_record, "engine_id").get());
            auto attacker = diagnosticInt(attacker_id);
            auto player_id = diagnosticInt(attrOr(avatar.get(), "playerVehicleID", Py_None).get());
            auto positive_id = [&](const p::Ref& v) { return v.get() != Py_None && greaterThanZero(v.get()); };
            if (!positive_id(target_id) || !positive_id(attacker) || !positive_id(player_id) ||
                test(attacker.get(), player_id.get(), Py_NE))
                return;
            p::Ref attacker_record = none();
            auto records = method(attr(battle, "_records").get(), "values");
            p::Ref iterator(PyObject_GetIter(records.get()));
            while (PyObject* raw = PyIter_Next(iterator.get())) {
                p::Ref candidate(raw);
                if (test(diagnosticInt(valueGet(candidate.get(), "engine_id").get()).get(), attacker.get(), Py_EQ)) {
                    replace(attacker_record, std::move(candidate));
                    break;
                }
            }
            if (PyErr_Occurred()) throw p::Error("records");
            auto team_of = [&](PyObject* record) { return diagnosticInt(valueGet(orDict(valueGet(record, "state")).get(), "team").get()); };
            auto target_team = team_of(target_record);
            auto attacker_team = attacker_record.get() == Py_None ? none() : team_of(attacker_record.get());
            auto avatar_team = diagnosticInt(attrOr(avatar.get(), "team", Py_None).get());
            auto arena = attrOr(avatar.get(), "arena", Py_None);
            auto arena_vehicles = attrOr(arena.get(), "vehicles", Py_None);
            if (!PyObject_HasAttrString(arena_vehicles.get(), "get")) replace(arena_vehicles, p::dict());
            auto arena_target = diagnosticVehicleInfo(method(arena_vehicles.get(), "get", target_id.get()).get());
            auto arena_attacker = diagnosticVehicleInfo(method(arena_vehicles.get(), "get", attacker.get()).get());
            p::Ref dp_target_id = none(), dp_target_team = none(), dp_attacker_id = none(), dp_attacker_team = none(), dp_player_team = none();
            auto provider = attrOr(avatar.get(), "guiSessionProvider", Py_None);
            auto get_arena_dp = attrOr(provider.get(), "getArenaDP", Py_None);
            auto arena_dp = PyCallable_Check(get_arena_dp.get()) ? p::Ref(PyObject_CallFunctionObjArgs(get_arena_dp.get(), nullptr)) : none();
            auto get_vehicle_info = attrOr(arena_dp.get(), "getVehicleInfo", Py_None);
            if (PyCallable_Check(get_vehicle_info.get())) {
                if (!contained(PyExc_Exception, [&] {
                        auto info = diagnosticVehicleInfo(p::Ref(PyObject_CallFunctionObjArgs(get_vehicle_info.get(), target_id.get(), nullptr)).get());
                        replace(dp_target_id, std::move(info.first)); replace(dp_target_team, std::move(info.second));
                    }))
                    PyErr_Clear();
                if (!contained(PyExc_Exception, [&] {
                        auto info = diagnosticVehicleInfo(p::Ref(PyObject_CallFunctionObjArgs(get_vehicle_info.get(), attacker.get(), nullptr)).get());
                        replace(dp_attacker_id, std::move(info.first)); replace(dp_attacker_team, std::move(info.second));
                    }))
                    PyErr_Clear();
            }
            auto get_number_of_team = attrOr(arena_dp.get(), "getNumberOfTeam", Py_None);
            if (PyCallable_Check(get_number_of_team.get()) && !contained(PyExc_Exception, [&] {
                    // BattleCtx.isAlly reads ArenaDP's independent cached player team through this boundary.
                    replace(dp_player_team, diagnosticInt(p::Ref(PyObject_CallFunctionObjArgs(get_number_of_team.get(), Py_False, nullptr)).get()));
                }))
                PyErr_Clear();
            const bool mismatch =
                !sameOrUnknown(attacker_team.get(), avatar_team.get()) || !sameOrUnknown(arena_target.second.get(), target_team.get()) ||
                !sameOrUnknown(arena_attacker.second.get(), attacker_team.get()) || !sameOrUnknown(dp_target_id.get(), target_id.get()) ||
                !sameOrUnknown(dp_target_team.get(), target_team.get()) || !sameOrUnknown(dp_attacker_id.get(), attacker.get()) ||
                !sameOrUnknown(dp_attacker_team.get(), attacker_team.get()) || !sameOrUnknown(dp_player_team.get(), avatar_team.get());
            const bool unknown = target_team.get() == Py_None || attacker_team.get() == Py_None || avatar_team.get() == Py_None ||
                                 arena_target.second.get() == Py_None || arena_attacker.second.get() == Py_None ||
                                 dp_target_id.get() == Py_None || dp_target_team.get() == Py_None || dp_attacker_id.get() == Py_None ||
                                 dp_attacker_team.get() == Py_None || dp_player_team.get() == Py_None;
            p::Ref values[] = {p::retain(target_id.get()), p::retain(attacker.get()),
                               boolean(!truthy(valueGet(target_record, "dead_marker_known", Py_False))),
                               diagnosticValue(target_team.get()), diagnosticValue(attacker_team.get()), diagnosticValue(avatar_team.get()),
                               diagnosticValue(arena_target.second.get()), diagnosticValue(arena_attacker.second.get()),
                               diagnosticValue(dp_target_id.get()), diagnosticValue(dp_target_team.get()),
                               diagnosticValue(dp_attacker_id.get()), diagnosticValue(dp_attacker_team.get()),
                               diagnosticValue(dp_player_team.get()), p::retain(target_id.get()), p::retain(attacker.get()),
                               asInt(reason_id), p::text(mismatch ? "no" : unknown ? "unknown" : "yes")};
            p::Ref tuple(PyTuple_New(17));
            for (Py_ssize_t i = 0; i < 17; ++i) PyTuple_SET_ITEM(tuple.get(), i, values[i].release());
            write("[Offline LAN 0.9.22] KILL-CONTRACT victim=%d attacker=%d "
                  "blind=%s runtime_team=%s/%s avatar_team=%s "
                  "arena_team=%s/%s arenaDP=%s:%s/%s:%s "
                  "arenaDP_player_team=%s payload=%d,%d,0,%d match=%s\n", std::move(tuple));
            reported = true;
        });
        if (ok) return boolean(reported).release();
        PyErr_Clear();
        if (!contained(PyExc_Exception, [&] { writeStdout(p::text("[Offline LAN 0.9.22] KILL-CONTRACT result=unavailable\n").get()); }))
            PyErr_Clear();
        Py_RETURN_FALSE;
    } catch (const std::exception& e) { return fail(e); }
}

// BattleRuntime._combat_event_source(event) (staticmethod)
PyObject* combatEventSourcePort(PyObject*, PyObject* args) {
    PyObject *event, *g;
    if (!PyArg_ParseTuple(args, "OO", &event, &g)) return nullptr;
    try { return combatEventSource(event).release(); } catch (const std::exception& e) { return fail(e); }
}

PyMethodDef methods[] = {
    {"projectile_cosmetic_allowed", projectileCosmeticAllowed, METH_VARARGS, nullptr},
    {"hit_damage_factor", hitDamageFactor, METH_VARARGS, nullptr},
    {"impact_sound_identity", impactSoundIdentity, METH_VARARGS, nullptr},
    {"surface_effect_material", surfaceEffectMaterial, METH_VARARGS, nullptr},
    {"release_target_lock", releaseTargetLock, METH_VARARGS, nullptr},
    {"event_entity_key", eventEntityKeyPort, METH_VARARGS, nullptr},
    {"report_effect", reportEffect, METH_VARARGS, nullptr},
    {"report_hit_impulse", reportHitImpulse, METH_VARARGS, nullptr},
    {"decoded_hit_impulse_direction", decodedHitImpulseDirection, METH_VARARGS, nullptr},
    {"present_hit_impulse", presentHitImpulse, METH_VARARGS, nullptr},
    {"event_shell", eventShell, METH_VARARGS, nullptr},
    {"shell_is_gold", shellIsGold, METH_VARARGS, nullptr},
    {"report_feedback_shell_failure", reportFeedbackShellFailure, METH_VARARGS, nullptr},
    {"feedback_shell_fields", feedbackShellFields, METH_VARARGS, nullptr},
    {"fallback_postmortem_viewpoint", fallbackPostmortemViewpoint, METH_VARARGS, nullptr},
    {"report_local_kill_contract", reportLocalKillContract, METH_VARARGS, nullptr},
    {"combat_event_source", combatEventSourcePort, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};
}
}

PyMethodDef* feedback_methods() { return feedback::methods; }
}
