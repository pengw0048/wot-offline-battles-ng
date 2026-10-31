// The hidden worker's LAN link: the pinned AuthorityWorkerLANClient and the LANClient paths it uses, on native
// values. The transport threads decode every frame; this file admits, projects and dispatches them.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_link_internal.hpp"
#include "stock_native_battle_params.hpp"
#include "stock_native_battle_subsystems.hpp"
#include "stock_wire_codec.hpp"
#include "stock_wire_net.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace native_battle {
namespace link {
namespace {
using K = Value::Kind;
constexpr std::int64_t AUTHORITY_ID = -1, PROTOCOL = 5, MAX_ID = 2147483647, MAX_MOTION = 10000000000000000LL;
constexpr double POLL_SECONDS = 1. / 60., PING_SECONDS = 1., WORKER_DUMMY_Y = -500.;
constexpr std::size_t MAX_OUTBOUND_MESSAGES = 256, MAX_OUTBOUND_BYTES = 256u * 1024u * 4u, MAX_RECEIVE = 512;
constexpr std::size_t MAX_TURRETS = 12, MAX_COMPACT_BYTES = 64u * 1024u, MAX_OUTFIT_BYTES = 64u * 1024u;

double clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---- value helpers -------------------------------------------------------------------------
const Value none;
const Value& at(const Value& v, const char* name) { return v.resolved().get(name); }
bool has(const Value& v, const char* name) { const Value& r = v.resolved(); return r.kind == K::Object && r.object.count(name) != 0; }
Value integer(std::int64_t v) { Value x; x.kind = K::Integer; x.integer = v; return x; }
Value number(double v) { Value x; x.kind = K::Number; x.number = v; return x; }
Value text(const std::string& v) { Value x; x.kind = K::Text; x.text = v; return x; }
Value boolean(bool v) { Value x; x.kind = K::Boolean; x.boolean = v; return x; }
Value object() { Value x; x.kind = K::Object; return x; }
Value array() { Value x; x.kind = K::Array; return x; }
Value shared(std::shared_ptr<const Value> v) { Value x; x.kind = K::Shared; x.reference = std::move(v); return x; }
Value& writable(Value& v) { if (v.reference) { Value copy = *v.reference; v = std::move(copy); } return v; }

// _exact_int: an int, or a float holding an integral value.
std::optional<std::int64_t> exact(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind == K::Integer && !v.unsigned_integer) return v.integer;
    if (v.kind == K::Number && std::isfinite(v.number) && std::floor(v.number) == v.number && std::abs(v.number) < 9.2e18)
        return std::int64_t(v.number);
    return std::nullopt;
}
// _projectile_int_range: an exact int inside [low, high].
std::optional<std::int64_t> ranged(const Value& raw, std::int64_t low, std::int64_t high) {
    const Value& v = raw.resolved();
    if (v.kind != K::Integer || v.unsigned_integer || v.integer < low || v.integer > high) return std::nullopt;
    return v.integer;
}
// A non-bool number inside [low, high].
std::optional<double> real(const Value& raw, double low, double high) {
    const Value& v = raw.resolved();
    if (v.kind != K::Integer && v.kind != K::Number) return std::nullopt;
    const double n = v.numeric(NAN);
    if (!std::isfinite(n) || n < low || n > high) return std::nullopt;
    return n;
}
double finite(const Value& raw, double fallback) {
    const Value& v = raw.resolved();
    if (v.kind != K::Integer && v.kind != K::Number) return fallback;
    const double n = v.numeric(NAN);
    return std::isfinite(n) ? n : fallback;
}
std::string safeText(const Value& raw, std::size_t limit = 80) {
    const Value& v = raw.resolved();
    std::string out = v.kind == K::Text ? v.text : std::string();
    if (out.size() > limit) out.resize(limit);
    return out;
}
// _strict_mapping_list: a list of at most `limit` objects.
bool rows(const Value& raw, std::size_t limit) {
    const Value& v = raw.resolved();
    if (v.kind != K::Array || v.array.size() > limit) return false;
    for (const auto& row : v.array) if (row.resolved().kind != K::Object) return false;
    return true;
}
std::optional<std::set<std::string>> capabilities(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind != K::Array || v.array.size() > 32) return std::nullopt;
    std::set<std::string> out;
    for (const auto& item : v.array) {
        const Value& r = item.resolved();
        if (r.kind != K::Text) return std::nullopt;
        const std::string name = safeText(r);
        if (name.empty() || !out.insert(name).second) return std::nullopt;
    }
    return out;
}

// Canonical base64 (decode then encode gives the same text), with its decoded size.
std::optional<std::size_t> base64(const std::string& s) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (s.empty() || s.size() % 4) return std::nullopt;
    std::size_t padding = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '=') { if (i < s.size() - 2) return std::nullopt; ++padding; continue; }
        if (padding || alphabet.find(c) == std::string::npos) return std::nullopt;
    }
    // Non-canonical trailing bits would re-encode differently.
    if (padding) {
        const auto last = alphabet.find(s[s.size() - padding - 1]);
        if ((padding == 1 && (last & 3)) || (padding == 2 && (last & 15))) return std::nullopt;
    }
    return s.size() / 4 * 3 - padding;
}
bool compactDescriptor(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind != K::Text) return false;
    const auto size = base64(v.text);
    return size && *size > 0 && *size <= MAX_COMPACT_BYTES;
}
std::optional<Value> outfits(const Value& raw) {
    const Value& v = raw.resolved();
    if (v.kind == K::Null) return object();
    if (v.kind != K::Object || v.object.size() > 3) return std::nullopt;
    Value out = object();
    std::size_t total = 0;
    for (const auto& entry : v.object) {
        const std::string& season = entry.first;
        if (season != "1" && season != "2" && season != "4") return std::nullopt;
        const Value& encoded = entry.second.resolved();
        if (encoded.kind != K::Text) return std::nullopt;
        const auto size = base64(encoded.text);
        if (!size || !*size || *size > MAX_OUTFIT_BYTES) return std::nullopt;
        total += *size;
        if (total > MAX_OUTFIT_BYTES * 3) return std::nullopt;
        out.object[season] = encoded;
    }
    return out;
}

// ---- the link ------------------------------------------------------------------------------
struct Static { Value source, canonical; };
struct Staged {
    std::map<std::int64_t, Static> outfits, params;
    bool outfits_valid = true, params_valid = true;
};

struct Link {
    std::shared_ptr<stock_wire_net::Native> net;
    Handler handler;
    Object bigworld, poll_id;
    std::uint64_t generation = 0;
    SenderState s;
    bool connected = false, running = false, negotiated = false;
    std::optional<std::int64_t> state_revision, host_player_id, server_time_ms, start_round, live_round, timing_round, turret_round;
    std::int64_t timing_tick = -1, team = 1, ping_seq = 0;
    std::string map;
    Value roster = array(), dummy;
    std::map<std::int64_t, Static> outfits, params;
    std::shared_ptr<const Value> last_snapshot;
    std::optional<double> combat_deadline, combat_end_deadline, combat_duration, rtt_ms, minimum_rtt_ms;
    std::string combat_phase;
    double last_ping = -1.;
    std::deque<double> frames;
    double frame_seconds = 0., last_frame = 0.;
    std::pair<std::optional<std::int64_t>, std::optional<std::int64_t>> frame_scope;
    std::map<std::string, Value> turrets;
    std::string error;
    int dispatch_depth = 0;
};
std::unique_ptr<Link> link;

void fail(const std::string& why) {
    if (link->error.empty()) link->error = why;
    link->running = false;
}
bool sendWire(const Value& wire, const std::optional<std::string>& key = std::nullopt) {
    if (!link->net || !link->connected || !link->running) return false;
    std::string payload, error;
    if (!stock_wire::encode_value(wire, payload, error)) {
        log("worker link could not encode " + safeText(at(wire, "type")) + ": " + error);
        return false;
    }
    const std::string type = safeText(at(wire, "type"));
    // Worker combat and protocol edges get a doubled backlog; continuous state keeps the exact bound.
    static const std::set<std::string> continuous = {"bot_state", "bot_observation", "projectile_progress", "simulation_progress",
                                                     "player_environment"};
    const std::size_t scale = key || continuous.count(type) ? 1 : 2;
    return stock_wire_net::send(*link->net, std::move(payload), type, key, MAX_OUTBOUND_MESSAGES * scale, MAX_OUTBOUND_BYTES * scale) !=
           stock_wire_net::Queued::refused;
}

