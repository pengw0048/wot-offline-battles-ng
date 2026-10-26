#pragma once

// Actual Win32 entry points called by the #1513 Tank construction graph.
// Header-only so the native probe executes the same adapter as the client.
#include "wot/vehicle_physics.hpp"

#include <windows.h>
#include <d3dx9math.h>
#include <cstring>
#include <stdexcept>

namespace stock_vehicle_native_math
{
// The original thunk 0x013dad43 jumps through the IAT entry for
// api-ms-win-crt-math-l1-1-0.dll!__libm_sse2_tanf, which takes and
// returns xmm0. std::tan under /fp:strict reaches a different UCRT entry.
__declspec(selectany) void * g_tangentEntry = NULL;

inline void * resolveTangentEntry()
{
    if (!g_tangentEntry)
    {
        HMODULE module = GetModuleHandleW( L"ucrtbase.dll" );
        if (!module) module = LoadLibraryW( L"api-ms-win-crt-math-l1-1-0.dll" );
        g_tangentEntry = module ? reinterpret_cast<void *>(
            GetProcAddress( module, "__libm_sse2_tanf" ) ) : NULL;
        if (!g_tangentEntry)
            throw std::runtime_error( "UCRT __libm_sse2_tanf is unavailable" );
    }
    return g_tangentEntry;
}

__declspec(naked) inline float __cdecl tangent( float )
{
    __asm
    {
        movss xmm0, dword ptr [esp + 4]
        call dword ptr [g_tangentEntry]
        movss dword ptr [esp + 4], xmm0
        fld dword ptr [esp + 4]
        ret
    }
}

inline wot::Vector3 normalize( wot::Vector3 value )
{
    const D3DXVECTOR3 source( value.x, value.y, value.z );
    D3DXVECTOR3 result;
    D3DXVec3Normalize( &result, &source );
    return { result.x, result.y, result.z };
}

inline D3DXMATRIX matrix( const wot::VehicleMatrix & value )
{
    D3DXMATRIX result;
    std::memcpy( &result, value.rows, sizeof( value.rows ) );
    return result;
}

inline wot::Vector3 transformCoordinate( wot::Vector3 value, const wot::VehicleMatrix & transform )
{
    const D3DXVECTOR3 source( value.x, value.y, value.z );
    const D3DXMATRIX native = matrix( transform );
    D3DXVECTOR4 result;
    D3DXVec3Transform( &result, &source, &native );
    return { result.x, result.y, result.z };
}

inline wot::Vector3 transformNormal( wot::Vector3 value, const wot::VehicleMatrix & transform )
{
    const D3DXVECTOR3 source( value.x, value.y, value.z );
    const D3DXMATRIX native = matrix( transform );
    D3DXVECTOR3 result;
    D3DXVec3TransformNormal( &result, &source, &native );
    return { result.x, result.y, result.z };
}

inline void inverse( wot::VehicleMatrix & destination, const wot::VehicleMatrix & source )
{
    // Start from the destination: a NULL return must leave it as D3DX left it.
    D3DXMATRIX result = matrix( destination );
    const D3DXMATRIX input = matrix( source );
    D3DXMatrixInverse( &result, NULL, &input );
    std::memcpy( destination.rows, &result, sizeof( destination.rows ) );
}

inline const wot::VehicleNativeMath & services()
{
    resolveTangentEntry();
    static const wot::VehicleNativeMath result =
        { &tangent, &normalize, &transformCoordinate, &transformNormal, &inverse };
    return result;
}
}
