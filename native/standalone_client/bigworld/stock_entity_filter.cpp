#include "pch.hpp"
#include "stock_entity_filter.hpp"
#include "vehicle_appearance.hpp"
#include "stock_scene_prediction.hpp"

#include "entity.hpp"
#include "py_entity.hpp"
#include "connection_control.hpp"
#include "space/entity_embodiment.hpp"
#include "pyscript/script_math.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

BW_BEGIN_NAMESPACE

bool StockEntityFilterGlobals::canExtrapolateOnLag = true;
bool StockEntityFilterGlobals::enableClientFilters = true;
bool StockEntityFilterGlobals::allowLagCollisions = true;
bool StockEntityFilterGlobals::allowCustomResets = true;
float StockEntityFilterGlobals::defaultMaxTimeBetweenInputs = 2.f;

bool StockEntityFilterGeometry::matches( const Entity & owner ) const
{
    IEntityEmbodiment * embodiment = owner.pPrimaryEmbodiment();
    return embodiment && embodiment->scriptObject().get() == model.get();
}

bool StockEntityFilterGeometry::load( Entity & owner )
{
    IEntityEmbodiment * embodiment = owner.pPrimaryEmbodiment();
    if (!embodiment)
        return false;
    PyObjectPtr objectPin( embodiment->scriptObject().get() );
    PyObject * object = objectPin.get();
    const uint64 expectedRevision = revision;
    BoundingBox bounds;
    if (!stockVehicleModelBounds( object, bounds ) || bounds.insideOut())
        return false;
    if (revision != expectedRevision || owner.isDestroyed() || owner.pPrimaryEmbodiment() != embodiment ||
        embodiment->scriptObject().get() != object) return false;
    PyObjectPtr previous = model;
    ++revision;
    model = object;
    const Vector3 minimum = bounds.minBounds();
    const Vector3 maximum = bounds.maxBounds();
    const float width = std::max( maximum.x - minimum.x, maximum.z - minimum.z );
    planarRadiusSquared = width * 0.5f * width;
    lowerPlanarRadiusSquared = planarRadiusSquared;
    radius = sqrtf( planarRadiusSquared );
    const float supportY = (maximum.y - minimum.y) * 0.8f + minimum.y;
    minimumY = minimum.y - supportY;
    maximumY = maximum.y - supportY;
    local[0].set( minimum.x - 0.09f, supportY, maximum.z + 0.09f );
    local[1].set( maximum.x + 0.09f, supportY, maximum.z + 0.09f );
    local[2].set( minimum.x - 0.09f, supportY, minimum.z - 0.09f );
    local[3].set( maximum.x + 0.09f, supportY, minimum.z - 0.09f );
    const Vector3 center = (minimum + maximum) * 0.5f;
    local[4].set( center.x, minimum.y - 0.09f, center.z );
    local[5].set( center.x, maximum.y + 0.09f, center.z );
    local[6] = center;
    boundsAge = 1.4f;
    return true;
}

void StockEntityFilterGeometry::update( const Matrix & pose, float elapsed )
{
    for (unsigned index = 0; index < 7; ++index)
        points[index] = pose.applyPoint( local[index] );
    const unsigned segments[4][2] = { {0,1}, {1,3}, {2,3}, {0,2} };
    for (unsigned index = 0; index < 4; ++index)
    {
        edges[index][0] = points[segments[index][0]];
        edges[index][1] = points[segments[index][1]];
    }
    edges[4][0] = (points[0] + points[1]) * 0.5f;
    edges[4][1] = (points[2] + points[3]) * 0.5f;
    edges[5][0] = (points[0] + points[2]) * 0.5f;
    edges[5][1] = (points[1] + points[3]) * 0.5f;
    edges[6][0] = points[4];
    edges[6][1] = points[5];
    if (model && static_cast<double>( boundsAge ) > 0.4)
    {
        PyObjectPtr modelPin = model;
        const uint64 expectedRevision = revision;
        PyAttachment * attachment = static_cast<PyAttachment *>( modelPin.get() );
        BoundingBox bounds;
        attachment->worldBoundingBox( bounds, attachment->worldTransform() );
        if (revision != expectedRevision || model != modelPin) return;
        boundsAge = 0.f;
        if (bounds.insideOut())
        {
            this->invalidate();
            return;
        }
        if (!bounds.insideOut())
        {
            const Vector3 minimum = bounds.minBounds();
            const Vector3 maximum = bounds.maxBounds();
            const Vector3 center = points[6];
            const float radiusX = std::max( fabsf( center.x - minimum.x ),
                fabsf( maximum.x - center.x ) );
            const float radiusZ = std::max( fabsf( center.z - minimum.z ),
                fabsf( maximum.z - center.z ) );
            const float radiusY = std::max( fabsf( center.y - minimum.y ),
                fabsf( maximum.y - center.y ) );
            const float horizontal = std::max( radiusX, radiusZ );
            planarRadiusSquared = horizontal * horizontal;
            verticalRadius = std::max( radiusY, horizontal * 0.5f );
        }
    }
    else
        boundsAge += elapsed;
}

