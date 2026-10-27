#include "pch.hpp"
#include "stock_camera_collider.hpp"
#include "stock_vehicle_stickers.hpp"

#include "app.hpp"
#include "camera/base_camera.hpp"
#include "duplo/pymodel.hpp"
#include "math/boundbox.hpp"
#include "model/model.hpp"
#include "model/super_model.hpp"
#include "moo/render_context.hpp"
#include "physics2/bsp.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/collision_obstacle.hpp"
#include "physics2/worldtri.hpp"
#include "romp/water.hpp"
#include "scene/scene.hpp"
#include "scene/spatial_query_operation.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>

BW_BEGIN_NAMESPACE

namespace
{
const float SEGMENT_EPSILON = 0.0001f;
const float ZERO_BAND = 0.0004f;
// #1513 0x14b250c (3e22f983): 1 / (2 * pi).
const float INVERSE_TWO_PI = 0.15915494f;
const uint8 IGNORED_TRIANGLES = 7;

inline float clampUnit( float value )
{
    return 0.f > value ? 0.f : (value < 1.f ? value : 1.f);
}

inline float distanceSquared( const Vector3 & a, const Vector3 & b )
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return (y * y + x * x) + z * z;
}

inline Vector3 transformPoint( const Vector3 & point, const Matrix & matrix )
{
    D3DXVECTOR4 result;
    D3DXVec3Transform( &result, &point, &matrix );
    return Vector3( result.x, result.y, result.z );
}

// #1513 007a7200: closest points of two segments (squared distance).
float closestSegmentSegment( const Vector3 & p1, const Vector3 & q1,
    const Vector3 & p2, const Vector3 & q2, Vector3 & c1, Vector3 & c2 )
{
    const float d1x = q1.x - p1.x, d1y = q1.y - p1.y, d1z = q1.z - p1.z;
    const float d2x = q2.x - p2.x, d2y = q2.y - p2.y, d2z = q2.z - p2.z;
    const float rx = p1.x - p2.x, ry = p1.y - p2.y, rz = p1.z - p2.z;
    const float a = (d1y * d1y + d1x * d1x) + d1z * d1z;
    const float e = (d2x * d2x + d2y * d2y) + d2z * d2z;
    const float f = (d2x * rx + ry * d2y) + rz * d2z;
    float s = 0.f;
    float t = 0.f;
    if (a <= SEGMENT_EPSILON)
    {
        if (e <= SEGMENT_EPSILON)
        {
            c1 = p1;
            c2 = p2;
            return distanceSquared( c1, c2 );
        }
        t = clampUnit( f / e );
    }
    else
    {
        const float c = (d1y * ry + d1x * rx) + d1z * rz;
        if (e <= SEGMENT_EPSILON)
        {
            s = clampUnit( -(c / a) );
        }
        else
        {
            const float b = (d1x * d2x + d1y * d2y) + d1z * d2z;
            const float denominator = e * a - b * b;
            s = denominator != 0.f ?
                clampUnit( (b * f - c * e) / denominator ) : 0.f;
            t = (f + b * s) / e;
            if (0.f > t)
            {
                t = 0.f;
                s = clampUnit( -(c / a) );
            }
            else if (t > 1.f)
            {
                t = 1.f;
                s = clampUnit( (b - c) / a );
            }
        }
    }
    c1 = Vector3( d1x * s + p1.x, d1y * s + p1.y, d1z * s + p1.z );
    c2 = Vector3( p2.x + d2x * t, d2y * t + p2.y, d2z * t + p2.z );
    return distanceSquared( c1, c2 );
}

// #1513 007a7870: barycentric projection strictly inside the triangle.
bool projectInsideTriangle( const Vector3 & p, const WorldTriangle & triangle,
    Vector3 & result )
{
    const Vector3 & v0 = triangle.v0();
    const Vector3 & v1 = triangle.v1();
    const Vector3 & v2 = triangle.v2();
    const float ax = v1.x - v0.x, ay = v1.y - v0.y, az = v1.z - v0.z;
    const float bx = v2.x - v0.x, by = v2.y - v0.y, bz = v2.z - v0.z;
    const float nx = ay * bz - az * by;
    const float ny = bx * az - ax * bz;
    const float nz = ax * by - bx * ay;
    const float wx = p.x - v0.x, wy = p.y - v0.y, wz = p.z - v0.z;
    const float c1x = ay * wz - az * wy;
    const float c1y = az * wx - ax * wz;
    const float c1z = ax * wy - ay * wx;
    const float nn = (nx * nx + ny * ny) + nz * nz;
    const float gamma = ((c1x * nx + c1y * ny) + c1z * nz) / nn;
    const float c2x = bz * wy - by * wz;
    const float c2y = bx * wz - bz * wx;
    const float c2z = by * wx - bx * wy;
    const float beta = ((nx * c2x + ny * c2y) + nz * c2z) / nn;
    const float alpha = (1.f - gamma) - beta;
    if (!(alpha > 0.f) || !(1.f > alpha) || !(beta > 0.f) ||
            !(1.f > beta) || !(gamma > 0.f) || !(1.f > gamma))
        return false;
    result = Vector3( (v1.x * beta + v0.x * alpha) + v2.x * gamma,
        (v1.y * beta + v0.y * alpha) + v2.y * gamma,
        (v1.z * beta + v0.z * alpha) + v2.z * gamma );
    return true;
}

