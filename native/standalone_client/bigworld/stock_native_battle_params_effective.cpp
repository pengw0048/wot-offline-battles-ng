// effective_params.py battle law: the canonical client-derived parameter snapshot and its living-crew queries.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_params.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace native_battle {
namespace params {
namespace {
using K = Value::Kind;
using Keys = std::set<std::string>;

const Keys LOADOUT_FLOATS{"crew_level", "commander_level", "effective_crew_level", "crew_multiplier", "crew_factor", "gun_rotation_factor",
                          "reload_factor", "aim_time_factor", "dispersion_factor", "repair_factor", "vehicle_rotation_factor", "radio_factor",
                          "bloom_move_factor", "bloom_rotation_factor", "bloom_turret_factor"};
const Keys LOADOUT_BOOLS{"has_big_kit", "from_client_factors", "has_rammer", "has_aim_drives", "has_ventilation", "has_stabiliser",
                         "has_rations", "has_brotherhood", "has_snap_shot", "has_smooth_ride", "has_sixth_sense"};
const Keys SPOTTING_FLOATS{"commander_level", "recon_level", "situational_level", "camouflage_level", "binocular_factor", "binocular_delay",
                           "camouflage_net_bonus", "camouflage_net_delay", "vision_factor", "camouflage_factor"};
const Keys SPOTTING_BOOLS{"has_binoculars", "has_camouflage_net", "from_client_factors"};
const Keys CRITICAL_DEVICE_NAMES{"engineHealth", "ammoBayHealth", "fuelTankHealth", "radioHealth", "leftTrackHealth", "rightTrackHealth",
                                 "gunHealth", "turretRotatorHealth", "surveyingDeviceHealth"};
const Keys CREW_TARGET_NAMES{"commander", "driver", "gunner", "gunner1", "gunner2", "loader", "loader1", "loader2", "radioman", "radioman1", "radioman2"};
const Keys SHELL_KINDS{"HOLLOW_CHARGE", "HIGH_EXPLOSIVE", "ARMOR_PIERCING", "ARMOR_PIERCING_HE", "ARMOR_PIERCING_CR"};
const Keys SOURCE_SHELL_KEYS{"kind", "caliber", "damage", "explosionRadius"};
const Keys SOURCE_SHELL_HE_KEYS{"explosionDamageFactor", "explosionDamageAbsorptionFactor", "explosionEdgeDamageFactor"};
const char* const STUN_SHELL_FIELDS[] = {"stunRadius", "stunDuration", "stunFactor", "guaranteedStunDuration", "damageDurationCoeff",
                                         "guaranteedStunEffect", "damageEffectCoeff"};
const std::map<std::string, std::string> DISCRETE_SKILL_ROLES{
    {"commander_sixthsense", "commander"}, {"commander_expert", "commander"}, {"gunner_sniper", "gunner"}, {"loader_intuition", "loader"},
    {"driver_rammingmaster", "driver"}, {"gunner_rancorous", "gunner"}, {"radioman_lasteffort", "radioman"}};
// crew_battle.DEFAULTS
const char* const CREW_BATTLE_FIELDS[] = {"adrenaline_health_fraction", "adrenaline_reload_factor", "engine_fire_factor", "damaged_gun_factor"};

const Value& at(const Value& v, const char* name) { return v.resolved().get(name); }
const Value& at(const Value& v, const std::string& name) { return v.resolved().get(name); }
bool has(const Value& v, const char* name) { return v.resolved().kind == K::Object && v.resolved().object.count(name); }
Keys keysOf(const Value& v) {
    Keys out;
    for (const auto& item : v.resolved().object) out.insert(item.first);
    return out;
}
bool mapping(const Value& v, const Keys& keys) { return v.resolved().kind == K::Object && keysOf(v) == keys; }
Keys join(const Keys& a, const Keys& b) { Keys out = a; out.insert(b.begin(), b.end()); return out; }

// effective_params._exact_int: a non-bool integer within the range.
std::optional<std::int64_t> exactInt(const Value& v, std::int64_t minimum, std::int64_t maximum) {
    const auto& r = v.resolved();
    if (r.kind != K::Integer || r.integer < minimum || r.integer > maximum) return {};
    return r.integer;
}
// effective_params._number: a finite non-bool int or float within the range.
std::optional<double> number(const Value& v, double minimum, double maximum) {
    const auto& r = v.resolved();
    if (r.kind != K::Integer && r.kind != K::Number) return {};
    const double n = r.kind == K::Integer ? double(r.integer) : r.number;
    if (!std::isfinite(n) || n < minimum || n > maximum) return {};
    return n;
}
std::optional<bool> boolean(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind != K::Boolean) return {};
    return r.boolean;
}
std::optional<std::vector<double>> tuple(const Value& v, std::size_t size, double minimum, double maximum) {
    const auto& r = v.resolved();
    if (r.kind != K::Array || r.array.size() != size) return {};
    std::vector<double> out;
    for (const auto& entry : r.array) {
        auto n = number(entry, minimum, maximum);
        if (!n) return {};
        out.push_back(*n);
    }
    return out;
}
bool validSkillName(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind != K::Text || r.text.empty() || r.text.size() > 64) return false;
    return std::all_of(r.text.begin(), r.text.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

Value num(double v) { Value x; x.kind = K::Number; x.number = v; return x; }
Value integer(std::int64_t v) { Value x; x.kind = K::Integer; x.integer = v; return x; }
Value flag(bool v) { Value x; x.kind = K::Boolean; x.boolean = v; return x; }
Value str(const std::string& v) { Value x; x.kind = K::Text; x.text = v; return x; }
Value object() { Value x; x.kind = K::Object; return x; }
Value array() { Value x; x.kind = K::Array; return x; }
Value numbers(const std::vector<double>& values) { Value x = array(); for (double v : values) x.array.push_back(num(v)); return x; }

std::optional<Value> loadout(const Value& v) {
    if (!mapping(v, join(join(LOADOUT_FLOATS, LOADOUT_BOOLS), {"terrain_resistance_factors"}))) return {};
    Value out = object();
    for (const auto& name : LOADOUT_FLOATS) {
        const double maximum = name.size() > 6 && name.compare(name.size() - 6, 6, "_level") == 0 ? 1000. : 100.;
        auto n = number(at(v, name), 0., maximum);
        if (!n) return {};
        out.object[name] = num(*n);
    }
    for (const auto& name : LOADOUT_BOOLS) {
        auto f = boolean(at(v, name));
        if (!f) return {};
        out.object[name] = flag(*f);
    }
    if (!out.object["from_client_factors"].boolean) return {};
    auto terrain = tuple(at(v, "terrain_resistance_factors"), 3, 0.000001, 1000.);
    if (!terrain) return {};
    out.object["terrain_resistance_factors"] = numbers(*terrain);
    return out;
}

std::optional<Value> physics(const Value& v) {
    static const std::vector<std::pair<const char*, std::pair<double, double>>> bounds{
        {"mass", {1., 1000000.}}, {"powerW", {1., 1000000000.}}, {"speedFwd", {0., 1000.}}, {"speedBwd", {0., 1000.}}, {"rotSpd", {0., 100.}},
        {"specificFriction", {0., 1000.}}, {"brakeDecel", {0., 10000.}}, {"trackCenter", {0.01, 100.}}, {"minPlaneNormalY", {-1., 1.}},
        {"nativePowerRatio", {0.000001, 1000.}}};
    Keys keys{"terrainResist", "rotationIsAroundCenter"};
    for (const auto& b : bounds) keys.insert(b.first);
    if (!mapping(v, keys)) return {};
    Value out = object();
    for (const auto& b : bounds) {
        auto n = number(at(v, b.first), b.second.first, b.second.second);
        if (!n) return {};
        out.object[b.first] = num(*n);
    }
    auto terrain = tuple(at(v, "terrainResist"), 3, 0.000001, 1000000.);
    auto pivot = boolean(at(v, "rotationIsAroundCenter"));
    if (!terrain || !pivot) return {};
    out.object["terrainResist"] = numbers(*terrain);
    out.object["rotationIsAroundCenter"] = flag(*pivot);
    return out;
}

std::optional<Value> spotting(const Value& v) {
    if (!mapping(v, join(join(SPOTTING_FLOATS, SPOTTING_BOOLS), {"invisibility_moving", "invisibility_still"}))) return {};
    Value out = object();
    for (const auto& name : SPOTTING_FLOATS) {
        const double maximum = name.size() > 6 && name.compare(name.size() - 6, 6, "_level") == 0 ? 1000. : 100.;
        auto n = number(at(v, name), 0., maximum);
        if (!n) return {};
        out.object[name] = num(*n);
    }
    for (const auto& name : SPOTTING_BOOLS) {
        auto f = boolean(at(v, name));
        if (!f) return {};
        out.object[name] = flag(*f);
    }
    if (!out.object["from_client_factors"].boolean) return {};
    for (const char* name : {"invisibility_moving", "invisibility_still"}) {
        auto pair = tuple(at(v, name), 2, -100., 100.);
        if (!pair || (*pair)[1] < 0.) return {};
        out.object[name] = numbers(*pair);
    }
    return out;
}

std::optional<Value> ramming(const Value& v) {
    if (!mapping(v, {"spall_coefficient", "ramming_bonus"})) return {};
    auto spall = number(at(v, "spall_coefficient"), 1., 100.);
    auto bonus = number(at(v, "ramming_bonus"), 0., 0.15);
    if (!spall || !bonus) return {};
    Value out = object();
    out.object["spall_coefficient"] = num(*spall);
    out.object["ramming_bonus"] = num(*bonus);
    return out;
}

std::optional<Value> ammo(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind != K::Array || r.array.size() > 64) return {};
    Value out = array();
    std::int64_t previous = -1;
    for (const auto& entry : r.array) {
        const auto& e = entry.resolved();
        if (e.kind != K::Array || e.array.size() != 2) return {};
        auto compact = exactInt(e.array[0], 1, 4294967295LL);
        auto count = exactInt(e.array[1], 0, 1000000);
        if (!compact || !count || *compact <= previous) return {};
        previous = *compact;
        Value row = array();
        row.array.push_back(integer(*compact));
        row.array.push_back(integer(*count));
        out.array.push_back(std::move(row));
    }
    return out;
}

std::optional<Value> camouflage(const Value& v) {
    if (!mapping(v, {"camouflage_id", "base_moving", "base_still", "shot_factor"})) return {};
    Value id;
    if (at(v, "camouflage_id").resolved().kind != K::Null) {
        auto exact = exactInt(at(v, "camouflage_id"), 0, 4294967295LL);
        if (!exact) return {};
        id = integer(*exact);
    }
    auto moving = number(at(v, "base_moving"), 0., 100.), still = number(at(v, "base_still"), 0., 100.), shot = number(at(v, "shot_factor"), 0., 1.);
    if (!moving || !still || !shot) return {};
    Value out = object();
    out.object["camouflage_id"] = id;
    out.object["base_moving"] = num(*moving);
    out.object["base_still"] = num(*still);
    out.object["shot_factor"] = num(*shot);
    return out;
}

std::optional<Value> skills(const Value& v) {
    if (!mapping(v, {"sixth_sense", "expert", "deadeye", "intuition_chances", "controlled_impact", "designated_target", "last_effort"})) return {};
    Value out = object();
    for (const char* name : {"sixth_sense", "expert", "deadeye", "controlled_impact", "designated_target", "last_effort"}) {
        auto f = boolean(at(v, name));
        if (!f) return {};
        out.object[name] = flag(*f);
    }
    auto intuition = exactInt(at(v, "intuition_chances"), 0, 16);
    if (!intuition) return {};
    out.object["intuition_chances"] = integer(*intuition);
    return out;
}

// stun_mechanics.shell_component, rounded to six places; empty for an invalid component.
std::optional<Value> stunComponent(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind != K::Object || keysOf(r) != Keys(std::begin(STUN_SHELL_FIELDS), std::end(STUN_SHELL_FIELDS))) return {};
    Value out = object();
    for (const char* name : STUN_SHELL_FIELDS) {
        const std::string n = name;
        const double maximum = n == "stunRadius" ? 100. : n == "stunDuration" ? 900. : 1.;
        auto value = number(at(r, name), 0., maximum);
        if (!value) return {};
        out.object[name] = num(std::round(*value * 1e6) / 1e6);
    }
    return out;
}

