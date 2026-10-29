#pragma once

// #1513 BigWorld.WGBspCollisionModel (type 01a5b1c0). The core below is the
// per-triangle arithmetic of its collectors; it has no engine dependency.
#include <cmath>
#include <cstdint>
#include <vector>

namespace wot
{
namespace stock_bsp
{
struct Point
{
    float x, y, z;
};

// CollideSegmentTriangleCollector set element (00f43ee0), ordered by 00f43e30.
struct SegmentHit
{
    std::uint32_t materialKind;
    float distance;
    Point normal;
};

// 00f43e30: distances within float(0.0004) order by raw normal, then kind.
inline bool segmentHitLess( const SegmentHit & a, const SegmentHit & b )
{
    // jb after comiss: an unordered difference takes the near branch.
    if (std::fabs( a.distance - b.distance ) >= 0.0004f)
        return b.distance > a.distance;
    if (a.normal.x == b.normal.x && a.normal.y == b.normal.y && a.normal.z == b.normal.z)
        return a.materialKind < b.materialKind;
    if (b.normal.x > a.normal.x) return true;
    if (a.normal.x > b.normal.x) return false;
    if (b.normal.y > a.normal.y) return true;
    if (a.normal.y > b.normal.y) return false;
    return b.normal.z > a.normal.z;
}

struct SegmentHitOrder
{
    bool operator()( const SegmentHit & a, const SegmentHit & b ) const
    {
        return segmentHitLess( a, b );
    }
};

// 00f43f1e and 00f44140: unnormalised (v2 - v0) x (v2 - v1).
inline Point rawNormal( const Point & v0, const Point & v1, const Point & v2 )
{
    const float ex = v2.x - v0.x, ey = v2.y - v0.y, ez = v2.z - v0.z;
    const float fx = v2.x - v1.x, fy = v2.y - v1.y, fz = v2.z - v1.z;
    const Point result = { fz * ey - fy * ez, fx * ez - fz * ex, fy * ex - fx * ey };
    return result;
}

// 00f4514a..00f451de: face the normalised normal against the unit ray and
// clamp the incidence cosine with comiss/minss operand order.
inline float facedCosine( Point & normal, const Point & direction )
{
    float cosine = normal.x * -direction.x + normal.y * -direction.y;
    cosine = cosine + normal.z * -direction.z;
    if (0.f > cosine)
    {
        normal.x = -normal.x;
        normal.y = -normal.y;
        normal.z = -normal.z;
        cosine = -cosine;
    }
    if (-1.f > cosine)
        return -1.f;
    return cosine < 1.f ? cosine : 1.f;
}

// 00f43ffb..00f440bf: bOutsidePolygonsOnly rejects a centre behind the face.
inline bool sphereCentreBehind( const Point & centre, const Point & v0, const Point & v1,
    const Point & v2 )
{
    const Point n = rawNormal( v0, v1, v2 );
    const float dx = centre.x - v0.x, dy = centre.y - v0.y, dz = centre.z - v0.z;
    const float side = (n.y * dy + dx * n.x) + dz * n.z;
    return -1e-05f > side;
}

// 00f666e0: closest point on triangle (a, b, c) to p.
inline Point closestPoint( const Point & p, const Point & a, const Point & b, const Point & c )
{
    const float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    const float d1 = abx * (p.x - a.x) + aby * (p.y - a.y) + abz * (p.z - a.z);
    const float d2 = acx * (p.x - a.x) + acy * (p.y - a.y) + acz * (p.z - a.z);
    if (d1 <= 0.f && d2 <= 0.f)
        return a;
    const float d3 = aby * (p.y - b.y) + abx * (p.x - b.x) + abz * (p.z - b.z);
    const float d4 = acy * (p.y - b.y) + acx * (p.x - b.x) + acz * (p.z - b.z);
    if (0.f <= d3 && d4 <= d3)
        return b;
    const float vc = d4 * d1 - d3 * d2;
    if (0.f < vc || d1 < 0.f || 0.f < d3)
    {
        const float cpy = p.y - c.y;
        const float d5 = aby * cpy + abx * (p.x - c.x) + abz * (p.z - c.z);
        const float d6 = cpy * acy + acx * (p.x - c.x) + (p.z - c.z) * acz;
        if (0.f <= d6 && d5 <= d6)
            return c;
        const float vb = d5 * d2 - d6 * d1;
        if (0.f < vb || d2 < 0.f || 0.f < d6)
        {
            const float va = d6 * d3 - d5 * d4;
            const float bc = d4 - d3;
            if (va <= 0.f && 0.f <= bc && 0.f <= d5 - d6)
            {
                const float denominator = (d5 - d6) + bc;
                if (denominator == 0.f)
                    return b;
                const float w = bc / denominator;
                const Point result = { b.x + (c.x - b.x) * w, b.y + (c.y - b.y) * w,
                    b.z + (c.z - b.z) * w };
                return result;
            }
            const float denominator = va + vb + vc;
            if (denominator == 0.f)
            {
                const Point result = { (abx + acx) * 0.25f + a.x, (aby + acy) * 0.25f + a.y,
                    (abz + acz) * 0.25f + a.z };
                return result;
            }
            const float inverse = 1.f / denominator;
            const float w = inverse * vc;
            const float v = inverse * vb;
            const Point result = { a.x + abx * v + acx * w, a.y + aby * v + acy * w,
                a.z + abz * v + acz * w };
            return result;
        }
        if (d2 - d6 != 0.f)
        {
            const float w = d2 / (d2 - d6);
            const Point result = { a.x + acx * w, a.y + acy * w, a.z + acz * w };
            return result;
        }
    }
    else if (d1 - d3 != 0.f)
    {
        const float v = d1 / (d1 - d3);
        const Point result = { a.x + abx * v, a.y + aby * v, a.z + abz * v };
        return result;
    }
    return a;
}

// CollideSphereTriangleCollector vector element (00f43fd0), 0x20 bytes.
struct SphereHit
{
    Point point;
    Point normal;
    float distanceSquared;
    std::uint32_t materialKind;
};

// 00f440c5..00f441e5 after the flag and facing checks: keep the nearest
// point per material kind, in first-contact order; the first normal stays.
inline void collectSphereHit( std::vector< SphereHit > & hits, const Point & centre,
    float radiusSquared, const Point & v0, const Point & v1, const Point & v2,
    std::uint32_t materialKind )
{
    const Point point = closestPoint( centre, v0, v1, v2 );
    const float dy = point.y - centre.y, dx = point.x - centre.x, dz = point.z - centre.z;
    const float distanceSquared = (dy * dy + dx * dx) + dz * dz;
    if (distanceSquared > radiusSquared)
        return;
    for (SphereHit & hit : hits)
    {
        if (hit.materialKind != materialKind)
            continue;
        if (hit.distanceSquared > distanceSquared)
        {
            hit.distanceSquared = distanceSquared;
            hit.point = point;
        }
        return;
    }
    const SphereHit hit = { point, rawNormal( v0, v1, v2 ), distanceSquared, materialKind };
    hits.push_back( hit );
}

// 00f4529b..00f4537a: the two faces of the sphere's x-min square, swept +x.
inline void sphereSweep( const Point & centre, float radius, Point first[3], Point second[3],
    Point & extent )
{
    const float minX = centre.x - radius;
    const float minY = centre.y - radius, maxY = centre.y + radius;
    const float minZ = centre.z - radius, maxZ = centre.z + radius;
    first[0] = Point{ minX, maxY, minZ };
    first[1] = Point{ minX, minY, maxZ };
    first[2] = Point{ minX, maxY, maxZ };
    second[0] = Point{ minX, maxY, minZ };
    second[1] = Point{ minX, minY, minZ };
    second[2] = Point{ minX, minY, maxZ };
    extent = Point{ (radius + radius) + minX, maxY, minZ };
}

// 00f44b12..00f44b62: maxss of the squared corner lengths, then sqrtss.
inline float boundingRadiusSquared( const Point & minimum, const Point & maximum )
{
    const float low = (minimum.x * minimum.x + minimum.y * minimum.y) + minimum.z * minimum.z;
    const float high = (maximum.x * maximum.x + maximum.y * maximum.y) + maximum.z * maximum.z;
    return low > high ? low : high;
}
}
}

#ifndef WOT_STOCK_BSP_COLLISION_MODEL_CORE_ONLY
#include "cstdmf/bw_namespace.hpp"
#include <Python.h>

BW_BEGIN_NAMESPACE
class BSPTree;
class Model;
class Vector3;

struct StockBspSegmentHit {
    float distance=0.f,angleCosine=0.f;
    std::uint32_t material=0;
    wot::stock_bsp::Point normal{};
};
// Native consumers retain the Model that owns the tree. The query uses the
// same collector and ordering as WGBspCollisionModel.localHitTest, without a
// Python tuple or callback at the per-ray boundary.
std::vector<StockBspSegmentHit> stockBspSegment(const BSPTree &,
    const Vector3 & start,const Vector3 & end);

// Native owners of a WGBspCollisionModel for consumers outside Python, such as
// wg_createHKAttachment (00653540). Both return NULL for any other object.
const BSPTree * stockBspCollisionModelTree( PyObject * object );
Model * stockBspCollisionModelModel( PyObject * object );
BW_END_NAMESPACE
#endif
