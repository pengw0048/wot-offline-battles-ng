#pragma once
#include "room_value.hpp"

namespace wot::room {
// Pure settlement laws from the pinned gameplay source, independent of the
// room's mutable counters and the account's durable garage transaction.
Value awardBattleAchievements(const Value &battle);
Value offlineRewards(const Value &statistics, bool won, bool participated,
                     int tier, int spotted_spgs, int capture_participants,
                     int killed_durability);
Value medalRewards(const Value &achievements, int tier);
std::int64_t battleBondReward(std::int64_t base_xp, bool eligible);
} // namespace wot::room
