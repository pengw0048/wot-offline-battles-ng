#pragma once
#include "cstdmf/bw_namespace.hpp"
BW_BEGIN_NAMESPACE
extern int StockTrajectoryDrawer_token;
bool initialiseStockTrajectoryDrawer();
void finaliseStockTrajectoryDrawer();
void tickStockTrajectoryDrawer(float dt);
bool drawStockTrajectoryDrawer();
BW_END_NAMESPACE
