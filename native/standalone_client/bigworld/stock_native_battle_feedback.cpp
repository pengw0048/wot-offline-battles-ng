// Battle-event rows, shot results and the other stock feedback of applied combat, plus the round result.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_feedback_internal.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace native_battle {
namespace feedback {
namespace {
std::int64_t toInt(PyObject* v) { return p::wide(p::Ref(PyNumber_Int(v)).get()); }
std::int64_t constant(PyObject* owner, const char* name) { return toInt(py::attr(owner, name).get()); }
p::Ref runtime(Battle& b, const char* name) { return py::optional(b.engine.modules.get(), name); }
// The module helper _field on a stock object: dict.get or getattr with a default.
p::Ref fieldOf(PyObject* value, const char* name, PyObject* fallback = Py_None) {
    if (PyDict_Check(value)) {
        PyObject* found = PyDict_GetItemString(value, name);
        return p::retain(found ? found : fallback);
    }
    auto found = py::optional(value, name);
    return found.get() == Py_None ? p::retain(fallback) : std::move(found);
}

// "<kind>:<id>" of a feedback role (assister/observer/target), or the pinned identity error.
std::string feedbackKey(const Value& event, const char* role, const char* label) {
    const std::string kind = text(field(event, (std::string(role) + "_kind").c_str()));
    const auto& id = field(event, (std::string(role) + "_id").c_str());
    if ((kind != "player" && kind != "bot") || isNull(id))
        throw p::Error(std::string(label) + " event has an invalid " + role + " identity");
    return kind + ":" + idText(id);
}

// BattleRuntime._assert_player_identity: reject drift that would relabel player damage as ally damage.
void assertPlayerIdentity(Battle& b, std::int64_t expected) {
    auto avatar = b.engine.avatar.get();
    const std::int64_t avatar_vehicle = toInt(py::optional(avatar, "playerVehicleID").get() == Py_None ? p::integer(0).get()
                                                                                                      : py::attr(avatar, "playerVehicleID").get());
    auto get_arena_dp = py::optional(py::optional(avatar, "guiSessionProvider").get(), "getArenaDP");
    if (!py::callable(get_arena_dp)) throw p::Error("#1513 ArenaDP provider is unavailable");
    auto arena_dp = py::invoke(get_arena_dp.get());
    auto player_vehicle = py::optional(arena_dp.get(), "getPlayerVehicleID");
    if (!py::callable(player_vehicle)) throw p::Error("#1513 ArenaDP player identity API is unavailable");
    const std::int64_t arena_vehicle = toInt(py::invoke(player_vehicle.get(), Py_False).get());
    if (avatar_vehicle != expected || arena_vehicle != expected)
        throw p::Error("#1513 player identity mismatch: expected=" + std::to_string(expected) + " avatar=" + std::to_string(avatar_vehicle) +
                       " arenaDP=" + std::to_string(arena_vehicle));
    compat::assertMarkerIdentity(b, expected);
}

// The module-only stock voice events a confirmed internal penetration replaces (critical_voice.py).
const char* const kModuleOnlyResults[] = {
    "enemy_no_hp_damage_at_attempt_by_player", "enemy_no_hp_damage_at_attempt_and_gun_damaged_by_player",
    "enemy_no_hp_damage_at_attempt_and_chassis_damaged_by_player", "enemy_no_hp_damage_at_no_attempt_by_player",
    "enemy_no_hp_damage_at_no_attempt_and_gun_damaged_by_player", "enemy_no_hp_damage_at_no_attempt_and_chassis_damaged_by_player",
    "enemy_no_piercing_by_player", "enemy_ricochet_by_player"};
const char* const kCriticalEvent = "enemy_hp_damaged_by_projectile_by_player";

// avatar.soundNotifications for one showShotResults call: play() remaps a module-only result to the critical event.
struct PriorityVoice {
    PyObject_HEAD
    PyObject* original;
    bool remapped;
};
void voiceDealloc(PyObject* self) {
    Py_XDECREF(reinterpret_cast<PriorityVoice*>(self)->original);
    PyObject_Del(self);
}
PyObject* voicePlay(PyObject* self, PyObject* args, PyObject* kwargs) {
    auto* voice = reinterpret_cast<PriorityVoice*>(self);
    if (!PyTuple_Check(args) || PyTuple_GET_SIZE(args) < 1) {
        PyErr_SetString(PyExc_TypeError, "play() takes a sound name");
        return nullptr;
    }
    p::Ref forwarded(PyTuple_GetSlice(args, 0, PyTuple_GET_SIZE(args)));
    PyObject* name = PyTuple_GET_ITEM(args, 0);
    if (PyString_Check(name)) {
        const std::string text = PyString_AsString(name);
        for (const char* option : kModuleOnlyResults)
            if (text == option) {
                PyObject* replaced = PyString_FromString(kCriticalEvent);
                if (!replaced) return nullptr;
                Py_DECREF(PyTuple_GET_ITEM(forwarded.get(), 0));
                PyTuple_SET_ITEM(forwarded.get(), 0, replaced);
                voice->remapped = true;
                break;
            }
    }
    PyObject* play = PyObject_GetAttrString(voice->original, "play");
    if (!play) return nullptr;
    PyObject* result = PyObject_Call(play, forwarded.get(), kwargs);
    Py_DECREF(play);
    return result;
}
PyObject* voiceGetattr(PyObject* self, PyObject* name) {
    PyObject* own = PyObject_GenericGetAttr(self, name);
    if (own || !PyErr_ExceptionMatches(PyExc_AttributeError)) return own;
    PyErr_Clear();
    return PyObject_GetAttr(reinterpret_cast<PriorityVoice*>(self)->original, name);
}
PyMethodDef voiceMethods[] = {{"play", reinterpret_cast<PyCFunction>(voicePlay), METH_VARARGS | METH_KEYWORDS, nullptr},
                              {nullptr, nullptr, 0, nullptr}};
PyTypeObject PriorityVoiceType = {PyVarObject_HEAD_INIT(nullptr, 0) "offline_native_battle.PriorityVoice"};

// critical_voice.present: announce a confirmed internal penetration without changing the HUD flags.
void presentCriticalVoice(Battle& b, PyObject* callback, PyObject* results) {
    if (!PriorityVoiceType.tp_dealloc) {
        PriorityVoiceType.tp_basicsize = sizeof(PriorityVoice);
        PriorityVoiceType.tp_dealloc = voiceDealloc;
        PriorityVoiceType.tp_getattro = voiceGetattr;
        PriorityVoiceType.tp_methods = voiceMethods;
        PriorityVoiceType.tp_flags = Py_TPFLAGS_DEFAULT;
        if (PyType_Ready(&PriorityVoiceType) < 0) throw p::Error("critical voice type failed");
    }
    auto avatar = b.engine.avatar.get();
    auto original = py::attr(avatar, "soundNotifications");
    auto* voice = PyObject_New(PriorityVoice, &PriorityVoiceType);
    if (!voice) throw p::Error("critical voice allocation failed");
    voice->original = original.get();
    Py_INCREF(voice->original);
    voice->remapped = false;
    p::Ref proxy(reinterpret_cast<PyObject*>(voice));
    py::set(avatar, "soundNotifications", proxy.get());
    auto restore = [&] {
        PyObject* current = PyObject_GetAttrString(avatar, "soundNotifications");
        if (!current) { PyErr_Clear(); return; }
        const bool ours = current == proxy.get();
        Py_DECREF(current);
        if (ours && PyObject_SetAttrString(avatar, "soundNotifications", original.get()) < 0) PyErr_Clear();
    };
    PyObject* result = PyObject_CallFunctionObjArgs(callback, results, nullptr);
    if (!result) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        restore();
        PyErr_Restore(type, value, trace);
        throw p::Error("showShotResults failed");
    }
    Py_DECREF(result);
    restore();
    if (voice->remapped) ++b.feedback->critical_voice_count;
}

void reportShellFailure(Battle& b, const std::string& reason) {
    auto& seen = b.feedback->shell_failures;
    if (seen.count(reason) || seen.size() >= 32) return;
    seen.insert(reason);
    log("FEEDBACK damage-log shell unresolved: " + reason);
}

// battle_feedback.gold_shell_names: one nation's gold-priced shells from the raw item definitions,
// probing <shell>/price/gold the way _xml.readPrice does. Immutable client content, read once per nation.
const std::set<std::string>* goldShellNames(const std::string& nation) {
    static std::map<std::string, std::optional<std::set<std::string>>> cache;
    auto found = cache.find(nation);
    if (found != cache.end()) return found->second ? &*found->second : nullptr;
    auto& entry = cache[nation];
    const std::string path = "scripts/item_defs/vehicles/" + nation + "/components/shells.xml";
    std::string reason;
    try {
        p::Ref resmgr(PyImport_ImportModule("ResMgr"));
        auto section = py::call(resmgr.get(), "openSection", p::text(path).get());
        if (section.get() == Py_None) reason = "section_unavailable";
        else {
            std::set<std::string> names;
            auto purge = [&] {
                PyObject* done = PyObject_CallMethod(resmgr.get(), const_cast<char*>("purge"), const_cast<char*>("sO"), path.c_str(), Py_True);
                if (done) Py_DECREF(done);
                else PyErr_Clear();
            };
            try {
                p::Ref items(PySequence_List(py::call(section.get(), "items").get()));
                for (Py_ssize_t i = 0; i < PyList_GET_SIZE(items.get()); ++i) {
                    PyObject* item = PyList_GET_ITEM(items.get(), i);
                    const std::string name = p::string(PyTuple_GET_ITEM(item, 0));
                    if (name == "icons" || name == "xmlns:xmlref") continue;
                    p::Ref price(PyObject_GetItem(PyTuple_GET_ITEM(item, 1), p::text("price/gold").get()));
                    if (price.get() != Py_None) names.insert(name);
                }
            } catch (...) { purge(); throw; }
            purge();
            entry = std::move(names);
        }
    } catch (const std::exception& e) {
        reason = describe(e);
    }
    if (!entry) log("FEEDBACK shell prices unresolved: nation=" + nation + " reason=" + reason);
    return entry ? &*entry : nullptr;
}

// BattleRuntime._shell_is_gold(shell)
bool shellIsGold(Battle& b, PyObject* shell) {
    auto identity = fieldOf(shell, "id");
    auto name = fieldOf(shell, "name");
    if (name.get() == Py_None || !(PyTuple_Check(identity.get()) || PyList_Check(identity.get())) || PySequence_Size(identity.get()) < 1)
        return false;
    auto names = py::optional(runtime(b, "nations").get(), "NAMES");
    if (!p::flag(names.get())) return false;
    PyObject* first = PySequence_GetItem(identity.get(), 0);
    PyObject* key = first ? PyNumber_Int(first) : nullptr;
    Py_XDECREF(first);
    PyObject* nation = key ? PyObject_GetItem(names.get(), key) : nullptr;
    Py_XDECREF(key);
    if (!nation) {
        if (PyErr_ExceptionMatches(PyExc_IndexError) || PyErr_ExceptionMatches(PyExc_KeyError) || PyErr_ExceptionMatches(PyExc_TypeError) ||
            PyErr_ExceptionMatches(PyExc_ValueError)) { PyErr_Clear(); return false; }
        throw p::Error("shell nation lookup failed");
    }
    p::Ref held(nation);
    const auto* gold = goldShellNames(p::string(p::Ref(PyObject_Str(nation)).get()));
    return gold && gold->count(p::string(p::Ref(PyObject_Str(name.get())).get()));
}
}

