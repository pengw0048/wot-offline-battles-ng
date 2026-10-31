// Hit presentation through the #1513 hit APIs (armour effects, splash, impulse, hit indicator) and the shot muzzle.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_feedback_internal.hpp"
#include <cmath>

namespace native_battle {
namespace feedback {
namespace {
std::int64_t toInt(PyObject* v) { return p::wide(p::Ref(PyNumber_Int(v)).get()); }
double length(PyObject* vector) { return p::scalar(p::Ref(PyNumber_Float(py::attr(vector, "length").get())).get()); }
p::Ref fieldOf(PyObject* value, const char* name, PyObject* fallback = Py_None) {
    if (PyDict_Check(value)) {
        PyObject* found = PyDict_GetItemString(value, name);
        return p::retain(found ? found : fallback);
    }
    auto found = py::optional(value, name);
    return found.get() == Py_None ? p::retain(fallback) : std::move(found);
}
// int(value) with `except (TypeError, ValueError)` falling back.
std::int64_t intOr(const Value& v, std::int64_t fallback) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Integer) return r.integer;
    if (r.kind == Value::Kind::Boolean) return r.boolean ? 1 : 0;
    if (r.kind == Value::Kind::Number && std::isfinite(r.number)) return std::int64_t(r.number);
    if (r.kind == Value::Kind::Text) {
        try { std::size_t end = 0; const auto n = std::stoll(r.text, &end); if (end == r.text.size()) return n; } catch (...) {}
    }
    return fallback;
}
std::int64_t intOr(PyObject* v, std::int64_t fallback) {
    PyObject* converted = PyNumber_Int(v);
    if (!converted) {
        if (!PyErr_ExceptionMatches(PyExc_TypeError) && !PyErr_ExceptionMatches(PyExc_ValueError)) throw p::Error("int conversion failed");
        PyErr_Clear();
        return fallback;
    }
    p::Ref held(converted);
    return p::wide(held.get());
}

// BattleRuntime._critical_hit_mask: #1513's device/destroyed/crew hit-direction bit fields.
std::int64_t criticalHitMask(const Value& critical) {
    static const std::map<std::string, int> devices{{"engineHealth", 0}, {"ammoBayHealth", 1}, {"fuelTankHealth", 2}, {"radioHealth", 3},
                                                    {"leftTrackHealth", 4}, {"rightTrackHealth", 4}, {"gunHealth", 5},
                                                    {"turretRotatorHealth", 6}, {"surveyingDeviceHealth", 7}};
    static const std::map<std::string, int> crew{{"commander", 0}, {"driver", 1}, {"radioman", 2}, {"gunner", 3}, {"loader", 4}};
    std::int64_t result = 0;
    for (const auto& event : field(critical, "events").resolved().array) {
        if ((has(event, "cause") ? text(field(event, "cause")) : "shot") != "shot") continue;
        const std::string kind = text(field(event, "kind")), state = text(field(event, "state")), name = text(field(event, "name"));
        if (kind == "device") {
            const auto found = devices.find(name);
            if (found == devices.end()) continue;
            if (state == "critical") result |= std::int64_t(1) << found->second;
            else if (state == "destroyed") result |= std::int64_t(1) << (12 + found->second);
        } else if (kind == "crew" && state == "destroyed") {
            std::string role = name;
            while (!role.empty() && std::isdigit(static_cast<unsigned char>(role.back()))) role.pop_back();
            const auto found = crew.find(role);
            if (found != crew.end()) result |= std::int64_t(1) << (24 + found->second);
        }
    }
    return result;
}

// BattleRuntime._hit_indicator_marker: the (damage, isBlocked) pair #1513 can draw.
std::pair<std::int64_t, bool> hitIndicatorMarker(const Value& event, PyObject* shot, std::int64_t damage) {
    const std::int64_t shot_result = std::max<std::int64_t>(0, std::min<std::int64_t>(integer(field(event, "shot_result"), 2), 2));
    const bool blocked = damage <= 0 && !field(event, "splash").truth() && shot_result != 2 && !isHe(shot);
    if (!blocked) return {damage, false};
    return {std::max<std::int64_t>(0, shellNominalDamage(shot)), true};
}

// BattleRuntime._local_vehicle_appearance: the player's own stock appearance while it is still live.
p::Ref localAppearance(Battle& b) {
    auto lookup = py::optional(b.engine.bigworld.get(), "entity");
    if (!py::callable(lookup)) return p::retain(Py_None);
    auto id = py::optional(b.engine.avatar.get(), "playerVehicleID");
    auto vehicle = py::invoke(lookup.get(), p::integer(p::flag(id.get()) ? toInt(id.get()) : 0).get());
    if (vehicle.get() == Py_None || !p::flag(py::optional(vehicle.get(), "isStarted").get())) return p::retain(Py_None);
    return py::optional(vehicle.get(), "appearance");
}

// native_remote_vehicle.present_shot_impulse: one retail hull shot impulse through a stock CompoundAppearance.
bool presentShotImpulse(Battle& b, PyObject* appearance, PyObject* direction, double impulse) {
    if (appearance == Py_None || py::optional(appearance, "swingingAnimator").get() == Py_None) return false;
    auto receive = py::optional(appearance, "receiveShotImpulse");
    if (!py::callable(receive)) return false;
    double values[3];
    for (int i = 0; i < 3; ++i) {
        static const char* const axes[] = {"x", "y", "z"};
        PyObject* raw = PyObject_GetAttrString(direction, axes[i]);
        PyObject* number = raw ? PyNumber_Float(raw) : nullptr;
        Py_XDECREF(raw);
        if (!number) { PyErr_Clear(); return false; }
        values[i] = PyFloat_AS_DOUBLE(number);
        Py_DECREF(number);
    }
    if (!(impulse > 0.) || !std::isfinite(impulse)) return false;
    for (double v : values) if (!std::isfinite(v)) return false;
    const double norm = std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2]);
    if (norm <= 1.0e-6) return false;
    // Retail's direction is a hit component's unit axis; normalise ours so the rocking magnitude stays the impulse.
    py::invoke(receive.get(), vector(b, values[0] / norm, values[1] / norm, values[2] / norm).get(), p::number(impulse).get());
    return true;
}

