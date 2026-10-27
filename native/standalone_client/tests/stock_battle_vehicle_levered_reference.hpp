// Host stand-ins for the LeveredSuspension UCRT/x87 entry points. cosf/sinf
// reuse the animation reference; atan2 matches Unicorn's fpatan (host double
// atan2 rounded to float), so oracle comparisons test the original sequence.
#pragma once
#include "stock_battle_vehicle_animation_reference.hpp"
#include "stock_battle_vehicle_levered_data.hpp"
#include <cmath>

extern "C" {
inline float refAtan2(float y, float x) {
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
}
}

inline const wot::StockLeveredMath & referenceLeveredMath() {
    static const wot::StockLeveredMath math = {referenceAnimationMath(), &refCos, &refSin, &refAtan2};
    return math;
}
