#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
// The worker link's outgoing messages: each pinned (144619f6) worker LAN client sender's checks and payload.
// A Python refusal (False/None) and a Python exception both become nullopt.
#include "stock_native_battle_link_internal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <vector>

namespace native_battle {
namespace link {
namespace {
using Kind = Value::Kind;
using Vec = std::array<double, 3>;
using Names = std::set<std::string>;
struct Refused {};
constexpr std::int64_t MAX_ID = 2147483647, MAX_MOTION_US = 10000000000000000LL, MAX_TIME_MS = 20000, MAX_BURST = 64;
constexpr double PI = 3.141592653589793, MAX_ORIGIN = 5000., MAX_VELOCITY = 3000., MAX_DISTANCE = 10000.,
                 MAX_PIERCING_LOSS = 100000., MAX_DEVICE_HP = 1000000000.;
constexpr std::size_t MAX_BATCH = 30, MAX_DESTRUCTIBLES = 64, MAX_TURRETS = 12;

void need(bool ok) { if (!ok) throw Refused(); }

Value boolV(bool v) { Value o; o.kind = Kind::Boolean; o.boolean = v; return o; }
Value intV(std::int64_t v) { Value o; o.kind = Kind::Integer; o.integer = v; return o; }
Value numV(double v) { Value o; o.kind = Kind::Number; o.number = v; return o; }
Value textV(std::string v) { Value o; o.kind = Kind::Text; o.text = std::move(v); return o; }
Value listV(std::vector<Value> v = {}) { Value o; o.kind = Kind::Array; o.array = std::move(v); return o; }
Value dictV() { Value o; o.kind = Kind::Object; return o; }
Value vecV(const Vec& v) { return listV({numV(v[0]), numV(v[1]), numV(v[2])}); }
Value optV(const std::optional<std::int64_t>& v) { return v ? intV(*v) : Value(); }

// Fields keep their stored (possibly Shared) Value so pass-through subtrees are not deep-copied twice.
const Value* field(const Value& v, const char* name) {
    const Value& r = v.resolved();
    if (r.kind != Kind::Object) return nullptr;
    auto i = r.object.find(name);
    return i == r.object.end() ? nullptr : &i->second;
}
const Value& none() { static const Value value; return value; }
// dict.get / a keyword parameter defaulting to None.
const Value& get(const Value& v, const char* name) { const Value* f = field(v, name); return f ? *f : none(); }
// A positional parameter without a default: a missing one was a TypeError.
const Value& required(const Value& m, const char* name) { const Value* f = field(m, name); need(f != nullptr); return *f; }
bool isNone(const Value& v) { return v.resolved().kind == Kind::Null; }
bool isDict(const Value& v) { return v.resolved().kind == Kind::Object; }
bool isList(const Value& v) { return v.resolved().kind == Kind::Array; }
bool isBool(const Value& v) { return v.resolved().kind == Kind::Boolean; }
bool isTrue(const Value& v) { return isBool(v) && v.resolved().boolean; }
bool is(const Value& v, const char* s) { const Value& r = v.resolved(); return r.kind == Kind::Text && r.text == s; }
bool oneOf(const Value& v, std::initializer_list<const char*> options) {
    for (const char* s : options) if (is(v, s)) return true;
    return false;
}
const std::vector<Value>& items(const Value& v) { need(isList(v)); return v.resolved().array; }
Names keys(const Value& v) {
    Names out;
    for (const auto& entry : v.resolved().object) out.insert(entry.first);
    return out;
}
bool keysAre(const Value& v, const Names& names) { return isDict(v) && keys(v) == names; }
// Python iteration of a list, a dict (its keys) or text (its characters).
std::vector<Value> elements(const Value& raw) {
    const Value& v = raw.resolved();
    std::vector<Value> out;
    if (v.kind == Kind::Array) return v.array;
    if (v.kind == Kind::Object) {
        for (const auto& entry : v.object) out.push_back(textV(entry.first));
    } else if (v.kind == Kind::Text) {
        for (std::size_t i = 0; i < v.text.size();) {
            std::size_t n = 1;
            while (i + n < v.text.size() && (static_cast<unsigned char>(v.text[i + n]) & 0xC0) == 0x80) ++n;
            out.push_back(textV(v.text.substr(i, n)));
            i += n;
        }
    } else {
        need(false);
    }
    return out;
}
// list(value or ())[:limit]
Value prefix(const Value& v, std::size_t limit) {
    Value out = listV();
    if (!v.truth()) return out;
    if (isList(v)) {
        const auto& all = v.resolved().array;
        out.array.assign(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(std::min(limit, all.size())));
    } else {
        out.array = elements(v);
        if (out.array.size() > limit) out.array.resize(limit);
    }
    return out;
}

// Integers: `big` marks a wire u64 above int64 (its bits in v).
struct Int { std::int64_t v = 0; bool big = false; };
Value intV(Int i) { Value o = intV(i.v); o.unsigned_integer = i.big; return o; }
bool negative(Int i) { return !i.big && i.v < 0; }
bool intOk(const Value& raw, std::int64_t lo, std::int64_t hi) {
    const Value& v = raw.resolved();
    return v.kind == Kind::Integer && !(v.unsigned_integer && v.integer < 0) && v.integer >= lo && v.integer <= hi;
}
// _projectile_int_range: a Python int (never bool or float) inside [lo, hi].
std::int64_t intIn(const Value& v, std::int64_t lo, std::int64_t hi) { need(intOk(v, lo, hi)); return v.resolved().integer; }
Int integral(double x) {
    need(std::isfinite(x) && std::floor(x) == x);
    if (x >= -9223372036854775808. && x < 9223372036854775808.) return Int{static_cast<std::int64_t>(x), false};
    need(x >= 0. && x < 18446744073709551616.);
    return Int{static_cast<std::int64_t>(static_cast<std::uint64_t>(x)), true};
}
// _exact_int: an int, or an integral float (numeric text is not produced natively and is refused).
Int exactInt(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind == Kind::Integer) return Int{v.integer, v.unsigned_integer && v.integer < 0};
    need(v.kind == Kind::Number);
    return integral(v.number);
}
// int(value): truncates a float, maps a bool to 0/1.
Int pyInt(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind == Kind::Boolean) return Int{v.boolean ? 1 : 0, false};
    if (v.kind == Kind::Number) { need(std::isfinite(v.number)); return integral(std::trunc(v.number)); }
    return exactInt(v);
}
std::optional<double> real(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind == Kind::Number) return v.number;
    if (v.kind != Kind::Integer) return std::nullopt;
    return v.unsigned_integer && v.integer < 0 ? static_cast<double>(static_cast<std::uint64_t>(v.integer))
                                               : static_cast<double>(v.integer);
}
// _projectile_float_range: float(value) of a Python int or float (never bool), finite, inside [lo, hi].
double floatIn(const Value& v, double lo, double hi) {
    const auto f = real(v);
    need(f && std::isfinite(*f) && *f >= lo && *f <= hi);
    return *f;
}
// _finite_float(value, 0.0)
double finiteFloat(const Value& raw) {
    const Value& v = raw.resolved();
    const double f = v.kind == Kind::Boolean ? (v.boolean ? 1. : 0.) : real(v).value_or(0.);
    return std::isfinite(f) ? f : 0.;
}
// _safe_text: str(value)[:limit] in characters.
std::string safeText(const Value& raw, const char* fallback, std::size_t limit) {
    const Value& v = raw.resolved();
    std::string s;
    char digits[64];
    switch (v.kind) {
    case Kind::Null: s = fallback; break;
    case Kind::Text: s = v.text; break;
    case Kind::Boolean: s = v.boolean ? "True" : "False"; break;
    case Kind::Integer:
        if (v.unsigned_integer && v.integer < 0) snprintf(digits, sizeof digits, "%llu", static_cast<unsigned long long>(v.integer));
        else snprintf(digits, sizeof digits, "%lld", static_cast<long long>(v.integer));
        s = digits;
        break;
    case Kind::Number:
        // Python 2.7 str(float): twelve significant digits, integral values keep ".0".
        if (std::isnan(v.number)) s = "nan";
        else if (std::isinf(v.number)) s = v.number < 0. ? "-inf" : "inf";
        else {
            snprintf(digits, sizeof digits, "%.12g", v.number);
            s = digits;
            if (s.find_first_of(".e") == std::string::npos) s += ".0";
        }
        break;
    default: need(false);
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && count++ == limit) return s.substr(0, i);
    return s;
}