// #1513 00f666e0: closest point of a triangle to a point.
Vector3 closestPointOnTriangle( const Vector3 & p, const Vector3 & a,
    const Vector3 & b, const Vector3 & c )
{
    const float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    const float apx = p.x - a.x, apy = p.y - a.y, apz = p.z - a.z;
    const float d1 = (abx * apx + aby * apy) + abz * apz;
    const float d2 = (acx * apx + acy * apy) + acz * apz;
    if (d1 <= 0.f && d2 <= 0.f)
        return a;
    const float bpx = p.x - b.x, bpy = p.y - b.y, bpz = p.z - b.z;
    const float d3 = (aby * bpy + abx * bpx) + abz * bpz;
    const float d4 = (acy * bpy + acx * bpx) + acz * bpz;
    if (d3 >= 0.f && d3 >= d4)
        return b;
    const float vc = d4 * d1 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
    {
        const float denominator = d1 - d3;
        if (denominator == 0.f)
            return a;
        const float v = d1 / denominator;
        return Vector3( a.x + abx * v, a.y + aby * v, a.z + abz * v );
    }
    const float cpx = p.x - c.x, cpy = p.y - c.y, cpz = p.z - c.z;
    const float d5 = (aby * cpy + abx * cpx) + abz * cpz;
    const float d6 = (cpy * acy + acx * cpx) + cpz * acz;
    if (d6 >= 0.f && d6 >= d5)
        return c;
    const float vb = d5 * d2 - d6 * d1;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
    {
        const float denominator = d2 - d6;
        if (denominator == 0.f)
            return a;
        const float w = d2 / denominator;
        return Vector3( a.x + acx * w, a.y + acy * w, a.z + acz * w );
    }
    const float va = d6 * d3 - d5 * d4;
    const float d43 = d4 - d3;
    const float d56 = d5 - d6;
    if (va <= 0.f && d43 >= 0.f && d56 >= 0.f)
    {
        const float denominator = d56 + d43;
        if (denominator == 0.f)
            return b;
        const float w = d43 / denominator;
        return Vector3( b.x + (c.x - b.x) * w, b.y + (c.y - b.y) * w,
            b.z + (c.z - b.z) * w );
    }
    const float denominator = (va + vb) + vc;
    if (denominator == 0.f)
    {
        // #1513 returns a quarter of the edge sum for a degenerate face.
        return Vector3( (abx + acx) * 0.25f + a.x, (aby + acy) * 0.25f + a.y,
            (abz + acz) * 0.25f + a.z );
    }
    const float inverse = 1.f / denominator;
    const float v = vb * inverse;
    const float w = vc * inverse;
    return Vector3( (a.x + abx * v) + acx * w, (a.y + aby * v) + acy * w,
        (a.z + abz * v) + acz * w );
}

// #1513 007a7590: squared distance from a segment to a triangle.
float closestSegmentTriangle( const Vector3 & a, const Vector3 & b,
    const WorldTriangle & triangle, Vector3 & onSegment, Vector3 & onTriangle )
{
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float length = sqrtf( (dx * dx + dy * dy) + dz * dz );
    const float inverse = 1.f / length;
    const Vector3 direction( dx * inverse, dy * inverse, dz * inverse );
    if (triangle.intersects( a, direction, length ))
    {
        const Vector3 hit( direction.x * length + a.x,
            a.y + direction.y * length, direction.z * length + a.z );
        onSegment = hit;
        onTriangle = hit;
        return 0.f;
    }

    float best = FLT_MAX;
    float result = FLT_MAX;
    const Vector3 * vertices[3] = { &triangle.v0(), &triangle.v1(), &triangle.v2() };
    for (unsigned i = 0; i < 3; ++i)
    {
        Vector3 p, q;
        const float distance = closestSegmentSegment( a, b, *vertices[i],
            *vertices[(i + 1) % 3], p, q );
        if (best > distance)
        {
            best = result = distance;
            onSegment = p;
            onTriangle = q;
        }
    }
    Vector3 projected;
    if (projectInsideTriangle( a, triangle, projected ))
    {
        const float distance = distanceSquared( projected, a );
        if (best > distance)
        {
            best = result = distance;
            onSegment = a;
            onTriangle = projected;
        }
    }
    if (projectInsideTriangle( b, triangle, projected ))
    {
        const float distance = distanceSquared( projected, b );
        if (best > distance)
        {
            result = distance;
            onSegment = b;
            onTriangle = projected;
        }
    }
    return result;
}

// #1513 011bd260: segment parameter on a plane within [0, 1].
bool intersectPlaneSegment( const PlaneEq & plane, const Vector3 & start,
    const Vector3 & end, Vector3 & result )
{
    const Vector3 & n = plane.normal();
    const float dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
    const float along = (n.x * dx + n.y * dy) + n.z * dz;
    if (-ZERO_BAND < along && ZERO_BAND > along)
        return false;
    const float t = (plane.d() - ((n.y * start.y + n.x * start.x) + n.z * start.z)) /
        along;
    if (!(t >= 0.f) || !(1.f >= t))
        return false;
    result = Vector3( dx * t + start.x, dy * t + start.y, dz * t + start.z );
    return true;
}

