#pragma once
#include "cstdmf/bw_namespace.hpp"

BW_BEGIN_NAMESPACE
// Runs every due BigWorld.WGFlockManager sweep; #1513 WorldApp::tick services
// its WG timer queue (008a5860) here.
void tickStockFlockManagers();
BW_END_NAMESPACE