void StockEntityFilterGeometry::invalidate()
{
    ++revision;
    radius = -1.f;
    planarRadiusSquared = -1.f;
    lowerPlanarRadiusSquared = -1.f;
    verticalRadius = 0.f;
    boundsAge = 1.4f;
    PyObjectPtr previous = model;
    model = NULL;
}

bool StockEntityFilterGeometry::segmentMayHit( const Vector3 & start,
    const Vector3 & end, unsigned mode ) const
{
    // 006f16c0 treats unknown (including unordered) radius conservatively.
    // This is a broad-phase question; the real mesh test still owns any hit.
    if (!(radius >= 0.f)) return true;
    const Vector3 & center = points[6];
    const float firstZ = center.z - start.z;
    const float startX = start.x - center.x;
    const float endZ = center.z - end.z;
    const float endX = center.x - end.x;
    const float firstX = center.x - start.x;
    const float deltaZ = firstZ - endZ;
    const float deltaX = firstX - endX;
    const float lengthSquared = deltaX * deltaX + deltaZ * deltaZ;
    const float length = sqrtf( lengthSquared );
    float radiusSquared = planarRadiusSquared;
    if (length != 0.f && mode == 0)
    {
        const float inverse = 1.f / length;
        const float slope = (end.y - start.y) * inverse;
        float projected = (deltaX * firstX + firstZ * deltaZ) * inverse * slope;
        const float centerOffset = center.y - start.y;
        // MAXSS keeps its second operand for unordered values.
        projected = projected > centerOffset ? projected : centerOffset;
        projected += start.y;
        if (center.y > projected) radiusSquared = lowerPlanarRadiusSquared;
        const float startZ = start.z - center.z;
        if (startX * startX + startZ * startZ > radiusSquared)
        {
            const float height = fabsf( radius * slope ) + verticalRadius;
            if (center.y - height > projected || projected > center.y + height)
                return false;
        }
    }
    const float dot = deltaX * firstX + firstZ * deltaZ;
    if (dot <= 0.f)
        return firstX * firstX + firstZ * firstZ <= radiusSquared;
    if (!(dot >= lengthSquared))
        return (firstX * firstX + firstZ * firstZ) -
            dot * dot / lengthSquared <= radiusSquared;
    return endX * endX + endZ * endZ <= radiusSquared;
}


namespace
{
class WgVehicleOutput : public PyObjectPlus
{
    Py_Header( WgVehicleOutput, PyObjectPlus )
public:
    WgVehicleOutput() : PyObjectPlus( &s_type_ ), position_( Vector3::zero() ),
        direction_( Vector3::zero() ), velocity_( Vector3::zero() ),
        angularVelocity_( Vector3::zero() ), space_( 0 ), vehicle_( 0 ) {}
    Vector3 position_;
    Direction3D direction_;
    Vector3 velocity_;
    Direction3D angularVelocity_;
    SpaceID space_;
    EntityID vehicle_;
    PY_RO_ATTRIBUTE_DECLARE( position_, position )
    PY_RO_ATTRIBUTE_DECLARE( direction_, direction )
    PY_RO_ATTRIBUTE_DECLARE( velocity_, velocity )
    PY_RO_ATTRIBUTE_DECLARE( angularVelocity_, angularVelocity )
    PY_RO_ATTRIBUTE_DECLARE( space_, space )
    PY_RO_ATTRIBUTE_DECLARE( vehicle_, vehicle )
};
PY_TYPEOBJECT( WgVehicleOutput )
PY_BEGIN_METHODS( WgVehicleOutput )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WgVehicleOutput )
    PY_ATTRIBUTE( position )
    PY_ATTRIBUTE( direction )
    PY_ATTRIBUTE( velocity )
    PY_ATTRIBUTE( angularVelocity )
    PY_ATTRIBUTE( space )
    PY_ATTRIBUTE( vehicle )
PY_END_ATTRIBUTES()

Vector3 & accumulatedVector( StockEntityFilterMotion & motion, unsigned index )
{
    if (index == 0) return motion.predictedVelocity;
    if (index == 1) return motion.predictedAngularVelocity;
    if (index == 2) return motion.predictedAcceleration;
    return motion.predictedAngularAcceleration;
}