// ---- shared helpers ---------------------------------------------------------------------------------------------

p::Ref eventTypes(Battle& b, const char* missing) {
    auto types = py::optional(runtime(b, "battle_feedback_common").get(), "BATTLE_EVENT_TYPE");
    if (types.get() == Py_None) throw p::Error(missing);
    return types;
}

p::Ref battleEventsCallback(Battle& b) {
    auto callback = py::optional(b.engine.avatar.get(), "onBattleEvents");
    if (!py::callable(callback)) throw p::Error("#1513 battle-event feedback boundary is unavailable");
    return callback;
}

p::Ref eventRow(PyObject* types, const char* type, std::int64_t target, p::Ref details) {
    auto row = p::dict();
    p::set(row.get(), "eventType", p::integer(constant(types, type)));
    p::set(row.get(), "targetID", p::integer(target));
    p::set(row.get(), "count", p::integer(1));
    p::set(row.get(), "details", details);
    return row;
}

// BattleRuntime._combat_record_team (staticmethod)
std::int64_t team(const Actor& v) {
    const auto& state = v.presented.state;
    if (!has(state, "team")) throw p::Error("combat feedback record has no team");
    const auto& raw = field(state, "team").resolved();
    if (raw.kind != Value::Kind::Integer && raw.kind != Value::Kind::Number && raw.kind != Value::Kind::Text && raw.kind != Value::Kind::Boolean)
        throw p::Error("combat feedback record has invalid team");
    const std::int64_t value = integer(raw);
    if (value <= 0) throw p::Error("combat feedback record has invalid team");
    return value;
}

