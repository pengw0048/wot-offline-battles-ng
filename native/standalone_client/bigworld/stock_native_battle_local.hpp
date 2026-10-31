#pragma once
// The player's own vehicle: drive, physics, suspension, Siege, contacts, drowning, overturn, audio, camera.
#include "stock_native_battle.hpp"

namespace native_battle {
struct LocalVehicle {
    Point position{};
    double yaw = 0., pitch = 0., roll = 0., speed = 0., turn_speed = 0.;
    bool airborne = false;
};

namespace local {
// Per frame on the visible client while live.
void drive(Battle& b, double dt);
void tickCritical(Battle& b, double dt);
void tickDrowning(Battle& b, double dt, double now);
void tickOverturn(Battle& b, double dt, double now);
// A Siege state change of the local vehicle reached presentation.
void siegeChanged(Battle& b, PyObject* entity, bool raised);
// The local vehicle died.
void died(Battle& b);
}
}
