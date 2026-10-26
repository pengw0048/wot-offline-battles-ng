#include "pch.hpp"
#include "stock_vehicle_water_sensor.hpp"
#include "stock_vehicle_water_sensor_data.hpp"
#include "stock_vehicle_components.hpp"
#include "stock_vehicle_visuals.hpp"

#include "moo/render_context.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/worldtri.hpp"
#include "pyscript/script_math.hpp"
#include "romp/water.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "space/space_manager.hpp"

#include <algorithm>
#include <cfloat>
#include <map>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
int StockVehicleWaterSensor_token = 1;

namespace
{
const float UPDATE_INTERVAL = .25f;   // 01a1308c
const size_t UPDATES_PER_FRAME = 3;   // 01a13088
const uint32 NO_PRIORITY_LIMIT = 0xffffffff;
const char * CONFIG_ARGUMENT = "() argument 1";

struct SensorState;
std::vector<wot::StockLowFrequencyEntry> schedule;
std::map<uint64, std::weak_ptr<SensorState> > scheduled;
uint64 nextToken = 0;

// 00f645e0: the first Water with a positive a302e0(false) distance wins.
float waterSurface( const std::array<float, 3> & from, const std::array<float, 3> & to )
{
    const Vector3 start( from[0], from[1], from[2] );
    const Vector3 end( to[0], to[1], to[2] );
    for (const Water * water : Waters::instance())
    {
        const float distance = water->stockGroundRayDistance( start, end );
        if (distance > 0.f)
            return distance;
    }
    return -1.f;
}

class HeavySplashHit : public CollisionCallback
{
public:
    bool found = false;
    virtual int operator()( const CollisionObstacle &, const WorldTriangle & triangle, float )
    {
        // ClosestCcbNative f62400 with collideSegment flags 0x82. Its
        // WGMaterialDisabler branch cannot occur before that real type exists.
        if (triangle.collisionFlags() & 0x82)
            return COLLIDE_ALL;
        found = true;
        return COLLIDE_BEFORE;
    }
};

// 0079501d: f642b0 resolves the camera space by ID; no space means no hit.
bool groundBelowSurface( SpaceID spaceID, const std::array<float, 3> & from,
    const std::array<float, 3> & to )
{
    ClientSpacePtr space( SpaceManager::instance().space( spaceID ) );
    if (!space)
        return false;
    HeavySplashHit hit;
    space->collide( Vector3( from[0], from[1], from[2] ), Vector3( to[0], to[1], to[2] ), hit );
    return hit.found;
}

std::array<float, 16> matrixValues( const Matrix & value )
{
    // Matrix::operator[] yields a Vector3 row; D3DXMATRIX::m holds all 16 values.
    std::array<float, 16> result;
    for (int row = 0; row != 4; ++row)
        for (int column = 0; column != 4; ++column)
            result[row * 4 + column] = value.m[row][column];
    return result;
}

void fireEvent( const PyObjectPtr & callback, PyObject * args )
{
    if (!args)
    {
        PyErr_PrintEx( 0 );
        return;
    }
    PyObjectPtr arguments( args, PyObjectPtr::STEAL_REFERENCE );
    // EventPyCallable 0x00615b30: a strong reference outlives replacement or
    // destruction by the callback; failures are printed and cleared.
    PyObjectPtr pin = callback;
    if (!pin)
        return;
    PyObject * result = PyObject_Call( pin.get(), arguments.get(), NULL );
    if (!result)
        PyErr_PrintEx( 0 );
    Py_XDECREF( result );
}

struct SensorState : public StockComponentState
{
    explicit SensorState( const wot::StockWaterSensorConfig & value ) : config( value ) {}
    virtual void activate()
    {
        if (!valid_ || active)
            return;
        active = true;
        token = ++nextToken;
        // 013efa50: a new entry first becomes due one interval later.
        wot::StockLowFrequencyEntry entry = { UPDATE_INTERVAL, UPDATE_INTERVAL, 0, token };
        schedule.push_back( entry );
        scheduled[token] = std::static_pointer_cast<SensorState>( this->shared_from_this() );
    }
    virtual void deactivate()
    {
        if (!active)
            return;
        active = false;
        const uint64 removed = token;
        // 013efab0 compacts the remaining entries in order.
        schedule.erase( std::remove_if( schedule.begin(), schedule.end(),
            [removed]( const wot::StockLowFrequencyEntry & entry ) { return entry.token == removed; } ),
            schedule.end() );
        scheduled.erase( removed );
    }
    virtual void destroy()
    {
        if (!valid_)
            return;
        this->deactivate();
        valid_ = false;
        // 00794a40 releases the event and link functions in this order.
        onUnderWaterSwitch = NULL;
        onWaterSplash = NULL;
        speedLink = NULL;
        plane = NULL;
        lodLink = NULL;
    }
    virtual PyObject * wrap( bool owning );
    bool current( uint64 epoch ) const { return valid_ && active && token == epoch; }
    void contain( const char * message )
    {
        ERROR_MSG( "WaterSensor: %s\n", message );
        if (PyErr_Occurred())
            PyErr_PrintEx( 0 );
        this->deactivate();
    }
    enum SpeedSample { SPEED_READY, SPEED_MISSING, SPEED_FAILED };
    SpeedSample sampleSpeed( float & value )
    {
        PyObjectPtr link = speedLink;
        if (!link)
            return SPEED_MISSING;
        if (isStockFloatLink( link.get() ))
            return sampleStockFloatLink( link.get(), value ) ? SPEED_READY : SPEED_MISSING;
        // LinkFromPython<float>: a Python producer failure is local to this sample.
        PyObject * result = PyObject_CallObject( link.get(), NULL );
        const bool converted = result && Script::setData( result, value, "speedLink" ) == 0;
        Py_XDECREF( result );
        if (converted)
            return SPEED_READY;
        PyErr_PrintEx( 0 );
        return SPEED_FAILED;
    }
    void update( uint64 epoch );