// Stage canonical static inputs for each row in place, restoring a lean row's cached outfits and effective parameters.
Staged prepare(Value& players, bool allow_lean) {
    Staged out;
    for (auto& raw : writable(players).array) {
        Value& entry = writable(raw);
        const auto id = exact(at(entry, "id"));
        for (int kind = 0; kind < 2; ++kind) {
            const char* name = kind ? "effective_params" : "outfits";
            auto& updates = kind ? out.params : out.outfits;
            auto& published = kind ? link->params : link->outfits;
            bool& valid = kind ? out.params_valid : out.outfits_valid;
            auto staged = id ? updates.find(*id) : updates.end();
            auto cached = id ? published.find(*id) : published.end();
            if (has(entry, name)) {
                const Value& source = at(entry, name);
                std::optional<Value> canonical;
                if (staged != updates.end() && staged->second.source == source) canonical = staged->second.canonical;
                else if (cached != published.end() && cached->second.source == source) {
                    // The published version is unchanged: reference it without staging another copy.
                    entry.object[name] = cached->second.canonical;
                    continue;
                } else {
                    // One shared version per accepted source, so every later row references it.
                    canonical = kind ? params::canonicalEffectiveParams(source) : outfits(source);
                    if (canonical) canonical = shared(std::make_shared<const Value>(std::move(*canonical)));
                }
                if (canonical && id) {
                    updates[*id] = Static{source, *canonical};
                    entry.object[name] = *canonical;
                } else if (!canonical) valid = false;
            } else if (staged != updates.end()) {
                entry.object[name] = staged->second.canonical;
                if (kind && !allow_lean) valid = false;
            } else if (cached != published.end()) {
                entry.object[name] = cached->second.canonical;
                if (kind && !allow_lean) valid = false;
            } else if (kind) valid = false;
        }
    }
    return out;
}
void commit(Staged& staged) {
    for (auto& entry : staged.outfits) link->outfits[entry.first] = std::move(entry.second);
    for (auto& entry : staged.params) link->params[entry.first] = std::move(entry.second);
}

// The worker's own Avatar carrier, parked below the map with the first real player's descriptor.
std::optional<Value> dummyPlayer(const Value& players) {
    if (link->dummy.kind == K::Object) return link->dummy;
    const Value* source = nullptr;
    for (const auto& row : players.resolved().array) {
        const auto id = exact(at(row, "id"));
        if (id && *id > 0 && at(row, "vehicle").truth()) { source = &row.resolved(); break; }
    }
    if (!source) return std::nullopt;
    const auto id = *exact(at(*source, "id"));
    const auto params = link->params.find(id);
    const Value effective = params != link->params.end() ? params->second.canonical : at(*source, "effective_params");
    if (effective.resolved().kind != K::Object) return std::nullopt;
    Value d = object();
    d.object["id"] = integer(AUTHORITY_ID);
    d.object["name"] = text("SimulationWorker");
    d.object["vehicle"] = at(*source, "vehicle");
    d.object["vehicle_compact_descr"] = at(*source, "vehicle_compact_descr");
    const auto team = exact(at(*source, "team"));
    d.object["team"] = integer(team && *team ? *team : 1);
    d.object["slot"] = integer(0);
    for (const char* axis : {"x", "z", "yaw", "aim_yaw", "gun_pitch", "speed"}) d.object[axis] = number(0.);
    d.object["y"] = number(WORKER_DUMMY_Y);
    d.object["world_pose"] = boolean(true);
    d.object["health"] = integer(1);
    d.object["max_health"] = integer(1);
    d.object["alive"] = boolean(true);
    d.object["critical"] = object();
    for (const char* name : {"critical_revision", "critical_base_revision", "critical_ack_seq", "input_seq", "landing_observation_seq"})
        d.object[name] = integer(0);
    d.object["up_cosine"] = number(1.);
    d.object["equipment_states"] = at(*source, "equipment_states").resolved().kind == K::Array ? at(*source, "equipment_states") : array();
    d.object["equipment_revision"] = at(*source, "equipment_revision");
    d.object["equipment_intent_seq"] = at(*source, "equipment_intent_seq");
    d.object["equipment_intent_result"] = at(*source, "equipment_intent_result").resolved().kind == K::Object ? at(*source, "equipment_intent_result") : object();
    d.object["outfits"] = object();
    d.object["effective_params"] = effective;
    link->outfits[AUTHORITY_ID] = Static{object(), object()};
    link->params[AUTHORITY_ID] = Static{effective, effective};
    link->team = d.object["team"].integer;
    link->dummy = d;
    return d;
}

// Bot equipment rows carry an index into the message's contract table, or reuse the previous row's contract.
void restoreEquipment(Value& state, const Value& contracts, const Value* previous) {
    Value& items = state.object["equipment_states"];
    if (!items.truth()) { if (items.kind == K::Null) state.object.erase("equipment_states"); return; }
    if (items.resolved().kind != K::Array) throw p::Error("Invalid equipment state list");
    writable(items);
    bool same = previous != nullptr;
    for (const char* k : {"id", "vehicle", "team", "slot"}) same = same && at(state, k) == at(*previous, k);
    const Value& old = previous ? at(*previous, "equipment_states") : none;
    const auto& table = contracts.resolved();
    for (std::size_t i = 0; i < items.array.size(); ++i) {
        Value& item = writable(items.array[i]);
        if (item.kind != K::Object) throw p::Error("Bot equipment state is not an object");
        const Value& contract = at(item, "equipment");
        if (contract.resolved().kind == K::Object) continue;
        const Value* selected = nullptr;
        if (contract.resolved().kind == K::Integer && table.kind == K::Array && table.array.size() <= 90) {
            const auto index = contract.resolved().integer;
            if (index >= 0 && std::size_t(index) < table.array.size() && table.array[std::size_t(index)].resolved().kind == K::Object)
                selected = &table.array[std::size_t(index)];
        }
        if (!selected && same && old.resolved().kind == K::Array && old.resolved().array.size() == items.array.size()) {
            const Value& c = at(old.resolved().array[i], "equipment");
            if (c.resolved().kind == K::Object) selected = &c;
        }
        if (!selected) throw p::Error("Bot equipment contract is unavailable");
        item.object["equipment"] = *selected;
    }
}