std::optional<Value> sourceShot(const Value& v) {
    if (!mapping(v, {"speed", "gravity", "maxDistance", "piercingPower", "deadeye", "shell"})) return {};
    const auto& shell = at(v, "shell").resolved();
    if (shell.kind != K::Object) return {};
    Keys shell_keys = keysOf(shell);
    shell_keys.erase("stun");
    if (shell_keys != SOURCE_SHELL_KEYS && shell_keys != join(SOURCE_SHELL_KEYS, SOURCE_SHELL_HE_KEYS)) return {};
    const auto& kind = at(shell, "kind").resolved();
    auto deadeye = boolean(at(v, "deadeye"));
    if (kind.kind != K::Text || !SHELL_KINDS.count(kind.text) || !deadeye) return {};
    auto speed = number(at(v, "speed"), 0.000001, 3000.), gravity = number(at(v, "gravity"), 0.000001, 500.);
    auto maximum = number(at(v, "maxDistance"), 0.000001, 10000.);
    auto piercing = tuple(at(v, "piercingPower"), 2, 0., 10000.);
    auto caliber = number(at(shell, "caliber"), 0.000001, 1000.);
    std::optional<std::vector<double>> damage;
    const auto& raw_damage = at(shell, "damage").resolved();
    if (raw_damage.kind == K::Array && raw_damage.array.size() == 2) {
        auto hull = number(raw_damage.array[0], 0., 10000.), device = number(raw_damage.array[1], 0., MAX_CRITICAL_DEVICE_HP);
        if (hull && device) damage = std::vector<double>{*hull, *device};
    }
    auto radius = number(at(shell, "explosionRadius"), 0., 100.);
    std::optional<std::vector<std::pair<const char*, std::optional<double>>>> he;
    if (std::all_of(SOURCE_SHELL_HE_KEYS.begin(), SOURCE_SHELL_HE_KEYS.end(), [&](const std::string& k) { return has(shell, k.c_str()); }))
        he = std::vector<std::pair<const char*, std::optional<double>>>{
            {"explosionDamageFactor", number(at(shell, "explosionDamageFactor"), 0.000001, 10000.)},
            {"explosionDamageAbsorptionFactor", number(at(shell, "explosionDamageAbsorptionFactor"), 0.000001, 10000.)},
            {"explosionEdgeDamageFactor", number(at(shell, "explosionEdgeDamageFactor"), 0.000001, 1.)}};
    if (!speed || !gravity || !maximum || !piercing || !caliber || !damage || (*damage)[0] <= 0. || !radius ||
        (he && std::any_of(he->begin(), he->end(), [](const std::pair<const char*, std::optional<double>>& e) { return !e.second; })))
        return {};
    Value out = object();
    out.object["speed"] = num(*speed);
    out.object["gravity"] = num(*gravity);
    out.object["maxDistance"] = num(*maximum);
    out.object["piercingPower"] = numbers(*piercing);
    out.object["deadeye"] = flag(*deadeye);
    Value s = object();
    s.object["kind"] = str(kind.text);
    s.object["caliber"] = num(*caliber);
    s.object["damage"] = numbers(*damage);
    s.object["explosionRadius"] = num(*radius);
    if (he) for (const auto& e : *he) s.object[e.first] = num(*e.second);
    if (has(shell, "stun")) {
        if (kind.text != "HIGH_EXPLOSIVE" || at(shell, "stun").resolved().kind == K::Null) return {};
        auto component = stunComponent(at(shell, "stun"));
        if (!component) return {};
        s.object["stun"] = std::move(*component);
    }
    out.object["shell"] = std::move(s);
    return out;
}

