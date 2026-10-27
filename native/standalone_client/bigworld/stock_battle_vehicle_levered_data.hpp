#pragma once

// Exact #1513 BW::LeveredSuspension kernels: constructor data (00786180),
// ground pass (00783a60), per-entry update (00787f50) and node writes (00789060).
// Header-only and engine-free so host probes execute the client arithmetic.
#include "stock_battle_vehicle_animation_data.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wot
{
// D3DX services of the base plus the UCRT/x87 entry points the bodies use.
struct StockLeveredMath
{
    StockAnimationMath base;
    float (*cos)( float value );             // __libm_sse2_cosf
    float (*sin)( float value );             // __libm_sse2_sinf
    float (*atan2)( float y, float x );      // fld y; fld x; fpatan; fstp dword
};

// 007860c0: limit record built from (minAngle, maxAngle) in radians.
struct StockLeverLimits
{
    float cos1 = 0.f, sin1 = 0.f, cos2 = 0.f, sin2 = 0.f;
    bool forward = false; // +0x18: cos(minAngle) >= 0
};

// Native 0x30 lever state (LeveredSuspension state +0x00 vector).
struct StockLeverState
{
    float cosine = 0.f, sine = 0.f;     // +0x00 solved start-node angle
    StockAnimationVector joint = {};    // +0x08 solved joint position (frame space)
    float jointLength = 0.f;            // +0x14 |joint local translation|
    float trackLength = 0.f;            // +0x18 |track local translation|
    StockLeverLimits limits;            // +0x1c..+0x2c
};

// Native 0xcc authored record: three (node index, authored local) pairs (00788ba0).
struct StockLeverAuthored
{
    int startNode = -1, jointNode = -1, trackNode = -1;
    StockAnimationMatrix start = stockAnimationIdentity();
    StockAnimationMatrix joint = stockAnimationIdentity();
    StockAnimationMatrix track = stockAnimationIdentity();
};

// Native 32-byte ground contact (00783a60 result, smoothed copy at state +0x0c).
struct StockLeverContact
{
    StockAnimationVector position = {};
    StockAnimationVector normal = {};
    float distance = 0.f;
    std::uint8_t hit = 0;
};

struct StockLeveredSuspension
{
    std::vector<StockLeverAuthored> authored; // state +0x24, sorted
    std::vector<StockLeverState> levers;      // state +0x00, sorted
    std::vector<StockAnimationVector> anchors; // state +0x18 start-node local translations
    std::vector<StockAnimationVector> probes;  // entry +0x50 ray origins (frame space)
    std::vector<StockLeverContact> contacts;   // flags +0x04 ground pass results
    std::vector<StockLeverContact> smoothed;   // state +0x0c
    float rayLength = 0.f;                     // entry +0x5c
    float interpolationSpeedMul = 1.f;         // state +0x34
    std::uint32_t split = 0;                   // state +0x3c last sorted lever with start x > 0
    std::uint8_t history = 1;                  // flags +0x00 LOD history bits (00784600 seeds 1)
};

struct StockLeverInput
{
    int startNode = -1, jointNode = -1, trackNode = -1;
    float minAngle = 0.f, maxAngle = 0.f;
};

struct StockLeveredNodeWrite
{
    int node = -1;
    StockAnimationMatrix local = {};
};

inline StockLeverLimits makeStockLeverLimits( float minAngle, float maxAngle, const StockLeveredMath & math )
{
    StockLeverLimits limits;
    limits.forward = math.cos( minAngle ) >= 0.f;
    float first = minAngle, second = maxAngle;
    if (!limits.forward)
    {
        first = maxAngle + 3.14159274f;
        second = minAngle + 3.14159274f;
    }
    limits.cos1 = math.cos( first );
    limits.sin1 = math.sin( first );
    limits.cos2 = math.cos( second );
    limits.sin2 = math.sin( second );
    return limits;
}

// 011bbaa0: writes the upper 3x3 of an axis-angle rotation in place.
inline void stockLeveredRotateAxis( StockAnimationMatrix & m, const StockAnimationVector & a, float angle,
    const StockLeveredMath & math )
{
    const float c = math.cos( angle );
    const float s = math.sin( angle );
    const float t = 1.f - c;
    m[0] = a[0] * a[0] * t + c;
    m[5] = a[1] * a[1] * t + c;
    m[10] = a[2] * a[2] * t + c;
    float p = a[0] * a[1] * t, q = a[2] * s;
    m[4] = q + p;
    m[1] = p - q;
    p = a[0] * a[2] * t; q = a[1] * s;
    m[8] = p - q;
    m[2] = q + p;
    p = a[2] * a[1] * t; q = a[0] * s;
    m[9] = q + p;
    m[6] = p - q;
}

// 00785910 comparator, inlined: right side (x > 0) first, then larger z.
inline bool stockLeverSortsBefore( const StockAnimationMatrix & value, const StockAnimationMatrix & other )
{
    const float vx = value[12], ox = other[12];
    if (vx > 0.f && 0.f > ox)
        return true;
    return vx * ox > 0.f && !(other[14] >= value[14]);
}

// 00786180 data. `locals` are the compound's authored node locals; returns false
// for inputs the original cannot handle (no levers, unresolved node, > 32 levers).
inline bool buildStockLeveredSuspension( StockLeveredSuspension & out, const std::vector<StockLeverInput> & levers,
    const std::vector<StockAnimationMatrix> & locals, float interpolationSpeedMul, const StockLeveredMath & math )
{
    const std::size_t count = levers.size();
    if (count == 0 || count > 32)
        return false;
    for (const StockLeverInput & lever : levers)
        for (int node : { lever.startNode, lever.jointNode, lever.trackNode })
            if (node < 0 || static_cast<std::size_t>( node ) >= locals.size())
                return false;
    std::vector<StockLeverAuthored> authored( count );
    std::vector<StockLeverState> states( count );
    for (std::size_t i = 0; i < count; ++i)
    {
        const StockLeverInput & lever = levers[i];
        StockLeverAuthored & a = authored[i];
        a.startNode = lever.startNode; a.start = locals[lever.startNode];
        a.jointNode = lever.jointNode; a.joint = locals[lever.jointNode];
        a.trackNode = lever.trackNode; a.track = locals[lever.trackNode];
        const StockAnimationMatrix & j = a.joint;
        const StockAnimationMatrix & t = a.track;
        states[i].jointLength = std::sqrt( j[12] * j[12] + j[13] * j[13] + j[14] * j[14] );
        states[i].trackLength = std::sqrt( t[12] * t[12] + t[13] * t[13] + t[14] * t[14] );
        states[i].limits = makeStockLeverLimits( lever.minAngle, lever.maxAngle, math );
    }
    // 00788c20: index insertion sort (std::sort below its 32-element cutoff).
    std::vector<std::size_t> order( count );
    for (std::size_t i = 0; i < count; ++i)
        order[i] = i;
    for (std::size_t next = 1; next < count; ++next)
    {
        const std::size_t value = order[next];
        const StockAnimationMatrix & v = authored[value].start;
        if (stockLeverSortsBefore( v, authored[order[0]].start ))
        {
            for (std::size_t k = next; k > 0; --k)
                order[k] = order[k - 1];
            order[0] = value;
            continue;
        }
        std::size_t hole = next;
        while (stockLeverSortsBefore( v, authored[order[hole - 1]].start ))
        {
            order[hole] = order[hole - 1];
            --hole;
        }
        order[hole] = value;
    }
    out = StockLeveredSuspension();
    for (std::size_t k = 0; k < count; ++k)
    {
        out.authored.push_back( authored[order[k]] );
        out.levers.push_back( states[order[k]] );
        if (out.authored[k].start[12] > 0.f)
            out.split = static_cast<std::uint32_t>( k );
        const StockAnimationMatrix & s = out.authored[k].start;
        out.anchors.push_back( {{ s[12], s[13], s[14] }} );
    }
    out.probes = out.anchors;
    out.contacts.assign( count, StockLeverContact() );
    out.rayLength = (out.levers[0].trackLength + out.levers[0].jointLength) * 1.5f;
    out.interpolationSpeedMul = interpolationSpeedMul > 1.f ? interpolationSpeedMul : 1.f;
    return true;
}

// 00783a60 first loop: shift in this frame's LOD gate (00781f70).
inline std::uint8_t advanceStockLeveredHistory( std::uint8_t history, bool allowed )
{
    return static_cast<std::uint8_t>( (allowed ? 1 : 0) | (history << 1) );
}

// 00783a60 per eligible entry. `frame` is the entry Matrix lambda (hull world).
// prepare(box[6]) receives (min.x, min.y - 1, min.z, max) when a space exists;
// query(origin, direction, length, distance&, normal&) is the collider ray.
template <class Prepare, class Query>
void groundStockLevered( StockLeveredSuspension & s, const StockAnimationMatrix & frame,
    const std::array<float, 6> & aabb, Prepare prepare, Query query, const StockLeveredMath & math )
{
    if (!(s.history & 1))
        return;
    std::vector<StockAnimationVector> points;
    for (const StockAnimationVector & probe : s.probes)
    {
        float t[4];
        math.base.transform( t, probe, frame );
        points.push_back( {{ t[0], t[1], t[2] }} );
    }
    const StockAnimationVector down = {{ -frame[4], -frame[5], -frame[6] }};
    const std::array<float, 6> box = {{ aabb[0], aabb[1] - 1.f, aabb[2], aabb[3], aabb[4], aabb[5] }};
    prepare( box );
    const float length = s.rayLength;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const StockAnimationVector & p = points[i];
        StockLeverContact contact;
        float distance = 0.f;
        StockAnimationVector normal = {};
        if (query( p, down, length, distance, normal ))
        {
            contact.normal = math.base.normalize( normal );
            contact.hit = 1;
        }
        else
        {
            distance = length;
            contact.normal = {{ -down[0], -down[1], -down[2] }};
            contact.hit = 0;
        }
        contact.position = {{ down[0] * distance + p[0], p[1] + down[1] * distance, p[2] + down[2] * distance }};
        contact.distance = distance;
        s.contacts[i] = contact;
    }
}

