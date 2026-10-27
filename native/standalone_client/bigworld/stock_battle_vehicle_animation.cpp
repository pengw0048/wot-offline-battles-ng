#include "pch.hpp"
#include "stock_battle_vehicle_animation.hpp"
#include "stock_vehicle_components.hpp"
#include "stock_vehicle_visuals.hpp"
#include "stock_audio_source.hpp"
#include "vehicle_appearance.hpp"

#include "pyscript/script_math.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE
int StockBattleVehicleAnimation_token = 1;

namespace
{
wot::StockAnimationMatrix portable( const Matrix & value )
{
    wot::StockAnimationMatrix result;
    std::memcpy( result.data(), &value, sizeof( result ) );
    return result;
}

Matrix native( const wot::StockAnimationMatrix & value )
{
    Matrix result;
    std::memcpy( &result, value.data(), sizeof( result ) );
    return result;
}

// Originals zero a stack matrix, then let the bound provider fill it.
bool sampleProvider( const MatrixProviderPtr & provider, wot::StockAnimationMatrix & value )
{
    MatrixProviderPtr pin = provider;
    if (!pin)
        return false;
    Matrix result;
    std::memset( &result, 0, sizeof( result ) );
    pin->matrix( result );
    value = portable( result );
    return true;
}

int setProvider( PyObject * object, MatrixProviderPtr & target, const char * name )
{
    MatrixProviderPtr value;
    if (Script::setData( object, value, name ) != 0)
        return -1;
    target = value;
    return 0;
}

PyObject * getProvider( const MatrixProviderPtr & value )
{
    if (!value)
        Py_RETURN_NONE;
    PyObject * result = value.get();
    Py_INCREF( result );
    return result;
}

// Swing and SuspensionSound hold a copied PyDataLink<float> std::function.
int setFloatLink( PyObject * object, StockFloatLinkReader & target )
{
    if (object == Py_None)
    {
        target = StockFloatLinkReader();
        return 0;
    }
    StockFloatLinkReader value;
    if (!copyStockFloatLink( object, value ))
    {
        PyErr_SetString( PyExc_TypeError, "lodLink requires a float DataLink or None" );
        return -1;
    }
    target = value;
    return 0;
}
}

// ---------------------------------------------------------------------------
// Vehicular.SwingingAnimator: #1513 PySwingingAnimator (0x48 bytes) is an
// auto-ticked MatrixProvider whose tick (007653e0) advances the native
// SwingingAnimator (0077f7b0) and whose matrix (00764cf0) publishes +0x44.
class PySwingingAnimator : public MatrixProvider
{
    Py_Header( PySwingingAnimator, MatrixProvider )
public:
    PySwingingAnimator() : MatrixProvider( true, &s_type_ ), alive_( true ), reported_( false ) {}

    virtual void tick( float elapsed )
    {
        if (!alive_)
            return;
        float lod;
        if (!lodLink_ || !lodLink_( lod ))
        {
            // Native 0077f7dc throws bad_function_call. Contain the cosmetic
            // owner and keep its last published pose.
            if (!reported_)
                ERROR_MSG( "SwingingAnimator has no valid float LOD link\n" );
            reported_ = true;
            return;
        }
        reported_ = false;
        const MatrixProviderPtr world = world_;
        const MatrixProviderPtr compensation = compensation_;
        wot::advanceStockSwinging( state_, elapsed, lod,
            [&world]( wot::StockAnimationMatrix & value ) { return sampleProvider( world, value ); },
            [&compensation]( wot::StockAnimationMatrix & value ) { return sampleProvider( compensation, value ); },
            stockBattleAnimationMath() );
    }

    // The original reads its native owner without a null check. After
    // destroy, a compound that still binds this provider gets the last pose.
    virtual void matrix( Matrix & value ) const { value = native( state_.output ); }

