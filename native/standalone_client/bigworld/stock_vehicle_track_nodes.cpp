#include "pch.hpp"
#include "stock_vehicle_track_nodes.hpp"
#include "stock_vehicle_components.hpp"
#include "stock_vehicle_visuals.hpp"
#include "stock_vehicle_wheels.hpp"
#include "vehicle_appearance.hpp"

#include "moo/renderer.hpp"
#include "pyscript/script_math.hpp"

#include <cfloat>
#include <cmath>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
int StockVehicleTrackNodes_token = 1;

namespace
{
float trackCos( float value ) { return static_cast<float>( std::cos( static_cast<double>( value ) ) ); }
float trackSin( float value ) { return static_cast<float>( std::sin( static_cast<double>( value ) ) ); }

// Exact #1513 analytic damped oscillator (00f64800 family). The phase is
// frequency * time; changing a coefficient rebases the amplitudes at t = 0.
struct TrackOscillator
{
    float mass = 1.f;
    float inverseMass = 1.f;
    float stiffness = 1.f;
    float decay = 0.f;
    float force = -9.8f;
    float rest = -9.8f;
    float frequency = 1.f;
    float cosine = 0.f;
    float sine = 0.f;
    float maxAmplitude = 0.f;
    float time = 0.f;

    void limit( float first, float second )
    {
        const float length = sqrtf( first * first + second * second );
        if (maxAmplitude < length)
        {
            const float scale = maxAmplitude / length;
            cosine *= scale;
            sine *= scale;
        }
    }
    void setMaxAmplitude( float value )
    {
        // 00f64c30
        maxAmplitude = value;
        this->limit( cosine, sine );
    }
    void setMass( float value )
    {
        // 00f64b20
        const float inverse = 1.f / value;
        const float next = stiffness * inverse;
        inverseMass = inverse;
        mass = value;
        frequency = next;
        rest = force / (next * next);
        const float c = trackCos( next * time );
        const float s = trackSin( next * time );
        const float oldCosine = cosine;
        time = 0.f;
        cosine = (sine * s + cosine * c + rest) - rest;
        sine = (frequency * sine * c - frequency * oldCosine * s) / frequency;
        this->limit( cosine, sine );
    }
    void setForce( float value )
    {
        // 00f64af0
        force = value;
        rest = value / (frequency * frequency);
    }
    void setStiffness( float value )
    {
        // 00f649f0
        const float c = trackCos( frequency * time );
        const float s = trackSin( frequency * time );
        const float old = frequency;
        const float next = inverseMass * value;
        time = 0.f;
        stiffness = value;
        frequency = next;
        const float nextRest = force / (next * next);
        const float nextSine = (sine * old * c - old * cosine * s) / next;
        const float nextCosine = (sine * s + cosine * c + rest) - nextRest;
        rest = nextRest;
        sine = nextSine;
        cosine = nextCosine;
        this->limit( sine, cosine );
    }
    void reset() { sine = cosine = 0.f; }
    void update( float elapsed )
    {
        // 00f64c80
        time = elapsed + time;
        const float factor = expf( -(decay * elapsed) );
        cosine *= factor;
        sine *= factor;
    }
    float value() const
    {
        const float phase = time * frequency;
        return sine * trackSin( phase ) + cosine * trackCos( phase ) + rest;
    }
    float velocity() const
    {
        const float phase = time * frequency;
        return sine * frequency * trackCos( phase ) - cosine * frequency * trackSin( phase );
    }
    void addImpulse( float value )
    {
        // 00f64850
        const float c = trackCos( frequency * time );
        const float s = trackSin( frequency * time );
        const float old = cosine;
        time = 0.f;
        const float nextCosine = (sine * s + cosine * c + rest) - rest;
        const float nextSine = ((sine * frequency * c - frequency * old * s) +
            inverseMass * value) / frequency;
        cosine = nextCosine;
        sine = nextSine;
        this->limit( nextSine, nextCosine );
    }
};

// 0078ea00 node record. Indices are -1 until 0078f430 links the node.
struct TrackNode
{
    TrackOscillator spring;
    float damping = 0.f;
    float elasticity = 0.f;
    float forwardCoefficient = 0.f;
    float backwardCoefficient = 0.f;
    float blend = 0.f;
    BW::string name;
    BW::string leftName;
    BW::string rightName;
    bool linked = false;
    // Compound local-array value recorded by addTrackNode (record +0x44).
    Matrix basis = Matrix::identity;
    Vector3 previousPosition = Vector3::zero();
    Vector3 previousVelocity = Vector3::zero();
    Vector3 wheelPosition = Vector3::zero();
    float previousOffset = 0.f;
    float offset = 0.f;
    float wheelDistanceSquared = 0.f;
    int wheel = -1;
    int left = -1;
    int right = -1;
    bool isLeft = false;
};

const float TRACK_NODE_MASS = 60.f;          // 01a12f2c
const float TRACK_NODE_RESTITUTION = 0.3f;   // 01a12f30
const float TRACK_NODE_VERTICAL_GAIN = -1.f; // 01a12f34
const float TRACK_NODE_BLEND_GAIN = 10.f;    // 01a12f38
// 01d325f8 is a zero-filled .bss float with no writer in #1513.
const float TRACK_NODE_LONGITUDINAL_GAIN = 0.f;

Vector3 rowVector( const Matrix & matrix, int row )
{
    return Vector3( matrix[row][0], matrix[row][1], matrix[row][2] );
}

// Node local with its spring offset applied along its own local Y.
Matrix offsetLocal( const Matrix & basis, float offset )
{
    Matrix local = basis;
    const Vector3 shift = basis.applyVector( Vector3( 0.f, offset, 0.f ) );
    local[3][0] += shift.x;
    local[3][1] += shift.y;
    local[3][2] += shift.z;
    return local;
}

struct TrackNodesState : public StockComponentState
{
    TrackNodesState( PyObjectPlus * model, const BW::string & hull ) :
        compound( model ), hullName( hull ) {}
    virtual void activate();
    virtual void deactivate();
    virtual void destroy();
    virtual PyObject * wrap( bool owning );

