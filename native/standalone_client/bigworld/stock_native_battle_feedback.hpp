#pragma once
// Stock presentation of applied combat: hit effects, impulses, shot results, battle-event rows, assists,
// the round result and the postmortem viewpoint.
#include "stock_native_battle.hpp"
#include <map>
#include <set>

namespace native_battle {
struct Feedback {
    // Engine ids whose credited detection already drew the stock spotting ribbon.
    std::set<std::int64_t> spot_feedback_sent;
    // Shell kinds the damage log could not resolve, reported once each.
    std::set<std::string> shell_failures;
    bool round_finished_notified = false;
    // The vehicle the postmortem camera observes (the player's own until it switches).
    std::optional<std::int64_t> spectated_engine_id;
    std::int64_t critical_voice_count = 0;
};

namespace feedback {
// What the projectile owner decided about one launch's canonical visual (see projectiles::launchVisual).
struct LaunchVisual {
    bool meta = true;               // the wire meta normalized; false still hands the tracer to the entity
    bool admitted = true;
    std::optional<std::int64_t> burst_index, burst_count;
    bool deferred = false;          // the visual starts after the muzzle (projectiles::deferredLaunchVisual)
};

// A combat event whose attacker is known: armour effects, impulse, hit indicator.
bool combatHit(Battle& b, const Value& event, Actor& target, Actor& attacker);
// Battle-event rows and shot results of one combat event; true when the stock callback received rows.
bool combat(Battle& b, const Value& event, Actor& target, Actor& attacker, std::optional<std::int64_t> reason);
// Fire/environment damage without an attacker vehicle.
bool environment(Battle& b, const Value& event, Actor& target, std::optional<std::int64_t> reason);
// Whether the local player attacked a target it cannot see (presentation then stays silent).
bool blindLocalAttack(const Battle& b, const Actor& target, const Actor& attacker);
bool damageSticker(Battle& b, const Value& event, Actor& target);
// The shot/bot_shot ordered event: muzzle, burst and the canonical tracer handoff of the firing vehicle.
bool showShot(Battle& b, const Value& event, bool update_state);
bool assist(Battle& b, const Value& event);
bool detection(Battle& b, const Value& event);
bool environmentCritical(Battle& b, const Value& event);
// The round result (a battle_result event or snapshot field) to the stock Avatar.
bool battleResult(Battle& b, const Value& result);
// Kept for the vehicle death edge; the Python-era kill-contract diagnostic is gone.
void reportLocalKill(Battle& b, const Actor& victim, std::int64_t attacker, std::int64_t reason);

// Shell of the attacker's current gun for this event: (GunShot, shell). Throws when unresolvable.
std::pair<p::Ref, p::Ref> eventShell(Battle& b, const Actor& attacker, const Value& event);
// (#1513 shell type index, gold) for the damage log.
std::pair<std::int64_t, bool> shellFields(Battle& b, const Value& event, const Actor* attacker);
// Hull damage as a percent of maximum health, for the stock impact sound.
double hitDamageFactor(Battle& b, const Value& event, const Actor& target);
// One EFFECT_MATERIALS name of the ground surface at a point, or none.
std::optional<std::string> surfaceMaterial(Battle& b, const Point& at);
bool releaseTargetLock(Battle& b, std::int64_t engine_id);
// HE near miss through the target's own hull bound effects (showSplashHitEffect).
bool splashHit(Battle& b, Actor& target, PyObject* effects_descr, PyObject* effects_index, const Point& impact,
               PyObject* direction, double damage_factor);

// Postmortem viewpoint (Avatar server bridge on_viewpoint_switch and vehicle death/removal).
bool switchPostmortemViewpoint(Battle& b, bool is_viewpoint, std::int64_t engine_id);
bool fallbackPostmortemViewpoint(Battle& b, std::int64_t excluded_engine_id);
bool releasePostmortemVisibility(Battle& b);
std::optional<std::int64_t> spectated(const Battle& b);
// The entity of a server-valid allied postmortem target other than the player (_spectator_record), or None.
p::Ref spectatorEntity(Battle& b, std::int64_t engine_id);
void setSpectated(Battle& b, std::optional<std::int64_t> engine_id);
}
}
