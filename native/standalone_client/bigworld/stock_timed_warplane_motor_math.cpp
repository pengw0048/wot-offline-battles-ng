#include "pch.hpp"
#include "stock_timed_warplane_motor.hpp"

#include <windows.h>
#include <d3dx9math.h>
#include <cstring>
#include <stdexcept>

BW_BEGIN_NAMESPACE
namespace stock_timed_warplane_math
{
// 00734bf4 calls UCRT __libm_sse2_atanf (xmm0 in and out).
void * g_atanEntry = NULL;

__declspec(naked) float __cdecl sse2Atan( float )
{
    __asm
    {
        movss xmm0, dword ptr [esp + 4]
        call dword ptr [g_atanEntry]
        movss dword ptr [esp + 4], xmm0
        fld dword ptr [esp + 4]
        ret
    }
}
}

namespace
{
D3DXMATRIX native( const wot::StockWarplaneMatrix & value )
{
    D3DXMATRIX result;
    std::memcpy( &result, value.data(), sizeof( result ) );
    return result;
}

wot::StockWarplaneMatrix portable( const D3DXMATRIX & value )
{
    wot::StockWarplaneMatrix result;
    std::memcpy( result.data(), &value, sizeof( value ) );
    return result;
}

wot::StockWarplaneMatrix lookAtLH( const wot::StockWarplaneVector & eye, const wot::StockWarplaneVector & at,
    const wot::StockWarplaneVector & up )
{
    const D3DXVECTOR3 e( eye[0], eye[1], eye[2] );
    const D3DXVECTOR3 a( at[0], at[1], at[2] );
    const D3DXVECTOR3 u( up[0], up[1], up[2] );
    D3DXMATRIX result;
    D3DXMatrixLookAtLH( &result, &e, &a, &u );
    return portable( result );
}

void inverse( wot::StockWarplaneMatrix & value )
{
    // 00734ad1 inverts in place; a singular input stays as D3DX left it.
    D3DXMATRIX result = native( value );
    D3DXMatrixInverse( &result, NULL, &result );
    value = portable( result );
}

std::array<float, 2> normalize2( float x, float y )
{
    D3DXVECTOR2 value( x, y );
    D3DXVec2Normalize( &value, &value );
    return {{ value.x, value.y }};
}

wot::StockWarplaneMatrix rotationZ( float angle )
{
    D3DXMATRIX result;
    std::memset( &result, 0, sizeof( result ) );
    D3DXMatrixRotationZ( &result, angle );
    return portable( result );
}

wot::StockWarplaneMatrix multiply( const wot::StockWarplaneMatrix & first, const wot::StockWarplaneMatrix & second )
{
    const D3DXMATRIX left = native( first );
    const D3DXMATRIX right = native( second );
    D3DXMATRIX result;
    D3DXMatrixMultiply( &result, &left, &right );
    return portable( result );
}
}

const wot::StockWarplaneMath & stockTimedWarplaneMath()
{
    using stock_timed_warplane_math::g_atanEntry;
    if (!g_atanEntry)
    {
        HMODULE module = GetModuleHandleW( L"ucrtbase.dll" );
        if (!module) module = LoadLibraryW( L"api-ms-win-crt-math-l1-1-0.dll" );
        g_atanEntry = module ? reinterpret_cast<void *>( GetProcAddress( module, "__libm_sse2_atanf" ) ) : NULL;
        if (!g_atanEntry)
            throw std::runtime_error( "UCRT __libm_sse2_atanf is unavailable" );
    }
    static const wot::StockWarplaneMath result = { &lookAtLH, &inverse, &normalize2,
        &stock_timed_warplane_math::sse2Atan, &rotationZ, &multiply };
    return result;
}
BW_END_NAMESPACE
