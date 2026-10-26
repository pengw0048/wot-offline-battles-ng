#pragma once
#include "math/matrix.hpp"
#include "math/boundbox.hpp"

BW_BEGIN_NAMESPACE
// 6c72c0 supplies this transform to the original generic scene culler.
inline Matrix stockWaterCellTransform(const BoundingBox& local, const Matrix& water)
{
    Matrix result;
    result.setZero();
    if (!local.insideOut())
    {
        result.setScale(local.maxBounds()-local.minBounds());
        result.translation((local.maxBounds()+local.minBounds())*.5f);
    }
    D3DXMatrixMultiply(reinterpret_cast<D3DXMATRIX*>(&result),
        reinterpret_cast<const D3DXMATRIX*>(&result),reinterpret_cast<const D3DXMATRIX*>(&water));
    return result;
}
inline bool stockWaterCellEnabled(const Matrix& transform)
{
    // abb0a0: the Water registration has param5[1] == false.
    return transform[0][0] != 0.f || transform[1][1] != 0.f || transform[2][2] != 0.f;
}
inline BoundingBox stockWaterCellBounds(const Matrix& transform)
{
    // 11bf030 / 11bf890 initialise a +/-0.5 cube. 11bc780 transforms its
    // minimum with D3DXVec3Transform, then accumulates signed axis components.
    const Vector3 minimum(-.5f,-.5f,-.5f);
    Vector4 point;
    D3DXVec3Transform(reinterpret_cast<D3DXVECTOR4*>(&point),
        reinterpret_cast<const D3DXVECTOR3*>(&minimum),reinterpret_cast<const D3DXMATRIX*>(&transform));
    Vector3 low(point.x,point.y,point.z), high = low;
    for (int axis=0;axis<3;++axis)
        for (int component=0;component<3;++component)
        {
            const float value = transform[axis][component]; // unit-cube extent is 1
            if (value > 0.f) high[component] += value;
            else low[component] += value;
        }
    return BoundingBox(low,high);
}
BW_END_NAMESPACE