// #1513 00ab9e70 reads the object's world box; empty boxes have no volume.
float obstacleVolume( Scene & scene, const CollisionObstacle & obstacle )
{
    AABB bounds;
    scene.getObjectOperation< SpatialQueryOperation >()->
        getWorldVisibilityBoundingBox( obstacle.sceneObject(), &bounds );
    const Vector3 & low = bounds.minBounds();
    const Vector3 & high = bounds.maxBounds();
    const bool valid = !(low.x > high.x) && !(low.y > high.y) && !(low.z > high.z);
    const float width = valid ? high.x - low.x : 0.f;
    float height = 0.f;
    if (valid)
    {
        height = high.y - low.y;
        height = 0.f > height ? 0.f : (height < 5.f ? height : 5.f);
    }
    const float depth = valid ? high.z - low.z : 0.f;
    return (height * width) * depth;
}

WorldTriangle worldTriangle( const CollisionObstacle & obstacle,
    const WorldTriangle & triangle )
{
    const Matrix & world = obstacle.transform();
    const Vector3 v2 = transformPoint( triangle.v2(), world );
    const Vector3 v1 = transformPoint( triangle.v1(), world );
    const Vector3 v0 = transformPoint( triangle.v0(), world );
    return WorldTriangle( v0, v1, v2, 0 );
}

// #1513 FrontCollisionCallback (007a6a20/007a6b90).
class FrontCollisionCallback : public CollisionCallback
{
public:
    struct Entry
    {
        Vector3 atTriangle = Vector3( 0.f, 0.f, 0.f );
        Vector3 atSegment = Vector3( 0.f, 0.f, 0.f );
        Vector3 nearestSegment = Vector3( 0.f, 0.f, 0.f );
        float segmentDistanceSq = FLT_MAX;
        float frontDistanceSq = FLT_MAX;
        bool hit = false;
    };

    FrontCollisionCallback( Scene & scene, const Vector3 & start,
            const Vector3 & end, const BW::vector< float > & minVolumes,
            float minVolume ) :
        entries_( minVolumes.size() ), start_( start ), end_( end ),
        scene_( scene ), minVolumes_( minVolumes ), minVolume_( minVolume ) {}

    const Entry & entry( size_t index ) const { return entries_[index]; }

private:
    int operator()( const CollisionObstacle & obstacle,
        const WorldTriangle & triangle, float ) override
    {
        // 00f64630 also skips WGMaterialDisabler materials; no such owner exists.
        if (triangle.flags() & IGNORED_TRIANGLES)
            return COLLIDE_ALL;
        const float volume = obstacleVolume( scene_, obstacle );
        if (minVolume_ > volume)
            return COLLIDE_ALL;
        // #1513 writes before the empty group vector here; no group, no record.
        if (entries_.empty())
            return COLLIDE_ALL;
        const WorldTriangle world = worldTriangle( obstacle, triangle );
        size_t index = 0;
        while (index < minVolumes_.size() && !(volume > minVolumes_[index]))
            ++index;
        if (index == minVolumes_.size())
            index = minVolumes_.size() - 1;
        Entry & entry = entries_[index];
        entry.hit = true;
        Vector3 onSegment( 0.f, 0.f, 0.f );
        Vector3 onTriangle( 0.f, 0.f, 0.f );
        const float distance = closestSegmentTriangle( start_, end_, world,
            onSegment, onTriangle );
        const float nearest = distanceSquared( start_, onSegment );
        if (entry.frontDistanceSq > nearest)
        {
            entry.frontDistanceSq = nearest;
            entry.nearestSegment = onSegment;
        }
        if (entry.segmentDistanceSq > distance)
        {
            entry.segmentDistanceSq = distance;
            entry.atSegment = onSegment;
            entry.atTriangle = onTriangle;
        }
        return COLLIDE_ALL;
    }

    BW::vector< Entry > entries_;
    Vector3 start_;
    Vector3 end_;
    Scene & scene_;
    const BW::vector< float > & minVolumes_;
    float minVolume_;
};

// #1513 RearCollisionCallback (007a6ad0/007a6ea0).
class RearCollisionCallback : public CollisionCallback
{
public:
    RearCollisionCallback( Scene & scene, const Vector3 & start,
            const Vector3 & end, float minVolume ) :
        start_( start ), end_( end ), nearest_( FLT_MAX ), scene_( scene ),
        minVolume_( minVolume )
    {
        direction_ = Vector3( end_.x - start_.x, end_.y - start_.y,
            end_.z - start_.z );
        D3DXVec3Normalize( &direction_, &direction_ );
    }

