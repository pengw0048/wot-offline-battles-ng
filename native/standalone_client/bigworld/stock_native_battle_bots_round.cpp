// The Bot authority's round start (the pinned BotRuntime.battle_start): round and authority fences,
// server orders, the authority manifest built from the server roster, and its pending/enqueued
// bookkeeping. The built round seeds the native owner's cold installation.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_bots_random.hpp"
#include "stock_native_battle_params.hpp"
#include "stock_native_battle_startup.hpp"
#include "stock_native_bot_combat_snapshot.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_motion_snapshot.hpp"
#include "stock_native_bot_owner.hpp"
#include "stock_native_bot_parking.hpp"
#include "stock_native_bot_planning_snapshot.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <set>
#include <tuple>

namespace native_battle {
namespace bots {
namespace {
using Kind = Value::Kind;
using stock_native_bot::BotSeed;
using stock_native_bot::RoundSeed;
using random27::Random;
using random27::stableSeed;
namespace step = wot::offline_bot_step;

const char* const CLASSES[] = {"lightTank", "mediumTank", "heavyTank", "AT-SPG", "SPG"};
const char* const SKILL_TIERS[] = {"rookie", "regular", "veteran", "elite"};
const double RATING_ANCHORS[] = {0., .34, .67, 1.};
constexpr double DEFAULT_RATING = .34;
constexpr std::int64_t DEFAULT_ROUTE_PRIORITY = 5;
// bot_gunnery._MODE_RATING_POINTS: one preset's piecewise-linear inverse CDF at u = 0, .25, .5, .75, 1.
const std::map<std::string, std::array<double, 5>> MODE_POINTS = {
    {"easy", {0., .03, .08, .14, .30}}, {"relaxed", {.05, .16, .28, .40, .67}},
    {"mixed", {.12, .42, .62, .80, 1.}}, {"hard", {.52, .70, .82, .92, 1.}}, {"brutal", {1., 1., 1., 1., 1.}}};
// siege_mechanics.VEHICLE_PARAMS: the vehicles with a #1513 Siege mode.
const std::set<std::string> SIEGE_VEHICLES = {"sweden:S10_Strv_103_0_Series", "sweden:S11_Strv_103B", "sweden:S21_UDES_03",
                                              "sweden:S22_Strv_S1"};
constexpr std::int64_t SIEGE_DISABLED = 0, SIEGE_SWITCHING_ON = 1, SIEGE_ENABLED = 2, SIEGE_SWITCHING_OFF = 3;
// critical_damage._OFFH_DEATH_DEVICES.
const char* const DEATH_DEVICES[] = {"engineHealth", "ammoBayHealth", "fuelTankHealth", "radioHealth", "gunHealth",
                                     "turretRotatorHealth", "surveyingDeviceHealth", "leftTrackHealth", "rightTrackHealth"};
constexpr double FIRE_DURATION_SECONDS = 9.0, FIRE_TICK_SECONDS = 1.0;
constexpr std::int64_t WORKER_AUTHORITY_ID = -1;   // lan_client.WORKER_AUTHORITY_ID
constexpr std::uint32_t SHORE_EDGE = 8;            // prebaked_navigation.SHORE_EDGE
constexpr double PARKING_RADIUS = 2., CLEARANCE_MARGIN = .5, RESERVATION_GAP = 6.;
constexpr double WORKER_CONTROL_SECONDS = .10;

// ---------------------------------------------------------------------------------------------- values
Value number(double v) { Value x; x.kind = Kind::Number; x.number = v; return x; }
Value integer(std::int64_t v) { Value x; x.kind = Kind::Integer; x.integer = v; return x; }
Value boolean(bool v) { Value x; x.kind = Kind::Boolean; x.boolean = v; return x; }
Value text(const std::string& v) { Value x; x.kind = Kind::Text; x.text = v; return x; }
Value object() { Value x; x.kind = Kind::Object; return x; }
Value array() { Value x; x.kind = Kind::Array; return x; }
const Value& at(const Value& v, const char* key) { return v.resolved().get(key); }
bool has(const Value& v, const char* key) { return v.resolved().kind == Kind::Object && v.resolved().object.count(key); }
bool isText(const Value& v) { return v.resolved().kind == Kind::Text; }
bool isNumber(const Value& v) { const auto k = v.resolved().kind; return k == Kind::Integer || k == Kind::Number; }
// Python float(value) with the caller's fallback where it raises (bot_runtime._number).
double pyNumber(const Value& v, double fallback = 0.) {
    const auto& r = v.resolved();
    if (r.kind == Kind::Integer) return double(r.integer);
    if (r.kind == Kind::Number) return r.number;
    if (r.kind == Kind::Boolean) return r.boolean ? 1. : 0.;
    if (r.kind == Kind::Text) {
        try { std::size_t end = 0; const double n = std::stod(r.text, &end); if (end == r.text.size()) return n; } catch (...) {}
    }
    return fallback;
}
// Python int(value); nullopt where it raises.
std::optional<std::int64_t> pyInt(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind == Kind::Integer) return r.integer;
    if (r.kind == Kind::Boolean) return std::int64_t(r.boolean);
    if (r.kind == Kind::Number) {
        if (!std::isfinite(r.number) || std::abs(r.number) >= 9.2e18) return {};
        return std::int64_t(std::trunc(r.number));
    }
    if (r.kind == Kind::Text) {
        try { std::size_t end = 0; const long long n = std::stoll(r.text, &end); if (end == r.text.size()) return std::int64_t(n); } catch (...) {}
    }
    return {};
}
std::int64_t needInt(const Value& v, const char* what) {
    auto n = pyInt(v);
    if (!n) throw p::Error(std::string("authority manifest ") + what + " is invalid");
    return *n;
}
// Python 2 str(value) of a decoded protocol value; JSON text decodes as unicode, so str() of a
// non-ASCII text raises and planner.stable_seed records '?'.
std::string pyStr(const Value& v) {
    const auto& r = v.resolved();
    switch (r.kind) {
    case Kind::Null: return "None";
    case Kind::Boolean: return r.boolean ? "True" : "False";
    case Kind::Integer: return std::to_string(r.integer);
    case Kind::Text:
        for (unsigned char c : r.text) if (c >= 0x80) return "?";
        return r.text;
    case Kind::Number: {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.12g", r.number);
        std::string s(buffer);
        if (s.find_first_of(".eni") == std::string::npos) s += ".0";
        return s;
    }
    default: return "?";
    }
}
double clamp(double v, double low, double high) { return std::max(low, std::min(high, v)); }
// round(x, 6) of the pinned interpreter: half away from zero at the scaled value.
double round6(double v) { return std::round(v * 1.e6) / 1.e6; }
double round3(double v) { return std::round(v * 1.e3) / 1.e3; }

// ---------------------------------------------------------------------------------------------- Python handles
// _value / _attribute_or_key: a mapping's key, else an attribute, else None.
p::Ref attribute(PyObject* o, const char* name) {
    if (!o || o == Py_None) return p::retain(Py_None);
    return p::value(o, name);
}
// planner._number: float(value), else the default; inf and nan also take the default.
double plannerNumber(PyObject* v, double fallback) {
    if (!v || v == Py_None) return fallback;
    PyObject* f = PyNumber_Float(v);
    if (!f) { if (!PyErr_ExceptionMatches(PyExc_Exception)) throw p::Error("planner number"); PyErr_Clear(); return fallback; }
    const double out = PyFloat_AS_DOUBLE(f);
    Py_DECREF(f);
    return std::isfinite(out) ? out : fallback;
}
std::string typeName(PyObject* descriptor) {
    auto type = attribute(descriptor, "type");
    auto name = attribute(type.get(), "name");
    return name.get() == Py_None ? std::string() : p::string(p::Ref(PyObject_Str(name.get())).get());
}
std::set<std::string> descriptorTags(PyObject* descriptor) {
    std::set<std::string> out;
    auto type = attribute(descriptor, "type");
    auto tags = attribute(type.get(), "tags");
    if (tags.get() == Py_None) return out;
    p::Ref iterator(PyObject_GetIter(tags.get()));
    while (PyObject* item = PyIter_Next(iterator.get())) {
        p::Ref owned(item);
        out.insert(p::string(p::Ref(PyObject_Str(item)).get()));
    }
    if (PyErr_Occurred()) throw p::Error("descriptor tags");
    return out;
}

// ---------------------------------------------------------------------------------------------- bot_gunnery
std::string normalizeSkillMode(const Value& v) {
    return isText(v) && MODE_POINTS.count(v.resolved().text) ? v.resolved().text : std::string("mixed");
}
bool skillTier(const Value& v) {
    if (!isText(v)) return false;
    for (const char* tier : SKILL_TIERS) if (v.resolved().text == tier) return true;
    return false;
}
double ratingForSkill(const std::string& skill) {
    for (int i = 0; i < 4; ++i) if (skill == SKILL_TIERS[i]) return RATING_ANCHORS[i];
    return DEFAULT_RATING;
}
double normalizeRating(const Value& v) {
    const auto& r = v.resolved();
    double rating = 0.;
    if (r.kind == Kind::Integer || r.kind == Kind::Number || r.kind == Kind::Boolean) rating = pyNumber(r);
    else if (r.kind == Kind::Text) {
        try { std::size_t end = 0; rating = std::stod(r.text, &end); if (end != r.text.size()) return DEFAULT_RATING; } catch (...) { return DEFAULT_RATING; }
    } else return DEFAULT_RATING;
    if (!std::isfinite(rating)) return DEFAULT_RATING;
    return clamp(rating, 0., 1.);
}
std::string skillForRating(double rating) {
    int best = 0;
    double best_distance = -1.;
    for (int i = 0; i < 4; ++i) {
        const double distance = std::abs(rating - RATING_ANCHORS[i]);
        if (best_distance < 0. || distance < best_distance - 1e-12) { best = i; best_distance = distance; }
    }
    return SKILL_TIERS[best];
}
double interpolate(const std::array<double, 5>& points, double position) {
    const int span = 4;
    const double scaled = clamp(position, 0., 1.) * span;
    const int index = int(scaled);
    if (index >= span) return points[4];
    return points[index] + (points[index + 1] - points[index]) * (scaled - index);
}
// bot_gunnery.resolve_rating: one roster slot's preset draw, from round and slot identity alone.
double resolveRating(const std::string& mode, const Value& round_id, std::int64_t team, std::int64_t slot) {
    Random rng(stableSeed("bot-skill-rating-v1|" + pyStr(round_id) + "|" + mode + "|" + std::to_string(team) + "|" + std::to_string(slot)));
    return interpolate(MODE_POINTS.at(mode), rng.random());
}

// ---------------------------------------------------------------------------------------------- planner
const char* const ROLE_NAMES[] = {"brawler", "support", "flanker", "sniper", "scout", "artillery"};
const std::map<std::string, std::array<double, 6>> ROLE_DEFAULTS = {
    {"heavyTank", {.92, .55, .18, .20, .05, 0.}}, {"mediumTank", {.46, .76, .86, .48, .42, 0.}},
    {"lightTank", {.10, .45, .72, .28, .96, 0.}}, {"AT-SPG", {.32, .78, .18, .92, .08, 0.}},
    {"SPG", {0., .10, 0., .16, 0., 1.}}};
const std::map<std::string, std::pair<double, double>> DESIRED_RANGES = {
    {"heavyTank", {72., 260.}}, {"mediumTank", {135., 340.}}, {"lightTank", {175., 320.}}, {"AT-SPG", {255., 450.}}, {"SPG", {650., 1250.}}};
double primaryArmor(PyObject* component) {
    auto armor = attribute(component, "primaryArmor");
    if (PyTuple_Check(armor.get()) || PyList_Check(armor.get())) {
        double best = 0.;
        bool any = false;
        for (Py_ssize_t i = 0; i < PySequence_Size(armor.get()); ++i) {
            const double v = plannerNumber(p::item(armor.get(), i).get(), 0.);
            best = any ? std::max(best, v) : v;
            any = true;
        }
        return any ? best : 0.;
    }
    return plannerNumber(armor.get() == Py_None ? nullptr : armor.get(), 0.);
}
double middleValue(PyObject* value) {
    if (value && (PyTuple_Check(value) || PyList_Check(value))) {
        const Py_ssize_t n = PySequence_Size(value);
        if (n > 0) {
            double sum = 0.;
            for (Py_ssize_t i = 0; i < n; ++i) sum += plannerNumber(p::item(value, i).get(), 0.);
            return sum / double(n);
        }
    }
    return plannerNumber(value, 0.);
}
// planner.build_vehicle_profile: tactical roles from class tags plus vehicle stats.
Value vehicleProfile(PyObject* descriptor) {
    const auto tags = descriptorTags(descriptor);
    std::string class_tag = "mediumTank";
    for (const char* candidate : {"heavyTank", "mediumTank", "lightTank", "AT-SPG", "SPG"})
        if (tags.count(candidate)) { class_tag = candidate; break; }
    auto roles = ROLE_DEFAULTS.at(class_tag);
    double speed = 0.;
    {
        auto physics = attribute(descriptor, "physics");
        if (physics.get() != Py_None && !PyDict_Check(physics.get())) throw p::Error("Expected a plain mapping");
        PyObject* limits = physics.get() == Py_None ? nullptr : PyDict_GetItemString(physics.get(), "speedLimits");
        if (limits) {
            PyObject* first = PySequence_GetItem(limits, 0);
            if (first) {
                p::Ref owned(first);
                PyObject* f = PyNumber_Float(first);
                if (f) { speed = std::abs(PyFloat_AS_DOUBLE(f)); Py_DECREF(f); } else PyErr_Clear();
            } else PyErr_Clear();
        }
    }
    auto hull = attribute(descriptor, "hull"), turret = attribute(descriptor, "turret");
    const double armor = std::max(primaryArmor(hull.get()), primaryArmor(turret.get()));
    if (speed >= 15.) { roles[2] = std::min(1., roles[2] + .12); roles[4] = std::min(1., roles[4] + .08); }
    else if (speed != 0. && speed < 9.) roles[2] = std::max(0., roles[2] - .16);
    if (armor >= 120. && class_tag != "AT-SPG") { roles[0] = std::min(1., roles[0] + .18); roles[3] = std::max(0., roles[3] - .08); }
    // The six role values never tie, so the strict maximum is order independent.
    int dominant = 0;
    for (int i = 1; i < 6; ++i) if (roles[i] > roles[dominant]) dominant = i;
    const auto ranges = DESIRED_RANGES.at(class_tag);
    const std::string vehicle_name = typeName(descriptor);
    Value profile = object();
    profile.object["class_tag"] = text(class_tag);
    profile.object["vehicle_name"] = text(vehicle_name.empty() ? class_tag : vehicle_name);
    Value role_values = object();
    for (int i = 0; i < 6; ++i) role_values.object[ROLE_NAMES[i]] = number(roles[i]);
    profile.object["roles"] = std::move(role_values);
    profile.object["dominant_role"] = text(ROLE_NAMES[dominant]);
    profile.object["desired_range"] = number(ranges.first);
    profile.object["fire_range"] = number(ranges.second);
    profile.object["speed"] = number(speed);
    profile.object["armor"] = number(armor);
    Value shells = array();
    auto gun = attribute(descriptor, "gun");
    auto shots = attribute(gun.get(), "shots");
    if (shots.get() != Py_None) {
        PyObject* iterator = PyObject_GetIter(shots.get());
        if (!iterator) PyErr_Clear();
        else {
            p::Ref owned(iterator);
            std::int64_t index = 0;
            while (PyObject* raw = PyIter_Next(iterator)) {
                p::Ref shot(raw);
                auto shell = attribute(shot.get(), "shell");
                auto kind = attribute(shell.get(), "kind");
                Value row = object();
                row.object["index"] = integer(index++);
                row.object["kind"] = text(p::flag(kind.get()) ? p::string(p::Ref(PyObject_Str(kind.get())).get()) : std::string());
                auto piercing = attribute(shell.get(), "piercingPower"), damage = attribute(shell.get(), "damage");
                row.object["penetration"] = number(middleValue(piercing.get() == Py_None ? nullptr : piercing.get()));
                row.object["damage"] = number(middleValue(damage.get() == Py_None ? nullptr : damage.get()));
                auto shot_speed = attribute(shot.get(), "speed");
                row.object["speed"] = number(plannerNumber(shot_speed.get() == Py_None ? nullptr : shot_speed.get(), 0.));
                shells.array.push_back(std::move(row));
            }
            if (PyErr_Occurred()) PyErr_Clear();
        }
    }
    profile.object["shells"] = std::move(shells);
    return profile;
}
// planner.make_personality: coherent per-battle preferences.
Value personality(std::uint32_t seed) {
    Random rng(seed);
    std::map<std::string, double> traits;
    for (const char* name : {"aggression", "caution", "teamwork", "patience", "initiative", "adaptability", "jiggle"})
        traits[name] = .18 + rng.random() * .64;
    traits["aggression"] = std::max(.05, std::min(.95, traits["aggression"] + (.5 - traits["caution"]) * .22));
    traits["caution"] = std::max(.05, std::min(.95, traits["caution"] + (.5 - traits["aggression"]) * .12));
    traits["route_jitter"] = rng.uniform(-4., 4.);
    traits["hold_jitter"] = rng.uniform(-1.5, 1.5);
    Value out = object();
    for (const auto& trait : traits) out.object[trait.first] = number(trait.second);
    return out;
}
// tactical_maps.normalize_map_name.
std::string normalizeMapName(const Value& raw) {
    std::string name = raw.resolved().kind == Kind::Null ? std::string() : (isText(raw) ? raw.resolved().text : pyStr(raw));
    std::replace(name.begin(), name.end(), '\\', '/');
    const auto slash = name.rfind('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".xml") == 0) name.resize(name.size() - 4);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return name;
}

// ---------------------------------------------------------------------------------------------- siege_mechanics
p::Ref fieldOr(PyObject* value, const char* name, PyObject* fallback) {
    if (PyDict_Check(value)) { PyObject* found = PyDict_GetItemString(value, name); return p::retain(found ? found : fallback); }
    PyObject* found = PyObject_GetAttrString(value, name);
    if (found) return p::Ref(found);
    if (!PyErr_ExceptionMatches(PyExc_AttributeError)) throw p::Error(name);
    PyErr_Clear();
    return p::retain(fallback);
}
// siege_mechanics.descriptor_pair: the immutable (travel, siege) pair of one resolved descriptor.
std::pair<Object, Object> descriptorPair(PyObject* descriptor) {
    auto travel = fieldOr(descriptor, "defaultVehicleDescr", descriptor);
    auto siege = fieldOr(descriptor, "siegeVehicleDescr", Py_None);
    if (siege.get() == Py_None) replace(siege, fieldOr(descriptor, "siege_descriptor", Py_None));
    bool mode = p::flag(fieldOr(descriptor, "hasSiegeMode", Py_False).get());
    if (!mode) mode = SIEGE_VEHICLES.count(typeName(travel.get())) > 0;
    if (!mode) return {own(std::move(travel)), Object()};
    if (siege.get() == Py_None) throw p::Error("#1513 Siege vehicle has no mode-specific descriptor");
    return {own(std::move(travel)), own(std::move(siege))};
}
// siege_mechanics.valid_wire_state for one manifest row.
bool validSiegeWire(const Value& state, const Value& time_left, bool siege_vehicle, const Value& total) {
    auto exact = [](const Value& v) -> std::optional<std::int64_t> {
        const auto& r = v.resolved();
        if (r.kind == Kind::Integer) return r.integer;
        if (r.kind == Kind::Number && std::isfinite(r.number) && std::floor(r.number) == r.number) return std::int64_t(r.number);
        return {};
    };
    const auto s = exact(state), t = exact(time_left), w = exact(total);
    if (!s || !t || *s < SIEGE_DISABLED || *s > SIEGE_SWITCHING_OFF || *t < 0 || *t > 4000) return false;
    const bool switching = *s == SIEGE_SWITCHING_ON || *s == SIEGE_SWITCHING_OFF;
    if (switching != (*t > 0)) return false;
    if (!w || *w < 0 || *w > 4000 || switching != (*w > 0) || *t > *w) return false;
    if (!siege_vehicle) return *s == SIEGE_DISABLED && *t == 0 && *w == 0;
    return true;
}

// ---------------------------------------------------------------------------------------------- critical
// _terminal_critical(state, descriptor, 'shot') of an undamaged Bot: every death device at 0 HP and
// the descriptor crew knocked out, in _canonical_critical's durable shape.
std::optional<Value> terminalCritical(PyObject* descriptor) {
    const auto roster = stock_native_bot_descriptor::crewRoster(descriptor);
    if (roster.empty()) return {};
    std::vector<std::string> names(std::begin(DEATH_DEVICES), std::end(DEATH_DEVICES));
    std::sort(names.begin(), names.end());
    Value devices = array(), destroyed = array();
    for (const auto& name : names) {
        const auto pool = stock_native_bot_descriptor::devicePool(descriptor, name);
        const double maximum = std::max(1., round3(std::max(1., pool.maximum.value_or(1.))));
        Value row = object();
        row.object["name"] = text(name);
        row.object["hp"] = number(0.);
        row.object["max_hp"] = number(maximum);
        row.object["state"] = text("destroyed");
        devices.array.push_back(std::move(row));
        destroyed.array.push_back(text(name));
    }
    auto crew_ko = roster;
    std::sort(crew_ko.begin(), crew_ko.end());
    Value ko = array(), crew = array();
    for (const auto& name : crew_ko) ko.array.push_back(text(name));
    for (const auto& name : roster) crew.array.push_back(text(name));
    Value out = object();
    out.object["devices"] = std::move(devices);
    out.object["destroyed"] = std::move(destroyed);
    out.object["crew_ko"] = std::move(ko);
    out.object["fire"] = boolean(false);
    out.object["ammo_rack_death"] = boolean(false);
    out.object["events"] = array();
    out.object["crew_roster"] = std::move(crew);
    return out;
}

// ---------------------------------------------------------------------------------------------- navigation graph
const std::vector<Value>* teamRows(const Value& routes, int team) {
    const auto& r = routes.resolved();
    if (r.kind != Kind::Object) return nullptr;
    auto found = r.object.find(std::to_string(team));
    if (found == r.object.end() || found->second.resolved().kind != Kind::Array) return nullptr;
    return &found->second.resolved().array;
}
// BotRuntime._validated_baked_routes.
void validateRoutes(const Value& routes) {
    if (routes.resolved().kind != Kind::Object) throw p::Error("navigation graph routes are missing");
    for (int team : {1, 2}) {
        const auto* rows = teamRows(routes, team);
        if (!rows || rows->empty()) throw p::Error("navigation graph routes are missing for team " + std::to_string(team));
        for (const auto& route : *rows) {
            if (route.resolved().kind != Kind::Object) throw p::Error("navigation graph route is invalid");
            const auto& points = at(route, "waypoints").resolved();
            if (points.kind != Kind::Array || points.array.empty() || points.array.size() > 16)
                throw p::Error("navigation graph route waypoint count is invalid");
            for (const auto& point : points.array) {
                const auto& row = point.resolved();
                if (row.kind != Kind::Array || row.array.size() < 2 || !isNumber(row.array[0]) || !isNumber(row.array[1]) ||
                    !std::isfinite(row.array[0].numeric(NAN)) || !std::isfinite(row.array[1].numeric(NAN)))
                    throw p::Error("navigation graph route waypoint is invalid");
            }
        }
    }
}
// prebaked_navigation.with_shore_hazards: wet edges within the bake's clearance radius are a motion veto.
std::vector<unsigned> shoreHazards(PyObject* graph) {
    auto raw = p::value(graph, "hazards");
    const int width = int(p::integral(graph, "width")), height = int(p::integral(graph, "height"));
    const Py_ssize_t count = PySequence_Size(raw.get());
    if (count < 0 || count != Py_ssize_t(width) * height) throw p::Error("navigation graph hazards are invalid");
    std::vector<unsigned> hazards(static_cast<std::size_t>(count));
    for (Py_ssize_t i = 0; i < count; ++i) hazards[std::size_t(i)] = unsigned(p::wide(p::item(raw.get(), i).get()));
    auto bake = p::value(graph, "bake");
    auto radii = PyDict_Check(bake.get()) ? p::value(bake.get(), "edge_clearance_radii") : p::retain(Py_None);
    std::optional<int> radius;
    if (p::flag(radii.get())) {
        double largest = -std::numeric_limits<double>::infinity();
        for (Py_ssize_t i = 0; i < PySequence_Size(radii.get()); ++i) largest = std::max(largest, p::scalar(p::item(radii.get(), i).get()));
        radius = int(std::ceil(largest / p::numeric(graph, "cell_size")));
    }
    std::vector<unsigned> classified(hazards.size());
    for (std::size_t i = 0; i < hazards.size(); ++i) classified[i] = hazards[i] & ~SHORE_EDGE;
    for (std::size_t index = 0; index < hazards.size(); ++index) {
        if (!(hazards[index] & 2u)) continue;
        bool near_water = !radius;
        if (radius) {
            const int row = int(index) / width, column = int(index) % width;
            for (int z = std::max(0, row - *radius); z < std::min(height, row + *radius + 1) && !near_water; ++z)
                for (int x = std::max(0, column - *radius); x < std::min(width, column + *radius + 1); ++x)
                    if (hazards[std::size_t(z) * width + x] & (1u | 4u)) { near_water = true; break; }
        }
        if (near_water) classified[index] |= SHORE_EDGE;
    }
    return classified;
}
// bot_tactics_runtime.default_routes without editor intent: the graph routes and their symmetric flag.
Value defaultRoutes(PyObject* graph) {
    Value routes = cs::read(p::value(graph, "routes").get());
    auto pairs = [&](const char* key) { return cs::read(p::value(graph, key).get()); };
    const Value spawns = pairs("spawn_anchors"), bases = pairs("objective_bases"), radii = pairs("objective_base_radii");
    auto rows = [](const Value& v) -> std::vector<Value> { return v.resolved().kind == Kind::Array ? v.resolved().array : std::vector<Value>(); };
    const auto s = rows(spawns), o = rows(bases), r = rows(radii);
    bool symmetric = s.size() == 2 && o.size() == 2 && r.size() == 2;
    for (std::size_t i = 0; symmetric && i < 2; ++i) {
        const auto& a = s[i].resolved().array;
        const auto& c = o[i].resolved().array;
        if (a.size() < 2 || c.size() < 2) throw p::Error("navigation graph objective anchors are invalid");
        symmetric = std::hypot(a[0].numeric() - c[0].numeric(), a[1].numeric() - c[1].numeric()) <= r[i].numeric();
    }
    for (int team : {1, 2}) {
        auto found = routes.object.find(std::to_string(team));
        if (found == routes.object.end()) continue;
        for (auto& route : found->second.array) route.object["_allocation_symmetric"] = boolean(symmetric);
    }
    return routes;
}
// planner._map_data_with_baked_routes: graph geometry with the tactical map's route strategy.
std::map<int, std::vector<Value>> directorRoutes(const std::string& map_name, const Value& baked) {
    p::Ref maps_module(PyImport_ImportModule("gui.mods.offline_lan_0922.ai.maps"));
    auto tactical = py::attr(maps_module.get(), "TACTICAL_MAPS");
    PyObject* map_data = PyDict_GetItemString(tactical.get(), map_name.c_str());
    std::map<int, std::vector<Value>> out;
    if (!map_data) return out;   // map_data None: the director has no routes
    auto strategy_routes = p::value(map_data, "routes");
    for (int team : {1, 2}) {
        std::map<std::string, Value> strategy;
        if (PyDict_Check(strategy_routes.get())) {
            auto key = p::integer(team);
            PyObject* rows = PyDict_GetItem(strategy_routes.get(), key.get());
            if (rows && rows != Py_None)
                for (Py_ssize_t i = 0; i < PySequence_Size(rows); ++i) {
                    Value route = cs::read(p::item(rows, i).get());
                    strategy[pyStr(at(route, "id"))] = std::move(route);
                }
        }
        const auto* rows = teamRows(baked, team);
        if (!rows) continue;
        for (const auto& raw : *rows) {
            if (raw.resolved().kind != Kind::Object) continue;
            Value waypoints = array();
            for (const auto& point : at(raw, "waypoints").resolved().array) {
                const auto& row = point.resolved().array;
                if (row.size() < 2 || !isNumber(row[0]) || !isNumber(row[1])) continue;
                Value converted = array();
                converted.array = {number(row[0].numeric()), number(row[1].numeric()), boolean(row.size() > 2 && row[2].truth())};
                waypoints.array.push_back(std::move(converted));
            }
            if (waypoints.array.empty()) continue;
            Value route = raw.resolved();
            auto found = strategy.find(pyStr(at(raw, "id")));
            if (found != strategy.end()) {
                for (const char* key : {"capacity", "risk", "role_weights", "class_weights"})
                    if (has(found->second, key)) route.object[key] = at(found->second, key);
            }
            if (at(route, "role_weights").resolved().kind != Kind::Object) route.object["role_weights"] = object();
            route.object["waypoints"] = std::move(waypoints);
            out[team].push_back(std::move(route));
        }
    }
    if (out[1].empty() || out[2].empty()) throw p::Error("navigation graph routes are missing for the director");
    return out;
}

// ---------------------------------------------------------------------------------------------- initial allocation
std::string classTag(const Value& state) { return at(at(state, "profile"), "class_tag").resolved().text; }
// initial_allocation: a priority candidate is (priority, family, paired, payload index).
struct Ticket { std::int64_t priority; std::string family; bool paired; std::size_t payload; };
// initial_allocation.ordered_pairs: per class tag, the two teams' Bots matched by (slot, id) order.
struct Pair { std::string tag; std::size_t ordinal; std::array<const Value*, 2> sides; };
std::vector<Pair> orderedPairs(const std::vector<const Value*>& states) {
    std::vector<const Value*> sorted(states);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Value* a, const Value* b) {
        return std::make_pair(at(*a, "slot").integer, at(*a, "id").integer) < std::make_pair(at(*b, "slot").integer, at(*b, "id").integer);
    });
    std::map<std::string, std::array<std::vector<const Value*>, 2>> groups;
    for (const auto* state : sorted) {
        const auto team = at(*state, "team").integer;
        if (team != 1 && team != 2) throw p::Error("initial allocation team is invalid");
        groups[classTag(*state)][std::size_t(team - 1)].push_back(state);
    }
    std::vector<Pair> out;
    for (const auto& group : groups) {
        const auto& sides = group.second;
        for (std::size_t ordinal = 0; ordinal < std::max(sides[0].size(), sides[1].size()); ++ordinal)
            out.push_back({group.first, ordinal, {ordinal < sides[0].size() ? sides[0][ordinal] : nullptr, ordinal < sides[1].size() ? sides[1][ordinal] : nullptr}});
    }
    return out;
}
// initial_allocation.choose: the highest priority families, one seeded draw among them.
std::optional<Ticket> choose(const std::string& seed, const std::string& scope, const std::string& tag, std::size_t ordinal,
                             const std::vector<Ticket>& candidates, int side = 0) {
    if (candidates.empty()) return {};
    std::int64_t priority = candidates.front().priority;
    for (const auto& c : candidates) priority = std::max(priority, c.priority);
    std::map<std::string, Ticket> choices;
    for (const auto& c : candidates) if (c.priority == priority) choices[c.family] = c;
    Random generator(seed + ":" + scope + ":" + tag + ":" + std::to_string(ordinal) + ":" + std::to_string(side));
    auto chosen = choices.begin();
    std::advance(chosen, std::ptrdiff_t(generator.randrange(choices.size())));
    return chosen->second;
}
std::vector<Ticket> top(const std::vector<Ticket>& rows) {
    if (rows.empty()) return {};
    std::int64_t priority = rows.front().priority;
    for (const auto& c : rows) priority = std::max(priority, c.priority);
    std::vector<Ticket> out;
    for (const auto& c : rows) if (c.priority == priority) out.push_back(c);
    return out;
}
// initial_allocation.allocate: explicit symmetric families pair across teams; the rest choose locally.
template<class Candidates, class Reserve>
void allocate(const std::vector<const Value*>& states, Candidates candidates, Reserve reserve, const std::string& seed, const std::string& scope) {
    for (const auto& pair : orderedPairs(states)) {
        std::array<std::vector<Ticket>, 2> available;
        for (int i = 0; i < 2; ++i) if (pair.sides[i]) available[i] = candidates(*pair.sides[i]);
        const std::array<std::vector<Ticket>, 2> tops = {top(available[0]), top(available[1])};
        std::set<std::string> shared, other;
        for (const auto& c : tops[0]) if (c.paired) shared.insert(c.family);
        for (const auto& c : tops[1]) if (c.paired) other.insert(c.family);
        for (auto i = shared.begin(); i != shared.end();) i = other.count(*i) ? std::next(i) : shared.erase(i);
        bool coupled = !shared.empty();
        for (const auto& rows : tops) for (const auto& c : rows) coupled = coupled && c.paired;
        auto among = [&](const std::set<std::string>& families) {
            std::vector<Ticket> rows;
            for (const auto& c : tops[0]) if (families.count(c.family)) rows.push_back(c);
            return rows;
        };
        std::optional<Ticket> selected = coupled ? choose(seed, scope, pair.tag, pair.ordinal, among(shared)) : std::nullopt;
        std::array<std::optional<Ticket>, 2> local;
        for (int i = 0; i < 2; ++i) local[i] = choose(seed, scope, pair.tag, pair.ordinal, available[i], i + 1);
        if (!coupled) {
            std::set<std::string> requested;
            for (const auto& c : local) if (c && c->paired && shared.count(c->family)) requested.insert(c->family);
            if (!requested.empty()) { selected = choose(seed, scope, pair.tag, pair.ordinal, among(requested)); coupled = true; }
        }
        for (int i = 0; i < 2; ++i) {
            if (!pair.sides[i]) continue;
            std::optional<Ticket> choice;
            if (coupled) {
                for (const auto& c : tops[std::size_t(i)]) if (c.family == selected->family) { choice = c; break; }
                if (!choice) throw p::Error("paired initial allocation family is unavailable");
            } else choice = local[std::size_t(i)];
            reserve(*pair.sides[i], choice);
        }
    }
}

// ---------------------------------------------------------------------------------------------- the round
struct Built {
    std::shared_ptr<RoundSeed> seed;
    std::uint64_t allocation_seed = 0;
};

// bot_tactics_runtime.assign_initial_routes without editor intent: lanes allocated with per-team,
// per-class capacity from the director catalog; a Bot with no open lane routes automatically.
void assignRoutes(RoundSeed& seed, const std::string& map_name, const std::string& allocation_seed) {
    std::vector<const Value*> states;
    for (const auto& bot : seed.bots) states.push_back(&bot.second.state);
    std::map<std::tuple<std::int64_t, std::string, std::string>, int> usage;
    std::vector<std::tuple<std::int64_t, std::string, std::string>> keys;
    std::vector<const Value*> payloads;
    std::map<std::int64_t, const Value*> plans;
    auto candidates = [&](const Value& state) {
        std::vector<Ticket> choices;
        const std::string tag = classTag(state);
        if (std::find(std::begin(CLASSES), std::end(CLASSES), tag) == std::end(CLASSES)) return choices;
        const auto team = at(state, "team").integer;
        auto rows = seed.routes.find(int(team));
        if (rows == seed.routes.end()) return choices;
        for (const auto& base : rows->second) {
            const auto& weights = at(base, "class_weights").resolved();
            if (weights.kind == Kind::Object && weights.object.count(tag) && weights.get(tag).numeric() <= 0.) continue;
            const auto key = std::make_tuple(team, (tag == "SPG" ? "spg:" : "") + at(base, "id").resolved().text, tag);
            auto used = usage.find(key);
            if (used != usage.end() && used->second >= 3) continue;
            const auto& priorities = at(base, "class_priorities").resolved();
            const std::int64_t priority = priorities.kind == Kind::Object && priorities.object.count(tag)
                ? std::int64_t(priorities.get(tag).numeric()) : DEFAULT_ROUTE_PRIORITY;
            keys.push_back(key);
            payloads.push_back(&base);
            choices.push_back({priority, "default:" + at(base, "id").resolved().text, at(base, "_allocation_symmetric").truth(), payloads.size() - 1});
        }
        return choices;
    };
    auto reserve = [&](const Value& state, const std::optional<Ticket>& selected) {
        const auto id = at(state, "id").integer;
        if (!selected) { plans[id] = nullptr; return; }
        plans[id] = payloads[selected->payload];
        ++usage[keys[selected->payload]];
    };
    allocate(states, candidates, reserve, allocation_seed, map_name + ":routes");
    for (const auto& plan : plans) {
        auto& state = seed.bots.at(plan.first).state;
        state.object["route"] = plan.second ? *plan.second : Value();
        seed.agents.at(plan.first).object["route"] = state.object["route"];
        log("BOT TACTICS route bot=" + std::to_string(plan.first) + " map=" + map_name + " status=" +
            (plan.second ? "random_default" : "full_or_unusable_routes_auto") + " id=" +
            (plan.second ? at(*plan.second, "id").resolved().text : std::string("auto_navigation")));
    }
}

// spg_positions.cell_bounds / point_in_cell.
std::array<double, 4> cellBounds(const std::string& cell, const std::array<double, 4>& bounds) {
    const std::string rows = "ABCDEFGHJK", columns = "1234567890";
    if (cell.size() != 2 || rows.find(cell[0]) == std::string::npos || columns.find(cell[1]) == std::string::npos)
        throw p::Error("invalid minimap cell");
    if (!(bounds[0] < bounds[2] && bounds[1] < bounds[3])) throw p::Error("empty arena bounds");
    const double dx = (bounds[2] - bounds[0]) / 10., dz = (bounds[3] - bounds[1]) / 10.;
    const double column = double(columns.find(cell[1])), row = double(rows.find(cell[0]));
    return {bounds[0] + column * dx, bounds[3] - (row + 1) * dz, bounds[0] + (column + 1) * dx, bounds[3] - row * dz};
}
bool pointInCell(const stock_native_bot_parking::Point& point, const std::string& cell, const std::array<double, 4>& bounds, double margin) {
    const auto box = cellBounds(cell, bounds);
    return box[0] + margin <= point[0] && point[0] <= box[2] - margin && box[1] + margin <= point[2] && point[2] <= box[3] - margin;
}
std::array<double, 4> bounds4(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind != Kind::Array || r.array.size() != 4) throw p::Error("SPG catalog bounds are invalid");
    return {r.array[0].numeric(NAN), r.array[1].numeric(NAN), r.array[2].numeric(NAN), r.array[3].numeric(NAN)};
}
// spg_positions.side_for_team from the graph bases.
std::string sideForTeam(std::int64_t team, const Value& bases) {
    const auto& r = bases.resolved();
    if ((team != 1 && team != 2) || r.kind != Kind::Array || r.array.size() != 2) return {};
    const auto& own = r.array[std::size_t(team - 1)].resolved().array;
    const auto& enemy = r.array[std::size_t(2 - team)].resolved().array;
    if (own.size() < 2 || enemy.size() < 2) return {};
    const double dx = own[0].numeric() - enemy[0].numeric(), dz = own[1].numeric() - enemy[1].numeric();
    if (std::abs(dx) + std::abs(dz) < 1.) return {};
    if (std::abs(dz) >= std::abs(dx)) return dz > 0. ? "north" : "south";
    return dx > 0. ? "east" : "west";
}
// spg_positions._has_initial_support: enough friendly non-SPG planned routes cross the zone's columns.
bool initialSupport(const Value& zone, std::int64_t team, const std::vector<const Value*>& states, const std::array<double, 4>& bounds) {
    const auto& required = at(zone, "requires_support");
    if (!required.truth()) return true;
    int count = 0;
    for (const auto* state : states) {
        if (at(*state, "team").integer != team || classTag(*state) == "SPG") continue;
        const auto& points = at(at(*state, "route"), "waypoints").resolved();
        bool supported = false;
        for (std::size_t i = 1; points.kind == Kind::Array && i < points.array.size() && !supported; ++i) {
            const auto& raw = points.array[i].resolved();
            const Value& coordinate = raw.kind == Kind::Object ? raw.get("x") : (raw.kind == Kind::Array && !raw.array.empty() ? raw.array[0] : raw);
            if (!isNumber(coordinate)) continue;
            const double x = coordinate.numeric();
            for (const auto& column : at(required, "columns").resolved().array) {
                const auto box = cellBounds("A" + column.resolved().text, bounds);
                if (box[0] <= x && x <= box[2]) { supported = true; break; }
            }
        }
        count += supported ? 1 : 0;
    }
    return count >= at(required, "minimum").numeric();
}
// spg_positions.assign_initial_positions: one sourced catalog destination per SPG, at most three a team.
void assignSpgPositions(RoundSeed& seed, const std::string& map_name, const Value& graph_value, const std::string& allocation_seed,
                        std::map<std::int64_t, std::string>& outcomes, std::map<std::int64_t, Value>& plans) {
    std::vector<const Value*> states;
    for (const auto& bot : seed.bots) states.push_back(&bot.second.state);
    std::vector<BotSeed*> artillery;
    for (auto& bot : seed.bots) if (classTag(bot.second.state) == "SPG") artillery.push_back(&bot.second);
    std::sort(artillery.begin(), artillery.end(), [](const BotSeed* a, const BotSeed* b) {
        return std::make_pair(at(a->state, "team").integer, at(a->state, "id").integer) < std::make_pair(at(b->state, "team").integer, at(b->state, "id").integer);
    });
    if (artillery.empty()) return;
    p::Ref data_module(PyImport_ImportModule("gui.mods.offline_lan_0922.spg_position_data"));
    const Value catalog = cs::read(py::attr(data_module.get(), "CATALOG").get());
    const Value& entry = at(at(catalog, "maps"), map_name.c_str());
    if (entry.resolved().kind == Kind::Null) {
        for (auto* bot : artillery) outcomes[at(bot->state, "id").integer] = "source_not_catalogued";
        return;
    }
    std::array<double, 4> bounds{};
    offline_navigation::MapInput map;
    std::array<std::array<int, 2>, 8> directions{};
    bool compatible = true;
    try {
        bounds = bounds4(at(entry, "bounds"));
        if (pyStr(at(graph_value, "map")) != map_name || pyStr(at(graph_value, "game_version")) != pyStr(at(catalog, "game_version")))
            throw p::Error("navigation graph is not the pinned #1513 graph");
        if (!stock_native_bot_planning::readMapInput(seed, map)) throw p::Error("SPG navigation grid");
        if (!(map.width > 0 && map.width <= 4096 && map.height > 0 && map.height <= 4096 && std::int64_t(map.width) * map.height <= 1000000 &&
              map.cell_size > 0. && map.cell_size <= 32.))
            throw p::Error("invalid SPG navigation grid");
        const auto& raw_directions = at(graph_value, "directions").resolved().array;
        std::set<std::array<int, 2>> unique;
        if (raw_directions.size() != 8) throw p::Error("invalid SPG graph edges");
        for (std::size_t i = 0; i < 8; ++i) {
            const auto& d = raw_directions[i].resolved().array;
            if (d.size() != 2) throw p::Error("invalid SPG graph edges");
            directions[i] = {int(d[0].numeric()), int(d[1].numeric())};
            if (std::max(std::abs(directions[i][0]), std::abs(directions[i][1])) != 1 || !unique.insert(directions[i]).second)
                throw p::Error("invalid SPG graph edges");
        }
        if (bounds4(at(graph_value, "bounds")) != bounds) throw p::Error("catalog and map bounds differ");
    } catch (const std::exception&) {
        PyErr_Clear();
        compatible = false;
    }
    if (!compatible) {
        for (auto* bot : artillery) outcomes[at(bot->state, "id").integer] = "incompatible_graph";
        return;
    }
    map.has_bounds = true;
    map.bounds = bounds;
    stock_native_bot_parking::Parking grid(map, directions);
    std::map<std::int64_t, std::vector<std::pair<stock_native_bot_parking::Point, double>>> reservations{{1, {}}, {2, {}}};
    std::map<std::int64_t, std::map<std::string, int>> used_zones{{1, {}}, {2, {}}};
    struct Candidate { const Value* zone; std::string cell; int index; stock_native_bot_parking::Point point; };
    std::map<std::pair<std::string, double>, std::vector<Candidate>> candidate_cache;
    std::map<std::int64_t, std::size_t> ordinals;
    {
        std::vector<const Value*> rows;
        for (auto* bot : artillery) rows.push_back(&bot->state);
        for (const auto& pair : orderedPairs(rows))
            for (const auto* actor : pair.sides) if (actor) ordinals[at(*actor, "id").integer] = pair.ordinal;
    }
    // _Graph.candidates: per sourced cell, up to eight clear points nearest its centre.
    auto candidatesFor = [&](const std::string& side, double radius) {
        std::vector<Candidate> result;
        for (const auto& zone : at(entry, "zones").resolved().array) {
            if (at(zone, "side").resolved().text != side) continue;
            for (const auto& cell_value : at(zone, "cells").resolved().array) {
                const std::string cell = cell_value.resolved().text;
                const auto box = cellBounds(cell, bounds);
                const double centre_x = (box[0] + box[2]) * .5, centre_z = (box[1] + box[3]) * .5;
                const int min_col = std::max(0, int(std::ceil((box[0] + radius - map.origin_x) / map.cell_size)));
                const int max_col = std::min(map.width - 1, int(std::floor((box[2] - radius - map.origin_x) / map.cell_size)));
                const int min_row = std::max(0, int(std::ceil((box[1] + radius - map.origin_z) / map.cell_size)));
                const int max_row = std::min(map.height - 1, int(std::floor((box[3] - radius - map.origin_z) / map.cell_size)));
                std::vector<std::pair<double, int>> ordered;
                for (int row = min_row; row <= max_row; ++row)
                    for (int col = min_col; col <= max_col; ++col) {
                        const int index = row * map.width + col;
                        if (!grid.usable(index)) continue;
                        const auto point = grid.point(index);
                        ordered.emplace_back((point[0] - centre_x) * (point[0] - centre_x) + (point[2] - centre_z) * (point[2] - centre_z), index);
                    }
                std::sort(ordered.begin(), ordered.end());
                std::vector<stock_native_bot_parking::Point> selected;
                for (const auto& row : ordered) {
                    const auto point = grid.point(row.second);
                    if (std::any_of(selected.begin(), selected.end(), [&](const stock_native_bot_parking::Point& old) {
                            return std::hypot(point[0] - old[0], point[2] - old[2]) < radius * 2 + RESERVATION_GAP;
                        }))
                        continue;
                    if (!grid.clear(row.second, radius)) continue;
                    selected.push_back(point);
                    result.push_back({&zone, cell, row.second, point});
                    if (selected.size() >= 8) break;
                }
            }
        }
        return result;
    };
    for (auto* bot : artillery) {
        const auto& state = bot->state;
        const auto actor = at(state, "id").integer, team = at(state, "team").integer;
        if (reservations[team].size() >= 3) { outcomes[actor] = "outside_three_spg_initial_capacity"; continue; }
        const std::string side = sideForTeam(team, at(graph_value, "bases"));
        if (side.empty() || pyStr(at(at(entry, "spawn_sides"), std::to_string(team).c_str())) != side) { outcomes[actor] = "unknown_spawn_side"; continue; }
        const auto& shape = at(state, "collision_shape").resolved().array;
        const double hw = shape.at(0).numeric(), hl = shape.at(1).numeric();
        if (!(.3 <= hw && hw <= 20. && .5 <= hl && hl <= 30.)) { outcomes[actor] = "vehicle_footprint_unavailable"; continue; }
        const double radius = std::hypot(hw, hl) + PARKING_RADIUS + CLEARANCE_MARGIN;
        const stock_native_bot_parking::Point origin{at(state, "x").numeric(), at(state, "y").numeric(), at(state, "z").numeric()};
        const auto key = std::make_pair(side, std::round(radius * 1.e5) / 1.e5);
        if (!candidate_cache.count(key)) candidate_cache[key] = candidatesFor(side, radius);
        const auto& candidates = candidate_cache[key];
        if (candidates.empty()) { outcomes[actor] = "no_safe_sourced_cell"; continue; }
        const auto reachable = grid.distances(origin);
        struct Choice { std::int64_t used, priority; double distance; std::string zone, cell; int index; const Candidate* candidate; };
        std::vector<Choice> choices;
        for (const auto& candidate : candidates) {
            const auto& point = candidate.point;
            if (!initialSupport(*candidate.zone, team, states, bounds)) continue;
            if (!std::isfinite(reachable.at(std::size_t(candidate.index)))) continue;
            if (std::any_of(reservations[team].begin(), reservations[team].end(), [&](const std::pair<stock_native_bot_parking::Point, double>& old) {
                    return std::hypot(point[0] - old.first[0], point[2] - old.first[2]) < radius + old.second + RESERVATION_GAP;
                }))
                continue;
            const std::string zone_id = at(*candidate.zone, "id").resolved().text;
            choices.push_back({used_zones[team][zone_id], std::int64_t(at(*candidate.zone, "priority").numeric()),
                               reachable[std::size_t(candidate.index)], zone_id, candidate.cell, candidate.index, &candidate});
        }
        if (choices.empty()) { outcomes[actor] = "no_reachable_unreserved_sourced_cell"; continue; }
        // The allocation draw ranks within each zone by (priority, distance, zone, cell, index), not usage.
        std::vector<const Choice*> ranked;
        for (const auto& choice : choices) ranked.push_back(&choice);
        std::stable_sort(ranked.begin(), ranked.end(), [](const Choice* a, const Choice* b) {
            return std::tie(a->priority, a->distance, a->zone, a->cell, a->index) < std::tie(b->priority, b->distance, b->zone, b->cell, b->index);
        });
        std::map<std::string, const Choice*> by_zone;
        for (const auto* choice : ranked) by_zone.emplace(choice->zone, choice);
        std::vector<Ticket> tickets;
        std::vector<const Choice*> payloads;
        for (const auto& zone : by_zone) {
            payloads.push_back(zone.second);
            tickets.push_back({-zone.second->priority, zone.first, false, payloads.size() - 1});
        }
        const auto chosen = choose(allocation_seed, map_name + ":source_spg", "SPG", ordinals.at(actor), tickets, int(team));
        const Choice& selected = *payloads[chosen->payload];
        const auto& zone = *selected.candidate->zone;
        const auto point = selected.candidate->point;
        const auto& enemy = at(graph_value, "bases").resolved().array.at(std::size_t(2 - team)).resolved().array;
        stock_native_bot_parking::Point face{enemy.at(0).numeric(), point[1], enemy.at(1).numeric()};
        if (at(zone, "face_cell").truth()) {
            const auto box = cellBounds(at(zone, "face_cell").resolved().text, bounds);
            face = {(box[0] + box[2]) * .5, point[1], (box[1] + box[3]) * .5};
        }
        // spg_positions.canonical_plan of the generated plan: it must stay inside its cell and the map.
        auto inside = [&](const stock_native_bot_parking::Point& v) {
            return bounds[0] <= v[0] && v[0] <= bounds[2] && bounds[1] <= v[2] && v[2] <= bounds[3] && -1000. <= v[1] && v[1] <= 1000.;
        };
        const std::string vehicle = at(state, "vehicle").resolved().text;
        if (!(radius <= 50.) || !inside(point) || !inside(face) || !pointInCell(point, selected.cell, bounds, PARKING_RADIUS) ||
            vehicle.empty() || vehicle.size() > 80) {
            outcomes[actor] = "plan_validation_failed";
            continue;
        }
        auto vector = [](const stock_native_bot_parking::Point& v) {
            Value out = object();
            out.object["x"] = number(v[0]);
            out.object["y"] = number(v[1]);
            out.object["z"] = number(v[2]);
            return out;
        };
        Value plan = object();
        plan.object["schema"] = integer(1);
        plan.object["catalog"] = at(catalog, "revision");
        plan.object["map"] = text(map_name);
        plan.object["mode"] = text("regular");
        plan.object["side"] = text(side);
        plan.object["zone"] = text(selected.zone);
        plan.object["cell"] = text(selected.cell);
        plan.object["source"] = at(zone, "source");
        plan.object["vehicle"] = text(vehicle);
        plan.object["point"] = vector(point);
        plan.object["face"] = vector(face);
        plan.object["radius"] = number(PARKING_RADIUS);
        plan.object["clearance"] = number(radius);
        plan.object["geometry"] = text("baked_checked");
        plan.object["fire_validation"] = text("runtime_required");
        reservations[team].emplace_back(point, radius);
        ++used_zones[team][selected.zone];
        plans[actor] = std::move(plan);
        outcomes[actor] = "assigned";
    }
}

