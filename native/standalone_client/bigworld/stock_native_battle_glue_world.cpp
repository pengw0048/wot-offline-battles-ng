// World integration: stock wreck presentation the vehicle death edges call, and the visibility foliage.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_pending.hpp"
#include "stock_native_battle_visibility_core.hpp"

namespace native_battle {
// BattleRuntime._refresh_detached_wreck: one turretless model refresh of a visible wreck.
bool world::refreshDetachedWreck(Battle& b, PyObject* entity) {
    auto appearance = py::optional(entity, "appearance");
    auto damage_state = py::optional(appearance.get(), "damageState");
    auto update = py::optional(damage_state.get(), "update");
    auto confirm = py::optional(entity, "confirmTurretDetachment");
    auto water = py::optional(appearance.get(), "waterSensor");
    if (!py::callable(update) || !py::callable(confirm) || water.get() == Py_None) return false;
    b.feature("late ammo-bay wreck refresh", [&] {
        py::invoke(update.get(), py::attr(entity, "health").get(), py::attr(entity, "isCrewActive").get(),
                   py::attr(water.get(), "isUnderWater").get());
        py::invoke(confirm.get());
    }, false);
    return true;
}

// The visibility law's foliage: the world's static and fallen-tree snapshot, or null when the map has none.
const native_visibility::FoliageSnapshot* world::foliageSnapshot(const Battle& b) {
    const auto* state = trees(b);
    return state ? &state->foliage : nullptr;
}
}