    float nearest() const { return nearest_; }

private:
    int operator()( const CollisionObstacle & obstacle,
        const WorldTriangle & triangle, float ) override
    {
        if (triangle.flags() & IGNORED_TRIANGLES)
            return COLLIDE_ALL;
        const float volume = obstacleVolume( scene_, obstacle );
        if (minVolume_ > volume)
            return COLLIDE_ALL;
        const WorldTriangle world = worldTriangle( obstacle, triangle );
        const Vector3 point = closestPointOnTriangle( start_, world.v0(),
            world.v1(), world.v2() );
        const float along = (direction_.y * (point.y - start_.y) +
            direction_.x * (point.x - start_.x)) + direction_.z * (point.z - start_.z);
        if (nearest_ > along)
            nearest_ = along;
        return COLLIDE_ALL;
    }

    Vector3 start_;
    Vector3 end_;
    Vector3 direction_;
    float nearest_;
    Scene & scene_;
    float minVolume_;
};

// #1513 007a6e80.
class IgnoreDestructiblesCollisionCallback : public CollisionCallback
{
    int operator()( const CollisionObstacle &, const WorldTriangle & triangle,
        float ) override
    {
        const unsigned material = triangle.materialKind();
        return material >= 0x47 && material <= 0x64 ? COLLIDE_ALL : COLLIDE_BEFORE;
    }
};

// #1513 CameraCollisionVisitor 007a41b0: nearest hit-vertex depth.
class CameraCollisionVisitor : public CollisionVisitor
{
public:
    CameraCollisionVisitor( const Vector3 & normal, float offset ) :
        depth_( -1.f ), normal_( normal ), offset_( offset ) {}

    float depth() const { return depth_; }

    bool visit( const WorldTriangle & triangle, float ) override
    {
        float nearest = this->vertexDepth( triangle.v0() );
        const float second = this->vertexDepth( triangle.v1() );
        nearest = nearest < second ? nearest : second;
        const float third = this->vertexDepth( triangle.v2() );
        nearest = nearest < third ? nearest : third;
        if (depth_ == -1.f || depth_ > nearest)
            depth_ = nearest;
        return false;
    }

private:
    float vertexDepth( const Vector3 & v ) const
    {
        return normal_.x * v.x + normal_.y * v.y + normal_.z * v.z - offset_;
    }

    float depth_;
    Vector3 normal_;
    float offset_;
};

// #1513 007a3ed0: move a camera outside the space box back onto it.
bool collideSpaceBounds( Vector3 & position, const Vector3 & pivot,
    const Vector3 & direction, const Vector3 & original )
{
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (!space)
        return false;
    const AABB bounds = space->bounds();
    const Vector3 & low = bounds.minBounds();
    const Vector3 & high = bounds.maxBounds();
    if (low.x > high.x || low.y > high.y || low.z > high.z)
        return false;
    if (!(low.x > original.x) && !(original.x > high.x) &&
            !(low.z > original.z) && !(original.z > high.z))
        return false;

    const PlaneEq planes[4] = {
        PlaneEq( Vector3( 0.f, 0.f, 1.f ), low.z ),
        PlaneEq( Vector3( 0.f, 0.f, -1.f ), -high.z ),
        PlaneEq( Vector3( 1.f, 0.f, 0.f ), low.x ),
        PlaneEq( Vector3( -1.f, 0.f, 0.f ), -high.x ) };
    bool found = false;
    float best = 0.f;
    Vector3 nearest( 0.f, 0.f, 0.f );
    for (const PlaneEq & plane : planes)
    {
        Vector3 hit;
        if (!intersectPlaneSegment( plane, pivot, original, hit ))
            continue;
        const float distance = sqrtf( distanceSquared( hit, pivot ) );
        if (best > distance || best == 0.f)
        {
            best = distance;
            nearest = hit;
        }
        found = true;
    }
    if (!found)
        return false;
    position = Vector3( direction.x * 0.25f + nearest.x,
        direction.y * 0.25f + nearest.y, direction.z * 0.25f + nearest.z );
    return true;
}

// #1513 0079c6d6/007a3565: lift the near plane's bottom edge out of water.
bool liftOutOfWater( Vector3 & position, const Vector3 & direction,
    const Vector3 & up )
{
    const Moo::Camera & camera = Moo::rc().camera();
    Vector3 xAxis( up.y * direction.z - up.z * direction.y,
        up.z * direction.x - up.x * direction.z, up.x * direction.y - up.y * direction.x );
    D3DXVec3Normalize( &xAxis, &xAxis );
    const Vector3 yAxis( xAxis.z * direction.y - xAxis.y * direction.z,
        direction.z * xAxis.x - xAxis.z * direction.x,
        xAxis.y * direction.x - direction.y * xAxis.x );
    const Vector3 planeSize = camera.nearPlanePoint( 1.f, 1.f );
    const float snapHeight = fabsf( planeSize.y * yAxis.y );
    const float nearPlane = camera.nearPlane();
    const Vector3 start(
        (position.x + direction.x * nearPlane) - yAxis.x * snapHeight,
        (direction.y * nearPlane + position.y) - yAxis.y * snapHeight,
        (position.z + direction.z * nearPlane) - yAxis.z * snapHeight );
    const Vector3 end( start.x, start.y + 20.f, start.z );
    for (uint i = 0; i < Waters::instance().size(); i++)
    {
        const float depth = Waters::instance()[i]->stockGroundRayDistance( start, end );
        if (depth > 0.f)
        {
            position.y = (depth + snapHeight) + position.y;
            return true;
        }
    }
    return false;
}
}


