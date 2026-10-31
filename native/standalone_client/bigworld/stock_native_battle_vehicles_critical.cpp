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
}

// acknowledge_local_damage_report: retire only a checkpoint the server canonically acknowledged.
bool acknowledgeLocalDamageReport(Battle& b, std::int64_t base_revision, std::int64_t ack_seq, std::int64_t server_revision) {
    auto& c = localCritical(b);
    base_revision = std::max<std::int64_t>(0, base_revision);
    ack_seq = std::max<std::int64_t>(0, ack_seq);
    server_revision = std::max<std::int64_t>(0, server_revision);
    if (server_revision < c.server_revision) return false;
    c.server_revision = server_revision;
    if (base_revision != c.base_revision) {
        c.base_revision = base_revision;
        c.next_seq = 0;
        c.owned = false;
        c.report.reset();
        return false;
    }
    if (c.report && ack_seq >= integer(field(*c.report, "critical_seq"))) {
        const auto& tracks = field(*c.report, "tracks").resolved();
        bool repair_complete = tracks.kind == Value::Kind::Array && !tracks.array.empty();
        for (const auto& row : tracks.array) repair_complete = repair_complete && field(row, "state").resolved().text == "critical";
        c.report.reset();
        c.owned = !repair_complete;
        return true;
    }
    return false;
}

// BattleRuntime._reconcile_critical_authority(record, source, canonical=None)
bool reconcileCriticalAuthority(Battle& b, Actor& v, const Value& source, Value* canonical) {
    if (auto native = critical::reconcile(b, v, source, canonical)) return *native;
    if (v.kind != "player") return true;
    for (const char* name : {"critical_revision", "critical_base_revision", "critical_ack_seq"})
        if (!has(source, name)) throw p::Error("#1513 player critical state has no revision contract");
    const std::int64_t revision = std::max<std::int64_t>(0, integer(field(source, "critical_revision")));
    const std::int64_t base_revision = std::max<std::int64_t>(0, integer(field(source, "critical_base_revision")));
    const std::int64_t ack_seq = std::max<std::int64_t>(0, integer(field(source, "critical_ack_seq")));
    auto& d = v.presented;
    const std::int64_t previous = d.critical_revision;
    if (revision < previous) return false;
    d.critical_revision = revision;
    d.critical_base_revision = base_revision;
    d.critical_ack_seq = ack_seq;
    if (v.local) {
        auto& c = localCritical(b);
        const std::optional<Value> pending = c.report;
        const bool acknowledged = acknowledgeLocalDamageReport(b, base_revision, ack_seq, revision);
        if (acknowledged && c.owned && pending && canonical && canonical->resolved().kind == Value::Kind::Object) {
            std::set<std::string> pending_tracks;
            for (const auto& row : field(*pending, "tracks").resolved().array)
                if (row.resolved().kind == Value::Kind::Object) pending_tracks.insert(field(row, "name").resolved().text);
            std::map<std::string, std::string> states;
            for (const auto& row : field(*canonical, "devices").resolved().array)
                if (row.resolved().kind == Value::Kind::Object) states[field(row, "name").resolved().text] = field(row, "state").resolved().text;
            std::set<std::string> destroyed;
            for (const auto& name : field(*canonical, "destroyed").resolved().array) destroyed.insert(name.resolved().text);
            bool repaired = !pending_tracks.empty();
            for (const auto& name : pending_tracks) {
                const auto found = states.find(name);
                repaired = repaired && found != states.end() && (found->second == "normal" || found->second == "critical") && !destroyed.count(name);
            }
            if (repaired) c.owned = false;
        }
        if (c.owned && base_revision == c.base_revision && canonical && canonical->resolved().kind == Value::Kind::Object) {
            // The owning client advances only tracks; server-owned gun, engine, fire and crew stay canonical.
            const std::set<std::string> names{"leftTrackHealth", "rightTrackHealth"};
            const Value local_state = d.critical.resolved().kind == Value::Kind::Object ? d.critical : Value();
            Value devices; devices.kind = Value::Kind::Array;
            for (const auto& row : field(*canonical, "devices").resolved().array)
                if (!names.count(field(row, "name").resolved().text)) devices.array.push_back(row.resolved());
            for (const auto& row : field(local_state, "devices").resolved().array)
                if (names.count(field(row, "name").resolved().text)) devices.array.push_back(row.resolved());
            std::set<std::string> kept;
            for (const auto& name : field(*canonical, "destroyed").resolved().array) if (!names.count(name.resolved().text)) kept.insert(name.resolved().text);
            for (const auto& name : field(local_state, "destroyed").resolved().array) if (names.count(name.resolved().text)) kept.insert(name.resolved().text);
            Value sorted; sorted.kind = Value::Kind::Array;
            for (const auto& name : kept) { Value t; t.kind = Value::Kind::Text; t.text = name; sorted.array.push_back(std::move(t)); }
            auto& out = writable(*canonical);
            out.object["devices"] = std::move(devices);
            out.object["destroyed"] = std::move(sorted);
        }
    }
    return revision > previous;
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
    if (!b.worker) presentCritical(b, v, events.get(), 0);
    return true;
}

