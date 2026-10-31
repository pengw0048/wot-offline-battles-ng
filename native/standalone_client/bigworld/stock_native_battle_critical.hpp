#pragma once
// The #1513 device and crew damage model on stock vehicle entities (critical_damage.py).
#include "stock_native_battle.hpp"

namespace native_battle {
struct Critical {};

namespace critical {
// Applies one canonical critical payload to a stock entity; returns the presentation events
// (a Python list of {kind, name, state, old_state, cause} rows for the stock UI callbacks).
p::Ref applyPayload(Battle& b, PyObject* entity, const Value& payload);
// Marks every device and crew member dead for one death cause; the canonical payload, or None.
p::Ref applyDeath(Battle& b, PyObject* entity, const char* cause);
// A crew/device stat multiplier ("reload", "aim_time", "vision", ...) for a stock entity.
double statFactor(Battle& b, PyObject* entity, const char* stat);
double clampVisionFactor(double factor);
// The local critical tick: repair timers, fire and the stock damage panel.
void tick(Battle& b, double dt);
}
}
