// Host boundary for the production CursorCamera update and model collision.
// Each primitive uses the float32 definition in stock_cursor_camera_oracle.py.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
#define CODE_INLINE
#define MF_FLOAT_EQUAL(value1, value2) \
	(std::abs(value1 - value2) < std::numeric_limits<float>::epsilon())
#define RAND_MAX_ORIGINAL 32767
typedef unsigned int uint;
namespace BW { template <class T> using vector = std::vector<T>; }

namespace oracle
{
inline float sinf_( float v ) { return float( std::sin( double( v ) ) ); }
inline float cosf_( float v ) { return float( std::cos( double( v ) ) ); }
inline float tanf_( float v ) { return float( std::tan( double( v ) ) ); }
inline float asinf_( float v ) { return float( std::asin( double( v ) ) ); }
inline float atan2f_( float y, float x ) { return float( std::atan2( double( y ), double( x ) ) ); }
inline float powf_( float b, float e ) { return float( std::pow( double( b ), double( e ) ) ); }
std::vector<int> randoms;
inline int rand_()
{
	if (randoms.empty()) std::abort();
	const int value = randoms.front();
	randoms.erase( randoms.begin() );
	return value;
}
}
#define sinf oracle::sinf_
#define cosf oracle::cosf_
#define tanf oracle::tanf_
#define asinf oracle::asinf_
#define atan2f oracle::atan2f_
#define powf oracle::powf_
#define rand oracle::rand_
#undef RAND_MAX
#define RAND_MAX RAND_MAX_ORIGINAL

const float MATH_PI = 3.14159265359f;
const float MATH_2PI = 6.28318530718f;
inline float DEG_TO_RAD( float angle ) { return angle * MATH_PI / 180.f; }
namespace Math
{
template <class TYPE> inline TYPE clamp( TYPE minValue, TYPE value, TYPE maxValue )
{ return value < minValue ? minValue : (value > maxValue ? maxValue : value); }
// @MATH_DECAY@
}

// @ANGLE@

struct Vector3
{
	float x, y, z;
	Vector3() : x( 0.f ), y( 0.f ), z( 0.f ) {}
	Vector3( float a, float b, float c ) : x( a ), y( b ), z( c ) {}
	void set( float a, float b, float c ) { x = a; y = b; z = c; }
	Vector3 operator+( const Vector3 & v ) const { return Vector3( x + v.x, y + v.y, z + v.z ); }
	Vector3 operator-( const Vector3 & v ) const { return Vector3( x - v.x, y - v.y, z - v.z ); }
	Vector3 operator*( float s ) const { return Vector3( x * s, y * s, z * s ); }
	Vector3 & operator+=( const Vector3 & v ) { x += v.x; y += v.y; z += v.z; return *this; }
	Vector3 & operator*=( float s ) { x *= s; y *= s; z *= s; return *this; }
	bool operator!=( const Vector3 & v ) const { return x != v.x || y != v.y || z != v.z; }
	float dotProduct( const Vector3 & v ) const { return x * v.x + y * v.y + z * v.z; }
	float lengthSquared() const { return this->dotProduct( *this ); }
	float length() const { return std::sqrt( this->lengthSquared() ); }
	Vector3 crossProduct( const Vector3 & v ) const
	{ return Vector3( y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x ); }
	void normalise()
	{
		const float n = this->length();
		if (n == 0.f) { x = y = z = 0.f; return; }
		x = x / n; y = y / n; z = z / n;
	}
	// @VECTOR_ANGLES@
	// The public double-precision setPitchYaw (lib/math/vector3.ipp).
	void setPitchYaw( float pitchInRadians, float yawInRadians )
	{
		double cosPitch = cosf( pitchInRadians );
		double sinPitch = sinf( -pitchInRadians );
		double cosYaw = cosf( yawInRadians );
		double sinYaw = sinf( yawInRadians );
		x = float( cosPitch * sinYaw );
		y = float( sinPitch );
		z = float( cosPitch * cosYaw );
	}
};
inline Vector3 operator*( float s, const Vector3 & v ) { return Vector3( s * v.x, s * v.y, s * v.z ); }

struct Vector4
{
	float x, y, z, w;
};