// combat_rules.is_he: the shell's kind (never its name: every HEAT shell contains "HE").
bool isHe(PyObject* shot) {
    auto shell = fieldOf(shot, "shell");
    if (!p::flag(shell.get())) return false;
    p::Ref kind(p::retain(Py_None));
    if (PyDict_Check(shell.get())) replace(kind, fieldOf(shell.get(), "kind"));
    else {
        replace(kind, py::optional(shell.get(), "kind"));
        if (kind.get() == Py_None) replace(kind, py::optional(py::optional(shell.get(), "type").get(), "kind"));
        if (kind.get() == Py_None) replace(kind, py::optional(py::optional(shell.get(), "type").get(), "name"));
    }
    return kind.get() != Py_None && p::string(p::Ref(PyObject_Str(kind.get())).get()) == "HIGH_EXPLOSIVE";
}

// combat_rules.shell_nominal_damage: the listed vehicle damage without the +/-25% roll.
std::int64_t shellNominalDamage(PyObject* shot) {
    auto shell = fieldOf(shot, "shell");
    if (!p::flag(shell.get())) return 0;
    auto raw = fieldOf(shell.get(), "damage");
    if (raw.get() == Py_None && !PyDict_Check(shell.get())) replace(raw, py::optional(py::optional(shell.get(), "type").get(), "damage"));
    double average = 0.;
    PyObject* first = PySequence_Check(raw.get()) && !PyString_Check(raw.get()) ? PySequence_GetItem(raw.get(), 0) : nullptr;
    PyObject* number = PyNumber_Float(first ? first : raw.get());
    Py_XDECREF(first);
    if (!number) { PyErr_Clear(); return 0; }
    average = PyFloat_AS_DOUBLE(number);
    Py_DECREF(number);
    if (!std::isfinite(average) || average <= 0.) return 0;
    return std::int64_t(average);
}