// BattleRuntime._decoded_hit_impulse_direction: retail's armour-segment axis of a direct hit, or None.
p::Ref decodedHitImpulseDirection(Battle& b, const Value& event, const Actor& target) {
    if (!has(event, "damage_sticker")) return p::retain(Py_None);
    auto entity = b.entity(target.engine_id);
    auto appearance = py::optional(entity.get(), "appearance");
    auto descriptor = py::optional(entity.get(), "typeDescriptor");
    auto compound = py::optional(appearance.get(), "compoundModel");
    if (entity.get() == Py_None || !p::flag(py::optional(entity.get(), "isStarted").get()) || descriptor.get() == Py_None ||
        compound.get() == Py_None)
        return p::retain(Py_None);
    p::Ref result(p::retain(Py_None));
    b.feature("vehicle hit impulse", [&] {
        p::Ref effects(PyImport_ImportModule("VehicleEffects"));
        auto decoder = py::attr(effects.get(), "DamageFromShotDecoder");
        p::Ref code(cs::write(field(event, "damage_sticker")));
        auto decoded = py::call(decoder.get(), "decodeSegment", code.get(), descriptor.get());
        if (PySequence_Size(decoded.get()) != 4) throw p::Error("damage segment decode has an unexpected shape");
        auto component = p::item(decoded.get(), 0), start = p::item(decoded.get(), 2), end = p::item(decoded.get(), 3);
        if (!p::flag(component.get()) || start.get() == Py_None || end.get() == Py_None) return;
        const int same = PyObject_RichCompareBool(start.get(), end.get(), Py_EQ);
        if (same < 0) throw p::Error("damage segment comparison failed");
        if (same) return;
        auto node = py::call(compound.get(), "node", component.get());
        if (node.get() == Py_None) return;
        p::Ref local(PyNumber_Subtract(end.get(), start.get()));
        if (length(local.get()) <= 0.001) return;
        auto matrix = py::call(b.engine.math.get(), "Matrix", node.get());
        auto applied = py::call(matrix.get(), "applyVector", local.get());
        auto direction = py::call(b.engine.math.get(), "Vector3", applied.get());
        if (length(direction.get()) <= 0.001) return;
        py::call(direction.get(), "normalise");
        replace(result, std::move(direction));
    }, false);
    return result;
}

