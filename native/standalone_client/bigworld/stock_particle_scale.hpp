#pragma once

#include "math/matrix.hpp"
#include "moo/moo_math.hpp"

BW_BEGIN_NAMESPACE

// #1513 009e717f..009e7239: uniform scale precedes the supplied world
// transform; the local offset is transformed by that scaled basis. Scale is
// render-only, never an action/emission or bounding-box mutation.
inline Matrix stockParticleScaleTransform( const Matrix & world,
    const Vector3 & localOffset, float scale )
{
    Matrix result( world );
    if (scale == 1.f)
    {
        // Keep the public SDK's unscaled path exactly unchanged.
        result.preTranslateBy( localOffset );
        return result;
    }
    Matrix scaling;
    D3DXMatrixScaling( &scaling, scale, scale, scale );
    D3DXMatrixMultiply( &result, &scaling, &world );
    Vector3 shift;
    D3DXVec3TransformNormal( &shift, &localOffset, &result );
    result[3] += shift;
    return result;
}

BW_END_NAMESPACE