// The runtime actor row: pose fields as floats inside their bounds, identity fields exact, stun factors rounded.
double roundSix(double v) {
    // CPython 2.7's round(v, 6), the server's own rounding of these factors.
    static PyObject* builtin = nullptr;
    if (!builtin) {
        p::Ref module(PyImport_ImportModule("__builtin__"));
        builtin = p::value(module.get(), "round").release();
    }
    p::Ref out(PyObject_CallFunction(builtin, const_cast<char*>("di"), v, 6));
    if (!out.get()) throw p::Error("round failed");
    return PyFloat_AsDouble(out.get());
}
Value canonicalRow(Value row) {
    if (row.kind != K::Object) throw p::Error("Invalid runtime row");
    static const std::map<std::string, double> bounds = {{"x", 2000.}, {"y", 1000.}, {"z", 2000.}, {"pitch", 3.14159265358979323846},
                                                         {"roll", 3.14159265358979323846}, {"gun_pitch", 1.2}, {"speed", 200.}};
    for (const char* k : {"x", "y", "z", "yaw", "pitch", "roll", "aim_yaw", "turret_yaw", "gun_pitch", "speed"}) {
        if (!has(row, k)) continue;
        const auto n = real(at(row, k), -INFINITY, INFINITY);
        if (!n) throw p::Error("Runtime pose is not finite");
        const auto bound = bounds.find(k);
        if (bound != bounds.end() && std::abs(*n) > bound->second) throw p::Error("Runtime pose exceeds bounds");
        row.object[k] = number(*n);
    }
    for (const char* k : {"id", "team", "slot", "health", "max_health", "fire_seq", "shell_index", "movement_dir", "rotation_dir",
                          "stun_end_server_time_ms"}) {
        if (!has(row, k)) continue;
        const auto n = exact(at(row, k));
        if (!n) throw p::Error("Runtime identity is not an integer");
        row.object[k] = integer(*n);
    }
    const Value& factors = at(row, "stun_factors");
    if (factors.truth()) {
        static const std::set<std::string> names = {"mobility", "traverse", "turret_speed", "vision", "speed", "reload", "aim_time",
                                                    "bloom_move", "bloom_rotation", "bloom_turret", "dispersion"};
        static const std::set<std::string> up = {"reload", "aim_time", "bloom_move", "bloom_rotation", "bloom_turret", "dispersion"};
        const Value& f = factors.resolved();
        if (f.kind != K::Object || f.object.size() != names.size()) throw p::Error("Invalid stun factor fields");
        Value normalized = object();
        for (const auto& entry : f.object) {
            if (!names.count(entry.first)) throw p::Error("Invalid stun factor fields");
            const bool increasing = up.count(entry.first) != 0;
            const auto n = real(entry.second, increasing ? 1. : 0., increasing ? 100. : 1.);
            if (!n) throw p::Error("Invalid stun factor");
            normalized.object[entry.first] = number(roundSix(*n));
        }
        row.object["stun_factors"] = std::move(normalized);
    }
    if (has(row, "velocity")) {
        const Value& v = at(row, "velocity").resolved();
        if (v.kind != K::Array || v.array.size() < 3) throw p::Error("Invalid runtime velocity");
        Value out = array();
        for (int i = 0; i < 3; ++i) {
            const auto n = real(v.array[std::size_t(i)], -INFINITY, INFINITY);
            if (!n) throw p::Error("Invalid runtime velocity");
            out.array.push_back(number(*n));
        }
        row.object["velocity"] = std::move(out);
    }
    return row;
}
// Canonical rows; an invalid row keeps its previous same-round version (counted as retained).
std::pair<Value, long> canonicalRows(Value incoming, const char* field, const Value* message) {
    const Value* previous = link->last_snapshot.get();
    const bool same_round = previous && (!message || at(*previous, "round_id") == at(*message, "round_id"));
    const bool same_scope = same_round && (!message || at(*previous, "authority_epoch") == at(*message, "authority_epoch"));
    std::map<std::int64_t, const Value*> indexed;
    if (same_round && at(*previous, field).resolved().kind == K::Array)
        for (const auto& row : at(*previous, field).resolved().array)
            if (const auto id = exact(at(row, "id"))) indexed[*id] = &row.resolved();
    Value out = array();
    long retained = 0;
    // Each contract is shared once per message; every equipment row references it.
    Value contracts = array();
    if (message && at(*message, "bot_equipment_contracts").resolved().kind == K::Array)
        for (const auto& contract : at(*message, "bot_equipment_contracts").resolved().array)
            contracts.array.push_back(contract.reference ? contract : shared(std::make_shared<const Value>(contract)));
    else if (message && has(*message, "bot_equipment_contracts")) contracts = at(*message, "bot_equipment_contracts");
    for (auto& raw : writable(incoming).array) {
        const auto id = exact(at(raw, "id"));
        const auto prior = id ? indexed.find(*id) : indexed.end();
        try {
            Value row = raw.reference ? *raw.reference : std::move(raw);
            if (std::string(field) == "bots") restoreEquipment(row, contracts, same_scope && prior != indexed.end() ? prior->second : nullptr);
            out.array.push_back(canonicalRow(std::move(row)));
        } catch (const std::exception&) {
            if (prior == indexed.end()) continue;
            out.array.push_back(*prior->second);
            ++retained;
        }
    }
    return {std::move(out), retained};
}

// The canonical projectile ledger rows of one snapshot.
bool vector3(const Value& raw, std::array<double, 3>& out, bool velocity) {
    const Value& v = raw.resolved();
    if (v.kind != K::Array || v.array.size() != 3) return false;
    double square = 0.;
    for (std::size_t i = 0; i < 3; ++i) {
        const double low = velocity ? -3000. : i == 1 ? -1000. : -5000., high = velocity ? 3000. : i == 1 ? 3000. : 5000.;
        const auto n = real(v.array[i], low, high);
        if (!n) return false;
        out[i] = *n;
        square += *n * *n;
    }
    return !velocity || (square > .000001 && square <= 9000000.);
}
bool close(double a, double b) { return std::abs(a - b) <= std::max(.001, std::abs(b) * .000001); }
bool sourceShot(const Value& shot, const std::array<double, 3>& velocity, double gravity, double maximum, bool he, double radius) {
    if (shot.resolved().kind != K::Object) return false;
    const auto speed = real(at(shot, "speed"), .000001, 3000.), g = real(at(shot, "gravity"), .000001, 500.),
               distance = real(at(shot, "maxDistance"), .000001, 10000.);
    const Value& shell = at(shot, "shell").resolved();
    if (!speed || !g || !distance || shell.kind != K::Object) return false;
    const double launch = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]);
    const std::string kind = safeText(at(shell, "kind"));
    const bool is_he = kind == "HIGH_EXPLOSIVE";
    const double explosion = finite(at(shell, "explosionRadius"), 0.);
    return close(launch, *speed) && close(gravity, *g) && close(maximum, *distance) && he == is_he && (!he || close(radius, explosion));
}
bool projectiles(const Value& raw, std::int64_t epoch, std::int64_t server_time) {
    static const std::set<std::string> expected = {
        "projectile_id", "shooter_kind", "shooter_id", "shot_seq", "burst_group_seq", "burst_index", "burst_count", "source_vehicle",
        "source_shot", "shell_index", "team", "origin", "velocity", "gravity", "max_distance", "max_time_ms", "is_he", "splash_radius",
        "penetration_factor", "launch_server_time_ms", "checked_through_ms", "checked_distance", "piercing_loss", "range_origin",
        "segment_origin", "segment_velocity", "segment_start_time_ms", "ricochet_count", "base_penetration_multiplier", "authority_epoch"};
    const Value& list = raw.resolved();
    if (list.kind != K::Array || list.array.size() > 256) return false;
    std::set<std::string> seen;
    for (const auto& item : list.array) {
        const Value& row = item.resolved();
        if (row.kind != K::Object) return false;
        std::set<std::string> keys;
        for (const auto& entry : row.object) keys.insert(entry.first);
        const std::string shooter = safeText(at(row, "shooter_kind"));
        if (keys.erase("bot_presentation_time_us") && (shooter != "bot" || !ranged(at(row, "bot_presentation_time_us"), 0, MAX_MOTION)))
            return false;
        auto player_expected = expected;
        player_expected.insert("fire_intent_seq");
        player_expected.insert("fire_input_seq");
        if (keys != expected && keys != player_expected) return false;
        if ((shooter == "player") != (keys == player_expected)) return false;
        const Value& id_value = at(row, "projectile_id").resolved();
        if (id_value.kind != K::Text) return false;
        const std::string& id = id_value.text;
        if (id.empty() || id.size() > 96 || seen.count(id)) return false;
        for (unsigned char c : id) if (c >= 128 || (!std::isalnum(c) && c != ':' && c != '_' && c != '-')) return false;
        const Value& vehicle = at(row, "source_vehicle").resolved();
        if (vehicle.kind != K::Text || vehicle.text.empty() || vehicle.text.size() > 128) return false;
        const auto shooter_id = ranged(at(row, "shooter_id"), 1, MAX_ID), shot = ranged(at(row, "shot_seq"), 1, MAX_ID),
                   group = ranged(at(row, "burst_group_seq"), 1, MAX_ID), count = ranged(at(row, "burst_count"), 1, 64);
        const auto index = ranged(at(row, "burst_index"), 0, count ? *count - 1 : 0), shell = ranged(at(row, "shell_index"), 0, 9),
                   team = ranged(at(row, "team"), 1, 2);
        if (!shooter_id || !shot || !group || !count || !index || *shot != *group + *index || !shell || !team ||
            (shooter != "bot" && shooter != "player"))
            return false;
        if (shooter == "player" && (!ranged(at(row, "fire_intent_seq"), 1, MAX_ID) || !ranged(at(row, "fire_input_seq"), 1, MAX_ID)))
            return false;
        std::array<double, 3> origin, velocity, range, segment, segment_velocity;
        if (!vector3(at(row, "origin"), origin, false) || !vector3(at(row, "velocity"), velocity, true) ||
            !vector3(at(row, "range_origin"), range, false) || !vector3(at(row, "segment_origin"), segment, false) ||
            !vector3(at(row, "segment_velocity"), segment_velocity, true))
            return false;
        const auto gravity = real(at(row, "gravity"), .000001, 500.), maximum = real(at(row, "max_distance"), .000001, 10000.),
                   radius = real(at(row, "splash_radius"), 0., 100.), penetration = real(at(row, "penetration_factor"), 0., 100.),
                   checked_distance = real(at(row, "checked_distance"), 0., 10000.1), loss = real(at(row, "piercing_loss"), 0., 100000.),
                   multiplier = real(at(row, "base_penetration_multiplier"), 0., 1.);
        const auto max_time = ranged(at(row, "max_time_ms"), 1, 20000), launch = ranged(at(row, "launch_server_time_ms"), 0, MAX_ID),
                   checked = ranged(at(row, "checked_through_ms"), 0, 20000), start = ranged(at(row, "segment_start_time_ms"), 0, 20000),
                   ricochet = ranged(at(row, "ricochet_count"), 0, 1), row_epoch = ranged(at(row, "authority_epoch"), 0, MAX_ID);
        const Value& he = at(row, "is_he").resolved();
        if (!gravity || !maximum || !radius || !penetration || !checked_distance || !loss || !multiplier || !max_time || !launch ||
            *launch > server_time + 250 || !checked || *checked > *max_time || *checked_distance > *maximum + .1 || !start ||
            *start > *checked || !ricochet || (*ricochet == 1 && *start >= *max_time) || !row_epoch || *row_epoch != epoch ||
            he.kind != K::Boolean || !sourceShot(at(row, "source_shot"), velocity, *gravity, *maximum, he.boolean, *radius))
            return false;
        if (*ricochet == 0) {
            if (*start != 0 || segment != origin || segment_velocity != velocity || *multiplier != 1.) return false;
        } else {
            const std::string kind = safeText(at(at(at(row, "source_shot"), "shell"), "kind"));
            if ((kind == "ARMOR_PIERCING" || kind == "ARMOR_PIERCING_CR") ? *multiplier != .75 : kind == "HOLLOW_CHARGE" ? *multiplier != 1. : true)
                return false;
        }
        seen.insert(id);
    }
    return true;
}

