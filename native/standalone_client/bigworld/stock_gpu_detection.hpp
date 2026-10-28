#pragma once
#include "cstdmf/stdmf.hpp"
BW_BEGIN_NAMESPACE
// Runs the original workload on the active D3D9 device. Returns the stock
// unsupported (1), failed (2), or measured (3..100) score.
int stockMeasureGpuPerformance();
bool stockMeasureGraphicsHardware(int64 (&scores)[5]);
void stockAdjustDetectedResolution(int status);
BW_END_NAMESPACE
