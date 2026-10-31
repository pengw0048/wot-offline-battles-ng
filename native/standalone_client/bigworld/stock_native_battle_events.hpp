#pragma once
// Ordered combat events: the accepted journal, combat application and its stock feedback.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Events {
    std::vector<Value> journal;
};

namespace events {
// The LAN `events` message.
void receive(Battle& b, const Value& message);
// Applies journaled events whose target vehicles are ready.
void drain(Battle& b);
// Whether a journaled combat event still targets this vehicle.
bool pendingCombatFor(const Battle& b, const Actor& vehicle);
}
}