namespace detail
{
// Local frame block built by 00787f50 and read by 00788830/007878d0.
struct StockLeverFrame
{
    StockAnimationMatrix world, inverse;
    StockAnimationVector axisZ, side, up; // +0x80 row2, +0x8c -row0, +0x98 row1
};

inline StockAnimationVector stockLeverCrossRows( const StockAnimationVector & n, const StockAnimationVector & z )
{
    return {{ z[2] * n[1] - z[1] * n[2], z[0] * n[2] - n[0] * z[2], n[0] * z[1] - z[0] * n[1] }};
}

inline void stockLeverRemoveSide( StockAnimationVector & n, const StockAnimationVector & p, const StockLeveredMath & math )
{
    const float d = n[1] * p[1] + n[0] * p[0] + n[2] * p[2];
    n[1] = n[1] - p[1] * d;
    n[0] = n[0] - p[0] * d;
    n[2] = n[2] - p[2] * d;
    n = math.base.normalize( n );
}

// 00788830: contact normal into the lever plane, cos(pi/8) up-cone clamp,
// then the contact re-expressed below the anchor along the frame up axis.
inline void projectStockLeverContact( const StockLeverFrame & f, const StockAnimationVector & anchor,
    StockLeverContact & c, float coneCos, const StockLeveredMath & math )
{
    StockAnimationVector & n = c.normal;
    stockLeverRemoveSide( n, f.side, math );
    const StockAnimationVector & up = f.up;
    if (coneCos > up[0] * n[0] + up[1] * n[1] + up[2] * n[2])
    {
        StockAnimationMatrix r = {};
        StockAnimationVector axis = {{ up[2] * n[1] - n[2] * up[1], n[2] * up[0] - up[2] * n[0],
            n[0] * up[1] - up[0] * n[1] }};
        axis = math.base.normalize( axis );
        stockLeveredRotateAxis( r, axis, 0.392699093f, math );
        n = math.base.normalize( math.base.transformNormal( up, r ) );
    }
    const StockAnimationVector e = stockLeverCrossRows( n, f.axisZ );
    StockAnimationVector g = {{ e[2] * n[1] - e[1] * n[2], e[0] * n[2] - e[2] * n[0], e[1] * n[0] - e[0] * n[1] }};
    g = math.base.normalize( g );
    float q[4], r[4];
    math.base.transform( q, anchor, f.world );
    math.base.transform( r, c.position, f.inverse );
    const float denominator = -f.axisZ[0] * g[0] + -f.axisZ[1] * g[1] + -f.axisZ[2] * g[2];
    const float k = (anchor[2] - r[2]) / denominator;
    const float px = c.position[0] - g[0] * k;
    const float py = c.position[1] - g[1] * k;
    const float pz = c.position[2] - g[2] * k;
    const float dx = q[0] - px, dy = q[1] - py, dz = q[2] - pz;
    const float distance = std::sqrt( dy * dy + dx * dx + dz * dz );
    c.distance = distance;
    c.position = {{ q[0] - distance * up[0], q[1] - up[1] * distance, q[2] - distance * up[2] }};
}

// 007878d0: two-bone circle intersection, start angle and the limit clamp
// (DAT_01a12e8c = 1). Writes the lever angle/joint and the track contact.
inline void solveStockLever( StockLeverContact & c, StockLeverState & lever, const StockAnimationVector & anchor,
    const StockLeverFrame & f, const StockLeveredMath & math )
{
    StockAnimationVector & n = c.normal;
    stockLeverRemoveSide( n, f.side, math );
    const StockAnimationVector e = stockLeverCrossRows( n, f.axisZ );
    StockAnimationVector g = {{ n[1] * e[2] - n[2] * e[1], e[0] * n[2] - n[0] * e[2], n[0] * e[1] - e[0] * n[1] }};
    g = math.base.normalize( g );
    const StockAnimationVector & p = f.side;
    const StockAnimationVector & up = f.up;
    const StockAnimationVector & z = f.axisZ;
    const StockAnimationVector h = {{ p[2] * g[1] - g[2] * p[1], g[2] * p[0] - p[2] * g[0], p[1] * g[0] - p[0] * g[1] }};
    const float a = lever.jointLength;
    const float l = lever.trackLength;
    const float b = (up[1] * g[1] + up[0] * g[0] + g[2] * up[2]) * c.distance;
    const float w = (h[1] * up[1] + up[0] * h[0] + h[2] * up[2]) * c.distance - l;
    const float discriminant = b * 4.f * b - (b * b - (a * a - w * w)) * 4.f;
    const float sign = lever.limits.forward ? -1.f : 1.f;
    float t = 0.f;
    if (discriminant >= 0.f)
        t = std::sqrt( discriminant ) * sign * 0.5f + b;
    float q[4];
    math.base.transform( q, anchor, f.world );
    StockAnimationVector & pos = c.position;
    pos[0] = g[0] * t + pos[0];
    pos[1] = g[1] * t + pos[1];
    pos[2] = g[2] * t + pos[2];
    StockAnimationVector & joint = lever.joint;
    joint = {{ pos[0] + h[0] * l, h[1] * l + pos[1], pos[2] + h[2] * l }};
    const float inverse = 1.f / a;
    const float dy = (joint[1] - q[1]) * inverse;
    const float dx = (joint[0] - q[0]) * inverse;
    const float dz = inverse * (joint[2] - q[2]);
    const float upDot = dy * up[1] + dx * up[0] + up[2] * dz;
    const float zDot = dy * z[1] + dx * z[0] + z[2] * dz;
    lever.sine = -upDot;
    lever.cosine = zDot;
    if (!lever.limits.forward)
    {
        lever.sine = upDot;
        lever.cosine = -zDot;
    }
    const float sine = lever.sine;
    float limitSine, deltaCosine;
    bool clamp = true;
    if (lever.limits.sin1 > sine)
    {
        deltaCosine = lever.limits.cos1 - lever.cosine;
        lever.cosine = lever.limits.cos1;
        limitSine = lever.limits.sin1;
    }
    else if (sine > lever.limits.sin2)
    {
        deltaCosine = lever.limits.cos2 - lever.cosine;
        lever.cosine = lever.limits.cos2;
        limitSine = lever.limits.sin2;
    }
    else
    {
        clamp = false;
        limitSine = deltaCosine = 0.f;
    }
    if (clamp)
    {
        float deltaSine = limitSine - sine;
        lever.sine = limitSine;
        if (!lever.limits.forward)
            deltaCosine = -deltaCosine;
        else
            deltaSine = -deltaSine;
        const float u = a * deltaCosine;
        const float v = a * deltaSine;
        const float oz = v * up[2] + z[2] * u;
        const float oy = u * z[1] + v * up[1];
        const float ox = z[0] * u + v * up[0];
        joint[0] = joint[0] + ox;
        joint[1] = joint[1] + oy;
        joint[2] = oz + joint[2];
        pos[0] = pos[0] + ox;
        pos[1] = oy + pos[1];
        pos[2] = oz + pos[2];
    }
    pos = {{ joint[0] - l * up[0], joint[1] - up[1] * l, joint[2] - up[2] * l }};
}

// 00787710: rotation about frame row0 that aligns row2 with contacts[i] - contacts[j].
inline StockAnimationMatrix stockLeverChain( const std::vector<StockLeverContact> & contacts, std::uint32_t i,
    std::uint32_t j, const StockAnimationMatrix & fallback, const StockAnimationMatrix & m,
    const StockLeveredMath & math )
{
    if (j <= i)
        return fallback;
    const StockAnimationVector & a = contacts[i].position;
    const StockAnimationVector & b = contacts[j].position;
    const StockAnimationVector d = math.base.normalize( {{ a[0] - b[0], a[1] - b[1], a[2] - b[2] }} );
    const float cx = m[10] * d[1] - m[9] * d[2];
    const float cy = d[2] * m[8] - m[10] * d[0];
    const float cz = m[9] * d[0] - d[1] * m[8];
    const float y = cy * m[1] + m[0] * cx + cz * m[2];
    const float x = m[9] * d[1] + m[8] * d[0] + m[10] * d[2];
    StockAnimationMatrix r = stockAnimationIdentity();
    stockLeveredRotateAxis( r, {{ m[0], m[1], m[2] }}, math.atan2( y, x ), math );
    return math.base.multiply( m, r );
}

inline StockAnimationMatrix stockLeverWithoutTranslation( StockAnimationMatrix m )
{
    m[12] = m[13] = m[14] = 0.f; // DAT_021538b4 (zero vector)
    m[15] = 1.f;
    return m;
}
}