struct Matrix
{
	float m[4][4];
	static const Matrix identity;
	Matrix() { std::memset( m, 0, sizeof( m ) ); }
	void setIdentity() { *this = identity; }
	Vector3 row( int i ) const { return Vector3( m[i][0], m[i][1], m[i][2] ); }
	Vector3 applyToOrigin() const { return this->row( 3 ); }
	Vector3 applyToUnitAxisVector( int i ) const { return this->row( i ); }
	Vector3 applyPoint( const Vector3 & v ) const
	{
		float r[3];
		for (int c = 0; c < 3; ++c) r[c] = v.x * m[0][c] + v.y * m[1][c] + v.z * m[2][c] + m[3][c];
		return Vector3( r[0], r[1], r[2] );
	}
	Vector3 applyVector( const Vector3 & v ) const
	{
		float r[3];
		for (int c = 0; c < 3; ++c) r[c] = v.x * m[0][c] + v.y * m[1][c] + v.z * m[2][c];
		return Vector3( r[0], r[1], r[2] );
	}
	bool invert( const Matrix & s )
	{
		const float (&a)[4][4] = s.m;
		const float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
			a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
			a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
		if (det == 0.f) return false;
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
			r.m[3][j] = -(a[3][0] * r.m[0][j] + a[3][1] * r.m[1][j] + a[3][2] * r.m[2][j]);
		r.m[3][3] = 1.f;
		*this = r;
		return true;
	}
	bool invert() { const Matrix copy( *this ); return this->invert( copy ); }
	void multiply( const Matrix & a, const Matrix & b )
	{
		Matrix r;
		for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] +
				a.m[i][3] * b.m[3][j];
		*this = r;
	}
	void postMultiply( const Matrix & b ) { const Matrix a( *this ); this->multiply( a, b ); }
	void setRotate( float yaw, float pitch, float roll )
	{
		const float sy = sinf( yaw ), cy = cosf( yaw ), sp = sinf( pitch ), cp = cosf( pitch );
		const float sr = sinf( roll ), cr = cosf( roll );
		const float rows[4][4] = {
			{ cr * cy + sr * sp * sy, sr * cp, sr * sp * cy - cr * sy, 0.f },
			{ cr * sp * sy - sr * cy, cr * cp, sr * sy + cr * sp * cy, 0.f },
			{ cp * sy, -sp, cp * cy, 0.f }, { 0.f, 0.f, 0.f, 1.f } };
		std::memcpy( m, rows, sizeof( m ) );
	}
	void setRotateZ( float angle )
	{
		const float s = sinf( angle ), c = cosf( angle );
		const float rows[4][4] = { { c, s, 0.f, 0.f }, { -s, c, 0.f, 0.f }, { 0.f, 0.f, 1.f, 0.f },
			{ 0.f, 0.f, 0.f, 1.f } };
		std::memcpy( m, rows, sizeof( m ) );
	}
	void lookAt( const Vector3 & position, const Vector3 & direction, const Vector3 & up )
	{
		const Vector3 at = position + direction;
		Vector3 z = at - position;
		z.normalise();
		Vector3 x = up.crossProduct( z );
		x.normalise();
		const Vector3 y = z.crossProduct( x );
		const float rows[4][4] = { { x.x, y.x, z.x, 0.f }, { x.y, y.y, z.y, 0.f },
			{ x.z, y.z, z.z, 0.f },
			{ -x.dotProduct( position ), -y.dotProduct( position ), -z.dotProduct( position ), 1.f } };
		std::memcpy( m, rows, sizeof( m ) );
	}
	// @MATRIX_ANGLES@
};
const Matrix Matrix::identity = [] { Matrix r; for (int i = 0; i < 4; ++i) r.m[i][i] = 1.f; return r; }();

namespace Moo
{
struct Camera
{
	float nearPlane_, farPlane_, fov_, aspectRatio_;
	float nearPlane() const { return nearPlane_; }
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

template <class T> struct HostPtr
{
	T * p = nullptr;
	HostPtr() {}
	HostPtr( T * value ) : p( value ) {}
	T * get() const { return p; }
	T * operator->() const { return p; }
	explicit operator bool() const { return p != nullptr; }
};

struct MatrixProvider
{
	Matrix value;
	int label = 0;
	virtual ~MatrixProvider() {}
	virtual void matrix( Matrix & m ) const { m = value; }
};
typedef HostPtr< MatrixProvider > MatrixProviderPtr;

struct Vector4Provider
{
	Vector4 value;
	void output( Vector4 & v ) const { v = value; }
};
typedef HostPtr< Vector4Provider > Vector4ProviderPtr;

struct PyObject
{
	virtual ~PyObject() {}
};
typedef HostPtr< PyObject > PyObjectPtr;

struct WorldTriangle
{
	Vector3 v_[3];
	uint16_t flags_;
	WorldTriangle( const Vector3 & a, const Vector3 & b, const Vector3 & c, uint16_t flags = 0 ) :
		flags_( flags ) { v_[0] = a; v_[1] = b; v_[2] = c; }
	const Vector3 & v0() const { return v_[0]; }
	const Vector3 & v1() const { return v_[1]; }
	const Vector3 & v2() const { return v_[2]; }
};

class CollisionVisitor
{
public:
	virtual ~CollisionVisitor() {}
	virtual bool visit( const WorldTriangle & hitTriangle, float dist ) = 0;
};
