// C ABI over the production battle-GUI core for the ctypes tests.
#include "stock_battle_gui_data.hpp"

#include <cstring>

namespace gui = wot::stock_battle_gui;

namespace
{
gui::Vec3 vec3( const float * v ) { return gui::Vec3{ v[0], v[1], v[2] }; }
gui::Mat4 mat4( const float * m ) { gui::Mat4 r; std::memcpy( &r, m, sizeof( r ) ); return r; }
typedef float (*Collide)( const float * start, const float * end );
gui::SegmentCollision collision( Collide collide )
{
    return [collide]( const gui::Vec3 & start, const gui::Vec3 & end )
    {
        return collide( &start.x, &end.x );
    };
}
}

extern "C" {

void * gm_new() { return new gui::GunMarkerSize(); }
void gm_free( void * p ) { delete static_cast< gui::GunMarkerSize * >( p ); }
void gm_set( void * p, const float * fields )
{
    gui::GunMarkerSize & s = *static_cast< gui::GunMarkerSize * >( p );
    s.minSize = fields[0]; s.maxSize = fields[1]; s.current = fields[2]; s.start = fields[3];
    s.target = fields[4]; s.elapsed = fields[5]; s.rate = fields[6];
}
void gm_get( void * p, float * fields )
{
    const gui::GunMarkerSize & s = *static_cast< gui::GunMarkerSize * >( p );
    const float values[] = { s.minSize, s.maxSize, s.current, s.start, s.target, s.elapsed, s.rate };
    std::memcpy( fields, values, sizeof( values ) );
}
int gm_acquire( void * p ) { return static_cast< gui::GunMarkerSize * >( p )->acquire(); }
void gm_release( void * p ) { static_cast< gui::GunMarkerSize * >( p )->acquired = false; }
void gm_set_start( void * p, float size ) { static_cast< gui::GunMarkerSize * >( p )->setStartSize( size ); }
void gm_update( void * p, float size, float relax )
{
    static_cast< gui::GunMarkerSize * >( p )->updateSize( size, relax );
}
float gm_clip_scale( void * p, float dt, float clip )
{
    return static_cast< gui::GunMarkerSize * >( p )->clipScale( dt, clip );
}

int crosshair_screen( const float * clip, float halfWidth, float halfHeight, double * out )
{
    const gui::ScreenPosition s = gui::crosshairScreenPosition(
        gui::Vec4{ clip[0], clip[1], clip[2], clip[3] }, halfWidth, halfHeight );
    out[0] = s.x;
    out[1] = s.y;
    return s.visible;
}
float crosshair_clip_size( double width, double height ) { return gui::crosshairClipSize( width, height ); }

float tank_angles( const float * camera, const float * hull, const float * turret, double * out )
{
    const gui::TankIndicatorAngles a = gui::tankIndicatorAngles( vec3( camera ), vec3( hull ),
        vec3( turret ) );
    out[0] = a.hullRotation;
    out[1] = a.turretRotation;
    return a.turretYaw;
}
int tank_constraint( int * state, float yaw, float minimum, float maximum )
{
    bool value = *state != 0;
    const int edge = gui::updateTurretConstraint( value, yaw, minimum, maximum );
    *state = value;
    return edge;
}

float row_yaw( const float * row ) { return gui::rowYaw( vec3( row ) ); }
float hit_rotation( float globalYaw, float cameraYaw ) { return gui::hitRotation( globalYaw, cameraYaw ); }
void hit_offsets( float * ranges, float upper, float lower )
{
    gui::HitLabelRanges r{ ranges[0], ranges[1], ranges[2], ranges[3] };
    r.setOffsets( upper, lower );
    ranges[0] = r.upperStart; ranges[1] = r.upperEnd; ranges[2] = r.lowerStart; ranges[3] = r.lowerEnd;
}
double hit_label( float total, const float * ranges, double current )
{
    const gui::HitLabelRanges r{ ranges[0], ranges[1], ranges[2], ranges[3] };
    return gui::hitLabelRotation( total, r, current );
}
int hit_label_needs( double current, double target ) { return gui::hitLabelNeedsRotation( current, target ); }

int trajectory_step( const float * position, const float * velocity, const float * gravity,
    float dt, int refined, const float * viewProjection, float * points, int capacity,
    float * state )
{
    std::vector< gui::Vec3 > out;
    const gui::TrajectoryState next = gui::trajectoryStep( out, vec3( position ), vec3( velocity ),
        vec3( gravity ), dt, refined != 0, mat4( viewProjection ) );
    const int count = static_cast< int >( out.size() );
    for (int i = 0; i < count && i < capacity; ++i)
        std::memcpy( points + 3 * i, &out[i], sizeof( gui::Vec3 ) );
    std::memcpy( state, &next.position, sizeof( gui::Vec3 ) );
    std::memcpy( state + 3, &next.velocity, sizeof( gui::Vec3 ) );
    return count;
}

void * spg_new( unsigned count ) { return new gui::SPGGunMarkerState( count ); }
void spg_free( void * p ) { delete static_cast< gui::SPGGunMarkerState * >( p ); }
// elapsed relaxRate maxTime tick scaleRate size previousSize outputSize angle smooth alpha beta filteredAngle filteredRate
void spg_get( void * p, float * f )
{
    const gui::SPGGunMarkerState & s = *static_cast< gui::SPGGunMarkerState * >( p );
    const float values[] = { s.elapsed, s.relaxRate, s.maxTime, s.serverTickLength, s.sizeScaleRate,
        s.size, s.previousSize, s.outputSize, s.dispersionAngle, s.smoothFiltering ? 1.f : 0.f,
        s.filterAlpha, s.filterBeta, s.filteredAngle, s.filteredRate };
    std::memcpy( f, values, sizeof( values ) );
}
void spg_set( void * p, const float * f )
{
    gui::SPGGunMarkerState & s = *static_cast< gui::SPGGunMarkerState * >( p );
    s.elapsed = f[0]; s.relaxRate = f[1]; s.maxTime = f[2]; s.serverTickLength = f[3];
    s.sizeScaleRate = f[4]; s.size = f[5]; s.previousSize = f[6]; s.outputSize = f[7];
    s.dispersionAngle = f[8]; s.smoothFiltering = f[9] != 0.f; s.filterAlpha = f[10];
    s.filterBeta = f[11]; s.filteredAngle = f[12]; s.filteredRate = f[13];
}
void spg_points( void * p, int which, float * out )
{
    const gui::SPGGunMarkerState & s = *static_cast< gui::SPGGunMarkerState * >( p );
    const std::vector< gui::Vec3 > & v = which == 0 ? s.current : which == 1 ? s.previous : s.output;
    std::memcpy( out, v.data(), v.size() * sizeof( gui::Vec3 ) );
}
void spg_set_points( void * p, int which, const float * in )
{
    gui::SPGGunMarkerState & s = *static_cast< gui::SPGGunMarkerState * >( p );
    std::vector< gui::Vec3 > & v = which == 0 ? s.current : which == 1 ? s.previous : s.output;
    std::memcpy( v.data(), in, v.size() * sizeof( gui::Vec3 ) );
}
void spg_relax( void * p, float t ) { static_cast< gui::SPGGunMarkerState * >( p )->setRelaxTime( t ); }
void spg_reset( void * p ) { static_cast< gui::SPGGunMarkerState * >( p )->reset(); }
void spg_conic( void * p, float a ) { static_cast< gui::SPGGunMarkerState * >( p )->setupConicDispersion( a ); }
void spg_update( void * p, const float * position, const float * velocity, const float * gravity,
    float size, Collide collide, const float * viewProjection )
{
    static_cast< gui::SPGGunMarkerState * >( p )->update( vec3( position ), vec3( velocity ),
        vec3( gravity ), size, collision( collide ), mat4( viewProjection ) );
}
int spg_advance( void * p, float dt, float minimum, float maximum )
{
    return static_cast< gui::SPGGunMarkerState * >( p )->advance( dt, minimum, maximum );
}

// frame: viewProjection[16] view[16] halfWidth halfHeight screenWidth screenHeight near far
void spg_element( const float * point, const float * frame, float scale, float * out )
{
    gui::SPGViewFrame f;
    f.viewProjection = mat4( frame );
    f.view = mat4( frame + 16 );
    f.halfWidth = frame[32]; f.halfHeight = frame[33]; f.screenWidth = frame[34];
    f.screenHeight = frame[35]; f.nearPlane = frame[36]; f.farPlane = frame[37];
    const gui::SPGElementView e = gui::spgElementView( vec3( point ), f, scale );
    std::memcpy( out, e.projection.m, sizeof( e.projection.m ) );
    out[16] = e.scale;
}
float spg_element_scale( float base, float size ) { return gui::spgElementScale( base, size ); }

}

