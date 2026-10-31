// The ordered LAN event journal: intake, validation, state merges, readiness and in-order application.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_events_internal.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include <algorithm>
#include <set>

namespace native_battle {
bool EventIds::add(const std::string& id) {
    if (!ids_.insert(id).second) return false;
    order_.push_back(id);
    while (order_.size() > limit_) { ids_.erase(order_.front()); order_.pop_front(); }
    return true;
}

namespace events {
namespace {
const Value& field(const Value& v, const char* name) { return v.resolved().get(name); }
bool has(const Value& v, const char* name) {
    const auto& r = v.resolved();
    return r.kind == Value::Kind::Object && r.object.count(name);
}
const std::string& text(const Value& v) { return v.resolved().text; }
bool exactInteger(const Value& v) { return v.resolved().kind == Value::Kind::Integer; }
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
Value make(std::int64_t v) { Value x; x.kind = Value::Kind::Integer; x.integer = v; return x; }
Value make(bool v) { Value x; x.kind = Value::Kind::Boolean; x.boolean = v; return x; }
Value make(const std::string& v) { Value x; x.kind = Value::Kind::Text; x.text = v; return x; }
bool oneOf(const std::string& v, std::initializer_list<const char*> options) {
    for (const char* o : options) if (v == o) return true;
    return false;
}
bool shotKind(const std::string& k) { return k == "shot" || k == "bot_shot"; }
bool combatKind(const std::string& k) { return oneOf(k, {"health", "hit", "bot_hit", "bot_human_hit", "bot_bot_hit"}); }
bool simpleKind(const std::string& k) {
    return oneOf(k, {"authority", "bot_manifest", "vehicle_statistics", "destructible", "projectile_ricochet", "projectile_impact",
                     "battle_result", "assist", "stun", "detection", "environment_critical"});
}
bool anyHit(const std::string& k) { return oneOf(k, {"hit", "bot_hit", "bot_human_hit", "bot_bot_hit"}); }
}

// BattleRuntime._event_entity_key(event, role): 'bot:N' / 'player:N' or none.
std::optional<std::string> entityKey(const Value& event, bool attacker) {
    auto id = [](const Value& v) {
        const auto& r = v.resolved();
        if (r.kind == Value::Kind::Text) return r.text;
        return std::to_string(std::int64_t(r.numeric()));
    };
    const char* bot = attacker ? "attacker_bot" : "target_bot";
    const char* player = attacker ? "attacker" : "target";
    if (field(event, bot).resolved().kind != Value::Kind::Null) return "bot:" + id(field(event, bot));
    if (field(event, player).resolved().kind != Value::Kind::Null) return "player:" + id(field(event, player));
    return {};
}

std::string stunKey(const Value& event) {
    const std::string kind = text(field(event, "target_kind"));
    const auto& target = field(event, "target_id");
    if ((kind != "player" && kind != "bot") || !exactInteger(target) || integer(target) <= 0)
        throw p::Error("stun event has an invalid target identity");
    return kind + ":" + std::to_string(integer(target));
}


// The state an ordered event targets: a presented vehicle's, else a pending Bot create's.
Value& knownState(Battle& b, const std::string& key) {
    if (Actor* v = b.vehicle(key)) if (v->engine_id) return v->presented.state;
    auto pending = b.roster->pending_bot_creates.find(key);
    if (pending != b.roster->pending_bot_creates.end()) return writable(pending->second).object["state"];
    throw p::Error("ordered LAN event references unknown entity " + key);
}

bool references(const Battle& b, const std::string& key) {
    for (const auto& event : b.events->journal)
        if (entityKey(event, false) == key || entityKey(event, true) == key) return true;
    return false;
}

bool pendingCombatFor(const Battle& b, const Actor& vehicle) {
    for (const auto& event : b.events->journal) {
        if (!combatKind(text(field(event, "kind"))) || field(event, "cosmetic_only").truth()) continue;
        if (entityKey(event, false) == vehicle.key) return true;
    }
    return false;
}

namespace {
bool missingProjectileAttackerAllowed(Battle& b, const Value& event) {
    const std::string source = text(field(event, "source")), kind = text(field(event, "kind"));
    const bool hit = anyHit(kind);
    if (source == "fire" && hit) return true;
    if (source != "shot" || !hit) return false;
    const auto& projectile = field(event, "projectile_id");
    if (projectile.resolved().kind == Value::Kind::Null) return false;
    return projectiles::inLineage(b, projectile);
}

// A shot's attacker carries the shot camouflage penalty.
void mergeShotState(Battle& b, const Value& event) {
    const auto key = entityKey(event, true);
    if (!key) throw p::Error("ordered shot event has no attacker");
    const double deadline = b.clock() + visibility::shotCamouflageSeconds();
    if (Actor* v = b.vehicle(*key); v && v->engine_id) {
        v->presented.shot_penalty_until = deadline;
        world::vehicleChanged(b, *v);
        return;
    }
    auto& state = writable(knownState(b, *key));
    state.object["shot_penalty_until"].kind = Value::Kind::Number;
    state.object["shot_penalty_until"].number = deadline;
}

void combatState(Value& state, const Value& event, const std::string& target) {
    if (!has(event, "health")) throw p::Error("ordered combat event has no health");
    if (!has(event, "death_reason")) throw p::Error("ordered combat event has no death_reason");
    auto& out = writable(state);
    const std::int64_t health = std::max<std::int64_t>(0, integer(field(event, "health")));
    out.object["health"] = make(health);
    const bool alive = health > 0 && !field(event, "dead").truth();
    out.object["alive"] = make(alive);
    out.object["display_health"] = make(std::max<std::int64_t>(0, has(event, "display_health") ? integer(field(event, "display_health")) : health));
    const auto& reason = field(event, "death_reason");
    if (reason.resolved().kind != Value::Kind::Integer && reason.resolved().kind != Value::Kind::Number) throw p::Error("ordered combat event has invalid death_reason");
    const std::int64_t death_reason = integer(reason);
    if (death_reason < 0) throw p::Error("ordered combat event has invalid death_reason");
    if (alive && death_reason) throw p::Error("nonfatal combat event has nonzero death_reason");
    out.object["death_reason"] = make(death_reason);
    if (auto attacker = entityKey(event, true)) {
        const auto colon = attacker->find(':');
        out.object["death_attacker_kind"] = make(attacker->substr(0, colon));
        out.object["death_attacker_id"] = make(std::int64_t(std::stoll(attacker->substr(colon + 1))));
    }
    const auto& critical = field(event, "critical");
    if (critical.resolved().kind == Value::Kind::Object && target.rfind("bot:", 0) == 0) {
        Value canonical = critical.resolved();
        writable(canonical).object["events"].kind = Value::Kind::Array;
        canonical.object["events"].array.clear();
        out.object["critical"] = std::move(canonical);
    }
    for (const char* name : {"critical_revision", "critical_base_revision", "critical_ack_seq", "combat_revision", "combat_base_revision", "combat_ack_seq"})
        if (has(event, name)) out.object[name] = field(event, name);
}

void mergeCombatState(Battle& b, const Value& event) {
    const auto target = entityKey(event, false);
    if (!target) throw p::Error("ordered combat event has no target");
    Value& state = knownState(b, *target);
    if (auto attacker = entityKey(event, true)) {
        try { knownState(b, *attacker); }
        catch (const p::Error&) { if (!missingProjectileAttackerAllowed(b, event)) throw; }
    }
    combatState(state, event, *target);
}

void mergeStunState(Battle& b, const Value& event) {
    const std::string target = stunKey(event);
    Value& state = knownState(b, target);
    const auto& active = field(event, "active").resolved();
    const auto& end = field(event, "stun_end_server_time_ms");
    if (active.kind != Value::Kind::Boolean) throw p::Error("stun event has an invalid active state");
    if (!exactInteger(end) || integer(end) < 0 || active.boolean != (integer(end) > 0)) throw p::Error("stun event has an invalid end time");
    auto& out = writable(state);
    out.object["stun_end_server_time_ms"] = make(integer(end));
    Value factors; factors.kind = Value::Kind::Object;
    if (integer(end) && field(event, "stun_factors").resolved().kind == Value::Kind::Object) factors = field(event, "stun_factors").resolved();
    out.object["stun_factors"] = std::move(factors);
    if (active.boolean) {
        const std::string kind = text(field(event, "attacker_kind"));
        const auto& attacker = field(event, "attacker_id");
        if ((kind != "player" && kind != "bot") || !exactInteger(attacker) || integer(attacker) <= 0)
            throw p::Error("stun event has an invalid attacker identity");
        out.object["stun_attacker_kind"] = make(kind);
        out.object["stun_attacker_id"] = make(integer(attacker));
    } else {
        out.object["stun_attacker_kind"] = make(std::string());
        out.object["stun_attacker_id"] = make(std::int64_t(0));
    }
}
}

void combatEventState(Value& state, const Value& event, const std::string& target) { combatState(state, event, target); }

// BattleRuntime._combat_attack_reason: the attack reason a combat source permits; none for player_left.
std::optional<std::int64_t> combatAttackReason(Battle& b, const Value& event) {
    const std::string source = combatSource(event);
    if (source == "player_left") {
        if (!has(event, "attack_reason") || field(event, "attack_reason").resolved().kind != Value::Kind::Null)
            throw p::Error("player_left event must have null attack_reason");
        if (!has(event, "death_reason") || integer(field(event, "death_reason"), -1) != 0) throw p::Error("player_left event must have zero death_reason");
        if (has(event, "attacker") || has(event, "attacker_bot")) throw p::Error("player_left event must not have an attacker");
        return {};
    }
    if (!has(event, "attack_reason")) throw p::Error("ordered combat event has no attack_reason");
    const auto& raw = field(event, "attack_reason").resolved();
    if (raw.kind != Value::Kind::Integer && raw.kind != Value::Kind::Number) throw p::Error("ordered combat event has invalid attack_reason");
    const std::int64_t reason = integer(raw);
    if (reason < 0) throw p::Error("ordered combat event has invalid attack_reason");
    auto of = [&](const char* name, std::int64_t fallback) { return vehicles::attackReason(b, name, fallback); };
    if (source == "client_simulation") return reason;
    if (source == "environment") {
        const bool dead = field(event, "dead").truth();
        const std::int64_t world = of("WORLD_COLLISION", 3), drowning = of("DROWNING", 5), overturn = of("OVERTURN", 7);
        const std::int64_t death = integer(field(event, "death_reason"), -1);
        const bool valid = (reason == world && death == (dead ? world : 0)) || ((reason == drowning || reason == overturn) && dead && death == reason);
        if (!valid) throw p::Error("environment event has invalid cause");
        return reason;
    }
    const std::int64_t expected = source == "shot" ? of("SHOT", 0) : source == "fire" ? of("FIRE", 1) : of("RAM", 2);
    if (reason != expected)
        throw p::Error("ordered combat event attack_reason does not match source: " + std::to_string(reason) + " != " + std::to_string(expected));
    return reason;
}

// BattleRuntime._validate_combat_event_contract
void validateCombat(Battle& b, const Value& event) {
    const auto& cosmetic = field(event, "cosmetic_only").resolved();
    if (cosmetic.kind != Value::Kind::Null && cosmetic.kind != Value::Kind::Boolean) throw p::Error("combat cosmetic_only flag is invalid");
    if (cosmetic.kind == Value::Kind::Boolean && cosmetic.boolean) {
        const auto& damage = field(event, "damage").resolved();
        const auto& health = field(event, "health").resolved();
        const std::int64_t shot_result = integer(field(event, "shot_result"), -1);
        const bool bad = text(field(event, "source")) != "shot" || damage.kind != Value::Kind::Integer || damage.integer != 0 ||
                         field(event, "dead").truth() || integer(field(event, "death_reason"), -1) != 0 || health.kind != Value::Kind::Integer ||
                         health.integer <= 0 || (shot_result != 0 && shot_result != 1) || field(event, "splash").truth() ||
                         field(event, "critical").resolved().kind != Value::Kind::Null;
        if (bad) throw p::Error("state-changing combat cannot be cosmetic_only");
    }
    const std::string source = combatSource(event);
    combatAttackReason(b, event);
    const std::string kind = text(field(event, "kind"));
    const bool hit = anyHit(kind);
    bool allowed = false;
    if (source == "shot" || source == "ram") allowed = hit;
    else if (source == "fire") allowed = hit || kind == "health";
    else if (source == "client_simulation" || source == "player_left") allowed = kind == "health";
    else allowed = field(event, "push_attributed").resolved().kind == Value::Kind::Boolean && field(event, "push_attributed").resolved().boolean
                       ? (hit || kind == "health") : kind == "health";
    if (!allowed) throw p::Error("ordered combat event source " + source + " does not allow kind " + kind);
    const auto& blocked = field(event, "blocked_damage");
    const std::int64_t blocked_value = blocked.resolved().kind == Value::Kind::Null ? 0 : integer(blocked);
    if ((blocked.resolved().kind != Value::Kind::Null && !exactInteger(blocked)) || blocked_value < 0 || blocked_value > 5000)
        throw p::Error("ordered combat event has invalid blocked_damage");
    if (blocked_value && (source != "shot" || field(event, "splash").truth() || integer(field(event, "shot_result"), 2) == 2))
        throw p::Error("ordered combat event has inconsistent blocked_damage");
    if (has(event, "damage_sticker")) {
        const auto& sticker = field(event, "damage_sticker");
        // Stickers span the full u64 range (2**64 - 1), as the pinned check allowed.
        if (source != "shot" || field(event, "splash").truth() || !exactInteger(sticker) ||
            (!sticker.resolved().unsigned_integer && integer(sticker) < 0))
            throw p::Error("ordered combat event has invalid damage_sticker");
    }
    const auto attacker = entityKey(event, true);
    const bool unowned_fire = source == "fire" && kind == "health";
    if (unowned_fire && (has(event, "attacker") || has(event, "attacker_bot"))) throw p::Error("unowned fire event must not have an attacker");
    if ((source == "shot" || source == "fire" || source == "ram") && !unowned_fire && !attacker)
        throw p::Error("ordered " + source + " combat event has no attacker");
    if ((source == "client_simulation" || source == "player_left") && attacker) throw p::Error("ordered " + source + " combat event must not have an attacker");
    if (source == "environment" && attacker && !(field(event, "push_attributed").resolved().kind == Value::Kind::Boolean && field(event, "push_attributed").resolved().boolean))
        throw p::Error("environment attacker has no push attribution");
}

// BattleRuntime._combat_event_source (staticmethod)
std::string combatSource(const Value& event) {
    const auto& source = field(event, "source").resolved();
    if (source.kind == Value::Kind::Text && !source.text.empty()) return source.text;
    return "shot";
}

namespace {
void prepare(Battle& b, const Value& event) {
    const std::string kind = text(field(event, "kind"));
    if (shotKind(kind)) {
        mergeShotState(b, event);
        projectiles::rememberLineage(b, event);
    } else if (combatKind(kind)) {
        validateCombat(b, event);
        if (!field(event, "cosmetic_only").truth()) mergeCombatState(b, event);
    } else if (kind == "stun") {
        mergeStunState(b, event);
    } else if (!simpleKind(kind)) {
        throw p::Error("ordered LAN event kind is unsupported: " + kind);
    }
}

// BattleRuntime._record_is_event_ready (staticmethod)
bool vehicleReady(const Actor* v) {
    if (!v || !v->engine_id) return false;
    if (!v->ready) return false;
    if (v->presented.remote && !v->presented.arena_added && !v->presented.simulation_entity) return false;
    return true;
}
bool pendingCreate(const Battle& b, const std::string& key) { return b.roster->pending_bot_creates.count(key) != 0; }
const Actor* presented(const Battle& b, const std::string& key) {
    const Actor* v = b.vehicle(key);
    return v && v->engine_id ? v : nullptr;
}

// BattleRuntime._event_is_ready(event); `wait` marks a presentation wait that must not block unrelated events.
bool ready(Battle& b, Value& event, bool& wait) {
    const std::string kind = text(field(event, "kind"));
    if (kind == "environment_critical" && !b.worker) {
        for (bool attacker : {false, true}) {
            const auto key = entityKey(event, attacker);
            if (!key) continue;
            const Actor* v = presented(b, *key);
            if ((pendingCreate(b, *key) || v) && !vehicleReady(v)) return false;
        }
        return true;
    }
    if (!b.worker && !b.replay && (kind == "projectile_impact" || kind == "projectile_ricochet")) {
        if (auto due = projectiles::presentationDue(b, event)) {
            if (b.clock() < *due) { wait = true; return false; }
        }
    }
    auto lost = [](const std::string& key) { return p::Error("ordered LAN event lost entity " + key + " before apply"); };
    if (shotKind(kind)) {
        const auto key = entityKey(event, true);
        const Actor* v = key ? presented(b, *key) : nullptr;
        if (!v && !(key && pendingCreate(b, *key))) throw lost(key.value_or("None"));
        if (!vehicleReady(v)) return false;
        if (!b.worker && !b.replay && field(event, "attacker_bot").resolved().kind != Value::Kind::Null &&
            field(event, "bot_presentation_time_us").resolved().kind != Value::Kind::Null) {
            const auto& alive = field(v->presented.state, "alive").resolved();
            if (alive.kind == Value::Kind::Boolean && !alive.boolean) return true;
            const std::int64_t launch = integer(field(event, "bot_presentation_time_us"));
            const bool shown = v->presented.presented_time_us && *v->presented.presented_time_us >= launch;
            if (!shown) wait = true;
            return shown;
        }
        return true;
    }
    if (combatKind(kind)) {
        const auto target = entityKey(event, false);
        const Actor* v = target ? presented(b, *target) : nullptr;
        if (!v && !(target && pendingCreate(b, *target))) throw lost(target.value_or("None"));
        if (!vehicleReady(v)) return false;
        const auto attacker = entityKey(event, true);
        if (!attacker) return true;
        const Actor* a = presented(b, *attacker);
        if (!a && !pendingCreate(b, *attacker)) {
            if (missingProjectileAttackerAllowed(b, event)) return true;
            throw lost(*attacker);
        }
        return vehicleReady(a);
    }
    if (kind == "stun") {
        const std::string target = stunKey(event);
        const Actor* v = presented(b, target);
        if (!v && !pendingCreate(b, target)) throw lost(target);
        return vehicleReady(v);
    }
    return true;
}

// BattleRuntime._apply_ordered_event(event): false asks for a retry after the rest of the journal.
bool applyOrdered(Battle& b, const Value& event) {
    const std::string kind = text(field(event, "kind"));
    if (kind == "authority") {
        if (b.worker) bots::projectileEvent(b, event, b.clock());
        projectiles::setEpoch(b, field(event, "authority_epoch"), b.clock());
        bots::reconcileAuthority(b, field(event, "player_id"));
    } else if (shotKind(kind)) {
        showShot(b, event, false);
        projectiles::accept(b, event);
    } else if (combatKind(kind)) {
        applyCombat(b, event, false);
    } else if (kind == "vehicle_statistics") applyStatisticsEvent(b, event);
    else if (kind == "environment_critical") environmentCriticalFeedback(b, event);
    else if (kind == "assist") assist(b, event);
    else if (kind == "detection") detection(b, event);
    else if (kind == "stun") {
        Actor* v = b.vehicle(stunKey(event));
        if (!v || !v->engine_id) throw p::Error("stun event has no ready target");
        vehicles::applyStun(b, *v, v->presented.state);
    } else if (kind == "destructible") {
        if (!b.world) return false;
        Value rows;
        rows.kind = Value::Kind::Array;
        rows.array.push_back(event);
        world::applyDestructibleState(b, rows);
    } else if (kind == "projectile_ricochet") projectiles::ricochet(b, event);
    else if (kind == "projectile_impact") projectiles::terminal(b, event);
    else if (kind == "battle_result") battleResult(b, event);
    else if (kind != "bot_manifest") throw p::Error("ordered LAN event kind is unsupported: " + kind);
    return true;
}

// BattleRuntime._ignore_live_payload: contain one recoverable live payload without ending the round.
void ignore(Battle& b, const std::string& error, const Value* event) {
    auto callback = py::optional(b.engine.client.get(), "_ignore_runtime_payload");
    if (py::callable(callback)) {
        auto details = p::dict();
        if (event) {
            p::set(details.get(), "round_id", p::integer(b.round_id));
            p::set(details.get(), "event_id", p::Ref(cs::write(field(*event, "event_id"))));
            p::set(details.get(), "event_kind", p::Ref(cs::write(field(*event, "kind"))));
        }
        PyObject* handled = PyObject_CallFunction(callback.get(), const_cast<char*>("ssO"), "events", error.c_str(), details.get());
        if (handled) { const bool done = PyObject_IsTrue(handled) > 0; Py_DECREF(handled); if (done) return; }
        else PyErr_Clear();
    }
    b.warn("recoverable events payload", error, false);
}
}

// BattleRuntime.on_events(message)
bool receive(Battle& b, const Value& message) {
    if (b.phase == Phase::Failed || b.phase == Phase::Stopped || b.phase == Phase::Leaving) return false;
    try {
        world::observeDestructiblesDisabled(b, message);
        projectiles::observeMessage(b, message);
    } catch (const std::exception& e) {
        ignore(b, describe(e), nullptr);
        return false;
    }
    auto& journal = *b.events;
    for (const auto& raw : field(message, "events").resolved().array) {
        std::string event_id;
        try {
            if (raw.resolved().kind != Value::Kind::Object) throw p::Error("ordered LAN event is malformed");
            Value event = raw.resolved();
            const auto& id = field(event, "event_id");
            if (!id.truth()) throw p::Error("ordered LAN event has no event_id");
            event_id = id.resolved().kind == Value::Kind::Text ? id.resolved().text : std::to_string(integer(id));
            if (journal.accepted.contains(event_id)) continue;
            prepare(b, event);
            journal.accepted.add(event_id);
            journal.journal.push_back(std::move(event));
        } catch (const std::exception& e) {
            const std::string error = describe(e);
            if (!event_id.empty()) { journal.accepted.add(event_id); journal.applied.add(event_id); }
            ignore(b, error, &raw);
        }
    }
    drain(b);
    return true;
}

// BattleRuntime._drain_event_journal: apply ready events in order; a waiting event blocks only its dependencies.
bool drain(Battle& b) {
    if (!b.events) return true;
    auto& events = *b.events;
    auto& journal = events.journal;
    std::size_t attempts = journal.size(), index = 0;
    std::set<std::pair<std::string, std::string>> blocked;
    auto idOf = [](const Value& e) {
        const auto& id = field(e, "event_id");
        return id.resolved().kind == Value::Kind::Text ? id.resolved().text : std::to_string(std::int64_t(id.numeric()));
    };
    while (index < journal.size() && attempts > 0) {
        --attempts;
        Value& event = journal[index];
        if (text(field(event, "kind")) == "authority" && !blocked.empty()) break;
        std::set<std::pair<std::string, std::string>> dependencies;
        const auto& projectile = field(event, "projectile_id");
        if (projectile.resolved().kind != Value::Kind::Null)
            dependencies.emplace("projectile", projectile.resolved().kind == Value::Kind::Text ? projectile.resolved().text
                                                                                                : std::to_string(std::int64_t(projectile.numeric())));
        if (combatKind(text(field(event, "kind"))) && !field(event, "cosmetic_only").truth())
            dependencies.emplace("health", entityKey(event, false).value_or(""));
        bool overlaps = false;
        for (const auto& d : dependencies) if (blocked.count(d)) { overlaps = true; break; }
        if (overlaps) { blocked.insert(dependencies.begin(), dependencies.end()); ++index; continue; }
        bool is_ready = false, wait = false;
        try {
            is_ready = ready(b, event, wait);
        } catch (const std::exception& e) {
            ignore(b, describe(e), &event);
            events.applied.add(idOf(event));
            journal.erase(journal.begin() + std::ptrdiff_t(index));
            continue;
        }
        if (!is_ready) {
            if (wait) { blocked.insert(dependencies.begin(), dependencies.end()); ++index; continue; }
            return false;
        }
        bool done = true;
        try {
            done = applyOrdered(b, event);
        } catch (const std::exception& e) {
            ignore(b, describe(e), &event);
            done = true;
        }
        if (!done) {
            Value retry = std::move(journal[index]);
            journal.erase(journal.begin() + std::ptrdiff_t(index));
            journal.push_back(std::move(retry));
            continue;
        }
        events.applied.add(idOf(journal[index]));
        journal.erase(journal.begin() + std::ptrdiff_t(index));
    }
    return journal.empty();
}
}
}