    PyObject * pyGet_lodLink()
    {
        return this->alive() ? makeStockFloatLink( lodLink_ ) : NULL;
    }
    int pySet_lodLink( PyObject * object )
    {
        return this->alive() ? setFloatLink( object, lodLink_ ) : -1;
    }
    PyObject * pyGet_worldMatrix() { return this->alive() ? getProvider( world_ ) : NULL; }
    int pySet_worldMatrix( PyObject * object )
    {
        return this->alive() ? setProvider( object, world_, "worldMatrix" ) : -1;
    }
    PyObject * pyGet_placingCompensationMatrix() { return this->alive() ? getProvider( compensation_ ) : NULL; }
    int pySet_placingCompensationMatrix( PyObject * object )
    {
        return this->alive() ? setProvider( object, compensation_, "placingCompensationMatrix" ) : -1;
    }
    PyObject * pyGet_accelSwingingPeriod() { return this->getFloat( state_.accelPeriod ); }
    int pySet_accelSwingingPeriod( PyObject * object ) { return this->setFloat( object, state_.accelPeriod, "accelSwingingPeriod" ); }
    PyObject * pyGet_accelSwingingDirection() { return this->getFloat( state_.accelDirection ); }
    int pySet_accelSwingingDirection( PyObject * object ) { return this->setFloat( object, state_.accelDirection, "accelSwingingDirection" ); }
    PyObject * pyGet_lodSetting() { return this->getFloat( state_.lodSetting ); }
    int pySet_lodSetting( PyObject * object ) { return this->setFloat( object, state_.lodSetting, "lodSetting" ); }
    PyObject * pyGet_maxMovementSpeed() { return this->getFloat( state_.maxMovementSpeed ); }
    int pySet_maxMovementSpeed( PyObject * object ) { return this->setFloat( object, state_.maxMovementSpeed, "maxMovementSpeed" ); }
    PyObject * pyGet_basisMatrix() { return this->alive() ? Script::getData( native( state_.basis ) ) : NULL; }
    int pySet_basisMatrix( PyObject * object )
    {
        if (!this->alive())
            return -1;
        Matrix value;
        if (Script::setData( object, value, "basisMatrix" ) != 0)
            return -1;
        // 00765000 writes the basis and its published output copy.
        state_.basis = state_.output = portable( value );
        return 0;
    }

    PY_METHOD_DECLARE( py_activate )
    PY_METHOD_DECLARE( py_deactivate )
    PY_METHOD_DECLARE( py_destroy )
    PY_METHOD_DECLARE( py_receiveShotImpulse )
    PY_METHOD_DECLARE( py_setupPitchSwinging )
    PY_METHOD_DECLARE( py_setupRollSwinging )
    PY_METHOD_DECLARE( py_setupShotSwinging )
    PY_FACTORY_DECLARE()

private:
    bool alive() const
    {
        if (!alive_)
            PyErr_SetString( PyExc_ValueError, "SwingingAnimator has been destroyed" );
        return alive_;
    }
    PyObject * getFloat( float value ) { return this->alive() ? Script::getData( value ) : NULL; }
    int setFloat( PyObject * object, float & target, const char * name )
    {
        if (!this->alive())
            return -1;
        float value;
        if (Script::setData( object, value, name ) != 0)
            return -1;
        target = value;
        return 0;
    }

    wot::StockSwingingState state_;
    StockFloatLinkReader lodLink_;
    MatrixProviderPtr world_;
    MatrixProviderPtr compensation_;
    bool alive_;
    bool reported_;
};

PyObject * PySwingingAnimator::pyNew( PyObject * args )
{
    if (!PyArg_ParseTuple( args, ":SwingingAnimator" ))
        return NULL;
    return new PySwingingAnimator();
}

