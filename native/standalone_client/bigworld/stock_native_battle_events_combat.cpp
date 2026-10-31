// Combat event application: target state, attacker identity, critical payload and health; presentation is feedback's.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_events_internal.hpp"
#include "stock_native_battle_vehicles_internal.hpp"
#include "stock_native_battle_pending.hpp"
#include <algorithm>

namespace native_battle {
namespace events {
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
Actor* presented(Battle& b, const std::string& key) {
    Actor* v = b.vehicle(key);
    return v && v->engine_id ? v : nullptr;
}
}

// BattleRuntime._combat_event_state: the target state an ordered combat event leaves behind.
void combatEventState(Value& state, const Value& event, const std::string& target);

// BattleRuntime._apply_combat_event(event, update_state=True)
void applyCombat(Battle& b, const Value& event, bool update_state) {
    validateCombat(b, event);
    const std::string source = combatSource(event);
    const auto reason = combatAttackReason(b, event);
    const auto target_key = entityKey(event, false);
    if (!target_key) throw p::Error("ordered combat event has no target");
    Actor* target = presented(b, *target_key);
    if (!target) throw p::Error("ordered combat event target is unavailable: " + *target_key);
    auto& d = target->presented;
    const Value latest = d.state;
    const bool cosmetic = field(event, "cosmetic_only").truth();
    Value state = latest;
    if (!cosmetic) combatEventState(state, event, *target_key);
    if (update_state && !cosmetic) d.state = state;
    const auto attacker_key = entityKey(event, true);
    Actor* attacker = attacker_key ? presented(b, *attacker_key) : nullptr;
    if (attacker_key && !attacker && !projectiles::missingAttackerAllowed(b, event))
        throw p::Error("ordered combat event attacker is unavailable: " + *attacker_key);
    const std::int64_t attacker_engine = attacker ? attacker->engine_id : 0;
    auto entity = b.entity(target->engine_id);
    if (entity.get() == Py_None) throw p::Error("ordered combat event target has no native entity: " + *target_key);
    if (attacker && b.entity(attacker->engine_id).get() == Py_None) throw p::Error("ordered combat event attacker has no native entity: " + *attacker_key);
    feedback::damageSticker(b, event, *target);
    const bool blind = attacker && feedback::blindLocalAttack(b, *target, *attacker);
    const bool dead_target = field(state, "health").numeric(0.) <= 0. || !field(state, "alive").truth(true);
    const bool suppress_postmortem = vehicles::suppressPostmortemKiller(b, *target, state, attacker_engine);
    if (attacker_key && !target->local && !cosmetic) py::set(entity.get(), "last_killer_id", p::integer(attacker_engine).get());
    if (target->local && attacker_key && !cosmetic) {
        const auto colon = attacker_key->find(':');
        b.events->local_last_attacker = std::stoll(attacker_key->substr(colon + 1));
    }
    if (source == "player_left" && attacker) throw p::Error("player_left event has an attacker");
    if (attacker) {
        feedback::combatHit(b, event, *target, *attacker);
        feedback::combat(b, event, *target, *attacker, reason);
    } else if (source == "environment" || source == "fire") {
        feedback::environment(b, event, *target, reason);
    }
    if (cosmetic) return;
    const auto& critical = field(event, "critical");
    if (critical.resolved().kind == Value::Kind::Object) {
        Value canonical = critical.resolved();
        writable(canonical).object["events"].kind = Value::Kind::Array;
        canonical.object["events"].array.clear();
        const bool apply = vehicles::reconcileCriticalAuthority(b, *target, event, &canonical);
        if (apply && !(canonical == d.critical)) {
            const bool edge = target->local && b.gun;
            if (edge) gun::edge(b);
            Value presentation = critical.resolved();
            writable(presentation).object["devices"] = field(canonical, "devices");
            presentation.object["destroyed"] = field(canonical, "destroyed");
            if (target->local && b.critical && b.critical->owned && d.critical_base_revision == b.critical->base_revision) {
                // The owning client already presented its own track transitions.
                Value filtered; filtered.kind = Value::Kind::Array;
                for (const auto& item : field(critical, "events").resolved().array) {
                    const std::string name = field(item, "name").resolved().text;
                    if (name != "leftTrackHealth" && name != "rightTrackHealth") filtered.array.push_back(item.resolved());
                }
                presentation.object["events"] = std::move(filtered);
            }
            auto events = critical::applyPayload(b, entity.get(), presentation);
            if (edge) gun::descriptorChanged(b, entity.get());
            writable(state).object["critical"] = canonical;
            d.critical = canonical;
            for (const auto& row : events.resolved().array)
                if (field(row, "kind").resolved().text == "ammo_rack" && field(row, "state").resolved().text == "destroyed")
                    d.ammo_death_cause = field(row, "cause").resolved().text;
            vehicles::presentCritical(b, *target, events, attacker_engine);
        }
    }
    if (has(event, "display_health"))
        writable(state).object["display_health"].kind = Value::Kind::Integer,
        state.object["display_health"].integer = std::max<std::int64_t>(0, integer(field(event, "display_health"), integer(field(state, "health"))));
    const std::int64_t death_reason = integer(field(event, "death_reason"));
    const bool force = std::max<std::int64_t>(0, integer(field(event, "damage"))) > 0 || field(event, "dead").truth();
    vehicles::HealthEdge edge;
    edge.attacker = attacker_engine;
    edge.reason = death_reason;
    edge.attack_reason = reason.value_or(0);
    edge.force = force;
    edge.suppress_combat = blind && !dead_target;
    edge.suppress_postmortem_killer = suppress_postmortem;
    vehicles::applyHealth(b, *target, state, edge);
    if (!update_state) d.state = latest;
}

// BattleRuntime._apply_vehicle_statistics_event: frags and team-killer state from the server.
void applyStatisticsEvent(Battle& b, const Value& event) {
    const auto& raw = field(event, "actor_id").resolved();
    if (raw.kind != Value::Kind::Integer && raw.kind != Value::Kind::Number && raw.kind != Value::Kind::Text) return;
    std::int64_t actor = 0;
    try { actor = raw.kind == Value::Kind::Text ? std::stoll(raw.text) : std::int64_t(raw.numeric()); }
    catch (...) { return; }
    Actor* v = presented(b, field(event, "actor_kind").resolved().text + ":" + std::to_string(actor));
    if (!v) return;
    auto& state = writable(v->presented.state);
    if (has(event, "frags")) state.object["frags"].kind = Value::Kind::Integer, state.object["frags"].integer = integer(field(event, "frags"));
    if (has(event, "team_killer")) state.object["team_killer"].kind = Value::Kind::Boolean, state.object["team_killer"].boolean = field(event, "team_killer").truth();
    vehicles::applyStatistics(b, *v, v->presented.state);
}

void showShot(Battle& b, const Value& event, bool update_state) { feedback::showShot(b, event, update_state); }
void environmentCriticalFeedback(Battle& b, const Value& event) { feedback::environmentCritical(b, event); }
void assist(Battle& b, const Value& event) { feedback::assist(b, event); }
void detection(Battle& b, const Value& event) { feedback::detection(b, event); }
void battleResult(Battle& b, const Value& event) { feedback::battleResult(b, event); }
void reportLocalKill(Battle& b, const Actor& victim, std::int64_t attacker, std::int64_t reason) { feedback::reportLocalKill(b, victim, attacker, reason); }
}
}