std::optional<Value> gun(const Value& v) {
    if (!mapping(v, {"clip_size", "shots"})) return {};
    auto clip = exactInt(at(v, "clip_size"), 1, 255);
    const auto& shots = at(v, "shots").resolved();
    if (!clip || shots.kind != K::Array || shots.array.empty() || shots.array.size() > 64) return {};
    Value rows = array();
    std::set<std::int64_t> seen;
    for (const auto& entry : shots.array) {
        if (!mapping(entry, {"compact_descr", "source_shot"})) return {};
        auto compact = exactInt(at(entry, "compact_descr"), 1, 4294967295LL);
        auto shot = sourceShot(at(entry, "source_shot"));
        if (!compact || seen.count(*compact) || !shot) return {};
        seen.insert(*compact);
        Value row = object();
        row.object["compact_descr"] = integer(*compact);
        row.object["source_shot"] = std::move(*shot);
        rows.array.push_back(std::move(row));
    }
    Value out = object();
    out.object["clip_size"] = integer(*clip);
    out.object["shots"] = std::move(rows);
    return out;
}

std::optional<Value> equipment(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind != K::Array || r.array.size() > 3) return {};
    Value out = array();
    std::set<std::int64_t> ids, compacts;
    for (const auto& raw : r.array) {
        EquipmentContract contract;
        try { contract = validateContract(raw); }
        catch (const Invalid&) { return {}; }
        // #1513 handExtinguishers has local ID 0 and compact descriptor 251.
        if (contract.id < 0 || contract.compact_descr <= 0 || ids.count(contract.id) || compacts.count(contract.compact_descr)) return {};
        ids.insert(contract.id);
        compacts.insert(contract.compact_descr);
        out.array.push_back(contract.toValue());
    }
    return out;
}

