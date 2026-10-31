#pragma once
// The battle world: destructibles, fallen-tree foliage, detached turrets and ram history.
#include "stock_native_battle.hpp"

namespace native_battle {
struct World {};

namespace world {
void foliage(Battle& b, double now);
void detachedTurrets(Battle& b, double now);
// Prebattle/worker registry prewarm.
void prewarm(Battle& b, double now);
// The LAN player_destructible_contact(_result) messages.
void destructibleContact(Battle& b, const Value& message);
void destructibleContactResult(Battle& b, const Value& message);
// A remote vehicle lost its turret (ammo rack): present it and, on the worker, plan the debris.
bool detachTurret(Battle& b, Actor& vehicle, PyObject* entity, const Value* state);
}
}