// 00764530/007645e0: the provider ticks from construction. These Svarog
// callbacks only satisfy ScriptComponentState and never gate the update.
PyObject * PySwingingAnimator::py_activate( PyObject * args )
{
    if (!PyArg_ParseTuple( args, ":activate" ))
        return NULL;
    Py_RETURN_NONE;
}
PyObject * PySwingingAnimator::py_deactivate( PyObject * args )
{
    if (!PyArg_ParseTuple( args, ":deactivate" ))
        return NULL;
    Py_RETURN_NONE;
}
PyObject * PySwingingAnimator::py_destroy( PyObject * args )
{
    if (!PyArg_ParseTuple( args, ":destroy" ))
        return NULL;
    // 00764580 deletes the native animator once; a second call is harmless.
    if (alive_)
    {
        alive_ = false;
        lodLink_ = StockFloatLinkReader();
        const MatrixProviderPtr world = world_;
        const MatrixProviderPtr compensation = compensation_;
        world_ = MatrixProviderPtr();
        compensation_ = MatrixProviderPtr();
    }
    Py_RETURN_NONE;
}
PyObject * PySwingingAnimator::py_receiveShotImpulse( PyObject * args )
{
    PyObject * directionObject;
    PyObject * impulseObject;
    if (!PyArg_ParseTuple( args, "OO:receiveShotImpulse", &directionObject, &impulseObject ))
        return NULL;
    Vector3 direction;
    float impulse;
    if (Script::setData( directionObject, direction, "receiveShotImpulse() argument 1" ) != 0 ||
        Script::setData( impulseObject, impulse, "receiveShotImpulse() argument 2" ) != 0 ||
        !this->alive())
        return NULL;
    wot::receiveStockSwingingImpulse( state_, {{ direction.x, direction.y, direction.z }}, impulse );
    Py_RETURN_NONE;
}
PyObject * PySwingingAnimator::py_setupPitchSwinging( PyObject * args )
{
    float value[6];
    if (!PyArg_ParseTuple( args, "ffffff:setupPitchSwinging",
            &value[0], &value[1], &value[2], &value[3], &value[4], &value[5] ) || !this->alive())
        return NULL;
    std::memcpy( state_.pitch, value, sizeof( value ) );
    Py_RETURN_NONE;
}
PyObject * PySwingingAnimator::py_setupRollSwinging( PyObject * args )
{
    float value[7];
    if (!PyArg_ParseTuple( args, "fffffff:setupRollSwinging",
            &value[0], &value[1], &value[2], &value[3], &value[4], &value[5], &value[6] ) ||
        !this->alive())
        return NULL;
    std::memcpy( state_.roll, value, sizeof( value ) );
    Py_RETURN_NONE;
}
PyObject * PySwingingAnimator::py_setupShotSwinging( PyObject * args )
{
    float value;
    if (!PyArg_ParseTuple( args, "f:setupShotSwinging", &value ) || !this->alive())
        return NULL;
    state_.shotSensitivity = value;
    Py_RETURN_NONE;
}
PY_TYPEOBJECT( PySwingingAnimator )
PY_BEGIN_METHODS( PySwingingAnimator )
    PY_METHOD( receiveShotImpulse )
    PY_METHOD( setupPitchSwinging )
    PY_METHOD( setupRollSwinging )
    PY_METHOD( setupShotSwinging )
    PY_METHOD( activate )
    PY_METHOD( deactivate )
    PY_METHOD( destroy )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PySwingingAnimator )
    PY_ATTRIBUTE( lodLink )
    PY_ATTRIBUTE( worldMatrix )
    PY_ATTRIBUTE( placingCompensationMatrix )
    PY_ATTRIBUTE( accelSwingingPeriod )
    PY_ATTRIBUTE( accelSwingingDirection )
    PY_ATTRIBUTE( lodSetting )
    PY_ATTRIBUTE( basisMatrix )
    PY_ATTRIBUTE( maxMovementSpeed )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PySwingingAnimator, "SwingingAnimator", Vehicular )