// BotRuntime._prepare_initial_spg_positions: an invalid optional plan is contained to the SPGs.
void prepareSpgPositions(RoundSeed& seed, const std::string& map_name, const Value& graph_value, const std::string& allocation_seed) {
    std::map<std::int64_t, std::string> outcomes;
    std::map<std::int64_t, Value> plans;
    std::vector<BotSeed*> artillery;
    for (auto& bot : seed.bots) if (classTag(bot.second.state) == "SPG") artillery.push_back(&bot.second);
    try {
        assignSpgPositions(seed, map_name, graph_value, allocation_seed, outcomes, plans);
    } catch (const std::exception&) {
        PyErr_Clear();
        plans.clear();
        outcomes.clear();
        for (auto* bot : artillery) outcomes[at(bot->state, "id").integer] = "invalid_optional_position_data";
    }
    for (auto* bot : artillery) {
        const auto id = at(bot->state, "id").integer;
        auto outcome = outcomes.find(id);
        if (outcome == outcomes.end()) continue;
        auto plan = plans.find(id);
        if (plan != plans.end()) {
            bot->state.object["_spg_initial"] = plan->second;
            Value origin = array();
            origin.array = {at(bot->state, "x"), at(bot->state, "y"), at(bot->state, "z")};
            bot->state.object["_spg_selection_origin"] = std::move(origin);
        } else bot->state.object.erase("_spg_initial");
        bot->state.object["_spg_initial_status"] = text(outcome->second);
        const Value* p = plan != plans.end() ? &plan->second : nullptr;
        char point[160] = "-";
        if (p) snprintf(point, sizeof(point), "(%.12g, %.12g, %.12g)", at(at(*p, "point"), "x").numeric(),
                             at(at(*p, "point"), "y").numeric(), at(at(*p, "point"), "z").numeric());
        log("SPG INITIAL map=" + map_name + " bot=" + std::to_string(id) + " status=" + outcome->second + " side=" +
            (p ? at(*p, "side").resolved().text : "-") + " zone=" + (p ? at(*p, "zone").resolved().text : "-") + " cell=" +
            (p ? at(*p, "cell").resolved().text : "-") + " point=" + point + " source=" +
            (p ? at(*p, "source").resolved().text : "legacy_fallback") + " fire=runtime_required");
    }
}