p::Ref vector(Battle& b, double x, double y, double z) {
    return py::call(b.engine.math.get(), "Vector3", p::number(x).get(), p::number(y).get(), p::number(z).get());
}

// ---- shells ------------------------------------------------------------------------------------------------------

std::pair<p::Ref, p::Ref> eventShell(Battle& b, const Actor& attacker, const Value& event) {
    auto entity = b.entity(attacker.engine_id);
    if (entity.get() == Py_None || py::attr(entity.get(), "typeDescriptor").get() == Py_None)
        throw p::Error("combat attacker descriptor is unavailable");
    auto empty = p::dict();
    auto gun = fieldOf(py::attr(entity.get(), "typeDescriptor").get(), "gun", empty.get());
    auto raw = fieldOf(gun.get(), "shots");
    p::Ref shots(PySequence_Tuple(p::flag(raw.get()) ? raw.get() : p::Ref(PyTuple_New(0)).get()));
    const Py_ssize_t count = PyTuple_GET_SIZE(shots.get());
    if (!count) throw p::Error("combat attacker has no shell descriptors");
    const std::int64_t requested = field(event, "shell_index").truth() ? integer(field(event, "shell_index")) : 0;
    const std::int64_t index = std::max<std::int64_t>(0, std::min<std::int64_t>(requested, count - 1));
    auto shot = p::retain(PyTuple_GET_ITEM(shots.get(), Py_ssize_t(index)));
    auto shell = fieldOf(shot.get(), "shell");
    if (shell.get() == Py_None) throw p::Error("combat attacker shell is unavailable");
    return {std::move(shot), std::move(shell)};
}

std::pair<std::int64_t, bool> shellFields(Battle& b, const Value& event, const Actor* attacker) {
    auto sentinel = py::optional(runtime(b, "battle_feedback_common").get(), "NONE_SHELL_TYPE");
    if (sentinel.get() == Py_None) throw p::Error("#1513 shell-type feedback sentinel is unavailable");
    const std::int64_t none_shell = toInt(sentinel.get());
    if (!attacker || source(event) != "shot" || !anyHit(text(field(event, "kind")))) return {none_shell, false};
    auto indices = py::optional(b.engine.constants.get(), "SHELL_TYPES_INDICES");
    if (!p::flag(indices.get())) throw p::Error("#1513 shell type indices are unavailable");
    p::Ref shell(p::retain(Py_None));
    try {
        auto pair = eventShell(b, *attacker, event);
        replace(shell, std::move(pair.second));
    } catch (const p::Error& e) {
        // The hit presentation owns the fatal shell contract; the log column must not cost the hit.
        if (PyErr_Occurred() && !PyErr_ExceptionMatches(PyExc_RuntimeError)) throw;
        reportShellFailure(b, describe(e));
        return {none_shell, false};
    }
    auto kind = fieldOf(shell.get(), "kind");
    if (kind.get() == Py_None) replace(kind, fieldOf(fieldOf(shell.get(), "type").get(), "name"));
    const int known = PySequence_Contains(indices.get(), kind.get());
    if (known < 0) throw p::Error("shell type lookup failed");
    if (!known) {
        reportShellFailure(b, "shell kind is not a #1513 shell type: " + p::string(p::Ref(PyObject_Repr(kind.get())).get()));
        return {none_shell, false};
    }
    const std::int64_t index = toInt(p::Ref(PyObject_GetItem(indices.get(), kind.get())).get());
    return {index, shellIsGold(b, shell.get())};
}

// ---- combat rows -------------------------------------------------------------------------------------------------

// BattleRuntime._is_blind_local_attack
bool blindLocalAttack(const Battle& b, const Actor& target, const Actor& attacker) {
    if (!attacker.local || target.local) return false;
    if (team(target) == team(attacker)) return false;
    return !target.presented.spot_marker_visible;
}

