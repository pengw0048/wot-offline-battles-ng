#include "pch.hpp"
#include "stock_vehicle_wheels.hpp"
#include "stock_vehicle_components.hpp"
#include "stock_vehicle_spline_tracks.hpp"
#include "stock_vehicle_visuals.hpp"
#include "vehicle_appearance.hpp"

#include "pyscript/script_math.hpp"

#include <cfloat>
#include <cmath>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
int StockVehicleWheels_token = 1;

namespace
{
// Exact 0078c7c0/0078c6e0 record: the angle starts at zero, the radius is
// stored with its float reciprocal and only addWheel has a leading factor.
struct WheelRecord
{
    BW::string node;
    bool linked = false;
    Matrix authored = Matrix::identity;
    float angle = 0.f;
    float radius = 0.f;
    float inverseRadius = 0.f;
    float leadingFactor = 0.f;
    bool left = false;
    bool leading = false;
};

struct WheelsSettings
{
    // 0077efe0 defaults: FLT_MAX distance and the unsigned maximum priority.
    float maxDistance = FLT_MAX;
    uint32 maxPriority = 0xffffffffU;
};

// 0078c3b0 advances a side unless 0078d880 stored its stop bit.
struct WheelsMotion
{
    std::vector<WheelRecord> wheels;
    unsigned stopped = 0;

    void advance( const Vector4 & movement, const bool hasSpline[2], const float spline[2] )
    {
        for (size_t index = 0; index < wheels.size(); ++index)
        {
            WheelRecord & wheel = wheels[index];
            if (stopped & (wheel.left ? 1U : 2U))
                continue;
            const int side = wheel.left ? 0 : 1;
            if (wheel.leading && hasSpline[side])
            {
                // A leading wheel follows the absolute spline phase.
                wheel.angle = wheel.leadingFactor * spline[side];
                continue;
            }
            // movementInfo y/z are per-frame left/right track displacements.
            const float speed = wheel.left ? movement.y : movement.z;
            const float angle = wheel.inverseRadius * speed + wheel.angle;
            wheel.angle = fmodf( angle, 6.2831855f );
        }
    }

    // D3DXMatrixRotationX(angle) * authored local, written into the compound
    // node-local array that the compound then evaluates into world matrices.
    static Matrix pose( const WheelRecord & wheel )
    {
        Matrix rotation;
        rotation.setRotateX( wheel.angle );
        Matrix local;
        local.multiply( rotation, wheel.authored );
        return local;
    }
};

bool lodEligible( const WheelsSettings & settings, uint32 priority, float distance )
{
    // 00781f70 rejects only an ordered larger distance or priority.
    return !(settings.maxDistance < distance) && !(settings.maxPriority < priority);
}
}

bool readStockLodSettings( PyObject * object, float & maxDistance, uint32 & maxPriority )
{
    // Exact 0077efe0: a tuple of at most two items. Missing items and a None
    // priority keep the FLT_MAX / UINT32_MAX defaults.
    if (!PyTuple_Check( object ))
    {
        PyErr_SetString( PyExc_TypeError, "LodSettings must be a tuple" );
        return false;
    }
    const Py_ssize_t count = PyTuple_GET_SIZE( object );
    if (count >= 3)
    {
        PyErr_SetString( PyExc_TypeError, "Invalid args count" );
        return false;
    }
    maxDistance = FLT_MAX;
    maxPriority = 0xffffffffU;
    if (count >= 1 && Script::setData( PyTuple_GET_ITEM( object, 0 ), maxDistance,
            "maxLodDistance" ) != 0)
        return false;
    if (count >= 2 && PyTuple_GET_ITEM( object, 1 ) != Py_None &&
        Script::setData( PyTuple_GET_ITEM( object, 1 ), maxPriority, "maxPriority" ) != 0)
        return false;
    return true;
}

namespace
{

struct WheelsState : public StockComponentState, public StockWheelsDataProvider
{
    explicit WheelsState( PyObjectPlus * model ) : compound( model ) {}
    virtual void activate();
    virtual void deactivate();
    virtual void destroy();
    virtual PyObject * wrap( bool owning );
    void link( WheelRecord & wheel );
    void update();

    virtual size_t wheelCount() const { return valid_ ? motion.wheels.size() : 0; }
    virtual float wheelRadius( size_t index ) const { return motion.wheels[index].radius; }
    virtual bool wheelIsLeft( size_t index ) const { return motion.wheels[index].left; }
    virtual bool wheelWorld( size_t index, Matrix & world ) const
    {
        if (!valid_ || index >= motion.wheels.size() || !motion.wheels[index].linked)
            return false;
        PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
        Matrix authored;
        Matrix parent;
        return model && stockVehicleCompoundNode( model.get(),
            motion.wheels[index].node, authored, parent, world );
    }