// BotRuntime._manifest_entry: the published identity, pose, loadout, ledgers, rating, route and SPG plan.
Value manifestEntry(const BotSeed& bot) {
    const Value& state = bot.state;
    Value result = object();
    for (const char* key : {"id", "team", "slot", "name", "vehicle", "health", "max_health", "x", "y", "z", "yaw", "fire_seq",
                            "shell_index", "next_shell_index", "ammo_remaining", "ammo_reload_pending", "reload_time", "reload_duration",
                            "clip", "clip_size", "siege_state", "siege_time_left_ms", "siege_transition_total_ms", "equipment_states",
                            "stun_end_server_time_ms"}) {
        if (!has(state, key)) throw p::Error(std::string("Bot manifest state lacks ") + key);
        result.object[key] = at(state, key);
    }
    Value profile = at(state, "profile").resolved();
    profile.object["parking_radius"] = number(std::hypot(at(state, "half_length").numeric(3.5), at(state, "half_width").numeric(1.7)));
    result.object["profile"] = std::move(profile);
    result.object["skill_rating"] = number(bot.rating);
    result.object["skill"] = text(skillForRating(bot.rating));
    PyObject* active = at(state, "siege_state").integer == SIEGE_ENABLED && bot.siege ? bot.siege.get() : bot.travel.get();
    if (auto terminal = terminalCritical(active)) result.object["terminal_critical"] = std::move(*terminal);
    if (has(state, "_spg_initial")) result.object["spg_initial"] = at(state, "_spg_initial");
    result.object["world_pose"] = boolean(true);
    const Value& route = at(state, "route");
    const auto& points = at(route, "waypoints").resolved();
    if (points.kind == Kind::Array && points.array.size() > 16) throw p::Error("bot route exceeds the 16-waypoint LAN protocol limit");
    Value wire = object();
    wire.object["id"] = has(route, "id") ? at(route, "id") : text("map_route");
    Value waypoints = array();
    if (points.kind == Kind::Array)
        for (const auto& point : points.array) {
            const auto& row = point.resolved().array;
            Value waypoint = object();
            waypoint.object["x"] = row.at(0);
            waypoint.object["y"] = number(0.);
            waypoint.object["z"] = row.at(1);
            waypoint.object["hold"] = boolean(row.size() > 2 && row[2].truth());
            waypoints.array.push_back(std::move(waypoint));
        }
    wire.object["waypoints"] = std::move(waypoints);
    for (const char* key : {"capacity", "risk"}) if (has(route, key)) wire.object[key] = at(route, key);
    for (const char* key : {"role_weights", "class_weights"})
        if (at(route, key).resolved().kind == Kind::Object) wire.object[key] = at(route, key);
    result.object["route"] = std::move(wire);
    return result;
}

