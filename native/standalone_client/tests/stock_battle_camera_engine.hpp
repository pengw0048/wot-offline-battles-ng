// Host boundary for the production battle camera collider and updates.
// Every primitive matches a definition in stock_battle_camera_oracle.py.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
typedef unsigned int uint;
typedef uint8_t uint8;
typedef uint16_t uint16;
namespace BW { template <class T> using vector = std::vector<T>; }

namespace oracle
{
inline float sinf_( float v ) { return float( std::sin( double( v ) ) ); }
inline float cosf_( float v ) { return float( std::cos( double( v ) ) ); }
inline float tanf_( float v ) { return float( std::tan( double( v ) ) ); }
inline float atanf_( float v ) { return float( std::atan( double( v ) ) ); }
inline float powf_( float b, float e ) { return float( std::pow( double( b ), double( e ) ) ); }
inline float sqrtf_( float v ) { return std::sqrt( v ); }
}
#define sinf oracle::sinf_
#define cosf oracle::cosf_
#define tanf oracle::tanf_
#define atanf oracle::atanf_
#define powf oracle::powf_

struct Vector2
{
	float x, y;
	Vector2() : x( 0.f ), y( 0.f ) {}
	Vector2( float a, float b ) : x( a ), y( b ) {}
};

struct Vector3
{
	float x, y, z;
	Vector3() : x( 0.f ), y( 0.f ), z( 0.f ) {}
	Vector3( float a, float b, float c ) : x( a ), y( b ), z( c ) {}
	float & operator[]( int i ) { return i == 0 ? x : (i == 1 ? y : z); }
	float operator[]( int i ) const { return i == 0 ? x : (i == 1 ? y : z); }
};

struct Vector4
{
	float x, y, z, w;
	Vector4() : x( 0.f ), y( 0.f ), z( 0.f ), w( 0.f ) {}
	Vector4( float a, float b, float c, float d ) : x( a ), y( b ), z( c ), w( d ) {}
};
typedef Vector4 D3DXVECTOR4;

struct Matrix
{
	float m[4][4];
	static const Matrix identity;
	Matrix() { std::memset( m, 0, sizeof( m ) ); }
	void setZero() { std::memset( m, 0, sizeof( m ) ); }
	Vector3 applyToOrigin() const { return Vector3( m[3][0], m[3][1], m[3][2] ); }
	Vector3 applyToUnitAxisVector( int i ) const { return Vector3( m[i][0], m[i][1], m[i][2] ); }
};
const Matrix Matrix::identity = [] { Matrix r; for (int i = 0; i < 4; ++i) r.m[i][i] = 1.f; return r; }();

