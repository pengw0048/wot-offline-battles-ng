// Critical state edges of one vehicle: canonical payloads, the player's revision contract and stock damage info.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include <algorithm>
#include <set>

namespace native_battle {
namespace vehicles {
namespace {
const Value& field(const Value& v, const char* name) { return v.resolved().get(name); }
bool has(const Value& v, const char* name) {
    const auto& r = v.resolved();
    return r.kind == Value::Kind::Object && r.object.count(name);
}
std::int64_t integer(const Value& v, std::int64_t fallback = 0) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Null) return fallback;
    if (r.kind == Value::Kind::Integer) return r.integer;
    return std::int64_t(r.numeric(double(fallback)));
}
Value& writable(Value& state) {
    if (state.reference) { Value copy = *state.reference; state = std::move(copy); }
    if (state.kind != Value::Kind::Object) state.kind = Value::Kind::Object;
    return state;
}
// BattleRuntime._critical_state(payload): a copy with no pending presentation events.
std::optional<Value> canonicalOf(const Value& payload) {
    if (payload.resolved().kind != Value::Kind::Object) return {};
    Value result = payload.resolved();
    writable(result).object["events"].kind = Value::Kind::Array;
    result.object["events"].array.clear();
    return result;
}
std::string text(PyObject* v) { return v && v != Py_None ? p::string(p::Ref(PyObject_Str(v)).get()) : std::string(); }

void clearCriticalHUD(cs::Presentation& d) {
    d.critical_hud_pending.clear();
    d.critical_hud_order.clear();
    d.critical_hud_entity = cs::Handle{};
    d.critical_hud_engine_id = 0;
    d.critical_hud_epoch = 0;
}

bool flushCriticalHUD(Battle& b, Actor& v) {
    auto& d = v.presented;
    if (d.critical_hud_pending.empty()) { clearCriticalHUD(d); return false; }
    const auto owner = b.vehicles;
    if (!owner || owner->retired) { clearCriticalHUD(d); return false; }
    const auto found = owner->actors.find(v.key);
    if (found == owner->actors.end() || found->second.get() != &v) {
        clearCriticalHUD(d); return false;
    }
    const auto actor = found->second;
    const auto epoch = owner->epoch;
    auto entity = b.entity(v.engine_id);
    auto current = [&] {
        return b.phase != Phase::Failed && b.phase != Phase::Stopped && b.phase != Phase::Leaving &&
            !b.worker && b.vehicles == owner && !owner->retired && owner->epoch == epoch && epoch == d.critical_hud_epoch &&
            b.vehicle(v.key) == actor.get() && v.local && !v.tombstone && !v.missing &&
            (!d.health || ((*d.health)[0] > 0 && (*d.health)[2])) &&
            v.engine_id == d.critical_hud_engine_id && entity.get() != Py_None &&
            entity.get() == d.critical_hud_entity.get() && b.entity(v.engine_id).get() == entity.get();
    };
    if (!current()) { clearCriticalHUD(d); return false; }
    if (b.phase == Phase::Idle || b.phase == Phase::Loading || !v.ready) return false;
    bool shown = false;
    const auto order = d.critical_hud_order;
    for (const auto& key : order) {
        if (!current()) { clearCriticalHUD(d); return shown; }
        if (!v.ready) return shown;
        const auto at = d.critical_hud_pending.find(key);
        if (at == d.critical_hud_pending.end()) continue;
        auto edge = at->second;
        // The native damage owner may have repaired this device even while the
        // stock controllers were absent. Never deliver its obsolete red edge.
        if (!critical::eventCurrent(b, entity.get(), edge.event)) { d.critical_hud_pending.erase(at); continue; }
        if (!damageInfoServiceable(b)) continue;
        auto descriptor = py::optional(entity.get(), "typeDescriptor");
        if (descriptor.get() == Py_None) continue;
        auto attached = py::optional(b.engine.avatar.get(), "getVehicleAttached");
        if (py::callable(attached)) {
            std::optional<std::int64_t> attached_id;
            if (!b.feature("critical HUD attachment", [&] {
                    auto vehicle = py::invoke(attached.get());
                    if (vehicle.get() != Py_None) attached_id = p::wide(py::attr(vehicle.get(), "id").get());
                }, false) || !attached_id) continue;
            if (*attached_id != v.engine_id) { clearCriticalHUD(d); return shown; }
        }
        const auto& kind = field(edge.event, "kind").resolved().text;
        if ((kind == "device" || kind == "crew") && !b.feature("critical HUD descriptor", [&] {
                edge.extra = criticalExtraIndex(descriptor.get(), field(edge.event, "name").resolved().text);
            }, false)) continue;
        if (!showDamageInfo(b, v.engine_id, edge.index, edge.extra, edge.attacker)) continue;
        shown = true;
        if (!current()) { clearCriticalHUD(d); return shown; }
        // A stock callback may synchronously install a newer edge on this key.
        const auto pending = d.critical_hud_pending.find(key);
        if (pending != d.critical_hud_pending.end() && pending->second.event == edge.event && pending->second.attacker == edge.attacker)
            d.critical_hud_pending.erase(pending);
    }
    d.critical_hud_order.erase(std::remove_if(d.critical_hud_order.begin(), d.critical_hud_order.end(),
        [&](const std::string& key) { return !d.critical_hud_pending.count(key); }), d.critical_hud_order.end());
    if (d.critical_hud_pending.empty()) clearCriticalHUD(d);
    return shown;
}
}

