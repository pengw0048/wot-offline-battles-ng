#pragma once
// Functions the Bot authority calls that other native battle subsystems own. Each replaces a
// Python call or a BattleRuntime attribute the pinned worker path read.
#include "stock_native_battle.hpp"
#include <array>

namespace native_battle {
namespace world {
}
namespace vehicles {
// _queue_bot_create(event) with event {'type': 'create', 'entity': 'bot:<id>', 'kind': 'bot', 'id': id, 'state': state}.
bool queueBotCreate(Battle& b, const Value& event);
}
namespace local {
// vehicle_physics.track_scroll(derive_params(descriptor), speed, turn, airborne, direction): the
// left and right belt scroll before the remote clamp. The params derive once per vehicle descriptor.
std::array<double, 2> trackScroll(Battle& b, Actor& vehicle, PyObject* descriptor, double speed, double turn, bool airborne,
                                  double direction);
}
}