    bool node( const BW::string & name, Matrix & authored, Matrix & parent, Matrix & world ) const
    {
        PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
        return model && stockVehicleCompoundNode( model.get(), name, authored, parent, world );
    }
    // Compound node-local array value, read without evaluating providers.
    bool local( const BW::string & name, Matrix & value ) const
    {
        PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
        return model && stockVehicleCompoundNodeLocal( model.get(), name, value );
    }
    // Compound node index and its parent index (-1 for the root).
    bool identity( const BW::string & name, int & index, int & parent ) const
    {
        PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
        return model && stockVehicleCompoundNodeIdentity( model.get(), name, index, parent );
    }
    bool rootInverse( Matrix & inverse ) const
    {
        Matrix authored;
        Matrix parent;
        Matrix world;
        return this->node( BW::string(), authored, parent, world ) && inverse.invert( world );
    }
    bool wheelInRoot( size_t index, const Matrix & inverseRoot, Matrix & result ) const
    {
        std::shared_ptr<StockWheelsDataProvider> source = wheels.lock();
        Matrix world;
        if (!source || index >= source->wheelCount() || !source->wheelWorld( index, world ))
            return false;
        result.multiply( world, inverseRoot );
        return true;
    }
    void link();
    void linkWheel( const Matrix & inverseRoot, TrackNode & node );
    void contact( const TrackNode & node, const Matrix & inverseRoot, float elapsed,
        float & penetration, float & impulse ) const;
    void simulate( float elapsed );
    void captureHull();
    void addNode( TrackNode node, float offset );
    bool publish( bool forward );

