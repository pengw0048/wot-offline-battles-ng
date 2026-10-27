#pragma once

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace wot
{
// Old-ST4 SpeedTreeRenderer transparency state. Only the camera target has a
// restored writer; wg_enableTreeHiding 6b1800, wg_setTreeHidingRadius 6b3d00
// and wg_enableTreeTransparency 6b1880 own the rest in #1513.
struct StockTreeHidingSettings
{
    bool hideClosest = false;
    float squaredHideRadius = 0.f, hideRadius = 0.f, hideRadiusAlpha = 0.f;
    bool transparency = true;
    float hiddenLeavesAlpha = .4f, hiddenBranchAlpha = .7f;
};

// b035a0: camera 1d59f80, target 1d59f8c and distance 1d59f1c.
struct StockTreeHidingFrame
{
    float camera[3] = { 0.f, 0.f, 0.f };
    float target[3] = { 0.f, 0.f, 0.f };
    float distance = 0.f;
};

struct StockTreeFade
{
    float leaves = 1.f, branches = 1.f;
    // afeec9: NaN is faded.
    bool faded() const { return leaves != 1.f || branches != 1.f; }
};

// world is a row-major D3D matrix with the translation in elements 12..14.
inline void captureStockTreeHiding( const float * world, const float * camera, StockTreeHidingFrame & frame )
{
    for (unsigned i = 0; i < 3; ++i) frame.camera[i] = camera[i];
    frame.target[0] = world[12];
    frame.target[1] = world[13] + 1.f;
    frame.target[2] = world[14];
    const float x = frame.camera[0] - world[12];
    const float y = frame.camera[1] - frame.target[1];
    const float z = frame.camera[2] - world[14];
    frame.distance = std::sqrt( (x * x + y * y) + z * z );
}

// 11bdf60 LineInEllipsoid. Its max-min is returned unrounded on the x87 stack.
inline double stockTreeLineInEllipsoid( const float * size, const float * start, const float * end )
{
    const float ix = 1.f / size[0], iy = 1.f / size[1], iz = 1.f / size[2];
    const float vx = ix * start[0], vy = iy * start[1], vz = iz * start[2];
    const float dx = ix * (end[0] - start[0]), dy = iy * (end[1] - start[1]), dz = iz * (end[2] - start[2]);
    const float a = (dx * dx + dy * dy) + dz * dz;
    const float b = (dx * vx + dx * vx + (dy * vy + dy * vy)) + (dz * vz + dz * vz);
    const float c = ((vy * vy + vx * vx) + vz * vz) - 1.f;
    const float discriminant = b * b - c * a * 4.f;
    if (!(discriminant > 0.f)) return 0.0;
    const float root = std::sqrt( discriminant );
    const float scale = .5f / a;
    float t0 = (root - b) * scale, t1 = (-b - root) * scale;
    if (0.f > t0) t0 = 0.f; else if (t0 > 1.f) t0 = 1.f;
    if (0.f > t1) t1 = 0.f; else if (t1 > 1.f) t1 = 1.f;
    const bool unordered = t0 != t0 || t1 != t1;
    const float high = t1 <= t0 || unordered ? t0 : t1;
    const float low = t0 <= t1 || unordered ? t0 : t1;
    return double( high ) - double( low );
}

struct StockTreeFadeInput
{
    const float * world;
    float boundsMin[3], boundsMax[3];
    float eye[3];
    float lodDistance;
    bool bush, model3d;
};

// afe460 0afe510..0afe8ec. Ops supplies the D3DX TransformNormal, in-place
// Inverse and Vec3Transform that the original calls.
template <class Ops>
StockTreeFade computeStockTreeFade( const StockTreeHidingSettings & settings,
    const StockTreeHidingFrame * frame, const StockTreeFadeInput & input )
{
    const float * world = input.world;
    float fade = 1.f;
    const float px = world[12] - input.eye[0], pz = world[14] - input.eye[2], py = world[13] - input.eye[1];
    const float squared = (px * px + py * py) + pz * pz;
    if (settings.hideClosest && settings.squaredHideRadius > squared)
        fade = (std::sqrt( squared ) - settings.hideRadiusAlpha) / (settings.hideRadius - settings.hideRadiusAlpha);
    if (frame && settings.transparency)
    {
        const float * low = input.boundsMin, * high = input.boundsMax;
        const float ex = high[0] - low[0], ey = high[1] - low[1], ez = high[2] - low[2];
        const float diagonal = std::sqrt( (ex * ex + ey * ey) + ez * ez );
        const float height = low[0] > high[0] || low[1] > high[1] || low[2] > high[2] ? 0.f : high[1] - low[1];
        if (!input.bush && input.model3d && world[5] * height > 6.f &&
            frame->distance > input.lodDistance - diagonal)
        {
            float centre[16];
            std::memcpy( centre, world, sizeof( centre ) );
            const float lift[3] = { 0.f, height * .5f, 0.f };
            float offset[3];
            Ops::transformNormal( offset, lift, centre );
            centre[12] += offset[0];
            centre[13] += offset[1];
            centre[14] += offset[2];
            Ops::inverse( centre );
            float camera[3], target[3];
            Ops::transformPoint( camera, frame->camera, centre );
            Ops::transformPoint( target, frame->target, centre );
            const float half[3] = { ex * .5f, ey * .5f, ez * .5f };
            const float radius = std::sqrt( (half[0] * half[0] + half[1] * half[1]) + half[2] * half[2] );
            const float grow = (radius + 1.2f) / radius;
            const float outer[3] = { half[0] * grow, half[1] * grow, half[2] * grow };
            const float big = float( stockTreeLineInEllipsoid( outer, camera, target ) );
            if (big > 1e-8f)
            {
                const float hidden = float( 1.0 - stockTreeLineInEllipsoid( half, camera, target ) / double( big ) );
                fade = hidden < fade ? hidden : fade;
            }
        }
    }
    StockTreeFade result;
    result.leaves = settings.hiddenLeavesAlpha > fade ? settings.hiddenLeavesAlpha : fade;
    result.branches = settings.hiddenBranchAlpha > fade ? settings.hiddenBranchAlpha : fade;
    return result;
}

// b058f0 over b012cd: the lower-cased file name contains a bushes.xml name.
inline bool stockTreeIsBush( const std::vector<std::string> & bushes, const std::string & lowerFileName )
{
    for (const auto & bush : bushes)
        if (std::strstr( lowerFileName.c_str(), bush.c_str() )) return true;
    return false;
}
}