// ---------------------------------------------------------------------------
// Vehicular.LinkedNodesPitchAnimator (0078b500, manager 005ae350 phase 2).
namespace
{
struct LinkedNodesSlave
{
    BW::string name;
    Matrix authored;
    bool found;
};

struct LinkedNodesGroup
{
    BW::string master;
    bool found;
    wot::StockLinkedNodesGroup table;
    std::vector<LinkedNodesSlave> slaves;
};

struct LinkedNodesState : public StockComponentState
{
    LinkedNodesState() : active( false ) {}
    virtual void activate();
    virtual void deactivate();
    virtual void destroy()
    {
        this->deactivate();
        valid_ = false;
        compound = WeakPyPtr<PyObjectPlus>();
        groups.clear();
    }
    virtual PyObject * wrap( bool owning );
    void update();
    WeakPyPtr<PyObjectPlus> compound;
    std::vector<LinkedNodesGroup> groups;
    bool active;
};
std::vector<std::weak_ptr<LinkedNodesState> > activeLinkedNodes;

void LinkedNodesState::activate()
{
    if (!valid_ || active)
        return;
    active = true;
    activeLinkedNodes.push_back( std::static_pointer_cast<LinkedNodesState>( this->shared_from_this() ) );
}
void LinkedNodesState::deactivate()
{
    if (!active)
        return;
    active = false;
    for (size_t index = 0; index < activeLinkedNodes.size(); ++index)
        if (activeLinkedNodes[index].lock().get() == this)
        {
            activeLinkedNodes[index] = activeLinkedNodes.back();
            activeLinkedNodes.pop_back();
            break;
        }
}

void LinkedNodesState::update()
{
    PyObjectPtr model( const_cast<PyObject *>( compound.getPyObj() ) );
    const wot::StockAnimationMath & math = stockBattleAnimationMath();
    for (size_t index = 0; index < groups.size() && valid_ && active; ++index)
    {
        LinkedNodesGroup & group = groups[index];
        if (!group.found)
            continue;
        Matrix local;
        if (!model || !stockVehicleCompoundNodeLocal( model.get(), group.master, local ))
        {
            ERROR_MSG( "LinkedNodesPitchAnimator lost its compound node %s\n", group.master.c_str() );
            this->deactivate();
            return;
        }
        wot::interpolateStockLinkedNodes( group.table,
            wot::stockLinkedNodesMasterAngle( portable( local ), math ) );
        for (size_t slave = 0; slave < group.slaves.size(); ++slave)
        {
            const LinkedNodesSlave & node = group.slaves[slave];
            if (!node.found)
                continue;
            // 0078c070 composes RotationX(angle) * authored node local.
            const wot::StockAnimationMatrix pose = math.multiply(
                math.rotationX( wot::stockLinkedNodesSlaveAngle( group.table, slave + 1 ) ),
                portable( node.authored ) );
            if (!writeStockVehicleCompoundNodeLocal( model.get(), node.name, native( pose ) ))
            {
                ERROR_MSG( "LinkedNodesPitchAnimator cannot write compound node %s\n", node.name.c_str() );
                this->deactivate();
                return;
            }
        }
    }
}

// 0076d0c0 DrivingNodes converter: at most 5 groups and 10 rows per group,
// rows capped at 21 items; an empty row ends its group, and every row reads
// (shortest row so far - 1) angles. A non-list group is skipped.
bool parseLinkedGroup( PyObject * rows, std::vector<BW::string> & names,
    std::vector<std::vector<float> > & values )
{
    const Py_ssize_t count = std::min<Py_ssize_t>( PyList_GET_SIZE( rows ), 10 );
    Py_ssize_t shortest = -1;
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        PyObject * row = PyList_GET_ITEM( rows, index );
        if (!PyList_Check( row ))
        {
            PyErr_SetString( PyExc_TypeError, "LinkedNodesPitchAnimator row must be a list" );
            return false;
        }
        const Py_ssize_t size = std::min<Py_ssize_t>( PyList_GET_SIZE( row ), 21 );
        if (size == 0)
            break;
        if (shortest == -1 || size < shortest)
            shortest = size;
        BW::string name;
        if (Script::setData( PyList_GET_ITEM( row, 0 ), name, "drivenJoints node" ) != 0)
            return false;
        std::vector<float> angles( static_cast<size_t>( shortest - 1 ), 0.f );
        for (Py_ssize_t item = 1; item < shortest; ++item)
            if (Script::setData( PyList_GET_ITEM( row, item ), angles[item - 1], "drivenJoints angle" ) != 0)
                return false;
        names.push_back( name );
        values.push_back( angles );
    }
    return true;
}
}

class LinkedNodesPitchAnimator : public StockComponentWrapper
{
    Py_Header( LinkedNodesPitchAnimator, StockComponentWrapper )
public:
    LinkedNodesPitchAnimator( const std::shared_ptr<StockComponentState> & state, bool owning ) :
        StockComponentWrapper( state, &s_type_, owning ) {}
    PY_FACTORY_DECLARE()
};

PyObject * LinkedNodesState::wrap( bool owning )
{
    return new LinkedNodesPitchAnimator( this->shared_from_this(), owning );
}