// Python 2.7 round(x, 6): the exact binary value to six decimals, an exact tie away from zero.
double round6(double x) {
    const double k = std::ldexp(x, 7);
    if (std::fabs(k) < 1099511627776. && std::floor(k) == k && std::fmod(k, 2.) != 0.) {
        const double r = static_cast<double>((static_cast<std::uint64_t>(std::fabs(k)) * 15625u + 1u) / 2u) / 1e6;
        return k < 0. ? -r : r;
    }
    char digits[400];
    snprintf(digits, sizeof digits, "%.6f", x);
    return std::strtod(digits, nullptr);
}
// stun_mechanics.shell_component / canonical_factors.
Value shellStun(const Value& v) {
    static const Names names{"stunRadius", "stunDuration", "stunFactor", "guaranteedStunDuration", "damageDurationCoeff",
                             "guaranteedStunEffect", "damageEffectCoeff"};
    need(keysAre(v, names));
    Value out = dictV();
    for (const auto& name : names)
        out.object[name] = numV(round6(floatIn(get(v, name.c_str()), 0., name == "stunRadius" ? 100. : name == "stunDuration" ? 900. : 1.)));
    return out;
}
Value stunFactors(const Value& v) {
    static const Names names{"mobility", "traverse", "turret_speed", "vision", "speed", "reload", "aim_time",
                             "bloom_move", "bloom_rotation", "bloom_turret", "dispersion"};
    static const Names increasing{"reload", "aim_time", "bloom_move", "bloom_rotation", "bloom_turret", "dispersion"};
    need(keysAre(v, names));
    Value out = dictV();
    for (const auto& name : names) {
        const bool up = increasing.count(name) != 0;
        out.object[name] = numV(round6(floatIn(get(v, name.c_str()), up ? 1. : 0., up ? 100. : 1.)));
    }
    return out;
}

Vec vector3(const Value& v, const Vec& lo, const Vec& hi) {
    const auto& parts = items(v);
    need(parts.size() == 3);
    return {floatIn(parts[0], lo[0], hi[0]), floatIn(parts[1], lo[1], hi[1]), floatIn(parts[2], lo[2], hi[2])};
}
Vec symmetric(const Value& v, double bound) { return vector3(v, {-bound, -bound, -bound}, {bound, bound, bound}); }
const Vec WORLD_LO{-5000., -1000., -5000.}, WORLD_HI{5000., 3000., 5000.};
Vec world(const Value& v) { return vector3(v, WORLD_LO, WORLD_HI); }
Vec launchVelocity(const Value& v) {
    const Vec r = symmetric(v, MAX_VELOCITY);
    const double squared = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
    need(squared > 0.000001 && squared <= MAX_VELOCITY * MAX_VELOCITY);
    return r;
}

// _strict_projectile_source_shot plus the launch fields _projectile_source_shot_matches_launch compares.
struct Shot { Value wire; double speed = 0., gravity = 0., max_distance = 0., radius = 0.; bool he = false; };
Shot sourceShot(const Value& v) {
    static const Names base{"kind", "caliber", "damage", "explosionRadius"};
    static const Names factors{"explosionDamageFactor", "explosionDamageAbsorptionFactor", "explosionEdgeDamageFactor"};
    Names with_factors = base;
    with_factors.insert(factors.begin(), factors.end());
    need(keysAre(v, {"speed", "gravity", "maxDistance", "piercingPower", "deadeye", "shell"}));
    const Value& shell = get(v, "shell");
    need(isDict(shell));
    Names fields = keys(shell);
    const bool has_stun = fields.erase("stun") != 0;
    need(fields == base || fields == with_factors);
    const Value &kind = get(shell, "kind"), &piercing = get(v, "piercingPower"), &damage = get(shell, "damage");
    need(oneOf(kind, {"HOLLOW_CHARGE", "HIGH_EXPLOSIVE", "ARMOR_PIERCING", "ARMOR_PIERCING_HE", "ARMOR_PIERCING_CR"}) &&
         isBool(get(v, "deadeye")) && items(piercing).size() == 2 && items(damage).size() == 2);
    Shot shot;
    shot.speed = floatIn(get(v, "speed"), 0.000001, MAX_VELOCITY);
    shot.gravity = floatIn(get(v, "gravity"), 0.000001, 500.);
    shot.max_distance = floatIn(get(v, "maxDistance"), 0.000001, MAX_DISTANCE);
    shot.radius = floatIn(get(shell, "explosionRadius"), 0., 100.);
    shot.he = is(kind, "HIGH_EXPLOSIVE");
    Value out_shell = dictV();
    out_shell.object["kind"] = textV(kind.resolved().text);
    out_shell.object["caliber"] = numV(floatIn(get(shell, "caliber"), 0.000001, 1000.));
    out_shell.object["damage"] = listV({numV(floatIn(items(damage)[0], 0.000001, 10000.)), numV(floatIn(items(damage)[1], 0., MAX_DEVICE_HP))});
    out_shell.object["explosionRadius"] = numV(shot.radius);
    if (fields == with_factors) {
        out_shell.object["explosionDamageFactor"] = numV(floatIn(get(shell, "explosionDamageFactor"), 0.000001, 10000.));
        out_shell.object["explosionDamageAbsorptionFactor"] = numV(floatIn(get(shell, "explosionDamageAbsorptionFactor"), 0.000001, 10000.));
        out_shell.object["explosionEdgeDamageFactor"] = numV(floatIn(get(shell, "explosionEdgeDamageFactor"), 0.000001, 1.));
    }
    if (has_stun) {
        need(shot.he && !isNone(get(shell, "stun")));
        out_shell.object["stun"] = shellStun(get(shell, "stun"));
    }
    shot.wire = dictV();
    auto& o = shot.wire.object;
    o["speed"] = numV(shot.speed);
    o["gravity"] = numV(shot.gravity);
    o["maxDistance"] = numV(shot.max_distance);
    o["piercingPower"] = listV({numV(floatIn(items(piercing)[0], 0., 10000.)), numV(floatIn(items(piercing)[1], 0., 10000.))});
    o["deadeye"] = boolV(get(v, "deadeye").resolved().boolean);
    o["shell"] = std::move(out_shell);
    return shot;
}
bool closeTo(double left, double right) { return std::fabs(left - right) <= std::max(0.001, std::fabs(right) * 0.000001); }