std::optional<Value> critical(const Value& v) {
    const Keys base{"devices"}, targets{"devices", "activation_targets"}, complete{"devices", "activation_targets", "crew_roster"};
    if (v.resolved().kind != K::Object) return {};
    const Keys keys = keysOf(v);
    if (keys != base && keys != targets && keys != complete) return {};
    const auto& devices = at(v, "devices").resolved();
    if (devices.kind != K::Array || devices.array.empty() || devices.array.size() > CRITICAL_DEVICE_NAMES.size()) return {};
    std::vector<std::pair<std::string, Value>> rows;
    std::set<std::string> seen;
    for (const auto& entry : devices.array) {
        if (!mapping(entry, {"name", "max_hp", "regen_hp"})) return {};
        const auto& name = at(entry, "name").resolved();
        auto maximum = number(at(entry, "max_hp"), 1., MAX_CRITICAL_DEVICE_HP);
        auto regen = number(at(entry, "regen_hp"), 0., MAX_CRITICAL_DEVICE_HP);
        if (name.kind != K::Text || !CRITICAL_DEVICE_NAMES.count(name.text) || seen.count(name.text) || !maximum || !regen || *regen > *maximum) return {};
        seen.insert(name.text);
        Value row = object();
        row.object["name"] = str(name.text);
        row.object["max_hp"] = num(*maximum);
        row.object["regen_hp"] = num(*regen);
        rows.emplace_back(name.text, std::move(row));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const std::pair<std::string, Value>& a, const std::pair<std::string, Value>& b) { return a.first < b.first; });
    std::vector<std::pair<std::int64_t, Value>> target_rows;
    std::set<std::int64_t> indexes;
    const auto& raw_targets = at(v, "activation_targets").resolved();
    if (raw_targets.truth()) {
        if (raw_targets.kind != K::Array) return {};
        for (const auto& raw : raw_targets.array) {
            if (!mapping(raw, {"index", "name"})) return {};
            auto index = exactInt(at(raw, "index"), 1, 65535);
            const auto& name = at(raw, "name").resolved();
            if (!index || indexes.count(*index) || name.kind != K::Text || (!CRITICAL_DEVICE_NAMES.count(name.text) && !CREW_TARGET_NAMES.count(name.text)))
                return {};
            indexes.insert(*index);
            Value row = object();
            row.object["index"] = integer(*index);
            row.object["name"] = str(name.text);
            target_rows.emplace_back(*index, std::move(row));
        }
    }
    std::stable_sort(target_rows.begin(), target_rows.end(),
                     [](const std::pair<std::int64_t, Value>& a, const std::pair<std::int64_t, Value>& b) { return a.first < b.first; });
    Value out = object();
    Value device_list = array();
    for (auto& row : rows) device_list.array.push_back(std::move(row.second));
    out.object["devices"] = std::move(device_list);
    if (keys.count("activation_targets")) {
        Value list = array();
        for (auto& row : target_rows) list.array.push_back(std::move(row.second));
        out.object["activation_targets"] = std::move(list);
    }
    if (keys.count("crew_roster")) {
        const auto& raw = at(v, "crew_roster").resolved();
        if (raw.kind != K::Array || raw.array.empty() || raw.array.size() > 11) return {};
        Value roster = array();
        std::set<std::string> names;
        for (const auto& entry : raw.array) {
            const auto& name = entry.resolved();
            if (name.kind != K::Text || !CREW_TARGET_NAMES.count(name.text) || names.count(name.text)) return {};
            names.insert(name.text);
            roster.array.push_back(str(name.text));
        }
        out.object["crew_roster"] = std::move(roster);
    }
    return out;
}