class PyExtrapolationData : public PyObjectPlus
{
    Py_Header( PyExtrapolationData, PyObjectPlus )
public:
    explicit PyExtrapolationData( PyWGEntityFilter * owner ) :
        PyObjectPlus( &s_type_ ), owner_( owner ) {}
    PyObject * read( unsigned index )
    {
        const StockEntityFilter * filter =
            static_cast<PyWGEntityFilter *>( owner_.get() )->native();
        if (!filter)
        {
            PyErr_SetString( PyExc_ValueError, "Extrapolation data requires an attached WGEntityFilter" );
            return NULL;
        }
        const auto motion = filter->motion();
        const Vector3 value = accumulatedVector( *motion, index );
        return Script::getData( value );
    }
    int write( unsigned index, PyObject * value )
    {
        PyWGEntityFilter * owner = static_cast<PyWGEntityFilter *>( owner_.get() );
        StockEntityFilter * filter = owner->native();
        if (!filter)
        {
            PyErr_SetString( PyExc_ValueError, "Extrapolation data requires an attached WGEntityFilter" );
            return -1;
        }
        const auto lifetime = filter->lifetime();
        const auto motion = filter->motion();
        Vector3 vector;
        if (Script::setData( value, vector, "extrapolation data" ) != 0) return -1;
        if (!lifetime->alive || owner->native() != filter)
        {
            PyErr_SetString( PyExc_ValueError, "WGEntityFilter changed during extrapolation conversion" );
            return -1;
        }
        accumulatedVector( *motion, index ) = vector;
        return 0;
    }
    PyObject * pyGet_accumLinearVelocity() { return this->read( 0 ); }
    int pySet_accumLinearVelocity( PyObject * value ) { return this->write( 0, value ); }
    PyObject * pyGet_accumAngularVelocity() { return this->read( 1 ); }
    int pySet_accumAngularVelocity( PyObject * value ) { return this->write( 1, value ); }
    PyObject * pyGet_accumLinearAcceleration() { return this->read( 2 ); }
    int pySet_accumLinearAcceleration( PyObject * value ) { return this->write( 2, value ); }
    PyObject * pyGet_accumAngularAcceleration() { return this->read( 3 ); }
    int pySet_accumAngularAcceleration( PyObject * value ) { return this->write( 3, value ); }
private:
    PyObjectPtr owner_;
};
PY_TYPEOBJECT( PyExtrapolationData )
PY_BEGIN_METHODS( PyExtrapolationData )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyExtrapolationData )
    PY_ATTRIBUTE( accumLinearVelocity )
    PY_ATTRIBUTE( accumAngularVelocity )
    PY_ATTRIBUTE( accumLinearAcceleration )
    PY_ATTRIBUTE( accumAngularAcceleration )
PY_END_ATTRIBUTES()
}

PyWGEntityFilter::PyWGEntityFilter( PyTypeObject * type ) : PyFilter( type ) {}

StockEntityFilter * PyWGEntityFilter::native()
{
    return dynamic_cast<StockEntityFilter *>( this->pAttachedFilter() );
}
const StockEntityFilter * PyWGEntityFilter::native() const
{
    return dynamic_cast<const StockEntityFilter *>( this->pAttachedFilter() );
}

bool PyWGEntityFilter::isLaggingNow() const
{
    const StockEntityFilter * filter = this->native();
    return !filter || filter->helper().extrapolating();
}

void PyWGEntityFilter::owner( PyEntity * value )
{
    owner_ = WeakPyPtr<PyEntity>( value );
    StockEntityFilter * filter = this->native();
    if (filter) filter->motion()->owner = owner_;
}

bool PyWGEntityFilter::ignoreInputs() const
{
    const StockEntityFilter * filter = this->native();
    return filter ? filter->settings().ignoreInputs : settings_.ignoreInputs;
}
void PyWGEntityFilter::ignoreInputs( bool value )
{
    settings_.ignoreInputs = value;
    StockEntityFilter * filter = this->native();
    if (filter) filter->settings().ignoreInputs = value;
}
bool PyWGEntityFilter::ignoreNextReset() const
{
    const StockEntityFilter * filter = this->native();
    return filter ? filter->settings().ignoreNextReset : settings_.ignoreNextReset;
}
void PyWGEntityFilter::ignoreNextReset( bool value )
{
    settings_.ignoreNextReset = value;
    StockEntityFilter * filter = this->native();
    if (filter) filter->settings().ignoreNextReset = value;
}
float PyWGEntityFilter::maxTimeBetweenInputs() const
{
    const StockEntityFilter * filter = this->native();
    return filter ? filter->settings().maxTimeBetweenInputs : settings_.maxTimeBetweenInputs;
}
void PyWGEntityFilter::maxTimeBetweenInputs( float value )
{
    settings_.maxTimeBetweenInputs = value;
    StockEntityFilter * filter = this->native();
    if (filter) filter->settings().maxTimeBetweenInputs = value;
}
float PyWGEntityFilter::maxMove() const
{
    const StockEntityFilter * filter = this->native();
    return filter ? filter->settings().maxMove : settings_.maxMove;
}
void PyWGEntityFilter::maxMove( float value )
{
    settings_.maxMove = value;
    StockEntityFilter * filter = this->native();
    if (filter) filter->settings().maxMove = value;
}

