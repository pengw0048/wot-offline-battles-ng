#pragma once
#include "cstdmf/stdmf.hpp"
BW_BEGIN_NAMESPACE
bool initialiseStockVehicleDecals();
void finaliseStockVehicleDecals();
void setStockVehicleDecalsQuality(int option);
void beginStockVehicleDecals();
void endStockVehicleDecals();
bool drawStockVehicleDecalsPhase(unsigned phase);
extern int StockVehicleDecals_token;
BW_END_NAMESPACE
