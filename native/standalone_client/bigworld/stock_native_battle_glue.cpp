// Integration glue: small adapters where one subsystem's call and another's definition were written apart.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_needs.hpp"

namespace native_battle {
// The local vehicle state is created by startup::start; its owners attach on the first drive slice.
void local::start(Battle& b) {
    if (!b.local) b.local = std::make_unique<LocalVehicle>();
}
// The local gun's reload sound and timeline end with the round.
void gun::retire(Battle& b) {
    if (!b.gun) return;
    gun::retireSound(b);
    b.gun.reset();
}
}
