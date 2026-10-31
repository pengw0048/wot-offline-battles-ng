#pragma once
// Functions the Bot authority calls that other native battle subsystems own. Each replaces a
// Python call or a BattleRuntime attribute the pinned worker path read.
#include "stock_native_battle.hpp"
#include <array>

namespace native_battle {
namespace world {
// battle._detached_turret_rows: the canonical detached turret rows keyed by owner, or None.
p::Ref detachedTurretRows(Battle& b);
// stock_native_world_snapshot.build(battle._destructibles): the native world snapshot DTO.
p::Ref destructibleSnapshot(Battle& b);
// destructibles_sensor.collision_preparation_scope_1513(): brackets every native world query of one
// Bot step. end() runs on every exit path, including an exception.
void beginCollisionPreparation(Battle& b);
void endCollisionPreparation(Battle& b);
}
namespace local {
// vehicle_physics.track_scroll(derive_params(descriptor), speed, turn, airborne, direction): the
// left and right belt scroll before the remote clamp. The params derive once per vehicle descriptor.
std::array<double, 2> trackScroll(Battle& b, Actor& vehicle, PyObject* descriptor, double speed, double turn, bool airborne,
                                  double direction);
}
}