void flushCriticalPresentations(Battle& b) {
    if (!b.vehicles) return;
    std::vector<std::shared_ptr<Actor>> actors;
    for (const auto& row : b.vehicles->actors)
        if (!row.second->presented.critical_hud_pending.empty()) actors.push_back(row.second);
    for (const auto& actor : actors) flushCriticalHUD(b, *actor);
}

// BattleRuntime._reconcile_critical_authority: the player's revision contract (native local owner or canonical fallback).
bool reconcileCriticalAuthority(Battle& b, Actor& v, const Value& source, Value* canonical) {
    return critical::reconcileAuthority(b, v, source, canonical);
}

// BattleRuntime._apply_critical_state(record, payload, authority=None)
bool applyCritical(Battle& b, Actor& v, const Value& payload, const Value* authority) {
    auto canonical = canonicalOf(payload);
    if (!canonical) return false;
    auto& d = v.presented;
    if (authority && !reconcileCriticalAuthority(b, v, *authority, &*canonical)) {
        if (v.local && d.critical.truth()) writable(d.state).object["critical"] = d.critical;
        return false;
    }
    if (*canonical == d.critical) return false;
    auto entity = b.entity(v.engine_id);
    if (entity.get() == Py_None) return false;
    const bool edge = v.local && b.gun;
    if (edge) gun::edge(b);
    auto events = critical::applyPayload(b, entity.get(), *canonical);
    if (edge) gun::descriptorChanged(b, entity.get());
    d.critical = *canonical;
    writable(d.state).object["critical"] = *canonical;
    if (!b.worker) presentCritical(b, v, events, 0);
    return true;
}