// _strict_critical_delta
Value criticalDelta(const Value& v) {
    static const Names devices{"engineHealth", "ammoBayHealth", "fuelTankHealth", "radioHealth", "leftTrackHealth",
                               "rightTrackHealth", "gunHealth", "turretRotatorHealth", "surveyingDeviceHealth"};
    static const Names crew{"commander", "driver", "gunner", "gunner1", "gunner2", "loader", "loader1", "loader2",
                            "radioman", "radioman1", "radioman2"};
    need(keysAre(v, {"devices", "crew_ko", "ignite"}));
    const auto &raw_devices = items(get(v, "devices")), &raw_crew = items(get(v, "crew_ko"));
    need(raw_devices.size() <= 16 && raw_crew.size() <= 11 && isBool(get(v, "ignite")));
    Value out_devices = listV();
    Names seen;
    for (const auto& raw : raw_devices) {
        need(keysAre(raw, {"name", "hp_loss"}));
        const std::string name = safeText(get(raw, "name"), "", 40);
        const double loss = floatIn(get(raw, "hp_loss"), 0., MAX_DEVICE_HP);
        need(devices.count(name) && seen.insert(name).second && loss > 0.);
        Value row = dictV();
        row.object["name"] = textV(name);
        row.object["hp_loss"] = numV(loss);
        out_devices.array.push_back(std::move(row));
    }
    Names knocked;
    for (const auto& raw : raw_crew) {
        const std::string name = safeText(raw, "", 40);
        need(crew.count(name) && knocked.insert(name).second);
    }
    Value out_crew = listV();
    for (const auto& name : knocked) out_crew.array.push_back(textV(name));
    Value out = dictV();
    out.object["devices"] = std::move(out_devices);
    out.object["crew_ko"] = std::move(out_crew);
    out.object["ignite"] = boolV(get(v, "ignite").resolved().boolean);
    return out;
}
double axis(const Value& v, const char* name, int index) {
    return floatIn(get(v, name), index == 1 ? -1000. : -MAX_ORIGIN, index == 1 ? 3000. : MAX_ORIGIN);
}
// _strict_projectile_effect
Value effect(const Value& v) {
    static const Names base{"target_kind", "target_id", "damage", "shot_result", "x", "y", "z"};
    static const Names critical{"critical", "critical_target_base_revision", "critical_target_ack_seq", "hull_damage", "critical_delta"};
    static const Names pose{"target_x", "target_y", "target_z"};
    need(isDict(v));
    const Names have = keys(v);
    Names stun{"stun_end_server_time_ms"};
    for (const char* name : {"stun_factors", "stun_duration_ms"}) if (have.count(name)) stun.insert(name);
    Names allowed = base;
    for (const Names* group : std::initializer_list<const Names*>{&critical, &stun, &pose}) allowed.insert(group->begin(), group->end());
    allowed.insert({"damage_sticker", "potential_damage", "structural_armor_hit", "high_explosive"});
    for (const auto& name : base) need(have.count(name) != 0);
    for (const auto& name : have) need(allowed.count(name) != 0);
    const bool has_critical = have.count("critical") != 0, has_stun = have.count("stun_end_server_time_ms") != 0;
    const bool has_pose = have.count("target_x") || have.count("target_y") || have.count("target_z");
    const bool has_sticker = have.count("damage_sticker") != 0, has_potential = have.count("potential_damage") != 0;
    Names expected = base;
    if (has_critical) expected.insert(critical.begin(), critical.end());
    if (has_stun) expected.insert(stun.begin(), stun.end());
    if (has_pose) expected.insert(pose.begin(), pose.end());
    for (const char* name : {"damage_sticker", "potential_damage", "structural_armor_hit", "high_explosive"})
        if (have.count(name)) expected.insert(name);
    const Value& kind = get(v, "target_kind");
    Value out = dictV();
    auto& o = out.object;
    o["target_id"] = intV(intIn(get(v, "target_id"), 1, MAX_ID));
    o["damage"] = intV(intIn(get(v, "damage"), 0, 5000));
    o["shot_result"] = intV(intIn(get(v, "shot_result"), 0, 2));
    o["x"] = numV(axis(v, "x", 0));
    o["y"] = numV(axis(v, "y", 1));
    o["z"] = numV(axis(v, "z", 2));
    need(oneOf(kind, {"player", "bot"}) && !(has_pose && has_sticker) && !(has_pose && has_potential) && have == expected);
    o["target_kind"] = textV(kind.resolved().text);
    if (has_critical) {
        need(isDict(get(v, "critical")));
        o["critical"] = get(v, "critical");
        o["critical_target_base_revision"] = intV(intIn(get(v, "critical_target_base_revision"), 0, MAX_ID));
        o["critical_target_ack_seq"] = intV(intIn(get(v, "critical_target_ack_seq"), 0, MAX_ID));
        o["hull_damage"] = intV(intIn(get(v, "hull_damage"), 0, 5000));
        o["critical_delta"] = criticalDelta(get(v, "critical_delta"));
    }
    if (has_stun) {
        const std::int64_t end = intIn(get(v, "stun_end_server_time_ms"), 1, MAX_ID);
        o["stun_end_server_time_ms"] = intV(end);
        if (have.count("stun_duration_ms")) o["stun_duration_ms"] = intV(intIn(get(v, "stun_duration_ms"), 0, end));
        if (have.count("stun_factors")) o["stun_factors"] = stunFactors(get(v, "stun_factors"));
    }
    if (has_sticker) {
        // 0 .. 2**64 - 1: any int that is not negative.
        const Value& sticker = get(v, "damage_sticker");
        need(sticker.resolved().kind == Kind::Integer);
        const Int value = exactInt(sticker);
        need(!negative(value));
        o["damage_sticker"] = intV(value);
    }
    if (has_potential) o["potential_damage"] = intV(intIn(get(v, "potential_damage"), 0, 5000));
    if (have.count("structural_armor_hit")) o["structural_armor_hit"] = boolV(isTrue(get(v, "structural_armor_hit")));
    if (have.count("high_explosive")) o["high_explosive"] = boolV(isTrue(get(v, "high_explosive")));
    if (has_pose) {
        o["target_x"] = numV(axis(v, "target_x", 0));
        o["target_y"] = numV(axis(v, "target_y", 1));
        o["target_z"] = numV(axis(v, "target_z", 2));
    }
    return out;
}
// _strict_projectile_wreck_hit
Value wreckHit(const Value& v) {
    need(keysAre(v, {"target_kind", "target_id"}) && oneOf(get(v, "target_kind"), {"player", "bot"}));
    Value out = dictV();
    out.object["target_kind"] = textV(get(v, "target_kind").resolved().text);
    out.object["target_id"] = intV(intIn(get(v, "target_id"), 1, MAX_ID));
    return out;
}
// _strict_projectile_destructible
Value shotDestructible(const Value& v) {
    static const Names base{"destructible_kind", "chunk_id", "item_index", "x", "y", "z", "fall_yaw", "speed", "is_shot"};
    need(isDict(v));
    Names have = keys(v);
    for (const auto& name : base) need(have.count(name) != 0);
    have.erase("mat_kind");
    need(have.size() == base.size());
    const Value& kind = get(v, "destructible_kind");
    need(oneOf(kind, {"tree", "column", "fragile", "module"}) && isTrue(get(v, "is_shot")));
    Value out = dictV();
    auto& o = out.object;
    o["destructible_kind"] = textV(kind.resolved().text);
    o["chunk_id"] = intV(intIn(get(v, "chunk_id"), 0, 4294967295LL));
    o["item_index"] = intV(intIn(get(v, "item_index"), 0, 1048575));
    o["x"] = numV(floatIn(get(v, "x"), WORLD_LO[0], WORLD_HI[0]));
    o["y"] = numV(floatIn(get(v, "y"), WORLD_LO[1], WORLD_HI[1]));
    o["z"] = numV(floatIn(get(v, "z"), WORLD_LO[2], WORLD_HI[2]));
    o["fall_yaw"] = numV(floatIn(get(v, "fall_yaw"), -PI * 4., PI * 4.));
    o["speed"] = numV(floatIn(get(v, "speed"), -200., 200.));
    o["is_shot"] = boolV(true);
    if (field(v, "mat_kind")) o["mat_kind"] = intV(intIn(get(v, "mat_kind"), 0, 65535));
    need(!is(kind, "module") || o.count("mat_kind"));
    return out;
}
Value shotDestructibles(const Value& raw, std::size_t& count) {
    Value out = listV();
    for (const auto& item : items(raw)) out.array.push_back(shotDestructible(item));
    count += out.array.size();
    return out;
}
std::string projectileId(const Value& raw) {
    const Value& v = raw.resolved();
    need(v.kind == Kind::Text && !v.text.empty() && v.text.size() <= 96);
    for (char c : v.text) {
        const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        need(alnum || c == ':' || c == '_' || c == '-');
    }
    return v.text;
}