// Project the server's relative battle timing onto this link's receive clock.
bool loadTiming(const Value& message) {
    const Value& timing = at(message, "timing").resolved();
    if (timing.kind != K::Object) return false;
    const auto round = exact(at(message, "round_id")), tick = exact(at(message, "server_tick")), start = exact(at(timing, "start_in_ms")),
               remaining = exact(at(timing, "remaining_ms")), duration = exact(at(timing, "duration_ms"));
    const std::string phase = safeText(at(timing, "phase"), 16);
    if (!round || round != link->s.round_id || !tick || *tick < 0 ||
        (phase != "loading" && phase != "prebattle" && phase != "battle" && phase != "finished") || !start || *start < 0 || !remaining ||
        *remaining < 0 || !duration || *duration <= 0 || *remaining > *duration)
        return false;
    if (link->timing_round == round && *tick <= link->timing_tick) return true;
    const double received = finite(at(message, "_client_received_time"), clock());
    const double one_way = link->rtt_ms ? std::max(0., std::min(.25, *link->rtt_ms / 2000.)) : 0.;
    const double seconds = double(*duration) / 1000.;
    double projected = 0.;
    if (phase == "prebattle") {
        const double next = received + double(*start) / 1000. - one_way;
        link->combat_deadline = !link->combat_deadline || std::abs(*link->combat_deadline - next) > .25 ? next : *link->combat_deadline * .8 + next * .2;
        projected = *link->combat_deadline + seconds;
    } else if (phase == "battle") {
        if (!link->combat_deadline) link->combat_deadline = received - one_way;
        projected = received + double(*remaining) / 1000. - one_way;
    } else projected = phase == "finished" ? received - one_way : received + seconds - one_way;
    link->combat_phase = phase;
    link->combat_duration = seconds;
    link->combat_end_deadline = !link->combat_end_deadline || std::abs(*link->combat_end_deadline - projected) > .25
                                    ? projected : *link->combat_end_deadline * .8 + projected * .2;
    link->timing_round = round;
    link->timing_tick = *tick;
    return true;
}

// Detached turret rows accumulate per round; a newer motion_seq replaces an actor's row.
void adoptTurrets(Value& message) {
    const auto round = exact(at(message, "round_id"));
    if (link->turret_round != round) { link->turret_round = round; link->turrets.clear(); }
    const Value& incoming = at(message, "detached_turrets").resolved();
    if (incoming.kind == K::Array)
        for (std::size_t i = 0; i < incoming.array.size() && i < MAX_TURRETS; ++i) {
            const Value& row = incoming.array[i].resolved();
            const std::string kind = safeText(at(row, "actor_kind"));
            const auto actor = ranged(at(row, "actor_id"), 1, MAX_ID);
            if (row.kind != K::Object || (kind != "bot" && kind != "player") || !actor || !ranged(at(row, "created_time_ms"), 0, MAX_ID)) continue;
            const std::string key = kind + ":" + std::to_string(*actor);
            const auto previous = link->turrets.find(key);
            if ((previous == link->turrets.end() && link->turrets.size() < MAX_TURRETS) ||
                (previous != link->turrets.end() && exact(at(row, "motion_seq")).value_or(0) > exact(at(previous->second, "motion_seq")).value_or(0)))
                link->turrets[key] = row;
        }
    if (!has(message, "detached_turrets") && link->turrets.empty()) return;
    Value list = array();
    for (const auto& entry : link->turrets) list.array.push_back(entry.second);
    message.object["detached_turrets"] = std::move(list);
}

// ---- dispatch ------------------------------------------------------------------------------
void notify(const std::string& kind, std::shared_ptr<const Value> message) {
    if (link->handler) link->handler(kind, message);
}
void notify(const std::string& kind, Value message) { notify(kind, std::make_shared<const Value>(std::move(message))); }

bool welcome(const Value& m) {
    const auto mine = capabilities(at(m, "capabilities")), server = capabilities(has(m, "server_capabilities") ? at(m, "server_capabilities") : array());
    const auto revision = exact(at(m, "state_revision")), round = exact(at(m, "round_id")), host = exact(at(m, "host_player_id")),
               epoch = ranged(at(m, "authority_epoch"), 0, MAX_ID), team_size = exact(at(m, "team_size")), protocol = exact(at(m, "protocol"));
    const auto server_time = has(m, "server_time_ms") ? ranged(at(m, "server_time_ms"), 0, MAX_ID) : std::nullopt;
    const std::string phase = safeText(at(m, "phase"), 16), map = safeText(at(m, "map"));
    auto both = [&](const char* name) { return mine && server && mine->count(name) && server->count(name); };
    if (!protocol || *protocol <= 0 || safeText(at(m, "role")) != "simulation_worker" || exact(at(m, "worker_id")) != AUTHORITY_ID ||
        !mine || !server || !server->count("destructible_catalog_v5") || !mine->count("projectile_ledger_v2") ||
        !mine->count("simulation_worker_v1") || !both("ram_contact_ledger_v2") || !both("human_ram_timeline_v1") ||
        !both("player_projectile_owner_v1") || !both("player_environment_v2") || !both("effective_params_v1") ||
        !both("ricochet_continuation_v1") || !revision || *revision < 0 || !round || *round < 0 || (host && *host <= 0) || !epoch ||
        !server_time || !team_size || *team_size < 1 || *team_size > 15 || exact(at(m, "bot_authority_id")) != AUTHORITY_ID ||
        (phase != "waiting" && phase != "loading" && phase != "battle") || map.empty()) {
        fail("invalid worker welcome");
        return false;
    }
    link->s.ready = true;
    link->s.phase = phase;
    link->map = map;
    link->s.round_id = round;
    link->state_revision = revision;
    link->host_player_id = host;
    link->s.bot_authority_id = AUTHORITY_ID;
    link->s.authority_epoch = epoch;
    link->server_time_ms = server_time;
    link->s.capabilities = *mine;
    link->s.server_capabilities = *server;
    link->negotiated = true;
    notify("welcome", m);
    return true;
}

