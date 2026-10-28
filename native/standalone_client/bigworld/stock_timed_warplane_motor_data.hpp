#pragma once

// Exact #1513 PyTimedWarplaneMotor trajectory (state at motor +0x10):
// init 00734c90, insertion 00734230, segment sample 00734520 and the
// per-rev update 007348e0. Header-only and engine-free for host probes.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wot
{
typedef std::array<float, 16> StockWarplaneMatrix; // D3DX row-major
typedef std::array<float, 3> StockWarplaneVector;

// D3DX/UCRT entry points of 007348e0, injected like the other stock kernels.
struct StockWarplaneMath
{
    StockWarplaneMatrix (*lookAtLH)( const StockWarplaneVector & eye, const StockWarplaneVector & at,
        const StockWarplaneVector & up );
    void (*inverse)( StockWarplaneMatrix & value );
    std::array<float, 2> (*normalize2)( float x, float y );
    float (*atan)( float value );
    StockWarplaneMatrix (*rotationZ)( float angle );
    StockWarplaneMatrix (*multiply)( const StockWarplaneMatrix & first, const StockWarplaneMatrix & second );
};

// 0x1c-byte control point: position, velocity, absolute BigWorld.time stamp.
struct StockWarplanePoint
{
    StockWarplaneVector position = {};
    StockWarplaneVector velocity = {};
    float time = 0.f;
};

struct StockTimedWarplane
{
    float roll = 0.f;                  // +0x00
    StockWarplaneMatrix transform = {{ 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f }}; // +0x04 published world transform
    float startTime = 0.f;             // +0x44
    float elapsed = 0.f;               // +0x48 sum of rev dTime
    float rollSpeed = 0.f;             // +0x4c
    StockWarplaneVector position = {}; // +0x58 sampled position
    StockWarplaneVector velocity = {}; // +0x64 sampled velocity
    std::vector<StockWarplanePoint> points; // +0x70
    std::uint32_t segment = 0;         // +0x7c
    float clock = 0.f;                 // +0x80 trajectory time (start + elapsed)
};

namespace detail
{
// 007341d0: the first point also seeds the sampled position and velocity.
inline std::uint32_t pushStockWarplanePoint( StockTimedWarplane & s, const StockWarplanePoint & point )
{
    if (s.points.empty())
    {
        s.position = point.position;
        s.velocity = point.velocity;
    }
    s.points.push_back( point );
    return static_cast<std::uint32_t>( s.points.size() - 1 );
}

// 00734520: cubic Hermite between timed points; holds before the first point
// and copies the last point once its segment is reached.
inline void sampleStockWarplane( StockTimedWarplane & s, float elapsed )
{
    const float t = elapsed + s.clock;
    s.clock = t;
    const std::size_t count = s.points.size();
    if (count < 2 || (!s.points.empty() && t < s.points[0].time))
        return;
    while (s.segment < count - 1)
    {
        const float next = s.points[s.segment + 1].time;
        if (t < next || t == next)
            break;
        ++s.segment;
    }
    const StockWarplanePoint & a = s.points[s.segment];
    if (s.segment == count - 1)
    {
        s.position = a.position;
        s.velocity = a.velocity;
        return;
    }
    const StockWarplanePoint & b = s.points[s.segment + 1];
    const float span = std::fabs( a.time - b.time );
    const float inverse = 1.f / span;
    const float u = (t - a.time) * inverse;
    const float u2 = u * u;
    const float u3 = u2 * u;
    const float h11 = u3 - u2;
    const float h01 = u2 * 3.f - (u3 + u3);
    const float h10 = (u3 - (u2 + u2)) + u;
    const float h00 = ((u3 + u3) - u2 * 3.f) + 1.f;
    StockWarplaneVector m0, m1;
    for (int axis = 0; axis < 3; ++axis)
    {
        m0[axis] = a.velocity[axis] * span;
        m1[axis] = b.velocity[axis] * span;
    }
    for (int axis = 0; axis < 3; ++axis)
        s.position[axis] = m0[axis] * h10 + a.position[axis] * h00 + b.position[axis] * h01 + m1[axis] * h11;
    const float d11 = u * 3.f * u - (u + u);
    const float d10 = (d11 - (u + u)) + 1.f;
    const float d00 = u * 6.f * u - u * 6.f;
    for (int axis = 0; axis < 3; ++axis)
        s.velocity[axis] = (d00 * (a.position[axis] - b.position[axis]) + m0[axis] * d10 + m1[axis] * d11) *
            inverse;
}
}

// 007348e0 with `now` = BigWorld.time and `elapsed` = rev dTime. `callback`
// receives (point index, position, velocity) for every point crossed.
template <class Callback>
void advanceStockTimedWarplane( StockTimedWarplane & s, float now, float elapsed, Callback callback,
    const StockWarplaneMath & math )
{
    if (s.points.empty())
        return;
    const std::uint32_t previousSegment = s.segment;
    const StockWarplaneVector previousPosition = s.position;
    const StockWarplaneVector previousVelocity = s.velocity;
    s.elapsed += elapsed;
    detail::sampleStockWarplane( s, elapsed );
    StockWarplaneVector position = s.position;
    const StockWarplaneVector velocity = s.velocity;
    float previousX;
    float previousZ;
    if (s.segment < static_cast<std::uint32_t>( s.points.size() ) - 1u)
    {
        previousX = previousPosition[0];
        previousZ = previousPosition[2];
    }
    else
    {
        // Past the last point the motor keeps flying along its last velocity.
        const float beyond = now - s.points.back().time;
        position[2] = velocity[2] * beyond + position[2];
        previousZ = velocity[2] * (beyond - elapsed) + previousPosition[2];
        position[0] = position[0] + velocity[0] * beyond;
        position[1] = velocity[1] * beyond + position[1];
        previousX = velocity[0] * (beyond - elapsed) + previousPosition[0];
    }
    for (std::uint32_t index = previousSegment + 1; index <= s.segment; ++index)
        callback( index, position, velocity );
    const StockWarplaneVector at = {{ velocity[0] + position[0], position[1] + velocity[1],
        position[2] + velocity[2] }};
    const StockWarplaneVector up = {{ 0.f, 1.f, 0.f }};
    s.transform = math.lookAtLH( position, at, up );
    math.inverse( s.transform );
    const float dx = position[0] - previousX;
    const float dz = position[2] - previousZ;
    const float travelled = std::sqrt( dx * dx + dz * dz );
    float curvature = 0.f;
    if (static_cast<double>( travelled ) >= 1e-05)
    {
        const std::array<float, 2> before = math.normalize2( previousVelocity[0], previousVelocity[2] );
        const std::array<float, 2> after = math.normalize2( velocity[0], velocity[2] );
        const float sign = velocity[2] * previousVelocity[0] - previousVelocity[2] * velocity[0] < 0.f ?
            -0.101936795f : 0.101936795f;
        const float ax = after[0] - before[0];
        const float az = after[1] - before[1];
        curvature = std::sqrt( ax * ax + az * az ) * (sign / travelled);
    }
    // Bank toward atan(v^2 * curvature / g) at rollSpeed rad/s without overshoot.
    const float target = math.atan( (velocity[1] * velocity[1] + velocity[0] * velocity[0] +
        velocity[2] * velocity[2]) * curvature );
    const float current = s.roll;
    const float step = target <= current ? -elapsed : elapsed;
    float roll = s.rollSpeed * step + current;
    if ((target - current) * (target - roll) < 0.f)
        roll = target;
    s.roll = roll;
    s.transform = math.multiply( math.rotationZ( roll ), s.transform );
}

// 00734c90 from the factory: reset, push both control points, then sample
// once at `now` with zero elapsed time.
inline void initStockTimedWarplane( StockTimedWarplane & s, float now, const StockWarplanePoint & first,
    const StockWarplanePoint & second, const StockWarplaneMath & math )
{
    s.points.clear();
    s.segment = 0;
    s.clock = now;
    s.position = StockWarplaneVector();
    s.velocity = StockWarplaneVector();
    s.startTime = now;
    s.elapsed = 0.f;
    detail::pushStockWarplanePoint( s, first );
    detail::pushStockWarplanePoint( s, second );
    advanceStockTimedWarplane( s, now, 0.f, []( std::uint32_t, const StockWarplaneVector &,
        const StockWarplaneVector & ) {}, math );
}

// 00734230: a point is accepted only after the motor clock and the last point.
// Once the last segment is reached the current pose is inserted first.
// Returns the accepted point index, or -1 when silently ignored.
inline long addStockTimedWarplanePoint( StockTimedWarplane & s, const StockWarplanePoint & point )
{
    const float now = s.elapsed + s.startTime;
    if (!(now < point.time) || !(s.points.back().time < point.time))
        return -1;
    if (static_cast<std::uint32_t>( s.points.size() ) - 1u <= s.segment)
    {
        StockWarplanePoint current;
        current.position = {{ s.transform[12], s.transform[13], s.transform[14] }};
        current.velocity = s.points.back().velocity;
        current.time = now;
        detail::pushStockWarplanePoint( s, current );
    }
    return static_cast<long>( detail::pushStockWarplanePoint( s, point ) );
}
}