// BotRuntime._player_collision_manifest: the client-effective human bodies the authority donates.
Value playerCollisionManifest(Battle& b, const Value& players) {
    std::map<std::int64_t, Value> rows;
    const auto& list = players.resolved();
    if (list.kind != Kind::Null && list.kind != Kind::Array) throw p::Error("player collision manifest row is invalid");
    for (const auto& raw : list.array) {
        if (raw.resolved().kind != Kind::Object) throw p::Error("player collision manifest row is invalid");
        const auto id = pyInt(at(raw, "id"));
        if (!id || !has(raw, "vehicle") || at(raw, "vehicle").resolved().kind == Kind::Null) throw p::Error("player collision manifest identity is invalid");
        const std::string vehicle = isText(at(raw, "vehicle")) ? at(raw, "vehicle").resolved().text : pyStr(at(raw, "vehicle"));
        if (*id == WORKER_AUTHORITY_ID) continue;
        if (*id <= 0 || rows.count(*id) || vehicle.empty()) throw p::Error("player collision manifest identity is invalid");
        const auto snapshot = params::canonicalEffectiveParams(at(raw, "effective_params"));
        if (!snapshot) throw p::Error("player effective parameters are missing or invalid");
        auto descriptor = startup::playerDescriptor(b, raw.resolved());
        if (descriptor.get() == Py_None) throw p::Error("player collision manifest descriptor is unavailable");
        const double mass = at(at(*snapshot, "physics"), "mass").numeric(NAN);
        const auto shape = stock_native_bot_descriptor::chassisShape(descriptor.get());
        if (!std::isfinite(mass) || mass <= 0. || std::any_of(shape.begin(), shape.end(), [](double v) { return !std::isfinite(v); }))
            throw p::Error("player collision manifest body is invalid");
        Value row = object();
        row.object["id"] = integer(*id);
        row.object["vehicle"] = text(vehicle);
        row.object["mass"] = number(mass);
        Value body = array();
        for (double v : shape) body.array.push_back(number(v));
        row.object["shape"] = std::move(body);
        Value ram = object();
        ram.object["spall_coefficient"] = at(at(*snapshot, "ramming"), "spall_coefficient");
        ram.object["ramming_bonus"] = at(at(*snapshot, "ramming"), "ramming_bonus");
        row.object["ram_profile"] = std::move(ram);
        rows[*id] = std::move(row);
    }
    Value out = array();
    for (auto& row : rows) out.array.push_back(std::move(row.second));
    return out;
}

