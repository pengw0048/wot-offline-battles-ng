#pragma once

#include "resmgr/datasection.hpp"

BW_BEGIN_NAMESPACE
extern int StockGraphicsDetection_token;
void configureStockGraphicsDetection( DataSectionPtr presets, DataSectionPtr bounds );
void readStockGraphicsDetectionPreferences( DataSectionPtr preferences );
void writeStockGraphicsDetectionPreferences( DataSectionPtr preferences );
BW_END_NAMESPACE