void PyWGEntityFilter::cacheSettings( const StockEntityFilter & filter,
    const StockEntityFilterSettings & settings )
{
    if (this->pAttachedFilter() == &filter) settings_ = settings;
}

void PyWGEntityFilter::setScriptInputCallback( PyObjectPtr callback )
{
    StockEntityFilter * filter = this->native();
    if (filter) filter->setScriptInputCallback( callback );
    else
    {
        PyObjectPtr previous = pendingCallback_;
        pendingCallback_ = callback;
    }
}

PyObject * PyWGEntityFilter::transfer( PyObject * arguments, bool asVehicle )
{
    PyObject * sourceObject;
    PyObject * offsetObject;
    if (!PyArg_ParseTuple( arguments, "OO:transferInput", &sourceObject, &offsetObject )) return NULL;
    if (!PyWGEntityFilter::Check( sourceObject ))
    {
        PyErr_SetString( PyExc_TypeError, "transferInput requires a WGEntityFilter source" );
        return NULL;
    }
    PyObjectPtr source( sourceObject );
    MatrixProviderPtr offset;
    if (Script::setData( offsetObject, offset, "transferInput() argument 2" ) != 0) return NULL;
    StockEntityFilter * filter = this->native();
    if (filter)
    {
        // 006e91a0/006e92d0 both use 006f1c10 when already attached.
        filter->transfer( *static_cast<PyWGEntityFilter *>( source.get() ), offset, true );
    }
    else
    {
        PyObjectPtr previousSource = asVehicle ? pendingVehicleTransfer_ : pendingTransfer_;
        MatrixProviderPtr previousOffset = pendingOffset_;
        if (asVehicle) pendingVehicleTransfer_ = source;
        else pendingTransfer_ = source;
        pendingOffset_ = offset;
    }
    Py_RETURN_NONE;
}
PyObject * PyWGEntityFilter::py_transferInput( PyObject * arguments )
{
    return this->transfer( arguments, false );
}
PyObject * PyWGEntityFilter::py_transferInputAsVehicle( PyObject * arguments )
{
    return this->transfer( arguments, true );
}

void PyWGEntityFilter::applySettings( StockEntityFilter & filter )
{
    const auto lifetime = filter.lifetime();
    filter.settings() = settings_;
    PyObjectPtr callback = pendingCallback_;
    PyObjectPtr sourceVehicle = pendingVehicleTransfer_;
    PyObjectPtr source = pendingTransfer_;
    MatrixProviderPtr offset = pendingOffset_;
    // Publish retirement before sampling providers or releasing Python values.
    pendingCallback_ = NULL;
    pendingVehicleTransfer_ = NULL;
    pendingTransfer_ = NULL;
    pendingOffset_ = NULL;
    filter.setScriptInputCallback( callback );
    if (lifetime->alive && sourceVehicle)
        filter.transfer( *static_cast<PyWGEntityFilter *>( sourceVehicle.get() ), offset, true );
    if (lifetime->alive && source)
        filter.transfer( *static_cast<PyWGEntityFilter *>( source.get() ), offset, false );
}

Filter * PyWGEntityFilter::getNewFilter()
{
    StockEntityFilter * filter = new StockEntityFilter( this,
        std::make_shared<StockEntityFilterMotion>() );
    this->applySettings( *filter );
    return filter;
}

bool PyWGEntityFilter::segmentMayHitEntity( Vector3 start, Vector3 end, unsigned mode ) const
{
    const StockEntityFilter * filter = this->native();
    return filter && filter->motion()->geometry.segmentMayHit( start, end, mode );
}

PyObject * PyWGEntityFilter::pyGet_movementExtrapolationData()
{
    return new PyExtrapolationData( this );
}