bool botAuthority(const SenderState& s) {
    return s.ready && (s.phase == "loading" || s.phase == "battle") && s.bot_authority_id && *s.bot_authority_id == -1;
}
bool epochIs(const SenderState& s, std::int64_t epoch) { return s.authority_epoch && *s.authority_epoch == epoch; }
// _owns_projectile_id and the launch proof rule as the Bot authority (a proof is a visible client's).
// Without authority the sender needs a player launch proof for the worker's own id, which no worker shot has.
void ownProjectile(const SenderState& s, const Value& id, const Value& launch_proof) {
    const std::string text = projectileId(id);
    need(botAuthority(s) && isNone(launch_proof));
    std::vector<std::string> parts;
    for (std::size_t start = 0;;) {
        const std::size_t end = text.find(':', start);
        parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    need(parts.size() == 4 && parts[0] == (s.round_id ? std::to_string(*s.round_id) : std::string("None")) && parts[1] == "b");
}
Value message(const char* type, const SenderState& s) {
    Value w = dictV();
    w.object["type"] = textV(type);
    w.object["round_id"] = optV(s.round_id);
    return w;
}

// turret_obstacle_schema.normalize_proposal; a Refused row is dropped.
double turretNumber(const Value& v, double lo, double hi) { return floatIn(v, lo, hi); }
Value turretInteger(const Value& v, std::int64_t hi) {
    const double n = floatIn(v, 0., static_cast<double>(hi));
    need(std::trunc(n) == n);
    return intV(static_cast<std::int64_t>(n));
}
const Value& item(const Value& v, const char* name) { return required(v, name); }
Value tvec(const Value& v, double bound) { return vecV(symmetric(v, bound)); }
long long actorNumber(const std::string& digits) {
    // int(text): optional surrounding whitespace and sign, then decimal digits.
    std::size_t i = digits.find_first_not_of(" \t\n\r\f\v"), end = digits.find_last_not_of(" \t\n\r\f\v");
    need(i != std::string::npos);
    bool minus = false;
    if (digits[i] == '+' || digits[i] == '-') minus = digits[i++] == '-';
    need(i <= end);
    long long n = 0;
    for (; i <= end; ++i) {
        need(digits[i] >= '0' && digits[i] <= '9');
        n = std::min(n * 10 + (digits[i] - '0'), 100000000000LL);
    }
    return minus ? -n : n;
}
std::pair<std::string, long long> actorKey(const Value& raw) {
    need(raw.resolved().kind == Kind::Text);
    const std::string& text = raw.resolved().text;
    const std::size_t colon = text.find(':');
    need(colon != std::string::npos && text.find(':', colon + 1) == std::string::npos);
    const std::string kind = text.substr(0, colon);
    need(kind == "bot" || kind == "player");
    const long long id = actorNumber(text.substr(colon + 1));
    need(id >= 1 && id <= MAX_ID);
    return {kind, id};
}
// turret_contact_ledger.normalize(...).values(), in input order.
Value contactAcks(const Value& rows) {
    const auto& all = items(rows);
    need(all.size() <= 30);
    Value out = listV();
    Names seen;
    for (const auto& row : all) {
        const auto& r = items(row);
        need(r.size() == 8);
        const auto actor = actorKey(r[0]);
        const std::string key = actor.first + ":" + std::to_string(actor.second);
        const Value& seq = r[1].resolved();
        need(seq.kind == Kind::Integer || seq.kind == Kind::Number);
        const Int sequence = exactInt(seq);
        need(!sequence.big && sequence.v >= 1 && sequence.v <= MAX_ID && seen.insert(key).second);
        Value checkpoint = listV({textV(key), intV(sequence.v)});
        for (std::size_t i = 2; i < 8; ++i) {
            const Value& part = r[i].resolved();
            need(part.kind == Kind::Integer || part.kind == Kind::Number);
            const double f = *real(part);
            need(std::isfinite(f) && std::fabs(f) <= 1e12);
            checkpoint.array.push_back(numV(f));
        }
        out.array.push_back(std::move(checkpoint));
    }
    return out;
}
Value turretFlight(const Value& v) {
    constexpr double FLIGHT = 8.0, ENERGY = 1.5 * MAX_VELOCITY * MAX_VELOCITY;
    need(isDict(v));
    const auto& segments = items(item(v, "segments"));
    need(!segments.empty() && segments.size() <= 4);
    Value parts = listV();
    for (const auto& segment : segments) {
        need(isDict(segment));
        const double start = turretNumber(item(segment, "start"), 0., FLIGHT), duration = turretNumber(item(segment, "duration"), 0., FLIGHT);
        need(start + duration <= FLIGHT + 1e-9);
        Value part = dictV();
        part.object["origin"] = tvec(item(segment, "origin"), MAX_ORIGIN);
        part.object["velocity"] = tvec(item(segment, "velocity"), MAX_VELOCITY);
        part.object["start"] = numV(start);
        part.object["duration"] = numV(duration);
        parts.array.push_back(std::move(part));
    }
    const Value &landed = item(v, "landed"), &contact = item(v, "contact");
    need(isBool(landed) && landed.resolved().boolean == !isNone(contact));
    Value out = dictV();
    auto& o = out.object;
    o["origin"] = tvec(item(v, "origin"), MAX_ORIGIN);
    o["velocity"] = tvec(item(v, "velocity"), MAX_VELOCITY);
    o["segments"] = std::move(parts);
    o["duration"] = numV(turretNumber(item(v, "duration"), 0., FLIGHT));
    o["contact"] = isNone(contact) ? Value() : tvec(contact, MAX_ORIGIN);
    o["rest"] = tvec(item(v, "rest"), MAX_ORIGIN);
    o["impact_velocity"] = tvec(item(v, "impact_velocity"), MAX_VELOCITY);
    o["energy"] = numV(turretNumber(item(v, "energy"), 0., ENERGY));
    o["landed"] = boolV(landed.resolved().boolean);
    if (field(v, "rest_attitude")) o["rest_attitude"] = tvec(get(v, "rest_attitude"), 1000.);
    if (field(v, "body")) {
        const Value& body = get(v, "body");
        need(isDict(body) && isBool(get(body, "grounded")) && isBool(get(body, "sleeping")));
        Value frame = dictV();
        auto& f = frame.object;
        f["position"] = tvec(item(body, "position"), MAX_ORIGIN);
        f["attitude"] = tvec(item(body, "attitude"), 1000.);
        f["centre"] = tvec(item(body, "centre"), MAX_ORIGIN);
        f["velocity"] = tvec(item(body, "velocity"), MAX_VELOCITY);
        f["angular_velocity"] = tvec(item(body, "angular_velocity"), 1000.);
        f["grounded"] = boolV(get(body, "grounded").resolved().boolean);
        f["sleeping"] = boolV(get(body, "sleeping").resolved().boolean);
        f["impact_serial"] = turretInteger(item(body, "impact_serial"), MAX_ID);
        f["acks"] = contactAcks(item(body, "acks"));
        f["impact"] = Value();
        if (!isNone(get(body, "impact"))) {
            const Value& impact = get(body, "impact");
            Value hit = dictV();
            hit.object["point"] = tvec(item(impact, "point"), MAX_ORIGIN);
            hit.object["normal"] = tvec(item(impact, "normal"), 1.);
            hit.object["velocity"] = tvec(item(impact, "velocity"), MAX_VELOCITY);
            hit.object["energy"] = numV(turretNumber(item(impact, "energy"), 0., ENERGY));
            f["impact"] = std::move(hit);
        }
        o["body"] = std::move(frame);
    }
    return out;
}
std::optional<Value> turretProposal(const Value& v) {
    if (!isDict(v)) return std::nullopt;
    try {
        const Value& kind = item(v, "actor_kind");
        if (!oneOf(kind, {"bot", "player"})) return std::nullopt;
        Value out = dictV();
        auto& o = out.object;
        o["actor_kind"] = textV(kind.resolved().text);
        const Value id = turretInteger(item(v, "actor_id"), MAX_ID);
        need(id.integer >= 1);
        o["actor_id"] = id;
        o["flight"] = turretFlight(item(v, "flight"));
        o["attitude"] = tvec(item(v, "attitude"), 1000.);
        o["spin"] = tvec(item(v, "spin"), 1000.);
        if (field(v, "motion_seq")) {
            const Value seq = turretInteger(get(v, "motion_seq"), MAX_ID);
            need(seq.integer >= 1);
            o["motion_seq"] = seq;
            o["motion_time_ms"] = turretInteger(item(v, "motion_time_ms"), MAX_ID);
            const Value& support = get(v, "support_key");
            if (!isNone(support)) actorKey(support);
            o["support_key"] = support;
        }
        return out;
    } catch (const Refused&) {
        return std::nullopt;
    }
}

// Coalesce key text: equal exactly when the Python key tuples compare equal (True == 1 == 1.0).
void keyOf(std::string& out, const Value& raw) {
    const Value& v = raw.resolved();
    char digits[64];
    switch (v.kind) {
    case Kind::Null: out += 'n'; return;
    case Kind::Boolean: out += v.boolean ? "i1;" : "i0;"; return;
    case Kind::Integer:
        if (v.unsigned_integer && v.integer < 0) snprintf(digits, sizeof digits, "i%llu;", static_cast<unsigned long long>(v.integer));
        else snprintf(digits, sizeof digits, "i%lld;", static_cast<long long>(v.integer));
        out += digits;
        return;
    case Kind::Number:
        if (std::isfinite(v.number) && std::floor(v.number) == v.number && v.number >= -9223372036854775808. &&
            v.number < 18446744073709551616.) {
            const Int i = integral(v.number);
            keyOf(out, intV(i));
            return;
        }
        snprintf(digits, sizeof digits, "f%.17g;", v.number);
        out += digits;
        return;
    case Kind::Text: out += 's' + std::to_string(v.text.size()) + ':' + v.text; return;
    case Kind::Array:
        out += 'a' + std::to_string(v.array.size()) + '[';
        for (const auto& x : v.array) keyOf(out, x);
        out += ']';
        return;
    case Kind::Object:
        out += 'o' + std::to_string(v.object.size()) + '{';
        for (const auto& x : v.object) { keyOf(out, textV(x.first)); keyOf(out, x.second); }
        out += '}';
        return;
    case Kind::Shared: out += 'n'; return;
    }
}
// tuple(tuple(row) for row in rows)
Value rowsOf(const Value& rows) {
    Value out = listV();
    for (const auto& row : elements(rows)) out.array.push_back(listV(elements(row)));
    return out;
}
Value withDefault(const Value& v, const char* name) {
    const Value* f = field(v, name);
    return f ? listV(elements(*f)) : listV();
}

// ---------------------------------------------------------------------------------------------------- senders

Value hello() {
    Value w = dictV();
    w.object["type"] = textV("hello");
    w.object["protocol"] = intV(5);
    w.object["client_build"] = textV("wot-0.9.22.0.1-cn-1513");
    Value capabilities = listV();
    for (const char* name : {"state_transfer_v1", "projectile_ledger_v2", "ricochet_continuation_v1", "destructible_catalog_v5",
                             "lean_snapshot_manifest_v1", "ram_contact_ledger_v2", "human_ram_timeline_v1",
                             "player_projectile_owner_v1", "player_environment_v2", "effective_params_v1", "simulation_worker_v1"})
        capabilities.array.push_back(textV(name));
    w.object["capabilities"] = std::move(capabilities);
    w.object["role"] = textV("simulation_worker");
    return w;
}

Value battleReady(const SenderState& s, const Value& m) {
    need(s.ready && s.phase == "loading");
    Value w = message("battle_ready", s);
    const Value& bases = get(m, "bases");
    if (isDict(bases)) {
        Value teams = dictV();
        for (const char* team : {"1", "2"})
            if (!isNone(get(bases, team))) teams.object[team] = get(bases, team);
        w.object["bases"] = std::move(teams);
    }
    return w;
}

Value botManifest(const SenderState& s, const Value& m) {
    const Value& bots = required(m, "bots");
    need(botAuthority(s));
    Value w = message("bot_manifest", s);
    w.object["bots"] = prefix(bots, 30);
    const Value& profiles = get(m, "player_collision_profiles");
    if (!isNone(profiles)) w.object["player_collision_profiles"] = prefix(profiles, 64);
    return w;
}

// AuthorityWorkerLANClient.send_projected_bot_state with its coalesce key.
void botState(const SenderState& s, const Value& m, Outgoing& out) {
    const Value& rows = required(m, "rows");
    need(botAuthority(s));
    const Value &edge_time = get(m, "edge_sample_time_us"), &edge_revision = get(m, "edge_revision");
    const Int edge_us = exactInt(edge_time), revision = exactInt(edge_revision);
    need(!negative(edge_us) && (revision.big || revision.v >= 1));
    Value w = message("bot_state", s);
    w.object["rows"] = rows;
    const Value &sample = get(m, "sample_time_us"), &horizon = get(m, "source_batch_horizon_us");
    if (!isNone(sample)) {
        const Int t = exactInt(sample);
        need(!t.big && t.v >= 0 && t.v <= MAX_MOTION_US);
        w.object["sample_time_us"] = intV(t.v);
        const Int h = exactInt(horizon);
        need(!h.big && t.v <= h.v && h.v <= MAX_MOTION_US);
        w.object["source_batch_horizon_us"] = intV(h.v);
    } else {
        need(isNone(horizon));
    }
    const Value &armors = get(m, "human_ram_armors"), &push = get(m, "push_contacts"), &turrets = get(m, "detached_turrets");
    if (!isNone(armors)) w.object["human_ram_armors"] = armors;
    if (!isNone(push)) {
        need(isDict(push));
        w.object["push_contacts"] = push;
    }
    if (!isNone(turrets)) {
        Value proposals = listV();
        if (isList(turrets)) {
            const auto& all = turrets.resolved().array;
            for (std::size_t i = 0; i < all.size() && i < MAX_TURRETS; ++i)
                if (auto row = turretProposal(all[i])) proposals.array.push_back(std::move(*row));
        }
        w.object["detached_turrets"] = std::move(proposals);
        w.object["authority_epoch"] = optV(s.authority_epoch);
    }
    // (round_id, authority_epoch, edge_sample_time_us, edge_revision, human ram edge, detached_turrets, push edge)
    Value key = listV({optV(s.round_id), optV(s.authority_epoch), intV(edge_us), intV(revision)});
    if (isNone(armors)) key.array.emplace_back();
    else {
        Value edge = listV();
        for (const auto& row : elements(armors)) {
            need(isDict(row));
            edge.array.push_back(listV({required(row, "seq"), required(row, "first_id"), required(row, "second_id"),
                                        required(row, "available"), get(row, "armor_first"), get(row, "armor_second")}));
        }
        key.array.push_back(std::move(edge));
    }
    const Value* proposals = field(w, "detached_turrets");
    key.array.push_back(proposals ? *proposals : Value());
    if (isNone(push)) key.array.emplace_back();
    else {
        Value contacts = listV();
        for (const auto& row : field(push, "contacts") ? elements(get(push, "contacts")) : std::vector<Value>()) {
            need(isDict(row));
            Value projected = listV();
            for (const char* name : {"seq", "attacker_kind", "attacker_id", "target_kind", "target_id"}) projected.array.push_back(get(row, name));
            contacts.array.push_back(std::move(projected));
        }
        key.array.push_back(listV({std::move(contacts), withDefault(push, "hazards"),
                                   field(push, "falls") ? rowsOf(get(push, "falls")) : listV(),
                                   field(push, "critical_totals") ? rowsOf(get(push, "critical_totals")) : listV(),
                                   withDefault(push, "environment_fires")}));
    }
    std::string text;
    keyOf(text, key);
    out.wire = std::move(w);
    out.coalesce = std::move(text);
}

Value botObservation(const SenderState& s, const Value& m) {
    const Value& contacts = required(m, "contacts");
    need(botAuthority(s));
    Value w = message("bot_observation", s);
    w.object["player_vision_ranges"] = prefix(get(m, "player_vision_ranges"), 30);
    w.object["contacts"] = prefix(contacts, 64);
    w.object["affordances"] = prefix(get(m, "affordances"), 16);
    w.object["radio_links"] = prefix(get(m, "radio_links"), 30);
    return w;
}

// ram_tracks.names: sorted unique track device names.
Value trackNames(const Value& v) {
    Names names;
    if (v.truth()) {
        const auto& all = items(v);
        need(all.size() <= 2);
        for (const auto& name : all) {
            need(oneOf(name, {"leftTrackHealth", "rightTrackHealth"}));
            names.insert(name.resolved().text);
        }
    }
    Value out = listV();
    for (const auto& name : names) out.array.push_back(textV(name));
    return out;
}
Value botRam(const SenderState& s, const Value& m) {
    const Value &bot_id = required(m, "bot_id"), &target_kind = required(m, "target_kind"), &target_id = required(m, "target_id");
    const Value &ram_seq = required(m, "ram_seq"), &to_bot = required(m, "damage_to_bot"), &to_target = required(m, "damage_to_target");
    need(botAuthority(s) && oneOf(target_kind, {"bot", "human"}));
    Value w = message("bot_ram_report", s);
    auto& o = w.object;
    o["bot_id"] = intV(pyInt(bot_id));
    o["target_kind"] = textV(target_kind.resolved().text);
    o["target_id"] = intV(pyInt(target_id));
    const Int seq = pyInt(ram_seq);
    o["ram_seq"] = !seq.big && seq.v < 1 ? intV(1) : intV(seq);
    for (const auto& damage : {std::make_pair("damage_to_bot", &to_bot), std::make_pair("damage_to_target", &to_target)}) {
        const Int value = damage.second->truth() ? pyInt(*damage.second) : Int{};
        o[damage.first] = negative(value) ? intV(0) : intV(value);
    }
    const Value &contact_player = get(m, "ram_contact_player_id"), &contact_seq = get(m, "ram_contact_seq");
    if (!isNone(contact_player) && !isNone(contact_seq)) {
        o["ram_contact_player_id"] = intV(pyInt(contact_player));
        o["ram_contact_seq"] = intV(pyInt(contact_seq));
    }
    const Value& positions = get(m, "contact_positions");
    if (!isNone(positions)) o["contact_positions"] = listV(elements(positions));
    const Value& impact_speed = get(m, "track_impact_speed");
    if (!isNone(impact_speed)) {
        o["track_contacts_bot"] = trackNames(get(m, "track_contacts_bot"));
        o["track_contacts_target"] = trackNames(get(m, "track_contacts_target"));
        const Value& speed = impact_speed.resolved();
        need(speed.kind == Kind::Boolean || real(speed).has_value());
        o["track_impact_speed"] = numV(speed.kind == Kind::Boolean ? (speed.boolean ? 1. : 0.) : *real(speed));
    }
    return w;
}

Value rulesState(const SenderState& s, const Value& m) {
    const Value& bases = required(m, "bases");
    need(botAuthority(s));
    Value w = message("rules_state", s), rules = dictV();
    rules.object["bases"] = isDict(bases) ? bases : dictV();
    w.object["rules"] = std::move(rules);
    return w;
}

Value battleResult(const SenderState& s, const Value& m) {
    const Value &winner = required(m, "winner"), &reason = required(m, "reason"), &base_team = get(m, "base_team");
    need(botAuthority(s));
    Value w = message("battle_result", s);
    w.object["winner"] = intV(pyInt(winner));
    w.object["reason"] = textV(safeText(reason, "battle finished", 80));
    w.object["base_team"] = intV(base_team.truth() ? pyInt(base_team) : Int{});
    return w;
}

Value destructible(const SenderState& s, const Value& m) {
    const Value& event = required(m, "event");
    need(s.ready && s.phase == "battle" && botAuthority(s) && isDict(event));
    const std::string kind = safeText(get(event, "destructible_kind"), "", 16);
    need(kind == "tree" || kind == "column" || kind == "fragile" || kind == "module");
    const Int chunk = exactInt(get(event, "chunk_id")), index = exactInt(get(event, "item_index"));
    need(isBool(get(event, "is_shot")));
    Value w = message("destructible", s);
    auto& o = w.object;
    o["destructible_kind"] = textV(kind);
    o["chunk_id"] = intV(chunk);
    o["item_index"] = intV(index);
    for (const char* name : {"x", "y", "z", "fall_yaw", "speed"}) o[name] = numV(finiteFloat(get(event, name)));
    o["is_shot"] = boolV(get(event, "is_shot").resolved().boolean);
    if (!isNone(get(event, "mat_kind"))) o["mat_kind"] = intV(exactInt(get(event, "mat_kind")));
    return w;
}

Value playerDestructibleContactResult(const SenderState& s, const Value& m) {
    const Value &player = required(m, "player_id"), &contact_seq = required(m, "contact_seq"), &accepted = required(m, "accepted"),
                &token = required(m, "token");
    need(s.ready && s.phase == "battle" && botAuthority(s) && isBool(accepted) && isList(token));
    const auto& rows = token.resolved().array;
    need(!rows.empty() && rows.size() <= 64);
    Value parsed = listV();
    for (const auto& raw : rows) {
        const auto& r = items(raw);
        need(r.size() == 3);
        const Int chunk = exactInt(r[0]), index = exactInt(r[1]);
        need(!negative(chunk) && !negative(index));
        parsed.array.push_back(listV({intV(chunk), intV(index), isNone(r[2]) ? Value() : intV(exactInt(r[2]))}));
    }
    Value w = message("player_destructible_contact_result", s);
    w.object["player_id"] = intV(intIn(player, 1, MAX_ID));
    w.object["contact_seq"] = intV(intIn(contact_seq, 1, MAX_ID));
    w.object["accepted"] = boolV(accepted.resolved().boolean);
    w.object["token"] = std::move(parsed);
    return w;
}

Value playerEnvironment(const SenderState& s, const Value& m) {
    const Value &observations = required(m, "observations"), &sample_seq = required(m, "sample_seq");
    const char* capability = "player_environment_v2";
    const std::int64_t sequence = intIn(sample_seq, 1, MAX_ID);
    need(s.authority_epoch && *s.authority_epoch >= 0 && *s.authority_epoch <= MAX_ID);
    need(s.phase == "battle" && botAuthority(s) && s.capabilities.count(capability) && s.server_capabilities.count(capability) &&
         isList(observations) && observations.resolved().array.size() <= 30);
    static const Names plain{"player_id", "input_seq", "level"}, drowning{"player_id", "input_seq", "level", "drowning_critical"};
    Value rows = listV();
    std::set<std::int64_t> seen;
    for (const auto& raw : observations.resolved().array) {
        need(keysAre(raw, plain) || keysAre(raw, drowning));
        const std::int64_t player = intIn(get(raw, "player_id"), 1, MAX_ID), input = intIn(get(raw, "input_seq"), 0, MAX_ID);
        const std::int64_t level = intIn(get(raw, "level"), 0, 2);
        need(seen.insert(player).second);
        Value row = dictV();
        row.object["player_id"] = intV(player);
        row.object["input_seq"] = intV(input);
        row.object["level"] = intV(level);
        if (field(raw, "drowning_critical")) {
            need(level == 2 && isDict(get(raw, "drowning_critical")));
            row.object["drowning_critical"] = get(raw, "drowning_critical");
        }
        rows.array.push_back(std::move(row));
    }
    Value w = message("player_environment", s);
    w.object["authority_epoch"] = intV(*s.authority_epoch);
    w.object["sample_seq"] = intV(sequence);
    w.object["observations"] = std::move(rows);
    return w;
}

Value simulationProgress(const SenderState& s, const Value& m) {
    const std::int64_t sequence = intIn(required(m, "frame_seq"), 1, MAX_ID);
    need(botAuthority(s) && s.round_id && s.authority_epoch);
    Value w = message("simulation_progress", s);
    w.object["authority_epoch"] = intV(*s.authority_epoch);
    w.object["frame_seq"] = intV(sequence);
    return w;
}

Value projectileLaunch(const SenderState& s, const Value& m) {
    const Value &shooter_kind = required(m, "shooter_kind"), &shooter_id = required(m, "shooter_id"), &shot_seq = required(m, "shot_seq"),
                &shell_index = required(m, "shell_index"), &origin = required(m, "origin"), &velocity = required(m, "velocity"),
                &gravity = required(m, "gravity"), &max_distance = required(m, "max_distance"), &max_time_ms = required(m, "max_time_ms"),
                &is_he = required(m, "is_he"), &splash_radius = required(m, "splash_radius");
    need(s.ready && s.phase == "battle" && oneOf(shooter_kind, {"player", "bot"}));
    const std::int64_t shooter = intIn(shooter_id, 1, MAX_ID), shell = intIn(shell_index, 0, 9);
    const Vec parsed_origin = world(origin), parsed_velocity = launchVelocity(velocity);
    const double parsed_gravity = floatIn(gravity, 0.000001, 500.), distance = floatIn(max_distance, 0.000001, MAX_DISTANCE);
    const std::int64_t time = intIn(max_time_ms, 1, MAX_TIME_MS);
    const double splash = floatIn(splash_radius, 0., 100.);
    const Value* penetration_field = field(m, "penetration_factor");
    const double penetration = penetration_field ? floatIn(*penetration_field, 0., 100.) : 1.;
    const Shot shot = sourceShot(get(m, "source_shot"));
    need(isBool(is_he));
    const bool he = is_he.resolved().boolean;
    const double speed = std::sqrt(parsed_velocity[0] * parsed_velocity[0] + parsed_velocity[1] * parsed_velocity[1] +
                                   parsed_velocity[2] * parsed_velocity[2]);
    need((he || splash == 0.) && closeTo(speed, shot.speed) && closeTo(parsed_gravity, shot.gravity) &&
         closeTo(distance, shot.max_distance) && he == shot.he && closeTo(splash, shot.radius));
    // A player launch needs a visible client identity; the worker launches only Bot shots.
    need(is(shooter_kind, "bot"));
    const std::int64_t epoch = intIn(get(m, "authority_epoch"), 0, MAX_ID), launch_time = intIn(get(m, "launch_time_us"), 0, MAX_MOTION_US);
    need(botAuthority(s) && epochIs(s, epoch));
    const Value& pose = get(m, "launch_pose");
    const auto& pose_items = items(pose);
    need(pose_items.size() == 6);
    const Vec position = world(listV({pose_items[0], pose_items[1], pose_items[2]}));
    Value launch_pose = vecV(position);
    for (std::size_t i = 3; i < 6; ++i) launch_pose.array.push_back(numV(floatIn(pose_items[i], -PI * 2., PI * 2.)));
    const std::int64_t seq = intIn(shot_seq, 1, MAX_ID);
    std::int64_t group = seq, index = 0, count = 1;
    const Value &group_seq = get(m, "burst_group_seq"), &burst_index = get(m, "burst_index"), &burst_count = get(m, "burst_count");
    if (!isNone(group_seq) || !isNone(burst_index) || !isNone(burst_count)) {
        group = intIn(group_seq, 1, MAX_ID);
        count = intIn(burst_count, 1, MAX_BURST);
        index = intIn(burst_index, 0, count - 1);
        need(seq == group + index);
    }
    Value w = message("projectile_launch", s);
    auto& o = w.object;
    o["shooter_kind"] = textV("bot");
    o["shooter_id"] = intV(shooter);
    o["shot_seq"] = intV(seq);
    o["burst_group_seq"] = intV(group);
    o["burst_index"] = intV(index);
    o["burst_count"] = intV(count);
    o["shell_index"] = intV(shell);
    o["source_shot"] = shot.wire;
    o["origin"] = vecV(parsed_origin);
    o["velocity"] = vecV(parsed_velocity);
    o["gravity"] = numV(parsed_gravity);
    o["max_distance"] = numV(distance);
    o["max_time_ms"] = intV(time);
    o["is_he"] = boolV(he);
    o["splash_radius"] = numV(splash);
    o["penetration_factor"] = numV(penetration);
    o["authority_epoch"] = intV(epoch);
    o["launch_time_us"] = intV(launch_time);
    o["launch_pose"] = std::move(launch_pose);
    // An invalid ammunition report is omitted, not refused.
    if (intOk(get(m, "shells_before_shot"), 1, 1000)) o["shells_before_shot"] = intV(get(m, "shells_before_shot").resolved().integer);
    return w;
}

std::int64_t currentEpoch(const SenderState& s, const Value& raw) {
    const std::int64_t epoch = intIn(raw, 0, MAX_ID);
    need(epochIs(s, epoch));
    return epoch;
}
double optionalFloat(const Value& m, const char* name, double fallback, double hi) {
    const Value* f = field(m, name);
    return f ? floatIn(*f, 0., hi) : fallback;
}

Value projectileProgress(const SenderState& s, const Value& m) {
    const Value &epoch_raw = required(m, "authority_epoch"), &cursors = required(m, "cursors");
    need(s.phase == "battle");
    const std::int64_t epoch = currentEpoch(s, epoch_raw);
    need(isList(cursors) && !cursors.resolved().array.empty() && cursors.resolved().array.size() <= MAX_BATCH);
    static const Names exact{"projectile_id", "base_checked_ms", "checked_through_ms", "checked_distance", "piercing_loss",
                             "penetration_factor", "destructibles"};
    Value parsed = listV();
    Names seen;
    std::size_t destructibles = 0;
    for (const auto& cursor : cursors.resolved().array) {
        need(isDict(cursor));
        Names have = keys(cursor);
        have.erase("launch_proof");
        need(have == exact);
        const std::string id = projectileId(get(cursor, "projectile_id"));
        const std::int64_t base = intIn(get(cursor, "base_checked_ms"), 0, MAX_TIME_MS), through = intIn(get(cursor, "checked_through_ms"), 0, MAX_TIME_MS);
        Value row = dictV();
        auto& o = row.object;
        o["checked_distance"] = numV(floatIn(get(cursor, "checked_distance"), 0., MAX_DISTANCE));
        o["piercing_loss"] = numV(floatIn(get(cursor, "piercing_loss"), 0., MAX_PIERCING_LOSS));
        o["penetration_factor"] = numV(floatIn(get(cursor, "penetration_factor"), 0., 100.));
        o["destructibles"] = shotDestructibles(get(cursor, "destructibles"), destructibles);
        need(!seen.count(id) && through >= base && destructibles <= MAX_DESTRUCTIBLES);
        ownProjectile(s, get(cursor, "projectile_id"), get(cursor, "launch_proof"));
        seen.insert(id);
        o["projectile_id"] = textV(id);
        o["base_checked_ms"] = intV(base);
        o["checked_through_ms"] = intV(through);
        parsed.array.push_back(std::move(row));
    }
    Value w = message("projectile_progress", s);
    w.object["authority_epoch"] = intV(epoch);
    w.object["cursors"] = std::move(parsed);
    return w;
}

Value projectileResolve(const SenderState& s, const Value& m) {
    const Value &epoch_raw = required(m, "authority_epoch"), &projectile_id = required(m, "projectile_id"),
                &base_checked_ms = required(m, "base_checked_ms"), &outcome = required(m, "outcome"),
                &resolved_time_ms = required(m, "resolved_time_ms"), &impact = required(m, "impact"), &direct = required(m, "direct"),
                &splash = required(m, "splash");
    need(s.phase == "battle");
    ownProjectile(s, projectile_id, get(m, "launch_proof"));
    const std::int64_t epoch = currentEpoch(s, epoch_raw);
    const std::string id = projectileId(projectile_id);
    const std::int64_t base = intIn(base_checked_ms, 0, MAX_TIME_MS), time = intIn(resolved_time_ms, 0, MAX_TIME_MS);
    need(oneOf(outcome, {"impact", "miss", "expired"}) && time >= base);
    const bool hit = is(outcome, "impact");
    const double distance = optionalFloat(m, "checked_distance", 0., MAX_DISTANCE), loss = optionalFloat(m, "piercing_loss", 0., MAX_PIERCING_LOSS);
    const double factor = optionalFloat(m, "penetration_factor", 1., 100.);
    const Value& hit_vehicle_raw = get(m, "hit_vehicle");
    need(isNone(hit_vehicle_raw) || isBool(hit_vehicle_raw));
    const bool hit_vehicle = isNone(hit_vehicle_raw) ? !isNone(direct) : hit_vehicle_raw.resolved().boolean;
    const Value& wreck_raw = get(m, "wreck_hit");
    const std::optional<Value> wreck = isNone(wreck_raw) ? std::nullopt : std::optional<Value>(wreckHit(wreck_raw));
    const Value& destructibles_raw = get(m, "destructibles");
    std::size_t destructible_count = 0;
    Value destructibles = isNone(destructibles_raw) ? listV() : shotDestructibles(destructibles_raw, destructible_count);
    need(destructible_count <= MAX_DESTRUCTIBLES);
    need(hit || isNone(impact));
    const Value parsed_impact = hit ? vecV(world(impact)) : Value();
    need((hit || !hit_vehicle) && isList(splash) && splash.resolved().array.size() <= MAX_BATCH);
    std::set<std::pair<std::string, std::int64_t>> targets;
    Value parsed_direct;
    if (!isNone(direct)) {
        parsed_direct = effect(direct);
        need(!parsed_direct.object.count("target_x"));
        targets.insert({parsed_direct.get("target_kind").text, parsed_direct.get("target_id").integer});
    }
    Value parsed_splash = listV();
    for (const auto& raw : splash.resolved().array) {
        Value parsed = effect(raw);
        need(!parsed.object.count("damage_sticker") && parsed.object.count("target_x"));
        need(targets.insert({parsed.get("target_kind").text, parsed.get("target_id").integer}).second);
        parsed_splash.array.push_back(std::move(parsed));
    }
    need(hit || (isNone(parsed_direct) && parsed_splash.array.empty()));
    need(isNone(parsed_direct) || hit_vehicle);
    need(!wreck || (hit && hit_vehicle && isNone(parsed_direct)));
    Value w = message("projectile_resolve", s);
    auto& o = w.object;
    o["authority_epoch"] = intV(epoch);
    o["projectile_id"] = textV(id);
    o["base_checked_ms"] = intV(base);
    o["outcome"] = textV(outcome.resolved().text);
    o["resolved_time_ms"] = intV(time);
    o["checked_distance"] = numV(distance);
    o["piercing_loss"] = numV(loss);
    o["penetration_factor"] = numV(factor);
    o["impact"] = parsed_impact;
    o["direct"] = std::move(parsed_direct);
    o["splash"] = std::move(parsed_splash);
    o["destructibles"] = std::move(destructibles);
    if (s.server_capabilities.count("projectile_hit_vehicle_v1")) o["hit_vehicle"] = boolV(hit_vehicle);
    if (wreck && s.server_capabilities.count("projectile_wreck_hit_v1")) o["wreck_hit"] = *wreck;
    return w;
}

Value projectileRicochet(const SenderState& s, const Value& m) {
    const Value &epoch_raw = required(m, "authority_epoch"), &projectile_id = required(m, "projectile_id"),
                &base_checked_ms = required(m, "base_checked_ms"), &resolved_time_ms = required(m, "resolved_time_ms"),
                &impact = required(m, "impact"), &segment_origin = required(m, "segment_origin"),
                &segment_velocity = required(m, "segment_velocity"), &multiplier_raw = required(m, "base_penetration_multiplier"),
                &direct = required(m, "direct");
    need(s.phase == "battle");
    ownProjectile(s, projectile_id, get(m, "launch_proof"));
    const std::int64_t epoch = currentEpoch(s, epoch_raw);
    const std::string id = projectileId(projectile_id);
    const std::int64_t base = intIn(base_checked_ms, 0, MAX_TIME_MS), time = intIn(resolved_time_ms, 0, MAX_TIME_MS);
    const Vec parsed_impact = world(impact), origin = world(segment_origin), velocity = launchVelocity(segment_velocity);
    const double multiplier = floatIn(multiplier_raw, 0., 1.);
    const double distance = optionalFloat(m, "checked_distance", 0., MAX_DISTANCE), loss = optionalFloat(m, "piercing_loss", 0., MAX_PIERCING_LOSS);
    const double factor = optionalFloat(m, "penetration_factor", 1., 100.);
    Value parsed_direct = effect(direct);
    const Value& destructibles_raw = get(m, "destructibles");
    std::size_t destructible_count = 0;
    Value destructibles = isNone(destructibles_raw) ? listV() : shotDestructibles(destructibles_raw, destructible_count);
    need(destructible_count <= MAX_DESTRUCTIBLES);
    // A bounce publishes no critical, stun or splash pose tokens.
    static const Names allowed{"target_kind", "target_id", "damage", "shot_result", "x", "y", "z",
                               "damage_sticker", "potential_damage", "structural_armor_hit", "high_explosive"};
    need(time >= base && (multiplier == 0.75 || multiplier == 1.) && parsed_direct.get("damage").integer == 0 &&
         parsed_direct.get("shot_result").integer == 0);
    for (const auto& entry : parsed_direct.object) need(allowed.count(entry.first) != 0);
    double squared = 0.;
    for (std::size_t i = 0; i < 3; ++i) squared += (origin[i] - parsed_impact[i]) * (origin[i] - parsed_impact[i]);
    need(std::sqrt(squared) <= 0.1);
    Value w = message("projectile_ricochet", s);
    auto& o = w.object;
    o["authority_epoch"] = intV(epoch);
    o["projectile_id"] = textV(id);
    o["base_checked_ms"] = intV(base);
    o["resolved_time_ms"] = intV(time);
    o["checked_distance"] = numV(distance);
    o["piercing_loss"] = numV(loss);
    o["penetration_factor"] = numV(factor);
    o["impact"] = vecV(parsed_impact);
    o["segment_origin"] = vecV(origin);
    o["segment_velocity"] = vecV(velocity);
    o["base_penetration_multiplier"] = numV(multiplier);
    o["direct"] = std::move(parsed_direct);
    o["destructibles"] = std::move(destructibles);
    return w;
}

// json.dumps(allow_nan=False) refused any non-finite number in the queued message.
bool finiteTree(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind == Kind::Number) return std::isfinite(v.number);
    if (v.kind == Kind::Array) return std::all_of(v.array.begin(), v.array.end(), finiteTree);
    if (v.kind == Kind::Object) {
        for (const auto& entry : v.object) if (!finiteTree(entry.second)) return false;
    }
    return true;
}
}

