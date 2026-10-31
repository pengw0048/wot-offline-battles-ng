// Integration glue: the calls other subsystems make into the projectile owner.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_pending.hpp"
#include "stock_native_battle_feedback_internal.hpp"
#include "stock_native_battle_world_imports.hpp"
#include "stock_native_battle_visibility_core.hpp"
#include <cmath>
#include <limits>

namespace native_battle {
namespace {
Value integerValue(std::int64_t v) { Value x; x.kind = Value::Kind::Integer; x.integer = v; return x; }
Value numberValue(double v) { Value x; x.kind = Value::Kind::Number; x.number = v; return x; }
bool isNull(const Value& v) { return v.resolved().kind == Value::Kind::Null; }
bool present(const Value& v, const char* key) { const auto& r = v.resolved(); return r.kind == Value::Kind::Object && r.object.count(key); }
std::optional<std::int64_t> whole(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Integer) return r.integer;
    if (r.kind == Value::Kind::Number && std::isfinite(r.number) && std::floor(r.number) == r.number) return std::int64_t(r.number);
    return std::nullopt;
}
// str(projectile_id) for the wire's text or integer ids.
std::string idText(const Value& v) {
    const auto& r = v.resolved();
    if (r.kind == Value::Kind::Text) return r.text;
    if (r.kind == Value::Kind::Integer) return std::to_string(r.integer);
    if (r.kind == Value::Kind::Number) return std::to_string(std::int64_t(r.number));
    if (r.kind == Value::Kind::Null) return "None";
    return {};
}
// The pinned _angle_delta: Python's floored modulo.
double angleDelta(double current, double target) {
    constexpr double pi = 3.14159265358979323846;
    const double period = 2. * pi, x = target - current + pi;
    return x - std::floor(x / period) * period - pi;
}
// int(value or 0)
std::int64_t intOrZero(const Value& v) {
    const auto& r = v.resolved();
    if (!r.truth()) return 0;
    if (r.kind == Value::Kind::Integer) return r.integer;
    return std::int64_t(r.numeric(0.));
}
const char* const immutable_fields[] = {
    "projectile_id", "shooter_kind", "shooter_id", "source_vehicle", "source_shot", "shot_seq", "shell_index", "burst_group_seq",
    "burst_index", "burst_count", "origin", "velocity", "range_origin", "gravity", "max_distance", "max_time_ms", "is_he",
    "splash_radius", "penetration_factor", "launch_server_time_ms", "fire_intent_seq", "fire_input_seq"};
bool launchChanged(const Value& current, const Value& canonical) {
    for (const char* name : immutable_fields)
        if (current.get(name).resolved() != canonical.get(name).resolved()) return true;
    return false;
}
}

namespace projectiles {
// combat_rules.apply_he_tuning: finite splash_fraction/armor_factor override the descriptor HE factors.
void applyHeTuning(Battle& b, const Value& tuning) {
    if (!b.projectiles) b.projectiles = std::make_unique<Projectiles>();
    auto& t = b.projectiles->he_tuning;
    t = {};
    const auto& r = tuning.resolved();
    if (r.kind == Value::Kind::Object) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (r.object.count("splash_fraction")) {
            const double v = r.get("splash_fraction").numeric(nan);
            if (std::isfinite(v)) t.splash_fraction = v;
        }
        if (r.object.count("armor_factor")) {
            const double v = r.get("armor_factor").numeric(nan);
            if (std::isfinite(v)) t.armor_factor = v;
        }
    }
    stock_native_projectile_damage::installHeTuning(&t);
}

// The round's projectile and local gun state; the HE tuning applied before it is kept.
void start(Battle& b) {
    if (!b.projectiles) b.projectiles = std::make_unique<Projectiles>();
    if (!b.gun) b.gun = std::make_unique<LocalGun>();
    stock_native_projectile_damage::installHeTuning(&b.projectiles->he_tuning);
}

void observeMessage(Battle& b, const Value& message) { observe(b, message); }
void reconcile(Battle& b, const Value& snapshot_message) { snapshot(b, snapshot_message); }

