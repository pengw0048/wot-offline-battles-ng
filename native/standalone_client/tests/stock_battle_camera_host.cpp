// Runs the production battle camera collider and updates over scripted
// engine boundaries. Floats travel as IEEE bit patterns in hexadecimal.
struct _object;
typedef _object PyObject;
std::shared_ptr< class StockVehicleStickerGeometry > stockVehicleStickerGeometry( PyObject * );

// @COLLIDER@

#include <iostream>
#include <sstream>
#include <string>

namespace
{
std::istream * input;
float readFloat()
{
	std::string token;
	*input >> token;
	const uint32_t bits = uint32_t( std::stoul( token, nullptr, 16 ) );
	float value;
	std::memcpy( &value, &bits, 4 );
	return value;
}
Vector3 readVector() { const float x = readFloat(), y = readFloat(); return Vector3( x, y, readFloat() ); }
Matrix readMatrix() { Matrix m; for (int i = 0; i < 16; ++i) m.m[i / 4][i % 4] = readFloat(); return m; }
int readInt() { int value; *input >> value; return value; }
void put( float value ) { uint32_t bits; std::memcpy( &bits, &value, 4 ); std::printf( " %08x", bits ); }
void put( const Vector3 & v ) { put( v.x ); put( v.y ); put( v.z ); }
void put( const Matrix & m ) { for (int i = 0; i < 16; ++i) put( m.m[i / 4][i % 4] ); }
HostHit readHit()
{
	HostHit hit;
	hit.obstacle.sceneObject_.type = 1;
	hit.obstacle.sceneObject_.id = readInt();
	hit.obstacle.transform_ = readMatrix();
	const Vector3 a = readVector(), b = readVector(), c = readVector();
	hit.triangle = WorldTriangle( a, b, c, uint16_t( readInt() ) );
	hit.distance = readFloat();
	return hit;
}
void putState( const StockAdvancedCameraCollider & a )
{
	put( a.smoothedSpeed_ ); put( a.smoothedOffset_ ); put( a.lastPosition_ ); put( a.offset_ );
	put( a.remaining_ ); put( a.visibilityTime_ ); put( a.slowTime_ );
	std::printf( " %u %u", a.currentGroup_, a.groupSentinel_ );
}
}

// The scripted collider the camera updates hand their pose to.
struct HostCollider
{
	bool result = false;
	Vector3 output;
	bool collide( Vector3 & position, const Vector3 & pivot, const Vector3 & direction,
		const Vector3 & up, bool advanced )
	{
		std::printf( " collide" ); put( position ); put( pivot ); put( direction ); put( up );
		std::printf( " %d", int( advanced ) );
		if (result) position = output;
		return result;
	}
};

struct Vector4Basic { Vector4 value; void set( const Vector4 & v ) { value = v; } };
struct HostVector4Provider { Vector4 value; void output( Vector4 & v ) const { v = value; } };

struct ThirdPersonProvider
{
	std::shared_ptr< MatrixProvider > base_;
	float heightAboveBase_ = 0.f, focusRadius_ = 0.f;
	Vector3 up_ = Vector3( 0.f, 1.f, 0.f );
	float distanceFromFocus_ = 0.f, yaw_ = 0.f, pitch_ = 0.f;
	Matrix local_ = Matrix::identity, matrix_ = Matrix::identity, idealMatrix_ = Matrix::identity;
	bool dirty_ = true;
	std::shared_ptr< Vector4Basic > positionAboveBase_ = std::make_shared< Vector4Basic >();
	HostCollider collider_;
	void update( bool collide );
};

// @TPP_UPDATE@

struct SpeedTreeTargetedCamera
{
	Matrix view_, invView_;
	void update( float ) { std::printf( " speedtree" ); }
};

struct HomingCamera : SpeedTreeTargetedCamera
{
	std::shared_ptr< HostVector4Provider > pivotPositionProvider_, cameraPositionProvider_,
		aimPointProvider_;
	Vector3 up_ = Vector3( 0.f, 1.f, 0.f );
	Vector2 aimPointClipCoords_;
	HostCollider collider_;
	void update( float dTime );
};

// @HOMING_UPDATE@

