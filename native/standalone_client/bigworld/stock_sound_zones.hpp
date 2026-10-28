#pragma once
#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_string.hpp"
#include "math/matrix.hpp"
BW_BEGIN_NAMESPACE
extern int StockSoundZones_token;
void tickStockSoundZones(const Matrix& cameraWorld, double now);
void stockSoundZonesMappingLoaded(const BW::string& path);
void finiStockSoundZones();
// Narrow bridges into the application's real Wwise source/global owners.
unsigned stockSoundZoneNameID(const BW::string& name);
void postStockSoundZoneEvent(unsigned event);
BW_END_NAMESPACE
