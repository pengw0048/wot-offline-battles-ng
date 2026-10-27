#pragma once

#include <cmath>
#include <string>

namespace wot
{
namespace stock_battle_presentation
{
// Row-vector matrix with the translation in row 3 (BigWorld/D3DX layout).
struct Matrix44
{
    float m[4][4];
};

struct Vector4f
{
    float x, y, z, w;
};

// Scaleform Matrix4F (rows, translation in column 3) and Matrix3F (3x4).
struct GfxMatrix4
{
    float m[4][4];
};

struct GfxMatrix3
{
    float m[3][4];
};

// d3dx9 D3DXVec3Transform: [x y z 1] * M without a perspective divide.
inline Vector4f transformPoint(const Matrix44& matrix, float x, float y, float z)
{
    const float (&m)[4][4] = matrix.m;
    Vector4f result = {
        x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0],
        x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1],
        x * m[0][2] + y * m[1][2] + z * m[2][2] + m[3][2],
        x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3]};
    return result;
}

// Scaleform Matrix4x4::MultiplyMatrix(m1, m2) = m1 * m2 (#1513 00b5f940).
inline GfxMatrix4 multiply(const GfxMatrix4& a, const GfxMatrix4& b)
{
    GfxMatrix4 result;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            result.m[row][column] = a.m[row][0] * b.m[0][column] + a.m[row][1] * b.m[1][column] +
                a.m[row][2] * b.m[2][column] + a.m[row][3] * b.m[3][column];
    return result;
}

inline GfxMatrix4 gfxIdentity4()
{
    GfxMatrix4 result = {{{1.f, 0.f, 0.f, 0.f}, {0.f, 1.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f}, {0.f, 0.f, 0.f, 1.f}}};
    return result;
}

// Scaleform "{0}{1}" (#1513 013e48ae, 013e97ea): the symbol and the decimal ID.
inline std::string instanceName(const std::string& symbol, unsigned id)
{
    std::string digits;
    do
    {
        digits.insert(digits.begin(), static_cast<char>('0' + id % 10));
        id /= 10;
    } while (id != 0);
    return symbol + digits;
}

// The render-context values read by the per-frame presenters.
struct Camera
{
    Matrix44 view;
    Matrix44 viewProjection;
    float nearPlane;
    float farPlane;
    float zoomFactor;
    float halfScreenWidth;
    float halfScreenHeight;
};
}
}