float collideStockCameraSegment( const Vector3 & start, const Vector3 & end )
{
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (!space)
        return -1.f;
    IgnoreDestructiblesCollisionCallback callback;
    return space->collide( start, end, callback );
}


void StockAdvancedCameraCollider::init( float fovRatio, float rollbackSpeed,
    float minimalCameraDistance, float speedThreshold, float minVolume )
{
    fovRatio_ = fovRatio;
    rollbackSpeed_ = rollbackSpeed;
    minimalCameraDistance_ = minimalCameraDistance;
    speedThreshold_ = speedThreshold;
    minVolume_ = minVolume;
}


void StockAdvancedCameraCollider::addVolumeGroup(
    const StockColliderVolumeGroup & group )
{
    // 007a51b0 keeps both vectors sorted by descending minimum volume.
    groups_.push_back( group );
    std::sort( groups_.begin(), groups_.end(),
        []( const StockColliderVolumeGroup & a, const StockColliderVolumeGroup & b )
        { return a.minVolume > b.minVolume; } );
    minVolumes_.push_back( group.minVolume );
    std::sort( minVolumes_.begin(), minVolumes_.end(),
        []( float a, float b ) { return a > b; } );
    ++groupSentinel_;
    currentGroup_ = groupSentinel_;
}


void StockAdvancedCameraCollider::scrollDelta( float delta )
{
    // 007a5270 changes only an offset that is already displaced.
    const float current = offset_;
    if (-ZERO_BAND < current && ZERO_BAND > current)
        return;
    if (-ZERO_BAND < delta && ZERO_BAND > delta)
        return;
    const float sum = current + delta;
    if (0.f > sum)
    {
        offset_ = 0.f;
        return;
    }
    const float limit = remaining_ + current;
    offset_ = limit < sum ? limit : sum;
}


