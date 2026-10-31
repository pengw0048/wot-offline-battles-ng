#pragma once
// World internals shared by its translation units.
#include "stock_native_battle_world.hpp"

namespace native_battle {
struct FallenFoliage {
    std::unique_ptr<stock_native_bot_foliage::Trees> trees;
};

namespace world {
// Baked static and fallen-tree foliage of this map (foliage/<map>.json beside config.json).
void loadFoliage(Battle& b, const std::string& config_dir);
// Accepted fallen trees follow their native matrices on the visible client; repeated calls are gated by cadence.
void refreshFallenFoliage(Battle& b, double now, bool force);
// Visible detached-turret presentation over World::turrets.
void presentTurretRows(Battle& b);
void advanceTurretPresentation(Battle& b, double now);
void retireTurretPresentation(Battle& b);
}
}