// 005adf30: DAT_01d32450 = cosf(pi/8).
inline float stockLeverConeCos( const StockLeveredMath & math )
{
    return math.cos( 0.392699093f );
}

// Exact 00787f50 for one entry. `frame` is the entry Matrix lambda result.
inline void updateStockLevered( StockLeveredSuspension & s, float elapsed, const StockAnimationMatrix & frame,
    const StockLeveredMath & math )
{
    if (s.smoothed.empty() && !s.contacts.empty())
        s.smoothed = s.contacts;
    if (!(s.history & 1))
        return;
    detail::StockLeverFrame f;
    f.world = frame;
    f.inverse = frame;
    math.base.inverse( f.inverse );
    f.axisZ = {{ frame[8], frame[9], frame[10] }};
    f.side = {{ -frame[0], -frame[1], -frame[2] }};
    f.up = {{ frame[4], frame[5], frame[6] }};
    const float rate = s.interpolationSpeedMul * elapsed * 1.f;
    float alpha;
    if ((s.history & 3) == 1)
        alpha = 1.f;
    else if (0.f > rate)
        alpha = 0.f;
    else
        alpha = rate < 1.f ? rate : 1.f; // minss: NaN selects 1
    const float coneCos = stockLeverConeCos( math );
    for (std::size_t i = 0; i < s.contacts.size(); ++i)
    {
        StockLeverContact & c = s.contacts[i];
        const StockLeverContact & previous = s.smoothed[i];
        detail::projectStockLeverContact( f, s.anchors[i], c, coneCos, math );
        // DAT_01a12e8d = 1: exponential smoothing of normal and distance.
        const float keep = 1.f - alpha;
        c.normal = math.base.normalize( {{ c.normal[0] * alpha + previous.normal[0] * keep,
            c.normal[1] * alpha + previous.normal[1] * keep, c.normal[2] * alpha + previous.normal[2] * keep }} );
        const float distance = c.distance;
        const float smoothed = (distance - previous.distance) * alpha + previous.distance;
        c.distance = smoothed;
        const float delta = distance - smoothed;
        c.position = {{ f.up[0] * delta + c.position[0], f.up[1] * delta + c.position[1],
            f.up[2] * delta + c.position[2] }};
        s.smoothed[i] = c;
        detail::solveStockLever( c, s.levers[i], s.anchors[i], f, math );
        float t[4];
        math.base.transform( t, s.levers[i].joint, f.inverse );
        float z = t[2];
        const float old = s.probes[i][2];
        if (s.levers[i].trackLength * 0.2f > std::fabs( t[2] - old ))
            z = old;
        s.probes[i] = {{ s.anchors[i][0], s.anchors[i][1], z }};
    }
}

