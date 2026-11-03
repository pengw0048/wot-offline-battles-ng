#pragma once
#include <algorithm>
namespace wot {
struct CustomizationRegion {
    int area = -1, region = -1;
};
// #1513 a40174..a40205 decodes the alpha returned by READBACK_SELECTION.
// A zero byte is the renderer's no-region sentinel, not chassis region zero.
inline CustomizationRegion customizationRegion( unsigned raw )
{
    if (!raw) return {};
    const float packed = std::max( 0.f, (static_cast<float>( raw ) - 2.f) *
        0.003952569328248501f ) * 4.f;
    const int area = static_cast<int>( packed + 0.05f );
    return { area, static_cast<int>( (packed - area) * 4.f + 0.05f ) };
}
inline int customizationMaskMode( int mode, int part )
{
    return mode == 4 ? 1 : mode == 3 && part != 3 ? 1 : mode;
}
inline bool customizationDepthOnly( int mode, int part, bool tracks )
{
    return tracks || ((mode == 1 || mode == 4) && part == 0) ||
        (mode == 4 && part == 2);
}
}
