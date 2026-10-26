#include "pch.hpp"
#include "stock_vehicle_terrain_sensor.hpp"
#include "stock_vehicle_terrain_sensor_data.hpp"
#include "stock_vehicle_water_sensor_data.hpp"
#include "stock_vehicle_components.hpp"
#include "stock_vehicle_visuals.hpp"

#include "moo/render_context.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/worldtri.hpp"
#include "pyscript/script_math.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "space/space_manager.hpp"

#include <algorithm>
#include <cfloat>
#include <map>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
int StockVehicleTerrainSensor_token = 1;

namespace
{
const float UPDATE_INTERVAL = .25f;          // 005af7cf
const size_t UPDATES_PER_FRAME = 3;          // 005af7d6
const uint32 NO_PRIORITY_LIMIT = 0xffffffff; // 01a12e38
const char * ARGUMENT_1 = "() argument 1";

struct SensorState;
std::vector<wot::StockLowFrequencyEntry> schedule;
std::map<uint64, std::weak_ptr<SensorState> > scheduled;
uint64 nextToken = 0;

class ClosestMatKind : public CollisionCallback
{
public:
    wot::StockTerrainMatKindHit hit;
    virtual int operator()( const CollisionObstacle &, const WorldTriangle & triangle, float distance )
    {
        // f64630's WGMaterialDisabler branch cannot occur before that real type exists.
        return hit.offer( triangle.collisionFlags(), triangle.materialKind(), distance );
    }
};

// 00f642b0 resolves the camera space by ID (f61bd0); no space means no hit.
bool groundMatKind( SpaceID spaceID, const std::array<float, 3> & from,
    const std::array<float, 3> & to, uint32 & kind )
{
    ClientSpacePtr space( SpaceManager::instance().space( spaceID ) );
    if (!space)
        return false;
    ClosestMatKind query;
    space->collide( Vector3( from[0], from[1], from[2] ), Vector3( to[0], to[1], to[2] ), query );
    if (!query.hit.found)
        return false;
    kind = query.hit.kind;
    return true;
}

std::array<float, 16> matrixValues( const Matrix & value )
{
    std::array<float, 16> result;
    for (int row = 0; row != 4; ++row)
        for (int column = 0; column != 4; ++column)
            result[row * 4 + column] = value.m[row][column];
    return result;
}

struct SensorState : public StockComponentState
{
    SensorState( MatrixProviderPtr source, const std::vector<std::array<float, 3> > & samples,
            float length ) :
        provider( source ), points( samples ), scanLength( length ), matKinds( samples.size(), 0 ) {}
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
        // 00796200 frees matKinds and the points, then the matrix and LOD links.
        std::vector<uint32>().swap( matKinds );
        std::vector<std::array<float, 3> >().swap( points );
        provider = NULL;
        lodLink = NULL;
    }
    virtual PyObject * wrap( bool owning );
    bool current( uint64 epoch ) const { return valid_ && active && token == epoch; }
    void contain( const char * message )
    {
        ERROR_MSG( "TerrainMatKindSensor: %s\n", message );
        this->deactivate();
    }
    void update( uint64 epoch );

    MatrixProviderPtr provider;
    std::vector<std::array<float, 3> > points;
    float scanLength;
    std::vector<uint32> matKinds;
    float maxLodDistance = FLT_MAX;
    uint32 maxPriority = NO_PRIORITY_LIMIT;
    PyObjectPtr lodLink;
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
        if (!wot::stockTerrainSensorLodAllows( maxLodDistance, maxPriority, distance, priority ))
            return;
    }
    // 00ac2f10: without a camera space the previous kinds stay published.
    ClientSpacePtr camera = DeprecatedSpaceHelpers::cameraSpace();
    if (!camera)
        return;
    MatrixProviderPtr source = provider;
    if (!source)
        // 00757cbd calls through a None provider; nothing is sampled instead.
        return this->contain( "has no MatrixProvider" );
    Matrix pose;
    source->matrix( pose );
    if (!this->current( epoch ))
        return;
    const SpaceID spaceID = camera->id();
    wot::sampleStockTerrainMatKinds( matKinds, points, scanLength, matrixValues( pose ),
        [&pose]( const std::array<float, 3> & point, const std::array<float, 16> & )
        {
            const Vector3 source( point[0], point[1], point[2] );
            Vector4 result;
            D3DXVec3Transform( &result, &source, &pose );
            return std::array<float, 4>{ { result.x, result.y, result.z, result.w } };
        },
        [spaceID]( const std::array<float, 3> & from, const std::array<float, 3> & to, uint32 & kind )
        {
            return groundMatKind( spaceID, from, to, kind );
        } );
}
}