bool roster(const Value& m) {
    const auto protocol = exact(at(m, "protocol")), round = exact(at(m, "round_id")), revision = exact(at(m, "state_revision")),
               host = exact(at(m, "host_player_id")), epoch = ranged(at(m, "authority_epoch"), 0, MAX_ID);
    const auto server_time = ranged(at(m, "server_time_ms"), 0, MAX_ID);
    const std::string phase = safeText(at(m, "phase"), 16), map = safeText(at(m, "map"));
    const bool listed = rows(at(m, "players"), 64);
    Value players = listed ? at(m, "players").resolved() : array();
    Staged staged = listed ? prepare(players, false) : Staged();
    std::set<std::int64_t> ids;
    bool compacts = true;
    for (const auto& row : players.array) {
        if (const auto id = exact(at(row, "id"))) ids.insert(*id);
        compacts = compacts && compactDescriptor(at(row, "vehicle_compact_descr"));
    }
    const bool any = !players.array.empty();
    if (!protocol || *protocol <= 0 || !round || *round < 0 || !revision || *revision < 0 ||
        (phase != "waiting" && phase != "loading" && phase != "battle") || map.empty() || !listed || !staged.outfits_valid || !compacts ||
        !staged.params_valid || (any && (!host || !ids.count(*host))) || (!any && host) || exact(at(m, "bot_authority_id")) != AUTHORITY_ID ||
        !epoch || (has(m, "server_time_ms") && !server_time)) {
        fail("invalid worker roster");
        return false;
    }
    if (link->s.round_id && *round < *link->s.round_id) return false;
    if (round == link->s.round_id && link->state_revision && *revision < *link->state_revision) return false;
    // A same-round waiting roster may overtake the loading barrier on another server thread; never demote for it.
    if (round == link->s.round_id && (link->s.phase == "loading" || link->s.phase == "battle") && phase == "waiting") return false;
    if (link->s.authority_epoch && *epoch < *link->s.authority_epoch) {
        fail("worker authority epoch regressed");
        return false;
    }
    const bool same_round = round == link->s.round_id;
    if (!same_round) {
        link->last_snapshot.reset();
        link->start_round.reset();
        link->live_round.reset();
        link->dummy = Value();
        link->server_time_ms.reset();
    }
    link->s.round_id = round;
    link->state_revision = revision;
    link->s.phase = phase;
    link->map = map;
    commit(staged);
    link->roster = std::move(players);
    link->host_player_id = host;
    link->s.bot_authority_id = AUTHORITY_ID;
    link->s.authority_epoch = epoch;
    Value out = m;
    if (server_time) {
        std::int64_t effective = *server_time;
        if (same_round && link->server_time_ms) effective = std::max(effective, *link->server_time_ms);
        out.object["server_time_ms"] = integer(effective);
        link->server_time_ms = effective;
    }
    notify("roster", std::move(out));
    return true;
}

// Real players (static inputs restored), then the worker's own carrier row.
std::optional<Staged> projectPlayers(Value& m, bool lean) {
    if (!rows(at(m, "players"), 63)) return std::nullopt;
    Value& players = m.object["players"];
    Staged staged = prepare(players, lean);
    if (!staged.params_valid) return std::nullopt;
    for (const auto& row : players.array) if (!compactDescriptor(at(row, "vehicle_compact_descr"))) return std::nullopt;
    return staged;
}

void battleStart(Value m) {
    if (exact(at(m, "bot_authority_id")) != AUTHORITY_ID) { fail("worker authority identity changed"); return; }
    auto projected = projectPlayers(m, false);
    if (!projected) { fail("worker player descriptor is unavailable"); return; }
    const auto round = exact(at(m, "round_id")), revision = exact(at(m, "state_revision"));
    if (!round || !revision || *revision < 0) { fail("invalid battle_start message"); return; }
    if (link->s.round_id && *round < *link->s.round_id) return;
    // A newer roster overtook this start barrier: deliver the start once, with that roster.
    if (round == link->s.round_id && link->state_revision && *revision < *link->state_revision) {
        if (link->start_round == round) return;
        m.object["state_revision"] = integer(*link->state_revision);
        if (!link->map.empty()) m.object["map"] = text(link->map);
        if (!link->roster.array.empty()) m.object["players"] = link->roster;
        if (link->host_player_id) m.object["host_player_id"] = integer(*link->host_player_id);
        m.object["bot_authority_id"] = integer(AUTHORITY_ID);
        if (link->s.authority_epoch) m.object["authority_epoch"] = integer(*link->s.authority_epoch);
        if (link->server_time_ms) m.object["server_time_ms"] = integer(*link->server_time_ms);
    }
    link->dummy = Value();
    const auto dummy = dummyPlayer(at(m, "players"));
    if (!dummy) { fail("worker runtime projection is unavailable"); return; }
    writable(m.object["players"]).array.push_back(*dummy);
    Value spawn = object();
    for (const char* axis : {"x", "y", "z", "yaw"}) spawn.object[axis] = at(*dummy, axis);
    m.object["spawn"] = std::move(spawn);
    m.object["vehicle"] = at(*dummy, "vehicle");
    const std::string map = safeText(at(m, "map")), phase = safeText(at(m, "phase"));
    const auto host = exact(at(m, "host_player_id")), epoch = ranged(at(m, "authority_epoch"), 0, MAX_ID);
    const auto start_time = has(m, "server_time_ms") ? ranged(at(m, "server_time_ms"), 0, MAX_ID) : std::nullopt;
    std::set<std::int64_t> ids;
    for (const auto& row : at(m, "players").resolved().array) if (const auto id = exact(at(row, "id"))) ids.insert(*id);
    if (phase != "loading" || map.empty() || !host || !ids.count(*host) || !ids.count(AUTHORITY_ID) || !epoch ||
        (round == link->s.round_id && link->s.authority_epoch && *epoch < *link->s.authority_epoch) || (has(m, "server_time_ms") && !start_time)) {
        fail("invalid battle_start message");
        return;
    }
    if (round != link->s.round_id) {
        link->last_snapshot.reset();
        link->start_round.reset();
        link->live_round.reset();
        link->timing_round.reset();
        link->timing_tick = -1;
        link->combat_deadline.reset();
        link->combat_end_deadline.reset();
        link->server_time_ms.reset();
    }
    link->s.phase = phase;
    link->map = map;
    link->s.round_id = round;
    link->state_revision = *revision;
    commit(*projected);
    // The roster keeps the real players; the carrier row is the last one.
    link->roster = at(m, "players").resolved();
    link->roster.array.pop_back();
    link->host_player_id = host;
    link->start_round = round;
    link->s.bot_authority_id = AUTHORITY_ID;
    link->s.authority_epoch = epoch;
    if (start_time) link->server_time_ms = start_time;
    adoptTurrets(m);
    notify("battle_start", std::move(m));
}

void battleLive(Value m) {
    const auto round = exact(at(m, "round_id")), revision = exact(at(m, "state_revision")), epoch = ranged(at(m, "authority_epoch"), 0, MAX_ID);
    const double countdown = finite(at(m, "countdown_seconds"), -1.), duration = finite(at(m, "battle_duration_seconds"), -1.);
    if (!round || round != link->s.round_id || (link->s.phase != "loading" && link->s.phase != "battle") || !revision || *revision < 0 ||
        exact(at(m, "bot_authority_id")) != AUTHORITY_ID || !epoch || (link->s.authority_epoch && *epoch < *link->s.authority_epoch) ||
        countdown < 0. || duration <= 0.) {
        fail("invalid battle_live message");
        return;
    }
    if (!loadTiming(m)) { fail("invalid battle timing"); return; }
    if (link->live_round == round) return;
    link->s.phase = "battle";
    if (!link->state_revision || *revision > *link->state_revision) link->state_revision = revision;
    link->live_round = round;
    link->s.authority_epoch = epoch;
    notify("battle_live", std::move(m));
}