PyObject * PyWGEntityFilter::pyGet_latestInput()
{
    const StockEntityFilter * filter = this->native();
    if (!filter)
    {
        PyErr_SetString( PyExc_ValueError, "latestInput requires an attached WGEntityFilter" );
        return NULL;
    }
    PyObjectPtr held( this );
    const auto history = filter->history();
    const auto lifetime = filter->lifetime();
    WgVehicleOutput * result = new WgVehicleOutput();
    PyObjectPtr resultPin( result, PyObjectPtr::STEAL_REFERENCE );
    if (!lifetime->alive || this->native() != filter)
    {
        PyErr_SetString( PyExc_ValueError, "WGEntityFilter changed during output allocation" );
        return NULL;
    }
    if (history->hasInput())
    {
        const auto latest = history->storedInput( 0 );
        const auto previous = history->storedInput( 1 );
        result->position_ = latest.position;
        result->direction_ = latest.direction;
        result->space_ = latest.spaceID;
        result->vehicle_ = latest.vehicleID;
        Position3D oldPosition = previous.position;
        Direction3D oldDirection = previous.direction;
        StockEntityFilter * mutableFilter = const_cast<StockEntityFilter *>( filter );
        mutableFilter->transformIntoCommon( previous.spaceID, previous.vehicleID,
            oldPosition, oldDirection );
        if (!lifetime->alive || this->native() != filter)
        {
            PyErr_SetString( PyExc_ValueError, "WGEntityFilter changed during coordinate conversion" );
            return NULL;
        }
        mutableFilter->transformFromCommon( latest.spaceID, latest.vehicleID,
            oldPosition, oldDirection );
        if (!lifetime->alive || this->native() != filter)
        {
            PyErr_SetString( PyExc_ValueError, "WGEntityFilter changed during coordinate conversion" );
            return NULL;
        }
        const float inverseTime = 1.f / static_cast<float>( latest.time - previous.time );
        result->velocity_ = (latest.position - oldPosition) * inverseTime;
        Quaternion oldRotation;
        Quaternion newRotation;
        Quaternion difference;
        D3DXQuaternionRotationYawPitchRoll( &oldRotation,
            oldDirection.yaw, oldDirection.pitch, oldDirection.roll );
        D3DXQuaternionRotationYawPitchRoll( &newRotation,
            latest.direction.yaw, latest.direction.pitch, latest.direction.roll );
        D3DXQuaternionInverse( &oldRotation, &oldRotation );
        D3DXQuaternionMultiply( &difference, &oldRotation, &newRotation );
        const float sign = difference.w <= 0.f ? -1.f : 1.f;
        difference.x *= sign;
        difference.y *= sign;
        difference.z *= sign;
        difference.w *= sign;
        const float clamped = difference.w <= 1.f ? difference.w : 1.f;
        const float angle = acosf( clamped <= -1.f ? -1.f : clamped );
        Vector3 axis( difference.x, difference.y, difference.z );
        D3DXVec3Normalize( &axis, &axis );
        result->angularVelocity_.yaw = (angle + angle) * axis.y * inverseTime;
    }
    Py_INCREF( result );
    return result;
}

StockEntityFilter::StockEntityFilter( PyWGEntityFilter * owner,
    const std::shared_ptr<StockEntityFilterMotion> & motion ) :
    Filter( owner ), motion_( motion ),
    helper_( std::make_shared<StockAvatarFilterHelper>( this->environment() ) )
{
    motion_->owner = owner->ownerIdentity();
    motion_->predictedVelocity.setZero();
    motion_->predictedAngularVelocity.setZero();
    motion_->predictedAcceleration.setZero();
    motion_->predictedAngularAcceleration.setZero();
    motion_->geometry.invalidate();
    helper_->listener( this );
}

StockEntityFilter::~StockEntityFilter()
{
    this->retire();
}

void StockEntityFilter::retire()
{
    lifetime_->alive = false;
    helper_->listener( NULL );
    const PyFilter * owner = this->pOwner();
    if (owner && PyWGEntityFilter::Check( const_cast<PyFilter *>( owner ) ))
    {
        PyWGEntityFilter * wrapper = const_cast<PyWGEntityFilter *>(
            static_cast<const PyWGEntityFilter *>( owner ) );
        wrapper->cacheSettings( *this, settings_ );
        // The patched public owner tolerates this early retirement followed
        // by Filter's normal destructor notification. Python callback releases
        // can now reenter without finding a partially destroyed native filter.
        if (wrapper->pAttachedFilter() == this) wrapper->onFilterDestroyed( this );
    }
}

Entity * StockEntityFilter::entity() const
{
    const PyEntity * owner = motion_->owner.get();
    return owner && !owner->isDestroyed() ? owner->pEntity().get() : NULL;
}

void StockEntityFilter::setScriptInputCallback( PyObjectPtr callback )
{
    PyObjectPtr previous = inputCallback_;
    inputCallback_ = callback.get() == Py_None ? PyObjectPtr() : callback;
}

