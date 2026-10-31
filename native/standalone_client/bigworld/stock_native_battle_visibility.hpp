#pragma once
// Spotting, team observation, target outline, Expert, radio and Sixth Sense.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Visibility {
    std::optional<std::int64_t> outlined_engine_id;
};

namespace visibility {
// Per frame on the visible client; hud_only before the battle goes live.
void spotting(Battle& b, double now, bool hud_only);
void outline(Battle& b, double now);
void expert(Battle& b, double now);
void validateTargetLock(Battle& b);
// The LAN bot_observation message.
bool observation(Battle& b, const Value& message);
// Drops the outline when its vehicle leaves presentation.
void clearOutline(Battle& b);
void releaseTargetLock(Battle& b, std::int64_t engine_id);
}
}
