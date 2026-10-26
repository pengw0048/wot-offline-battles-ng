#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "pyscript/script.hpp"

#include <functional>

BW_BEGIN_NAMESPACE
extern int StockVehicleWaterSensor_token;

// WaterSensorManager, Svarog phase 1 after the LOD calculators (005af540).
void tickStockWaterSensors( float frameTime );

// DataLinks.createBoolLink(sensor, "isInWater"|"isUnderWater"|"isSplash").
bool stockWaterSensorBoolLink( PyObject * owner, const BW::string & name,
    std::function<bool()> & reader );
BW_END_NAMESPACE
