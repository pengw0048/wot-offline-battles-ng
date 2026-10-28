#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace wot
{
struct StockControlPointData
{
    struct Definition
    {
        float transform[16], radius;
        std::uint32_t team, base;
        float overTerrainHeight, radiusColour[4];
        std::uint32_t flag, flagstaff, radiusVisual;
        float flagScale;
        std::uint32_t sound, flags, visibilityMask;
    };
    std::vector<Definition> definitions;
    bool read(const void* bytes, std::size_t size, std::string& error)
    {
        definitions.clear(); error.clear();
        if (!bytes || size < 8) { error = "Truncated WTCP2 array"; return false; }
        std::uint32_t stride, count;
        std::memcpy(&stride, bytes, 4);
        std::memcpy(&count, static_cast<const char*>(bytes) + 4, 4);
        if (stride != 124 || count > (size - 8) / 124 || size - 8 != std::size_t(count) * 124)
        { error = "Invalid WTCP2 array extent"; return false; }
        std::vector<Definition> next(count);
        if (count) std::memcpy(next.data(), static_cast<const char*>(bytes) + 8, std::size_t(count) * 124);
        for (const auto& value : next)
        {
            for (float component : value.transform) if (!std::isfinite(component))
            { error = "Non-finite WTCP2 transform"; return false; }
            for (float component : value.radiusColour) if (!std::isfinite(component))
            { error = "Non-finite WTCP2 radius colour"; return false; }
            if (!std::isfinite(value.radius) || !std::isfinite(value.overTerrainHeight) ||
                !std::isfinite(value.flagScale))
            { error = "Non-finite WTCP2 scalar"; return false; }
        }
        definitions.swap(next); return true;
    }
    // 737fd0/7380e0 quantise to ARGB before the fashion expands it again.
    static std::uint32_t colour(const float* rgba)
    {
        std::uint32_t channels[4];
        for (unsigned i = 0; i != 4; ++i)
        {
            const float clamped = rgba[i] < 0.f ? 0.f : (rgba[i] < 1.f ? rgba[i] : 1.f);
            channels[i] = static_cast<std::uint32_t>(clamped * 255.f + .5f);
        }
        return (channels[3] << 24) | (channels[0] << 16) | (channels[1] << 8) | channels[2];
    }
    static void emblemTransform(const float* rect, float* output)
    {
        // 738270 deliberately does not reject zero/negative rectangles.
        output[0] = 1.f / (rect[2] - rect[0]);
        output[1] = 1.f / (rect[3] - rect[1]);
        output[2] = -(rect[0] * output[0]);
        output[3] = -(rect[1] * output[1]);
    }
};
static_assert(sizeof(StockControlPointData::Definition) == 124, "Original WTCP2 stride");
}
