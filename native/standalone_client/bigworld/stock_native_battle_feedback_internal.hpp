#pragma once
// Feedback subsystem internals: event field access and the calls it needs from other subsystems.
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_feedback.hpp"
#include <string>

namespace native_battle {
namespace feedback {
inline const Value& field(const Value& v, const char* name) { return v.resolved().get(name); }
inline bool has(const Value& v, const char* name) {
    const auto& r = v.resolved();
    return r.kind == Value::Kind::Object && r.object.count(name);
}
inline const std::string& text(const Value& v) { return v.resolved().text; }
inline bool isNull(const Value& v) { return v.resolved().kind == Value::Kind::Null; }
inline bool isTrue(const Value& v) { const auto& r = v.resolved(); return r.kind == Value::Kind::Boolean && r.boolean; }
// int(v or 0) with a fallback for a missing field.
inline std::int64_t integer(const Value& v, std::int64_t fallback = 0) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Null) return fallback;
    if (r.kind == Value::Kind::Integer) return r.integer;
    return std::int64_t(r.numeric(double(fallback)));
}
// max(0, int(event.get(name, 0) or 0))
inline std::int64_t nonNegative(const Value& event, const char* name) {
    return std::max<std::int64_t>(0, field(event, name).truth() ? integer(field(event, name)) : 0);
}
// The module helper _number: a finite float, else 0.
inline double number(const Value& v) { return v.resolved().numeric(0.); }
inline std::string idText(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Text) return r.text;
    if (r.kind == Value::Kind::Integer) return std::to_string(r.integer);
    return std::to_string(std::int64_t(r.numeric()));
}
inline std::string source(const Value& event) {
    const auto& s = field(event, "source").resolved();
    return s.kind == Value::Kind::Text && !s.text.empty() ? s.text : "shot";
}
inline bool anyHit(const std::string& k) { return k == "hit" || k == "bot_hit" || k == "bot_human_hit" || k == "bot_bot_hit"; }
// BattleRuntime._event_entity_key(event, role)
inline std::optional<std::string> entityKey(const Value& event, bool attacker) {
    const char* bot = attacker ? "attacker_bot" : "target_bot";
    const char* player = attacker ? "attacker" : "target";
    if (!isNull(field(event, bot))) return "bot:" + idText(field(event, bot));
    if (!isNull(field(event, player))) return "player:" + idText(field(event, player));
    return {};
}
// A presented vehicle (one with a stock entity), or null.
inline Actor* presentedVehicle(const Battle& b, const std::string& key) {
    Actor* v = b.vehicle(key);
    return v && v->engine_id ? v : nullptr;
}
// The stock BATTLE_EVENT_TYPE group, or the pinned error.
p::Ref eventTypes(Battle& b, const char* missing);
p::Ref battleEventsCallback(Battle& b);
p::Ref eventRow(PyObject* types, const char* type, std::int64_t target, p::Ref details);
std::int64_t team(const Actor& v);
bool isHe(PyObject* shot);
std::int64_t shellNominalDamage(PyObject* shot);
p::Ref vector(Battle& b, double x, double y, double z);
// BattleRuntime._impact_sound_identity: the stock impact sound's keyword arguments.
p::Ref impactSoundIdentity(Battle& b, const Actor& target, std::int64_t attacker, PyObject* direction, double damage_factor);
}

// Calls feedback needs from other subsystems (declared here until their owners publish them).
namespace projectiles {
// Whether the projectile's canonical visual was admitted (BattleRuntime._projectile_cosmetic_allowed).
bool cosmeticAllowed(const Battle& b, const Value& event);
// Marks one local launch presented; false when it already was (_local_projectile_presented/_presentations).
bool markLocalPresented(Battle& b, const std::string& projectile_id);
// For a canonical launch (origin/velocity/gravity/maxDistance present): installs/validates its meta and starts
// (or defers) its visual. None when its visual already terminated.
std::optional<feedback::LaunchVisual> launchVisual(Battle& b, const Value& event, PyObject* entity);
void deferredLaunchVisual(Battle& b, const Value& event, PyObject* entity);
}
namespace gun {
// (committed, count) of the local player's burst in flight, from the local fire intent.
std::optional<std::pair<std::int64_t, std::int64_t>> localBurst(const Battle& b);
}
namespace local {
// The stock swinging animator the local body-swinging rebind installed, or None.
p::Ref swingingAnimator(Battle& b);
// The local vehicle's body pose matrix provider (BattleRuntime._local_body_pose).
p::Ref bodyPose(Battle& b);
}
namespace remote {
// factory.present_hit_impulse(engine_id, direction, impulse) for a native remote vehicle.
}
namespace visibility {
double shotCamouflageSeconds();
// Re-evaluate spotting on the next frame (the AOI centre moved).
void resetSpottingTimer(Battle& b);
}
}