// One Bot's round-start record: descriptor pair, rating, director agent, pose, combat and ledgers.
// One named step of a Bot's installation, so a descriptor failure says which read raised.
template<class Body> auto labeled(const char* name, const Body& body) {
    try { return body(); }
    catch (const std::exception& e) { const std::string why = describe(e); PyErr_Clear(); throw p::Error(std::string(name) + ": " + why); }
}
BotSeed installBot(Battle& b, RoundSeed& seed, const Value& raw, std::int64_t bot_id, const std::string& vehicle_name, std::pair<Object, Object> pair,
                   double rating, std::uint32_t battle_seed, std::int64_t server_time_ms) {
    BotSeed bot;
    const Value raw_state = has(raw, "siege_state") ? at(raw, "siege_state") : integer(SIEGE_DISABLED);
    const Value raw_time = has(raw, "siege_time_left_ms") ? at(raw, "siege_time_left_ms") : integer(0);
    const Value raw_total = has(raw, "siege_transition_total_ms") ? at(raw, "siege_transition_total_ms") : integer(0);
    if (!validSiegeWire(raw_state, raw_time, bool(pair.second), raw_total)) throw p::Error("authority manifest Bot Siege state is invalid");
    const std::int64_t siege_state = std::int64_t(raw_state.numeric()), wire_time = std::int64_t(raw_time.numeric()), wire_total = std::int64_t(raw_total.numeric());
    if (siege_state == SIEGE_ENABLED && !pair.second) throw p::Error("non-Siege vehicle cannot enter Siege mode");
    PyObject* descriptor = siege_state == SIEGE_ENABLED ? pair.second.get() : pair.first.get();
    bot.travel = std::move(pair.first);
    bot.siege = std::move(pair.second);
    bot.rating = rating;
    bot.crew_level = stock_native_bot_descriptor::crewLevelForRating(rating);
    const std::int64_t team = pyInt(at(raw, "team")).value_or(1), slot = pyInt(at(raw, "slot")).value_or(0);
    const Value name = has(raw, "name") ? at(raw, "name") : text("Bot");
    // BattleDirector.register / register_profile.
    Value profile;
    if (at(raw, "profile").resolved().kind == Kind::Object) {
        profile = at(raw, "profile").resolved();
        profile.object["roles"] = at(profile, "roles").resolved().kind == Kind::Object ? at(profile, "roles") : object();
        profile.object["shells"] = at(profile, "shells").resolved().kind == Kind::Array ? at(profile, "shells") : array();
    } else profile = labeled("vehicle profile", [&] { return vehicleProfile(descriptor); });
    const auto agent_seed = stableSeed(std::to_string(battle_seed) + "|" + std::to_string(bot_id) + "|" + pyStr(name) + "|" + pyStr(at(profile, "vehicle_name")));
    Value agent = object();
    agent.object["id"] = integer(bot_id);
    agent.object["team"] = integer(team);
    agent.object["profile"] = profile;
    agent.object["personality"] = personality(agent_seed);
    agent.object["seed"] = integer(agent_seed);
    agent.object["route"] = Value();
    agent.object["waypoint_index"] = integer(0);
    agent.object["health_fraction"] = number(1.);
    seed.agents[bot_id] = std::move(agent);
    // BotRuntime.states row.
    const auto spawn = labeled("formation pose", [&] { return startup::formationPose(b, team, slot); });
    bot.spawn = spawn.first;
    auto max_health_value = attribute(descriptor, "maxHealth");
    const std::int64_t max_health = std::max<std::int64_t>(1, max_health_value.get() != Py_None ? std::int64_t(p::scalar(max_health_value.get()))
                                                                                                 : std::int64_t(pyNumber(at(raw, "max_health"), 1000.)));
    const std::int64_t health = std::max<std::int64_t>(0, std::min<std::int64_t>(std::int64_t(pyNumber(at(raw, "health"), double(max_health))), max_health));
    const auto shape = labeled("chassis shape", [&] { return stock_native_bot_descriptor::chassisShape(descriptor); });
    p::Ref drive(labeled("drive", [&] { return stock_native_bot::readUnmodifiedDrive(descriptor); }));
    auto misc = attribute(descriptor, "miscAttrs");
    auto spall_raw = attribute(misc.get(), "antifragmentationLiningFactor");
    const double spall = spall_raw.get() == Py_None ? 1. : plannerNumber(spall_raw.get(), NAN);
    if (!std::isfinite(spall) || spall < 1.) throw p::Error("#1513 Spall Liner factor is invalid");
    Value& s = bot.state;
    s = object();
    auto& o = s.object;
    o["id"] = integer(bot_id);
    o["team"] = integer(team);
    o["slot"] = integer(slot);
    o["name"] = name;
    o["vehicle"] = text(vehicle_name);
    o["x"] = number(pyNumber(at(raw, "x"), spawn.first[0]));
    o["y"] = number(pyNumber(at(raw, "y"), spawn.first[1]));
    o["z"] = number(pyNumber(at(raw, "z"), spawn.first[2]));
    o["yaw"] = number(pyNumber(at(raw, "yaw"), spawn.second));
    o["aim_yaw"] = o["yaw"];
    for (const char* key : {"turret_yaw", "gun_pitch", "desired_gun_pitch", "push_x", "push_z", "air_lateral_x", "air_lateral_z", "slide_speed",
                            "vertical_speed", "last_drive_pitch", "suspension_pitch_velocity", "suspension_roll_velocity",
                            "_suspension_support_vertical_speed", "suspension_pitch", "_siege_intent_elapsed", "_drown_check", "_drown_time",
                            "_overturn_check", "_overturn_time"})
        o[key] = number(0.);
    for (const char* key : {"gun_aligned", "hull_aiming", "airborne", "grounded_once", "_drowning", "_overturned"}) o[key] = boolean(false);
    o["health"] = integer(health);
    o["max_health"] = integer(max_health);
    o["alive"] = boolean((has(raw, "alive") ? at(raw, "alive").truth() : health > 0) && health > 0);
    o["fire_seq"] = integer(std::max<std::int64_t>(0, std::int64_t(pyNumber(at(raw, "fire_seq"), 0.))));
    o["shell_index"] = integer(std::max<std::int64_t>(0, std::min<std::int64_t>(std::int64_t(pyNumber(at(raw, "shell_index"), 0.)), 9)));
    o["speed"] = number(pyNumber(at(raw, "speed")));
    o["movement_dir"] = integer(0);
    o["rotation_dir"] = integer(0);
    o["half_length"] = number(shape[1]);
    o["half_width"] = number(shape[0]);
    Value body = array();
    for (double v : shape) body.array.push_back(number(v));
    o["collision_shape"] = std::move(body);
    o["mass"] = number(p::numeric(drive.get(), "mass"));
    Value ram = object();
    ram.object["spall_coefficient"] = number(spall);
    ram.object["ramming_bonus"] = number(0.);
    o["ram_profile"] = std::move(ram);
    if (at(raw, "ram_motion").truth()) throw p::Error("authority manifest Bot ram motion belongs to an authority restore");
    o["ram_motion"] = array();
    o["_ram_motion_seq"] = integer(0);
    o["pitch"] = number(pyNumber(at(raw, "pitch")));
    o["roll"] = number(pyNumber(at(raw, "roll")));
    o["terrain_pitch"] = number(pyNumber(at(raw, "pitch")));
    o["siege_state"] = integer(siege_state);
    o["siege_time_left_ms"] = integer(wire_time);
    o["_siege_time_left"] = number(double(wire_time) / 1000.);
    o["siege_transition_total_ms"] = integer(wire_total);
    o["_siege_transition_total"] = number(double(wire_total) / 1000.);
    o["_siege_intent"] = boolean(siege_state == SIEGE_ENABLED);
    o["_overturn_level"] = integer(0);
    o["critical"] = at(raw, "critical").resolved().kind == Kind::Object ? at(raw, "critical").resolved() : object();
    for (const char* key : {"combat_revision", "combat_base_revision", "combat_ack_seq"})
        o[key] = integer(std::max<std::int64_t>(0, std::int64_t(pyNumber(at(raw, key), 0.))));
    o["combat_fire_elapsed"] = number(round6(clamp(pyNumber(at(raw, "combat_fire_elapsed"), 0.), 0., FIRE_DURATION_SECONDS)));
    o["combat_fire_timer"] = number(round6(clamp(pyNumber(at(raw, "combat_fire_timer"), 0.), 0., FIRE_TICK_SECONDS - .000001)));
    o["profile"] = profile;
    o["route"] = Value();
    // _observe_bot_stun: the server-owned stun end anchored to the worker's equipment clock.
    const auto stun = has(raw, "stun_end_server_time_ms") ? pyInt(at(raw, "stun_end_server_time_ms")) : std::optional<std::int64_t>(0);
    if (!stun || at(raw, "stun_end_server_time_ms").resolved().kind == Kind::Boolean || *stun < 0 || server_time_ms < 0) throw p::Error("bot stun clock is invalid");
    o["stun_end_server_time_ms"] = integer(*stun);
    o["_stun_until_equipment_time"] = number(std::max(0., double(*stun - server_time_ms) / 1000.));
    // _install_bot_equipments: one independent exact consumable ledger, published as trusted snapshots.
    if (has(raw, "equipment_states")) throw p::Error("authority manifest Bot equipment belongs to an authority restore");
    Value contracts = array();
    for (const auto& contract : params::defaultBotConsumables(py::attr(b.engine.vehicles.get(), "g_cache").get())) contracts.array.push_back(contract.toValue());
    Value wire_states = array();
    for (const auto& contract : params::botConsumableContracts(&contracts)) {
        const params::EquipmentState equipment(contract, 0.);
        wire_states.array.push_back(equipment.trustedSnapshot(0.));
        stock_native_bot::SeedEquipment row;
        row.kind = contract.kind;
        row.uses_left = equipment.uses_left;
        row.ready_at = equipment.ready_at;
        row.active = equipment.active;
        row.autoactivate = contract.autoactivate;
        row.repair_all = contract.repair_all;
        row.cooldown_seconds = contract.cooldown_seconds;
        row.auto_reaction_seconds = contract.auto_reaction_seconds;
        row.bonus_value = contract.bonus_value;
        row.crew_level_increase = contract.crew_level_increase;
        row.fire_starting_chance_factor = contract.fire_starting_chance_factor;
        row.engine_power_factor = contract.engine_power_factor;
        row.turret_rotation_speed_factor = contract.turret_rotation_speed_factor;
        row.engine_hp_loss_per_second = contract.engine_hp_loss_per_second;
        bot.equipment.push_back(std::move(row));
    }
    o["equipment_states"] = std::move(wire_states);
    // The fresh gun, ammunition and combat ledger exactly as the cold installation will read them.
    step::CombatSnapshot combat;
    if (!labeled("combat", [&] { return step::load_combat_snapshot(bot, descriptor, combat); })) throw p::Error("Bot " + std::to_string(bot_id) + " combat installation failed");
    const auto& gun = *combat.gun;
    o["shell_index"] = integer(combat.ammo.loaded);
    o["next_shell_index"] = integer(combat.ammo.next);
    Value remaining = array();
    for (int n : combat.ammo.remaining) remaining.array.push_back(integer(n));
    o["ammo_remaining"] = std::move(remaining);
    o["ammo_reload_pending"] = boolean(combat.ammo.reload_pending);
    o["combat_seq"] = o["combat_ack_seq"];
    const double reload_factor = combat.initial_critical_factors.at("reload");
    o["clip_size"] = integer(combat.gun_config.clip_size);
    o["clip"] = integer(gun.clip);
    o["reload_time"] = number(gun.remaining(reload_factor));
    o["reload_duration"] = number(gun.duration(reload_factor));
    return bot;
}