// BattleRuntime._present_hit_impulse: the stock hull rock of one hit (the splash quarter for a near miss).
bool hitImpulse(Battle& b, const Value& event, Actor& target, PyObject* effects, PyObject* splash_direction) {
    const auto& state = target.presented.state;
    if (field(event, "dead").truth()) return false;
    const auto& health = field(state, "health");
    const std::int64_t hp = has(state, "health") ? (health.truth() ? integer(health) : 0) : 1;
    if (!field(state, "alive").truth(true) || hp <= 0) return false;
    auto raw = fieldOf(effects, "targetImpulse");
    double impulse = 0.;
    if (raw.get() != Py_None) {
        PyObject* number = PyNumber_Float(raw.get());
        if (number) { impulse = PyFloat_AS_DOUBLE(number); Py_DECREF(number); }
        else PyErr_Clear();
        if (!std::isfinite(impulse)) impulse = 0.;
    }
    if (impulse <= 0.) return false;
    p::Ref direction(p::retain(Py_None));
    if (field(event, "splash").truth()) {
        impulse /= 4.;
        replace(direction, p::retain(splash_direction));
    } else {
        replace(direction, decodedHitImpulseDirection(b, event, target));
    }
    if (direction.get() == Py_None) return false;
    if (target.local) {
        // The matching local stock-animator rebind owns this capability.
        auto appearance = localAppearance(b);
        auto animator = local::swingingAnimator(b);
        if (animator.get() == Py_None || !b.featureEnabled("local body swinging") ||
            py::optional(appearance.get(), "swingingAnimator").get() != animator.get())
            return false;
        return presentShotImpulse(b, appearance.get(), direction.get(), impulse);
    }
    if (target.native_remote) return remote::presentHitImpulse(b, target.engine_id, direction.get(), impulse);
    return false;
}

// BattleRuntime._impact_sound_identity: the stock impact sound's keyword arguments.
p::Ref impactSoundIdentity(Battle& b, const Actor& target, std::int64_t attacker, PyObject* direction, double damage_factor) {
    auto id = py::optional(b.engine.avatar.get(), "playerVehicleID");
    const std::int64_t player = p::flag(id.get()) ? toInt(id.get()) : 0;
    auto lookup = py::optional(b.engine.bigworld.get(), "entity");
    if (player <= 0 || !py::callable(lookup) || py::invoke(lookup.get(), p::integer(player).get()).get() == Py_None) return p::dict();
    auto out = p::dict();
    p::set(out.get(), "isPlayerVehicle", p::boolean(target.local));
    p::set(out.get(), "entity_id", p::integer(target.engine_id));
    p::set(out.get(), "attackerID", p::integer(attacker));
    p::set(out.get(), "damageFactor", p::number(damage_factor));
    p::set(out.get(), "hitdir", p::retain(direction));
    return out;
}

// _MutedShootingExtra: keeps stock shot bookkeeping while suppressing the cosmetic shoot extra.
struct MutedExtra {
    PyObject_HEAD
    PyObject* original;
};
void mutedDealloc(PyObject* self) {
    Py_XDECREF(reinterpret_cast<MutedExtra*>(self)->original);
    PyObject_Del(self);
}
PyObject* mutedNoop(PyObject*, PyObject*) { Py_RETURN_NONE; }
PyObject* mutedGetattr(PyObject* self, PyObject* name) {
    PyObject* own = PyObject_GenericGetAttr(self, name);
    if (own || !PyErr_ExceptionMatches(PyExc_AttributeError)) return own;
    PyErr_Clear();
    return PyObject_GetAttr(reinterpret_cast<MutedExtra*>(self)->original, name);
}
PyMethodDef mutedMethods[] = {{"startFor", mutedNoop, METH_VARARGS, nullptr}, {"stopFor", mutedNoop, METH_VARARGS, nullptr},
                              {nullptr, nullptr, 0, nullptr}};
