// C entry points over the production core, using the #1513 record layout
// (0xc4 bytes, points 0x20 bytes) so the oracle can compare raw bytes.
#include "wot/stock_ballistics.hpp"

#include <cstring>
#include <vector>

using namespace wot::stock_ballistics;

namespace
{
struct Raw
{
    std::int32_t shotID;
    std::uint32_t handle;
    std::uint32_t points[3];
    float plane[4];
    float matrix[16];
    float refStart[3], refVelocity[3], velocity0[3], start[3], impact[3], velocity[3];
    float gravity, startTime, maxDistance, holdTime, deathTime;
    std::uint8_t visible, autoScale, pad[2];
};
static_assert( sizeof( Raw ) == 0xc4, "#1513 record size" );

struct RawPoint
{
    float position[3];
    float time;
    std::uint64_t item;
    std::uint32_t material;
    std::uint8_t final, pad[3];
};
static_assert( sizeof( RawPoint ) == 0x20, "#1513 point size" );

Vec3 v3( const float * f ) { return { f[0], f[1], f[2] }; }
void put( float * f, const Vec3 & v ) { f[0] = v.x; f[1] = v.y; f[2] = v.z; }

Projectile load( const Raw & raw, const RawPoint * points, int count )
{
    Projectile p;
    p.shotID = raw.shotID;
    p.handle = raw.handle;
    for (int i = 0; i < count; ++i)
        p.points.push_back( Point{ v3( points[i].position ), points[i].time, points[i].item,
            points[i].material, points[i].final != 0 } );
    p.plane = Plane{ raw.plane[0], raw.plane[1], raw.plane[2], raw.plane[3] };
    std::memcpy( &p.matrix, raw.matrix, sizeof( p.matrix ) );
    p.refStart = v3( raw.refStart ); p.refVelocity = v3( raw.refVelocity );
    p.velocity0 = v3( raw.velocity0 ); p.start = v3( raw.start );
    p.impactDirection = v3( raw.impact ); p.velocity = v3( raw.velocity );
    p.gravity = raw.gravity; p.startTime = raw.startTime; p.maxDistance = raw.maxDistance;
    p.holdTime = raw.holdTime; p.deathTime = raw.deathTime;
    p.visible = raw.visible != 0; p.autoScale = raw.autoScale != 0;
    return p;
}

void store( Raw & raw, const Projectile & p )
{
    raw.shotID = p.shotID;
    raw.plane[0] = p.plane.x; raw.plane[1] = p.plane.y; raw.plane[2] = p.plane.z;
    raw.plane[3] = p.plane.d;
    std::memcpy( raw.matrix, &p.matrix, sizeof( raw.matrix ) );
    put( raw.impact, p.impactDirection ); put( raw.velocity, p.velocity );
    raw.holdTime = p.holdTime; raw.deathTime = p.deathTime;
    raw.visible = p.visible ? 1 : 0;
}

Parameters params( const float * values, int autoScaleOff )
{
    Parameters result;
    result.hidingTime = values[0]; result.timeAfterDeath = values[1];
    result.autoScaleDistance = values[2]; result.serverTickLength = values[3];
    result.autoScaleOff = autoScaleOff != 0;
    return result;
}
} // namespace

extern "C" {

int sb_subdivide( const float * position, const float * velocity, const float * gravity,
    float dt, float tolerance, float * end, float * chords, int capacity )
{
    std::vector<Vec3> out;
    Vec3 p, v;
    Simulator::subdivide( p, v, out, v3( position ), v3( velocity ), v3( gravity ), dt, tolerance );
    put( end, p ); put( end + 3, v );
    const int count = int( out.size() );
    for (int i = 0; i < count && i < capacity; ++i) put( chords + 3 * i, out[i] );
    return count;
}

int sb_intersect( const float * plane, const float * a, const float * b, float * out )
{
    Vec3 result;
    const bool hit = Simulator::intersect( Plane{ plane[0], plane[1], plane[2], plane[3] },
        result, v3( a ), v3( b ) );
    if (hit) put( out, result );
    return hit ? 1 : 0;
}

void sb_set_matrix( void * record, const float * position, const float * values,
    int autoScaleOff, const float * camera )
{
    Raw & raw = *static_cast<Raw *>( record );
    Projectile p = load( raw, NULL, 0 );
    Simulator::setMatrix( p, v3( position ), params( values, autoScaleOff ), v3( camera ) );
    store( raw, p );
}

int sb_update( void * record, const void * points, int count, float now, const float * values,
    int autoScaleOff, const float * camera )
{
    Raw & raw = *static_cast<Raw *>( record );
    Projectile p = load( raw, static_cast<const RawPoint *>( points ), count );
    const bool terminal = Simulator::update( p, now, params( values, autoScaleOff ), NULL,
        v3( camera ) );
    store( raw, p );
    return terminal ? 1 : 0;
}

void sb_terminal_plane( const void * record, const float * end, float * plane )
{
    const Projectile p = load( *static_cast<const Raw *>( record ), NULL, 0 );
    const Plane result = Simulator::terminalPlane( p, v3( end ) );
    plane[0] = result.x; plane[1] = result.y; plane[2] = result.z; plane[3] = result.d;
}

}