void StockEntityFilter::transfer( const PyWGEntityFilter & source,
    MatrixProviderPtr offset, bool asVehicle )
{
    const StockEntityFilter * sourceFilter = source.native();
    if (!sourceFilter) return;
    Entity * destinationEntity = this->entity();
    EntityPtr destinationEntityPin( destinationEntity );
    const SpaceID destinationSpace = destinationEntity ? destinationEntity->spaceID() : 0;
    const auto sourceHistory = sourceFilter->helper_;
    const auto sourceLifetime = sourceFilter->lifetime_;
    const auto history = helper_;
    const auto lifetime = lifetime_;
    Entity * sourceEntity = sourceFilter->entity();
    EntityPtr sourceEntityPin( sourceEntity );
    if (asVehicle && !sourceEntity)
    {
        ERROR_MSG( "WGEntityFilter transfer source has no live entity\n" );
        return;
    }
    const SpaceID spaceID = sourceEntity ? sourceEntity->spaceID() : 0;
    const EntityID vehicleID = sourceEntity ? sourceEntity->entityID() : 0;
    Matrix matrix = Matrix::identity;
    if (offset) offset->matrix( matrix );
    if (!lifetime->alive || !sourceLifetime->alive || source.native() != sourceFilter) return;
    if (destinationEntity && (destinationEntity->isDestroyed() || this->entity() != destinationEntity ||
            destinationEntity->spaceID() != destinationSpace)) return;
    if (sourceEntity && (sourceEntity->isDestroyed() || sourceFilter->entity() != sourceEntity ||
            sourceEntity->spaceID() != spaceID)) return;
    if (asVehicle)
        history->transferInputsAsVehicle( *sourceHistory, matrix, spaceID, vehicleID );
    else
        history->transferInputs( *sourceHistory, matrix );
}

void StockEntityFilter::reset( double time )
{
    if (settings_.ignoreNextReset)
    {
        settings_.ignoreNextReset = false;
        return;
    }
    helper_->reset( time );
}

bool StockEntityFilter::getLastInput( double & time, SpaceID & spaceID,
    EntityID & vehicleID, Position3D & position, Vector3 & error,
    Direction3D & direction ) const
{
    return helper_->getLastInput( time, spaceID, vehicleID, position, error, direction );
}

void StockEntityFilter::input( double time, SpaceID spaceID, EntityID vehicleID,
    const Position3D & position, const Vector3 & error, const Direction3D & direction )
{
    if (settings_.ignoreInputs) return;
    const auto lifetime = lifetime_;
    const auto history = helper_;
    PyObjectPtr callback = inputCallback_;
    if (callback)
    {
        PyObjectPtr arguments( Py_BuildValue( "(diiNNN)", time,
            static_cast<int32>( spaceID ), static_cast<int32>( vehicleID ),
            Script::getData( position ), Script::getData( error ), Script::getData( direction ) ),
            PyObjectPtr::STEAL_REFERENCE );
        PyObjectPtr result( arguments ? PyObject_CallObject( callback.get(), arguments.get() ) : NULL,
            PyObjectPtr::STEAL_REFERENCE );
        if (!result) PyErr_Print();
    }
    if (!lifetime->alive) return;
    const auto previous = history->storedInput( 0 );
    if (StockEntityFilterGlobals::allowCustomResets &&
        (time - previous.time > static_cast<double>( settings_.maxTimeBetweenInputs ) ||
         (position - history->nextWaypointPosition()).length() - error.length() >= settings_.maxMove))
        this->reset( time );
    if (!lifetime->alive) return;
    history->input( time, spaceID, vehicleID, position, error, direction );
}

void StockEntityFilter::output( double time, MovementFilterTarget & target )
{
    Entity & owner = static_cast<Entity &>( target );
    EntityPtr entityPin( &owner );
    const auto lifetime = lifetime_;
    const auto history = helper_;
    if (!history->hasInput()) return;
    SpaceID spaceID;
    EntityID vehicleID;
    Position3D position;
    Vector3 velocity;
    Direction3D direction;
    failed_ = false;
    history->output( time, spaceID, vehicleID, position, velocity, direction );
    if (!lifetime->alive || owner.isDestroyed() || owner.pFilter() != this) return;
    if (failed_)
    {
        if (PyErr_Occurred()) PyErr_Print();
        return;
    }
    target.setSpaceVehiclePositionAndDirection( spaceID, vehicleID, position, direction );
    if (!lifetime->alive || owner.isDestroyed() || owner.pFilter() != this) return;
    owner.localVelocity( velocity );
}

void StockEntityFilter::onReset()
{
    motion_->predictedVelocity.setZero();
    motion_->predictedAngularVelocity.setZero();
    motion_->predictedAcceleration.setZero();
    motion_->predictedAngularAcceleration.setZero();
}

bool StockEntityFilter::tryCopyState( const MovementFilter & source )
{
    const StockEntityFilter * other = dynamic_cast<const StockEntityFilter *>( &source );
    if (!other) return false;
    *helper_ = *other->helper_;
    helper_->listener( this );
    motion_->predictedVelocity = other->motion_->predictedVelocity;
    motion_->predictedAngularVelocity = other->motion_->predictedAngularVelocity;
    motion_->predictedAcceleration = other->motion_->predictedAcceleration;
    motion_->predictedAngularAcceleration = other->motion_->predictedAngularAcceleration;
    motion_->geometry.invalidate();
    settings_ = other->settings_;
    return true;
}

