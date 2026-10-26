#pragma once

#include "cstdmf/bw_namespace.hpp"

BW_BEGIN_NAMESPACE
extern int StockVehicleTerrainSensor_token;

// TerrainMatKindSensor manager 005af760: Svarog phase 1, static factory
// 01493430, directly after DetailedEngineState 0149342c.
void tickStockTerrainMatKindSensors( float frameTime );
BW_END_NAMESPACE
