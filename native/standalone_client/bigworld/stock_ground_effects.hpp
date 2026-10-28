#pragma once
#include "cstdmf/bw_namespace.hpp"
BW_BEGIN_NAMESPACE
class Water;
class Vector3;
// The worker has no render camera or GPU Water::init. The bound compiled-space
// owner admits only fully constructed CPU query data for the current space.
float collideStockWorkerWater(const Water* water,const Vector3& start,const Vector3& end);
extern int StockGroundEffects_token;
bool initialiseStockGroundEffects();
void tickStockGroundEffects(float elapsed);
void finiStockGroundEffects();
BW_END_NAMESPACE