// A live payload's round: older is dropped, a future one without its start barrier is a lineage conflict.
bool runtimeRound(const Value& m, const char* error) {
    const auto round = exact(at(m, "round_id"));
    if (!round) { fail(error); return false; }
    if (link->s.round_id && *round < *link->s.round_id) return false;
    if (round != link->s.round_id) { fail(error); return false; }
    return true;
}

void events(Value m) {
    if (!runtimeRound(m, "invalid events message")) return;
    const auto tick = exact(at(m, "server_tick")), server_time = ranged(at(m, "server_time_ms"), 0, MAX_ID),
               epoch = ranged(at(m, "authority_epoch"), 0, MAX_ID);
    if (epoch && link->s.authority_epoch && *epoch < *link->s.authority_epoch) return;
    if (!tick || *tick < 0 || !rows(at(m, "events"), 256) || !server_time || !epoch) { fail("invalid events message"); return; }
    std::vector<std::pair<std::optional<std::int64_t>, std::int64_t>> updates;
    for (const auto& event : at(m, "events").resolved().array) {
        const std::string kind = safeText(at(event, "kind"));
        if (kind != "authority" && kind != "bot_authority") continue;
        const auto event_epoch = ranged(at(event, "authority_epoch"), 0, MAX_ID);
        const Value& id = at(event, "player_id").resolved();
        const auto parsed = exact(id);
        if (parsed && *parsed != AUTHORITY_ID) { fail("invalid bot authority event"); return; }
        if (!parsed && !(id.kind == K::Null && exact(at(m, "bot_authority_id")) == AUTHORITY_ID)) { fail("invalid bot authority event"); return; }
        if (!event_epoch || (link->s.authority_epoch && *event_epoch < *link->s.authority_epoch) || *event_epoch > *epoch) {
            fail("invalid bot authority event");
            return;
        }
        updates.emplace_back(parsed, *event_epoch);
    }
    for (const auto& update : updates) { link->s.bot_authority_id = update.first; link->s.authority_epoch = update.second; }
    link->s.authority_epoch = epoch;
    link->server_time_ms = server_time;
    adoptTurrets(m);
    notify("events", std::move(m));
}

// Snapshot admission: identity, ordering and the actor rows the worker's battle reads.
void snapshot(Value m) {
    if (exact(at(m, "bot_authority_id")) != AUTHORITY_ID) { fail("worker authority identity changed"); return; }
    auto projected = projectPlayers(m, true);
    if (!projected) { fail("worker player descriptor is unavailable"); return; }
    const auto round = exact(at(m, "round_id"));
    if (round && link->s.round_id && *round < *link->s.round_id) return;
    const auto protocol = exact(at(m, "protocol"));
    if (!protocol || (link->negotiated ? *protocol <= 0 : *protocol != PROTOCOL)) { fail("protocol mismatch"); return; }
    if (!round || round != link->s.round_id) { fail("invalid snapshot message"); return; }
    auto server_time = ranged(at(m, "server_time_ms"), 0, MAX_ID);
    if (has(m, "server_time_ms") && !server_time) { fail("invalid server time"); return; }
    if (server_time && link->server_time_ms && *server_time < *link->server_time_ms) {
        server_time = link->server_time_ms;
        m.object["server_time_ms"] = integer(*server_time);
    }
    const auto tick = exact(at(m, "server_tick")), epoch = ranged(at(m, "authority_epoch"), 0, MAX_ID),
               projectile_revision = ranged(at(m, "projectile_revision"), 0, MAX_ID), bot_revision = ranged(at(m, "bot_state_revision"), 0, MAX_ID);
    const bool has_motion = has(m, "motion_time_us"), has_bot = has(m, "bot_state_time_us");
    const auto motion = has_motion ? ranged(at(m, "motion_time_us"), 0, MAX_MOTION) : std::nullopt;
    const auto bot_time = has_bot ? ranged(at(m, "bot_state_time_us"), 0, MAX_MOTION) : std::nullopt;
    const Value* prior = link->last_snapshot && exact(at(*link->last_snapshot, "round_id")) == round ? link->last_snapshot.get() : nullptr;
    const auto previous_revision = prior ? ranged(at(*prior, "bot_state_revision"), 0, MAX_ID) : std::nullopt;
    bool motion_valid = has_motion == has_bot && (!has_motion || (motion && bot_time && *bot_time <= *motion));
    if (motion_valid && prior) {
        const bool old_motion = has(*prior, "motion_time_us"), old_bot = has(*prior, "bot_state_time_us");
        if (old_motion != old_bot) motion_valid = false;
        else if (old_motion) {
            const auto previous_motion = ranged(at(*prior, "motion_time_us"), 0, MAX_MOTION), previous_bot = ranged(at(*prior, "bot_state_time_us"), 0, MAX_MOTION);
            motion_valid = has_motion && previous_motion && previous_bot && *motion >= *previous_motion && *bot_time >= *previous_bot;
        }
    }
    const bool valid_projectiles = server_time && epoch && projectile_revision &&
                                   (!link->s.authority_epoch || *epoch >= *link->s.authority_epoch) &&
                                   projectiles(at(m, "projectiles"), *epoch, *server_time);
    const Value& retained_manifest = prior ? at(*prior, "bot_manifest") : none;
    const bool lean = !has(m, "bot_manifest") && link->s.server_capabilities.count("lean_snapshot_manifest_v1") &&
                      retained_manifest.resolved().kind == K::Array;
    // The worker's carrier row joins the real players before the actor rows are admitted.
    const auto dummy = dummyPlayer(at(m, "players"));
    if (!dummy) { fail("worker runtime projection is unavailable"); return; }
    writable(m.object["players"]).array.push_back(*dummy);
    std::vector<std::string> reasons;
    long retained = 0;
    Value player_rows, bot_rows;
    if (!rows(at(m, "bots"), 30)) reasons.push_back("bots");
    else { auto canonical = canonicalRows(std::move(m.object["bots"]), "bots", &m); bot_rows = std::move(canonical.first); retained += canonical.second; }
    { auto canonical = canonicalRows(std::move(m.object["players"]), "players", nullptr); player_rows = std::move(canonical.first); retained += canonical.second; }
    if (!tick || *tick < 0) reasons.push_back("server_tick");
    if (!bot_revision) reasons.push_back("bot_state_revision");
    else if (previous_revision && *bot_revision < *previous_revision) reasons.push_back("bot_state_revision_regressed");
    if (!motion_valid) reasons.push_back("motion_timing");
    if (!valid_projectiles) reasons.push_back("projectiles");
    if (!projected->outfits_valid) reasons.push_back("player_outfits");
    if (has(m, "bot_manifest") && !rows(at(m, "bot_manifest"), 30)) reasons.push_back("bot_manifest");
    if (!has(m, "bot_manifest") && !lean) reasons.push_back("bot_manifest_missing");
    if (has(m, "bot_orders") && (!rows(at(m, "bot_orders"), 30) || !exact(at(m, "bot_order_revision")) || *exact(at(m, "bot_order_revision")) < 0))
        reasons.push_back("bot_orders");
    if (has(m, "destructibles") &&
        (!rows(at(m, "destructibles"), 4096) || !exact(at(m, "destructible_revision")) || *exact(at(m, "destructible_revision")) < 0))
        reasons.push_back("destructibles");
    if (!reasons.empty()) {
        std::string joined;
        for (const auto& reason : reasons) joined += (joined.empty() ? "" : ",") + reason;
        log("snapshot rejected reasons=" + joined + " round=" + std::to_string(*round) + " tick=" + std::to_string(tick.value_or(-1)));
        fail("invalid snapshot message");
        return;
    }
    if (has(m, "timing") && !loadTiming(m)) { fail("invalid battle timing"); return; }
    commit(*projected);
    // Each actor row is one shared version for the battle's readers.
    for (Value* list : {&player_rows, &bot_rows})
        for (auto& row : list->array) if (!row.reference) row = shared(std::make_shared<const Value>(std::move(row)));
    m.object["players"] = std::move(player_rows);
    m.object["bots"] = std::move(bot_rows);
    m.object.erase("bot_equipment_contracts");
    if (lean) m.object["bot_manifest"] = retained_manifest;
    else {
        // Rows shared by later lean snapshots of this lineage.
        Value& manifest = m.object["bot_manifest"];
        for (auto& row : manifest.array) if (!row.reference) row = shared(std::make_shared<const Value>(std::move(row)));
    }
    if (retained) log("degraded snapshot round=" + std::to_string(*round) + " tick=" + std::to_string(tick.value_or(-1)) +
                      " retained=" + std::to_string(retained));
    adoptTurrets(m);
    auto accepted = std::make_shared<const Value>(std::move(m));
    link->last_snapshot = accepted;
    if (server_time) link->server_time_ms = server_time;
    link->s.bot_authority_id = AUTHORITY_ID;
    link->s.authority_epoch = epoch;
    notify("snapshot", accepted);
}

