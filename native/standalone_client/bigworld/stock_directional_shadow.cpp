#include "pch.hpp"
#include "stock_directional_shadow.hpp"
#include "pyscript/script_math.hpp"
#include "wot/stock_shadow_rectangle.hpp"

#include <utility>

BW_BEGIN_NAMESPACE
namespace
{
struct DirectionalShadowFocus
{
    MatrixProviderPtr target;
    unsigned generation = 0;
    bool open = false;
};

DirectionalShadowFocus & directionalShadowFocus()
{
    // Explicit script teardown releases Python owners before interpreter fini.
    static DirectionalShadowFocus * state = new DirectionalShadowFocus;
    return *state;
}
}

void setStockDirectionalShadowFocus( MatrixProvider * target )
{
    DirectionalShadowFocus & state = directionalShadowFocus();
    if (state.target.get() == target || (target && !state.open)) return;
    MatrixProviderPtr previous( target );
    std::swap( state.target, previous );
    ++state.generation;
}

void openStockDirectionalShadowFocus()
{
    directionalShadowFocus().open = true;
}

void closeStockDirectionalShadowFocus()
{
    directionalShadowFocus().open = false;
    setStockDirectionalShadowFocus( NULL );
}

bool stockDirectionalShadowFocus( Matrix & result )
{
    DirectionalShadowFocus & state = directionalShadowFocus();
    const MatrixProviderPtr target = state.target;
    const unsigned generation = state.generation;
    if (!state.open || !target) return false;
    Matrix snapshot;
    target->matrix( snapshot );
    if (!state.open || generation != state.generation) return false;
    result = snapshot;
    return true;
}

bool stockDirectionalShadowTightMatrices( Matrix & view, Matrix & projection,
    Matrix & texture, Matrix & cullViewProjection,
    const Matrix & splitViewProjection, const Vector3 & lightDirection,
    float lightDistance )
{
    const D3DXVECTOR3 zero( 0.f, 0.f, 0.f );
    const D3DXVECTOR3 direction( lightDirection.x, lightDirection.y, lightDirection.z );
    D3DXVECTOR3 up( 0.f, 1.f, 0.f );
    if (direction.x*direction.x+direction.z*direction.z < .001f)
        up = D3DXVECTOR3( 0.f, 0.f, 1.f );
    Matrix lightView, inverseLightView, lightSplit, inverseLightSplit;
    D3DXMatrixLookAtLH( &lightView, &zero, &direction, &up );
    if (!D3DXMatrixInverse( &inverseLightView, NULL, &lightView )) return false;
    D3DXMatrixMultiply( &lightSplit, &inverseLightView, &splitViewProjection );
    if (!D3DXMatrixInverse( &inverseLightSplit, NULL, &lightSplit )) return false;

    wot::StockShadowPoint2 points[8];
    float minZ = std::numeric_limits<float>::max(), maxZ = -minZ;
    for (unsigned i = 0; i < 8; ++i)
    {
        // 9d9b70 corner order is significant for equal-area rectangle ties.
        const D3DXVECTOR3 corner( (i&1) ? -1.f : 1.f,
            (i&2) ? -1.f : 1.f, (i&4) ? 0.f : 1.f );
        D3DXVECTOR3 point;
        D3DXVec3TransformCoord( &point, &corner, &inverseLightSplit );
        points[i] = {point.x, point.y};
        minZ = std::min(minZ,point.z); maxZ = std::max(maxZ,point.z);
    }
    const wot::StockShadowRectangle2 rectangle = wot::stock_shadow_rectangle(points,8);
    const float width = 2.f*std::sqrt(rectangle.axis1.x*rectangle.axis1.x+
        rectangle.axis1.y*rectangle.axis1.y);
    const float height = 2.f*std::sqrt(rectangle.axis0.x*rectangle.axis0.x+
        rectangle.axis0.y*rectangle.axis0.y);
    // 9cb46a supplies 500m light distance, 10m extra light depth, zero
    // cull padding. The nearest split is deliberately not texel-snapped.
    D3DXMatrixOrthoLH( &projection, width, height, 0.f, maxZ-minZ+lightDistance+10.f );
    const D3DXVECTOR3 centerLight( rectangle.center.x, rectangle.center.y, minZ );
    const D3DXVECTOR3 axisLight( rectangle.axis0.x, rectangle.axis0.y, 0.f );
    D3DXVECTOR3 centerWorld, axisWorld;
    D3DXVec3TransformCoord( &centerWorld, &centerLight, &inverseLightView );
    D3DXVec3TransformNormal( &axisWorld, &axisLight, &inverseLightView );
    const D3DXVECTOR3 eye( centerWorld.x-lightDistance*direction.x,
        centerWorld.y-lightDistance*direction.y, centerWorld.z-lightDistance*direction.z );
    D3DXMatrixLookAtLH( &view, &eye, &centerWorld, &axisWorld );
    D3DXMatrixMultiply( &cullViewProjection, &view, &projection );
    Matrix scaleBias;
    D3DXMatrixIdentity( &scaleBias );
    scaleBias(0,0) = .5f; scaleBias(1,1) = -.5f;
    scaleBias(3,0) = .5f; scaleBias(3,1) = .5f;
    D3DXMatrixMultiply( &texture, &cullViewProjection, &scaleBias );
    return true;
}
BW_END_NAMESPACE