std::optional<Outgoing> build(SenderState& s, const Value& message) {
    try {
        const Value& type_value = get(message, "type").resolved();
        if (type_value.kind != Kind::Text) return std::nullopt;
        const std::string& type = type_value.text;
        Outgoing out;
        if (type == "bot_state") botState(s, message, out);
        else if (type == "hello") out.wire = hello();
        else if (type == "battle_ready") out.wire = battleReady(s, message);
        else if (type == "bot_manifest") out.wire = botManifest(s, message);
        else if (type == "bot_observation") out.wire = botObservation(s, message);
        else if (type == "bot_ram") out.wire = botRam(s, message);
        else if (type == "rules_state") out.wire = rulesState(s, message);
        else if (type == "battle_result") out.wire = battleResult(s, message);
        else if (type == "destructible") out.wire = destructible(s, message);
        else if (type == "player_destructible_contact_result") out.wire = playerDestructibleContactResult(s, message);
        else if (type == "player_environment") out.wire = playerEnvironment(s, message);
        else if (type == "simulation_progress") out.wire = simulationProgress(s, message);
        else if (type == "projectile_launch") out.wire = projectileLaunch(s, message);
        else if (type == "projectile_progress") out.wire = projectileProgress(s, message);
        else if (type == "projectile_resolve") out.wire = projectileResolve(s, message);
        else if (type == "projectile_ricochet") out.wire = projectileRicochet(s, message);
        else return std::nullopt;
        if (!finiteTree(out.wire)) return std::nullopt;
        return out;
    } catch (const Refused&) {
        return std::nullopt;
    }
}
}
}