    wot::StockWaterSensorConfig config;
    wot::StockWaterSensorState data;
    float maxLodDistance = FLT_MAX;
    uint32 maxPriority = NO_PRIORITY_LIMIT;
    PyObjectPtr lodLink;
    MatrixProviderPtr plane;
    PyObjectPtr speedLink;
    PyObjectPtr onWaterSplash;
    PyObjectPtr onUnderWaterSwitch;
    bool active = false;
    uint64 token = 0;
};

void SensorState::update( uint64 epoch )
{
    if (lodLink)
    {
        uint32 priority;
        float distance;
        if (!sampleStockLodLink( lodLink.get(), priority, distance ))
            return this->contain( "lost its native LOD state" );
        if (!wot::stockWaterSensorLodAllows( maxLodDistance, maxPriority, distance, priority ))
            return;
    }
    MatrixProviderPtr provider = plane;
    if (!provider)
        // The original std::function throws bad_function_call before any write.
        return this->contain( "has no sensorPlaneLink" );
    Matrix pose;
    provider->matrix( pose );
    if (!this->current( epoch ))
        return;
    const wot::StockWaterSensorSample sample = wot::measureStockWaterSensor( data, config,
        matrixValues( pose ), waterSurface,
        [&pose]( const std::array<float, 3> & point, const std::array<float, 16> & )
        {
            const Vector3 source( point[0], point[1], point[2] );
            Vector4 result;
            D3DXVec3Transform( &result, &source, &pose );
            return std::array<float, 4>{ { result.x, result.y, result.z, result.w } };
        } );
    if (sample.splashEdge)
    {
        float speed = 0.f;
        // An empty speedLink throws only here, after the sample is published.
        const SensorState::SpeedSample sampled = this->sampleSpeed( speed );
        if (sampled == SPEED_MISSING)
            return this->contain( "has no speedLink" );
        if (!this->current( epoch ))
            return;
        if (sampled == SPEED_READY && speed >= config.lightVelocity)
        {
            bool heavy = false;
            ClientSpacePtr camera = DeprecatedSpaceHelpers::cameraSpace();
            if (camera)
                heavy = !groundBelowSurface( camera->id(), sample.hitPoint,
                    wot::stockWaterSensorHeavyProbeEnd( sample.hitPoint, config ) ) &&
                    speed >= config.heavyVelocity;
            const Vector3 point( sample.hitPoint[0], sample.hitPoint[1], sample.hitPoint[2] );
            fireEvent( onWaterSplash, Py_BuildValue( "(NN)", Script::getData( point ),
                Script::getData( heavy ) ) );
            if (!this->current( epoch ))
                return;
        }
    }
    if (sample.underWaterChanged)
        fireEvent( onUnderWaterSwitch, Py_BuildValue( "(N)",
            Script::getData( (data.underWater & 1) != 0 ) ) );
}
}