bool StockAdvancedCameraCollider::collide( float dTime, Vector3 & position,
    const Vector3 & pivot, const Vector3 & up )
{
    // #1513 007a52d0.
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (!space)
        return false;
    Scene & scene = space->scene();

    const float vx = pivot.x - position.x;
    const float vy = pivot.y - position.y;
    const float vz = pivot.z - position.z;
    Vector3 right( up.y * vz - up.z * vy, up.z * vx - up.x * vz,
        up.x * vy - up.y * vx );
    Vector3 upward( vy * right.z - vz * right.y, vz * right.x - vx * right.z,
        vx * right.y - vy * right.x );
    const Moo::Camera & camera = Moo::rc().camera();
    const float halfTan = tanf( camera.fov() * 0.5f );
    float radius = (fovRatio_ * camera.nearPlane()) * halfTan;
    radius = radius + radius;
    const float length = sqrtf( (vx * vx + vy * vy) + vz * vz );
    const float inverse = 1.f / length;
    const Vector3 n( inverse * vx, inverse * vy, inverse * vz );
    D3DXVec3Normalize( &right, &right );
    D3DXVec3Normalize( &upward, &upward );

    const float rx = right.x * radius, ry = right.y * radius, rz = right.z * radius;
    const float ux = upward.x * radius, uy = upward.y * radius, uz = upward.z * radius;
    const float lead = offset_ - radius;
    const Vector3 centre( position.x + n.x * lead, position.y + n.y * lead,
        n.z * lead + position.z );
    const Vector3 a( centre.x - rx, centre.y - ry, centre.z - rz );
    const Vector3 b( centre.x + ux, centre.y + uy, centre.z + uz );
    const Vector3 c( centre.x + rx, centre.y + ry, centre.z + rz );
    const Vector3 d( centre.x - ux, centre.y - uy, centre.z - uz );
    const WorldTriangle first( a, b, d, 0 );
    const WorldTriangle second( c, d, b, 0 );

    const float sweep = (length - offset_) + radius;
    const float sx = sweep * n.x, sy = sweep * n.y, sz = sweep * n.z;
    const float nrx = n.x * radius, nry = n.y * radius, nrz = n.z * radius;
    const Vector3 front( nrx + centre.x, nry + centre.y, nrz + centre.z );

    FrontCollisionCallback hits( scene, front, pivot, minVolumes_, minVolume_ );
    space->collide( first, Vector3( sx + a.x, sy + a.y, a.z + sz ), hits );
    space->collide( second, Vector3( sx + c.x, sy + c.y, c.z + sz ), hits );

    const float moved = sqrtf( distanceSquared( position, lastPosition_ ) );
    const float speed = moved * (1.f / dTime);
    lastPosition_ = position;
    slowTime_ = speedThreshold_ > speed ? slowTime_ + dTime : 0.f;

    float best = offset_;
    bool hitAny = false;
    for (unsigned i = 0; i < minVolumes_.size(); ++i)
    {
        const FrontCollisionCallback::Entry & entry = hits.entry( i );
        const StockColliderVolumeGroup & group = groups_[i];
        if (!entry.hit)
        {
            if (currentGroup_ == i)
                currentGroup_ = groupSentinel_;
            visibilityTime_ = 0.f;
            continue;
        }
        const float distance = sqrtf( entry.segmentDistanceSq );
        hitAny = true;
        if (!(radius > distance))
        {
            currentGroup_ = groupSentinel_;
            visibilityTime_ = 0.f;
            continue;
        }
        if (currentGroup_ != i)
        {
            const float qx = entry.atTriangle.x - front.x;
            const float qy = entry.atTriangle.y - front.y;
            const float qz = entry.atTriangle.z - front.z;
            const float projection = (qx * n.x + qy * n.y) + qz * n.z;
            if (0.f > projection)
            {
                // Advance to where the swept sphere leaves the obstacle.
                const float bTerm = projection * -2.f;
                const float cTerm = ((qx * qx + qy * qy) + qz * qz) - radius * radius;
                const float discriminant = bTerm * bTerm - cTerm * 4.f;
                float advance = 0.f;
                if (!(0.f > discriminant))
                {
                    const float root = sqrtf( discriminant ) * 0.5f;
                    const float middle = bTerm * -0.5f;
                    if (-ZERO_BAND < root && ZERO_BAND > root)
                    {
                        advance = middle;
                    }
                    else
                    {
                        advance = middle - root;
                        if (0.f > advance)
                            advance = root + middle;
                    }
                }
                const float candidate = offset_ + advance;
                best = candidate > best ? candidate : best;
                continue;
            }
            if (group.lowSpeedLimit > slowTime_ &&
                    group.vehicleVisibilityLimit > visibilityTime_ &&
                    entry.frontDistanceSq > group.criticalDistanceSq)
                continue;
            visibilityTime_ = 0.f;
            currentGroup_ = group.canSkip ? i : groupSentinel_;
        }

        // Approach the obstacle with a speed-dependent critical damping.
        const float towardsSegment = sqrtf( distanceSquared( entry.atSegment, front ) );
        const float towardsPivot = sqrtf( distanceSquared( front, pivot ) );
        const float ratio = 1.f - towardsSegment / towardsPivot;
        const float factor = 1.f > ratio ? ratio : 1.f;
        const float cameraSpeed = group.cameraSpeedFactor * speed;
        const float low = group.approachSpeed.x + cameraSpeed;
        const float high = group.approachSpeed.y + cameraSpeed;
        float rate = powf( 2.5f, (factor * radius) / (distance + 0.001f) );
        rate = low > rate ? low : (high < rate ? high : rate);
        smoothedOffset_ = best;
        smoothedSpeed_ = rate;
        const float blend = dTime / (INVERSE_TWO_PI / smoothedSpeed_ + dTime);
        const float target = ((offset_ + towardsSegment) + radius) - smoothedOffset_;
        const float smoothed = blend * target + smoothedOffset_;
        smoothedOffset_ = smoothed;
        best = smoothed > best ? smoothed : best;
    }
    offset_ = best;

    if (!hitAny && best > 0.f)
    {
        // Roll back towards the ideal distance while nothing obstructs it.
        const Vector3 rear( position.x - nrx, position.y - nry, position.z - nrz );
        RearCollisionCallback behind( scene, front, rear, minVolume_ );
        const float back = offset_ + radius;
        const float bx = -(n.x * back), by = -(n.y * back), bz = -(n.z * back);
        space->collide( first, Vector3( a.x + bx, a.y + by, a.z + bz ), behind );
        space->collide( second, Vector3( c.x + bx, c.y + by, c.z + bz ), behind );
        const float clearance = behind.nearest() - radius;
        const float current = offset_;
        const float retreat = 0.f > clearance ? 0.f :
            (current < clearance ? current : clearance);
        smoothedOffset_ = current;
        smoothedSpeed_ = rollbackSpeed_;
        const float blend = dTime / (INVERSE_TWO_PI / smoothedSpeed_ + dTime);
        const float smoothed = blend * ((current - retreat) - smoothedOffset_) +
            smoothedOffset_;
        smoothedOffset_ = smoothed;
        offset_ = smoothed;
    }

    const float room = length - minimalCameraDistance_;
    const float limit = room > 0.f ? room : 0.f;
    const float offset = 0.f > offset_ ? 0.f : (offset_ < limit ? offset_ : limit);
    remaining_ = room - offset;
    offset_ = offset;
    position = Vector3( position.x + n.x * offset, position.y + n.y * offset,
        n.z * offset + position.z );
    if (-ZERO_BAND < remaining_ && ZERO_BAND > remaining_)
    {
        // At the minimum distance keep the near plane below any ceiling.
        IgnoreDestructiblesCollisionCallback callback;
        const Vector3 top( pivot.x, pivot.y + radius, pivot.z );
        const float clearance = space->collide( pivot, top, callback );
        if (clearance >= 0.f)
            position.y = position.y - (radius - clearance);
    }
    return true;
}


void StockCameraCollider::dynamicColliders(
    BW::vector< StockCameraDynamicCollider > & colliders )
{
    // Release the replaced providers only after the new list is installed.
    dynamic_.swap( colliders );
}


