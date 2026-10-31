#pragma once
// Native replacements for the battle-time stock hooks compat.py installs on #1513 classes.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Compat {};

namespace compat {
// Installs every battle hook into the stock classes; returns false when a stock seam is missing.
bool install(Battle& b);
void uninstall(Battle& b);
}
}
