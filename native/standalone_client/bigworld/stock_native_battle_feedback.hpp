#pragma once
// Stock presentation of applied combat: hit effects, impulses, shot results, battle-event rows, assists and the result.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Feedback {};

namespace feedback {
// A combat event whose attacker is known: hit effects and the shot-results/battle-event rows.
void combatHit(Battle& b, const Value& event, Actor& target, Actor& attacker);
bool combat(Battle& b, const Value& event, Actor& target, Actor& attacker, std::optional<std::int64_t> reason);
// Fire/environment damage without an attacker vehicle.
void environment(Battle& b, const Value& event, Actor& target, std::optional<std::int64_t> reason);
// Whether the local player attacked a target it cannot see (presentation then stays silent).
bool blindLocalAttack(const Battle& b, const Actor& target, const Actor& attacker);
void damageSticker(Battle& b, const Value& event, Actor& target);
// The shot/bot_shot ordered event: muzzle presentation of the firing vehicle.
void showShot(Battle& b, const Value& event, bool update_state);
void assist(Battle& b, const Value& event);
void detection(Battle& b, const Value& event);
void environmentCritical(Battle& b, const Value& event);
void battleResult(Battle& b, const Value& event);
void reportLocalKill(Battle& b, const Actor& victim, std::int64_t attacker, std::int64_t reason);
}
}