    WeakPyPtr<PyObjectPlus> compound;
    BW::string hullName;
    bool hullLinked = false;
    int hullIndex = -1;
    Matrix hullLocal = Matrix::identity;
    std::vector<TrackNode> nodes;
    std::weak_ptr<StockWheelsDataProvider> wheels;
    float maxDistance = FLT_MAX;
    uint32 maxPriority = 0xffffffffU;
    PyObjectPtr lodLink;
    float thickness = 0.f;
    float maxAmplitude = 0.f;
    float maxOffset = 0.f;
    float gravity = 0.f;
    float warmup = 0.f;
    unsigned eligibility = 1;
    bool resetVelocity = false;
    bool linked = false;
    bool active = false;
};
std::vector<std::weak_ptr<TrackNodesState> > activeTrackNodes;

bool forwardPipeline()
{
    // 0078e7b0/0078d8e0 test Renderer+8 == 1, the BigWorld.isForwardPipeline value.
    return Renderer::pInstance() &&
        Renderer::instance().pipelineType() == IRendererPipeline::TYPE_FORWARD_SHADING;
}

void TrackNodesState::linkWheel( const Matrix & inverseRoot, TrackNode & node )
{
    // 0078f1a0 picks the highest same-side wheel below the node whose
    // radius spans the node along root-space Z.
    std::shared_ptr<StockWheelsDataProvider> source = wheels.lock();
    Matrix authored;
    Matrix parent;
    Matrix world;
    if (!source || !node.linked || !this->node( node.name, authored, parent, world ))
        return;
    Matrix nodeRoot;
    nodeRoot.multiply( world, inverseRoot );
    const Vector3 position = nodeRoot.applyToOrigin();
    for (size_t index = 0; index < source->wheelCount(); ++index)
    {
        if (source->wheelIsLeft( index ) != node.isLeft)
            continue;
        Matrix wheel;
        if (!this->wheelInRoot( index, inverseRoot, wheel ))
            continue;
        const Vector3 centre = wheel.applyToOrigin();
        const float radius = source->wheelRadius( index );
        if (!(centre.y < position.y) || !(centre.z - radius < position.z) ||
            !(position.z < centre.z + radius))
            continue;
        if (node.wheel >= 0)
        {
            Matrix current;
            if (this->wheelInRoot( static_cast<size_t>( node.wheel ), inverseRoot, current ) &&
                centre.y < current.applyToOrigin().y)
                continue;
        }
        node.wheel = static_cast<int>( index );
        node.wheelPosition = centre;
        node.wheelDistanceSquared = (centre.z - position.z) * (centre.z - position.z);
    }
}

void TrackNodesState::link()
{
    // 0078f430 runs once, at the first activation.
    Matrix inverseRoot;
    if (!this->rootInverse( inverseRoot ))
        inverseRoot = Matrix::identity;
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        TrackNode & node = nodes[index];
        Matrix authored;
        Matrix parent;
        Matrix world;
        if (node.linked && this->node( node.name, authored, parent, world ))
            node.previousPosition = world.applyToOrigin();
        // Empty sibling names never match a configured node name.
        for (size_t other = 0; other < nodes.size(); ++other)
        {
            if (nodes[other].name == node.leftName)
                node.left = static_cast<int>( other );
            if (nodes[other].name == node.rightName)
                node.right = static_cast<int>( other );
        }
        this->linkWheel( inverseRoot, node );
    }
}

void TrackNodesState::contact( const TrackNode & node, const Matrix & inverseRoot,
    float elapsed, float & penetration, float & impulse ) const
{
    // 0078ee30
    penetration = 0.f;
    impulse = 0.f;
    std::shared_ptr<StockWheelsDataProvider> source = wheels.lock();
    Matrix wheel;
    Matrix authored;
    Matrix parent;
    Matrix world;
    if (!source || node.wheel < 0 || !node.linked ||
        !this->wheelInRoot( static_cast<size_t>( node.wheel ), inverseRoot, wheel ) ||
        !this->node( node.name, authored, parent, world ))
        return;
    Matrix parentRoot;
    parentRoot.multiply( parent, inverseRoot );
    Matrix nodeRoot;
    nodeRoot.multiply( offsetLocal( node.basis, node.offset ), parentRoot );
    const float radius = source->wheelRadius( static_cast<size_t>( node.wheel ) );
    const float wheelY = wheel[3][1];
    const float surface = sqrtf( radius * radius - node.wheelDistanceSquared ) + wheelY +
        thickness * 0.5f;
    const float nodeY = nodeRoot[3][1];
    if (!(nodeY < surface))
        return;
    const float rate = (wheelY - node.wheelPosition.y) / elapsed;
    float wheelSpeed = -1.f;
    if (-1.f <= rate)
        wheelSpeed = 1.f <= rate ? 1.f : rate;
    const float speed = node.spring.velocity();
    if (speed - wheelSpeed < -0.1f)
        impulse = ((wheelSpeed - TRACK_NODE_RESTITUTION * speed) - speed) * node.spring.mass;
    penetration = surface - nodeY;
}