// BattleRuntime._present_critical: critical transitions through the stock #1513 UI callbacks.
bool presentCritical(Battle& b, Actor& v, const Value& events, std::int64_t attacker) {
    if (b.worker || (!v.local && !v.presented.spot_visible)) return false;
    auto entity = b.entity(v.engine_id);
    if (entity.get() == Py_None || py::attr(entity.get(), "typeDescriptor").get() == Py_None) return false;
    b.feature("critical fire effect", [&] { syncFireEffect(b, entity.get()); }, false);
    const auto& rows = events.resolved().array;
    if (rows.empty()) return flushCriticalHUD(b, v);
    bool shown = false;
    auto get = [](const Value& row, const char* name) { return field(row, name).resolved().text; };
    for (const auto& row : rows) {
        if (get(row, "kind") != "ammo_rack" || get(row, "state") != "destroyed") continue;
        auto callback = py::optional(entity.get(), "showAmmoBayEffect");
        if (!py::callable(callback)) throw p::Error("#1513 ammo-bay effect boundary is unavailable");
        auto modes = py::optional(b.engine.constants.get(), "AMMOBAY_DESTRUCTION_MODE");
        if (modes.get() == Py_None || !PyObject_HasAttrString(modes.get(), "HE_DETONATION")) throw p::Error("#1513 ammo-bay destruction mode is unavailable");
        if (b.feature("ammo-bay effect", [&] {
                py::invoke(callback.get(), py::attr(modes.get(), "HE_DETONATION").get(), p::number(0.).get(), p::number(0.).get());
            }, false)) shown = true;
    }
    if (!v.local || !b.engine.avatar) return shown;
    auto& hud = v.presented;
    const auto epoch = b.vehicles ? b.vehicles->epoch : 0;
    if (hud.critical_hud_engine_id != v.engine_id || hud.critical_hud_entity.get() != entity.get() || hud.critical_hud_epoch != epoch)
        clearCriticalHUD(hud);
    auto indices = py::optional(b.engine.constants.get(), "DAMAGE_INFO_INDICES");
    auto suffix = [](const std::string& cause) -> std::string {
        if (cause == "fire") return "_AT_FIRE";
        if (cause == "ramming") return "_AT_RAMMING";
        if (cause == "world_collision") return "_AT_WORLD_COLLISION";
        if (cause == "drowning") return "_AT_DROWNING";
        return "_AT_SHOT";
    };
    auto descriptor = py::attr(entity.get(), "typeDescriptor");
    for (const auto& row : rows) {
        const std::string kind = get(row, "kind"), state = get(row, "state"), old = get(row, "old_state");
        std::string cause = get(row, "cause");
        if (cause.empty()) cause = "shot";
        std::int64_t extra = 0;
        std::string code;
        auto extraIndex = [&](const char* why) {
            extra = criticalExtraIndex(descriptor.get(), get(row, "name"));
            if (extra <= 0) throw p::Error(why);
        };
        if (kind == "device") {
            extraIndex("#1513 device critical extra is invalid");
            const bool repaired = cause == "repair" || (old == "destroyed" && (state == "critical" || state == "normal")) || (old == "critical" && state == "normal");
            if (repaired) code = state == "normal" ? "DEVICE_REPAIRED" : "DEVICE_REPAIRED_TO_CRITICAL";
            else code = std::string(state == "destroyed" ? "DEVICE_DESTROYED" : "DEVICE_CRITICAL") + suffix(cause);
        } else if (kind == "crew") {
            extraIndex("#1513 crew critical extra is invalid");
            if (state == "normal") code = "TANKMAN_RESTORED";
            else if (cause == "world_collision" || cause == "drowning") code = "TANKMAN_HIT" + suffix(cause);
            else if (cause == "fire") code = "TANKMAN_HIT";
            else code = "TANKMAN_HIT_AT_SHOT";
        } else if (kind == "fire") {
            const bool burning = field(row, "state").truth();
            code = burning ? (cause == "ramming" ? "DEVICE_STARTED_FIRE_AT_RAMMING" : "DEVICE_STARTED_FIRE_AT_SHOT") : "FIRE_STOPPED";
        } else continue;
        PyObject* index = indices.get() != Py_None ? PyObject_GetItem(indices.get(), p::text(code).get()) : nullptr;
        if (!index) { PyErr_Clear(); throw p::Error("#1513 damage-info index is unavailable: " + code); }
        p::Ref held(index);
        cs::Presentation::CriticalNotification edge;
        edge.event = row;
        edge.attacker = attacker;
        edge.index = p::wide(index);
        edge.extra = extra;
        const std::string key = kind + ":" + get(row, "name");
        hud.critical_hud_engine_id = v.engine_id;
        hud.critical_hud_epoch = epoch;
        hud.critical_hud_entity = cs::Handle(p::retain(entity.get()).release());
        if (!hud.critical_hud_pending.count(key)) hud.critical_hud_order.push_back(key);
        hud.critical_hud_pending[key] = std::move(edge);
    }
    return flushCriticalHUD(b, v) || shown;
}