// D3DX entry points with the shared float32 definitions.
inline void D3DXVec3Normalize( Vector3 * out, const Vector3 * v )
{
	const float length = std::sqrt( (v->x * v->x + v->y * v->y) + v->z * v->z );
	if (length == 0.f) { *out = Vector3( 0.f, 0.f, 0.f ); return; }
	*out = Vector3( v->x / length, v->y / length, v->z / length );
}
inline void D3DXVec3Transform( Vector4 * out, const Vector3 * v, const Matrix * m )
{
	float r[4];
	for (int c = 0; c < 4; ++c)
		r[c] = ((v->x * m->m[0][c] + v->y * m->m[1][c]) + v->z * m->m[2][c]) + m->m[3][c];
	*out = Vector4( r[0], r[1], r[2], r[3] );
}
inline void D3DXVec3TransformNormal( Vector3 * out, const Vector3 * v, const Matrix * m )
{
	float r[3];
	for (int c = 0; c < 3; ++c)
		r[c] = (v->x * m->m[0][c] + v->y * m->m[1][c]) + v->z * m->m[2][c];
	*out = Vector3( r[0], r[1], r[2] );
}
inline Matrix * D3DXMatrixInverse( Matrix * out, float *, const Matrix * s )
{
	const float (&a)[4][4] = s->m;
	const float det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
		a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])) +
		a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
	if (det == 0.f) return NULL;
	const float rcp = 1.f / det;
	Matrix r;
	r.m[0][0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
	r.m[0][1] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
	r.m[0][2] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
	r.m[1][0] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
	r.m[1][1] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
	r.m[1][2] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
	r.m[2][0] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
	r.m[2][1] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
	r.m[2][2] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
	for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = r.m[i][j] * rcp;
	for (int j = 0; j < 3; ++j)
		r.m[3][j] = -((a[3][0] * r.m[0][j] + a[3][1] * r.m[1][j]) + a[3][2] * r.m[2][j]);
	r.m[3][3] = 1.f;
	*out = r;
	return out;
}
inline Matrix * D3DXMatrixMultiply( Matrix * out, const Matrix * a, const Matrix * b )
{
	Matrix r;
	for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
		r.m[i][j] = ((a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j]) +
			a->m[i][2] * b->m[2][j]) + a->m[i][3] * b->m[3][j];
	*out = r;
	return out;
}
inline Matrix * D3DXMatrixTranslation( Matrix * out, float x, float y, float z )
{
	*out = Matrix::identity;
	out->m[3][0] = x; out->m[3][1] = y; out->m[3][2] = z;
	return out;
}
inline Matrix * D3DXMatrixRotationX( Matrix * out, float angle )
{
	const float s = sinf( angle ), c = cosf( angle );
	*out = Matrix::identity;
	out->m[1][1] = c; out->m[1][2] = s; out->m[2][1] = -s; out->m[2][2] = c;
	return out;
}
inline Matrix * D3DXMatrixRotationY( Matrix * out, float angle )
{
	const float s = sinf( angle ), c = cosf( angle );
	*out = Matrix::identity;
	out->m[0][0] = c; out->m[0][2] = -s; out->m[2][0] = s; out->m[2][2] = c;
	return out;
}
inline Vector3 hostCross( const Vector3 & a, const Vector3 & b )
{
	return Vector3( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x );
}
inline float hostDot( const Vector3 & a, const Vector3 & b ) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }
inline Matrix * D3DXMatrixLookAtLH( Matrix * out, const Vector3 * eye, const Vector3 * at,
	const Vector3 * up )
{
	Vector3 z( at->x - eye->x, at->y - eye->y, at->z - eye->z );
	D3DXVec3Normalize( &z, &z );
	Vector3 x = hostCross( *up, z );
	D3DXVec3Normalize( &x, &x );
	const Vector3 y = hostCross( z, x );
	const float rows[4][4] = { { x.x, y.x, z.x, 0.f }, { x.y, y.y, z.y, 0.f }, { x.z, y.z, z.z, 0.f },
		{ -hostDot( x, *eye ), -hostDot( y, *eye ), -hostDot( z, *eye ), 1.f } };
	std::memcpy( out->m, rows, sizeof( rows ) );
	return out;
}

struct PlaneEq
{
	Vector3 normal_;
	float d_;
	PlaneEq( const Vector3 & n, float d ) : normal_( n ), d_( d ) {}
	const Vector3 & normal() const { return normal_; }
	float d() const { return d_; }
};

struct AABB
{
	Vector3 min_, max_;
	AABB() : min_( FLT_MAX, FLT_MAX, FLT_MAX ), max_( -FLT_MAX, -FLT_MAX, -FLT_MAX ) {}
	AABB( const Vector3 & a, const Vector3 & b ) : min_( a ), max_( b ) {}
	const Vector3 & minBounds() const { return min_; }
	const Vector3 & maxBounds() const { return max_; }
	// lib/math/boundbox.cpp AABB::transformBy, with the host row arithmetic.
	void transformBy( const Matrix & t )
	{
		const Vector3 size( max_.x - min_.x, max_.y - min_.y, max_.z - min_.z );
		Vector4 p;
		D3DXVec3Transform( &p, &min_, &t );
		min_ = Vector3( p.x, p.y, p.z );
		max_ = min_;
		for (int axis = 0; axis < 3; ++axis)
		{
			const Vector3 a = t.applyToUnitAxisVector( axis );
			const Vector3 v( size[axis] * a.x, size[axis] * a.y, size[axis] * a.z );
			for (int c = 0; c < 3; ++c)
			{
				if (v[c] > 0) max_[c] = max_[c] + v[c];
				else min_[c] = min_[c] + v[c];
			}
		}
	}
};
typedef AABB BoundingBox;

