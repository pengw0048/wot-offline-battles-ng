#pragma once
// Portable #1513 scene-query laws shared by the BigWorld binding and its tests.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wot {

// f61a90 classifies wg_collideSegment's optional fifth argument once.
struct StockSegmentFilter {
    std::uint16_t skipFlags = 0;   // +0x18 WorldTriangle::Flags
    long selectMask = -1;          // +0x28 PyInt value, -1 when absent
    bool callable = false;         // +0x1c PyCallable_Check(argument) == 1
};

enum class StockSegmentStep { Skip, Farther, Accept, Consult };

// f620a0 before the optional Python filter and the material-disabler check.
inline StockSegmentStep classifyStockSegmentTriangle(const StockSegmentFilter& filter,
    std::uint16_t triangleFlags, float distance, float nearest)
{
    if (filter.skipFlags & triangleFlags) return StockSegmentStep::Skip;
    if (distance > nearest) return StockSegmentStep::Farther;
    if (filter.selectMask >= 0)
        return (static_cast<unsigned long>(filter.selectMask) & triangleFlags & 0xffu) != 0 ?
            StockSegmentStep::Accept : StockSegmentStep::Skip;
    return filter.callable ? StockSegmentStep::Consult : StockSegmentStep::Accept;
}

// cvttss2si: NaN and out-of-range values produce the integer indefinite value.
inline std::int32_t stockTruncate(float value)
{
    if (!(value > -2147483904.f && value < 2147483648.f))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}

inline std::int32_t stockWrap(std::int64_t value)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value & 0xffffffff));
}

// a302e0 useEdgeAlpha_ branch after the bbActual test accepted local x/z.
// The original reads before the alpha table for a negative tap; such a tap
// takes its existing beyond-the-table branch here instead.
inline bool stockWaterEdgeAlphaAccepts(const std::uint32_t* alpha, std::size_t size,
    std::uint32_t gridX, std::uint32_t gridZ, float sizeX, float sizeZ,
    float tessellation, float localX, float localZ)
{
    const std::int32_t width = static_cast<std::int32_t>(gridX);
    const std::int32_t depth = static_cast<std::int32_t>(gridZ);
    const std::int32_t count = static_cast<std::int32_t>(size);
    const float inverse = 1.f / tessellation;
    const float x = (sizeX * .5f + localX) * inverse;
    const float z = (sizeZ * .5f + localZ) * inverse;
    auto alphaAt = [&](std::int32_t index) { return alpha[index] >> 24; };

    std::int32_t nearestX = stockTruncate(x + .5f), nearestZ = stockTruncate(z + .5f);
    if (depth < nearestZ) nearestZ = depth;
    if (width < nearestX) nearestX = width;
    const std::int32_t nearest = stockWrap(std::int64_t(nearestZ) * width + nearestX);
    if (nearest < count && nearest >= 0 && alphaAt(nearest) < 0x0a) return false;

    const std::int32_t floorX = stockTruncate(std::floor(x));
    const std::int32_t floorZ = stockTruncate(std::floor(z));
    std::int32_t ceilX = stockTruncate(std::ceil(x)), ceilZ = stockTruncate(std::ceil(z));
    if (width < ceilX) ceilX = width;
    if (depth < ceilZ) ceilZ = depth;
    const std::int32_t ceilRow = stockWrap(std::int64_t(width) * ceilZ);
    const std::int32_t floorRow = stockWrap(std::int64_t(width) * floorZ);
    const std::int32_t taps[4] = {
        stockWrap(std::int64_t(floorRow) + ceilX), stockWrap(std::int64_t(floorRow) + floorX),
        stockWrap(std::int64_t(ceilRow) + floorX), stockWrap(std::int64_t(ceilRow) + ceilX)};
    if (taps[3] >= count) return true;
    std::uint32_t sum = 0;
    for (std::int32_t tap : taps) {
        if (tap < 0 || tap >= count) return true;
        sum += alphaAt(tap);
    }
    return (sum & ~3u) >= 0x28;
}

} // namespace wot