PyObject * LinkedNodesPitchAnimator::pyNew( PyObject * args )
{
    PyObject * model;
    PyObject * joints;
    if (!PyArg_ParseTuple( args, "OO:LinkedNodesPitchAnimator", &model, &joints ))
        return NULL;
    Matrix root;
    if (!stockVehicleCompoundNodeLocal( model, BW::string(), root ))
    {
        PyErr_SetString( PyExc_TypeError, "LinkedNodesPitchAnimator requires an active PyCompoundModel" );
        return NULL;
    }
    if (!PyList_Check( joints ))
    {
        PyErr_SetString( PyExc_TypeError,
            "LinkedNodesPitchAnimator() argument 2 Argument parsing error: Expected driven nodes desc" );
        return NULL;
    }
    std::shared_ptr<LinkedNodesState> state = std::make_shared<LinkedNodesState>();
    state->compound = WeakPyPtr<PyObjectPlus>( static_cast<PyObjectPlus *>( model ) );
    const Py_ssize_t count = std::min<Py_ssize_t>( PyList_GET_SIZE( joints ), 5 );
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        PyObject * rows = PyList_GET_ITEM( joints, index );
        if (!PyList_Check( rows ))
            continue;
        std::vector<BW::string> names;
        std::vector<std::vector<float> > values;
        if (!parseLinkedGroup( rows, names, values ))
            return NULL;
        // 0078ba80 skips an empty group but keeps every authored slave row.
        if (names.empty())
            continue;
        LinkedNodesGroup group;
        group.master = names[0];
        group.table = wot::makeStockLinkedNodesGroup( values );
        Matrix unused;
        group.found = stockVehicleCompoundNodeLocal( model, group.master, unused );
        for (size_t row = 1; row < names.size(); ++row)
        {
            LinkedNodesSlave slave;
            slave.name = names[row];
            slave.found = stockVehicleCompoundNodeLocal( model, slave.name, slave.authored );
            if (!slave.found)
                slave.authored = Matrix::identity;
            group.slaves.push_back( slave );
        }
        // 0078bd20 logs an unknown node and keeps constructing the animator.
        if (!group.found)
            ERROR_MSG( "LinkedNodesPitchAnimator::linkNodes Cannot find node: %s in compound: %s\n",
                group.master.c_str(), model->ob_type->tp_name );
        for (size_t slave = 0; slave < group.slaves.size(); ++slave)
            if (!group.slaves[slave].found)
                ERROR_MSG( "LinkedNodesPitchAnimator::linkNodes Cannot find node: %s in compound: %s\n",
                    group.slaves[slave].name.c_str(), model->ob_type->tp_name );
        state->groups.push_back( group );
    }
    // Svarog components start inactive; the appearance system activates them.
    return new LinkedNodesPitchAnimator( state, true );
}
PY_TYPEOBJECT( LinkedNodesPitchAnimator )
PY_BEGIN_METHODS( LinkedNodesPitchAnimator )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( LinkedNodesPitchAnimator )
PY_END_ATTRIBUTES()
PY_FACTORY( LinkedNodesPitchAnimator, Vehicular )

void tickStockLinkedNodesPitchAnimators()
{
    const std::vector<std::weak_ptr<LinkedNodesState> > snapshot = activeLinkedNodes;
    for (size_t index = 0; index < snapshot.size(); ++index)
    {
        std::shared_ptr<LinkedNodesState> state = snapshot[index].lock();
        if (state && state->valid() && state->active)
            state->update();
    }
}

// ---------------------------------------------------------------------------
// Vehicular.SuspensionSound (00789b30, manager 005ae190 phase 3).
namespace
{
struct SuspensionSoundState : public StockComponentState
{
    SuspensionSoundState() : active( false ), reported( false ) {}
    virtual void activate();
    virtual void deactivate();
    virtual void destroy()
    {
        this->deactivate();
        valid_ = false;
        // 00789f70 releases the providers, then stops and detaches the
        // sound object before its lodLink.
        body = MatrixProviderPtr();
        vehicle = MatrixProviderPtr();
        StockAudioSource sound = source;
        source = StockAudioSource();
        stopStockAudioSource( sound );
        setStockAudioSourceMatrix( sound, MatrixProviderPtr() );
        lodLink = StockFloatLinkReader();
    }
    virtual PyObject * wrap( bool owning );
    bool angle( float & value ) const
    {
        wot::StockAnimationMatrix bodyMatrix;
        wot::StockAnimationMatrix vehicleMatrix;
        if (!sampleProvider( body, bodyMatrix ) || !sampleProvider( vehicle, vehicleMatrix ))
            return false;
        value = wot::stockSuspensionSoundAngle( bodyMatrix, vehicleMatrix, stockBattleAnimationMath() );
        return true;
    }
    void post( int slot )
    {
        // 0124ee30 ignores event ID zero: slot 8 and unset or empty names.
        if (slot < 8 && !events[slot].empty())
            playStockAudioSource( source, events[slot] );
    }
    void setVehicleState( uint16 value );
    void update( float elapsed );