void TrackNodesState::simulate( float elapsed )
{
    // 0078fa70
    if (!(FLT_EPSILON <= elapsed))
        return;
    warmup = warmup - elapsed * 10.f;
    if (warmup <= 0.f)
        warmup = 0.f;
    Matrix inverseRoot = Matrix::identity;
    Matrix rootWorld;
    rootWorld.setZero();
    {
        Matrix authored;
        Matrix parent;
        if (this->node( BW::string(), authored, parent, rootWorld ))
            inverseRoot.invert( rootWorld );
        else
            rootWorld.setZero();
    }
    const float force = gravity * rootWorld[1][1];
    const float inverseElapsed = 1.f / elapsed;
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        TrackNode & node = nodes[index];
        if (node.spring.mass != TRACK_NODE_MASS)
            node.spring.setMass( TRACK_NODE_MASS );
        node.spring.decay = node.damping + warmup;
        node.spring.setForce( force );
        Matrix authored;
        Matrix parent;
        Matrix world;
        if (!node.linked || !this->node( node.name, authored, parent, world ))
            continue;
        Matrix rest;
        rest.multiply( node.basis, parent );
        const Vector3 position = rest.applyToOrigin();
        Vector3 displacement = position - node.previousPosition;
        if ((eligibility & 3U) == 1U || resetVelocity)
            displacement = Vector3::zero();
        Vector3 velocity = displacement * inverseElapsed;
        Matrix inverse;
        inverse.invert( rest );
        displacement = inverse.applyVector( displacement );
        velocity = inverse.applyVector( velocity );
        float blend = displacement.z * TRACK_NODE_BLEND_GAIN + node.blend;
        node.blend = blend;
        blend = 0.f <= blend ? (1.f <= blend ? 1.f : blend) : 0.f;
        node.blend = blend;
        const float stiffness = ((node.forwardCoefficient - node.backwardCoefficient) * blend +
            node.backwardCoefficient) * node.elasticity;
        if (FLT_EPSILON < fabsf( stiffness - node.spring.stiffness ))
            node.spring.setStiffness( stiffness );
        node.spring.update( elapsed );
        const float leftOffset = node.left >= 0 ? nodes[node.left].previousOffset : 0.f;
        const float rightOffset = node.right >= 0 ? nodes[node.right].previousOffset : 0.f;
        node.offset = node.spring.value() + (rightOffset + leftOffset) * 0.5f;
        const float speed = node.spring.velocity();
        const float longitudinal = (velocity.z - node.previousVelocity.z) * TRACK_NODE_LONGITUDINAL_GAIN;
        const float vertical = (velocity.y - node.previousVelocity.y) * TRACK_NODE_VERTICAL_GAIN;
        float penetration;
        float impulse;
        this->contact( node, inverseRoot, elapsed, penetration, impulse );
        const float offset = node.offset;
        float total = impulse + (longitudinal + vertical) * node.spring.mass;
        node.offset = offset + penetration;
        if (maxOffset < offset + penetration)
        {
            if (0.1f < speed)
                total = total - (TRACK_NODE_RESTITUTION + 1.f) * speed * node.spring.mass;
            node.offset = maxOffset;
        }
        node.spring.addImpulse( total );
        node.previousPosition = position;
        node.previousVelocity = velocity;
    }
    for (size_t index = 0; index < nodes.size(); ++index)
        nodes[index].previousOffset = nodes[index].offset;
    resetVelocity = false;
    if (wheels.lock())
        for (size_t index = 0; index < nodes.size(); ++index)
        {
            TrackNode & node = nodes[index];
            Matrix wheel;
            if (node.wheel >= 0 &&
                this->wheelInRoot( static_cast<size_t>( node.wheel ), inverseRoot, wheel ))
                node.wheelPosition = wheel.applyToOrigin();
        }
}