// crew_battle.canonical: one native-derived crew battle row.
std::optional<Value> crewBattle(const Value& v) {
    if (!mapping(v, Keys(std::begin(CREW_BATTLE_FIELDS), std::end(CREW_BATTLE_FIELDS)))) return {};
    Value out = object();
    for (const char* name : CREW_BATTLE_FIELDS) {
        auto n = number(at(v, name), 0., 1.);
        if (!n) return {};
        const std::string key = name;
        if ((key == "adrenaline_reload_factor" || key == "damaged_gun_factor") && *n == 0.) return {};
        out.object[name] = num(*n);
    }
    return out;
}

std::optional<Value> crew(const Value& v) {
    if (!mapping(v, {"members", "dynamic_spotting"})) return {};
    const auto& raw_members = at(v, "members").resolved();
    if (raw_members.kind != K::Array || raw_members.array.empty() || raw_members.array.size() > 6) return {};
    const Keys allowed_roles{"commander", "driver", "gunner", "loader", "radioman"};
    Value members = array();
    std::vector<std::string> instances;
    for (const auto& raw : raw_members.array) {
        if (!mapping(raw, {"instance", "roles", "skills"})) return {};
        const auto& instance = at(raw, "instance").resolved();
        const auto& roles = at(raw, "roles").resolved();
        const auto& member_skills = at(raw, "skills").resolved();
        if (instance.kind != K::Text || !CREW_TARGET_NAMES.count(instance.text) ||
            std::find(instances.begin(), instances.end(), instance.text) != instances.end() || roles.kind != K::Array || roles.array.empty() ||
            roles.array.size() > allowed_roles.size() || member_skills.kind != K::Array || member_skills.array.size() > 32)
            return {};
        std::vector<std::string> canonical_roles;
        for (const auto& role : roles.array) {
            const auto& r = role.resolved();
            if (r.kind != K::Text || !allowed_roles.count(r.text) || std::find(canonical_roles.begin(), canonical_roles.end(), r.text) != canonical_roles.end())
                return {};
            canonical_roles.push_back(r.text);
        }
        std::string base = instance.text;
        while (!base.empty() && base.back() >= '0' && base.back() <= '9') base.pop_back();
        if (std::find(canonical_roles.begin(), canonical_roles.end(), base) == canonical_roles.end()) return {};
        std::vector<std::pair<std::string, Value>> canonical_skills;
        std::set<std::string> skill_names;
        for (const auto& skill : member_skills.array) {
            if (!mapping(skill, {"name", "level", "active", "enabled"})) return {};
            const auto& name = at(skill, "name");
            auto active = boolean(at(skill, "active")), enabled = boolean(at(skill, "enabled"));
            auto level = number(at(skill, "level"), 0., 100.);
            const bool valid = validSkillName(name);
            const auto required = valid ? skillRequiredRole(name.resolved().text) : std::optional<std::string>();
            if (!valid || skill_names.count(name.resolved().text) || !active || !enabled || !level ||
                (required && std::find(canonical_roles.begin(), canonical_roles.end(), *required) == canonical_roles.end()))
                return {};
            skill_names.insert(name.resolved().text);
            Value row = object();
            row.object["name"] = str(name.resolved().text);
            row.object["level"] = num(*level);
            row.object["active"] = flag(*active);
            row.object["enabled"] = flag(*enabled);
            canonical_skills.emplace_back(name.resolved().text, std::move(row));
        }
        std::stable_sort(canonical_skills.begin(), canonical_skills.end(),
                         [](const std::pair<std::string, Value>& a, const std::pair<std::string, Value>& b) { return a.first < b.first; });
        instances.push_back(instance.text);
        Value member = object();
        member.object["instance"] = str(instance.text);
        Value role_list = array();
        for (const auto& role : canonical_roles) role_list.array.push_back(str(role));
        member.object["roles"] = std::move(role_list);
        Value skill_list = array();
        for (auto& row : canonical_skills) skill_list.array.push_back(std::move(row.second));
        member.object["skills"] = std::move(skill_list);
        members.array.push_back(std::move(member));
    }
    const auto& dynamic = at(v, "dynamic_spotting");
    if (!mapping(dynamic, {"crew", "states"})) return {};
    const auto& raw_roster = at(dynamic, "crew").resolved();
    const auto& raw_states = at(dynamic, "states").resolved();
    if (raw_roster.kind != K::Array || raw_roster.array.size() != instances.size() || raw_states.kind != K::Object) return {};
    for (std::size_t i = 0; i < instances.size(); ++i)
        if (raw_roster.array[i].resolved().kind != K::Text || raw_roster.array[i].resolved().text != instances[i]) return {};
    Keys expected;
    for (std::size_t mask = 0; mask < (std::size_t(1) << instances.size()); ++mask)
        for (int fire : {0, 1}) expected.insert(std::to_string(mask) + ":" + std::to_string(fire));
    if (keysOf(raw_states) != expected) return {};
    const Keys row_keys{"vision", "signal", "camouflage", "base_moving", "base_still", "invisibility_moving", "invisibility_still"};
    Value states = object();
    std::size_t with_battle = 0;
    for (const auto& key : expected) {
        const auto& row = at(raw_states, key).resolved();
        if (row.kind != K::Object) return {};
        const bool has_battle = has(row, "battle_factors");
        with_battle += has_battle;
        if (!mapping(row, has_battle ? join(row_keys, {"battle_factors"}) : row_keys)) return {};
        Value out = object();
        if (has_battle) {
            auto battle = crewBattle(at(row, "battle_factors"));
            if (!battle) return {};
            out.object["battle_factors"] = std::move(*battle);
        }
        for (const char* name : {"vision", "signal", "camouflage"}) {
            auto n = number(at(row, name), 0., 10.);
            if (!n) return {};
            out.object[name] = num(*n);
        }
        for (const char* name : {"base_moving", "base_still"}) {
            auto n = number(at(row, name), 0., 100.);
            if (!n) return {};
            out.object[name] = num(*n);
        }
        for (const char* name : {"invisibility_moving", "invisibility_still"}) {
            auto pair = tuple(at(row, name), 2, -100., 100.);
            if (!pair || (*pair)[1] < 0.) return {};
            out.object[name] = numbers(*pair);
        }
        states.object[key] = std::move(out);
    }
    if (with_battle && with_battle != expected.size()) return {};
    Value out = object();
    out.object["members"] = std::move(members);
    Value spotting_rows = object();
    Value roster = array();
    for (const auto& instance : instances) roster.array.push_back(str(instance));
    spotting_rows.object["crew"] = std::move(roster);
    spotting_rows.object["states"] = std::move(states);
    out.object["dynamic_spotting"] = std::move(spotting_rows);
    return out;
}