std::optional<double> estimatedServerTimeMs(Battle& b, double now) {
    if (!b.projectiles) return std::nullopt;
    const auto estimated = estimatedServerTime(b, now);
    return estimated ? std::optional<double>(double(*estimated)) : std::nullopt;
}
// BattleRuntime._turret_server_time_ms: the projectile clock, unclamped before its anchor.
std::optional<double> serverTimeMs(const Battle& b, double now) {
    if (!b.projectiles || !b.projectiles->server_time_ms) return std::nullopt;
    const auto& s = *b.projectiles;
    return double(std::max<std::int64_t>(0, std::int64_t(double(*s.server_time_ms) + 1000. * (now - s.server_local_time))));
}
std::optional<std::pair<double, double>> serverClockAnchor(const Battle& b) {
    if (!b.projectiles || !b.projectiles->server_time_ms) return std::nullopt;
    return std::make_pair(double(*b.projectiles->server_time_ms), b.projectiles->server_local_time);
}

bool inLineage(Battle& b, const Value& projectile_id) {
    return b.projectiles && b.projectiles->lineage.count(idText(projectile_id)) != 0;
}
void rememberLineage(Battle& b, const Value& shot_event) {
    if (const auto normalized = normalize(shot_event)) b.projectiles->lineage.insert(idText(normalized->get("projectile_id")));
}
// BattleRuntime._missing_projectile_attacker_allowed: delayed damage may outlive its shooter.
bool missingAttackerAllowed(Battle& b, const Value& event) {
    const auto& source = event.get("source").resolved();
    const auto& kind = event.get("kind").resolved();
    const bool hit = kind.kind == Value::Kind::Text &&
                     (kind.text == "hit" || kind.text == "bot_hit" || kind.text == "bot_human_hit" || kind.text == "bot_bot_hit");
    const bool text = source.kind == Value::Kind::Text;
    if (text && source.text == "fire" && hit) return true;
    if (!text || source.text != "shot" || !hit) return false;
    const auto& projectile = event.get("projectile_id");
    return !isNull(projectile) && inLineage(b, projectile);
}
// lan_client.MAX_PROJECTILE_DAMAGE_STICKER is (1 << 64) - 1; the event reader holds a sticker as int64.
std::int64_t maxDamageSticker(Battle&) { return std::numeric_limits<std::int64_t>::max(); }

// A Bot terminal waits until its displayed flight reaches the resolved time.
std::optional<double> presentationDue(Battle& b, const Value& terminal_event) {
    if (!b.projectiles) return std::nullopt;
    const auto found = b.projectiles->metas.find(idText(terminal_event.get("projectile_id")));
    if (found == b.projectiles->metas.end() || isNull(found->second.launch.get("bot_presentation_time_us"))) return std::nullopt;
    if (!found->second.bot_visual_launch_time) return std::numeric_limits<double>::infinity();
    const auto& elapsed = present(terminal_event, "resolved_time_ms") ? terminal_event.get("resolved_time_ms")
                                                                      : terminal_event.get("segment_start_time_ms");
    return *found->second.bot_visual_launch_time + double(whole(elapsed).value_or(0)) / 1000.;
}

// BattleRuntime._projectile_boundary_pose: one network or native pose in canonical numeric form.
Value boundaryPose(Battle&, const Point& position, const Value& state_argument) {
    static const Value empty = [] { Value v; v.kind = Value::Kind::Object; return v; }();
    const Value& state = state_argument.resolved().kind == Value::Kind::Object ? state_argument.resolved() : empty;
    auto number = [&](const char* name, double fallback) { return state.get(name).numeric(fallback); };
    const double yaw = number("yaw", 0.), aim_yaw = number("aim_yaw", yaw);
    Value pose;
    pose.kind = Value::Kind::Object;
    const char* axes[] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) pose.object[axes[i]] = numberValue(std::isfinite(position[i]) ? position[i] : 0.);
    pose.object["yaw"] = numberValue(yaw);
    pose.object["pitch"] = numberValue(number("pitch", 0.));
    pose.object["roll"] = numberValue(number("roll", 0.));
    pose.object["turret_yaw"] = numberValue(number("turret_yaw", angleDelta(yaw, aim_yaw)));
    pose.object["gun_pitch"] = numberValue(number("gun_pitch", 0.));
    pose.object["siege_state"] = integerValue(intOrZero(state.get("siege_state")));
    return pose;
}