void TrackNodesState::addNode( TrackNode node, float offset )
{
    // 0078ea00
    if (0.001f < fabsf( node.forwardCoefficient - node.backwardCoefficient ))
        node.blend = (1.f - node.backwardCoefficient) /
            (node.forwardCoefficient - node.backwardCoefficient);
    node.previousOffset = node.offset = offset;
    // The oscillator is configured before the node is linked.
    node.spring.setMaxAmplitude( maxAmplitude );
    node.spring.setMass( TRACK_NODE_MASS );
    node.spring.decay = node.damping;
    node.spring.setForce( gravity );
    node.spring.setStiffness( node.elasticity );
    node.spring.reset();
    // 0078f7c0 records the node's current local-array value.
    node.linked = this->local( node.name, node.basis );
    if (!node.linked)
    {
        node.basis = Matrix::identity;
        ERROR_MSG( "TrackNodesAnimator::linkWithCompound Cannot find node with name: %s in model\n",
            node.name.c_str() );
    }
    nodes.push_back( node );
}

void TrackNodesState::captureHull()
{
    // 0078e1a0 -> 0078f7c0 records the hull swinging node's index and its
    // current local-array value, not the assembly-time authored local.
    int parent;
    hullLinked = this->identity( hullName, hullIndex, parent ) && this->local( hullName, hullLocal );
    if (!hullLinked)
    {
        hullIndex = -1;
        hullLocal = Matrix::identity;
        ERROR_MSG( "TrackNodesAnimator::linkWithCompound Cannot find node with name: %s in model\n",
            hullName.c_str() );
    }
}

bool TrackNodesState::publish( bool forward )
{
    PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
    if (!model)
        return false;
    Matrix correction = Matrix::identity;
    if (forward && hullLinked)
    {
        // Forward rendering cancels the hull swing for its direct children:
        // recorded hull local * inverse(current hull local) (0078d8e0).
        Matrix current;
        if (!this->local( hullName, current ))
            return false;
        Matrix inverse;
        if (!inverse.invert( current ))
            inverse = Matrix::identity;
        correction.multiply( hullLocal, inverse );
    }
    for (size_t index = 0; index < nodes.size(); ++index)
    {
        const TrackNode & node = nodes[index];
        if (!node.linked)
            continue;
        Matrix basis = node.basis;
        if (forward && hullLinked)
        {
            // 0078d8e0 compares the node's parent index with the hull index.
            int index;
            int parent;
            if (!this->identity( node.name, index, parent ))
                return false;
            if (parent == hullIndex)
                basis.multiply( node.basis, correction );
        }
        if (!writeStockVehicleCompoundNodeLocal( model.get(), node.name,
                offsetLocal( basis, node.offset ) ))
            return false;
    }
    return true;
}

void TrackNodesState::activate()
{
    if (!valid_ || active)
        return;
    if (!linked)
    {
        // 0078e7b0: link, then settle one second at 1/60 s with extra damping.
        linked = true;
        this->link();
        if (!forwardPipeline())
        {
            warmup = 10.f;
            do
                this->simulate( 0.016666668f );
            while (0.f < warmup && valid_);
            resetVelocity = true;
        }
    }
    active = true;
    activeTrackNodes.push_back( std::static_pointer_cast<TrackNodesState>( this->shared_from_this() ) );
}
void TrackNodesState::deactivate()
{
    if (!active)
        return;
    active = false;
    for (size_t index = 0; index < activeTrackNodes.size(); ++index)
        if (activeTrackNodes[index].lock().get() == this)
        {
            activeTrackNodes[index] = activeTrackNodes.back();
            activeTrackNodes.pop_back();
            break;
        }
}
void TrackNodesState::destroy()
{
    this->deactivate();
    valid_ = false;
    compound = WeakPyPtr<PyObjectPlus>();
    wheels.reset();
    lodLink = NULL;
    nodes.clear();
}
}