BotAuthority& authorityOf(Battle& b) {
    if (!b.bots) b.bots = std::make_unique<BotAuthority>();
    return *b.bots;
}
bool authorityIsLocal(const Battle& b, const BotAuthority& a) { return a.authority_id && *a.authority_id == b.local_player; }
void clearPending(BotAuthority& a) {
    a.pending_manifest = Value();
    a.pending_round = Value();
    a.pending_authority.reset();
}

// _server_order_signature: a live target's aim/face and the moving advance goal are not strategy.
Value orderSignature(const Value* order) {
    Value result = order && order->resolved().kind == Kind::Object ? order->resolved() : object();
    if (at(result, "target_id").resolved().kind != Kind::Null) {
        result.object.erase("aim_position");
        if (!at(result, "stable_hull_face").truth()) result.object.erase("face_position");
        if (isText(at(result, "combat_mode")) && at(result, "combat_mode").resolved().text == "advance_contact") result.object.erase("move_position");
    }
    return result;
}
// BotRuntime._apply_orders: the newest complete server order set; changed Bots get a new token.
void applyOrders(BotAuthority& a, const Value& message) {
    if (!has(message, "bot_orders")) return;
    const auto& orders = at(message, "bot_orders").resolved();
    if (orders.kind != Kind::Array) return;
    const auto revision = pyInt(at(message, "bot_order_revision"));
    if (!revision || *revision < a.orders.revision) return;
    std::map<std::int64_t, Value> accepted;
    for (std::size_t i = 0; i < std::min<std::size_t>(30, orders.array.size()); ++i) {
        const auto& raw = orders.array[i].resolved();
        if (raw.kind != Kind::Object || at(raw, "id").resolved().kind == Kind::Null) return;
        const auto id = pyInt(at(raw, "id"));
        if (!id || accepted.count(*id)) return;
        Value order = raw;
        for (const char* name : {"aim_position", "face_position", "move_position", "route_anchor"}) {
            const auto& point = at(raw, name).resolved();
            if (point.kind == Kind::Object) {
                Value row = array();
                row.array = {number(point.get("x").numeric()), number(point.get("y").numeric()), number(point.get("z").numeric())};
                order.object[name] = std::move(row);
            } else if (point.kind == Kind::Array) {
                // _boundary_point: a short sequence is the origin.
                Value row = array();
                for (std::size_t k = 0; k < 3; ++k) row.array.push_back(number(point.array.size() >= 3 ? point.array[k].numeric() : 0.));
                order.object[name] = std::move(row);
            }
        }
        for (const char* name : {"desired_range", "fire_range", "throttle_override", "hull_angle_degrees", "arrival_radius"})
            if (at(raw, name).resolved().kind != Kind::Null) order.object[name] = number(pyNumber(at(raw, name)));
        for (const char* name : {"id", "team", "target_id", "route_index", "shell_index"})
            if (at(raw, name).resolved().kind != Kind::Null) {
                const auto n = pyInt(at(raw, name));
                if (!n) return;
                order.object[name] = integer(*n);
            }
        if (at(raw, "cover_id").resolved().kind != Kind::Null && (!isText(at(raw, "cover_id")) || at(raw, "cover_id").resolved().text.size() > 80)) return;
        accepted[*id] = std::move(order);
    }
    std::set<std::int64_t> changed;
    for (const auto& row : a.orders.orders) changed.insert(row.first);
    for (const auto& row : accepted) changed.insert(row.first);
    for (auto i = changed.begin(); i != changed.end();) {
        auto before = a.orders.orders.find(*i), after = accepted.find(*i);
        const bool same = orderSignature(before != a.orders.orders.end() ? &before->second : nullptr) ==
                          orderSignature(after != accepted.end() ? &after->second : nullptr);
        i = same ? changed.erase(i) : std::next(i);
    }
    for (const auto id : changed) ++a.orders.tokens[id];
    a.orders.orders = std::move(accepted);
    a.orders.revision = *revision;
    if (!changed.empty() && a.control_seconds > 0. && a.refresh_bank + 1.0e-9 < a.control_seconds) a.refresh_bank += a.control_seconds;
}