    WeakPyPtr<PyObjectPlus> compound;
    WheelsMotion motion;
    WheelsSettings settings;
    PyObjectPtr lodLink;
    Vector4ProviderPtr movement;
    StockSplineMovementReader spline[2];
    bool active = false;
};
std::vector<std::weak_ptr<WheelsState> > activeWheels;

void WheelsState::activate()
{
    if (!valid_ || active)
        return;
    active = true;
    activeWheels.push_back( std::static_pointer_cast<WheelsState>( this->shared_from_this() ) );
}
void WheelsState::deactivate()
{
    if (!active)
        return;
    active = false;
    for (size_t index = 0; index < activeWheels.size(); ++index)
        if (activeWheels[index].lock().get() == this)
        {
            // 0078d2d0 swaps the deactivated record out of the active prefix.
            activeWheels[index] = activeWheels.back();
            activeWheels.pop_back();
            break;
        }
}
void WheelsState::destroy()
{
    this->deactivate();
    valid_ = false;
    compound = WeakPyPtr<PyObjectPlus>();
    lodLink = NULL;
    movement = Vector4ProviderPtr();
    spline[0] = spline[1] = StockSplineMovementReader();
    motion.wheels.clear();
}

void WheelsState::link( WheelRecord & wheel )
{
    // 0078d560 copies the compound node's current local matrix. A missing
    // node keeps the identity basis and is reported, not raised.
    PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
    Matrix parent;
    Matrix world;
    wheel.linked = model && stockVehicleCompoundNode( model.get(), wheel.node,
        wheel.authored, parent, world );
    if (!wheel.linked)
    {
        wheel.authored = Matrix::identity;
        ERROR_MSG( "WheelsAnimator::linkWithCompound Cannot find node with name: %s in model\n",
            wheel.node.c_str() );
    }
}

void WheelsState::update()
{
    if (!valid_ || !active)
        return;
    if (lodLink)
    {
        uint32 priority;
        float distance;
        if (!sampleStockLodLink( lodLink.get(), priority, distance ))
        {
            ERROR_MSG( "WheelsAnimator lost its native LOD state\n" );
            this->deactivate();
            return;
        }
        if (!lodEligible( settings, priority, distance ))
            return;
    }
    // 021538c0 is the zero Vector4 used when movementInfo is unset.
    Vector4 value( 0.f, 0.f, 0.f, 0.f );
    Vector4ProviderPtr provider = movement;
    if (provider)
        provider->output( value );
    bool hasSpline[2] = { false, false };
    float splineValue[2] = { 0.f, 0.f };
    for (int side = 0; side < 2; ++side)
    {
        StockSplineMovementReader reader = spline[side];
        hasSpline[side] = reader && reader( splineValue[side] );
    }
    if (!valid_ || !active)
        return;
    motion.advance( value, hasSpline, splineValue );
    PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
    for (size_t index = 0; index < motion.wheels.size(); ++index)
    {
        const WheelRecord & wheel = motion.wheels[index];
        // The original writes node index -1 out of bounds; contain it here.
        if (!wheel.linked)
            continue;
        if (!model || !writeStockVehicleCompoundNodeLocal( model.get(), wheel.node,
                WheelsMotion::pose( wheel ) ))
        {
            ERROR_MSG( "WheelsAnimator lost its active compound\n" );
            this->deactivate();
            return;
        }
    }
}
}

class WheelsAnimator : public StockComponentWrapper
{
    Py_Header( WheelsAnimator, StockComponentWrapper )
public:
    WheelsAnimator( const std::shared_ptr<StockComponentState> & state, bool owning ) :
        StockComponentWrapper( state, &s_type_, owning ) {}
    std::shared_ptr<WheelsState> state() const
    {
        std::shared_ptr<WheelsState> value =
            std::dynamic_pointer_cast<WheelsState>( this->component() );
        if (!value)
            PyErr_SetString( PyExc_ValueError, "WheelsAnimator has been destroyed" );
        return value;
    }
    PY_METHOD_DECLARE( py_addWheel )
    PY_METHOD_DECLARE( py_addWheelGroup )
    PY_METHOD_DECLARE( py_setLodLink )
    PY_METHOD_DECLARE( py_setLodSettings )
    PY_METHOD_DECLARE( py_setMovementInfo )
    PY_METHOD_DECLARE( py_setSplineTrackMovementData )
    PY_METHOD_DECLARE( py_stopAnimatingWheels )
    PY_FACTORY_DECLARE()
};

