#pragma once

#include <algorithm>

namespace wot
{
struct StockTextureQuality
{
    unsigned mipSkip;
    unsigned filter;
    bool compressed;
    bool supported;
};

inline StockTextureQuality stockTextureQuality(unsigned option, unsigned anisotropy)
{
    // #1513 94d8b0 stores these values in its four QualityOption records.
    const unsigned cap = std::min(anisotropy, 8u);
    switch (option)
    {
    // The public loader does not yet prepend the authored HD top mip.
    case 0: return {0, 1, false, false};
    case 1: return {1, cap > 4 ? 1u : 2u, false, cap >= 4};
    case 2: return {2, cap > 2 ? 2u : 3u, false, cap >= 2};
    default: return {2, 4, true, true};
    }
}

inline unsigned stockTextureLogicalMipSkip(unsigned qualitySkip, unsigned lodMode)
{
    // 94e900 is shared by initial loading and the quality-change callback.
    switch (lodMode)
    {
    case 1: return qualitySkip;
    case 2: return std::min(qualitySkip, 1u);
    case 3: return qualitySkip ? qualitySkip - 1 : 0;
    default: return 0;
    }
}

inline unsigned stockTextureBaseMipSkip(unsigned logicalSkip, unsigned splitCount,
    unsigned logicalMipCount)
{
    // 9b42c0 clamps against the logical chain before removing split top mips.
    // A missing header leaves the original zero count (unsigned -1 bound).
    const unsigned clipped = std::min(logicalSkip, logicalMipCount - 1);
    return clipped > splitCount ? clipped - splitCount : 0;
}
}