// The manifest build of a fresh round: line-up rows, director, lanes, SPG plans and the native generation.
std::vector<Value> buildManifest(Battle& b, BotAuthority& a, const Value& message) {
    // Native failures carry no source position: name the round-start step that raised.
    auto stage = [](const std::string& name, const auto& body) {
        try { return body(); }
        catch (const std::exception& e) { const std::string why = describe(e); PyErr_Clear(); throw p::Error("Bot round start " + name + ": " + why); }
    };
    const std::string mode = normalizeSkillMode(at(message, "bot_skill_mode"));
    std::map<std::pair<std::int64_t, std::int64_t>, std::string> pins;
    for (const auto& raw : at(message, "bot_lineup").resolved().array) {
        if (raw.resolved().kind != Kind::Object || !skillTier(at(raw, "skill"))) continue;
        const auto team = pyInt(at(raw, "team")), slot = pyInt(at(raw, "slot"));
        if (team && slot) pins[{*team, *slot}] = at(raw, "skill").resolved().text;
    }
    if (has(message, "battle_mode") && !(isText(at(message, "battle_mode")) && at(message, "battle_mode").resolved().text == "regular"))
        throw p::Error("the native Bot round start supports the regular battle mode only");
    const Value round_id = at(message, "round_id");
    auto seed = std::make_shared<RoundSeed>();
    seed->round = pyInt(round_id).value_or(0);
    seed->control_seconds = a.control_seconds;
    // BotRuntime._ensure_navigation_graph: the startup graph, validated for this map.
    const std::string map_name = normalizeMapName(at(message, "map"));
    if (map_name.empty()) throw p::Error("battle map name is unavailable");
    PyObject* graph = b.startup ? b.startup->navigation_graph.get() : nullptr;
    if (!graph || graph == Py_None) throw p::Error("standard battle map is not supported: " + map_name);
    if (normalizeMapName(cs::read(p::value(graph, "map").get())) != map_name) throw p::Error("required navigation graph is missing for " + map_name);
    if (p::string(p::value(graph, "format").get()) != "offline-lan-0922-navgraph" || p::integral(graph, "version", -1) != 2)
        throw p::Error("unsupported baked navigation graph");
    const Value baked_routes = stage("baked routes", [&] { return defaultRoutes(graph); });
    stage("route validation", [&] { validateRoutes(baked_routes); });
    seed->map = map_name;
    seed->graph = borrow(graph);
    seed->hazards = stage("shore hazards", [&] { return shoreHazards(graph); });
    seed->routes = stage("director routes", [&] { return directorRoutes(map_name, baked_routes); });
    const auto battle_seed = stableSeed(pyStr(pyInt(round_id) ? round_id : integer(0)) + "|" + map_name);
    const std::int64_t server_time_ms = std::max<std::int64_t>(0, std::int64_t(pyNumber(at(message, "server_time_ms"), 0.)));
    for (const auto& raw : at(message, "bots").resolved().array) {
        if (raw.resolved().kind != Kind::Object || at(raw, "id").resolved().kind == Kind::Null) continue;
        const std::int64_t bot_id = needInt(at(raw, "id"), "Bot id");
        // _select_bot_vehicle: the requested type, else the (team, slot) assignment, else the config type.
        std::string vehicle_name;
        if (at(raw, "vehicle").truth()) vehicle_name = at(raw, "vehicle").resolved().text;
        else {
            const auto& assigned = vehicleAssignments(b);
            auto found = assigned.find({pyInt(at(raw, "team")).value_or(1), pyInt(at(raw, "slot")).value_or(0)});
            vehicle_name = found != assigned.end() ? found->second : b.config.resolved().get("vehicle").resolved().text;
        }
        std::pair<Object, Object> pair;
        try {
            auto resolved = startup::descriptorFor(b, vehicle_name);
            pair = descriptorPair(resolved.get());
        } catch (const std::exception& e) {
            throw p::Error("bot " + std::to_string(bot_id) + " vehicle " + vehicle_name + " descriptor is unavailable: " + describe(e));
        }
        // _resolve_bot_rating: the published rating, the published tier, the host's pin, then the room preset.
        double rating;
        if (has(raw, "skill_rating")) rating = normalizeRating(at(raw, "skill_rating"));
        else if (skillTier(at(raw, "skill"))) rating = ratingForSkill(at(raw, "skill").resolved().text);
        else {
            const auto team = pyInt(has(raw, "team") ? at(raw, "team") : integer(1)), slot = pyInt(has(raw, "slot") ? at(raw, "slot") : integer(0));
            if (!team || !slot) throw p::Error("authority manifest Bot slot identity is invalid");
            auto pin = pins.find({*team, *slot});
            rating = pin != pins.end() ? ratingForSkill(pin->second) : resolveRating(mode, round_id, *team, *slot);
        }
        seed->bots[bot_id] = stage("Bot " + std::to_string(bot_id) + " " + vehicle_name, [&] {
            return installBot(b, *seed, raw, bot_id, vehicle_name, std::move(pair), rating, battle_seed, server_time_ms);
        });
    }
    // _prepare_user_routes and _prepare_initial_spg_positions with one round allocation seed.
    std::random_device device;
    const std::uint64_t allocation = (std::uint64_t(device()) << 32) ^ std::uint64_t(device());
    const std::string allocation_seed = std::to_string(allocation);
    stage("routes", [&] { assignRoutes(*seed, map_name, allocation_seed); });
    Value graph_value = object();
    for (const char* key : {"map", "game_version", "directions", "bounds", "bases"}) graph_value.object[key] = cs::read(p::value(graph, key).get());
    stage("SPG positions", [&] { prepareSpgPositions(*seed, map_name, graph_value, allocation_seed); });
    // _ensure_native_simulation: a new generation retires the previous native owner.
    seed->generation = ++a.generation;
    if (a.owner) {
        auto retired = std::move(a.owner);
        stock_native_bot::owner::close(*retired);
    }
    std::vector<std::pair<std::pair<std::int64_t, std::int64_t>, const BotSeed*>> ordered;
    for (const auto& bot : seed->bots)
        ordered.push_back({{at(bot.second.state, "slot").integer, at(bot.second.state, "team").integer}, &bot.second});
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    Value outgoing = object();
    outgoing.object["type"] = text("bot_manifest");
    Value bots = array();
    stage("manifest", [&] { for (const auto& row : ordered) bots.array.push_back(manifestEntry(*row.second)); });
    outgoing.object["bots"] = std::move(bots);
    if (at(message, "human_ram_timeline").truth())
        outgoing.object["player_collision_profiles"] = stage("player collision profiles", [&] { return playerCollisionManifest(b, at(message, "players")); });
    a.profiles.clear();
    for (const auto& bot : seed->bots) {
        Value row = object();
        for (const char* name : {"mass", "collision_shape", "ram_profile", "vehicle", "team"}) row.object[name] = at(bot.second.state, name);
        a.profiles[bot.first] = std::move(row);
    }
    a.seed = std::move(seed);
    a.pending_manifest = outgoing;
    a.pending_round = a.round_id;
    a.pending_authority = a.authority_id;
    return {std::move(outgoing)};
}
}

