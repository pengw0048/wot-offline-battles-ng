// Integration glue: the calls other subsystems make into the local vehicle laws.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_local.hpp"

namespace native_battle {
// The Bot track feed's belt law; the drive parameters derive once per Bot descriptor.
std::array<double, 2> local::trackScroll(Battle& b, Actor& vehicle, PyObject* descriptor, double speed, double turn, bool airborne,
                                         double direction) {
    auto& cached = b.local->belt_params[vehicle.key];
    if (cached.descriptor.get() != descriptor || cached.revision != vehicle.descriptor_revision) {
        cached.params = deriveParams(descriptor);
        cached.descriptor = borrow(descriptor);
        cached.revision = vehicle.descriptor_revision;
    }
    return local::trackScroll(cached.params, speed, turn, airborne, direction);
}
}
