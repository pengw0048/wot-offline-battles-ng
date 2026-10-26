#pragma once

#include "py_filter.hpp"
#include "filter.hpp"
#include "stock_avatar_filter_helper.hpp"
#include "pyscript/script_math.hpp"

#include <cfloat>
#include <memory>

BW_BEGIN_NAMESPACE
class Entity;
class PyEntity;
class StockEntityFilter;

struct StockEntityFilterGlobals
{
    static bool canExtrapolateOnLag;
    static bool enableClientFilters;
    static bool allowLagCollisions;
    static bool allowCustomResets;
    static float defaultMaxTimeBetweenInputs;
};

struct StockEntityFilterSettings
{
    bool ignoreInputs = false;
    bool ignoreNextReset = false;
    float maxTimeBetweenInputs = StockEntityFilterGlobals::defaultMaxTimeBetweenInputs;
    float maxMove = FLT_MAX;
};

// Real model-bounds producer, pose-updated geometry and original broad phase.
// Edges 0..3 are the vehicle perimeter, 4/5 the cross and 6 the vertical.
struct StockEntityFilterGeometry
{
    float radius = -1.f;
    float planarRadiusSquared = -1.f;
    float lowerPlanarRadiusSquared = -1.f;
    float verticalRadius = 0.f;
    float minimumY = 0.f;
    float maximumY = 0.f;
    float boundsAge = 1.4f;
    Vector3 local[7];
    Vector3 points[7];
    Vector3 edges[7][2];
    PyObjectPtr model;
    uint64 revision = 0;
    bool matches( const Entity & owner ) const;
    bool load( Entity & owner );
    void update( const Matrix & pose, float elapsed );
    bool segmentMayHit( const Vector3 & start, const Vector3 & end, unsigned mode ) const;
    void invalidate();
};

struct StockEntityFilterMotion
{
    Vector3 predictedVelocity = Vector3::zero();
    Vector3 predictedAngularVelocity = Vector3::zero();
    Vector3 predictedAcceleration = Vector3::zero();
    Vector3 predictedAngularAcceleration = Vector3::zero();
    StockEntityFilterGeometry geometry;
    WeakPyPtr<PyEntity> owner;
};

class PyWGEntityFilter : public PyFilter
{
    Py_Header( PyWGEntityFilter, PyFilter )
public:
    explicit PyWGEntityFilter( PyTypeObject * type = &s_type_ );
    virtual bool isLaggingNow() const;
    void owner( PyEntity * value );
    bool attached() const { return this->pAttachedFilter() != NULL; }
    StockEntityFilter * native();
    const StockEntityFilter * native() const;
    void applySettings( StockEntityFilter & filter );
    void cacheSettings( const StockEntityFilter & filter, const StockEntityFilterSettings & settings );
    const WeakPyPtr<PyEntity> & ownerIdentity() const { return owner_; }

    bool ignoreInputs() const;
    void ignoreInputs( bool value );
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, ignoreInputs, ignoreInputs )
    bool ignoreNextReset() const;
    void ignoreNextReset( bool value );
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, ignoreNextReset, ignoreNextReset )
    float maxTimeBetweenInputs() const;
    void maxTimeBetweenInputs( float value );
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( float, maxTimeBetweenInputs, maxTimeBetweenInputs )
    float maxMove() const;
    void maxMove( float value );
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( float, maxMove, maxMove )
    PY_RW_ATTRIBUTE_DECLARE( StockEntityFilterGlobals::canExtrapolateOnLag, canExtrapolateOnLag )
    PY_RW_ATTRIBUTE_DECLARE( StockEntityFilterGlobals::enableClientFilters, enableClientFilters )
    PY_RW_ATTRIBUTE_DECLARE( StockEntityFilterGlobals::allowLagCollisions, allowLagCollisions )
    PY_RW_ATTRIBUTE_DECLARE( StockEntityFilterGlobals::allowCustomResets, allowCustomResets )
    PY_RW_ATTRIBUTE_DECLARE( StockEntityFilterGlobals::defaultMaxTimeBetweenInputs, defaultMaxTimeBetweenInputs )
    PyObject * pyGet_latestInput();
    PY_RO_ATTRIBUTE_SET( latestInput )
    PyObject * pyGet_movementExtrapolationData();
    PY_RO_ATTRIBUTE_SET( movementExtrapolationData )
    bool segmentMayHitEntity( Vector3 start, Vector3 end, unsigned mode ) const;
    PY_AUTO_METHOD_DECLARE( RETDATA, segmentMayHitEntity,
        ARG( Vector3, ARG( Vector3, ARG( unsigned, END ) ) ) )
    void setScriptInputCallback( PyObjectPtr callback );
    PY_AUTO_METHOD_DECLARE( RETVOID, setScriptInputCallback, ARG( PyObjectPtr, END ) )
    PY_METHOD_DECLARE( py_transferInput )
    PY_METHOD_DECLARE( py_transferInputAsVehicle )
    static PyWGEntityFilter * New() { return new PyWGEntityFilter(); }
    PY_AUTO_FACTORY_DECLARE( PyWGEntityFilter, END )