PyTypeObject MutedExtraType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.MutedShootingExtra"};
p::Ref mutedExtra(PyObject* original) {
    if (!MutedExtraType.tp_dealloc) {
        MutedExtraType.tp_basicsize = sizeof(MutedExtra);
        MutedExtraType.tp_dealloc = mutedDealloc;
        MutedExtraType.tp_getattro = mutedGetattr;
        MutedExtraType.tp_methods = mutedMethods;
        MutedExtraType.tp_flags = Py_TPFLAGS_DEFAULT;
        if (PyType_Ready(&MutedExtraType) < 0) throw p::Error("muted shooting extra type failed");
    }
    auto* muted = PyObject_New(MutedExtra, &MutedExtraType);
    if (!muted) throw p::Error("muted shooting extra allocation failed");
    muted->original = original;
    Py_INCREF(original);
    return p::Ref(reinterpret_cast<PyObject*>(muted));
}

// BattleRuntime._show_local_shot_without_extra: stock local shot state without starting another effect.
void localShotWithoutExtra(Battle& b, PyObject* entity, std::int64_t burst_count) {
    auto show = [&] { py::call(entity, "showShooting", p::integer(burst_count).get(), Py_False); };
    auto extras = py::optional(py::optional(entity, "typeDescriptor").get(), "extrasDict");
    p::Ref original(p::retain(Py_None));
    if (extras.get() != Py_None) {
        PyObject* found = PyObject_CallMethod(extras.get(), const_cast<char*>("get"), const_cast<char*>("s"), "shoot");
        if (found) replace(original, p::Ref(found));
        else if (PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
        else throw p::Error("shoot extra lookup failed");
    }
    // No shoot extra means no cosmetic work to suppress; the dispersion handshake still matters.
    if (original.get() == Py_None) { show(); return; }
    b.feature("shot muzzle retirement", [&] { py::call(original.get(), "stopFor", entity); }, false);
    auto muted = mutedExtra(original.get());
    if (PyObject_SetItem(extras.get(), p::text("shoot").get(), muted.get()) < 0) {
        // A non-mutable descriptor cannot be intercepted; keep stock shot convergence.
        PyErr_Clear();
        show();
        return;
    }
    try { show(); }
    catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        if (PyObject_SetItem(extras.get(), p::text("shoot").get(), original.get()) < 0) PyErr_Clear();
        PyErr_Restore(type, value, trace);
        throw;
    }
    if (PyObject_SetItem(extras.get(), p::text("shoot").get(), original.get()) < 0) throw p::Error("shoot extra restore failed");
}

// BattleRuntime._show_replay_local_muzzle: the native shooting extra without a new fire intent.
void replayMuzzle(PyObject* entity, std::int64_t burst_count) {
    p::Ref extra(PyObject_GetItem(py::attr(py::attr(entity, "typeDescriptor").get(), "extrasDict").get(), p::text("shoot").get()));
    // An already-running native extra owns callbacks and must retire before the next shot (Vehicle.showShooting).
    py::call(extra.get(), "stopFor", entity);
    py::call(extra.get(), "startFor", entity, p::integer(burst_count).get());
}
}