std::optional<Value> battleBooster(const Value& v) {
    if (!mapping(v, {"compact_descr", "skill_overrides"})) return {};
    static const std::map<std::string, std::pair<double, double>> bounds{
        {"sixth_sense_delay", {0., 3.}}, {"designated_target_duration", {0., 4.}}, {"designated_target_sector", {0., 3.141592653589793}}, {"last_effort_duration", {0., 5.}}};
    auto compact = exactInt(at(v, "compact_descr"), 1, 2147483647LL);
    const auto& overrides = at(v, "skill_overrides").resolved();
    if (!compact || overrides.kind != K::Object) return {};
    Value result = object();
    for (const auto& item : overrides.object) {
        auto bound = bounds.find(item.first);
        if (bound == bounds.end()) return {};
        auto n = number(item.second, bound->second.first, bound->second.second);
        if (!n) return {};
        result.object[item.first] = num(*n);
    }
    Value out = object();
    out.object["compact_descr"] = integer(*compact);
    out.object["skill_overrides"] = std::move(result);
    return out;
}

// effective_params._crew_projection: the crew object of a snapshot (or the crew object itself).
const Value& crewProjection(const Value& value) {
    const auto& v = value.resolved();
    if (v.kind != K::Object) throw Invalid("crew skill snapshot is invalid");
    const auto& crew_value = has(v, "crew") ? at(v, "crew").resolved() : v;
    if (crew_value.kind != K::Object || at(crew_value, "members").resolved().kind != K::Array) throw Invalid("crew skill snapshot is invalid");
    return crew_value;
}
std::set<std::string> knockedOut(const Value& crew_value, const Value& crew_ko) {
    const Value* ko = &crew_ko.resolved();
    if (ko->kind == K::Null) return {};
    if (ko->kind == K::Object) {
        if (!has(*ko, "crew_ko")) return {};
        ko = &at(*ko, "crew_ko").resolved();
    }
    if (ko->kind != K::Array) throw Invalid("critical crew state is invalid");
    std::set<std::string> roster, out;
    for (const auto& member : at(crew_value, "members").resolved().array) roster.insert(at(member, "instance").resolved().text);
    for (const auto& instance : ko->array) {
        const auto& r = instance.resolved();
        const std::string name = r.kind == K::Text ? r.text : std::to_string(std::int64_t(r.numeric()));
        if (!roster.count(name)) throw Invalid("critical crew state is outside its snapshot");
        out.insert(name);
    }
    return out;
}
std::string lowered(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}
}

