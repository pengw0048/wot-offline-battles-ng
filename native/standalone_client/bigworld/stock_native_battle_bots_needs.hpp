#pragma once
// Functions the Bot authority calls that other native battle subsystems own. Each replaces a
// Python call or a BattleRuntime attribute the pinned worker path read.
#include "stock_native_battle.hpp"
#include <array>

namespace native_battle {
namespace world {
}
namespace startup {
// The pinned BattleRuntime's BotRuntime(...) construction with its resolver bundle (descriptor,
// player descriptor, vehicle selector, formation spawn, baked graph, bounds, arena bounds, Bot
// equipment, WORKER_CONTROL_SECONDS on the worker). The bundle reads startup state.
p::Ref createBotRuntime(Battle& b);
}
namespace vehicles {
// _queue_bot_create({'type': 'create', 'entity': 'bot:<id>', 'kind': 'bot', 'id': id, 'state': state}).
void queueBotCreate(Battle& b, const Value& state);
}
namespace local {
// vehicle_physics.track_scroll(derive_params(descriptor), speed, turn, airborne, direction): the
// left and right belt scroll before the remote clamp. The params derive once per vehicle descriptor.
std::array<double, 2> trackScroll(Battle& b, Actor& vehicle, PyObject* descriptor, double speed, double turn, bool airborne,
                                  double direction);
}
}
