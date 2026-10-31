#pragma once
// Round startup and teardown, LAN session entry points, replay recording and frame diagnostics.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Startup {
    Object frame_callback, ammo_callback;
};
struct Diagnostics {};

namespace startup {
void frame(Battle& b);
void beginBattle(Battle& b);
bool prebattleTransitionReady(Battle& b, double now);
void startTeamChatWhenRosterReady(Battle& b);
void maybeSendBattleReady(Battle& b);
}
}