// Original 0x01a116e0: returned for an event without a Python handler.
class StockNativeEventWrapper : public PyObjectPlus
{
    Py_Header( StockNativeEventWrapper, PyObjectPlus )
public:
    StockNativeEventWrapper() : PyObjectPlus( &s_type_ ) {}
};
PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME( StockNativeEventWrapper, &Super::s_type_,
    "PyNativeEventWrapper" )
PY_BEGIN_METHODS( StockNativeEventWrapper )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( StockNativeEventWrapper )
PY_END_ATTRIBUTES()

// Original DataLinks::PyDataLink<Matrix> returned by sensorPlaneLink.
class StockMatrixLink : public PyObjectPlus
{
    Py_Header( StockMatrixLink, PyObjectPlus )
public:
    explicit StockMatrixLink( MatrixProviderPtr provider ) :
        PyObjectPlus( &s_type_ ), provider_( provider ) {}
    MatrixProviderPtr provider() const { return provider_; }
    PY_METHOD_DECLARE( pyCall )
private:
    MatrixProviderPtr provider_;
};
PyObject * StockMatrixLink::pyCall( PyObject * )
{
    MatrixProviderPtr provider = provider_;
    if (!provider)
    {
        // The original empty function throws std::bad_function_call.
        PyErr_SetString( PyExc_TypeError, "PyDataLink<Matrix> is empty" );
        return NULL;
    }
    Matrix value;
    provider->matrix( value );
    return Script::getData( value );
}
PY_TYPEOBJECT_SPECIALISE_CALL( StockMatrixLink, &StockMatrixLink::_pyCall )
PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME( StockMatrixLink, &Super::s_type_,
    "DataLinks::PyDataLink<Matrix>" )
PY_BEGIN_METHODS( StockMatrixLink )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( StockMatrixLink )
PY_END_ATTRIBUTES()