void StockEntityFilter::updateVelocityReceipts(
    const StockAvatarFilterHelper::Waypoint & current,
    const StockAvatarFilterHelper::Waypoint & next )
{
    if (current.time != current.input.time || next.time != next.input.time)
        return;
    const float elapsed = static_cast<float>( next.time - current.time );
    Quaternion inverse;
    Quaternion difference;
    D3DXQuaternionInverse( &inverse, &current.orientation );
    D3DXQuaternionMultiply( &difference, &inverse, &next.orientation );
    const float sign = difference.w <= 0.f ? -1.f : 1.f;
    difference.x *= sign;
    difference.y *= sign;
    difference.z *= sign;
    difference.w *= sign;
    const float angle = acosf( std::max( -1.f, std::min( 1.f, difference.w ) ) ) * 2.f;
    Vector3 angular( difference.x, difference.y, difference.z );
    D3DXVec3Normalize( &angular, &angular );
    angular *= angle;
    const Vector3 linear = next.position - current.position;
    const float window = std::min( elapsed, 0.25f );
    const float inverseTime = 1.f / elapsed;
    const float remainder = 0.25f - window;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        // #1513 006f0060 retains these two EMA stages and operation order.
        const float linearChange = linear[axis] * inverseTime * window * 4.f -
            motion_->predictedVelocity[axis] * window * 4.f;
        const float angularChange = angular[axis] * inverseTime * window * 4.f -
            motion_->predictedAngularVelocity[axis] * window * 4.f;
        motion_->predictedVelocity[axis] += linearChange;
        motion_->predictedAngularVelocity[axis] += angularChange;
        motion_->predictedAcceleration[axis] =
            (motion_->predictedAcceleration[axis] * remainder +
                linearChange * inverseTime * window) * 4.f;
        motion_->predictedAngularAcceleration[axis] =
            (motion_->predictedAngularAcceleration[axis] * remainder +
                angularChange * inverseTime * window) * 4.f;
    }
}


bool StockEntityFilter::updateGeometry( const StockAvatarFilterHelper::Waypoint & current,
    const StockAvatarFilterHelper::Waypoint & next )
{
    const auto lifetime = lifetime_;
    const auto motion = motion_;
    Entity * owner = this->entity();
    if (!owner || !owner->pPrimaryEmbodiment())
    {
        motion->geometry.invalidate();
        return lifetime->alive;
    }
    EntityPtr entityPin( owner );
    if (motion->geometry.radius >= 0.f && !motion->geometry.matches( *owner ))
        motion->geometry.invalidate();
    if (!lifetime->alive) return false;
    if (motion->geometry.radius < 0.f && !motion->geometry.load( *owner ))
        return lifetime->alive;
    if (!lifetime->alive) return false;
    if (motion->geometry.radius >= 0.f)
    {
        Matrix pose;
        D3DXMatrixRotationQuaternion( &pose, &next.orientation );
        pose.translation( next.position );
        motion->geometry.update( pose, static_cast<float>( next.time - current.time ) );
    }
    return lifetime->alive;
}

bool StockEntityFilter::lagEntitiesBlocked() const
{
    const auto lifetime = lifetime_;
    const auto motion = motion_;
    Entity * owner = this->entity();
    if (!owner || !(motion->geometry.radius >= 0.f)) return false;
    EntityPtr entityPin( owner );
    const SpaceID spaceID = owner->spaceID();
    struct Gather : EntityVisitor
    {
        std::vector<EntityPtr> entities;
        bool onVisitEntity( Entity * entity )
        {
            entities.push_back( EntityPtr( entity ) );
            return true;
        }
    } gather;
    ConnectionControl::instance().visitEntities( gather );
    for (const auto & target : gather.entities)
    {
        if (target.get() == owner || target->isDestroyed() || target->spaceID() != spaceID) continue;
        const StockEntityFilter * other = dynamic_cast<const StockEntityFilter *>( target->pFilter() );
        if (!other || !(other->motion_->geometry.radius >= 0.f)) continue;
        const auto otherMotion = other->motion_;
        const Vector3 first = owner->position();
        const Vector3 second = target->position();
        const float deltaX = first.x - second.x;
        const float deltaZ = first.z - second.z;
        if (!(sqrtf( deltaX * deltaX + deltaZ * deltaZ ) -
            (motion->geometry.radius + otherMotion->geometry.radius) * 1.1f < 0.f)) continue;
        PyObjectPtr targetPin( target->pPyEntity().get() );
        if (!targetPin)
        {
            PyErr_SetString( PyExc_RuntimeError, "WGEntityFilter mesh collision target has no script owner" );
            return false;
        }
        for (unsigned edge = 4; edge < 7; ++edge)
        {
            const Vector3 start = motion->geometry.edges[edge][0];
            const Vector3 end = motion->geometry.edges[edge][1];
            const float length = (end - start).length();
            PyObjectPtr method( PyObject_GetAttrString( targetPin.get(), "collideSegment" ),
                PyObjectPtr::STEAL_REFERENCE );
            PyObjectPtr arguments( Py_BuildValue( "(NNO)", Script::getData( start ),
                Script::getData( end ), Py_None ), PyObjectPtr::STEAL_REFERENCE );
            PyObjectPtr result( method && arguments ? PyObject_CallObject( method.get(), arguments.get() ) : NULL,
                PyObjectPtr::STEAL_REFERENCE );
            if (!result) PyErr_Print();
            if (!lifetime->alive) return false;
            if (owner->isDestroyed() || owner->spaceID() != spaceID || owner->pFilter() != this ||
                target->isDestroyed() || target->spaceID() != spaceID || target->pFilter() != other)
            {
                PyErr_SetString( PyExc_RuntimeError, "WGEntityFilter owner changed during mesh collision query" );
                return false;
            }
            if (!result || !PyTuple_Check( result.get() ) || PyTuple_GET_SIZE( result.get() ) == 0) continue;
            float distance;
            if (Script::setData( PyTuple_GET_ITEM( result.get(), 0 ), distance, "collideSegment distance" ) != 0)
            {
                // f5cae0 prints and clears both call and conversion failures.
                PyErr_Print();
                continue;
            }
            if (!lifetime->alive) return false;
            if (owner->isDestroyed() || owner->pFilter() != this ||
                target->isDestroyed() || target->pFilter() != other)
            {
                PyErr_SetString( PyExc_RuntimeError, "WGEntityFilter owner changed during collision conversion" );
                return false;
            }
            if (distance < length) return true;
        }
    }
    return false;
}

