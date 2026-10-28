#pragma once
#include "cstdmf/stdmf.hpp"

BW_BEGIN_NAMESPACE
// Original detector935570: one fresh raw RDTSC measurement around Sleep(500).
int64 stockMeasureCpuFrequency();
BW_END_NAMESPACE