// BattleRuntime._present_critical: critical transitions through the stock #1513 UI callbacks.
bool presentCritical(Battle& b, Actor& v, PyObject* events, std::int64_t attacker) {
    if (b.worker || (!v.local && !v.presented.spot_visible)) return false;
    auto entity = b.entity(v.engine_id);
    if (entity.get() == Py_None || py::attr(entity.get(), "typeDescriptor").get() == Py_None) return false;
    syncFireEffect(b, entity.get());
    if (!p::flag(events)) return false;
    bool shown = false;
    p::Ref rows(PySequence_List(events));
    const Py_ssize_t count = PyList_GET_SIZE(rows.get());
    auto get = [](PyObject* row, const char* name) { return text(PyDict_GetItemString(row, name)); };
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PyList_GET_ITEM(rows.get(), i);
        if (get(row, "kind") != "ammo_rack" || get(row, "state") != "destroyed") continue;
        auto callback = py::optional(entity.get(), "showAmmoBayEffect");
        if (!py::callable(callback)) throw p::Error("#1513 ammo-bay effect boundary is unavailable");
        auto modes = py::optional(b.engine.constants.get(), "AMMOBAY_DESTRUCTION_MODE");
        if (modes.get() == Py_None || !PyObject_HasAttrString(modes.get(), "HE_DETONATION")) throw p::Error("#1513 ammo-bay destruction mode is unavailable");
        py::invoke(callback.get(), py::attr(modes.get(), "HE_DETONATION").get(), p::number(0.).get(), p::number(0.).get());
        shown = true;
    }
    if (!v.local || !b.engine.avatar || !damageInfoServiceable(b)) return shown;
    auto indices = py::optional(b.engine.constants.get(), "DAMAGE_INFO_INDICES");
    auto suffix = [](const std::string& cause) -> std::string {
        if (cause == "fire") return "_AT_FIRE";
        if (cause == "ramming") return "_AT_RAMMING";
        if (cause == "world_collision") return "_AT_WORLD_COLLISION";
        if (cause == "drowning") return "_AT_DROWNING";
        return "_AT_SHOT";
    };
    auto descriptor = py::attr(entity.get(), "typeDescriptor");
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject* row = PyList_GET_ITEM(rows.get(), i);
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
            const bool burning = p::flag(PyDict_GetItemString(row, "state"));
            code = burning ? (cause == "ramming" ? "DEVICE_STARTED_FIRE_AT_RAMMING" : "DEVICE_STARTED_FIRE_AT_SHOT") : "FIRE_STOPPED";
        } else continue;
        PyObject* index = indices.get() != Py_None ? PyObject_GetItem(indices.get(), p::text(code).get()) : nullptr;
        if (!index) { PyErr_Clear(); throw p::Error("#1513 damage-info index is unavailable: " + code); }
        p::Ref held(index);
        if (showDamageInfo(b, v.engine_id, p::wide(index), extra, attacker)) shown = true;
    }
    return shown;
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