void StockEntityFilter::onWaypoint( double /* time */, StockAvatarFilterHelper::Waypoint & current,
    StockAvatarFilterHelper::Waypoint & next )
{
    if (!StockEntityFilterGlobals::enableClientFilters) return;
    const auto lifetime = lifetime_;
    const auto history = helper_;
    const auto motion = motion_;
    if (!this->updateGeometry( current, next ) || !lifetime->alive) return;
    if (!history->extrapolating())
    {
        this->updateVelocityReceipts( current, next );
        return;
    }
    if (!StockEntityFilterGlobals::canExtrapolateOnLag) return;
    const Vector3 linear = motion->predictedVelocity;
    const Vector3 angular = motion->predictedAngularVelocity;
    if (fabsf( linear.x ) < 0.01f && fabsf( linear.y ) < 0.01f && fabsf( linear.z ) < 0.01f &&
        fabsf( angular.x ) < 0.01f && fabsf( angular.y ) < 0.01f && fabsf( angular.z ) < 0.01f) return;
    if (StockEntityFilterGlobals::allowLagCollisions && motion->geometry.radius >= 0.f)
    {
        bool blocked;
        const bool success = queryStockEntityLagScenePrediction( next.spaceID,
            &motion->geometry.edges[4][0], motion->owner, linear, blocked );
        if (!lifetime->alive) return;
        if (!success)
        {
            failed_ = true;
            return;
        }
        if (blocked) return;
        const bool entitiesBlocked = this->lagEntitiesBlocked();
        if (!lifetime->alive) return;
        if (PyErr_Occurred())
        {
            failed_ = true;
            return;
        }
        if (entitiesBlocked) return;
    }
    const float elapsed = static_cast<float>( next.time - current.time );
    motion->predictedVelocity += motion->predictedAcceleration * elapsed;
    motion->predictedAngularVelocity += motion->predictedAngularAcceleration * elapsed;
    next.position = current.position + motion->predictedVelocity * elapsed;
    const Vector3 rotation = motion->predictedAngularVelocity * elapsed;
    Quaternion delta;
    D3DXQuaternionRotationAxis( &delta, &rotation, rotation.length() );
    D3DXQuaternionMultiply( &next.orientation, &current.orientation, &delta );
}

PY_TYPEOBJECT( PyWGEntityFilter )
PY_BEGIN_METHODS( PyWGEntityFilter )
    PY_METHOD( transferInput )
    PY_METHOD( transferInputAsVehicle )
    PY_METHOD( segmentMayHitEntity )
    PY_METHOD( setScriptInputCallback )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyWGEntityFilter )
    PY_ATTRIBUTE( latestInput )
    PY_ATTRIBUTE( movementExtrapolationData )
    PY_ATTRIBUTE( ignoreInputs )
    PY_ATTRIBUTE( ignoreNextReset )
    PY_ATTRIBUTE( maxTimeBetweenInputs )
    PY_ATTRIBUTE( maxMove )
    PY_ATTRIBUTE( canExtrapolateOnLag )
    PY_ATTRIBUTE( allowLagCollisions )
    PY_ATTRIBUTE( enableClientFilters )
    PY_ATTRIBUTE( allowCustomResets )
    PY_ATTRIBUTE( defaultMaxTimeBetweenInputs )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyWGEntityFilter, "WGEntityFilter", BigWorld )

BW_END_NAMESPACE