// The pinned BotRuntime.battle_start for this process. Only the hidden worker can be the authority;
// it builds a fresh round's manifest once and publishes it through the pending manifest tenure.
std::vector<Value> roundStart(Battle& b, const Value& raw) {
    auto& a = authorityOf(b);
    const Value& message = raw.resolved();
    if (message.kind != Kind::Object) throw p::Error("Bot round start message is invalid");
    // The server pins bot_tactics.empty() when no profile is configured; authored profiles are not ported yet.
    if (has(message, "bot_tactics")) {
        const Value& tactics = at(message, "bot_tactics").resolved();
        const bool empty = tactics.kind == Kind::Null || (tactics.kind == Kind::Object && at(tactics, "behavior").resolved().array.empty() &&
                                                          at(tactics, "maps").resolved().object.empty());
        if (!empty) throw p::Error("authored Bot tactics profiles are not part of the native round start");
    }
    if (b.worker) a.control_seconds = WORKER_CONTROL_SECONDS;
    const Value round_id = at(message, "round_id");
    if (round_id != a.round_id) {
        a.round_id = round_id;
        a.seed.reset();
        a.profiles.clear();
        a.finished = a.manifest_sent = false;
        clearPending(a);
        a.orders = stock_native_bot::ServerOrders();
        a.refresh_bank = 0.;
    }
    applyOrders(a, message);
    if (at(message, "battle_result").resolved().kind != Kind::Null) a.finished = true;
    const auto previous = a.authority_id;
    const auto& raw_authority = at(message, "bot_authority_id").resolved();
    if (raw_authority.kind == Kind::Null) a.authority_id.reset();
    else if (raw_authority.kind == Kind::Integer) a.authority_id = raw_authority.integer;
    else throw p::Error("Bot authority id is invalid");
    if (previous != a.authority_id) {
        a.manifest_sent = false;
        clearPending(a);
        // The new owner has no decision cache: its first physical slice refreshes.
        a.refresh_bank = a.control_seconds;
        if (previous && authorityIsLocal(b, a) && at(message, "bot_manifest").resolved().kind == Kind::Array)
            throw p::Error("a Bot authority takeover is not supported by the hidden worker");
    }
    if (!authorityIsLocal(b, a)) return {};
    if (!b.worker) throw p::Error("visible client attempted to become bot authority");
    if (a.finished) { clearPending(a); return {}; }
    if (const Value* pending = pendingManifest(b)) return {*pending};
    if (a.manifest_sent) return {};
    if (at(message, "bot_manifest").truth()) throw p::Error("a restored Bot authority manifest is not supported by the hidden worker");
    return buildManifest(b, a, message);
}

const Value* pendingManifest(const Battle& b) {
    const auto* a = b.bots.get();
    if (!a || a->manifest_sent || a->pending_manifest.kind == Kind::Null || !authorityIsLocal(b, *a) || a->pending_round != a->round_id ||
        a->pending_authority != a->authority_id)
        return nullptr;
    return &a->pending_manifest;
}

bool discardPendingManifest(Battle& b) {
    if (!b.bots) return false;
    const bool pending = b.bots->pending_manifest.kind != Kind::Null;
    clearPending(*b.bots);
    return pending;
}

bool markManifestEnqueued(Battle& b, const Value& message) {
    const Value* pending = pendingManifest(b);
    if (!pending || message.resolved().kind != Kind::Object || message != *pending) return false;
    b.bots->manifest_sent = true;
    clearPending(*b.bots);
    return true;
}

// The round's Bot skill context for visible-client peer drive: tactics, skill mode, lineup pins and round.
Value peerSkillContext(const Battle& b) {
    const Value& start = b.start.resolved();
    Value out;
    out.kind = Kind::Object;
    out.object["tactics"] = at(start, "bot_tactics");
    Value mode;
    mode.kind = Kind::Text;
    mode.text = normalizeSkillMode(at(start, "bot_skill_mode"));
    out.object["mode"] = mode;
    const Value& round_id = at(start, "round_id").resolved();
    Value round;
    round.kind = Kind::Text;
    round.text = round_id.kind == Kind::Text ? round_id.text : round_id.kind == Kind::Integer ? std::to_string(round_id.integer) : "None";
    out.object["round"] = round;
    Value pins;
    pins.kind = Kind::Array;
    for (const auto& raw : at(start, "bot_lineup").resolved().array) {
        const auto& row = raw.resolved();
        if (row.kind != Kind::Object || !skillTier(at(row, "skill"))) continue;
        const Value& team = at(row, "team").resolved();
        const Value& slot = at(row, "slot").resolved();
        if (team.kind != Kind::Integer || slot.kind != Kind::Integer) continue;
        Value pin;
        pin.kind = Kind::Array;
        pin.array = {team, slot, at(row, "skill").resolved()};
        pins.array.push_back(std::move(pin));
    }
    out.object["pins"] = pins;
    return out;
}
}
}