// BattleRuntime._present_splash_hit: one HE near miss, as showSplashHitEffect presents it.
bool splashHit(Battle& b, Actor& target, PyObject* effects_descr, PyObject* effects_index, const Point& impact, PyObject* direction,
               double damage_factor) {
    PyObject* raw = PyObject_GetItem(effects_descr, p::text("armorSplashHit").get());
    if (!raw) {
        if (PyErr_ExceptionMatches(PyExc_KeyError) || PyErr_ExceptionMatches(PyExc_TypeError)) { PyErr_Clear(); return false; }
        throw p::Error("armorSplashHit lookup failed");
    }
    p::Ref effects_list(raw);
    if (!p::flag(effects_list.get())) return false;
    PyObject* sequence = PySequence_Fast(effects_list.get(), "");
    if (!sequence || PySequence_Fast_GET_SIZE(sequence) != 3) {
        Py_XDECREF(sequence);
        if (!PyErr_Occurred() || PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_ValueError)) { PyErr_Clear(); return false; }
        throw p::Error("armorSplashHit unpack failed");
    }
    p::Ref parts(sequence);
    auto entity = b.entity(target.engine_id);
    if (entity.get() == Py_None || !p::flag(py::optional(entity.get(), "isStarted").get()) || !vehicles::alive(b, target, entity.get())) return false;
    if (!b.featureEnabled("projectile impact presentation")) return false;
    auto hull = py::optional(py::optional(b.engine.modules.get(), "tank_part_names").get(), "HULL");
    if (hull.get() == Py_None) throw p::Error("#1513 HULL part name is unavailable");
    bool shown = false;
    b.feature("projectile impact presentation", [&] {
        // The compound-only fallback builds its bound-effect owner lazily and raises without a compound.
        auto add = py::optional(py::attr(py::attr(entity.get(), "appearance").get(), "boundEffects").get(), "addNewToNode");
        if (!py::callable(add)) return;
        auto matrix = py::call(b.engine.math.get(), "Matrix");
        p::Ref origin(Py_BuildValue("(ddd)", 0., 0., 0.));
        py::call(matrix.get(), "setTranslate", origin.get());
        p::Ref positional(PyTuple_Pack(4, hull.get(), matrix.get(), PySequence_Fast_GET_ITEM(parts.get(), 1), PySequence_Fast_GET_ITEM(parts.get(), 0)));
        auto keywords = p::dict();
        p::set(keywords.get(), "entity", p::retain(entity.get()));
        p::set(keywords.get(), "damageFactor", p::number(damage_factor));
        p::Ref(PyObject_Call(add.get(), positional.get(), keywords.get()));
        shown = true;
    });
    (void)effects_index;
    (void)impact;
    (void)direction;
    return shown;
}