PyObject * WheelsState::wrap( bool owning )
{
    return new WheelsAnimator( this->shared_from_this(), owning );
}

PyObject * WheelsAnimator::pyNew( PyObject * args )
{
    PyObject * model;
    if (!PyArg_ParseTuple( args, "O:WheelsAnimator", &model ))
        return NULL;
    uint32 space;
    BoundingBox bounds;
    if (!stockVehicleCompoundContext( model, space, bounds ))
    {
        PyErr_SetString( PyExc_TypeError, "WheelsAnimator requires an active CompoundModel" );
        return NULL;
    }
    // 0076d730 inserts an active pool record, then calls IComponent
    // deactivate; the appearance's ComponentSystem later activates it.
    return new WheelsAnimator(
        std::make_shared<WheelsState>( static_cast<PyObjectPlus *>( model ) ), true );
}

PyObject * WheelsAnimator::py_addWheel( PyObject * args )
{
    PyObject * sideObject;
    PyObject * radiusObject;
    PyObject * nameObject;
    PyObject * leadingObject;
    PyObject * syncObject;
    if (!PyArg_ParseTuple( args, "OOOOO:addWheel", &sideObject, &radiusObject,
            &nameObject, &leadingObject, &syncObject ))
        return NULL;
    WheelRecord wheel;
    float syncAngle;
    if (Script::setData( sideObject, wheel.left ) != 0 ||
        Script::setData( radiusObject, wheel.radius ) != 0 ||
        Script::setData( nameObject, wheel.node ) != 0 ||
        Script::setData( leadingObject, wheel.leading ) != 0 ||
        Script::setData( syncObject, syncAngle ) != 0)
        return NULL;
    std::shared_ptr<WheelsState> value = this->state();
    if (!value)
        return NULL;
    // 0078d080 reports a non-positive radius and still adds the wheel.
    if (wheel.radius <= 0.f)
        ERROR_MSG( "addWheel - invalid <radius> parameter. It must be great then 0.\n" );
    wheel.inverseRadius = 1.f / wheel.radius;
    wheel.leadingFactor = syncAngle * 0.017453292f;
    value->link( wheel );
    value->motion.wheels.push_back( wheel );
    Py_RETURN_NONE;
}

PyObject * WheelsAnimator::py_addWheelGroup( PyObject * args )
{
    PyObject * sideObject;
    PyObject * radiusObject;
    PyObject * namesObject;
    if (!PyArg_ParseTuple( args, "OOO:addWheelGroup", &sideObject, &radiusObject, &namesObject ))
        return NULL;
    bool left;
    float radius;
    if (Script::setData( sideObject, left ) != 0 || Script::setData( radiusObject, radius ) != 0)
        return NULL;
    PyObjectPtr sequence( PySequence_Fast( namesObject, "addWheelGroup requires a name sequence" ),
        PyObjectPtr::STEAL_REFERENCE );
    if (!sequence)
        return NULL;
    std::vector<BW::string> names;
    const Py_ssize_t count = PySequence_Fast_GET_SIZE( sequence.get() );
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        BW::string name;
        if (Script::setData( PySequence_Fast_GET_ITEM( sequence.get(), index ), name ) != 0)
            return NULL;
        names.push_back( name );
    }
    std::shared_ptr<WheelsState> value = this->state();
    if (!value)
        return NULL;
    // 0078d150 reports both malformed inputs and continues.
    if (radius <= 0.f)
        ERROR_MSG( "addWheel - invalid <radius> parameter. It must be great then 0.\n" );
    if (names.empty())
        ERROR_MSG( "addWheel - invalid <nodeNames> parameter. It is empty.\n" );
    for (size_t index = 0; index < names.size(); ++index)
    {
        WheelRecord wheel;
        wheel.node = names[index];
        wheel.left = left;
        wheel.radius = radius;
        wheel.inverseRadius = 1.f / radius;
        value->link( wheel );
        value->motion.wheels.push_back( wheel );
    }
    Py_RETURN_NONE;
}

PyObject * WheelsAnimator::py_setLodLink( PyObject * args )
{
    PyObject * link;
    if (!PyArg_ParseTuple( args, "O:setLodLink", &link ))
        return NULL;
    std::shared_ptr<WheelsState> value = this->state();
    if (!value)
        return NULL;
    uint32 priority;
    float distance;
    // 0077f160 accepts None or a LodState DataLink; a dead producer cannot
    // be retained here because its sample is already unavailable.
    if (link != Py_None && !sampleStockLodLink( link, priority, distance ))
    {
        PyErr_SetString( PyExc_TypeError, "lodLink must be set to a PyDataLink" );
        return NULL;
    }
    // 0077f160 clears the functor for None.
    value->lodLink = link == Py_None ? NULL : link;
    Py_RETURN_NONE;
}