class PyWaterSensor : public StockComponentWrapper
{
    Py_Header( PyWaterSensor, StockComponentWrapper )
public:
    PyWaterSensor( const std::shared_ptr<StockComponentState> & state, bool owning ) :
        StockComponentWrapper( state, &s_type_, owning ) {}
    std::shared_ptr<SensorState> state() const
    {
        std::shared_ptr<SensorState> value =
            std::dynamic_pointer_cast<SensorState>( this->component() );
        if (!value)
            PyErr_SetString( PyExc_ValueError, "WaterSensor has been destroyed" );
        return value;
    }
    PyObject * pyGet_isUnderWater() { return this->history( &wot::StockWaterSensorState::underWater ); }
    PY_RO_ATTRIBUTE_SET( isUnderWater )
    PyObject * pyGet_waterHeight()
    {
        std::shared_ptr<SensorState> value = this->state();
        return value ? Script::getData( value->data.waterHeight ) : NULL;
    }
    PY_RO_ATTRIBUTE_SET( waterHeight )
    PyObject * pyGet_speedLink()
    {
        std::shared_ptr<SensorState> value = this->state();
        if (!value)
            return NULL;
        return Script::getData( value->speedLink );
    }
    int pySet_speedLink( PyObject * object )
    {
        std::shared_ptr<SensorState> value = this->state();
        if (!value)
            return -1;
        if (!object || (object != Py_None && !isStockFloatLink( object ) && !PyCallable_Check( object )))
        {
            PyErr_Format( PyExc_TypeError, "%s must be set to a PyDataLink or callable", "speedLink" );
            return -1;
        }
        value->speedLink = object == Py_None ? NULL : object;
        return 0;
    }
    PyObject * pyGet_sensorPlaneLink()
    {
        std::shared_ptr<SensorState> value = this->state();
        return value ? static_cast<PyObject *>( new StockMatrixLink( value->plane ) ) : NULL;
    }
    int pySet_sensorPlaneLink( PyObject * object )
    {
        std::shared_ptr<SensorState> value = this->state();
        if (!value)
            return -1;
        MatrixProviderPtr provider;
        if (object && StockMatrixLink::Check( object ))
            provider = static_cast<StockMatrixLink *>( object )->provider();
        else if (object && object != Py_None && MatrixProvider::Check( object ))
            provider = static_cast<MatrixProvider *>( object );
        else if (object != Py_None)
        {
            PyErr_Format( PyExc_TypeError, "%s must be set to a PyDataLink or MatrixProvider",
                "sensorPlaneLink" );
            return -1;
        }
        // Keep the previous provider alive until the replacement is visible.
        MatrixProviderPtr previous = value->plane;
        value->plane = provider;
        return 0;
    }
    PyObject * pyGet_isSplash() { return this->history( &wot::StockWaterSensorState::splash ); }
    PY_RO_ATTRIBUTE_SET( isSplash )
    PyObject * pyGet_isInWater() { return this->history( &wot::StockWaterSensorState::inWater ); }
    PY_RO_ATTRIBUTE_SET( isInWater )
    PyObject * pyGet_onUnderWaterSwitch() { return this->event( &SensorState::onUnderWaterSwitch ); }
    int pySet_onUnderWaterSwitch( PyObject * object )
    {
        return this->event( &SensorState::onUnderWaterSwitch, object, "onUnderWaterSwitch" );
    }
    PyObject * pyGet_onWaterSplash() { return this->event( &SensorState::onWaterSplash ); }
    int pySet_onWaterSplash( PyObject * object )
    {
        return this->event( &SensorState::onWaterSplash, object, "onWaterSplash" );
    }
    PY_METHOD_DECLARE( py_setLodLink )
    PY_METHOD_DECLARE( py_setLodSettings )
    PY_FACTORY_DECLARE()
private:
    PyObject * history( uint8 wot::StockWaterSensorState::* member )
    {
        std::shared_ptr<SensorState> value = this->state();
        return value ? Script::getData( ((value->data.*member) & 1) != 0 ) : NULL;
    }
    PyObject * event( PyObjectPtr SensorState::* member )
    {
        std::shared_ptr<SensorState> value = this->state();
        if (!value)
            return NULL;
        PyObjectPtr callback = (*value).*member;
        if (!callback)
            return new StockNativeEventWrapper();
        PyObject * result = callback.get();
        Py_INCREF( result );
        return result;
    }
    int event( PyObjectPtr SensorState::* member, PyObject * object, const char * name )
    {
        std::shared_ptr<SensorState> value = this->state();
        if (!value)
            return -1;
        if (!object || (object != Py_None && !PyCallable_Check( object )))
        {
            PyErr_Format( PyExc_TypeError, "%s must be callable", name );
            return -1;
        }
        // Retain the old callable until assignment completes; it may be running.
        PyObjectPtr previous = (*value).*member;
        (*value).*member = object == Py_None ? NULL : object;
        return 0;
    }
};

