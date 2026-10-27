#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_vector.hpp"
#include "math/vector2.hpp"
#include "math/vector3.hpp"
#include "pyscript/script_math.hpp"
#include <memory>

BW_BEGIN_NAMESPACE
class StockVehicleStickerGeometry;

// #1513 CameraCollider::DynamicCollisionInfo: (node, bounds, geometry link).
struct StockCameraDynamicCollider
{
    MatrixProviderPtr node;
    MatrixProviderPtr bounds;
    std::shared_ptr< StockVehicleStickerGeometry > geometry;
};

// #1513 ColliderVolumeGroup (0x20 bytes). criticalDistanceSq is stored squared.
struct StockColliderVolumeGroup
{
    float minVolume = 0.f;
    float lowSpeedLimit = 0.f;
    float vehicleVisibilityLimit = 0.f;
    Vector2 approachSpeed = Vector2( 0.f, 0.f );
    float cameraSpeedFactor = 0.1f;
    float criticalDistanceSq = 25.f;
    bool canSkip = false;
};

// #1513 advanced collider (CameraCollider+0xc): volume-group obstruction state.
class StockAdvancedCameraCollider
{
public:
    explicit StockAdvancedCameraCollider( bool enabled ) : enabled_( enabled ) {}

    bool enabled() const { return enabled_; }
    void init( float fovRatio, float rollbackSpeed, float minimalCameraDistance,
        float speedThreshold, float minVolume );
    void addVolumeGroup( const StockColliderVolumeGroup & group );
    void scrollDelta( float delta );
    bool collide( float dTime, Vector3 & position, const Vector3 & pivot,
        const Vector3 & up );

    // Recovered state, in #1513 field order.
    BW::vector< StockColliderVolumeGroup > groups_;
    BW::vector< float > minVolumes_;
    float smoothedSpeed_ = 2.5f;
    float smoothedOffset_ = 0.f;
    Vector3 lastPosition_ = Vector3( 0.f, 0.f, 0.f );
    float offset_ = 0.f;
    float remaining_ = 0.f;
    float visibilityTime_ = 0.f;
    float slowTime_ = 0.f;
    float fovRatio_ = 2.f;
    float rollbackSpeed_ = 1.f;
    float minimalCameraDistance_ = 2.f;
    float speedThreshold_ = 0.1f;
    // #1513 leaves this uninitialised until initAdvancedCollider.
    float minVolume_ = 0.f;
    unsigned currentGroup_ = 1;
    unsigned groupSentinel_ = 1;

private:
    bool enabled_;
};

// #1513 CameraCollider (0x68): dynamic hulls, space or advanced collider,
// space bounds and water.
class StockCameraCollider
{
public:
    explicit StockCameraCollider( bool advanced ) : advanced_( advanced ) {}

    void dynamicColliders( BW::vector< StockCameraDynamicCollider > & colliders );
    StockAdvancedCameraCollider & advanced() { return advanced_; }
    bool collide( Vector3 & position, const Vector3 & pivot,
        const Vector3 & direction, const Vector3 & up, bool useAdvanced );

private:
    bool collideDynamic( Vector3 & position, const Vector3 & direction,
        const Vector3 & up ) const;

    BW::vector< StockCameraDynamicCollider > dynamic_;
    StockAdvancedCameraCollider advanced_;
};

// Python converters with the #1513 error texts.
int setStockCameraDynamicColliders( PyObject * object,
    BW::vector< StockCameraDynamicCollider > & colliders, const char * varName );
int setStockColliderVolumeGroup( PyObject * object,
    StockColliderVolumeGroup & group, const char * varName );

// #1513 0x1a138c8: skips destructible material kinds 0x47..0x64.
float collideStockCameraSegment( const Vector3 & start, const Vector3 & end );

BW_END_NAMESPACE