PyObject * WheelsAnimator::py_setLodSettings( PyObject * args )
{
    PyObject * object;
    if (!PyArg_ParseTuple( args, "O:setLodSettings", &object ))
        return NULL;
    WheelsSettings settings;
    if (!readStockLodSettings( object, settings.maxDistance, settings.maxPriority ))
        return NULL;
    std::shared_ptr<WheelsState> value = this->state();
    if (!value)
        return NULL;
    value->settings = settings;
    Py_RETURN_NONE;
}

PyObject * WheelsAnimator::py_setMovementInfo( PyObject * args )
{
    PyObject * object;
    if (!PyArg_ParseTuple( args, "O:setMovementInfo", &object ))
        return NULL;
    Vector4ProviderPtr provider;
    if (Script::setData( object, provider ) != 0)
        return NULL;
    std::shared_ptr<WheelsState> value = this->state();
    if (!value)
        return NULL;
    value->movement = provider;
    Py_RETURN_NONE;
}

PyObject * WheelsAnimator::py_setSplineTrackMovementData( PyObject * args )
{
    PyObject * tracks[2];
    if (!PyArg_ParseTuple( args, "OO:setSplineTrackMovementData", &tracks[0], &tracks[1] ))
        return NULL;
    // 00af9890 accepts None or a WGSplineTrack; none exists without the
    // spline-track manager, so both functors are cleared (0078d790).
    for (int side = 0; side < 2; ++side)
        if (tracks[side] != Py_None && !isStockSplineTrack( tracks[side] ))
        {
            PyErr_Format( PyExc_TypeError,
                "() argument %d must be set to a WGSplineTrack or None", side + 1 );
            return NULL;
        }
    std::shared_ptr<WheelsState> value = this->state();
    if (!value)
        return NULL;
    value->spline[0] = value->spline[1] = StockSplineMovementReader();
    Py_RETURN_NONE;
}

PyObject * WheelsAnimator::py_stopAnimatingWheels( PyObject * args )
{
    PyObject * leftObject;
    PyObject * rightObject;
    if (!PyArg_ParseTuple( args, "OO:stopAnimatingWheels", &leftObject, &rightObject ))
        return NULL;
    bool left;
    bool right;
    if (Script::setData( leftObject, left ) != 0 || Script::setData( rightObject, right ) != 0)
        return NULL;
    std::shared_ptr<WheelsState> value = this->state();
    if (!value)
        return NULL;
    // 0078d880 replaces both bits; it does not accumulate earlier requests.
    value->motion.stopped = (left ? 1U : 0U) | (right ? 2U : 0U);
    Py_RETURN_NONE;
}

PY_TYPEOBJECT( WheelsAnimator )
PY_BEGIN_METHODS( WheelsAnimator )
    PY_METHOD( addWheel )
    PY_METHOD( addWheelGroup )
    PY_METHOD( setLodLink )
    PY_METHOD( setLodSettings )
    PY_METHOD( setMovementInfo )
    PY_METHOD( setSplineTrackMovementData )
    PY_METHOD( stopAnimatingWheels )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( WheelsAnimator )
PY_END_ATTRIBUTES()
PY_FACTORY( WheelsAnimator, Vehicular )

std::shared_ptr<StockWheelsDataProvider> stockWheelsDataProvider( PyObject * animator )
{
    if (!animator || !WheelsAnimator::Check( animator ))
        return std::shared_ptr<StockWheelsDataProvider>();
    std::shared_ptr<WheelsState> value = std::dynamic_pointer_cast<WheelsState>(
        static_cast<WheelsAnimator *>( animator )->component() );
    return value;
}

void tickStockVehicleWheels( float )
{
    // The manager's elapsed-time read is discarded; movementInfo is already
    // the per-frame displacement.
    const std::vector<std::weak_ptr<WheelsState> > snapshot = activeWheels;
    for (size_t index = 0; index < snapshot.size(); ++index)
    {
        std::shared_ptr<WheelsState> value = snapshot[index].lock();
        if (value)
            value->update();
    }
}

namespace
{
class WheelsScriptShutdown : public Script::FiniTimeJob
{
    virtual void fini() { activeWheels.clear(); }
} wheelsScriptShutdown;
}
BW_END_NAMESPACE