bool StockCameraCollider::collideDynamic( Vector3 & position,
    const Vector3 & direction, const Vector3 & up ) const
{
    // #1513 007a2c30, the CursorCamera hull sweep over compound parts.
    if (dynamic_.empty())
        return false;
    bool collided = false;
    const Moo::Camera & camera = Moo::rc().camera();
    const Vector3 nearPlane = camera.nearPlanePoint( 1.f, 1.f );
    const float halfWidth = nearPlane.x * 1.5f;
    const float halfHeight = nearPlane.y * 1.5f;
    // Providers and links can re-enter Python and replace the list.
    const BW::vector< StockCameraDynamicCollider > colliders = dynamic_;
    for (const StockCameraDynamicCollider & collider : colliders)
    {
        // #1513 dereferences missing providers and links; skip them instead.
        if (!collider.node || !collider.bounds || !collider.geometry)
            continue;
        Matrix world;
        world.setZero();
        collider.node->matrix( world );
        if (distanceSquared( world.applyToOrigin(), position ) > 400.f)
            continue;
        Matrix boundsMatrix;
        boundsMatrix.setZero();
        collider.bounds->matrix( boundsMatrix );
        BoundingBox bounds( Vector3( 0.f, 0.f, 0.f ), Vector3( 1.f, 1.f, 1.f ) );
        bounds.transformBy( boundsMatrix );
        const Vector3 & low = bounds.minBounds();
        const Vector3 & high = bounds.maxBounds();
        if (!(position.x >= low.x - 0.5f) || !(position.y >= low.y - 0.5f) ||
                !(position.z >= low.z - 0.5f) || !(high.x + 0.5f > position.x) ||
                !(high.y + 0.5f > position.y) || !(high.z + 0.5f > position.z))
            continue;

        Matrix inverse( world );
        D3DXMatrixInverse( &inverse, NULL, &world );
        const Vector3 local = transformPoint( position, inverse );
        Vector3 dir;
        D3DXVec3TransformNormal( &dir, &direction, &inverse );
        Vector3 upward;
        D3DXVec3TransformNormal( &upward, &up, &inverse );
        const Vector3 reach( dir.x * 20.f, dir.y * 20.f, dir.z * 20.f );
        const Vector3 start( local.x - reach.x, local.y - reach.y, local.z - reach.z );
        Vector3 xAxis( upward.y * dir.z - upward.z * dir.y,
            upward.z * dir.x - upward.x * dir.z, upward.x * dir.y - upward.y * dir.x );
        D3DXVec3Normalize( &xAxis, &xAxis );
        const Vector3 yAxis( dir.y * xAxis.z - dir.z * xAxis.y,
            dir.z * xAxis.x - dir.x * xAxis.z, dir.x * xAxis.y - dir.y * xAxis.x );
        const Vector3 cornerA(
            (start.x - xAxis.x * halfWidth) + halfHeight * yAxis.x,
            (start.y - halfWidth * xAxis.y) + halfHeight * yAxis.y,
            (start.z - halfWidth * xAxis.z) + halfHeight * yAxis.z );
        const float width = halfWidth + halfWidth;
        const float height = halfHeight + halfHeight;
        const Vector3 cornerB( xAxis.x * width + cornerA.x,
            xAxis.y * width + cornerA.y, xAxis.z * width + cornerA.z );
        const Vector3 cornerC( cornerA.x - height * yAxis.x,
            cornerA.y - height * yAxis.y, cornerA.z - height * yAxis.z );
        const Vector3 cornerD( xAxis.x * width + cornerC.x,
            xAxis.y * width + cornerC.y, xAxis.z * width + cornerC.z );
        const Vector3 sweep( reach.x * 1.25f, reach.y * 1.25f, reach.z * 1.25f );

        PyModelPtr model;
        Matrix linked;
        if (!collider.geometry->acquire( model, linked ))
            continue;
        SuperModel * superModel = model->pSuperModel();
        if (!superModel || superModel->nModels() == 0)
            continue;
        superModel->beginRead();
        struct Guard { SuperModel * model; ~Guard() { model->endRead(); } } guard = { superModel };
        ModelPtr top = superModel->topModel( 0 );
        const BSPTree * tree = top ? top->decompose() : NULL;
        if (!tree)
            continue;
        CameraCollisionVisitor hit( dir,
            (start.x * dir.x + dir.y * start.y) + dir.z * start.z );
        tree->intersects( WorldTriangle( cornerA, cornerB, cornerD ), sweep, &hit );
        tree->intersects( WorldTriangle( cornerA, cornerD, cornerC ), sweep, &hit );
        if (hit.depth() == -1.f)
            continue;
        const float advance = (hit.depth() - camera.nearPlane()) - 0.2f;
        const Vector3 moved( advance * dir.x + start.x, dir.y * advance + start.y,
            dir.z * advance + start.z );
        const float along = (dir.y * (moved.y - local.y) + dir.x * (moved.x - local.x)) +
            dir.z * (moved.z - local.z);
        if (along >= 0.f)
            continue;
        position = transformPoint( moved, world );
        collided = true;
    }
    return collided;
}


