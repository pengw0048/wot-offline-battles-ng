#pragma once
#include "cstdmf/bw_namespace.hpp"
BW_BEGIN_NAMESPACE
extern int StockGroundEffects_token;
bool initialiseStockGroundEffects();
void tickStockGroundEffects(float elapsed);
void finiStockGroundEffects();
BW_END_NAMESPACE