struct WorldTriangle
{
	Vector3 v_[3];
	uint16_t flags_;
	WorldTriangle() : flags_( 0 ) {}
	WorldTriangle( const Vector3 & a, const Vector3 & b, const Vector3 & c, uint16_t flags = 0 ) :
		flags_( flags ) { v_[0] = a; v_[1] = b; v_[2] = c; }
	const Vector3 & v0() const { return v_[0]; }
	const Vector3 & v1() const { return v_[1]; }
	const Vector3 & v2() const { return v_[2]; }
	uint16_t flags() const { return flags_; }
	unsigned materialKind() const { return flags_ >> 8; }
	// #1513 01172190, the engine's WorldTriangle::intersects(start, dir, length).
	bool intersects( const Vector3 & start, const Vector3 & d, float & length ) const
	{
		const float e2x = v_[2].x - v_[0].x, e2y = v_[2].y - v_[0].y, e2z = v_[2].z - v_[0].z;
		const float px = d.y * e2z - d.z * e2y;
		const float py = d.z * e2x - d.x * e2z;
		const float pz = d.x * e2y - d.y * e2x;
		const float e1x = v_[1].x - v_[0].x, e1y = v_[1].y - v_[0].y, e1z = v_[1].z - v_[0].z;
		const float det = (e1x * px + e1y * py) + e1z * pz;
		if (det > -1e-6f && 1e-6f > det) return false;
		const float inv = 1.f / det;
		const float tx = start.x - v_[0].x, ty = start.y - v_[0].y, tz = start.z - v_[0].z;
		const float u = ((py * ty + tx * px) + pz * tz) * inv;
		if (0.f > u || u > 1.f) return false;
		const float qx = e1z * ty - e1y * tz;
		const float qy = e1x * tz - e1z * tx;
		const float qz = e1y * tx - e1x * ty;
		const float v = ((d.x * qx + d.y * qy) + d.z * qz) * inv;
		if (0.f > v || v + u > 1.f) return false;
		const float distance = ((e2x * qx + e2y * qy) + qz * e2z) * inv;
		if (!(distance > 0.f) || !(length > distance)) return false;
		length = distance;
		return true;
	}
};

const int COLLIDE_STOP = 0;
const int COLLIDE_BEFORE = 1;
const int COLLIDE_AFTER = 2;
const int COLLIDE_ALL = 3;

struct SceneObject { int type; long id; };
struct CollisionObstacle
{
	Matrix transform_;
	SceneObject sceneObject_;
	const Matrix & transform() const { return transform_; }
	const SceneObject & sceneObject() const { return sceneObject_; }
};
class CollisionCallback
{
public:
	virtual ~CollisionCallback() {}
	int visit( const CollisionObstacle & o, const WorldTriangle & t, float d ) { return (*this)( o, t, d ); }
protected:
	virtual int operator()( const CollisionObstacle &, const WorldTriangle &, float ) { return COLLIDE_STOP; }
};

// The scripted engine world: what a sweep or segment query reports.
struct HostHit { CollisionObstacle obstacle; WorldTriangle triangle; float distance; };
struct HostWorld
{
	std::vector< HostHit > sweep;
	std::vector< HostHit > rear;
	std::vector< HostHit > segment;
	std::map< long, AABB > bounds;
	AABB spaceBounds;
	bool hasSpace = true;
	std::vector< float > waters;
	float dTime = 0.f;
	bool sceneCheck = false;
	Vector3 sceneMove;
	int sweepCalls = 0, segmentCalls = 0;
};
inline HostWorld & world() { static HostWorld value; return value; }