    wot::StockSuspensionSoundState kernel;
    StockFloatLinkReader lodLink;
    MatrixProviderPtr body;
    MatrixProviderPtr vehicle;
    BW::string events[8];
    StockAudioSource source;
    bool active;
    bool reported;
};
std::vector<std::weak_ptr<SuspensionSoundState> > activeSuspensionSounds;

void SuspensionSoundState::activate()
{
    if (!valid_ || active)
        return;
    active = true;
    activeSuspensionSounds.push_back(
        std::static_pointer_cast<SuspensionSoundState>( this->shared_from_this() ) );
}
void SuspensionSoundState::deactivate()
{
    if (!active)
        return;
    active = false;
    for (size_t index = 0; index < activeSuspensionSounds.size(); ++index)
        if (activeSuspensionSounds[index].lock().get() == this)
        {
            activeSuspensionSounds[index] = activeSuspensionSounds.back();
            activeSuspensionSounds.pop_back();
            break;
        }
}

// 0078ab90: leaving state 2 stops the current motion pair and every sound;
// entering it samples the current pitch difference as the rest angle.
void SuspensionSoundState::setVehicleState( uint16 value )
{
    kernel.vehicleState = value;
    kernel.angularSpeed = 0.f;
    if (value != 2)
    {
        if (kernel.motion != 0)
        {
            this->post( wot::STOCK_SUSPENSION_SOUND_TRANSITIONS[kernel.motion][1] );
            this->post( wot::STOCK_SUSPENSION_SOUND_TRANSITIONS[0][0] );
            kernel.motion = 0;
        }
        stopStockAudioSource( source );
        return;
    }
    float current = 0.f;
    if (!this->angle( current ))
        current = 0.f;
    kernel.angle = kernel.startAngle = current;
    kernel.motion = 0;
}

void SuspensionSoundState::update( float elapsed )
{
    if (kernel.vehicleState != 2)
        return;
    float lod;
    if (!lodLink || !lodLink( lod ))
    {
        // Native 00789686 throws bad_function_call for an empty link.
        ERROR_MSG( "SuspensionSound has no valid float LOD link\n" );
        this->deactivate();
        return;
    }
    if (!wot::stockSuspensionSoundLodAllows( lod, kernel.lodSetting ))
        return;
    const wot::StockSuspensionSoundTransition transition = wot::advanceStockSuspensionSound(
        kernel, elapsed, [this]( float & value ) { return this->angle( value ); } );
    if (transition.changed)
    {
        this->post( transition.stop );
        this->post( transition.start );
    }
}
}

class SuspensionSound : public StockComponentWrapper
{
    Py_Header( SuspensionSound, StockComponentWrapper )
public:
    SuspensionSound( const std::shared_ptr<StockComponentState> & state, bool owning ) :
        StockComponentWrapper( state, &s_type_, owning ) {}
    std::shared_ptr<SuspensionSoundState> state() const
    {
        std::shared_ptr<SuspensionSoundState> value =
            std::dynamic_pointer_cast<SuspensionSoundState>( this->component() );
        if (!value)
            PyErr_SetString( PyExc_ValueError, "SuspensionSound has been destroyed" );
        return value;
    }
    PyObject * pyGet_lodLink()
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        return value ? makeStockFloatLink( value->lodLink ) : NULL;
    }
    int pySet_lodLink( PyObject * object )
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        return value ? setFloatLink( object, value->lodLink ) : -1;
    }
    PyObject * pyGet_lodSetting() { return this->getFloat( &wot::StockSuspensionSoundState::lodSetting ); }
    int pySet_lodSetting( PyObject * object )
    {
        return this->setFloat( object, &wot::StockSuspensionSoundState::lodSetting, "lodSetting" );
    }
    PyObject * pyGet_angleLimitValue() { return this->getFloat( &wot::StockSuspensionSoundState::angleLimit ); }
    int pySet_angleLimitValue( PyObject * object )
    {
        return this->setFloat( object, &wot::StockSuspensionSoundState::angleLimit, "angleLimitValue" );
    }
    PyObject * pyGet_bodyMatrix()
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        return value ? getProvider( value->body ) : NULL;
    }
    int pySet_bodyMatrix( PyObject * object )
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        return value ? setProvider( object, value->body, "bodyMatrix" ) : -1;
    }
    PyObject * pyGet_vehicleMatrix()
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        return value ? getProvider( value->vehicle ) : NULL;
    }
    int pySet_vehicleMatrix( PyObject * object )
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        if (!value || setProvider( object, value->vehicle, "vehicleMatrix" ) != 0)
            return -1;
        // 0078ab40 also places the sound object on the vehicle provider.
        setStockAudioSourceMatrix( value->source, value->vehicle );
        return 0;
    }
    PyObject * pyGet_vehicleState()
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        return value ? Script::getData( value->kernel.vehicleState ) : NULL;
    }
    int pySet_vehicleState( PyObject * object )
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        uint16 state;
        if (!value || Script::setData( object, state, "vehicleState" ) != 0)
            return -1;
        value->setVehicleState( state );
        return 0;
    }
    PY_METHOD_DECLARE( py_setSoundsForState )
    PY_FACTORY_DECLARE()