class TrackNodesAnimator : public StockComponentWrapper
{
    Py_Header( TrackNodesAnimator, StockComponentWrapper )
public:
    TrackNodesAnimator( const std::shared_ptr<StockComponentState> & state, bool owning ) :
        StockComponentWrapper( state, &s_type_, owning ) {}
    std::shared_ptr<TrackNodesState> state() const
    {
        std::shared_ptr<TrackNodesState> value =
            std::dynamic_pointer_cast<TrackNodesState>( this->component() );
        if (!value)
            PyErr_SetString( PyExc_ValueError, "TrackNodesAnimator has been destroyed" );
        return value;
    }
    PY_METHOD_DECLARE( py_setParameters )
    PY_METHOD_DECLARE( py_addTrackNode )
    PY_METHOD_DECLARE( py_setWheelsDataProvider )
    PY_METHOD_DECLARE( py_setLodLink )
    PY_METHOD_DECLARE( py_setLodSettings )
    PY_FACTORY_DECLARE()
};

PyObject * TrackNodesState::wrap( bool owning )
{
    return new TrackNodesAnimator( this->shared_from_this(), owning );
}

PyObject * TrackNodesAnimator::pyNew( PyObject * args )
{
    PyObject * model;
    PyObject * nameObject;
    if (!PyArg_ParseTuple( args, "OO:TrackNodesAnimator", &model, &nameObject ))
        return NULL;
    BW::string name;
    if (Script::setData( nameObject, name ) != 0)
        return NULL;
    uint32 space;
    BoundingBox bounds;
    if (!stockVehicleCompoundContext( model, space, bounds ))
    {
        PyErr_SetString( PyExc_TypeError, "TrackNodesAnimator requires an active CompoundModel" );
        return NULL;
    }
    std::shared_ptr<TrackNodesState> value =
        std::make_shared<TrackNodesState>( static_cast<PyObjectPlus *>( model ), name );
    value->captureHull();
    return new TrackNodesAnimator( value, true );
}

PyObject * TrackNodesAnimator::py_setParameters( PyObject * args )
{
    float thickness;
    float gravity;
    float maxAmplitude;
    float maxOffset;
    if (!PyArg_ParseTuple( args, "ffff:setParameters", &thickness, &gravity, &maxAmplitude, &maxOffset ))
        return NULL;
    std::shared_ptr<TrackNodesState> value = this->state();
    if (!value)
        return NULL;
    // 0078f920 stores the amplitude limit as an absolute value.
    value->thickness = thickness;
    value->gravity = gravity;
    value->maxAmplitude = fabsf( maxAmplitude );
    value->maxOffset = maxOffset;
    Py_RETURN_NONE;
}

PyObject * TrackNodesAnimator::py_addTrackNode( PyObject * args )
{
    PyObject * nameObject;
    PyObject * sideObject;
    PyObject * offsetObject;
    PyObject * leftObject;
    PyObject * rightObject;
    PyObject * springObject;
    if (!PyArg_ParseTuple( args, "OOOOOO:addTrackNode", &nameObject, &sideObject, &offsetObject,
            &leftObject, &rightObject, &springObject ))
        return NULL;
    TrackNode node;
    float offset;
    if (Script::setData( nameObject, node.name ) != 0 ||
        Script::setData( sideObject, node.isLeft ) != 0 ||
        Script::setData( offsetObject, offset ) != 0 ||
        Script::setData( leftObject, node.leftName ) != 0 ||
        Script::setData( rightObject, node.rightName ) != 0)
        return NULL;
    // 007718e0 requires exactly (damping, elasticity, forward, backward).
    if (!PyTuple_Check( springObject ) || PyTuple_GET_SIZE( springObject ) != 4)
    {
        PyErr_SetString( PyExc_TypeError,
            "() argument 6 must be set to a (float, float, float, float) tuple" );
        return NULL;
    }
    float spring[4];
    for (Py_ssize_t index = 0; index < 4; ++index)
        if (Script::setData( PyTuple_GET_ITEM( springObject, index ), spring[index] ) != 0)
            return NULL;
    std::shared_ptr<TrackNodesState> value = this->state();
    if (!value)
        return NULL;
    node.damping = spring[0];
    node.elasticity = spring[1];
    node.forwardCoefficient = spring[2];
    node.backwardCoefficient = spring[3];
    value->addNode( node, offset );
    Py_RETURN_NONE;
}