protected:
    virtual Filter * getNewFilter();
private:
    PyObject * transfer( PyObject * arguments, bool asVehicle );
    StockEntityFilterSettings settings_;
    PyObjectPtr pendingTransfer_;
    PyObjectPtr pendingVehicleTransfer_;
    MatrixProviderPtr pendingOffset_;
    PyObjectPtr pendingCallback_;
    WeakPyPtr<PyEntity> owner_;
};

class StockEntityFilter : public Filter, public StockAvatarFilterHelper::Listener
{
public:
    struct Lifetime { bool alive = true; };
    StockEntityFilter( PyWGEntityFilter * owner,
        const std::shared_ptr<StockEntityFilterMotion> & motion );
    virtual ~StockEntityFilter();
    StockAvatarFilterHelper & helper() { return *helper_; }
    const StockAvatarFilterHelper & helper() const { return *helper_; }
    const std::shared_ptr<StockAvatarFilterHelper> & history() const { return helper_; }
    const std::shared_ptr<Lifetime> & lifetime() const { return lifetime_; }
    const std::shared_ptr<StockEntityFilterMotion> & motion() const { return motion_; }
    StockEntityFilterSettings & settings() { return settings_; }
    const StockEntityFilterSettings & settings() const { return settings_; }
    Entity * entity() const;
    void setScriptInputCallback( PyObjectPtr callback );
    void transfer( const PyWGEntityFilter & source, MatrixProviderPtr offset, bool asVehicle );
    virtual void reset( double time );
    virtual void input( double time, SpaceID spaceID, EntityID vehicleID,
        const Position3D & position, const Vector3 & error, const Direction3D & direction );
    virtual void output( double time, MovementFilterTarget & target );
    virtual bool getLastInput( double & time, SpaceID & spaceID, EntityID & vehicleID,
        Position3D & position, Vector3 & error, Direction3D & direction ) const;
    virtual void onReset();
    virtual void onWaypoint( double time, StockAvatarFilterHelper::Waypoint & current,
        StockAvatarFilterHelper::Waypoint & next );
protected:
    void retire();
    virtual bool tryCopyState( const MovementFilter & source );
    void updateVelocityReceipts( const StockAvatarFilterHelper::Waypoint & current,
        const StockAvatarFilterHelper::Waypoint & next );
    bool updateGeometry( const StockAvatarFilterHelper::Waypoint & current,
        const StockAvatarFilterHelper::Waypoint & next );
    bool lagEntitiesBlocked() const;
    std::shared_ptr<StockEntityFilterMotion> motion_;
    std::shared_ptr<StockAvatarFilterHelper> helper_;
    std::shared_ptr<Lifetime> lifetime_ = std::make_shared<Lifetime>();
    StockEntityFilterSettings settings_;
    PyObjectPtr inputCallback_;
    bool failed_ = false;
};

BW_END_NAMESPACE