// BattleRuntime._projectile_cosmetic_allowed
bool cosmeticAllowed(const Battle& b, const Value& event) {
    const auto& id = event.get("projectile_id");
    if (isNull(id) || !b.projectiles) return true;
    const auto found = b.projectiles->visuals.find(idText(id));
    return found == b.projectiles->visuals.end() || found->second.admitted;
}

// The pinned _local_projectile_presented / _local_projectile_presentations fence of an own shot.
bool markLocalPresented(Battle& b, const std::string& projectile_id) {
    auto& s = *b.projectiles;
    if (s.local_presented.contains(projectile_id) || s.local_presentations.count(projectile_id)) return false;
    s.local_presented.add(projectile_id);
    return true;
}

// The canonical launch half of BattleRuntime._show_shot: meta, muzzle mark and the tracer start (or its deferral).
std::optional<feedback::LaunchVisual> launchVisual(Battle& b, const Value& event, PyObject* entity) {
    auto& s = *b.projectiles;
    if (s.visual_terminals.contains(idText(event.get("projectile_id")))) return std::nullopt;
    feedback::LaunchVisual out;
    const auto normalized = normalize(event);
    if (!normalized) { out.meta = false; return out; }
    const auto id = idText(normalized->get("projectile_id"));
    const auto found = s.metas.find(id);
    ProjectileMeta* meta = nullptr;
    if (found != s.metas.end()) {
        // The delayed muzzle presents the immutable launch, not a new authority cursor.
        if (launchChanged(found->second.launch, *normalized)) throw p::Error("canonical projectile launch changed");
        meta = &found->second;
    } else {
        meta = &install(b, *normalized);
    }
    meta->source_descriptor = own(py::attr(entity, "typeDescriptor"));
    Value muzzle;
    muzzle.kind = Value::Kind::Boolean;
    muzzle.boolean = true;
    meta->launch.object["muzzle_presented"] = muzzle;
    const bool bot_stamped = !isNull(meta->launch.get("bot_presentation_time_us"));
    if (bot_stamped) meta->presented_ricochet_count = 0;
    out.burst_index = whole(meta->launch.get("burst_index"));
    out.burst_count = whole(meta->launch.get("burst_count"));
    // A Bot muzzle owns a fresh first segment; an own or human launch presents its stored segment.
    const auto start = visualStart(b, entity, bot_stamped ? *normalized : meta->launch);
    if (bot_stamped) {
        out.deferred = true;
        meta->muzzle_visual_start = start;
    } else {
        ensureVisual(b, id, b.clock(), start);
    }
    const auto visual = s.visuals.find(id);
    out.admitted = visual == s.visuals.end() || visual->second.admitted;
    return out;
}
// The Bot tracer starts after its muzzle, from the HP_gunFire sampled before it.
void deferredLaunchVisual(Battle& b, const Value& event, PyObject*) {
    auto& s = *b.projectiles;
    const auto id = idText(event.get("projectile_id"));
    const auto found = s.metas.find(id);
    if (found == s.metas.end()) return;
    const auto start = found->second.muzzle_visual_start;
    found->second.muzzle_visual_start.reset();
    ensureVisual(b, id, b.clock(), start);
}

// The LAN input `shoot` control: one fresh trigger edge (the native owner reads the rotator's laid gun).
bool shoot(Battle& b, double, double) { return shoot(b, false); }

// BattleRuntime._defer_cancel_native_shot_wait: close #1513's trigger handshake after the mailbox returns.
bool cancelNativeShotWait(Battle& b) {
    schedule(b, 0., [](Battle& battle) {
        // A duplicate trigger while one canonical launch is pending keeps the accepted shot's wait.
        if (battle.gun && battle.gun->intent) return;
        try {
            auto cancel = py::optional(battle.engine.avatar.get(), "cancelWaitingForShot");
            if (!py::callable(cancel)) throw p::Error("#1513 shot acknowledgement cancel boundary is unavailable");
            py::invoke(cancel.get());
        } catch (const std::exception& e) {
            battle.fail(describe(e));
        }
    });
    return true;
}
}
}