PyObject * SensorState::wrap( bool owning )
{
    return new PyWaterSensor( this->shared_from_this(), owning );
}

PyObject * PyWaterSensor::pyNew( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
    {
        PyErr_SetString( PyExc_TypeError,
            "PyWaterSensor() expects 1 argument of type WaterSensor::Config" );
        return NULL;
    }
    PyObject * source = PyTuple_GET_ITEM( args, 0 );
    if (!PyTuple_Check( source ) || PyTuple_GET_SIZE( source ) != 5)
    {
        PyErr_Format( PyExc_TypeError, "%s must be set to a tuple with %d arguments",
            CONFIG_ARGUMENT, 5 );
        return NULL;
    }
    // 007796b0 starts from Vector3::zero/Vector2::zero and converts every item.
    Vector3 turret( 0.f, 0.f, 0.f );
    Vector2 carrying( 0.f, 0.f );
    wot::StockWaterSensorConfig config;
    const bool first = Script::setData( PyTuple_GET_ITEM( source, 0 ), turret, "" ) == 0;
    const bool rest = Script::setData( PyTuple_GET_ITEM( source, 1 ), carrying, "" ) == 0 &&
        Script::setData( PyTuple_GET_ITEM( source, 2 ), config.lightVelocity, "" ) == 0 &&
        Script::setData( PyTuple_GET_ITEM( source, 3 ), config.heavyVelocity, "" ) == 0 &&
        Script::setData( PyTuple_GET_ITEM( source, 4 ), config.heavyDepth, "" ) == 0;
    if (!first || !rest)
    {
        PyErr_Format( PyExc_TypeError, "failed to unpack %s", CONFIG_ARGUMENT );
        return NULL;
    }
    config.turretOffset = { { turret.x, turret.y, turret.z } };
    config.carryingPoint = { { carrying.x, carrying.y } };
    // 00777dc0 deactivates the pooled component before publishing its owner.
    return new PyWaterSensor( std::make_shared<SensorState>( config ), true );
}

PyObject * PyWaterSensor::py_setLodLink( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
    {
        PyErr_SetString( PyExc_TypeError, "setLodLink() expects 1 argument of type "
            "DataLinks::DataLink<LodProcessing::LodState>::Type" );
        return NULL;
    }
    PyObject * link = PyTuple_GET_ITEM( args, 0 );
    uint32 priority;
    float distance;
    if (link != Py_None && !sampleStockLodLink( link, priority, distance ))
    {
        PyErr_Format( PyExc_TypeError, "%s must be set to a PyDataLink", CONFIG_ARGUMENT );
        return NULL;
    }
    std::shared_ptr<SensorState> value = this->state();
    if (!value)
        return NULL;
    value->lodLink = link == Py_None ? NULL : link;
    Py_RETURN_NONE;
}

PyObject * PyWaterSensor::py_setLodSettings( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
    {
        PyErr_SetString( PyExc_TypeError,
            "setLodSettings() expects 1 argument of type LodProcessing::LodSettings" );
        return NULL;
    }
    PyObject * settings = PyTuple_GET_ITEM( args, 0 );
    // 0077efe0 reads ob_size/ob_item without a type check; an empty tuple
    // would read past its items. Both are rejected rather than reinterpreted.
    if (!PyTuple_Check( settings ) || PyTuple_GET_SIZE( settings ) == 0)
    {
        PyErr_Format( PyExc_TypeError, "%s must be set to a LodSettings tuple", CONFIG_ARGUMENT );
        return NULL;
    }
    if (PyTuple_GET_SIZE( settings ) >= 3)
    {
        PyErr_SetString( PyExc_TypeError, "Invalid args count" );
        return NULL;
    }
    float distance;
    uint32 priority = NO_PRIORITY_LIMIT;
    if (Script::setData( PyTuple_GET_ITEM( settings, 0 ), distance, "maxLodDistance" ) != 0)
        return NULL;
    if (PyTuple_GET_SIZE( settings ) == 2 && PyTuple_GET_ITEM( settings, 1 ) != Py_None &&
        Script::setData( PyTuple_GET_ITEM( settings, 1 ), priority, "maxPriority" ) != 0)
        return NULL;
    std::shared_ptr<SensorState> value = this->state();
    if (!value)
        return NULL;
    value->maxLodDistance = distance;
    value->maxPriority = priority;
    Py_RETURN_NONE;
}