int main()
{
	input = &std::cin;
	std::string command;
	StockAdvancedCameraCollider advanced( true );
	std::vector< StockColliderVolumeGroup > groups;
	while (std::cin >> command)
	{
		std::printf( "%s", command.c_str() );
		HostWorld & w = world();
		if (command == "camera")
		{
			Moo::Camera & c = Moo::rc().camera_;
			c.nearPlane_ = readFloat(); c.farPlane_ = readFloat(); c.fov_ = readFloat();
			c.aspectRatio_ = readFloat();
		}
		else if (command == "space") w.hasSpace = readInt() != 0;
		else if (command == "dtime") w.dTime = readFloat();
		else if (command == "spacebounds")
		{
			const Vector3 a = readVector();
			w.spaceBounds = AABB( a, readVector() );
		}
		else if (command == "waters")
		{
			w.waters.clear();
			for (int n = readInt(); n > 0; --n) w.waters.push_back( readFloat() );
		}
		else if (command == "sweep" || command == "rear" || command == "segment")
		{
			std::vector< HostHit > & hits = command == "sweep" ? w.sweep :
				(command == "rear" ? w.rear : w.segment);
			hits.clear();
			for (int n = readInt(); n > 0; --n) hits.push_back( readHit() );
		}
		else if (command == "objbounds")
		{
			const long id = readInt();
			const Vector3 a = readVector();
			w.bounds[id] = AABB( a, readVector() );
		}
		else if (command == "segseg")
		{
			const Vector3 p1 = readVector(), q1 = readVector(), p2 = readVector(), q2 = readVector();
			Vector3 c1, c2;
			put( closestSegmentSegment( p1, q1, p2, q2, c1, c2 ) ); put( c1 ); put( c2 );
		}
		else if (command == "inside")
		{
			const Vector3 p = readVector(), a = readVector(), b = readVector();
			Vector3 r( 0.f, 0.f, 0.f );
			const bool hit = projectInsideTriangle( p, WorldTriangle( a, b, readVector() ), r );
			std::printf( " %d", int( hit ) ); put( r );
		}
		else if (command == "closest")
		{
			const Vector3 p = readVector(), a = readVector(), b = readVector();
			put( closestPointOnTriangle( p, a, b, readVector() ) );
		}
		else if (command == "segtri")
		{
			const Vector3 a = readVector(), b = readVector(), v0 = readVector(), v1 = readVector();
			Vector3 s( 0.f, 0.f, 0.f ), t( 0.f, 0.f, 0.f );
			put( closestSegmentTriangle( a, b, WorldTriangle( v0, v1, readVector() ), s, t ) );
			put( s ); put( t );
		}
		else if (command == "plane")
		{
			const Vector3 n = readVector();
			const float d = readFloat();
			const Vector3 s = readVector(), e = readVector();
			Vector3 r( 0.f, 0.f, 0.f );
			std::printf( " %d", int( intersectPlaneSegment( PlaneEq( n, d ), s, e, r ) ) ); put( r );
		}
		else if (command == "triray")
		{
			const Vector3 a = readVector(), b = readVector(), c = readVector();
			const Vector3 s = readVector(), d = readVector();
			float length = readFloat();
			std::printf( " %d", int( WorldTriangle( a, b, c ).intersects( s, d, length ) ) ); put( length );
		}
		else if (command == "spacebox")
		{
			Vector3 position = readVector();
			const Vector3 pivot = readVector(), direction = readVector();
			const Vector3 original = position;
			std::printf( " %d", int( collideSpaceBounds( position, pivot, direction, original ) ) );
			put( position );
		}
		else if (command == "advinit")
		{
			advanced = StockAdvancedCameraCollider( true );
			const float a = readFloat(), b = readFloat(), c = readFloat(), d = readFloat();
			advanced.init( a, b, c, d, readFloat() );
		}
		else if (command == "advgroup")
		{
			StockColliderVolumeGroup g;
			g.minVolume = readFloat(); g.lowSpeedLimit = readFloat(); g.vehicleVisibilityLimit = readFloat();
			g.approachSpeed.x = readFloat(); g.approachSpeed.y = readFloat();
			g.cameraSpeedFactor = readFloat(); g.criticalDistanceSq = readFloat();
			g.canSkip = readInt() != 0;
			advanced.addVolumeGroup( g );
		}
		else if (command == "advoffset")
		{
			advanced.offset_ = readFloat(); advanced.remaining_ = readFloat();
		}
		else if (command == "scroll")
		{
			advanced.scrollDelta( readFloat() );
			putState( advanced );
		}
		else if (command == "advcollide")
		{
			const float dt = readFloat();
			Vector3 position = readVector();
			const Vector3 pivot = readVector();
			const int result = advanced.collide( dt, position, pivot, readVector() );
			std::printf( " %d", result ); put( position ); putState( advanced );
			std::printf( " %d %d", w.sweepCalls, w.segmentCalls );
			w.sweepCalls = w.segmentCalls = 0;
		}
		else if (command == "tpp")
		{
			ThirdPersonProvider tpp;
			const int hasBase = readInt();
			if (hasBase) { tpp.base_ = std::make_shared< MatrixProvider >(); tpp.base_->value = readMatrix(); }
			tpp.heightAboveBase_ = readFloat(); tpp.focusRadius_ = readFloat();
			tpp.distanceFromFocus_ = readFloat(); tpp.yaw_ = readFloat(); tpp.pitch_ = readFloat();
			tpp.up_ = readVector();
			tpp.collider_.result = readInt() != 0;
			tpp.collider_.output = readVector();
			const int frames = readInt();
			const int collide = readInt();
			for (int i = 0; i < frames; ++i)
			{
				tpp.update( collide != 0 );
				put( tpp.matrix_ ); put( tpp.idealMatrix_ ); put( tpp.local_ );
				put( tpp.positionAboveBase_->value.x ); put( tpp.positionAboveBase_->value.y );
				put( tpp.positionAboveBase_->value.z ); put( tpp.positionAboveBase_->value.w );
			}
		}
		else if (command == "homing")
		{
			HomingCamera camera;
			std::shared_ptr< HostVector4Provider > * providers[3] = { &camera.cameraPositionProvider_,
				&camera.aimPointProvider_, &camera.pivotPositionProvider_ };
			for (auto * provider : providers)
			{
				if (!readInt()) continue;
				*provider = std::make_shared< HostVector4Provider >();
				const Vector3 v = readVector();
				(*provider)->value = Vector4( v.x, v.y, v.z, 1.f );
			}
			camera.up_ = readVector();
			camera.aimPointClipCoords_.x = readFloat(); camera.aimPointClipCoords_.y = readFloat();
			camera.collider_.result = readInt() != 0;
			camera.collider_.output = readVector();
			camera.update( readFloat() );
			put( camera.view_ ); put( camera.invView_ );
		}
		std::printf( "\n" );
	}
	return 0;
}