std::optional<std::string> skillRequiredRole(const std::string& raw) {
    const std::string name = lowered(raw);
    auto exact = DISCRETE_SKILL_ROLES.find(name);
    if (exact != DISCRETE_SKILL_ROLES.end()) return exact->second;
    const std::string prefix = name.substr(0, name.find('_'));
    for (const char* role : {"commander", "driver", "gunner", "loader", "radioman"})
        if (prefix == role) return prefix;
    return {};
}

std::vector<SkillCarrier> livingSkillStates(const Value& snapshot, const std::string& raw_wanted, const Value& crew_ko) {
    const std::string wanted = lowered(raw_wanted);
    if (!validSkillName(str(wanted))) throw Invalid("invalid crew skill name");
    const auto& crew_value = crewProjection(snapshot);
    const auto knocked_out = knockedOut(crew_value, crew_ko);
    std::vector<SkillCarrier> out;
    for (const auto& member : at(crew_value, "members").resolved().array) {
        const std::string instance = at(member, "instance").resolved().text;
        if (knocked_out.count(instance)) continue;
        for (const auto& skill : at(member, "skills").resolved().array) {
            const auto& active = at(skill, "active").resolved(), &enabled = at(skill, "enabled").resolved();
            if (at(skill, "name").resolved().text == wanted && active.kind == K::Boolean && active.boolean && enabled.kind == K::Boolean &&
                enabled.boolean && at(skill, "level").numeric() >= 100.) {
                SkillCarrier carrier{instance, {}, at(skill, "level").numeric()};
                for (const auto& role : at(member, "roles").resolved().array) carrier.roles.push_back(role.resolved().text);
                out.push_back(std::move(carrier));
            }
        }
    }
    return out;
}

double livingSkillLevel(const Value& snapshot, const std::string& raw_wanted, const Value& crew_ko) {
    const std::string wanted = lowered(raw_wanted);
    if (!validSkillName(str(wanted))) throw Invalid("invalid crew skill name");
    const auto& crew_value = crewProjection(snapshot);
    const auto knocked_out = knockedOut(crew_value, crew_ko);
    double result = 0.;
    for (const auto& member : at(crew_value, "members").resolved().array) {
        if (knocked_out.count(at(member, "instance").resolved().text)) continue;
        for (const auto& skill : at(member, "skills").resolved().array) {
            const auto& active = at(skill, "active").resolved(), &enabled = at(skill, "enabled").resolved();
            if (at(skill, "name").resolved().text == wanted && active.kind == K::Boolean && active.boolean && enabled.kind == K::Boolean && enabled.boolean)
                result = std::max(result, at(skill, "level").numeric());
        }
    }
    return result;
}

SkillSummary skillSummary(const Value& crew_value, const Value& crew_ko) {
    SkillSummary s;
    s.sixth_sense = livingSkillCount(crew_value, "commander_sixthsense", crew_ko) > 0;
    s.expert = livingSkillCount(crew_value, "commander_expert", crew_ko) > 0;
    s.deadeye = livingSkillCount(crew_value, "gunner_sniper", crew_ko) > 0;
    s.controlled_impact = livingSkillCount(crew_value, "driver_rammingmaster", crew_ko) > 0;
    s.designated_target = livingSkillCount(crew_value, "gunner_rancorous", crew_ko) > 0;
    s.last_effort = livingSkillCount(crew_value, "radioman_lasteffort", crew_ko) > 0;
    s.intuition_chances = livingSkillCount(crew_value, "loader_intuition", crew_ko);
    return s;
}