PY_TYPEOBJECT( PyWaterSensor )
PY_BEGIN_METHODS( PyWaterSensor )
    PY_METHOD( setLodLink )
    PY_METHOD( setLodSettings )
PY_END_METHODS()
// Original 00779e00 registration order.
PY_BEGIN_ATTRIBUTES( PyWaterSensor )
    PY_ATTRIBUTE( isUnderWater )
    PY_ATTRIBUTE( waterHeight )
    PY_ATTRIBUTE( speedLink )
    PY_ATTRIBUTE( sensorPlaneLink )
    PY_ATTRIBUTE( isSplash )
    PY_ATTRIBUTE( isInWater )
    PY_ATTRIBUTE( onUnderWaterSwitch )
    PY_ATTRIBUTE( onWaterSplash )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyWaterSensor, "WaterSensor", Vehicular )

bool stockWaterSensorBoolLink( PyObject * owner, const BW::string & name,
    std::function<bool()> & reader )
{
    uint8 wot::StockWaterSensorState::* member = NULL;
    if (name == "isInWater")
        member = &wot::StockWaterSensorState::inWater;
    else if (name == "isUnderWater")
        member = &wot::StockWaterSensorState::underWater;
    else if (name == "isSplash")
        member = &wot::StockWaterSensorState::splash;
    if (!member || !PyWaterSensor::Check( owner ))
        return false;
    std::shared_ptr<SensorState> state = std::dynamic_pointer_cast<SensorState>(
        static_cast<PyWaterSensor *>( owner )->component() );
    if (!state)
        return false;
    std::weak_ptr<SensorState> weak = state;
    reader = [weak, member]()
    {
        std::shared_ptr<SensorState> value = weak.lock();
        return value && value->valid() && ((value->data.*member) & 1) != 0;
    };
    return true;
}

void tickStockWaterSensors( float frameTime )
{
    wot::advanceStockLowFrequency( schedule, frameTime, Moo::rc().frameTimestamp(),
        UPDATES_PER_FRAME );
    const size_t count = std::min( UPDATES_PER_FRAME, schedule.size() );
    const std::vector<wot::StockLowFrequencyEntry> work( schedule.begin(), schedule.begin() + count );
    for (size_t index = 0; index < work.size(); ++index)
    {
        const wot::StockLowFrequencyEntry & entry = work[index];
        if (!(entry.remaining <= 0.f))
            break;
        std::map<uint64, std::weak_ptr<SensorState> >::const_iterator owner =
            scheduled.find( entry.token );
        std::shared_ptr<SensorState> value =
            owner == scheduled.end() ? std::shared_ptr<SensorState>() : owner->second.lock();
        if (!value || !value->current( entry.token ))
            continue;
        value->update( entry.token );
        // 013efb20 restarts the processed entry after its callbacks.
        for (size_t slot = 0; slot < schedule.size(); ++slot)
            if (schedule[slot].token == entry.token)
            {
                schedule[slot].remaining = schedule[slot].reset = UPDATE_INTERVAL;
                break;
            }
    }
}

namespace
{
class WaterSensorScriptShutdown : public Script::FiniTimeJob
{
    virtual void fini()
    {
        schedule.clear();
        scheduled.clear();
    }
} waterSensorScriptShutdown;
}
BW_END_NAMESPACE
