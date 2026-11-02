#pragma once

#include <algorithm>
#include <cmath>

namespace wot {

// #1513 9ceba0, called at 9cc467 for each directional-shadow split.
// The DX9 branch (9cc42a) supplies (.7, .7, 1) and 24 depth bits.
// Return normalised light depth, not world units or atlas texels.
inline float stock_directional_shadow_depth_bias(float projectionX,
    float projectionY, float projectionZ, unsigned width, unsigned height)
{
    const float texelX = std::fabs(2.f / (float(width) * projectionX));
    const float texelY = std::fabs(2.f / (float(height) * projectionY));
    const float depthQuantum = std::fabs(1.f / (16777216.f * projectionZ));
    return projectionZ * std::max(std::max(.7f * texelX, .7f * texelY), depthQuantum);
}

} // namespace wot