bool StockCameraCollider::collide( Vector3 & position, const Vector3 & pivot,
    const Vector3 & direction, const Vector3 & up, bool useAdvanced )
{
    // #1513 007a34c0.
    bool collided = this->collideDynamic( position, direction, up );
    if (useAdvanced && advanced_.enabled())
    {
        collided |= advanced_.collide( App::instance().getFrameGameDelta(),
            position, pivot, up );
    }
    else
    {
        // 007a3770 is #1513's space check; like CursorCamera this keeps the
        // public BaseCamera::sceneCheck. Exact configs never reach it.
        collided |= BaseCamera::sceneCheck( position, pivot, direction, up );
    }
    const Vector3 original = position;
    collided |= collideSpaceBounds( position, pivot, direction, original );
    if (liftOutOfWater( position, direction, up ))
        return true;
    return collided;
}


int setStockCameraDynamicColliders( PyObject * object,
    BW::vector< StockCameraDynamicCollider > & colliders, const char * varName )
{
    // 0079f950 parses the whole sequence before the owner replaces its list.
    if (!PySequence_Check( object ))
    {
        PyErr_Format( PyExc_TypeError, "%s must be set to a sequence of %s",
            varName, "struct BW::CameraCollider::DynamicCollisionInfo" );
        return -1;
    }
    const Py_ssize_t size = PySequence_Size( object );
    if (size < 0)
        return -1;
    BW::vector< StockCameraDynamicCollider > parsed( size );
    for (Py_ssize_t i = 0; i < size; ++i)
    {
        PyObjectPtr item( PySequence_GetItem( object, i ), PyObjectPtr::STEAL_REFERENCE );
        if (!item || !PySequence_Check( item.get() ) || PySequence_Size( item.get() ) != 3)
        {
            PyErr_SetString( PyExc_TypeError, "Tuple of 3 arguments required" );
            return -1;
        }
        StockCameraDynamicCollider & collider = parsed[i];
        PyObjectPtr node( PySequence_GetItem( item.get(), 0 ), PyObjectPtr::STEAL_REFERENCE );
        if (!node || Script::setData( node.get(), collider.node, "" ) != 0)
            return -1;
        PyObjectPtr bounds( PySequence_GetItem( item.get(), 1 ), PyObjectPtr::STEAL_REFERENCE );
        if (!bounds || Script::setData( bounds.get(), collider.bounds, "" ) != 0)
            return -1;
        PyObjectPtr link( PySequence_GetItem( item.get(), 2 ), PyObjectPtr::STEAL_REFERENCE );
        if (!link)
            return -1;
        if (link.get() != Py_None)
        {
            collider.geometry = stockVehicleStickerGeometry( link.get() );
            if (!collider.geometry)
            {
                PyErr_Format( PyExc_TypeError, "%s must be set to a PyDataLink", "" );
                return -1;
            }
        }
    }
    colliders.swap( parsed );
    return 0;
}


int setStockColliderVolumeGroup( PyObject * object,
    StockColliderVolumeGroup & group, const char * )
{
    // 007a6340 reads a 7-item tuple; #1513 returns -1 without an error otherwise.
    if (!object || !PyTuple_Check( object ) || PyTuple_GET_SIZE( object ) != 7)
    {
        PyErr_SetString( PyExc_TypeError,
            "ColliderVolumeGroup must be set to a tuple of 7 elements" );
        return -1;
    }
    static const char * const names[7] = { "volumeGroup.m_minVolume",
        "volumeGroup.m_lowSpeedLimit", "volumeGroup.m_vehicleVisibilityLimit",
        "volumeGroup.m_approachSpeed", "volumeGroup.m_cameraSpeedFactor",
        "volumeGroup.m_criticalDistanceSq", "volumeGroup.m_canSkip" };
    StockColliderVolumeGroup parsed = group;
    int index = 0;
    int failed = Script::setData( PyTuple_GET_ITEM( object, 0 ), parsed.minVolume, names[0] );
    if (!failed && ++index)
        failed = Script::setData( PyTuple_GET_ITEM( object, 1 ), parsed.lowSpeedLimit, names[1] );
    if (!failed && ++index)
        failed = Script::setData( PyTuple_GET_ITEM( object, 2 ),
            parsed.vehicleVisibilityLimit, names[2] );
    if (!failed && ++index)
        failed = Script::setData( PyTuple_GET_ITEM( object, 3 ), parsed.approachSpeed, names[3] );
    if (!failed && ++index)
        failed = Script::setData( PyTuple_GET_ITEM( object, 4 ),
            parsed.cameraSpeedFactor, names[4] );
    if (!failed && ++index)
        failed = Script::setData( PyTuple_GET_ITEM( object, 5 ),
            parsed.criticalDistanceSq, names[5] );
    if (!failed && ++index)
        failed = Script::setData( PyTuple_GET_ITEM( object, 6 ), parsed.canSkip, names[6] );
    if (failed)
    {
        PyErr_Format( PyExc_TypeError,
            "error during converting %d elem into %s field of CollisionPredictor",
            index, names[index] );
        return -1;
    }
    parsed.criticalDistanceSq = parsed.criticalDistanceSq * parsed.criticalDistanceSq;
    group = parsed;
    return 0;
}

BW_END_NAMESPACE
