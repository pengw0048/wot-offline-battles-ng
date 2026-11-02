// The #1513 module and crew damage model: canonical payloads, death, crashed tracks and stat factors.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_critical.hpp"
#include "stock_native_bot_descriptor.hpp"
#include <algorithm>
#include <cmath>

namespace native_battle {
namespace critical {
namespace {
namespace descriptors = stock_native_bot_descriptor;

const char* const death_devices[] = {"engineHealth", "ammoBayHealth", "fuelTankHealth", "radioHealth", "gunHealth",
                                     "turretRotatorHealth", "surveyingDeviceHealth", "leftTrackHealth", "rightTrackHealth"};
constexpr double critical_fraction = .5, min_vision_factor = .5;
constexpr double damaged_module = .5, destroyed_module = .25;

// #1513's role factor for the surviving share of a role's slots; a live commander adds level/10.
double roleFactor(double share, bool commander_alive) {
    const double per_slot = commander_alive ? 110. : 100.;
    return .57 + .43 * (per_slot * std::max(0., std::min(1., share)) / 100.);
}
const double crew_fit = roleFactor(1., true), crew_role_out = roleFactor(0., true), crew_commander_out = roleFactor(1., false);
const double ko_time = crew_fit / crew_role_out, ko_speed = crew_role_out / crew_fit;
const double commander_time = crew_fit / crew_commander_out, commander_speed = crew_commander_out / crew_fit;

Value text(const std::string& s) { Value v; v.kind = Value::Kind::Text; v.text = s; return v; }
Value number(double n) { Value v; v.kind = Value::Kind::Number; v.number = n; return v; }
Value boolean(bool n) { Value v; v.kind = Value::Kind::Boolean; v.boolean = n; return v; }
Value object() { Value v; v.kind = Value::Kind::Object; return v; }
Value array() { Value v; v.kind = Value::Kind::Array; return v; }
Value names(const std::set<std::string>& values) {
    auto out = array();
    for (const auto& n : values) out.array.push_back(text(n));
    return out;
}
std::string roleBase(const std::string& instance) {
    auto end = instance.find_last_not_of("0123456789");
    return end == std::string::npos ? std::string() : instance.substr(0, end + 1);
}

p::Ref descriptorOf(PyObject* entity) { return py::optional(entity, "typeDescriptor"); }
std::int64_t idOf(PyObject* entity) { return p::wide(py::attr(entity, "id").get()); }

std::optional<double> maxHp(PyObject* descriptor, const std::string& name) {
    if (!descriptor || descriptor == Py_None) return {};
    return descriptors::devicePool(descriptor, name).maximum;
}
std::optional<double> regenHp(PyObject* descriptor, const std::string& name) {
    if (!descriptor || descriptor == Py_None) return {};
    const auto pool = descriptors::devicePool(descriptor, name);
    return pool.maximum ? pool.regeneration : std::nullopt;
}
std::string deviceState(double hp, std::optional<double> maximum) {
    if (hp <= 0.) return "destroyed";
    if (maximum && *maximum != 0. && hp <= *maximum * critical_fraction) return "critical";
    return "normal";
}

// The crew instance names ("commander", "gunner1", ...) and each instance's roles.
std::vector<std::string> roster(PyObject* descriptor) {
    auto names = descriptor && descriptor != Py_None ? descriptors::crewRoster(descriptor) : std::vector<std::string>{};
    if (names.empty()) names = {"commander", "driver", "gunner1", "loader1", "radioman1"};
    return names;
}
std::vector<std::vector<std::string>> crewRoles(PyObject* descriptor) {
    std::vector<std::vector<std::string>> out;
    auto type = py::optional(descriptor, "type");
    auto roles = py::optional(type.get(), "crewRoles");
    if (!PyList_Check(roles.get()) && !PyTuple_Check(roles.get())) return out;
    for (Py_ssize_t i = 0; i < PySequence_Size(roles.get()); ++i) {
        auto row = p::item(roles.get(), i);
        std::vector<std::string> members;
        if (PyList_Check(row.get()) || PyTuple_Check(row.get()))
            for (Py_ssize_t j = 0; j < PySequence_Size(row.get()); ++j) members.push_back(p::string(p::item(row.get(), j).get()));
        out.push_back(std::move(members));
    }
    return out;
}
// Impaired base roles: a small crew's man covers several roles, so one casualty can impair more than one.
void recomputeImpaired(VehicleDamage& d, PyObject* descriptor) {
    d.crew_impaired.clear();
    if (d.crew_ko.empty()) return;
    const auto names = roster(descriptor);
    const auto roles = crewRoles(descriptor);
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (!d.crew_ko.count(names[i])) continue;
        if (i < roles.size()) d.crew_impaired.insert(roles[i].begin(), roles[i].end());
        else d.crew_impaired.insert(roleBase(names[i]));
    }
}

// The pinned _state(vehicle): accepted damage with HP-derived yellow devices.
struct Snapshot {
    std::map<std::string, double> devices;
    std::set<std::string> destroyed, critical, crew_ko;
    bool fire = false, ammo_rack_death = false;
};
Snapshot snapshot(const VehicleDamage& d, PyObject* descriptor) {
    Snapshot s{d.devices, d.destroyed, d.critical, d.crew_ko, d.fire, d.ammo_rack_death};
    for (const auto& device : s.devices)
        if (!s.destroyed.count(device.first) && deviceState(device.second, maxHp(descriptor, device.first)) == "critical")
            s.critical.insert(device.first);
    for (const auto& name : s.destroyed) s.critical.erase(name);
    return s;
}

// The pinned _payload(before, after, descriptor, cause): device records plus edge events, Null when unchanged.
Value payloadOf(const Snapshot& before, const Snapshot& after, PyObject* descriptor, const char* cause, bool force = false) {
    std::set<std::string> all;
    for (const auto* s : {&before, &after}) {
        for (const auto& d : s->devices) all.insert(d.first);
        all.insert(s->critical.begin(), s->critical.end());
        all.insert(s->destroyed.begin(), s->destroyed.end());
    }
    auto devices = array(), events = array();
    auto event = [&](const char* kind, const std::string* name, Value state, const std::string* old_state) {
        auto row = object();
        row.object["kind"] = text(kind);
        if (name) row.object["name"] = text(*name);
        if (old_state) row.object["old_state"] = text(*old_state);
        row.object["state"] = std::move(state);
        if (cause) row.object["cause"] = text(cause);
        events.array.push_back(std::move(row));
    };
    for (const auto& name : all) {
        auto at = after.devices.find(name);
        double hp = 0.;
        if (at != after.devices.end()) hp = at->second;
        else if (before.devices.count(name)) hp = before.devices.at(name);
        auto maximum = maxHp(descriptor, name);
        const double max_hp = maximum ? *maximum : std::max(1., double(std::round(hp)));
        const std::string state = after.destroyed.count(name) ? "destroyed" : after.critical.count(name) ? "critical" : deviceState(hp, max_hp);
        auto record = object();
        record.object["name"] = text(name);
        record.object["hp"] = number(std::max(0., hp));
        record.object["max_hp"] = number(std::max(1., max_hp));
        record.object["state"] = text(state);
        devices.array.push_back(std::move(record));
        std::string old_state;
        auto old = before.devices.find(name);
        if (old == before.devices.end()) old_state = "normal";
        else if (before.destroyed.count(name)) old_state = "destroyed";
        else if (before.critical.count(name)) old_state = "critical";
        else old_state = deviceState(old->second, maximum ? maximum : std::optional<double>(std::max(1., max_hp)));
        if (old_state != state) event("device", &name, text(state), &old_state);
    }
    for (const auto& name : after.crew_ko)
        if (!before.crew_ko.count(name)) event("crew", &name, text("destroyed"), nullptr);
    for (const auto& name : before.crew_ko)
        if (!after.crew_ko.count(name)) event("crew", &name, text("normal"), nullptr);
    if (before.fire != after.fire) event("fire", nullptr, boolean(after.fire), nullptr);
    if (!before.ammo_rack_death && after.ammo_rack_death) event("ammo_rack", nullptr, text("destroyed"), nullptr);
    const bool changed = !events.array.empty() || before.devices != after.devices || before.destroyed != after.destroyed ||
                         before.critical != after.critical || before.crew_ko != after.crew_ko || before.ammo_rack_death != after.ammo_rack_death;
    if (!changed && !force) return Value();
    auto out = object();
    out.object["devices"] = std::move(devices);
    out.object["destroyed"] = names(after.destroyed);
    out.object["crew_ko"] = names(after.crew_ko);
    out.object["fire"] = boolean(after.fire);
    out.object["ammo_rack_death"] = boolean(after.ammo_rack_death);
    out.object["events"] = std::move(events);
    return out;
}

// Mirrors a destroyed-track edge into the stock CompoundAppearance crashed-track visuals.
void syncCrashedTrack(PyObject* entity, const std::set<std::string>& before, const std::set<std::string>& after) {
    auto appearance = py::optional(entity, "appearance");
    if (appearance.get() == Py_None) return;
    for (const auto& track : {std::make_pair("leftTrackHealth", true), std::make_pair("rightTrackHealth", false)}) {
        const bool was = before.count(track.first) != 0, is = after.count(track.first) != 0;
        if (was == is) continue;
        auto callback = py::optional(appearance.get(), is ? "addCrashedTrack" : "delCrashedTrack");
        if (!py::callable(callback)) continue;
        // Optional presentation: a streamed or retiring appearance may decline it.
        PyObject* done = PyObject_CallFunctionObjArgs(callback.get(), track.second ? Py_True : Py_False, nullptr);
        if (done) Py_DECREF(done);
        else PyErr_Clear();
    }
}

// The fire ending restores a destroyed fuel tank to its regeneration cap.
void extinguish(VehicleDamage& d, PyObject* descriptor) {
    if (!d.fire) return;
    d.fire = false;
    const auto cap = regenHp(descriptor, "fuelTankHealth");
    if (!cap) return;
    auto at = d.devices.find("fuelTankHealth");
    if (at == d.devices.end() || at->second >= *cap) return;
    at->second = *cap;
    d.destroyed.erase("fuelTankHealth");
    d.critical.insert("fuelTankHealth");
}

double crewStatFactor(const std::set<std::string>& roles, const std::string& stat) {
    if (roles.empty()) return 1.;
    const bool commander = roles.count("commander") != 0;
    auto has = [&](const char* role) { return roles.count(role) != 0; };
    double f = 1.;
    if (stat == "reload") { if (has("loader")) f *= ko_time; if (commander) f *= commander_time; }
    else if (stat == "dispersion" || stat == "aim_time") { if (has("gunner")) f *= ko_time; if (commander) f *= commander_time; }
    else if (stat == "turret_speed") { if (has("gunner")) f *= ko_speed; if (commander) f *= commander_speed; }
    else if (stat == "mobility") { if (has("driver")) f *= ko_speed; if (commander) f *= commander_speed; }
    else if (stat == "vision") { if (commander) f *= ko_speed; if (has("radioman")) f *= ko_speed; }
    else if (stat == "signal") { if (has("radioman")) f *= ko_speed; if (commander) f *= commander_speed; }
    return f;
}

double moduleStatFactor(const VehicleDamage& d, PyObject* descriptor, const std::string& stat) {
    struct Spec { const char* device; double damaged; std::optional<double> dead; };
    static const std::map<std::string, Spec> specs = {
        {"reload", {"ammoBayHealth", 1. / damaged_module, {}}},       {"dispersion", {"gunHealth", 1. / damaged_module, {}}},
        {"aim_time", {"gunHealth", 1. / damaged_module, {}}},         {"turret_speed", {"turretRotatorHealth", damaged_module, 0.}},
        {"mobility", {"engineHealth", damaged_module, 0.}},           {"vision", {"surveyingDeviceHealth", damaged_module, destroyed_module}},
        {"signal", {"radioHealth", damaged_module, destroyed_module}}};
    const auto spec = specs.find(stat);
    if (spec == specs.end()) return 1.;
    const std::string name = spec->second.device;
    if (d.destroyed.count(name)) return spec->second.dead ? *spec->second.dead : 1.;
    const auto hp = d.devices.find(name);
    if (hp == d.devices.end()) return 1.;
    if (d.critical.count(name) || deviceState(hp->second, maxHp(descriptor, name)) == "critical") return spec->second.damaged;
    return 1.;
}

// The accepted crew's conditional effects for the current physical crew (crew_battle.py).
struct BattleFactors { double adrenaline_health = 0., adrenaline_reload = 1., engine_fire = 1., damaged_gun = 1.; };
BattleFactors forCritical(const Value& effective_params, const VehicleDamage* d) {
    BattleFactors out;
    const auto& dynamic = effective_params.resolved().get("crew").get("dynamic_spotting").resolved();
    std::int64_t mask = 0;
    std::int64_t index = 0;
    for (const auto& name : dynamic.get("crew").resolved().array) {
        if (d && d->crew_ko.count(name.resolved().text)) mask |= std::int64_t(1) << index;
        ++index;
    }
    const auto key = std::to_string(mask) + ":" + std::to_string(d && d->fire ? 1 : 0);
    const auto& factors = dynamic.get("states").get(key).get("battle_factors").resolved();
    if (factors.kind != Value::Kind::Object || factors.object.empty()) return out;
    out.adrenaline_health = factors.get("adrenaline_health_fraction").numeric(out.adrenaline_health);
    out.adrenaline_reload = factors.get("adrenaline_reload_factor").numeric(out.adrenaline_reload);
    out.engine_fire = factors.get("engine_fire_factor").numeric(out.engine_fire);
    out.damaged_gun = factors.get("damaged_gun_factor").numeric(out.damaged_gun);
    return out;
}
double numberOr(PyObject* value, double fallback) {
    if (!value || value == Py_None) return fallback;
    PyObject* f = PyNumber_Float(value);
    if (!f) { PyErr_Clear(); return fallback; }
    const double out = PyFloat_AS_DOUBLE(f);
    Py_DECREF(f);
    return std::isfinite(out) ? out : fallback;
}
}

const VehicleDamage* damage(const Battle& b, std::int64_t engine_id) {
    if (!b.critical) return nullptr;
    const auto at = b.critical->vehicles.find(engine_id);
    return at == b.critical->vehicles.end() ? nullptr : &at->second;
}

bool eventCurrent(const Battle& b, PyObject* entity, const Value& event) {
    const auto* d = damage(b, idOf(entity));
    if (!d) return false;
    const auto& kind = event.get("kind").resolved().text;
    const auto& name = event.get("name").resolved().text;
    const auto& state = event.get("state").resolved();
    if (kind == "fire") return state.truth() == d->fire;
    if (kind == "crew") return state.text == (d->crew_ko.count(name) ? "destroyed" : "normal");
    if (kind != "device") return false;
    std::string current = "normal";
    if (d->destroyed.count(name)) current = "destroyed";
    else if (d->critical.count(name)) current = "critical";
    else {
        const auto hp = d->devices.find(name);
        if (hp != d->devices.end()) current = deviceState(hp->second, maxHp(descriptorOf(entity).get(), name));
    }
    return state.text == current;
}

// critical_damage.apply_payload: install one server-relayed state without re-rolling any damage law.
Value applyPayload(Battle& b, PyObject* entity, const Value& raw) {
    const auto& payload = raw.resolved();
    if (payload.kind != Value::Kind::Object) return array();
    auto descriptor = descriptorOf(entity);
    auto& d = b.critical->vehicles[idOf(entity)];
    const auto before = snapshot(d, descriptor.get());
    const bool was_on_fire = d.fire;
    std::map<std::string, double> devices;
    std::set<std::string> critical;
    for (const auto& row : payload.get("devices").resolved().array) {
        const auto& record = row.resolved();
        if (record.kind != Value::Kind::Object) continue;
        const auto& name = record.get("name").resolved();
        if (!name.truth()) continue;
        devices[name.text] = std::max(0., record.get("hp").numeric(0.));
        if (record.get("state").resolved().text == "critical") critical.insert(name.text);
    }
    d.devices = std::move(devices);
    d.destroyed.clear();
    for (const auto& name : payload.get("destroyed").resolved().array) d.destroyed.insert(name.resolved().text);
    for (const auto& name : d.destroyed) critical.erase(name);
    d.critical = std::move(critical);
    d.crew_ko.clear();
    for (const auto& name : payload.get("crew_ko").resolved().array) d.crew_ko.insert(name.resolved().text);
    const bool on_fire = payload.get("fire").truth();
    if (was_on_fire && !on_fire) extinguish(d, descriptor.get());
    else d.fire = on_fire;
    d.ammo_rack_death = payload.get("ammo_rack_death").truth();
    recomputeImpaired(d, descriptor.get());
    const auto after = snapshot(d, descriptor.get());
    syncCrashedTrack(entity, before.destroyed, after.destroyed);
    const auto& events = payload.get("events").resolved();
    if (events.kind == Value::Kind::Array && !events.array.empty()) return events;
    auto derived = payloadOf(before, after, descriptor.get(), "network");
    if (derived.kind == Value::Kind::Null) return array();
    auto out = array();
    for (const auto& source : derived.get("events").resolved().array) {
        Value event = source;
        const auto& state = event.object["state"];
        const bool repaired = (state.kind == Value::Kind::Text && state.text == "normal") ||
                              (event.object["kind"].text == "device" && event.object["old_state"].text == "destroyed" &&
                               state.kind == Value::Kind::Text && state.text == "critical");
        event.object["cause"] = text(repaired ? "repair" : "shot");
        out.array.push_back(std::move(event));
    }
    return out;
}

// critical_damage.apply_death: every module at 0 HP and every crewman down for a terminal state.
Value applyDeath(Battle& b, PyObject* entity, const char* cause) {
    if (!entity || entity == Py_None) return Value();
    auto descriptor = descriptorOf(entity);
    auto& d = b.critical->vehicles[idOf(entity)];
    const auto before = snapshot(d, descriptor.get());
    if (d.fire) extinguish(d, descriptor.get());
    for (const char* name : death_devices) {
        d.devices[name] = 0.;
        d.destroyed.insert(name);
    }
    d.critical.clear();
    for (const auto& name : roster(descriptor.get())) d.crew_ko.insert(name);
    recomputeImpaired(d, descriptor.get());
    return payloadOf(before, snapshot(d, descriptor.get()), descriptor.get(), cause);
}

double moduleFactor(Battle& b, PyObject* entity, const std::string& stat) {
    if (!entity || entity == Py_None) return 1.;
    const auto* d = damage(b, idOf(entity));
    if (!d) return 1.;
    auto descriptor = descriptorOf(entity);
    return moduleStatFactor(*d, descriptor.get(), stat);
}

double statFactor(Battle& b, PyObject* entity, const std::string& stat) {
    if (!entity || entity == Py_None) return 1.;
    const auto* d = damage(b, idOf(entity));
    const double crew = d ? crewStatFactor(d->crew_impaired, stat) : 1.;
    const double modules = moduleFactor(b, entity, stat);
    double stun = 1.;
    auto factors = py::optional(entity, "_offlineStunFactors");
    if (PyDict_Check(factors.get())) stun = numberOr(PyDict_GetItemString(factors.get(), stat.c_str()), 1.);
    return crew * modules * stun;
}

double localStatFactor(Battle& b, PyObject* entity, const std::string& stat, const Value* effective_params) {
    const double value = statFactor(b, entity, stat);
    if ((stat != "reload" && stat != "dispersion") || !entity || entity == Py_None || !effective_params ||
        effective_params->resolved().kind == Value::Kind::Null)
        return value;
    const auto factors = forCritical(*effective_params, damage(b, idOf(entity)));
    auto descriptor = descriptorOf(entity);
    const double maximum = numberOr(py::optional(entity, "maxHealth").get(), numberOr(py::optional(descriptor.get(), "maxHealth").get(), 1.));
    const double health = numberOr(py::optional(entity, "health").get(), 0.);
    if (stat == "reload" && 0. < health && health < maximum * factors.adrenaline_health) return value * factors.adrenaline_reload;
    const double module = moduleFactor(b, entity, stat);
    // Armorer reduces the damaged gun's dispersion without improving a healthy gun.
    if (stat == "dispersion" && module > 1.) return value * std::max(1., module * factors.damaged_gun) / module;
    return value;
}

double clampVisionFactor(double factor) {
    if (std::isnan(factor)) return factor;
    return std::max(min_vision_factor, std::min(1., factor));
}
}
}
