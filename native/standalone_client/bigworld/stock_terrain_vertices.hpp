#pragma once

#include "cstdmf/stdmf.hpp"
#include "math/vector2.hpp"
#include <d3dx9math.h>

BW_BEGIN_NAMESPACE
namespace Terrain
{

// #1513 aeae70/aeac60 and shaders/formats/terrain.xml: the shared grid is
// TEXCOORD0/HALF4 on stream 0; current/next heights are POSITION0/HALF2 on 1.
struct StockTerrainGridVertex { D3DXFLOAT16 values[4]; };
struct StockTerrainHeightVertex { D3DXFLOAT16 values[2]; };
static_assert( sizeof( StockTerrainGridVertex ) == 8, "Stock terrain grid stride" );
static_assert( sizeof( StockTerrainHeightVertex ) == 4, "Stock terrain height stride" );

inline bool stockTerrainGridMorphEnabled( uint32 resolutionX, uint32 resolutionZ )
{
    // aeb19f..aeb1ca enables the four-period table only for complete periods.
    return (resolutionX - 1) % 4 == 0 && (resolutionZ - 1) % 4 == 0;
}

inline void packStockTerrainGridVertex( StockTerrainGridVertex & destination,
    float x, float z, uint32 column, uint32 row, bool morph )
{
    // Original table 0150e110. GridOnly VSs consume zw to morph x/z and sample
    // the height atlas; ordinary VSs consume only the normalised grid xy.
    static const float phase[4] = { 0.f, 1.f, 0.f, -1.f };
    const float values[4] = { x, z, morph ? phase[column % 4] : 0.f,
        morph ? phase[row % 4] : 0.f };
    D3DXFloat32To16Array( destination.values, values, 4 );
}

inline void packStockTerrainHeightVertex( StockTerrainHeightVertex & destination,
    const Vector2 & source )
{
    // aea760 converts the final two heights, including the authored 0.007
    // geomorph fix. Keep the public CPU generation and interpolation unchanged.
    const float values[2] = { source.x, source.y };
    D3DXFloat32To16Array( destination.values, values, 2 );
}

} // namespace Terrain
BW_END_NAMESPACE