struct SpatialQueryOperation
{
	void getWorldVisibilityBoundingBox( const SceneObject & object, AABB * bb )
	{ *bb = world().bounds[object.id]; }
};
struct Scene
{
	SpatialQueryOperation spatial;
	template < class T > T * getObjectOperation() { return &spatial; }
};
struct ClientSpace
{
	Scene scene_;
	Scene & scene() { return scene_; }
	AABB bounds() const { return world().spaceBounds; }
	float collide( const WorldTriangle &, const Vector3 &, CollisionCallback & cc ) const
	{
		// The front sweep makes calls 1 and 2 of a collide; the rear sweep follows.
		const std::vector< HostHit > & hits = ++world().sweepCalls <= 2 ? world().sweep : world().rear;
		for (const HostHit & hit : hits) cc.visit( hit.obstacle, hit.triangle, hit.distance );
		return -1.f;
	}
	float collide( const Vector3 &, const Vector3 &, CollisionCallback & cc ) const
	{
		++world().segmentCalls;
		float result = -1.f;
		for (const HostHit & hit : world().segment)
			if (cc.visit( hit.obstacle, hit.triangle, hit.distance ) != COLLIDE_ALL) result = hit.distance;
		return result;
	}
};
struct ClientSpacePtr
{
	ClientSpace * p;
	ClientSpace * operator->() const { return p; }
	bool operator!() const { return p == nullptr; }
};
namespace DeprecatedSpaceHelpers
{
inline ClientSpacePtr cameraSpace()
{
	static ClientSpace space;
	return ClientSpacePtr{ world().hasSpace ? &space : nullptr };
}
}

namespace Moo
{
struct Camera
{
	float nearPlane_, farPlane_, fov_, aspectRatio_;
	float nearPlane() const { return nearPlane_; }
	float fov() const { return fov_; }
	float aspectRatio() const { return aspectRatio_; }
	// lib/moo/camera.cpp
	Vector3 nearPlanePoint( float xClip, float yClip ) const
	{
		const float yLength = nearPlane_ * tanf( fov_ * 0.5f );
		const float xLength = yLength * aspectRatio_;
		return Vector3( xLength * xClip, yLength * yClip, nearPlane_ );
	}
};
struct RenderContext
{
	Camera camera_;
	const Camera & camera() const { return camera_; }
};
inline RenderContext & rc() { static RenderContext value; return value; }
}

struct Water
{
	float depth;
	float stockGroundRayDistance( const Vector3 &, const Vector3 & ) const { return depth; }
};
struct Waters
{
	std::vector< Water > items;
	static Waters & instance()
	{
		static Waters value;
		value.items.clear();
		for (float d : world().waters) value.items.push_back( Water{ d } );
		return value;
	}
	uint size() const { return uint( items.size() ); }
	const Water * operator[]( uint i ) const { return &items[i]; }
};

struct App
{
	static App & instance() { static App value; return value; }
	float getFrameGameDelta() const { return world().dTime; }
};

struct BaseCamera
{
	static bool sceneCheck( Vector3 & position, const Vector3 &, const Vector3 &, const Vector3 & )
	{
		if (!world().sceneCheck) return false;
		position = Vector3( position.x + world().sceneMove.x, position.y + world().sceneMove.y,
			position.z + world().sceneMove.z );
		return true;
	}
};

// Dynamic colliders: providers, a geometry link and a scripted BSP.
struct MatrixProvider
{
	Matrix value;
	virtual ~MatrixProvider() {}
	virtual void matrix( Matrix & m ) const { m = value; }
};
typedef std::shared_ptr< MatrixProvider > MatrixProviderPtr;

class CollisionVisitor
{
public:
	virtual ~CollisionVisitor() {}
	virtual bool visit( const WorldTriangle & hitTriangle, float dist ) = 0;
};
struct BSPTree
{
	std::vector< WorldTriangle > hits;
	bool intersects( const WorldTriangle &, const Vector3 &, CollisionVisitor * visitor ) const
	{
		for (const WorldTriangle & t : hits) visitor->visit( t, 0.f );
		return !hits.empty();
	}
};
struct Model { BSPTree tree; const BSPTree * decompose() const { return &tree; } };
typedef std::shared_ptr< Model > ModelPtr;
struct SuperModel
{
	ModelPtr model;
	int reads = 0;
	int nModels() const { return model ? 1 : 0; }
	ModelPtr topModel( int ) const { return model; }
	void beginRead() { ++reads; }
	void endRead() { --reads; }
};
struct PyModel
{
	SuperModel superModel;
	SuperModel * pSuperModel() { return &superModel; }
};
typedef std::shared_ptr< PyModel > PyModelPtr;
class StockVehicleStickerGeometry
{
public:
	PyModelPtr model;
	bool available = true;
	virtual ~StockVehicleStickerGeometry() {}
	virtual bool acquire( PyModelPtr & m, Matrix & ) { m = model; return available; }
};