// BattleRuntime._present_combat_feedback: DAMAGE/CRIT/KILL, shot results and received-damage rows.
bool combat(Battle& b, const Value& event, Actor& target, Actor& attacker, std::optional<std::int64_t> reason) {
    if (b.worker) return false;
    const bool attributed_terrain = text(field(event, "source")) == "environment" && isTrue(field(event, "push_attributed"));
    // A confirmed shove's terrain outcome remains credited after the target falls out of spotting.
    if (blindLocalAttack(b, target, attacker) && !attributed_terrain) return false;
    auto types = eventTypes(b, "#1513 battle feedback constants are unavailable");
    const std::int64_t damage = nonNegative(event, "damage");
    auto reason_value = [&] { return reason ? p::integer(*reason) : p::retain(Py_None); };
    const auto shell = shellFields(b, event, &attacker);
    auto shell_type = [&] { return p::integer(shell.first); };
    const auto& items = field(field(event, "critical"), "events").resolved();
    // CRIT counts newly damaged devices/crew, not fire, ammo-rack death effects or repairs.
    std::vector<std::size_t> critical_events;
    for (std::size_t i = 0; i < items.array.size(); ++i) {
        const auto& item = items.array[i];
        const std::string kind = text(field(item, "kind")), state = text(field(item, "state"));
        if (kind == "crew" && state == "destroyed") critical_events.push_back(i);
        else if (kind == "device" && (state == "critical" || state == "destroyed")) {
            const std::string old = has(item, "old_state") ? text(field(item, "old_state")) : "normal";
            if (old != state && old != "destroyed") critical_events.push_back(i);
        }
    }
    std::int64_t critical_count = std::int64_t(critical_events.size());
    if (isTrue(field(event, "critical_feedback_separate"))) critical_count = 0;
    if (text(field(event, "kind")) == "environment_critical") critical_count = integer(field(event, "critical_count"));
    if (attacker.local) assertPlayerIdentity(b, attacker.engine_id);
    const bool enemy = team(target) != team(attacker);
    auto avatar = b.engine.avatar.get();
    if (attacker.local && enemy) {
        compat::assertMarkerDamageType(b, avatar, attacker.engine_id);
    }
    auto output = p::list();
    auto packDamage = [&](std::int64_t amount) {
        return p::integer(toInt(py::call(types.get(), "packDamage", p::integer(amount).get(), reason_value().get(), Py_False, shell_type().get(),
                                         shell.second ? Py_True : Py_False).get()));
    };
    auto packCrits = [&] {
        return p::integer(toInt(py::call(types.get(), "packCrits", p::integer(critical_count).get(), reason_value().get(), shell_type().get(),
                                         shell.second ? Py_True : Py_False).get()));
    };
    const bool dead = field(event, "dead").truth();
    if (attacker.local && enemy) {
        if (damage > 0) p::append(output.get(), eventRow(types.get(), "DAMAGE", target.engine_id, packDamage(damage)));
        if (critical_count > 0) p::append(output.get(), eventRow(types.get(), "CRIT", target.engine_id, packCrits()));
        if (dead) p::append(output.get(), eventRow(types.get(), "KILL", target.engine_id, p::integer(0)));
    }
    if (attacker.local && source(event) == "shot" && anyHit(text(field(event, "kind")))) {
        auto flags_type = py::optional(b.engine.constants.get(), "VEHICLE_HIT_FLAGS");
        if (flags_type.get() == Py_None) throw p::Error("#1513 VEHICLE_HIT_FLAGS are unavailable");
        auto flag = [&](const char* name) { return constant(flags_type.get(), name); };
        const std::int64_t shot_result = std::max<std::int64_t>(0, std::min<std::int64_t>(integer(field(event, "shot_result"), 2), 2));
        const bool external_blast = field(event, "splash").truth();
        bool direct_he = false;
        if (!external_blast) {
            auto indices = py::attr(b.engine.constants.get(), "SHELL_TYPES_INDICES");
            direct_he = shell.first == toInt(p::Ref(PyObject_GetItem(indices.get(), p::text("HIGH_EXPLOSIVE").get())).get());
        }
        // Origin (direct / nearby) is independent of blast damage.
        const bool blast_damage = external_blast || (direct_he && shot_result != 2);
        std::int64_t flags = flag(external_blast ? "ATTACK_IS_EXTERNAL_EXPLOSION" : "ATTACK_IS_DIRECT_PROJECTILE");
        if (external_blast) {
            if (damage > 0) flags |= flag("MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_EXPLOSION");
        } else if (direct_he) {
            if (damage <= 0) flags |= flag("MATERIAL_WITH_POSITIVE_DF_NOT_PIERCED_BY_PROJECTILE");
            else if (shot_result == 2) flags |= flag("MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE");
            else flags |= flag("MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_EXPLOSION");
        } else if (shot_result == 2) flags |= flag("MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE");
        else if (shot_result == 1) flags |= flag("MATERIAL_WITH_POSITIVE_DF_NOT_PIERCED_BY_PROJECTILE");
        else flags |= flag("RICOCHET");
        const std::string critical_cause = blast_damage ? "explosion" : "shot";
        const std::int64_t pierced = flag(blast_damage ? "DEVICE_PIERCED_BY_EXPLOSION" : "DEVICE_PIERCED_BY_PROJECTILE");
        const std::int64_t device = flag(blast_damage ? "DEVICE_DAMAGED_BY_EXPLOSION" : "DEVICE_DAMAGED_BY_PROJECTILE");
        const std::int64_t chassis = flag(blast_damage ? "CHASSIS_DAMAGED_BY_EXPLOSION" : "CHASSIS_DAMAGED_BY_PROJECTILE");
        const std::int64_t gun = flag(blast_damage ? "GUN_DAMAGED_BY_EXPLOSION" : "GUN_DAMAGED_BY_PROJECTILE");
        for (std::size_t i = 0; i < items.array.size(); ++i) {
            const auto& item = items.array[i];
            const std::string cause = has(item, "cause") ? text(field(item, "cause")) : "shot";
            if (cause != critical_cause) continue;
            const std::string kind = text(field(item, "kind"));
            if (kind == "fire" && field(item, "state").truth()) flags |= flag("FIRE_STARTED");
            else if (std::find(critical_events.begin(), critical_events.end(), i) != critical_events.end()) {
                // #1513's voice selector tests DEVICE_PIERCED for module-only hits and ricochets.
                flags |= pierced;
                if (kind != "device") continue;
                const std::string name = text(field(item, "name"));
                flags |= device;
                if (name == "leftTrackHealth" || name == "rightTrackHealth") flags |= chassis;
                else if (name == "gunHealth") flags |= gun;
            }
        }
        // A module hit must not replace the requested plain HE voice.
        if (direct_he)
            flags &= ~(flag("GUN_DAMAGED_BY_PROJECTILE") | flag("GUN_DAMAGED_BY_EXPLOSION") | flag("CHASSIS_DAMAGED_BY_PROJECTILE") |
                       flag("CHASSIS_DAMAGED_BY_EXPLOSION"));
        if (dead) flags |= flag("VEHICLE_KILLED");
        auto callback = py::optional(avatar, "showShotResults");
        if (!py::callable(callback)) throw p::Error("#1513 shot-result feedback boundary is unavailable");
        bool internal_destroyed = false;
        for (std::size_t i : critical_events) {
            const auto& item = items.array[i];
            if (text(field(item, "state")) != "destroyed") continue;
            const std::string kind = text(field(item, "kind")), name = text(field(item, "name"));
            const bool internal = kind == "crew" || (kind == "device" && name != "leftTrackHealth" && name != "rightTrackHealth" && name != "gunHealth");
            if (internal && (has(item, "cause") ? text(field(item, "cause")) : "shot") == "shot") { internal_destroyed = true; break; }
        }
        p::Ref shifted(PyNumber_Lshift(p::integer(flags).get(), p::integer(32).get()));
        p::Ref packed(PyNumber_Or(shifted.get(), p::integer(target.engine_id).get()));
        p::Ref results(PyList_New(1));
        PyList_SET_ITEM(results.get(), 0, packed.release());
        if (enemy && damage <= 0 && shot_result == 2 && internal_destroyed && !direct_he && !external_blast && !dead)
            presentCriticalVoice(b, callback.get(), results.get());
        else
            py::invoke(callback.get(), results.get());
    }
    if (target.local && (enemy || isTrue(field(event, "push_attributed")))) {
        const std::int64_t blocked = nonNegative(event, "blocked_damage");
        if (blocked > 0) p::append(output.get(), eventRow(types.get(), "TANKING", attacker.engine_id, packDamage(blocked)));
        if (damage > 0) p::append(output.get(), eventRow(types.get(), "RECEIVED_DAMAGE", attacker.engine_id, packDamage(damage)));
        if (critical_count > 0) p::append(output.get(), eventRow(types.get(), "RECEIVED_CRIT", attacker.engine_id, packCrits()));
    }
    const bool any = PyList_GET_SIZE(output.get()) != 0;
    if (any) py::invoke(battleEventsCallback(b).get(), output.get());
    return any;
}