// BattleRuntime._present_combat_hit: one shot hit through the #1513 hit APIs.
bool combatHit(Battle& b, const Value& event, Actor& target, Actor& attacker) {
    if (b.worker) return false;
    if (source(event) != "shot" || !anyHit(text(field(event, "kind")))) return false;
    if (!field(event, "world_pose").truth()) throw p::Error("shot hit event has no world impact pose");
    auto shell_pair = eventShell(b, attacker, event);
    PyObject *shot = shell_pair.first.get(), *shell = shell_pair.second.get();
    const Point attacker_position = vehicles::position(b, attacker), target_position = vehicles::position(b, target);
    const Point impact{number(field(event, "x")), number(field(event, "y")), number(field(event, "z"))};
    const Point& origin = field(event, "splash").truth() ? impact : attacker_position;
    auto direction = vector(b, target_position[0] - origin[0], target_position[1] - origin[1], target_position[2] - origin[2]);
    // A legal HE blast can damage its own shooter; use a harmless presentation normal.
    if (length(direction.get()) <= 0.001) replace(direction, vector(b, 0., 1., 0.));
    py::call(direction.get(), "normalise");
    const std::int64_t damage = nonNegative(event, "damage");
    const std::int64_t shot_result = std::max<std::int64_t>(0, std::min<std::int64_t>(integer(field(event, "shot_result"), 2), 2));
    const bool same_vehicle = &attacker == &target || attacker.key == target.key;
    auto avatar = b.engine.avatar.get();
    if (target.local && !same_vehicle) {
        // The hit indicator points from the player back toward the attacker.
        const double yaw = std::atan2(-(attacker_position[0] - target_position[0]), -(attacker_position[2] - target_position[2]));
        const auto marker = hitIndicatorMarker(event, shot, damage);
        py::call(avatar, "showOwnVehicleHitDirection", p::number(yaw).get(), p::integer(attacker.engine_id).get(), p::integer(marker.first).get(),
                 p::integer(criticalHitMask(field(event, "critical"))).get(), p::boolean(marker.second).get(), p::boolean(isHe(shot)).get(),
                 p::integer(target.engine_id).get());
    }
    // An armour effect belongs to the visible world model, not to a radio-only marker.
    if (!target.local && !target.presented.spot_visible) return false;
    if (!projectiles::cosmeticAllowed(b, event)) return false;
    auto effects_index = fieldOf(shell, "effectsIndex");
    if (effects_index.get() == Py_None) throw p::Error("combat shell effects index is unavailable");
    auto cache = py::attr(b.engine.vehicles.get(), "g_cache");
    p::Ref effects_descr(PyObject_GetItem(py::attr(cache.get(), "shotEffects").get(), effects_index.get()));
    // Retail rocks the hull before it spawns the armour effect.
    b.feature("vehicle hit impulse", [&] { hitImpulse(b, event, target, effects_descr.get(), direction.get()); });
    const double damage_factor = hitDamageFactor(b, event, target);
    if (field(event, "splash").truth()) return splashHit(b, target, effects_descr.get(), effects_index.get(), impact, direction.get(), damage_factor);
    const bool he = isHe(shot);
    static const char* const by_result[] = {"armorRicochet", "armorResisted", "armorHit"};
    const char* group = he ? (damage > 0 ? "armorHit" : "armorResisted") : by_result[shot_result];
    p::Ref effect_group(PyObject_GetItem(effects_descr.get(), p::text(group).get()));
    PyObject* sequence = PySequence_Fast(effect_group.get(), "armour effect group is not a sequence");
    if (!sequence) throw p::Error("armour effect group unpack failed");
    p::Ref parts(sequence);
    if (PySequence_Fast_GET_SIZE(sequence) != 3) throw p::Error("armour effect group must have three parts");
    auto hit_position = vector(b, impact[0], impact[1], impact[2]);
    auto add_effect = py::optional(py::optional(avatar, "terrainEffects").get(), "addNew");
    if (!py::callable(add_effect)) throw p::Error("#1513 terrain hit-effects boundary is unavailable");
    if (!b.featureEnabled("projectile impact presentation")) return false;
    const bool shown = b.feature("projectile impact presentation", [&] {
        auto offset = py::call(direction.get(), "scale", p::number(0.4).get());
        p::Ref start(PyNumber_Subtract(hit_position.get(), offset.get()));
        p::Ref end(PyNumber_Add(hit_position.get(), offset.get()));
        auto keywords = impactSoundIdentity(b, target, attacker.engine_id, direction.get(), damage_factor);
        p::set(keywords.get(), "dir", p::retain(direction.get()));
        p::set(keywords.get(), "start", start);
        p::set(keywords.get(), "end", end);
        p::set(keywords.get(), "showShockWave", p::boolean(target.local));
        p::set(keywords.get(), "showFlashBang", p::boolean(target.local));
        p::Ref positional(PyTuple_Pack(4, hit_position.get(), PySequence_Fast_GET_ITEM(parts.get(), 1), PySequence_Fast_GET_ITEM(parts.get(), 0), Py_None));
        p::Ref(PyObject_Call(add_effect.get(), positional.get(), keywords.get()));
    });
    if (!shown) return false;
    if (he && damage > 0) splashHit(b, target, effects_descr.get(), effects_index.get(), impact, direction.get(), damage_factor);
    return true;
}