// Exact 00789060 for one entry. `locals` is mutated in the original order and
// every write is appended; `worlds` are the compound world matrices (node 0 = root).
inline void writeStockLeveredNodes( const StockLeveredSuspension & s, const StockAnimationMatrix & frame,
    std::vector<StockAnimationMatrix> & locals, const std::vector<StockAnimationMatrix> & worlds,
    std::vector<StockLeveredNodeWrite> & writes, const StockLeveredMath & math )
{
    auto write = [&]( int node, const StockAnimationMatrix & value ) {
        locals[node] = value;
        writes.push_back( { node, value } );
    };
    if ((s.history & 3) == 2)
        for (const StockLeverAuthored & a : s.authored)
        {
            write( a.startNode, a.start );
            write( a.jointNode, a.joint );
            write( a.trackNode, a.track );
        }
    if (!(s.history & 1))
        return;
    const StockAnimationMatrix root = detail::stockLeverWithoutTranslation( worlds[0] );
    const StockAnimationMatrix hull = detail::stockLeverWithoutTranslation( frame );
    const std::uint32_t last = static_cast<std::uint32_t>( s.smoothed.size() ) - 1u;
    const StockAnimationMatrix front = detail::stockLeverChain( s.smoothed, 0, s.split, root, hull, math );
    const StockAnimationMatrix back = detail::stockLeverChain( s.smoothed, s.split + 1u, last, root, hull, math );
    for (std::size_t k = 0; k < s.contacts.size(); ++k)
    {
        const StockLeverAuthored & a = s.authored[k];
        StockAnimationMatrix start = locals[a.startNode];
        const float angle = math.atan2( s.levers[k].sine, s.levers[k].cosine );
        StockAnimationMatrix rotated = math.base.rotationX( angle );
        for (int c = 12; c < 16; ++c)
            rotated[c] = start[c];
        write( a.startNode, rotated );
        StockAnimationMatrix track = k > s.split ? back : front;
        StockAnimationMatrix joint = worlds[a.jointNode];
        math.base.inverse( joint );
        track[12] = s.contacts[k].position[0];
        track[13] = s.contacts[k].position[1];
        track[14] = s.contacts[k].position[2];
        track[15] = 1.f;
        write( a.trackNode, math.base.multiply( track, joint ) );
    }
}
}