// BattleRuntime._present_environment_feedback: attacker-free fall or fire damage to the stock damage log.
bool environment(Battle& b, const Value& event, Actor& target, std::optional<std::int64_t> reason) {
    const std::int64_t world_collision = vehicles::attackReason(b, "WORLD_COLLISION", 3);
    const std::string src = source(event);
    const bool unowned_fire = src == "fire" && text(field(event, "kind")) == "health" && reason == vehicles::attackReason(b, "FIRE", 1);
    if (b.worker || !target.local || !(unowned_fire || (src == "environment" && reason == world_collision))) return false;
    const std::int64_t damage = nonNegative(event, "damage");
    if (damage <= 0) return false;
    auto types = eventTypes(b, "#1513 battle feedback constants are unavailable");
    auto callback = battleEventsCallback(b);
    auto details = p::integer(toInt(py::call(types.get(), "packDamage", p::integer(damage).get(),
                                             reason ? p::integer(*reason).get() : Py_None).get()));
    auto rows = p::list();
    p::append(rows.get(), eventRow(types.get(), "RECEIVED_DAMAGE", target.engine_id, std::move(details)));
    py::invoke(callback.get(), rows.get());
    return true;
}

// BattleRuntime._apply_assist_event: one server-attributed assist to the damage log.
bool assist(Battle& b, const Value& event) {
    if (b.worker) return false;
    Actor* assister = presentedVehicle(b, feedbackKey(event, "assister", "assist"));
    if (!assister || !assister->local) return false;
    Actor* target = presentedVehicle(b, feedbackKey(event, "target", "assist"));
    if (!target) throw p::Error("assist event has no known target");
    const std::string category = text(field(event, "category"));
    const char* name = category == "radio" ? "RADIO_ASSIST" : category == "track" ? "TRACK_ASSIST" : category == "stun" ? "STUN_ASSIST" : nullptr;
    if (!name) throw p::Error("assist category is unsupported: " + (isNull(field(event, "category")) ? std::string("None") : idText(field(event, "category"))));
    auto types = eventTypes(b, "#1513 battle feedback constants are unavailable");
    auto callback = battleEventsCallback(b);
    const std::int64_t damage = nonNegative(event, "damage");
    auto details = p::integer(toInt(py::call(types.get(), "packDamage", p::integer(damage).get(),
                                             p::integer(vehicles::attackReason(b, "SHOT", 0)).get()).get()));
    auto rows = p::list();
    p::append(rows.get(), eventRow(types.get(), name, target->engine_id, std::move(details)));
    py::invoke(callback.get(), rows.get());
    return true;
}

