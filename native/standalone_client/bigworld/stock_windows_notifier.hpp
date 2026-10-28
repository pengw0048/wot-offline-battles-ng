#pragma once

#include "cstdmf/stdmf.hpp"

BW_BEGIN_NAMESPACE
// Called on focus gain without constructing a notifier during partial startup.
void resetStockWindowsNotifier();
// Retire native resources before App's main-loop/COM shutdown, without Python.
void finaliseStockWindowsNotifier();
BW_END_NAMESPACE
