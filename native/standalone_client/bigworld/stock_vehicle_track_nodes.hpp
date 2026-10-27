#pragma once

#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE
extern int StockVehicleTrackNodes_token;

// Svarog phase 2 TrackNodesAnimatorManager (005ae6f0, vft slot 0 0078d8e0),
// registered after WheelsAnimatorManager and fed by its wheel poses.
void tickStockVehicleTrackNodes( float frameTime );
BW_END_NAMESPACE
