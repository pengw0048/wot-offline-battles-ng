#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace wot
{
struct StockCustomAAMode
{
    bool fxaa;
    std::uint32_t temporalSamples;
    std::uint32_t mipMapLodBias;
    bool eightBitHistory;
};

// 9519b0 reads the four original 16-byte rows at 14f8650. These describe
// different algorithms; the last two entries are not FXAA quality presets.
inline StockCustomAAMode stockCustomAAMode(std::uint32_t mode, bool temporalSupported)
{
    const StockCustomAAMode modes[] = {
        {false, 0, 0, true}, {true, 0, 0, true},
        {false, 4, 1, true}, {false, 4, 2, false}
    };
    return modes[mode < (temporalSupported ? 4u : 2u) ? mode : 0u];
}

inline const std::array<std::uint32_t, 11>& stockMultisamplingCandidates()
{
    static const std::array<std::uint32_t, 11> candidates = {{
        2, 4, 6, 8, 12, 16, 32, 0x408, 0x808, 0x410, 0x810
    }};
    return candidates;
}

struct StockD3D9Multisampling
{
    std::uint32_t samples;
    std::uint32_t quality;
};

struct StockMultisamplingSelection
{
    std::uint32_t type = 0;
    std::uint32_t quality = 0;
    bool pending = false;

    // 652400 changes the desired DisplayContext before queuing flag 4 on
    // WindowController. The actual device reset belongs to the next frame.
    void request(std::uint32_t desired)
    {
        if (type == desired && quality == 0) return;
        type = desired;
        quality = 0;
        pending = true;
    }
};

// b2ea00/b2e9b0 translate the original vendor-encoded coverage modes into
// actual D3DMULTISAMPLE_TYPE and quality. The desired enum remains distinct.
inline StockD3D9Multisampling stockD3D9Multisampling(
    std::uint32_t desired, std::uint32_t quality, bool nvidia)
{
    if (nvidia)
    {
        switch (desired)
        {
        case 0x408: return {4, 2};
        case 0x808: return {8, 0};
        case 0x410: return {4, 4};
        case 0x810: return {8, 2};
        }
    }
    return {desired, quality};
}

// b26d10 has an original NVIDIA coverage-mode branch before the ordinary
// CheckDeviceMultiSampleType query. Never replace it with an all-supported list.
template<class Check>
inline bool stockD3D9SupportsMultisampling(std::uint32_t desired, bool nvidia, Check check)
{
    if (nvidia)
    {
        if (desired - 0x408u <= 0x408u) return true;
        if (desired - 8u <= 0x18u) return false;
    }
    return static_cast<std::int32_t>(desired) <= 16 && check(desired);
}

// 99ff20 advances once per rendered frame. History changes every frame in
// normal temporal mode, and only after a complete cycle in static accumulation.
struct StockTemporalAAFrame
{
    std::uint32_t frame = 0;
    std::uint32_t sampleIndex = 0;
    std::uint32_t previousHistory = 0;
    std::uint32_t currentHistory = 1;
    float jitterX = 0.f;
    float jitterY = 0.f;

    void advance(std::uint32_t samples, bool supported, bool staticAccumulation)
    {
        ++frame;
        const std::uint32_t count = supported && samples > 1 ? samples : 0;
        sampleIndex = count ? frame % count : 0;
        if (!staticAccumulation || !sampleIndex)
        {
            previousHistory = currentHistory;
            currentHistory = 1 - currentHistory;
        }
        static const float two[][2] = {{-.5f, 0.f}, {0.f, .5f}};
        static const float three[][2] = {{-.3f, -.3f}, {.3f, 0.f}, {0.f, .3f}};
        static const float four[][2] = {{-.125f, -.375f}, {.375f, -.125f},
            {.125f, .375f}, {-.375f, .125f}};
        const float* point = count == 2 ? two[sampleIndex] :
            count == 3 ? three[sampleIndex] : count == 4 ? four[sampleIndex] : nullptr;
        if (point)
        {
            jitterX = point[0]; jitterY = point[1];
        }
        else if (!count)
        {
            jitterX = jitterY = 0.f;
        }
        else
        {
            float y = 0.f, weight = 1.f / 3.f;
            for (std::uint32_t n = sampleIndex; static_cast<std::int32_t>(n) > 0; n /= 3)
            {
                const float digit = static_cast<float>(n % 3) * weight;
                weight = weight * (1.f / 3.f);
                y = y + digit;
            }
            float x = 0.f; weight = .5f;
            for (std::uint32_t n = sampleIndex; static_cast<std::int32_t>(n) > 0; n /= 2)
            {
                const float digit = static_cast<float>(n % 2) * weight;
                weight = weight * .5f;
                x = x + digit;
            }
            jitterX = x - .5f; jitterY = y - .5f;
        }
    }
};

struct StockTemporalAAKernel
{
    // 99fb70 binds five float4 offsets and three float4 vectors per weight
    // array. 99ecd0 fills nine pairs/weights; the remaining lanes stay zero.
    std::array<float, 20> offsets = {{}};
    std::array<float, 12> weights = {{}};
    std::array<float, 12> lowPassWeights = {{}};
};

inline StockTemporalAAKernel stockTemporalAAKernel(
    float width, float height, float jitterX, float jitterY)
{
    static const float grid[][2] = {{0.f,0.f},{-1.f,-1.f},{0.f,-1.f},{1.f,-1.f},
        {-1.f,0.f},{1.f,0.f},{-1.f,1.f},{0.f,1.f},{1.f,1.f}};
    StockTemporalAAKernel result;
    const float invWidth = 1.f / width, invHeight = 1.f / height;
    float sum = 0.f, lowPassSum = 0.f;
    for (unsigned i = 0; i != 9; ++i)
    {
        const float y = (grid[i][1] - jitterY) * .875f;
        const float x = (grid[i][0] - jitterX) * .875f;
        const float exponent = (y * y) * -2.29f - (x * x) * 2.29f;
        const float weight = std::exp(exponent);
        result.weights[i] = weight;
        sum = weight + sum;
        const float lowX = x * .25f, lowY = y * .25f;
        const float lowExponent = (lowY * lowY) * -2.29f - (lowX * lowX) * 2.29f;
        const float lowWeight = std::exp(lowExponent);
        result.lowPassWeights[i] = lowWeight;
        lowPassSum = lowWeight + lowPassSum;
        result.offsets[i * 2] = invWidth * grid[i][0];
        result.offsets[i * 2 + 1] = invHeight * grid[i][1];
    }
    const float inverseSum = 1.f / sum, inverseLowPassSum = 1.f / lowPassSum;
    for (unsigned i = 0; i != 9; ++i)
    {
        result.weights[i] = inverseSum * result.weights[i];
        result.lowPassWeights[i] = inverseLowPassSum * result.lowPassWeights[i];
    }
    return result;
}

// 99ea50 pads the active input rectangle by one texel, clamped to the physical
// allocation, then 9522f0 converts it back to an unscaled viewport descriptor.
// This changes the viewport only; 999220 keeps Screen equal to the RT size.
inline std::uint32_t stockTemporalInputDimension(std::uint32_t physical, float scale)
{
    if (scale == 1.f) return physical;
    const int rounded = static_cast<int>(static_cast<float>(physical) * scale + .5f);
    const std::uint32_t active = static_cast<std::uint32_t>(rounded > 0 ? rounded : 1);
    const std::uint32_t padded = active < physical ? active + 1 : physical;
    return static_cast<std::uint32_t>(static_cast<float>(padded) / scale + .5f);
}
}