private:
    PyObject * getFloat( float wot::StockSuspensionSoundState::* member )
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        return value ? Script::getData( value->kernel.*member ) : NULL;
    }
    int setFloat( PyObject * object, float wot::StockSuspensionSoundState::* member, const char * name )
    {
        std::shared_ptr<SuspensionSoundState> value = this->state();
        float result;
        if (!value || Script::setData( object, result, name ) != 0)
            return -1;
        value->kernel.*member = result;
        return 0;
    }
};

PyObject * SuspensionSoundState::wrap( bool owning )
{
    return new SuspensionSound( this->shared_from_this(), owning );
}

PyObject * SuspensionSound::pyNew( PyObject * args )
{
    int id;
    if (!PyArg_ParseTuple( args, "i:SuspensionSound", &id ))
        return NULL;
    std::shared_ptr<SuspensionSoundState> state = std::make_shared<SuspensionSoundState>();
    char name[64];
    bw_snprintf( name, sizeof( name ), "suspensionSound_%d", id );
    state->source = createStockAudioSource( name );
    return new SuspensionSound( state, true );
}

// 0078a880 assigns each slot whose 01d32460 name is state + "_underLimit" or
// state + "_overLimit"; unmatched states change nothing.
PyObject * SuspensionSound::py_setSoundsForState( PyObject * args )
{
    PyObject * objects[3];
    if (!PyArg_ParseTuple( args, "OOO:setSoundsForState", &objects[0], &objects[1], &objects[2] ))
        return NULL;
    BW::string state;
    BW::string under;
    BW::string over;
    if (Script::setData( objects[0], state, "setSoundsForState() argument 1" ) != 0 ||
        Script::setData( objects[1], under, "setSoundsForState() argument 2" ) != 0 ||
        Script::setData( objects[2], over, "setSoundsForState() argument 3" ) != 0)
        return NULL;
    std::shared_ptr<SuspensionSoundState> value = this->state();
    if (!value)
        return NULL;
    for (int slot = 0; slot < 8; ++slot)
    {
        const BW::string name = wot::STOCK_SUSPENSION_SOUND_SLOTS[slot];
        if (name == state + "_underLimit")
            value->events[slot] = under;
        if (name == state + "_overLimit")
            value->events[slot] = over;
    }
    Py_RETURN_NONE;
}
PY_TYPEOBJECT( SuspensionSound )
PY_BEGIN_METHODS( SuspensionSound )
    PY_METHOD( setSoundsForState )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( SuspensionSound )
    PY_ATTRIBUTE( lodLink )
    PY_ATTRIBUTE( lodSetting )
    PY_ATTRIBUTE( bodyMatrix )
    PY_ATTRIBUTE( vehicleMatrix )
    PY_ATTRIBUTE( angleLimitValue )
    PY_ATTRIBUTE( vehicleState )
PY_END_ATTRIBUTES()
PY_FACTORY( SuspensionSound, Vehicular )

void tickStockSuspensionSounds( float frameTime )
{
    const std::vector<std::weak_ptr<SuspensionSoundState> > snapshot = activeSuspensionSounds;
    for (size_t index = 0; index < snapshot.size(); ++index)
    {
        std::shared_ptr<SuspensionSoundState> state = snapshot[index].lock();
        if (state && state->valid() && state->active)
            state->update( frameTime );
    }
}

namespace
{
class BattleAnimationScriptShutdown : public Script::FiniTimeJob
{
    virtual void fini()
    {
        activeLinkedNodes.clear();
        activeSuspensionSounds.clear();
    }
} battleAnimationScriptShutdown;
}
BW_END_NAMESPACE
