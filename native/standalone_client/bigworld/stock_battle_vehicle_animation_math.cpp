#include "pch.hpp"
#include "stock_battle_vehicle_animation.hpp"

#include <windows.h>
#include <d3dx9math.h>
#include <cstring>
#include <stdexcept>
#include <string>

BW_BEGIN_NAMESPACE
namespace stock_battle_animation_math
{
// 0078a3b2/0078c0e6 call UCRT __libm_sse2_asinf (xmm0 in and out), not the
// x87 asin entry that std::asin reaches under /fp:strict.
void * g_asinEntry = NULL;
void * g_cosEntry = NULL;
void * g_sinEntry = NULL;

__declspec(naked) float __cdecl sse2Asin( float )
{
    __asm
    {
        movss xmm0, dword ptr [esp + 4]
        call dword ptr [g_asinEntry]
        movss dword ptr [esp + 4], xmm0
        fld dword ptr [esp + 4]
        ret
    }
}

// 007860c0 calls __libm_sse2_cosf/__libm_sse2_sinf with the same convention.
__declspec(naked) float __cdecl sse2Cos( float )
{
    __asm
    {
        movss xmm0, dword ptr [esp + 4]
        call dword ptr [g_cosEntry]
        movss dword ptr [esp + 4], xmm0
        fld dword ptr [esp + 4]
        ret
    }
}

__declspec(naked) float __cdecl sse2Sin( float )
{
    __asm
    {
        movss xmm0, dword ptr [esp + 4]
        call dword ptr [g_sinEntry]
        movss dword ptr [esp + 4], xmm0
        fld dword ptr [esp + 4]
        ret
    }
}

// 00787710/00789060 evaluate fld y; fld x; fpatan; fstp dword.
float x87Atan2( float y, float x )
{
    float result;
    __asm
    {
        fld dword ptr [y]
        fld dword ptr [x]
        fpatan
        fstp dword ptr [result]
    }
    return result;
}

void * ucrtEntry( const char * name )
{
    HMODULE module = GetModuleHandleW( L"ucrtbase.dll" );
    if (!module) module = LoadLibraryW( L"api-ms-win-crt-math-l1-1-0.dll" );
    void * entry = module ? reinterpret_cast<void *>( GetProcAddress( module, name ) ) : NULL;
    if (!entry)
        throw std::runtime_error( std::string( "UCRT " ) + name + " is unavailable" );
    return entry;
}
}

namespace
{
using stock_battle_animation_math::sse2Asin;

D3DXMATRIX native( const wot::StockAnimationMatrix & value )
{
    D3DXMATRIX result;
    std::memcpy( &result, value.data(), sizeof( result ) );
    return result;
}

wot::StockAnimationMatrix portable( const D3DXMATRIX & value )
{
    wot::StockAnimationMatrix result;
    std::memcpy( result.data(), &value, sizeof( value ) );
    return result;
}

void transform( float out[4], const wot::StockAnimationVector & value, const wot::StockAnimationMatrix & matrix )
{
    const D3DXVECTOR3 source( value[0], value[1], value[2] );
    const D3DXMATRIX transform = native( matrix );
    D3DXVECTOR4 result;
    D3DXVec3Transform( &result, &source, &transform );
    out[0] = result.x; out[1] = result.y; out[2] = result.z; out[3] = result.w;
}

wot::StockAnimationVector transformNormal( const wot::StockAnimationVector & value,
    const wot::StockAnimationMatrix & matrix )
{
    const D3DXVECTOR3 source( value[0], value[1], value[2] );
    const D3DXMATRIX transform = native( matrix );
    D3DXVECTOR3 result;
    D3DXVec3TransformNormal( &result, &source, &transform );
    return {{ result.x, result.y, result.z }};
}

void inverse( wot::StockAnimationMatrix & value )
{
    // The originals invert in place; a singular input stays as D3DX left it.
    D3DXMATRIX result = native( value );
    D3DXMatrixInverse( &result, NULL, &result );
    value = portable( result );
}

wot::StockAnimationVector normalize( const wot::StockAnimationVector & value )
{
    const D3DXVECTOR3 source( value[0], value[1], value[2] );
    D3DXVECTOR3 result;
    D3DXVec3Normalize( &result, &source );
    return {{ result.x, result.y, result.z }};
}

wot::StockAnimationMatrix rotationYawPitchRoll( float yaw, float pitch, float roll )
{
    D3DXMATRIX result;
    D3DXMatrixRotationYawPitchRoll( &result, yaw, pitch, roll );
    return portable( result );
}

wot::StockAnimationMatrix rotationX( float angle )
{
    D3DXMATRIX result;
    std::memset( &result, 0, sizeof( result ) );
    D3DXMatrixRotationX( &result, angle );
    return portable( result );
}

wot::StockAnimationMatrix multiply( const wot::StockAnimationMatrix & first,
    const wot::StockAnimationMatrix & second )
{
    const D3DXMATRIX left = native( first );
    const D3DXMATRIX right = native( second );
    D3DXMATRIX result;
    D3DXMatrixMultiply( &result, &left, &right );
    return portable( result );
}
}

const wot::StockAnimationMath & stockBattleAnimationMath()
{
    if (!stock_battle_animation_math::g_asinEntry)
        stock_battle_animation_math::g_asinEntry = stock_battle_animation_math::ucrtEntry( "__libm_sse2_asinf" );
    static const wot::StockAnimationMath result = { &transform, &transformNormal, &inverse,
        &normalize, &rotationYawPitchRoll, &rotationX, &multiply, &sse2Asin };
    return result;
}

const wot::StockLeveredMath & stockBattleLeveredMath()
{
    using namespace stock_battle_animation_math;
    if (!g_cosEntry)
        g_cosEntry = ucrtEntry( "__libm_sse2_cosf" );
    if (!g_sinEntry)
        g_sinEntry = ucrtEntry( "__libm_sse2_sinf" );
    static const wot::StockLeveredMath result = { stockBattleAnimationMath(), &sse2Cos, &sse2Sin, &x87Atan2 };
    return result;
}
BW_END_NAMESPACE