double boosterSkillValue(const Value& snapshot, const std::string& name, double fallback) {
    const auto& booster = at(snapshot, "battle_booster").resolved();
    if (!booster.truth()) return fallback;
    const auto& overrides = at(booster, "skill_overrides").resolved();
    if (overrides.kind != K::Object || !has(overrides, name.c_str())) return fallback;
    return at(overrides, name).numeric(fallback);
}

std::optional<Value> canonicalEffectiveParams(const Value& value) {
    const auto& v = value.resolved();
    if (v.kind != K::Object) return {};
    Keys keys = keysOf(v);
    keys.erase("battle_booster");
    const Keys top{"version", "loadout", "physics", "spotting", "ramming", "ammo", "camouflage", "skills", "crew", "gun"};
    const Keys with_equipment = join(top, {"equipment"}), with_critical = join(top, {"equipment", "critical"});
    if (keys != top && keys != with_equipment && keys != with_critical) return {};
    auto version = exactInt(at(v, "version"), 1, 1);
    if (!version || *version != EFFECTIVE_PARAMS_SCHEMA_VERSION) return {};
    auto loadout_value = loadout(at(v, "loadout"));
    auto physics_value = physics(at(v, "physics"));
    auto spotting_value = spotting(at(v, "spotting"));
    auto ramming_value = ramming(at(v, "ramming"));
    auto ammo_value = ammo(at(v, "ammo"));
    auto camouflage_value = camouflage(at(v, "camouflage"));
    auto skills_value = skills(at(v, "skills"));
    auto crew_value = crew(at(v, "crew"));
    auto gun_value = gun(at(v, "gun"));
    auto equipment_value = has(v, "equipment") ? equipment(at(v, "equipment")) : std::optional<Value>(array());
    std::optional<Value> critical_value;
    if (has(v, "critical")) {
        critical_value = critical(at(v, "critical"));
        if (!critical_value) return {};
    }
    if (!loadout_value || !physics_value || !spotting_value || !ramming_value || !ammo_value || !camouflage_value || !skills_value || !crew_value ||
        !gun_value || !equipment_value)
        return {};
    std::optional<Value> booster;
    if (has(v, "battle_booster")) {
        booster = battleBooster(at(v, "battle_booster"));
        if (!booster) return {};
    }
    if (critical_value && has(*critical_value, "crew_roster")) {
        const auto& roster = at(*critical_value, "crew_roster");
        const auto& crew_roster = at(at(*crew_value, "dynamic_spotting"), "crew");
        if (!(roster == crew_roster)) return {};
    }
    SkillSummary expected;
    double controlled_impact = 0.;
    try {
        expected = skillSummary(*crew_value);
        controlled_impact = livingSkillLevel(*crew_value, "driver_rammingmaster");
    } catch (const Invalid&) {
        return {};
    }
    const auto& s = *skills_value;
    if (at(s, "sixth_sense").resolved().boolean != expected.sixth_sense || at(s, "expert").resolved().boolean != expected.expert ||
        at(s, "deadeye").resolved().boolean != expected.deadeye || at(s, "controlled_impact").resolved().boolean != expected.controlled_impact ||
        at(s, "designated_target").resolved().boolean != expected.designated_target || at(s, "last_effort").resolved().boolean != expected.last_effort ||
        at(s, "intuition_chances").resolved().integer != expected.intuition_chances)
        return {};
    if (at(*loadout_value, "has_sixth_sense").resolved().boolean != expected.sixth_sense) return {};
    if (std::fabs(at(*ramming_value, "ramming_bonus").numeric() - controlled_impact * 0.0015) > 0.0000001) return {};
    for (const auto& shot : at(*gun_value, "shots").resolved().array)
        if (at(at(shot, "source_shot"), "deadeye").resolved().boolean != expected.deadeye) return {};
    Value out = object();
    out.object["version"] = integer(EFFECTIVE_PARAMS_SCHEMA_VERSION);
    out.object["loadout"] = std::move(*loadout_value);
    out.object["physics"] = std::move(*physics_value);
    out.object["spotting"] = std::move(*spotting_value);
    out.object["ramming"] = std::move(*ramming_value);
    out.object["ammo"] = std::move(*ammo_value);
    out.object["camouflage"] = std::move(*camouflage_value);
    out.object["skills"] = std::move(*skills_value);
    out.object["crew"] = std::move(*crew_value);
    out.object["gun"] = std::move(*gun_value);
    out.object["equipment"] = std::move(*equipment_value);
    if (critical_value) out.object["critical"] = std::move(*critical_value);
    if (booster) out.object["battle_booster"] = std::move(*booster);
    return out;
}
}
}