// Original PySTLSequence 0x01a0c7b8 over a read-only PySTLSequenceHolder
// (pOwner_ NULL, writable_ false) that 0077e460 binds to component+0x7c.
// This view follows the component; once it is destroyed it reads as empty.
class StockMatKindSequence : public PyObjectPlus
{
    Py_Header( StockMatKindSequence, PyObjectPlus )
public:
    explicit StockMatKindSequence( const std::shared_ptr<SensorState> & state ) :
        PyObjectPlus( &s_type_ ), state_( state ) {}

    PY_SIZE_INQUIRY_METHOD( pySeq_length )
    PY_BINARY_FUNC_METHOD( pySeq_concat )
    PY_INTARG_FUNC_METHOD( pySeq_repeat )
    PY_INTARG_FUNC_METHOD( pySeq_item )
    PY_INTINTARG_FUNC_METHOD( pySeq_slice )
    PY_INTOBJARG_PROC_METHOD( pySeq_ass_item )
    PY_INTINTOBJARG_PROC_METHOD( pySeq_ass_slice )
    PY_OBJOBJ_PROC_METHOD( pySeq_contains )
    PY_BINARY_FUNC_METHOD( pySeq_inplace_concat )
    PY_INTARG_FUNC_METHOD( pySeq_inplace_repeat )
    static PySequenceMethods s_seq_methods_;

    PyObject * pyRepr();
    PyObject * pyGet_length() { return Script::getData( int( this->size() ) ); }
    PY_RO_ATTRIBUTE_SET( length )

private:
    int size() const
    {
        std::shared_ptr<SensorState> state = state_.lock();
        return state && state->valid() ? int( state->matKinds.size() ) : 0;
    }
    PyObject * at( int index ) const
    {
        std::shared_ptr<SensorState> state = state_.lock();
        if (!state || !state->valid() || index < 0 || size_t( index ) >= state->matKinds.size())
            Py_RETURN_NONE;
        return Script::getData( state->matKinds[index] );
    }
    std::weak_ptr<SensorState> state_;
};

PySequenceMethods StockMatKindSequence::s_seq_methods_ = {
    _pySeq_length, _pySeq_concat, _pySeq_repeat, _pySeq_item, _pySeq_slice,
    _pySeq_ass_item, _pySeq_ass_slice, _pySeq_contains, _pySeq_inplace_concat,
    _pySeq_inplace_repeat
};

Py_ssize_t StockMatKindSequence::pySeq_length() { return this->size(); }

PyObject * StockMatKindSequence::pySeq_concat( PyObject * other )
{
    if (!PySequence_Check( other ))
    {
        PyErr_SetString( PyExc_TypeError, "PySTLSequence: Argument to + must be a sequence" );
        return NULL;
    }
    const int size = this->size();
    const Py_ssize_t otherSize = PySequence_Size( other );
    if (otherSize < 0)
        return NULL;
    PyObject * list = PyList_New( size + otherSize );
    if (!list)
        return NULL;
    for (int index = 0; index < size; ++index)
        PyList_SET_ITEM( list, index, this->at( index ) );
    for (Py_ssize_t index = 0; index < otherSize; ++index)
    {
        PyObject * item = PySequence_GetItem( other, index );
        if (!item)
        {
            Py_DECREF( list );
            return NULL;
        }
        PyList_SET_ITEM( list, size + index, item );
    }
    return list;
}

PyObject * StockMatKindSequence::pySeq_repeat( Py_ssize_t count )
{
    if (count <= 0)
        return PyList_New( 0 );
    const int size = this->size();
    PyObject * list = PyList_New( size * count );
    if (!list)
        return NULL;
    for (int index = 0; index < size; ++index)
        PyList_SET_ITEM( list, index, this->at( index ) );
    for (Py_ssize_t copy = 1; copy < count; ++copy)
        for (int index = 0; index < size; ++index)
        {
            PyObject * item = PyList_GET_ITEM( list, index );
            Py_INCREF( item );
            PyList_SET_ITEM( list, copy * size + index, item );
        }
    return list;
}

