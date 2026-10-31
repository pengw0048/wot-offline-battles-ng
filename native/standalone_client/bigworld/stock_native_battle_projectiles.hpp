#pragma once
// Projectiles and the local gun: in-flight shells, launches and terminals, reload, ammunition, fire intents.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Projectiles {};
struct LocalGun {};

namespace projectiles {
// Per frame while live: advance flights and present terminals.
void advance(Battle& b, double now);
// Prebattle sampling of collision poses on the projectile authority.
void prebattleSample(Battle& b, double now);
// Accepts one projectile event (launch/terminal) the event journal admitted.
bool accept(Battle& b, const Value& event);
// The LAN fire_intent_result message.
void fireIntentResult(Battle& b, const Value& message);
}

namespace gun {
// 10 Hz gun clock: reload, clip, ammunition and server marker selection.
void ammoTick(Battle& b);
// Closes the gun timeline before a synchronous state mutation (siege, stun, critical).
void edge(Battle& b);
// Per frame on the visible client: burst continuation and live reload sound.
void frame(Battle& b, double now);
}
}
