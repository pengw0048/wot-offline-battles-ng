#pragma once
#include <array>

namespace wot
{
inline int stockTPSShaderQuality( int quality, bool sniper )
{
    const int maximum = sniper ? 3 : 4;
    return quality < 0 ? 0 : quality > maximum ? maximum : quality;
}

// #1513 f98c90 updates the near-distance interval; f9a370 packs the two
// ParticlesParams rows consumed by the original tPS and shimmer shaders.
inline std::array<float, 8> stockTPSParticleParameters( float fov, bool sniper,
    int quality, bool offscreen )
{
    float start = 2.f, end = 5.f;
    if (sniper)
    {
        const float shift = (0.1f / (fov + 0.0001f) - 0.1f) * 7.f;
        start = start + shift;
        end = end + shift;
    }
    if (start < 0.f) start = 0.f;
    if (end < 0.0001f) { start = 0.f;end = 0.0001f; }
    else if (end - start < 0.0001f) end = start + 0.0001f;
    const float inverse = 1.f / (end - start);
    // Alpha override and debug mode retain their original zero defaults.
    return { 0.f, inverse, -(inverse * start), 0.f,
        0.f, float(stockTPSShaderQuality(quality,sniper)), offscreen ? 1.f : 0.f, 0.f };
}
}