PyObject * TrackNodesAnimator::py_setWheelsDataProvider( PyObject * args )
{
    PyObject * object;
    if (!PyArg_ParseTuple( args, "O:setWheelsDataProvider", &object ))
        return NULL;
    std::shared_ptr<StockWheelsDataProvider> provider;
    if (object != Py_None)
    {
        if (!StockComponentWrapper::Check( object ))
        {
            PyErr_SetString( PyExc_TypeError, "() argument 1 must be a component" );
            return NULL;
        }
        provider = stockWheelsDataProvider( object );
        if (!provider)
        {
            PyErr_SetString( PyExc_TypeError, "() argument 1 must be a IWheelDataProvider" );
            return NULL;
        }
    }
    std::shared_ptr<TrackNodesState> value = this->state();
    if (!value)
        return NULL;
    value->wheels = provider;
    Py_RETURN_NONE;
}

PyObject * TrackNodesAnimator::py_setLodLink( PyObject * args )
{
    PyObject * link;
    if (!PyArg_ParseTuple( args, "O:setLodLink", &link ))
        return NULL;
    std::shared_ptr<TrackNodesState> value = this->state();
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
    value->lodLink = link == Py_None ? NULL : link;
    Py_RETURN_NONE;
}

PyObject * TrackNodesAnimator::py_setLodSettings( PyObject * args )
{
    PyObject * object;
    if (!PyArg_ParseTuple( args, "O:setLodSettings", &object ))
        return NULL;
    float distance;
    uint32 priority;
    if (!readStockLodSettings( object, distance, priority ))
        return NULL;
    std::shared_ptr<TrackNodesState> value = this->state();
    if (!value)
        return NULL;
    value->maxDistance = distance;
    value->maxPriority = priority;
    Py_RETURN_NONE;
}

PY_TYPEOBJECT( TrackNodesAnimator )
PY_BEGIN_METHODS( TrackNodesAnimator )
    PY_METHOD( setParameters )
    PY_METHOD( addTrackNode )
    PY_METHOD( setWheelsDataProvider )
    PY_METHOD( setLodLink )
    PY_METHOD( setLodSettings )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( TrackNodesAnimator )
PY_END_ATTRIBUTES()
PY_FACTORY( TrackNodesAnimator, Vehicular )

void tickStockVehicleTrackNodes( float frameTime )
{
    // 0078d8e0 first shifts every component's LOD history, then advances
    // and publishes each eligible component.
    const std::vector<std::weak_ptr<TrackNodesState> > snapshot = activeTrackNodes;
    for (size_t index = 0; index < snapshot.size(); ++index)
    {
        std::shared_ptr<TrackNodesState> value = snapshot[index].lock();
        if (!value || !value->active)
            continue;
        bool eligible = true;
        if (value->lodLink)
        {
            uint32 priority;
            float distance;
            if (!sampleStockLodLink( value->lodLink.get(), priority, distance ))
            {
                ERROR_MSG( "TrackNodesAnimator lost its native LOD state\n" );
                value->deactivate();
                continue;
            }
            eligible = !(value->maxDistance < distance) && !(value->maxPriority < priority);
        }
        value->eligibility = ((value->eligibility << 1) | (eligible ? 1U : 0U)) & 0xffU;
    }
    const bool forward = forwardPipeline();
    for (size_t index = 0; index < snapshot.size(); ++index)
    {
        std::shared_ptr<TrackNodesState> value = snapshot[index].lock();
        if (!value || !value->active || !(value->eligibility & 1U))
            continue;
        if (!forward)
            value->simulate( frameTime );
        if (value->valid() && value->active && !value->publish( forward ))
        {
            ERROR_MSG( "TrackNodesAnimator lost its active compound\n" );
            value->deactivate();
        }
    }
}

namespace
{
class TrackNodesScriptShutdown : public Script::FiniTimeJob
{
    virtual void fini() { activeTrackNodes.clear(); }
} trackNodesScriptShutdown;
}
BW_END_NAMESPACE