void observation(Value m) {
    if (!runtimeRound(m, "invalid bot observation message")) return;
    if (!rows(at(m, "contacts"), 64)) { fail("invalid bot observation message"); return; }
    if (has(m, "server_time_ms")) link->server_time_ms = ranged(at(m, "server_time_ms"), 0, MAX_ID);
    notify("bot_observation", std::move(m));
}

void contact(Value m) {
    const Value& player = at(m, "player").resolved();
    const auto id = player.kind == K::Object ? exact(at(player, "id")) : std::nullopt;
    const auto params = id && *id > 0 ? link->params.find(*id) : link->params.end();
    if (params == link->params.end()) { fail("worker player effective parameters are unavailable"); return; }
    Value row = player;
    row.object["effective_params"] = params->second.canonical;
    m.object["player"] = std::move(row);
    notify("player_destructible_contact", std::move(m));
}

double frameLatency(double now) {
    if (link->frames.empty() || link->frame_scope != std::make_pair(link->s.round_id, link->s.authority_epoch)) return -1.;
    return 1000. * std::max(link->frame_seconds / double(link->frames.size()), now - link->last_frame);
}

// One message after the protocol, round and server-clock checks of LANClient._handle_message.
void handle(Value m, double now) {
    const std::string kind = safeText(at(m, "type"));
    if (kind == "worker_ping") {
        const auto player = exact(at(m, "player_id")), seq = exact(at(m, "seq"));
        if (!player || *player <= 0 || !seq || *seq <= 0) return;
        Value pong = object();
        pong.object["type"] = text("worker_pong");
        pong.object["player_id"] = integer(*player);
        pong.object["seq"] = integer(*seq);
        const double frame = frameLatency(now);
        pong.object["frame_ms"] = frame > 0. ? number(frame) : Value();
        pong.object["round_id"] = at(m, "round_id");
        pong.object["authority_epoch"] = at(m, "authority_epoch");
        sendWire(pong);
        return;
    }
    static const std::set<std::string> recoverable = {"snapshot", "events", "bot_observation"};
    static const std::set<std::string> state_types = {"welcome", "roster", "battle_start", "battle_live", "snapshot", "events",
                                                      "bot_observation", "player_destructible_contact"};
    const auto round = exact(at(m, "round_id"));
    if (recoverable.count(kind) && link->s.round_id && round && *round < *link->s.round_id) return;
    if (has(m, "protocol") || state_types.count(kind)) {
        const auto protocol = exact(at(m, "protocol"));
        const bool matches = kind == "welcome" || link->negotiated ? protocol && *protocol > 0 : protocol == PROTOCOL;
        if (!matches) { fail("protocol mismatch"); return; }
    }
    if (round && round == link->s.round_id && has(m, "server_time_ms") && kind != "snapshot") {
        auto server_time = ranged(at(m, "server_time_ms"), 0, MAX_ID);
        if (!server_time) { fail("invalid server time"); return; }
        if (link->server_time_ms && *server_time < *link->server_time_ms) {
            server_time = link->server_time_ms;
            m.object["server_time_ms"] = integer(*server_time);
        }
        if (!recoverable.count(kind)) link->server_time_ms = server_time;
    }
    if (kind == "welcome") welcome(m);
    else if (kind == "roster") roster(m);
    else if (kind == "battle_start") battleStart(std::move(m));
    else if (kind == "battle_live") battleLive(std::move(m));
    else if (kind == "snapshot") snapshot(std::move(m));
    else if (kind == "events") events(std::move(m));
    else if (kind == "bot_observation") observation(std::move(m));
    else if (kind == "player_destructible_contact") contact(std::move(m));
    else if (kind == "pong") {
        const double client_time = finite(at(m, "client_time"), 0.);
        if (client_time <= 0.) return;
        const double sample = std::max(0., (finite(at(m, "_client_received_time"), now) - client_time) * 1000.);
        link->rtt_ms = link->rtt_ms ? *link->rtt_ms * .75 + sample * .25 : sample;
        if (!link->minimum_rtt_ms || sample < *link->minimum_rtt_ms) link->minimum_rtt_ms = sample;
    } else if (kind == "error") {
        Value error = object();
        error.object["message"] = text(safeText(at(m, "message")).empty() ? (safeText(at(m, "code")).empty() ? "server error" : safeText(at(m, "code")))
                                                                         : safeText(at(m, "message")));
        error.object["code"] = text(safeText(at(m, "code"), 32));
        notify("error", std::move(error));
    }
}

// The snapshot lineage that one poll may coalesce: round, epoch, authority and map.
std::optional<std::array<Value, 4>> lineage(const Value& m) {
    if (safeText(at(m, "type")) != "snapshot") return std::nullopt;
    return std::array<Value, 4>{at(m, "round_id"), at(m, "authority_epoch"), at(m, "bot_authority_id"), at(m, "map")};
}
// A later lean snapshot supersedes motion but keeps a pending order section and the measured producer cadence.
Value mergeOrders(const Value* previous, Value message) {
    if (!previous || lineage(*previous) != lineage(message)) return message;
    Value timing = object();
    for (const Value* source : {previous, static_cast<const Value*>(&message)}) {
        const Value& measured = at(*source, "_client_coalesced_timing").resolved();
        if (measured.kind != K::Object) continue;
        for (const char* name : {"source_interval_us", "snapshot_interval_us"})
            if (const auto value = ranged(at(measured, name), 1, MAX_MOTION))
                timing.object[name] = integer(std::max(exact(at(timing, name)).value_or(0), *value));
    }
    for (const auto& pair : {std::make_pair("bot_state_time_us", "source_interval_us"), std::make_pair("motion_time_us", "snapshot_interval_us")}) {
        const auto first = ranged(at(*previous, pair.first), 0, MAX_MOTION), last = ranged(at(message, pair.first), 0, MAX_MOTION);
        if (first && last && *last > *first) timing.object[pair.second] = integer(std::max(exact(at(timing, pair.second)).value_or(0), *last - *first));
    }
    if (!timing.object.empty()) message.object["_client_coalesced_timing"] = std::move(timing);
    const auto previous_revision = has(*previous, "bot_orders") ? exact(at(*previous, "bot_order_revision")) : std::nullopt;
    if (!previous_revision) return message;
    const auto revision = has(message, "bot_orders") ? exact(at(message, "bot_order_revision")) : std::nullopt;
    if (has(message, "bot_orders") && (!revision || *revision >= *previous_revision)) return message;
    message.object["bot_orders"] = at(*previous, "bot_orders");
    message.object["bot_order_revision"] = integer(*previous_revision);
    return message;
}

