#pragma once

#include "resmgr/datasection.hpp"

BW_BEGIN_NAMESPACE
extern int StockGraphicsDetection_token;
void configureStockGraphicsDetection( DataSectionPtr presets, DataSectionPtr bounds );
void readStockGraphicsDetectionPreferences( DataSectionPtr preferences );
void writeStockGraphicsDetectionPreferences( DataSectionPtr preferences );
int measureStockGraphicsDetection();
void scheduleStockGraphicsDetection(bool apply, int overrideGroup);
void finishStockGraphicsDetectionStartup();
BW_END_NAMESPACE
