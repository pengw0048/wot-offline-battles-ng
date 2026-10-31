#pragma once
// Events subsystem internals shared by its translation units.
#include "stock_native_battle.hpp"

namespace native_battle {
namespace events {
std::optional<std::string> entityKey(const Value& event, bool attacker);
std::string stunKey(const Value& event);
std::string combatSource(const Value& event);
std::optional<std::int64_t> combatAttackReason(Battle& b, const Value& event);
void validateCombat(Battle& b, const Value& event);
Value& knownState(Battle& b, const std::string& key);
void combatEventState(Value& state, const Value& event, const std::string& target);
// Event application.
void applyCombat(Battle& b, const Value& event, bool update_state);
void showShot(Battle& b, const Value& event, bool update_state);
void applyStatisticsEvent(Battle& b, const Value& event);
void environmentCriticalFeedback(Battle& b, const Value& event);
void assist(Battle& b, const Value& event);
void detection(Battle& b, const Value& event);
void battleResult(Battle& b, const Value& event);
}
}