PyObject * StockMatKindSequence::pySeq_item( Py_ssize_t index )
{
    if (index < 0 || index >= this->size())
    {
        PyErr_SetString( PyExc_IndexError, "PySTLSequence index out of range" );
        return NULL;
    }
    return this->at( int( index ) );
}

PyObject * StockMatKindSequence::pySeq_slice( Py_ssize_t first, Py_ssize_t last )
{
    const int size = this->size();
    first = std::min<Py_ssize_t>( std::max<Py_ssize_t>( first, 0 ), size );
    last = std::min<Py_ssize_t>( std::max<Py_ssize_t>( last, 0 ), size );
    if (size == 0 || last <= first)
        return PyList_New( 0 );
    PyObject * list = PyList_New( last - first );
    if (!list)
        return NULL;
    for (Py_ssize_t index = first; index < last; ++index)
        PyList_SET_ITEM( list, index - first, this->at( int( index ) ) );
    return list;
}

int StockMatKindSequence::pySeq_ass_item( Py_ssize_t, PyObject * )
{
    PyErr_Format( PyExc_TypeError, "Cannot assign an item in a read-only PySTLSequence" );
    return -1;
}

int StockMatKindSequence::pySeq_ass_slice( Py_ssize_t, Py_ssize_t, PyObject * )
{
    PyErr_Format( PyExc_TypeError, "Cannot assign a slice in a read-only PySTLSequence" );
    return -1;
}

int StockMatKindSequence::pySeq_contains( PyObject * object )
{
    // 0077ede0 converts with setData(uint32) and clears a conversion failure.
    uint32 value = 0;
    if (Script::setData( object, value, "" ) != 0)
    {
        PyErr_Clear();
        return 0;
    }
    std::shared_ptr<SensorState> state = state_.lock();
    if (!state || !state->valid())
        return 0;
    return std::find( state->matKinds.begin(), state->matKinds.end(), value ) !=
        state->matKinds.end();
}

PyObject * StockMatKindSequence::pySeq_inplace_concat( PyObject * )
{
    PyErr_Format( PyExc_TypeError, "Cannot do += on a read-only PySTLSequence" );
    return NULL;
}

PyObject * StockMatKindSequence::pySeq_inplace_repeat( Py_ssize_t )
{
    PyErr_Format( PyExc_TypeError, "Cannot do *= on a read-only PySTLSequence" );
    return NULL;
}

PyObject * StockMatKindSequence::pyRepr()
{
    const int entered = Py_ReprEnter( this );
    if (entered != 0)
        return entered > 0 ? PyString_FromString( "[...]" ) : NULL;
    PyObject * text = PyString_FromString( "[" );
    PyObject * comma = PyString_FromString( ", " );
    for (int index = 0; index < this->size() && text != NULL; ++index)
    {
        if (index > 0)
            PyString_Concat( &text, comma );
        PyObject * item = this->at( index );
        PyString_ConcatAndDel( &text, PyObject_Repr( item ) );
        Py_DECREF( item );
    }
    Py_XDECREF( comma );
    PyString_ConcatAndDel( &text, PyString_FromString( "]" ) );
    Py_ReprLeave( this );
    return text;
}

PY_TYPEOBJECT_SPECIALISE_SEQ( StockMatKindSequence, &StockMatKindSequence::s_seq_methods_ )
PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME( StockMatKindSequence, &Super::s_type_, "PySTLSequence" )
PY_BEGIN_METHODS( StockMatKindSequence )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( StockMatKindSequence )
    PY_ATTRIBUTE( length )
PY_END_ATTRIBUTES()

class PyTerrainMatKindSensor : public StockComponentWrapper
{
    Py_Header( PyTerrainMatKindSensor, StockComponentWrapper )
public:
    PyTerrainMatKindSensor( const std::shared_ptr<StockComponentState> & state, bool owning ) :
        StockComponentWrapper( state, &s_type_, owning ) {}
    std::shared_ptr<SensorState> state() const
    {
        std::shared_ptr<SensorState> value =
            std::dynamic_pointer_cast<SensorState>( this->component() );
        if (!value)
            PyErr_SetString( PyExc_ValueError, "TerrainMatKindSensor has been destroyed" );
        return value;
    }
    PyObject * pyGet_matKinds()
    {
        // 0077ee40 never checks the component; a destroyed one reads as empty.
        return new StockMatKindSequence(
            std::dynamic_pointer_cast<SensorState>( this->component() ) );
    }
    PY_RO_ATTRIBUTE_SET( matKinds )
    PY_METHOD_DECLARE( py_setLodLink )
    PY_METHOD_DECLARE( py_setLodSettings )
    PY_FACTORY_DECLARE()
};