namespace {
// BattleRuntime._present_direct_spot: the stock ribbon for a credited detection, once per vehicle.
bool directSpot(Battle& b, Actor& target) {
    if (b.feedback->spot_feedback_sent.count(target.engine_id)) return false;
    auto types = eventTypes(b, "#1513 spotting feedback constants are unavailable");
    auto pack = py::optional(types.get(), "packVisibility");
    if (!py::callable(pack)) throw p::Error("#1513 visibility feedback packer is unavailable");
    auto callback = battleEventsCallback(b);
    auto rows = p::list();
    p::append(rows.get(), eventRow(types.get(), "SPOTTED", target.engine_id, p::integer(0)));
    p::append(rows.get(), eventRow(types.get(), "TARGET_VISIBILITY", target.engine_id,
                                   p::integer(toInt(py::invoke(pack.get(), Py_True, Py_True).get()))));
    py::invoke(callback.get(), rows.get());
    b.feedback->spot_feedback_sent.insert(target.engine_id);
    return true;
}
}

// BattleRuntime._apply_detection_event: the spotting ribbon the server credited to the local player.
bool detection(Battle& b, const Value& event) {
    if (b.worker) return false;
    Actor* observer = presentedVehicle(b, feedbackKey(event, "observer", "detection"));
    if (!observer || !observer->local) return false;
    Actor* target = presentedVehicle(b, feedbackKey(event, "target", "detection"));
    if (!target) throw p::Error("detection event has no known target");
    b.feature("spotting feedback", [&] { directSpot(b, *target); });
    return true;
}

// BattleRuntime._apply_environment_critical_feedback: an environment critical checkpoint as CRIT rows.
bool environmentCritical(Battle& b, const Value& event) {
    if (b.worker) return false;
    const auto& count = field(event, "critical_count").resolved();
    const std::string cause = text(field(event, "cause"));
    const std::int64_t reason = cause == "world_collision" ? 3 : cause == "drowning" ? 5 : cause == "fire" ? 1 : -1;
    if (count.kind != Value::Kind::Integer || count.integer < 1 || count.integer > 65535 || reason < 0) return false;
    const auto target_key = entityKey(event, false), attacker_key = entityKey(event, true);
    Actor* target = target_key ? presentedVehicle(b, *target_key) : nullptr;
    Actor* attacker = attacker_key ? presentedVehicle(b, *attacker_key) : nullptr;
    if (!target || !attacker) return false;
    return combat(b, event, *target, *attacker, reason);
}

// BattleRuntime._present_damage_sticker: one admitted direct hit to the stock sticker owner.
bool damageSticker(Battle& b, const Value& event, Actor& target) {
    const char* feature_name = "projectile damage stickers";
    if (b.worker || !has(event, "damage_sticker") || field(event, "splash").truth() || !b.featureEnabled(feature_name)) return false;
    auto entity = b.entity(target.engine_id);
    if (entity.get() == Py_None) return false;
    if (!p::flag(py::optional(entity.get(), "isStarted").get()) || py::optional(entity.get(), "typeDescriptor").get() == Py_None) return false;
    bool added = false;
    b.feature(feature_name, [&] {
        p::Ref effects(PyImport_ImportModule("VehicleEffects"));
        auto decoder = py::attr(effects.get(), "DamageFromShotDecoder");
        p::Ref code(cs::write(field(event, "damage_sticker")));
        auto decoded = py::call(decoder.get(), "decodeSegment", code.get(), py::attr(entity.get(), "typeDescriptor").get());
        if (PySequence_Size(decoded.get()) != 4) throw p::Error("damage sticker decode has an unexpected shape");
        auto component = p::item(decoded.get(), 0), sticker = p::item(decoded.get(), 1), start = p::item(decoded.get(), 2), end = p::item(decoded.get(), 3);
        // #1513 VehicleStickers creates only hull/turret/gun owners.
        if (PyString_Check(component.get()) && p::string(component.get()) == "chassis") return;
        auto add = py::optional(py::optional(entity.get(), "appearance").get(), "addDamageSticker");
        const int same = PyObject_RichCompareBool(start.get(), end.get(), Py_EQ);
        if (same < 0) throw p::Error("damage sticker comparison failed");
        if (!p::flag(component.get()) || start.get() == Py_None || end.get() == Py_None || same || !py::callable(add))
            throw p::Error("#1513 damage-sticker boundary is unavailable");
        py::invoke(add.get(), code.get(), component.get(), sticker.get(), start.get(), end.get());
        added = true;
    }, false);
    return added;
}

