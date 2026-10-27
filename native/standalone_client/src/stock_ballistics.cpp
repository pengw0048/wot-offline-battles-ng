#include "wot/stock_ballistics.hpp"

#include <cmath>
#include <cstring>
#include <set>

namespace wot {
namespace stock_ballistics {

namespace
{
const float EPSILON = 0.0004f;        // 014b24f8
const float NO_TIME = 1000000.f;      // 014dd4e0
const float IMPACT_BACKOFF = 0.01f;   // 014b2500
const float PREDICTION_TIME = 1000.f;
const float CHORD_TOLERANCE = 0.03f;
const std::uint16_t STATIC_FLAGS = 0x80;
const int STATIC_ITERATIONS = 100;
Scene * s_scene = NULL;

Vec3 sub( const Vec3 & a, const Vec3 & b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

// Two's-complement negation, as the original `neg` instruction.
std::int32_t negated( std::int32_t value )
{
    return std::int32_t( 0u - std::uint32_t( value ) );
}

// D3DXVec3Normalize; a zero vector stays zero.
Vec3 normalise( const Vec3 & v )
{
    const float length = std::sqrt( (v.x * v.x + v.y * v.y) + v.z * v.z );
    if (length == 0.f) return { 0.f, 0.f, 0.f };
    return { v.x / length, v.y / length, v.z / length };
}

// sqrt((dx^2 + dy^2) + dz^2), the common #1513 SSE order.
float distance( const Vec3 & a, const Vec3 & b )
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt( (dx * dx + dy * dy) + dz * dz );
}

bool activePlane( const Plane & plane )
{
    const float n = ((plane.x * plane.x + plane.y * plane.y) + plane.z * plane.z) - 1.f;
    return EPSILON > std::fabs( n );
}

// 8abb02: the plane is reached when the point lies on or beyond it.
bool beyondPlane( const Plane & plane, const Vec3 & p )
{
    const float side = (p.y * plane.y + p.x * plane.x) + p.z * plane.z;
    return EPSILON > std::fabs( side - plane.d ) || side > plane.d;
}

// Flight time along the reference trajectory (8ab36f, 8aa126).
float referenceTime( const Vec3 & p, const Vec3 & refStart, const Vec3 & refVelocity )
{
    if (std::fabs( refVelocity.x ) >= EPSILON) return (p.x - refStart.x) / refVelocity.x;
    if (std::fabs( refVelocity.z ) >= EPSILON) return (p.z - refStart.z) / refVelocity.z;
    return NO_TIME;
}

// 8ab700 performs the same division on x87 with double precision.
float referenceTimeX87( const Vec3 & p, const Vec3 & refStart, const Vec3 & refVelocity )
{
    if (std::fabs( refVelocity.x ) >= EPSILON)
        return float( (double( p.x ) - double( refStart.x )) / double( refVelocity.x ) );
    if (std::fabs( refVelocity.z ) >= EPSILON)
        return float( (double( p.z ) - double( refStart.z )) / double( refVelocity.z ) );
    return NO_TIME;
}

struct Chord { Vec3 velocity; Vec3 start; Vec3 end; };

// Trajectory context of 8a9b80, read by its candidate writer 8ab0a0.
struct Prediction
{
    std::vector<Point> * points;
    std::int32_t spaceID;
    Vec3 refStart;
    Vec3 refVelocity;
    Vec3 tracerCamera;
    Vec3 gunPosition;
    bool cameraTest;
    Vec3 segmentStart;
    Vec3 segmentEnd;
};

void pushFinal( std::vector<Point> & points, const Vec3 & p, float time )
{
    points.push_back( Point{ p, time, 0u, 0u, true } );
}

// 8ab0a0: water ahead of a static hit, seen from the tracer camera for
// attributed shots, replaces it as the final point.
bool pushCandidate( Prediction & context, Scene & scene, Vec3 point, float limit,
    const Point & meta )
{
    bool fromCamera = false;
    float water;
    bool waterTested = false;
    if (context.cameraTest)
    {
        const Vec3 direction = normalise( sub( point, context.tracerCamera ) );
        const float gy = point.y - context.gunPosition.y;
        const float gx = point.x - context.gunPosition.x;
        const float gz = point.z - context.gunPosition.z;
        if (context.tracerCamera.y > context.gunPosition.y &&
            (gy * gy + gx * gx) + gz * gz > 900.f)
        {
            fromCamera = true;
            const Vec3 end = { point.x + direction.x * 1.5f,
                point.y + direction.y * 1.5f, point.z + direction.z * 1.5f };
            SceneHit sight;
            if (!scene.collideSegment( context.spaceID, context.tracerCamera, end,
                    STATIC_FLAGS, SceneFilter(), sight ))
                goto ordinary;
            const float sy = sight.point.y - context.tracerCamera.y;
            const float sx = sight.point.x - context.tracerCamera.x;
            const float sz = sight.point.z - context.tracerCamera.z;
            limit = std::sqrt( (sy * sy + sx * sx) + sz * sz );
            water = scene.collideWater( context.tracerCamera, end );
            waterTested = true;
        }
    }
    if (!waterTested)
        water = scene.collideWater( context.segmentStart, context.segmentEnd );
    if (0.f > water || water > limit) goto ordinary;
    if (water < 0.f || water != water) return false;
    {
        const Vec3 & base = fromCamera ? context.tracerCamera : context.segmentStart;
        const Vec3 direction = normalise( sub( point, base ) );
        const Vec3 surface = { direction.x * water + base.x,
            base.y + direction.y * water, base.z + direction.z * water };
        pushFinal( *context.points, surface,
            referenceTimeX87( surface, context.refStart, context.refVelocity ) );
        return true;
    }
ordinary:
    Point candidate = meta;
    candidate.position = point;
    candidate.time = referenceTime( point, context.refStart, context.refVelocity );
    context.points->push_back( candidate );
    if (candidate.final) return true;
    context.segmentStart = point;
    return false;
}
} // namespace

Parameters & parameters()
{
    static Parameters s_parameters;
    return s_parameters;
}

void setScene( Scene * scene ) { s_scene = scene; }
Scene * scene() { return s_scene; }

Simulator::Simulator( Callbacks callbacks, std::function<float()> clock,
        std::function<Vec3()> cameraPosition ) :
    callbacks_( std::move( callbacks ) ), clock_( std::move( clock ) ),
    cameraPosition_( std::move( cameraPosition ) ),
    freeHead_( INVALID_HANDLE ), freeTail_( INVALID_HANDLE ), growth_( 50u ),
    indices_( 20, -1 )
{
    projectiles_.reserve( 20 );
    growSlots( 20 );
}

void Simulator::growSlots( std::uint32_t count )
{
    std::uint32_t index = std::uint32_t( slots_.size() );
    const std::uint32_t end = index + count;
    slots_.resize( end, Slot{ 0u, INVALID_HANDLE } );
    for (; index < end; ++index)
    {
        if (freeTail_ != INVALID_HANDLE) slots_[freeTail_].next = index;
        freeTail_ = index;
        slots_[index].next = INVALID_HANDLE;
        if (freeHead_ == INVALID_HANDLE) freeHead_ = freeTail_;
    }
}

std::uint32_t Simulator::allocateHandle()
{
    if (freeHead_ == INVALID_HANDLE) growSlots( growth_ );
    const std::uint32_t index = freeHead_;
    freeHead_ = slots_[index].next;
    if (freeHead_ == INVALID_HANDLE) freeTail_ = INVALID_HANDLE;
    return (slots_[index].generation << 24) | (index & INVALID_HANDLE);
}

// 8abea0: swap-and-pop, then return the slot to the free list.
std::size_t Simulator::remove( std::uint32_t handle )
{
    const std::uint32_t slot = handle & INVALID_HANDLE;
    if (slot >= indices_.size()) indices_.resize( slot + 1, -1 );
    const std::size_t index = std::size_t( indices_[slot] );
    indices_[slot] = -1;
    if (index + 1 != projectiles_.size())
        projectiles_[index] = std::move( projectiles_.back() );
    projectiles_.pop_back();
    if (index < projectiles_.size())
    {
        const std::uint32_t moved = projectiles_[index].handle & INVALID_HANDLE;
        if (moved >= indices_.size()) indices_.resize( moved + 1, -1 );
        indices_[moved] = std::int32_t( index );
    }
    slots_[slot].generation = (slots_[slot].generation + 1u) & 0xffu;
    if (freeTail_ != INVALID_HANDLE) slots_[freeTail_].next = slot;
    freeTail_ = slot;
    slots_[slot].next = INVALID_HANDLE;
    if (freeHead_ == INVALID_HANDLE) freeHead_ = freeTail_;
    return index;
}

Projectile * Simulator::find( std::int32_t shotID )
{
    for (Projectile & projectile : projectiles_)
        if (projectile.shotID == shotID) return &projectile;
    return NULL;
}

// 8ab770, with the handle generation also checked: a reused slot must not
// drive a stale motor's model.
const Projectile * Simulator::lookup( std::uint32_t handle ) const
{
    const std::uint32_t slot = handle & INVALID_HANDLE;
    if (slot >= indices_.size() || indices_[slot] == -1) return NULL;
    const Projectile & projectile = projectiles_[std::size_t( indices_[slot] )];
    return projectile.handle == handle ? &projectile : NULL;
}

// 8a9290: one server tick of flight split into chords within tolerance.
void Simulator::subdivide( Vec3 & endPosition, Vec3 & endVelocity, std::vector<Vec3> & chords,
    const Vec3 & position, const Vec3 & velocity, const Vec3 & g, float dt, float tolerance )
{
    const Vec3 v1 = { velocity.x + dt * g.x, velocity.y + g.y * dt, velocity.z + g.z * dt };
    const Vec3 sum = { velocity.x + v1.x, velocity.y + v1.y, velocity.z + v1.z };
    const Vec3 p1 = { position.x + (dt * sum.x) * 0.5f, position.y + (dt * sum.y) * 0.5f,
        position.z + (dt * sum.z) * 0.5f };
    std::vector<Chord> stack;
    stack.push_back( Chord{ velocity, position, p1 } );
    while (!stack.empty())
    {
        const Chord chord = stack.back();
        stack.pop_back();
        const Vec3 & V = chord.velocity;
        const Vec3 & S = chord.start;
        const Vec3 & E = chord.end;
        const float dy = E.y - S.y, dx = E.x - S.x, dz = E.z - S.z;
        Vec3 n = { -(dx * dy), dx * dx - (-dz) * dz, (-dz) * dy };
        bool emit = tolerance > std::fabs( n.y );
        if (!emit)
        {
            n = normalise( n );
            const float dot = (V.y * n.y + V.x * n.x) + n.z * V.z;
            const float t = -(dot / (g.y * n.y));
            const float h = (t * t) * 0.5f;
            const Vec3 o = { g.x * h + V.x * t, g.y * h + V.y * t, g.z * h + V.z * t };
            const float deviation = std::fabs( (o.y * n.y + o.x * n.x) + n.z * o.z );
            emit = !(deviation > tolerance);
            if (!emit)
            {
                const Vec3 M = { o.x + S.x, S.y + o.y, S.z + o.z };
                const Vec3 Vm = { g.x * t + V.x, g.y * t + V.y, g.z * t + V.z };
                stack.push_back( Chord{ Vm, M, E } );
                stack.push_back( Chord{ V, S, M } );
            }
        }
        if (emit) chords.push_back( E );
    }
    endPosition = p1;
    endVelocity = v1;
}

// 8a9b80: reference flight until maximum distance, arena boundary or a
// final static point, recording intervening destructible candidates.
bool Simulator::predict( Projectile & projectile, const Vec3 & gunPosition,
    const Vec3 & tracerCameraPosition )
{
    const Parameters & params = parameters();
    Prediction context = { &projectile.points, params.spaceID, projectile.refStart,
        projectile.refVelocity, tracerCameraPosition, gunPosition, projectile.autoScale,
        Vec3(), Vec3() };
    const Vec3 gravity = { 0.f, -projectile.gravity, 0.f };
    const float dt = params.serverTickLength;
    // A non-positive tick never advances; #1513 would spin here forever.
    if (!(dt > 0.f) || !std::isfinite( dt )) return true;
    Vec3 position = projectile.refStart;
    Vec3 velocity = projectile.refVelocity;
    float total = 0.f;
    std::vector<Vec3> chords;
    chords.reserve( 0x400 );
    while (true)
    {
        Vec3 nextPosition, nextVelocity;
        chords.clear();
        subdivide( nextPosition, nextVelocity, chords, position, velocity, gravity, dt,
            CHORD_TOLERANCE );
        Vec3 previous = position;
        for (const Vec3 & point : chords)
        {
            context.segmentStart = previous;
            context.segmentEnd = point;
            const float d0 = distance( previous, projectile.refStart );
            const float d1 = distance( point, projectile.refStart );
            if (d1 > projectile.maxDistance)
            {
                const float fraction = (projectile.maxDistance - d0) / (d1 - d0);
                const Vec3 clipped = { (point.x - previous.x) * fraction + previous.x,
                    previous.y + (point.y - previous.y) * fraction,
                    previous.z + (point.z - previous.z) * fraction };
                pushFinal( projectile.points, clipped,
                    referenceTime( clipped, projectile.refStart, projectile.refVelocity ) );
                return true;
            }
            if (!callbacks_.boundary) return false;
            Vec3 hit;
            if (callbacks_.boundary( hit, context.segmentStart, context.segmentEnd ))
            {
                pushFinal( projectile.points, hit,
                    referenceTime( hit, projectile.refStart, projectile.refVelocity ) );
                return true;
            }
            if (Scene * owner = scene())
            {
                std::set<std::uint64_t> seen;
                for (int i = 0; i < STATIC_ITERATIONS; ++i)
                {
                    SceneHit result;
                    const SceneFilter unseen = [&seen]( std::uint64_t item )
                        { return seen.find( item ) == seen.end(); };
                    if (!owner->collideSegment( context.spaceID, context.segmentStart,
                            context.segmentEnd, STATIC_FLAGS, unseen, result ))
                        break;
                    Point meta = { result.point, 0.f, 0u, 0u, true };
                    // Destructible material kinds 0x49..0x56 do not end the trace.
                    if (result.materialKind - 0x49u <= 0xdu)
                    {
                        meta.itemID = result.itemID;
                        meta.materialKind = result.materialKind;
                        meta.final = false;
                        seen.insert( result.itemID );
                    }
                    if (pushCandidate( context, *owner, result.point,
                            distance( result.point, context.segmentStart ), meta ))
                        return true;
                }
            }
            previous = point;
        }
        total = total + dt;
        position = nextPosition;
        velocity = nextVelocity;
        if (!(total < PREDICTION_TIME)) return true;
    }
}

AddResult Simulator::add( std::uint32_t & handle, std::int32_t shotID, float gravity,
    const Vec3 & refStart, const Vec3 & refVelocity, const Vec3 & start, float maxDistance,
    bool autoScale, const Vec3 & gunPosition, const Vec3 & tracerCameraPosition )
{
    handle = INVALID_HANDLE;
    if (find( shotID )) return AddResult::REJECTED;
    const float now = clock_();
    // A re-used ID ends its hidden predecessor's remaining flight.
    if (Projectile * hidden = find( negated( shotID ) )) hidden->startTime = now - 1.f;

    Projectile projectile;
    projectile.shotID = shotID;
    projectile.handle = INVALID_HANDLE;
    projectile.plane = Plane{ 0.f, 0.f, 0.f, 0.f };
    std::memset( &projectile.matrix, 0, sizeof( projectile.matrix ) );
    projectile.refStart = refStart;
    projectile.refVelocity = refVelocity;
    projectile.velocity0 = refVelocity;
    projectile.start = start;
    projectile.impactDirection = Vec3{ 0.f, 0.f, 0.f };
    projectile.velocity = refVelocity;
    projectile.gravity = gravity;
    projectile.startTime = now;
    projectile.maxDistance = maxDistance;
    projectile.holdTime = -1.f;
    projectile.deathTime = -1.f;
    projectile.visible = false;
    projectile.autoScale = autoScale;
    const Vec3 camera = cameraPosition_ ? cameraPosition_() : Vec3{ 0.f, 0.f, 0.f };
    setMatrix( projectile, refStart, parameters(), camera );
    if (!predict( projectile, gunPosition, tracerCameraPosition ))
        return AddResult::NO_BOUNDARY;

    if (!projectile.points.empty())
    {
        const Point & last = projectile.points.back();
        const float dx = last.position.x - start.x;
        const float dy = last.position.y - start.y;
        const float dz = last.position.z - start.z;
        if (!(std::sqrt( (dx * dx + dy * dy) + dz * dz ) >= EPSILON)) return AddResult::REJECTED;
        const float t = last.time;
        if (!(std::fabs( t ) >= EPSILON)) return AddResult::REJECTED;
        const float inverse = 1.f / t;
        Vec3 v = { inverse * dx, inverse * dy - (t * gravity) * -0.5f, inverse * dz };
        const float fromStart = distance( refStart, start );
        const float fromEnd = distance( refStart, last.position );
        if (fromStart > fromEnd) v = Vec3{ -v.x, v.y * -1.f, v.z * -1.f };
        projectile.velocity0 = v;
        projectile.velocity = v;
    }

    handle = allocateHandle();
    const std::uint32_t slot = handle & INVALID_HANDLE;
    if (slot >= indices_.size()) indices_.resize( slot + 1, -1 );
    indices_[slot] = std::int32_t( projectiles_.size() );
    projectile.handle = handle;
    projectiles_.push_back( std::move( projectile ) );
    return AddResult::ADDED;
}

// Plane through the endpoint, normal to the reference velocity there.
Plane Simulator::terminalPlane( const Projectile & projectile, const Vec3 & end )
{
    const Vec3 & s = projectile.refStart;
    const Vec3 & v = projectile.refVelocity;
    const float t = std::fabs( double( v.x ) ) > 1e-6 ?
        (s.x - end.x) / v.x : (s.z - end.z) / v.z;
    const Vec3 n = normalise( Vec3{ v.x, v.y + projectile.gravity * t, v.z } );
    return Plane{ n.x, n.y, n.z, (end.y * n.y + end.x * n.x) + end.z * n.z };
}

// 8ab800: the hidden record keeps flying under -shotID to its endpoint.
void Simulator::hide( std::int32_t shotID, const Vec3 & end )
{
    if (!find( shotID )) return;
    // 0 and INT_MIN are their own negation; #1513 would remove the record and
    // then write past the end of its vector.
    if (negated( shotID ) != shotID)
        if (Projectile * previous = find( negated( shotID ) )) remove( previous->handle );
    Projectile * projectile = find( shotID );
    projectile->shotID = negated( shotID );
    projectile->plane = terminalPlane( *projectile, end );
}

// 8ab470: an alive record stops at the endpoint; a dead one reports where.
bool Simulator::explode( std::int32_t shotID, const Vec3 & end, Vec3 & position,
    Vec3 & direction )
{
    Projectile * projectile = find( shotID );
    if (!projectile) return false;
    if (projectile->deathTime > 0.f)
    {
        position = Vec3{ projectile->matrix.m[3][0], projectile->matrix.m[3][1],
            projectile->matrix.m[3][2] };
        direction = projectile->impactDirection;
        return true;
    }
    projectile->plane = terminalPlane( *projectile, end );
    const Point * nearest = NULL;
    float best = 3.4028234663852886e38f;
    for (const Point & point : projectile->points)
    {
        const Vec3 d = sub( point.position, end );
        const float squared = (d.x * d.x + d.y * d.y) + d.z * d.z;
        if (best > squared) nearest = &point;
        best = squared < best ? squared : best;
    }
    if (nearest)
    {
        std::vector<Point> kept( 1, *nearest );
        projectile->points.swap( kept );
    }
    return false;
}

void Simulator::hold( std::int32_t shotID )
{
    if (Projectile * projectile = find( shotID ))
        projectile->holdTime = parameters().hidingTime;
}

// 011bd260: segment a->b against the plane, parameter in [0, 1].
bool Simulator::intersect( const Plane & plane, Vec3 & out, const Vec3 & a, const Vec3 & b )
{
    const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const float denominator = (plane.x * dx + plane.y * dy) + plane.z * dz;
    if (!(denominator <= -EPSILON) && EPSILON > denominator) return false;
    const float t = (plane.d - ((plane.y * a.y + plane.x * a.x) + plane.z * a.z)) /
        denominator;
    if (!(t >= 0.f) || 1.f < t) return false;
    out = Vec3{ dx * t + a.x, dy * t + a.y, dz * t + a.z };
    return true;
}

// 8ac170: orientation along the current velocity, optionally scaled with
// camera distance beyond the auto-scale distance.
void Simulator::setMatrix( Projectile & projectile, const Vec3 & position,
    const Parameters & params, const Vec3 & camera )
{
    float (&m)[4][4] = projectile.matrix.m;
    const Vec3 d = normalise( projectile.velocity );
    float a = 0.f, b = 1.f;
    if (std::fabs( d.y ) > 0.97f) { a = 1.f; b = 0.f; }
    const Vec3 r = normalise( Vec3{ d.z * b, -(d.z * a), a * d.y - b * d.x } );
    m[0][0] = r.x; m[0][1] = r.y; m[0][2] = r.z; m[0][3] = 0.f;
    m[1][0] = r.z * d.y - r.y * d.z;
    m[1][1] = d.z * r.x - r.z * d.x;
    m[1][2] = r.y * d.x - r.x * d.y;
    m[1][3] = 0.f;
    m[2][0] = d.x; m[2][1] = d.y; m[2][2] = d.z; m[2][3] = 0.f;
    m[3][0] = position.x; m[3][1] = position.y; m[3][2] = position.z; m[3][3] = 1.f;
    if (!projectile.autoScale) return;

    const float cy = camera.y - position.y, cx = camera.x - position.x;
    const float cz = camera.z - position.z;
    const float squared = (cy * cy + cx * cx) + cz * cz;
    const float limit = params.autoScaleDistance;
    float scale = 1.f;
    if (!(limit * limit >= squared) && !params.autoScaleOff)
        scale = std::sqrt( squared ) * (1.f / limit);
    float s[4][4] = {};
    for (int row = 0; row < 3; ++row)
        s[row][row] = scale / std::sqrt( (m[row][0] * m[row][0] + m[row][1] * m[row][1]) +
            m[row][2] * m[row][2] );
    s[3][3] = 1.f;
    // D3DXMatrixScaling then D3DXMatrixMultiply(S, M).
    float product[4][4];
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            product[row][column] = ((s[row][0] * m[0][column] + s[row][1] * m[1][column]) +
                s[row][2] * m[2][column]) + s[row][3] * m[3][column];
    std::memcpy( m, product, sizeof( product ) );
}

// 8ab9b0: true when the record reaches a terminal point this frame.
bool Simulator::update( Projectile & p, float now, const Parameters & params, Scene * owner,
    const Vec3 & camera )
{
    float elapsed = now;
    if (p.holdTime > 0.f) elapsed = elapsed - p.holdTime;
    elapsed = elapsed - p.startTime;
    if (0.f >= elapsed) return false;
    if (!p.visible && elapsed > params.hidingTime) p.visible = true;

    const Point * hit = NULL;
    for (const Point & point : p.points)
    {
        if (point.time > elapsed) break;
        if (point.final) { hit = &point; break; }
        if (!owner) continue;
        const std::uint64_t item = point.itemID;
        SceneHit result;
        if (owner->collideSegment( params.spaceID, point.position, point.position,
                STATIC_FLAGS, [item]( std::uint64_t other ) { return other == item; },
                result ))
        {
            hit = &point;
            break;
        }
    }

    const Vec3 previous = { p.matrix.m[3][0], p.matrix.m[3][1], p.matrix.m[3][2] };
    Vec3 end;
    Vec3 velocity;
    if (hit)
    {
        end = hit->position;
        if (activePlane( p.plane ) && beyondPlane( p.plane, end ))
        {
            Vec3 crossing;
            if (intersect( p.plane, crossing, previous, end )) end = crossing;
        }
        velocity = p.velocity0;
    }
    else
    {
        const float t = elapsed;
        const float gt = p.gravity * t;
        const Vec3 position = { p.velocity0.x * t + p.start.x,
            (p.start.y + p.velocity0.y * t) + (gt * t) * -0.5f, p.start.z + p.velocity0.z * t };
        velocity = Vec3{ p.velocity0.x, p.velocity0.y - gt, p.velocity0.z };
        if (!(activePlane( p.plane ) && beyondPlane( p.plane, position )))
        {
            p.velocity = velocity;
            setMatrix( p, position, params, camera );
            return false;
        }
        if (!intersect( p.plane, end, previous, position ) &&
            !intersect( p.plane, end, p.refStart, p.start ))
            end = previous;
    }
    p.impactDirection = normalise( sub( end, previous ) );
    p.velocity = velocity;
    p.visible = false;
    const Vec3 backoff = { end.x - p.impactDirection.x * IMPACT_BACKOFF,
        end.y - p.impactDirection.y * IMPACT_BACKOFF, end.z - p.impactDirection.z * IMPACT_BACKOFF };
    setMatrix( p, backoff, params, camera );
    return true;
}

// 8ac4d0. Python callbacks may add, hide or remove records; each step
// re-resolves its record by handle instead of holding a vector iterator.
void Simulator::tick()
{
    if (!clock_) return;
    const float now = clock_();
    const Vec3 camera = cameraPosition_ ? cameraPosition_() : Vec3{ 0.f, 0.f, 0.f };
    std::vector<std::uint32_t> handles;
    handles.reserve( projectiles_.size() );
    for (const Projectile & projectile : projectiles_) handles.push_back( projectile.handle );
    for (std::uint32_t handle : handles)
    {
        Projectile * p = const_cast<Projectile *>( lookup( handle ) );
        if (!p || !(0.f > p->deathTime)) continue;
        if (!update( *p, now, parameters(), scene(), camera )) continue;
        p->deathTime = now;
        if (callbacks_.kill)
        {
            const std::int32_t shotID = p->shotID;
            const Vec3 position = { p->matrix.m[3][0], p->matrix.m[3][1], p->matrix.m[3][2] };
            const Vec3 direction = p->impactDirection;
            const auto kill = callbacks_.kill;
            kill( shotID, position, direction );
        }
    }

    const float after = parameters().timeAfterDeath;
    std::size_t index = 0;
    while (index < projectiles_.size())
    {
        const Projectile & candidate = projectiles_[index];
        if (!(candidate.deathTime > 0.f) || !(now - candidate.deathTime >= after))
        {
            ++index;
            continue;
        }
        const std::uint32_t handle = candidate.handle;
        if (callbacks_.erase)
        {
            const auto erase = callbacks_.erase;
            erase( candidate.shotID );
        }
        if (lookup( handle )) index = remove( handle );
        else index = 0;
    }
}

void Simulator::detach()
{
    callbacks_ = Callbacks();
    projectiles_.clear();
    std::fill( indices_.begin(), indices_.end(), -1 );
}

} // namespace stock_ballistics
} // namespace wot