PyObject * SensorState::wrap( bool owning )
{
    return new PyTerrainMatKindSensor( this->shared_from_this(), owning );
}

namespace
{
// Script::setData( PyObject *, BW::vector<Vector3> & ) as inlined at 00642450.
// Its unchecked PySequence_Size/GetItem failures are returned as errors here.
bool readPoints( PyObject * object, std::vector<std::array<float, 3> > & points )
{
    const char * name = "() argument 2";
    if (!PySequence_Check( object ))
    {
        PyErr_Format( PyExc_TypeError, "%s must be set to a sequence of %s", name,
            "class BW::Vector3" );
        return false;
    }
    const BW::string element = BW::string( name ) + " element";
    const Py_ssize_t size = PySequence_Size( object );
    if (size < 0)
        return false;
    points.resize( size );
    for (Py_ssize_t index = 0; index < size; ++index)
    {
        PyObject * item = PySequence_GetItem( object, index );
        if (!item)
            return false;
        Vector3 value;
        const int failed = Script::setData( item, value, element.c_str() );
        Py_DECREF( item );
        if (failed)
            return false;
        points[index] = { { value.x, value.y, value.z } };
    }
    return true;
}
}

PyObject * PyTerrainMatKindSensor::pyNew( PyObject * args )
{
    if (PyTuple_Size( args ) != 3)
    {
        // 0068e100 strips the Ptr suffix from MatrixProviderPtr.
        PyErr_SetString( PyExc_TypeError, "PyTerrainMatKindSensor() expects 3 arguments of "
            "types MatrixProvider, BW::vector<Vector3>, float" );
        return NULL;
    }
    // 0069ef50: None is an empty MatrixProviderPtr.
    PyObject * source = PyTuple_GET_ITEM( args, 0 );
    MatrixProviderPtr provider;
    if (source != Py_None)
    {
        if (!MatrixProvider::Check( source ))
        {
            PyErr_Format( PyExc_TypeError, "%s must be set to a MatrixProvider or None", ARGUMENT_1 );
            return NULL;
        }
        provider = static_cast<MatrixProvider *>( source );
    }
    std::vector<std::array<float, 3> > points;
    if (!readPoints( PyTuple_GET_ITEM( args, 1 ), points ))
        return NULL;
    float scanLength;
    if (Script::setData( PyTuple_GET_ITEM( args, 2 ), scanLength, "() argument 3" ) != 0)
        return NULL;
    // 0077dac0 deactivates the pooled component before 0077e460 wraps it.
    return new PyTerrainMatKindSensor(
        std::make_shared<SensorState>( provider, points, scanLength ), true );
}

PyObject * PyTerrainMatKindSensor::py_setLodLink( PyObject * args )
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
        PyErr_Format( PyExc_TypeError, "%s must be set to a PyDataLink", ARGUMENT_1 );
        return NULL;
    }
    // 00767c9b dereferences the component without a check after destroy.
    std::shared_ptr<SensorState> value = this->state();
    if (!value)
        return NULL;
    value->lodLink = link == Py_None ? NULL : link;
    Py_RETURN_NONE;
}

PyObject * PyTerrainMatKindSensor::py_setLodSettings( PyObject * args )
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
        PyErr_Format( PyExc_TypeError, "%s must be set to a LodSettings tuple", ARGUMENT_1 );
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

PY_TYPEOBJECT( PyTerrainMatKindSensor )
// Method table 01a12d1c; activate/deactivate/destroy/isOwning are inherited.
PY_BEGIN_METHODS( PyTerrainMatKindSensor )
    PY_METHOD( setLodLink )
    PY_METHOD( setLodSettings )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyTerrainMatKindSensor )
    PY_ATTRIBUTE( matKinds )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyTerrainMatKindSensor, "TerrainMatKindSensor", Vehicular )

void tickStockTerrainMatKindSensors( float frameTime )
{
    // 007965d0 is the same LowFrequencyUpdater body as WaterSensor's manager.
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
        // 013efb20 restarts the processed entry.
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
class TerrainSensorScriptShutdown : public Script::FiniTimeJob
{
    virtual void fini()
    {
        schedule.clear();
        scheduled.clear();
    }
} terrainSensorScriptShutdown;
}
BW_END_NAMESPACE