// BattleRuntime._apply_battle_result: the round result to the stock Avatar, once.
bool battleResult(Battle& b, const Value& result) {
    if (result.resolved().kind != Value::Kind::Object) return false;
    b.result = result.resolved();
    auto& fb = *b.feedback;
    if (b.worker) {
        // Stop authority simulation during the server's result-publication window.
        b.live = false;
        fb.round_finished_notified = true;
        return true;
    }
    if (fb.round_finished_notified || !b.engine.avatar || !b.running()) return false;
    auto finish = py::optional(b.engine.constants.get(), "FINISH_REASON");
    if (finish.get() == Py_None) throw p::Error("FINISH_REASON is unavailable");
    std::string name = has(result, "reason") ? idText(field(result, "reason")) : std::string();
    if (isNull(field(result, "reason")) && has(result, "reason")) name = "none";
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    auto contains = [&](const char* part) { return name.find(part) != std::string::npos; };
    p::Ref reason(p::retain(Py_None));
    if (contains("eliminat") || contains("exterminat") || name == "team_eliminated") replace(reason, py::attr(finish.get(), "EXTERMINATION"));
    else if (contains("base") || contains("captur")) replace(reason, py::attr(finish.get(), "BASE"));
    else if (contains("timeout") || contains("time_out")) replace(reason, py::attr(finish.get(), "TIMEOUT"));
    else {
        auto unknown = py::optional(finish.get(), "UNKNOWN");
        if (unknown.get() == Py_None) replace(unknown, p::integer(4));
        auto failure = py::optional(finish.get(), "FAILURE");
        replace(reason, failure.get() == Py_None ? std::move(unknown) : std::move(failure));
    }
    auto avatar = b.engine.avatar.get();
    auto callback = py::optional(avatar, "onRoundFinished");
    if (!py::callable(callback)) throw p::Error("Avatar.onRoundFinished is unavailable");
    auto clamp = [&](const char* name) { return std::max<std::int64_t>(0, std::min<std::int64_t>(integer(field(result, name)), 2)); };
    const std::int64_t winner = clamp("winner"), base = clamp("base_team");
    if (base == 1 || base == 2) {
        auto captured = py::optional(py::optional(avatar, "arena").get(), "onTeamBaseCaptured");
        if (py::callable(captured)) py::invoke(captured.get(), p::integer(base).get(), p::integer(0).get());
    }
    // Publish the afterbattle period first so its owners hold the outcome before the notification runs.
    b.feature("afterbattle arena period", [&] {
        startup::binding::arenaPeriod(b, "afterbattle", 0., std::make_pair(winner, std::int64_t(toInt(reason.get()))));
    });
    py::invoke(callback.get(), p::integer(winner).get(), reason.get());
    fb.round_finished_notified = true;
    return true;
}

void reportLocalKill(Battle&, const Actor&, std::int64_t, std::int64_t) {}

// ---- small feedback helpers -----------------------------------------------------------------------------------------

// BattleRuntime._hit_damage_factor: hull damage as a percent of maximum health.
double hitDamageFactor(Battle& b, const Value& event, const Actor& target) {
    const std::int64_t damage = nonNegative(event, "damage");
    if (damage <= 0) return 0.;
    double maximum = field(target.presented.state, "max_health").resolved().numeric(0.);
    if (maximum <= 0.) {
        auto entity = b.entity(target.engine_id);
        auto descriptor = py::optional(entity.get(), "typeDescriptor");
        auto health = fieldOf(descriptor.get(), "maxHealth");
        PyObject* number = health.get() != Py_None ? PyNumber_Float(health.get()) : nullptr;
        if (number) { maximum = PyFloat_AS_DOUBLE(number); Py_DECREF(number); }
        else PyErr_Clear();
        if (!std::isfinite(maximum)) maximum = 0.;
    }
    if (maximum <= 0.) return 0.;
    return std::max(0., std::min(100., 100. * double(damage) / maximum));
}

// BattleRuntime._surface_effect_material: one EFFECT_MATERIALS name at a point, or none.
std::optional<std::string> surfaceMaterial(Battle& b, const Point& at) {
    auto calculation = runtime(b, "effect_material_calculation");
    auto materials = runtime(b, "material_kinds");
    if (calculation.get() == Py_None || materials.get() == Py_None) return {};
    try {
        auto surface = py::call(calculation.get(), "calcSurfaceMaterialNearPoint", vector(b, at[0], at[1], at[2]).get(), vector(b, 0., 1., 0.).get(),
                                py::attr(b.engine.avatar.get(), "spaceID").get());
        auto index = py::attr(surface.get(), "effectIdx");
        if (index.get() == Py_None) return {};
        p::Ref name(PyObject_GetItem(py::attr(materials.get(), "EFFECT_MATERIALS").get(), p::integer(toInt(index.get())).get()));
        return p::string(p::Ref(PyObject_Str(name.get())).get());
    } catch (const std::exception&) {
        PyErr_Clear();
        return {};
    }
}

// BattleRuntime._release_target_lock
bool releaseTargetLock(Battle& b, std::int64_t engine_id) {
    if (!b.engine.avatar) return false;
    return compat::releaseTargetLock(b, b.engine.avatar.get(), engine_id);
}
}
}