// BattleRuntime._show_shot: one ordered shot's muzzle, burst and canonical tracer handoff.
bool showShot(Battle& b, const Value& event, bool update_state) {
    const auto key = entityKey(event, true);
    if (!key) throw p::Error("ordered shot event has no attacker");
    Actor* v = presentedVehicle(b, *key);
    if (!v) throw p::Error("ordered shot event attacker is unavailable: " + *key);
    const auto& projectile = field(event, "projectile_id");
    const std::string projectile_text = !has(event, "projectile_id") ? std::string() : isNull(projectile) ? std::string("None") : idText(projectile);
    const bool local = v->local;
    if (local && !b.replay && !projectiles::markLocalPresented(b, projectile_text)) return true;
    if (update_state) v->presented.shot_penalty_until = b.clock() + visibility::shotCamouflageSeconds();
    // #1513 Vehicle.showShooting owns a descriptor burst's effects through one native call.
    const auto local_burst = local ? gun::localBurst(b) : std::nullopt;
    if (b.worker) return true;
    auto entity = b.entity(v->engine_id);
    if (entity.get() == Py_None) throw p::Error("ordered shot event attacker has no native entity: " + *key);
    std::vector<const char*> transient;
    auto set = [&](const char* name, PyObject* value, bool temporary) {
        py::set(entity.get(), name, value);
        if (temporary) transient.push_back(name);
    };
    auto cleanup = [&] {
        for (const char* name : transient)
            if (PyObject_DelAttrString(entity.get(), name) < 0) PyErr_Clear();
        transient.clear();
    };
    try {
        bool admitted = true, deferred = false, meta = false;
        std::optional<std::int64_t> meta_burst_count;
        std::int64_t burst_index = intOr(has(event, "burst_index") ? field(event, "burst_index") : Value(), 0);
        set("_offlineLANShotIndex", p::integer(nonNegative(event, "shell_index")).get(), false);
        if (has(event, "shot_yaw") && has(event, "shot_pitch") && p::flag(py::optional(entity.get(), "_offlineLANPresentation").get())) {
            set("_offlineLANShotYaw", p::number(number(field(event, "shot_yaw"))).get(), true);
            set("_offlineLANShotPitch", p::number(number(field(event, "shot_pitch"))).get(), true);
        }
        const bool canonical = has(event, "origin") && has(event, "velocity") && has(event, "gravity") && has(event, "maxDistance");
        if (canonical) {
            if (const auto launch = projectiles::launchVisual(b, event, entity.get())) {
                meta = launch->meta;
                if (meta) {
                    admitted = launch->admitted;
                    deferred = launch->deferred;
                    if (launch->burst_index) burst_index = *launch->burst_index;
                    meta_burst_count = launch->burst_count;
                }
                // RemoteVehicle.showShooting owns muzzle and recoil; the native tracer owns the flight.
                set("_offlineLANShotOrigin", p::Ref(cs::write(field(event, "origin"))).get(), true);
                set("_offlineLANShotVelocity", p::Ref(cs::write(field(event, "velocity"))).get(), true);
                set("_offlineLANShotGravity", p::number(number(field(event, "gravity"))).get(), true);
                set("_offlineLANShotMaxDistance", p::Ref(cs::write(field(event, "maxDistance"))).get(), true);
                set("_offlineLANProjectileID", p::Ref(cs::write(projectile)).get(), true);
                set("_offlineLANCanonicalTracerOwned", Py_True, true);
            }
        }
        if (local_burst) burst_index = local_burst->first;
        if (burst_index == 0) {
            auto gun = py::attr(py::attr(entity.get(), "typeDescriptor").get(), "gun");
            p::Ref one(Py_BuildValue("(i)", 1));
            auto burst = fieldOf(gun.get(), "burst", one.get());
            std::int64_t descriptor_count = 1;
            PyObject* first = PySequence_GetItem(burst.get(), 0);
            if (first) { p::Ref held(first); descriptor_count = intOr(held.get(), 1); }
            else if (PyErr_ExceptionMatches(PyExc_TypeError) || PyErr_ExceptionMatches(PyExc_IndexError)) PyErr_Clear();
            else throw p::Error("burst lookup failed");
            std::int64_t burst_count = meta ? meta_burst_count.value_or(1)
                                     : has(event, "burst_count") ? intOr(field(event, "burst_count"), 1) : descriptor_count;
            if (burst_count <= 1) burst_count = 1;
            if (local_burst) burst_count = local_burst->second;
            if (admitted && b.featureEnabled("shot muzzle presentation") && (local || v->presented.spot_visible)) {
                if (b.replay && local) b.feature("shot muzzle presentation", [&] { replayMuzzle(entity.get(), burst_count); });
                else b.feature("shot muzzle presentation", [&] { py::call(entity.get(), "showShooting", p::integer(burst_count).get(), Py_False); });
            } else if (local && !b.replay) {
                b.feature("local shot convergence", [&] { localShotWithoutExtra(b, entity.get(), burst_count); });
            }
        }
        if (deferred) projectiles::deferredLaunchVisual(b, event, entity.get());
    } catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        cleanup();
        PyErr_Restore(type, value, trace);
        throw;
    }
    cleanup();
    return true;
}
}
}
