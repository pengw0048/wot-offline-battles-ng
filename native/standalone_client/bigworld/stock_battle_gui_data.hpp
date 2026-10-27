#pragma once

// Engine-independent state and arithmetic of the #1513 battle crosshair views,
// gun-marker data providers and hit/tank indicators. The BigWorld bindings own
// Python, GFx values, matrix providers and the render context.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

// Every Windows TU binds the D3DX calls the original makes, so the inline
// bodies agree across the client; host builds use the portable forms below.
#if defined(_WIN32) && !defined(WOT_STOCK_BATTLE_GUI_HOST_MATH)
#define WOT_STOCK_BATTLE_GUI_USE_D3DX
#include <d3dx9math.h>
#endif

namespace wot {
namespace stock_battle_gui {

struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

// D3DXMATRIX / BW::Matrix layout: row vectors, translation in row 3.
struct Mat4
{
    float m[4][4];
    static Mat4 zero() { Mat4 r; std::memset( &r, 0, sizeof( r ) ); return r; }
    static Mat4 identity()
    {
        Mat4 r = zero();
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.f;
        return r;
    }
};

// Scaleform Render::Matrix4F layout: M[row][column], translation in column 3.
struct SFMatrix4
{
    float m[4][4];
    static SFMatrix4 zero() { SFMatrix4 r; std::memset( &r, 0, sizeof( r ) ); return r; }
};

const float kRadiansToDegrees = 57.2957763671875f;
const float kMissingPoint = -100000.f;

// --- The D3DX operations the original calls through d3dx9_43.dll ----------

#if defined(WOT_STOCK_BATTLE_GUI_USE_D3DX)
inline const D3DXMATRIX * dx( const Mat4 & m ) { return reinterpret_cast< const D3DXMATRIX * >( &m ); }
inline D3DXMATRIX * dx( Mat4 & m ) { return reinterpret_cast< D3DXMATRIX * >( &m ); }
inline const D3DXVECTOR3 * dx( const Vec3 & v ) { return reinterpret_cast< const D3DXVECTOR3 * >( &v ); }
inline D3DXVECTOR3 * dx( Vec3 & v ) { return reinterpret_cast< D3DXVECTOR3 * >( &v ); }

inline Vec3 normalize( Vec3 v ) { D3DXVec3Normalize( dx( v ), dx( v ) ); return v; }
inline Vec4 transform( const Vec3 & v, const Mat4 & m )
{
    Vec4 r;
    D3DXVec3Transform( reinterpret_cast< D3DXVECTOR4 * >( &r ), dx( v ), dx( m ) );
    return r;
}
inline Vec4 transform( const Vec4 & v, const Mat4 & m )
{
    Vec4 r;
    D3DXVec4Transform( reinterpret_cast< D3DXVECTOR4 * >( &r ),
        reinterpret_cast< const D3DXVECTOR4 * >( &v ), dx( m ) );
    return r;
}
inline Vec3 transformNormal( const Vec3 & v, const Mat4 & m )
{
    Vec3 r;
    D3DXVec3TransformNormal( dx( r ), dx( v ), dx( m ) );
    return r;
}
inline Mat4 lookAtLH( const Vec3 & eye, const Vec3 & at, const Vec3 & up )
{
    Mat4 r = Mat4::zero();
    D3DXMatrixLookAtLH( dx( r ), dx( eye ), dx( at ), dx( up ) );
    return r;
}
inline Mat4 inverse( const Mat4 & m )
{
    Mat4 r = m;
    D3DXMatrixInverse( dx( r ), NULL, dx( m ) );
    return r;
}
inline Mat4 rotationX( float angle ) { Mat4 r = Mat4::zero(); D3DXMatrixRotationX( dx( r ), angle ); return r; }
inline Mat4 rotationZ( float angle ) { Mat4 r = Mat4::zero(); D3DXMatrixRotationZ( dx( r ), angle ); return r; }
#else
inline Vec3 normalize( Vec3 v )
{
    const float length = std::sqrt( v.x * v.x + v.y * v.y + v.z * v.z );
    if (length == 0.f) return Vec3{ 0.f, 0.f, 0.f };
    return Vec3{ v.x / length, v.y / length, v.z / length };
}
inline Vec4 transform( const Vec3 & v, const Mat4 & m )
{
    Vec4 r;
    r.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
    r.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
    r.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
    r.w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
    return r;
}
inline Vec4 transform( const Vec4 & v, const Mat4 & m )
{
    Vec4 r;
    r.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + v.w * m.m[3][0];
    r.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + v.w * m.m[3][1];
    r.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + v.w * m.m[3][2];
    r.w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + v.w * m.m[3][3];
    return r;
}
inline Vec3 transformNormal( const Vec3 & v, const Mat4 & m )
{
    return Vec3{ v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
        v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
        v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] };
}
inline Vec3 cross( const Vec3 & a, const Vec3 & b )
{
    return Vec3{ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline float dot( const Vec3 & a, const Vec3 & b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Mat4 lookAtLH( const Vec3 & eye, const Vec3 & at, const Vec3 & up )
{
    const Vec3 z = normalize( Vec3{ at.x - eye.x, at.y - eye.y, at.z - eye.z } );
    const Vec3 x = normalize( cross( up, z ) );
    const Vec3 y = cross( z, x );
    Mat4 r = Mat4::identity();
    r.m[0][0] = x.x; r.m[1][0] = x.y; r.m[2][0] = x.z;
    r.m[0][1] = y.x; r.m[1][1] = y.y; r.m[2][1] = y.z;
    r.m[0][2] = z.x; r.m[1][2] = z.y; r.m[2][2] = z.z;
    r.m[3][0] = -dot( x, eye ); r.m[3][1] = -dot( y, eye ); r.m[3][2] = -dot( z, eye );
    return r;
}
inline Mat4 inverse( const Mat4 & a )
{
    // Cofactor expansion; D3DX leaves its output unchanged for a singular input.
    const float * s = &a.m[0][0];
    float inv[16];
    inv[0] = s[5]*s[10]*s[15] - s[5]*s[11]*s[14] - s[9]*s[6]*s[15] + s[9]*s[7]*s[14] + s[13]*s[6]*s[11] - s[13]*s[7]*s[10];
    inv[4] = -s[4]*s[10]*s[15] + s[4]*s[11]*s[14] + s[8]*s[6]*s[15] - s[8]*s[7]*s[14] - s[12]*s[6]*s[11] + s[12]*s[7]*s[10];
    inv[8] = s[4]*s[9]*s[15] - s[4]*s[11]*s[13] - s[8]*s[5]*s[15] + s[8]*s[7]*s[13] + s[12]*s[5]*s[11] - s[12]*s[7]*s[9];
    inv[12] = -s[4]*s[9]*s[14] + s[4]*s[10]*s[13] + s[8]*s[5]*s[14] - s[8]*s[6]*s[13] - s[12]*s[5]*s[10] + s[12]*s[6]*s[9];
    inv[1] = -s[1]*s[10]*s[15] + s[1]*s[11]*s[14] + s[9]*s[2]*s[15] - s[9]*s[3]*s[14] - s[13]*s[2]*s[11] + s[13]*s[3]*s[10];
    inv[5] = s[0]*s[10]*s[15] - s[0]*s[11]*s[14] - s[8]*s[2]*s[15] + s[8]*s[3]*s[14] + s[12]*s[2]*s[11] - s[12]*s[3]*s[10];
    inv[9] = -s[0]*s[9]*s[15] + s[0]*s[11]*s[13] + s[8]*s[1]*s[15] - s[8]*s[3]*s[13] - s[12]*s[1]*s[11] + s[12]*s[3]*s[9];
    inv[13] = s[0]*s[9]*s[14] - s[0]*s[10]*s[13] - s[8]*s[1]*s[14] + s[8]*s[2]*s[13] + s[12]*s[1]*s[10] - s[12]*s[2]*s[9];
    inv[2] = s[1]*s[6]*s[15] - s[1]*s[7]*s[14] - s[5]*s[2]*s[15] + s[5]*s[3]*s[14] + s[13]*s[2]*s[7] - s[13]*s[3]*s[6];
    inv[6] = -s[0]*s[6]*s[15] + s[0]*s[7]*s[14] + s[4]*s[2]*s[15] - s[4]*s[3]*s[14] - s[12]*s[2]*s[7] + s[12]*s[3]*s[6];
    inv[10] = s[0]*s[5]*s[15] - s[0]*s[7]*s[13] - s[4]*s[1]*s[15] + s[4]*s[3]*s[13] + s[12]*s[1]*s[7] - s[12]*s[3]*s[5];
    inv[14] = -s[0]*s[5]*s[14] + s[0]*s[6]*s[13] + s[4]*s[1]*s[14] - s[4]*s[2]*s[13] - s[12]*s[1]*s[6] + s[12]*s[2]*s[5];
    inv[3] = -s[1]*s[6]*s[11] + s[1]*s[7]*s[10] + s[5]*s[2]*s[11] - s[5]*s[3]*s[10] - s[9]*s[2]*s[7] + s[9]*s[3]*s[6];
    inv[7] = s[0]*s[6]*s[11] - s[0]*s[7]*s[10] - s[4]*s[2]*s[11] + s[4]*s[3]*s[10] + s[8]*s[2]*s[7] - s[8]*s[3]*s[6];
    inv[11] = -s[0]*s[5]*s[11] + s[0]*s[7]*s[9] + s[4]*s[1]*s[11] - s[4]*s[3]*s[9] - s[8]*s[1]*s[7] + s[8]*s[3]*s[5];
    inv[15] = s[0]*s[5]*s[10] - s[0]*s[6]*s[9] - s[4]*s[1]*s[10] + s[4]*s[2]*s[9] + s[8]*s[1]*s[6] - s[8]*s[2]*s[5];
    const float det = s[0] * inv[0] + s[1] * inv[4] + s[2] * inv[8] + s[3] * inv[12];
    if (det == 0.f) return a;
    Mat4 r;
    for (int i = 0; i != 16; ++i) (&r.m[0][0])[i] = inv[i] / det;
    return r;
}
inline Mat4 rotationX( float angle )
{
    Mat4 r = Mat4::identity();
    const float s = std::sin( angle ), c = std::cos( angle );
    r.m[1][1] = c; r.m[1][2] = s; r.m[2][1] = -s; r.m[2][2] = c;
    return r;
}
inline Mat4 rotationZ( float angle )
{
    Mat4 r = Mat4::identity();
    const float s = std::sin( angle ), c = std::cos( angle );
    r.m[0][0] = c; r.m[0][1] = s; r.m[1][0] = -s; r.m[1][1] = c;
    return r;
}
#endif

inline SFMatrix4 multiply( const SFMatrix4 & a, const SFMatrix4 & b )
{
    // Scaleform Matrix4x4::MultiplyMatrix(m1, m2): this = m1 * m2.
    SFMatrix4 r;
    for (int i = 0; i != 4; ++i)
        for (int j = 0; j != 4; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] +
                a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}

// x86 minss: the first operand only when it is smaller, so a NaN yields b.
inline float minss( float a, float b ) { return a < b ? a : b; }

// The original comiss/ja + minss clamp; a NaN value passes through.
inline float clampss( float value, float minimum, float maximum )
{
    return minimum > value ? minimum : minss( maximum, value );
}

// Yaw of a matrix row as the original x87 fpatan(x, z), stored as float.
inline float rowYaw( const Vec3 & row )
{
    const Vec3 n = normalize( row );
    return static_cast< float >( std::atan2( static_cast< double >( n.x ),
        static_cast< double >( n.z ) ) );
}

// --- WGAbstractGunMarkerDataProvider / WGGunMarkerDataProvider -------------

// Shared provider state (+0xc..+0x18) and the default size filter (+0x1c..+0x2c).
struct GunMarkerSize
{
    float minSize = 5.f;        // sizeConstraint, 13eb290
    float maxSize = 300.f;
    bool acquired = false;      // one view per provider
    float current = 0.f;
    float start = 0.f;
    float target = 0.f;
    float elapsed = 0.f;
    // #1513 leaves +0x2c uninitialised until updateSize; zero keeps the start size.
    float rate = 0.f;

    // 13ebdb0: the view's DataProviderHolder acquires an unacquired provider.
    bool acquire()
    {
        if (acquired) return false;
        acquired = true;
        return true;
    }

    // 13eca80.
    void setStartSize( float size ) { current = start = size; }

    // 13ecfb0.
    void updateSize( float size, float relaxTime )
    {
        start = current;
        elapsed = 0.f;
        target = size;
        rate = (-0.0004f < relaxTime && relaxTime < 0.0004f) ? 1.f :
            1.f / std::fabs( relaxTime );
    }

    // 13ec360: blends the size and returns the radius clip's percent scale.
    float clipScale( float dTime, float clipSize )
    {
        elapsed = dTime + elapsed;
        const float t = minss( rate * elapsed, 1.f );
        current = (1.f - t) * start + target * t;
        const float clamped = clampss( current * 1.72333002090454102f, minSize, maxSize );
        return static_cast< float >( static_cast< double >( clamped ) /
            static_cast< double >( clipSize ) * 100.0 );
    }
};

// --- WGCrosshairFlash ------------------------------------------------------

struct ScreenPosition
{
    double x;
    double y;
    bool visible;
};

// 13df810: clip-space gun-marker position to stage pixels.
inline ScreenPosition crosshairScreenPosition( Vec4 clip, float halfWidth, float halfHeight )
{
    if (0.f < clip.w)
    {
        clip.x = clip.x * (1.f / clip.w);
        clip.y = clip.y * (1.f / clip.w);
    }
    ScreenPosition result;
    result.visible = 0.f < clip.w;
    result.x = static_cast< double >( (clip.x + 1.f) * halfWidth );
    result.y = static_cast< double >( (1.f - clip.y) * halfHeight );
    return result;
}

// 13df3c0 invalidateCrosshair: the radius clip size is min(width, height).
inline float crosshairClipSize( double width, double height )
{
    return static_cast< float >( height <= width ? height : width );
}

// --- WGTankIndicatorFlash ---------------------------------------------------

struct TankIndicatorAngles
{
    double hullRotation;
    double turretRotation;
    float turretYaw;
};

// 13e5e70: Flash rotations from the camera, hull and local turret rows 2.
inline TankIndicatorAngles tankIndicatorAngles( const Vec3 & cameraForward,
    const Vec3 & hullForward, const Vec3 & turretForward )
{
    const float cameraYaw = rowYaw( cameraForward );
    const float hullYaw = rowYaw( hullForward );
    const float hullDegrees = hullYaw * kRadiansToDegrees;
    const float cameraDegrees = cameraYaw * -kRadiansToDegrees;
    TankIndicatorAngles result;
    result.hullRotation = static_cast< double >( hullDegrees + cameraDegrees );
    result.turretYaw = rowYaw( turretForward );
    result.turretRotation = static_cast< double >(
        result.turretYaw * kRadiansToDegrees + hullDegrees + cameraDegrees );
    return result;
}

// 13e5e70: whether the turret is at (within 0.005 rad of) or beyond a limit.
inline bool turretAtConstraint( float yaw, float minimum, float maximum )
{
    if (yaw < minimum) return true;
    const float toMinimum = yaw - minimum;
    if (-0.005f < toMinimum && toMinimum < 0.005f) return true;
    if (maximum < yaw) return true;
    const float toMaximum = yaw - maximum;
    return -0.005f < toMaximum && toMaximum < 0.005f;
}

// Returns +1/-1 when onArrivedConstraint(True/False) is due, 0 otherwise.
inline int updateTurretConstraint( bool & state, float yaw, float minimum, float maximum )
{
    if (minimum * minimum + maximum * maximum == 0.f) return 0;
    if (turretAtConstraint( yaw, minimum, maximum ))
    {
        if (state) return 0;
        state = true;
        return 1;
    }
    if (!state) return 0;
    state = false;
    return -1;
}

// --- WGHitIndicatorFlash ------------------------------------------------------

// offsetRotationElementsInDegree ranges (+0x180..+0x18c).
struct HitLabelRanges
{
    float upperStart = 0.f;
    float upperEnd = 0.f;
    float lowerStart = 0.f;
    float lowerEnd = 0.f;

    // 13e0be0: negative offsets are ignored.
    void setOffsets( float upper, float lower )
    {
        if (0.f <= upper && 0.f <= lower)
        {
            upperStart = upper;
            upperEnd = 360.f - upper;
            lowerStart = 180.f - lower;
            lowerEnd = lower + 180.f;
        }
    }
};

// 13e0ed0: the hit clip faces the hit relative to the camera row 2.
inline float hitRotation( float globalYaw, float cameraYaw )
{
    return (globalYaw + cameraYaw) * kRadiansToDegrees + 180.f;
}

// 13e0ed0: the rotation that keeps the tank name and icon readable.
inline double hitLabelRotation( float totalRotation, const HitLabelRanges & ranges,
    double currentRotation )
{
    float angle = static_cast< float >( std::fmod( static_cast< double >( totalRotation ), 360.0 ) );
    if (angle < 0.f) angle = angle + 360.f;
    const bool facingLeft = !(angle < ranges.upperStart) && !(ranges.lowerStart <= angle);
    const bool facingRight = !(ranges.upperEnd <= angle) && !(angle < ranges.lowerEnd);
    if (facingLeft) return -90.0;
    if (facingRight) return 90.0;
    return currentRotation;
}

inline bool hitLabelNeedsRotation( double current, double target )
{
    return 0.0004 <= std::fabs( current - target );
}

// --- WGSPGGunMarkerDataProvider -----------------------------------------------

// Returns the Ccb_ distance of the nearest accepted hit, negative on a miss.
typedef std::function< float( const Vec3 & start, const Vec3 & end ) > SegmentCollision;

struct TrajectorySegment
{
    Vec3 velocity;
    Vec3 start;
    Vec3 end;
};

struct TrajectoryState
{
    Vec3 position;
    Vec3 velocity;
};

// 13ea7c0: one server tick, subdivided until each chord follows the curve.
inline TrajectoryState trajectoryStep( std::vector< Vec3 > & points, const Vec3 & position,
    const Vec3 & velocity, const Vec3 & gravity, float dt, bool screenRefined,
    const Mat4 & viewProjection )
{
    const Vec3 nextVelocity{ velocity.x + dt * gravity.x, velocity.y + dt * gravity.y,
        velocity.z + dt * gravity.z };
    const Vec3 next{ dt * (velocity.x + nextVelocity.x) * 0.5f + position.x,
        position.y + dt * (velocity.y + nextVelocity.y) * 0.5f,
        position.z + dt * (velocity.z + nextVelocity.z) * 0.5f };
    std::vector< TrajectorySegment > pending;
    pending.push_back( TrajectorySegment{ velocity, position, next } );
    while (!pending.empty())
    {
        const TrajectorySegment segment = pending.back();
        pending.pop_back();
        const float dx = segment.end.x - segment.start.x;
        const float dy = segment.end.y - segment.start.y;
        const float dz = segment.end.z - segment.start.z;
        Vec3 normal{ -(dx * dy), dx * dx - (-dz) * dz, dy * -dz };
        if (std::fabs( normal.y ) < 0.03f)
        {
            points.push_back( segment.end );
            continue;
        }
        normal = normalize( normal );
        const Vec3 & v = segment.velocity;
        const float apex = -((v.y * normal.y + v.x * normal.x + normal.z * v.z) /
            (gravity.y * normal.y));
        const float half = apex * apex * 0.5f;
        const Vec3 offset{ v.x * apex + half * gravity.x, gravity.y * half + v.y * apex,
            gravity.z * half + v.z * apex };
        const float deviation = std::fabs(
            offset.y * normal.y + offset.x * normal.x + normal.z * offset.z );
        float threshold = 10.f;
        if (screenRefined)
        {
            // Either endpoint inside the NDC square needs a 0.1 m chord error.
            // #1513 swaps D3DXVec4Transform's pOut/pV here (013eac13, 013eacc2)
            // and so tests a stale stack vector; this evaluates the endpoints.
            const Vec4 a = transform( Vec4{ segment.start.x, segment.start.y, segment.start.z, 1.f },
                viewProjection );
            bool inside = false;
            if (0.f < a.w)
            {
                const float scale = 1.f / a.w;
                const float x = a.x * scale, y = a.y * scale;
                inside = -1.f <= x && x <= 1.f && -1.f <= y && y <= 1.f;
            }
            if (!inside)
            {
                const Vec4 b = transform( Vec4{ segment.end.x, segment.end.y, segment.end.z, 1.f },
                    viewProjection );
                if (0.f < b.w)
                {
                    const float scale = 1.f / b.w;
                    const float y = scale * b.y, x = scale * b.x;
                    inside = -1.f <= x && x <= 1.f && -1.f <= y && y <= 1.f;
                }
            }
            if (inside) threshold = 0.1f;
        }
        if (!(threshold < deviation))
        {
            points.push_back( segment.end );
            continue;
        }
        const Vec3 middle{ offset.x + segment.start.x, segment.start.y + offset.y,
            segment.start.z + offset.z };
        const Vec3 middleVelocity{ gravity.x * apex + v.x, v.y + gravity.y * apex,
            v.z + gravity.z * apex };
        pending.push_back( TrajectorySegment{ middleVelocity, middle, segment.end } );
        pending.push_back( TrajectorySegment{ v, segment.start, middle } );
    }
    return TrajectoryState{ next, nextVelocity };
}

// f5d1c0: a Ccb_ distance along the normalised segment direction.
inline bool collidePoint( const SegmentCollision & collide, const Vec3 & start,
    const Vec3 & end, Vec3 & hit )
{
    const float distance = collide( start, end );
    if (!(0.f <= distance)) return false;
    const Vec3 direction = normalize( Vec3{ end.x - start.x, end.y - start.y, end.z - start.z } );
    hit = Vec3{ start.x + direction.x * distance, start.y + direction.y * distance,
        start.z + direction.z * distance };
    return true;
}

class SPGGunMarkerState
{
public:
    explicit SPGGunMarkerState( std::uint32_t count ) :
        current( count, Vec3{ 0.f, 0.f, 0.f } ), previous( count, Vec3{ 0.f, 0.f, 0.f } ),
        output( count, Vec3{ 0.f, 0.f, 0.f } ), count_( count ) {}

    std::vector< Vec3 > current;    // +0x40, the traced cone
    std::vector< Vec3 > previous;   // +0x4c, the blend origin
    std::vector< Vec3 > output;     // +0x58, the displayed points

    float elapsed = 0.f;            // +0x20
    float relaxRate = 0.1f;         // +0x24, exposed read-only as relaxTime
    float maxTime = 5.f;            // +0x28
    float serverTickLength = 0.1f;  // +0x2c
    float sizeScaleRate = 10.f;     // +0x30
    float size = -1.f;              // +0x34, -1 after reset
    float previousSize = -1.f;      // +0x38
    float outputSize = -1.f;        // +0x3c
    float dispersionAngle = 0.f;    // +0xb8
    bool smoothFiltering = false;   // +0xbc
    float filterAlpha = 1.f;        // +0xc0
    float filterBeta = 0.f;         // +0xc4
    float filteredAngle = 0.f;      // +0xc8
    float filteredRate = 0.f;       // +0xcc

    std::uint32_t count() const { return count_; }

    // 13eb820.
    void setRelaxTime( float relaxTime )
    {
        relaxRate = (relaxTime <= -0.0004f || 0.0004f <= relaxTime) ?
            1.f / std::fabs( relaxTime ) : 1.f;
    }

    // 13eb7c0.
    void reset() { size = -1.f; }

    // 13eb970 clears the flat-radial callback, which this port never installs.
    void setupConicDispersion( float angle ) { dispersionAngle = angle; }

    // 13ecaa0.
    void update( const Vec3 & position, const Vec3 & velocity, const Vec3 & gravity,
        float newSize, const SegmentCollision & collide, const Mat4 & viewProjection )
    {
        const bool snap = size == -1.f;
        if (snap)
        {
            if (smoothFiltering)
            {
                filterAlpha = 0.4f;
                filterBeta = 0.001f;
                filteredAngle = dispersionAngle;
                filteredRate = 0.f;
            }
        }
        else
        {
            previous = output;
            const float t = relaxRate * elapsed;
            if (smoothFiltering && 0.f < t)
            {
                float predicted = t * filteredRate + filteredAngle;
                const float residual = dispersionAngle - predicted;
                predicted = residual * filterAlpha + predicted;
                filteredRate = (filterBeta / (t + 1.1920928955078125e-07f)) * residual + filteredRate;
                filteredAngle = predicted;
            }
        }
        elapsed = 0.f;
        previousSize = snap ? newSize : size;
        size = newSize;
        conicPoints( position, velocity, gravity,
            smoothFiltering ? filteredAngle : dispersionAngle, collide, viewProjection );
        if (snap) previous = current;
    }

    // 13ecda0: the per-frame blend; false while no update follows a reset.
    bool advance( float dTime, float minSize, float maxSize )
    {
        elapsed = elapsed + dTime;
        const float blend = minss( 2.f, relaxRate * elapsed );
        if (size == -1.f) return false;
        const float sizeBlend = minss( blend, 1.f );
        outputSize = clampss( ((1.f - sizeBlend) * previousSize + size * sizeBlend) * sizeScaleRate,
            minSize, maxSize );
        for (std::uint32_t i = 0; i != count_; ++i)
        {
            const Vec3 & c = current[i];
            const Vec3 & p = previous[i];
            if (!(1.f < blend))
            {
                const float rest = 1.f - blend;
                output[i] = Vec3{ p.x * rest + c.x * blend, c.y * blend + p.y * rest,
                    c.z * blend + p.z * rest };
            }
            else
            {
                const float extra = blend - 1.f;
                output[i] = Vec3{ c.x + (c.x - p.x) * extra, c.y + extra * (c.y - p.y),
                    c.z + extra * (c.z - p.z) };
            }
        }
        return true;
    }

private:
    // 13ec050: the centre and a cone of trajectories around the shot velocity.
    void conicPoints( const Vec3 & position, const Vec3 & velocity, const Vec3 & gravity,
        float angle, const SegmentCollision & collide, const Mat4 & viewProjection )
    {
        const float speed = std::sqrt( velocity.x * velocity.x + velocity.y * velocity.y +
            velocity.z * velocity.z );
        const Mat4 frame = inverse( lookAtLH( Vec3{ 0.f, 0.f, 0.f }, velocity, Vec3{ 0.f, 1.f, 0.f } ) );
        simulate( 0, position, velocity, gravity, false, collide, viewProjection );
        const Mat4 tilt = rotationX( -angle );
        const Vec3 edge{ tilt.m[2][0], tilt.m[2][1], tilt.m[2][2] };
        const float step = 6.28318548202514648f / static_cast< float >( count_ - 1 );
        for (std::uint32_t i = 1; i < count_; ++i)
        {
            const Mat4 spin = rotationZ( static_cast< float >( i ) * step + 1.57079637050628662f );
            const Vec3 local = transformNormal( edge, spin );
            const Vec3 world = transformNormal( local, frame );
            const Vec3 shot{ world.x * speed, world.y * speed, world.z * speed };
            simulate( i, position, shot, gravity, i < (count_ >> 1), collide, viewProjection );
        }
    }

    // 13ebdc0: the first scene hit within maxTime, else the missing sentinel.
    void simulate( std::uint32_t index, const Vec3 & position, const Vec3 & velocity,
        const Vec3 & gravity, bool screenRefined, const SegmentCollision & collide,
        const Mat4 & viewProjection )
    {
        Vec3 point = position;
        Vec3 pointVelocity = velocity;
        Vec3 hit{ 0.f, 0.f, 0.f };
        bool found = false;
        float t = 0.f;
        std::vector< Vec3 > & points = segments_;
        if (0.f < maxTime)
        {
            do
            {
                points.clear();
                trajectoryStep( points, point, pointVelocity, gravity, serverTickLength,
                    screenRefined, viewProjection );
                Vec3 previous = point;
                for (const Vec3 & next : points)
                {
                    if (collidePoint( collide, previous, next, hit ))
                    {
                        found = true;
                        break;
                    }
                    previous = next;
                }
                if (found) break;
                t = t + serverTickLength;
                const float half = t * t * 0.5f;
                point = Vec3{ gravity.x * half + (position.x + velocity.x * t),
                    gravity.y * half + (position.y + velocity.y * t),
                    gravity.z * half + (position.z + velocity.z * t) };
                pointVelocity = Vec3{ velocity.x + gravity.x * t, velocity.y + gravity.y * t,
                    velocity.z + gravity.z * t };
            }
            while (t < maxTime);
        }
        current[index] = found ? hit : Vec3{ kMissingPoint, kMissingPoint, kMissingPoint };
    }

    std::uint32_t count_;
    std::vector< Vec3 > segments_;  // +0x64
};

// --- WGSPGCrosshairFlash ----------------------------------------------------------

struct SPGElementView
{
    SFMatrix4 projection;   // DisplayInfo ProjectionMatrix3D
    float scale;            // Matrix3D diagonal x/y
};

struct SPGViewFrame
{
    Mat4 viewProjection;
    Mat4 view;
    float halfWidth;
    float halfHeight;
    float screenWidth;
    float screenHeight;
    float nearPlane;
    float farPlane;
};

// 13e7110: one SPG crosshair element's 3D projection.
inline SPGElementView spgElementView( const Vec3 & point, const SPGViewFrame & frame, float scale )
{
    float depth = kMissingPoint, x = kMissingPoint, y = kMissingPoint;
    if (point.x != kMissingPoint || point.y != kMissingPoint || point.z != kMissingPoint)
    {
        const Vec4 clip = transform( point, frame.viewProjection );
        if (0.f < clip.w)
        {
            x = clip.x * (1.f / clip.w);
            y = clip.y * (1.f / clip.w);
            depth = transform( point, frame.view ).z;
        }
    }
    SFMatrix4 depthScale = SFMatrix4::zero();
    depthScale.m[0][0] = depth;
    depthScale.m[1][1] = depth;
    depthScale.m[2][2] = 0.f;
    depthScale.m[3][3] = depth;
    SFMatrix4 projection = SFMatrix4::zero();
    projection.m[3][3] = 1.f;
    projection.m[0][0] = 0.05f / frame.screenWidth + 0.05f / frame.screenWidth;
    projection.m[1][1] = -0.05f / frame.screenHeight + -0.05f / frame.screenHeight;
    const float range = frame.farPlane - frame.nearPlane;
    projection.m[2][2] = 1.f / range;
    projection.m[2][3] = (-1.f / range) * frame.nearPlane;
    const float stageX = static_cast< float >( static_cast< double >( frame.halfWidth ) *
        static_cast< double >( x ) * 20.0 );
    const float stageY = static_cast< float >( static_cast< double >( frame.halfHeight ) *
        static_cast< double >( y ) * -20.0 );
    SFMatrix4 translation = SFMatrix4::zero();
    translation.m[0][0] = translation.m[1][1] = translation.m[2][2] = translation.m[3][3] = 1.f;
    translation.m[0][3] = stageX;
    translation.m[1][3] = stageY;
    translation.m[2][3] = depth;
    SPGElementView result;
    result.projection = multiply( multiply( projection, translation ), depthScale );
    result.scale = scale;
    return result;
}

// 13e7110: the element scale from setPointsBaseScale and the provider size.
inline float spgElementScale( float pointsBaseScale, float outputSize )
{
    return pointsBaseScale * outputSize * 0.00999999977648258209f;
}

} // namespace stock_battle_gui
} // namespace wot