// The portable D3DX operations, shared with the original-instruction oracle.
extern "C" {
void d3dx_lookat( float * out, const float * eye, const float * at, const float * up )
{
    const gui::Mat4 m = gui::lookAtLH( vec3( eye ), vec3( at ), vec3( up ) );
    std::memcpy( out, &m, sizeof( m ) );
}
void d3dx_inverse( float * out, const float * in )
{
    const gui::Mat4 m = gui::inverse( mat4( in ) );
    std::memcpy( out, &m, sizeof( m ) );
}
void d3dx_normalize( float * out, const float * in )
{
    const gui::Vec3 v = gui::normalize( vec3( in ) );
    std::memcpy( out, &v, sizeof( v ) );
}
void d3dx_rotation_x( float * out, float angle )
{
    const gui::Mat4 m = gui::rotationX( angle );
    std::memcpy( out, &m, sizeof( m ) );
}
void d3dx_rotation_z( float * out, float angle )
{
    const gui::Mat4 m = gui::rotationZ( angle );
    std::memcpy( out, &m, sizeof( m ) );
}
void d3dx_transform_normal( float * out, const float * v, const float * m )
{
    const gui::Vec3 r = gui::transformNormal( vec3( v ), mat4( m ) );
    std::memcpy( out, &r, sizeof( r ) );
}
void d3dx_vec4_transform( float * out, const float * v, const float * m )
{
    const gui::Vec4 r = gui::transform( gui::Vec4{ v[0], v[1], v[2], v[3] }, mat4( m ) );
    std::memcpy( out, &r, sizeof( r ) );
}
}