void dispatch(Value m, double now) {
    const double received = finite(at(m, "_client_received_time"), now);
    m.object["_client_dispatch_delay"] = number(std::max(0., now - received));
    handle(std::move(m), now);
}
// One received batch: only the newest snapshot of a lineage is handled, except at manifest, turret and event barriers.
void dispatchBatch(std::vector<Value>& batch, double now) {
    std::optional<Value> latest;
    for (auto& message : batch) {
        if (!link->running) return;
        const std::string kind = safeText(at(message, "type"));
        if (kind == "snapshot") {
            if (latest && ((has(*latest, "bot_manifest") && !has(message, "bot_manifest")) ||
                           (at(*latest, "detached_turrets").truth() && at(*latest, "detached_turrets") != at(message, "detached_turrets")))) {
                dispatch(std::move(*latest), now);
                latest.reset();
            }
            latest = mergeOrders(latest ? &*latest : nullptr, std::move(message));
        } else if (kind == "events" && latest && at(message, "round_id") == at(*latest, "round_id") &&
                   at(message, "server_tick") == at(*latest, "server_tick")) {
            dispatch(std::move(message), now);
            dispatch(std::move(*latest), now);
            latest.reset();
        } else {
            if (latest) { dispatch(std::move(*latest), now); latest.reset(); }
            dispatch(std::move(message), now);
        }
    }
    if (latest && link->running) dispatch(std::move(*latest), now);
}

// Take received frames and dispatch them; the transport ending or a refused message reports one error.
void drainReceived() {
    if (!link || !link->net || link->dispatch_depth) return;
    ++link->dispatch_depth;
    std::vector<stock_wire_net::Frame> frames;
    const auto status = stock_wire_net::receive(*link->net, MAX_RECEIVE, frames);
    const double now = clock();
    std::vector<Value> batch;
    batch.reserve(frames.size());
    for (auto& frame : frames) {
        if (!frame.message) { fail("invalid battle frame: " + frame.error); break; }
        Value message = std::move(*frame.message);
        message.object["_client_received_time"] = number(now - frame.age);
        batch.push_back(std::move(message));
    }
    if (link->running) dispatchBatch(batch, now);
    --link->dispatch_depth;
    if (status.state == "connected" && !link->connected && link->running) link->connected = true;
    if ((status.state == "closed" || status.state == "failed") && link->running) {
        link->connected = false;
        fail(status.state == "closed" ? "server closed the connection" : (status.error.empty() ? "LAN transport failed" : status.error));
    }
}

void report() {
    if (!link || link->running || link->error.empty()) return;
    Value error = object();
    error.object["message"] = text(link->error);
    link->error.clear();
    notify("error", std::move(error));
}

PyObject* pollEntry(PyObject* self, PyObject*);
PyMethodDef pollDef = {"worker_link_poll", pollEntry, METH_NOARGS, nullptr};
void schedulePoll() {
    if (!link || !link->running || link->poll_id) return;
    p::Ref self(Py_BuildValue("(K)", static_cast<unsigned long long>(link->generation)));
    p::Ref fn(PyCFunction_New(&pollDef, self.get()));
    link->poll_id = own(py::call(link->bigworld.get(), "callback", p::number(POLL_SECONDS).get(), fn.get()));
}
PyObject* pollEntry(PyObject* self, PyObject*) {
    if (!link || PyLong_AsUnsignedLongLong(PyTuple_GET_ITEM(self, 0)) != link->generation) Py_RETURN_NONE;
    link->poll_id = Object();
    try {
        drainReceived();
        const double now = clock();
        if (link->connected && link->running && now - link->last_ping >= PING_SECONDS) {
            link->last_ping = now;
            Value ping = object();
            ping.object["type"] = text("ping");
            ping.object["seq"] = integer(++link->ping_seq);
            ping.object["client_time"] = number(now);
            sendWire(ping);
        }
        report();
        schedulePoll();
    } catch (const std::exception& e) {
        log("worker link poll failed: " + describe(e));
        if (link) { fail(describe(e)); report(); }
    }
    Py_RETURN_NONE;
}
}

// ---- session and battle entry points -------------------------------------------------------
bool connect(PyObject* bigworld, const std::string& host, int port, Handler handler) {
    const std::uint64_t generation = link ? link->generation + 1 : 1;
    disconnect(false);
    link.reset(new Link);
    link->generation = generation;
    link->bigworld = borrow(bigworld);
    link->handler = std::move(handler);
    link->running = true;
    link->net = stock_wire_net::open(host, port, 3.0);
    // The server requires hello to be the first frame on the wire.
    Value hello = object();
    hello.object["type"] = text("hello");
    auto built = build(link->s, hello);
    std::string payload, error;
    if (!built || !stock_wire::encode_value(built->wire, payload, error) ||
        stock_wire_net::send(*link->net, std::move(payload), "hello", std::nullopt, MAX_OUTBOUND_MESSAGES, MAX_OUTBOUND_BYTES) ==
            stock_wire_net::Queued::refused) {
        disconnect(false);
        throw p::Error("worker hello could not be queued" + (error.empty() ? std::string() : ": " + error));
    }
    schedulePoll();
    return true;
}

void disconnect(bool flush) {
    if (!link) return;
    ++link->generation;
    if (link->poll_id && link->poll_id.get() != Py_None) {
        PyObject* done = PyObject_CallMethod(link->bigworld.get(), const_cast<char*>("cancelCallback"), const_cast<char*>("O"), link->poll_id.get());
        if (done) Py_DECREF(done); else PyErr_Clear();
    }
    link->poll_id = Object();
    link->handler = nullptr;
    if (link->net) {
        if (link->connected) {
            Value leave = object();
            leave.object["type"] = text("leave");
            std::string payload, error;
            if (stock_wire::encode_value(leave, payload, error))
                stock_wire_net::send(*link->net, std::move(payload), "leave", std::nullopt, MAX_OUTBOUND_MESSAGES * 2, MAX_OUTBOUND_BYTES * 2);
        }
        stock_wire_net::close(*link->net, flush ? 0.5 : 0.);
    }
    link->net.reset();
    link->connected = link->running = false;
}

bool connected() { return link && link->connected; }
std::string phase() { return link ? link->s.phase : std::string(); }
std::optional<std::int64_t> botAuthorityId() { return link ? link->s.bot_authority_id : std::nullopt; }
void dropAuthority() { if (link) link->s.bot_authority_id.reset(); }
std::int64_t team() { return link ? link->team : 1; }
std::optional<std::int64_t> team(const Battle&) { return link ? std::optional<std::int64_t>(link->team) : std::nullopt; }
std::shared_ptr<const Value> lastSnapshot() { return link ? link->last_snapshot : nullptr; }
double now() { return clock(); }
std::optional<double> rttMs() { return link ? link->rtt_ms : std::nullopt; }
std::optional<double> minimumRttMs() { return link ? link->minimum_rtt_ms : std::nullopt; }

Timing timing() {
    Timing out;
    if (!link) return out;
    const double now = clock();
    if (link->combat_deadline) out.countdown = *link->combat_deadline - now;
    if (link->combat_end_deadline) out.remaining = *link->combat_end_deadline - now;
    out.duration = link->combat_duration;
    out.phase = link->combat_phase;
    return out;
}

void recordFrame(double seconds) {
    if (!link || !(seconds > 0.) || !std::isfinite(seconds)) return;
    const auto scope = std::make_pair(link->s.round_id, link->s.authority_epoch);
    if (scope != link->frame_scope) { link->frame_scope = scope; link->frames.clear(); link->frame_seconds = 0.; }
    link->frames.push_back(seconds);
    link->frame_seconds += seconds;
    while (link->frames.size() > 1 && link->frame_seconds - link->frames.front() >= 1.) { link->frame_seconds -= link->frames.front(); link->frames.pop_front(); }
    link->last_frame = clock();
}

bool send(Battle&, const Value& message) {
    if (!link || !link->running) return false;
    auto built = build(link->s, message);
    return built && sendWire(built->wire, built->coalesce);
}

bool authority() {
    return link && link->s.ready && (link->s.phase == "loading" || link->s.phase == "battle") && link->s.bot_authority_id == AUTHORITY_ID;
}
std::optional<std::int64_t> roundId(const Battle&) { return link ? link->s.round_id : std::nullopt; }
std::optional<std::int64_t> authorityEpoch(const Battle&) { return link ? link->s.authority_epoch : std::nullopt; }
bool negotiated(const Battle&, const std::string& capability) {
    return link && link->s.capabilities.count(capability) && link->s.server_capabilities.count(capability);
}
void drain(Battle&) {
    if (!link || !link->running) return;
    try { drainReceived(); }
    catch (const std::exception& e) { fail(describe(e)); }
}
}
}