// BattleRuntime._critical_extra_index: the exact descriptor extra index #1513 Avatar uses.
std::int64_t criticalExtraIndex(PyObject* descriptor, const std::string& raw) {
    std::string name = raw;
    if (name.size() < 6 || name.compare(name.size() - 6, 6, "Health") != 0) name += "Health";
    auto extras_dict = py::optional(descriptor, "extrasDict");
    p::Ref selected(p::retain(Py_None));
    if (extras_dict.get() != Py_None) replace(selected, py::call(extras_dict.get(), "get", p::text(name).get()));
    auto extras = py::optional(descriptor, "extras");
    if (extras.get() != Py_None) {
        p::Ref rows(PySequence_List(extras.get()));
        for (Py_ssize_t i = 0; i < PyList_GET_SIZE(rows.get()); ++i) {
            PyObject* extra = PyList_GET_ITEM(rows.get(), i);
            if (extra == selected.get() || p::string(py::optional(extra, "name").get()) == name) return i;
        }
    }
    const std::int64_t index = selected.get() != Py_None ? std::int64_t(py::optional(selected.get(), "index").get() != Py_None
                                                                          ? p::wide(py::attr(selected.get(), "index").get()) : 0) : 0;
    if (index <= 0) throw p::Error("#1513 descriptor has no critical extra: " + name);
    return index;
}

// BattleRuntime._damage_info_is_serviceable: stock can still service a damage-info notification.
bool damageInfoServiceable(Battle& b) {
    auto shared = py::optional(py::optional(b.engine.avatar.get(), "guiSessionProvider").get(), "shared");
    if (shared.get() == Py_None) return false;
    if (py::optional(shared.get(), "messages").get() == Py_None || py::optional(shared.get(), "vehicleState").get() == Py_None) return false;
    if (py::optional(b.engine.avatar.get(), "vehicleTypeDescriptor").get() == Py_None) return false;
    auto loader = py::optional(b.engine.modules.get(), "app_loader");
    auto battle_app = py::optional(loader.get(), "getDefBattleApp");
    if (py::callable(battle_app) && py::invoke(battle_app.get()).get() == Py_None) return false;
    return true;
}

// BattleRuntime._show_damage_info: one stock damage-info notification, contained on failure.
bool showDamageInfo(Battle& b, std::int64_t engine_id, std::int64_t damage_index, std::int64_t extra_index, std::int64_t attacker) {
    return b.feature("damage-info presentation", [&] {
        auto codes = py::optional(b.engine.constants.get(), "DAMAGE_INFO_CODES");
        auto extras = py::optional(py::attr(b.engine.avatar.get(), "vehicleTypeDescriptor").get(), "extras");
        if (damage_index < 0 || damage_index >= PyObject_Size(codes.get()))
            throw p::Error("#1513 damage-info index is out of range: " + std::to_string(damage_index));
        if (extra_index < 0 || extra_index >= PyObject_Size(extras.get()))
            throw p::Error("#1513 damage-info extra index is out of range: " + std::to_string(extra_index));
        py::call(b.engine.avatar.get(), "showVehicleDamageInfo", p::integer(engine_id).get(), p::integer(damage_index).get(),
                 p::integer(extra_index).get(), p::integer(attacker).get(), p::integer(0).get());
    }, false);
}
}
}
