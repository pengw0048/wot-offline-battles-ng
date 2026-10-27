#include "pch.hpp"
#include "stock_ballistics_simulator.hpp"
#include "stock_ballistics_scene.hpp"

#include "wot/stock_ballistics.hpp"

#include "app.hpp"
#include "duplo/motor.hpp"
#include "duplo/pymodel.hpp"
#include "moo/render_context.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "space/forward_declarations.hpp"

#include <cstring>
#include <memory>
#include <vector>

BW_BEGIN_NAMESPACE

namespace
{
namespace ballistics = wot::stock_ballistics;

ballistics::Vec3 portable( const Vector3 & value ) { return { value.x, value.y, value.z }; }
Vector3 native( const ballistics::Vec3 & value ) { return Vector3( value.x, value.y, value.z ); }

// #1513 manager 0x8a6bd0: simulators tick in registration order.
BW::vector< std::weak_ptr< ballistics::Simulator > > & simulators()
{
    static BW::vector< std::weak_ptr< ballistics::Simulator > > s_simulators;
    return s_simulators;
}

// 0x8a6b10: the client's frame-start game time, as float.
float frameTime()
{
    return float( App::instance().getGameTimeFrameStart() );
}

// 0x8ac31f reads the active camera position for tracer auto-scaling.
ballistics::Vec3 cameraPosition()
{
    return portable( Moo::rc().invView().applyToOrigin() );
}

// 0x89f420: None or an unconvertible result is no boundary hit.
std::function< bool( ballistics::Vec3 &, const ballistics::Vec3 &, const ballistics::Vec3 & ) >
    boundaryCallback( const ScriptObject & function )
{
    if (!function.exists() || !function.isCallable()) return nullptr;
    return [function]( ballistics::Vec3 & hit, const ballistics::Vec3 & start,
        const ballistics::Vec3 & end )
    {
        ScriptObject result = function.callFunction(
            ScriptArgs::create( native( start ), native( end ) ), ScriptErrorPrint() );
        if (!result.exists() || result.get() == Py_None) return false;
        Vector3 value;
        if (Script::setData( result.get(), value, "" ) != 0)
        {
            ScriptErrorPrint().handleError();
            return false;
        }
        hit = portable( value );
        return true;
    };
}

// 0x89f5d0: (shotID, position, impact direction).
std::function< void( std::int32_t, const ballistics::Vec3 &, const ballistics::Vec3 & ) >
    killCallback( const ScriptObject & function )
{
    if (!function.exists() || !function.isCallable()) return nullptr;
    return [function]( std::int32_t shotID, const ballistics::Vec3 & position,
        const ballistics::Vec3 & direction )
    {
        function.callFunction( ScriptArgs::create( int( shotID ), native( position ),
            native( direction ) ), ScriptErrorPrint() );
    };
}

// 0x89f510: (shotID).
std::function< void( std::int32_t ) > deleteCallback( const ScriptObject & function )
{
    if (!function.exists() || !function.isCallable()) return nullptr;
    return [function]( std::int32_t shotID )
    {
        function.callFunction( ScriptArgs::create( int( shotID ) ), ScriptErrorPrint() );
    };
}
} // namespace


/**
 *	#1513 PyProjectileMotor (type 01a167f0, vtable 014ee79c). rev 0x8a0180
 *	copies its record's visibility and transform onto the owning model.
 *	The original keeps a raw simulator pointer; this one cannot outlive it.
 */
class PyProjectileMotor : public Motor
{
	Py_Header( PyProjectileMotor, Motor )

public:
	PyProjectileMotor( std::weak_ptr< ballistics::Simulator > simulator,
			std::uint32_t handle ) :
		Motor( &s_type_ ), simulator_( simulator ), handle_( handle ) {}

	virtual void rev( float )
	{
		std::shared_ptr< ballistics::Simulator > simulator = simulator_.lock();
		if (!simulator || pOwner_ == NULL) return;
		const ballistics::Projectile * projectile = simulator->lookup( handle_ );
		if (projectile == NULL) return;
		Matrix world;
		BW_STATIC_ASSERT( sizeof( world ) == sizeof( projectile->matrix ),
			matrix_layout );
		std::memcpy( &world, &projectile->matrix, sizeof( world ) );
		pOwner_->visible( projectile->visible );
		pOwner_->worldTransform( world );
	}

private:
	std::weak_ptr< ballistics::Simulator > simulator_;
	std::uint32_t handle_;
};

PY_TYPEOBJECT( PyProjectileMotor )

PY_BEGIN_METHODS( PyProjectileMotor )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyProjectileMotor )
PY_END_ATTRIBUTES()


/**
 *	#1513 PyBallisticsSimulator (type 01a16700, methods 01a168b8). The
 *	presentation core owns tracer records; damage stays with the gameplay owner.
 */
class PyBallisticsSimulatorObject : public PyObjectPlus
{
	Py_Header( PyBallisticsSimulatorObject, PyObjectPlus )

public:
	explicit PyBallisticsSimulatorObject(
			std::shared_ptr< ballistics::Simulator > simulator ) :
		PyObjectPlus( &s_type_ ), simulator_( simulator ) {}

	~PyBallisticsSimulatorObject()
	{
		simulator_->detach();
	}

	// 0x89f8a0 -> 0x8aa960. An invalid handle returns None.
	PyObject * addProjectile( int32_t shotID, float gravity, const Vector3 & refStart,
		const Vector3 & refVelocity, const Vector3 & start, float maxDistance,
		bool autoScale, const Vector3 & gunPosition, const Vector3 & tracerCamera )
	{
		std::uint32_t handle = ballistics::INVALID_HANDLE;
		const ballistics::AddResult result = simulator_->add( handle, shotID, gravity,
			portable( refStart ), portable( refVelocity ), portable( start ), maxDistance,
			autoScale, portable( gunPosition ), portable( tracerCamera ) );
		if (result == ballistics::AddResult::NO_BOUNDARY)
		{
			// #1513 throws bad_function_call here and terminates.
			PyErr_SetString( PyExc_TypeError, "PyBallisticsSimulator.addProjectile: "
				"the arena boundary callback is not callable" );
			return NULL;
		}
		if ((handle & ballistics::INVALID_HANDLE) == ballistics::INVALID_HANDLE)
			Py_RETURN_NONE;
		return new PyProjectileMotor( simulator_, handle );
	}

	// 0x89fcb0 -> 0x8ab800.
	void hideProjectile( int32_t shotID, const Vector3 & end )
	{
		simulator_->hide( shotID, portable( end ) );
	}

	// 0x89fba0 -> 0x8ab470: None, or (position, direction) of a dead record.
	PyObject * explodeProjectile( int32_t shotID, const Vector3 & end )
	{
		ballistics::Vec3 position, direction;
		if (!simulator_->explode( shotID, portable( end ), position, direction ))
			Py_RETURN_NONE;
		PyObject * result = PyTuple_New( 2 );
		PyTuple_SET_ITEM( result, 0, Script::getData( native( position ) ) );
		PyTuple_SET_ITEM( result, 1, Script::getData( native( direction ) ) );
		return result;
	}

	// 0x89fd90 -> 0x8ab970.
	void holdProjectile( int32_t shotID )
	{
		simulator_->hold( shotID );
	}

	// 0x89fe80: process-wide statics 01a17264..01a17270.
	void setFixedBallisticsParams( float hidingTime, float timeAfterDeath,
		float autoScaleDistance, float serverTickLength )
	{
		ballistics::Parameters & parameters = ballistics::parameters();
		parameters.hidingTime = hidingTime;
		parameters.timeAfterDeath = timeAfterDeath;
		parameters.autoScaleDistance = autoScaleDistance;
		parameters.serverTickLength = serverTickLength;
	}

	// 0x89ffb0: static 01d350ac.
	void setVariableBallisticsParams( SpaceID spaceID )
	{
		ballistics::parameters().spaceID = spaceID;
	}

	// 0x89fe10: static 01d350b0.
	void setBallisticsAutoScale( bool value )
	{
		ballistics::parameters().autoScaleOff = value;
	}

	PY_AUTO_METHOD_DECLARE( RETOWN, addProjectile, ARG( int32_t, ARG( float,
		ARG( Vector3, ARG( Vector3, ARG( Vector3, ARG( float, ARG( bool,
		ARG( Vector3, ARG( Vector3, END ) ) ) ) ) ) ) ) ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, hideProjectile,
		ARG( int32_t, ARG( Vector3, END ) ) )
	PY_AUTO_METHOD_DECLARE( RETOWN, explodeProjectile,
		ARG( int32_t, ARG( Vector3, END ) ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, holdProjectile, ARG( int32_t, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, setFixedBallisticsParams,
		ARG( float, ARG( float, ARG( float, ARG( float, END ) ) ) ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, setVariableBallisticsParams,
		ARG( SpaceID, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, setBallisticsAutoScale, ARG( bool, END ) )

private:
	std::shared_ptr< ballistics::Simulator > simulator_;
};

PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME( PyBallisticsSimulatorObject,
	&Super::s_type_, "PyBallisticsSimulator" )

PY_BEGIN_METHODS( PyBallisticsSimulatorObject )
	PY_METHOD( addProjectile )
	PY_METHOD( hideProjectile )
	PY_METHOD( explodeProjectile )
	PY_METHOD( holdProjectile )
	PY_METHOD( setFixedBallisticsParams )
	PY_METHOD( setVariableBallisticsParams )
	PY_METHOD( setBallisticsAutoScale )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyBallisticsSimulatorObject )
PY_END_ATTRIBUTES()


// 0x89f730 -> 0x89f060. A non-callable argument leaves that callback empty.
static PyObject * PyBallisticsSimulator( const ScriptObject & boundary,
	const ScriptObject & kill, const ScriptObject & erase )
{
	ballistics::Callbacks callbacks;
	callbacks.boundary = boundaryCallback( boundary );
	callbacks.kill = killCallback( kill );
	callbacks.erase = deleteCallback( erase );
	std::shared_ptr< ballistics::Simulator > simulator =
		std::make_shared< ballistics::Simulator >( std::move( callbacks ),
			frameTime, cameraPosition, stockBallisticsScene() );
	simulators().push_back( simulator );
	return new PyBallisticsSimulatorObject( simulator );
}
PY_AUTO_MODULE_FUNCTION( RETOWN, PyBallisticsSimulator, ARG( ScriptObject,
	ARG( ScriptObject, ARG( ScriptObject, END ) ) ), BigWorld )


/**
 *	#1513 PySalvo (type 01a17058). ProjectileMover.__init__ constructs it
 *	unconditionally; its artillery generator and methods serve combat
 *	equipment only, so this object keeps none of the parsed settings.
 */
class PySalvoObject : public PyObjectPlus
{
	Py_Header( PySalvoObject, PyObjectPlus )

public:
	PySalvoObject() : PyObjectPlus( &s_type_ ) {}
};

PY_GENERAL_TYPEOBJECT_WITH_BASE_WITH_NAME( PySalvoObject, &Super::s_type_, "PySalvo" )

PY_BEGIN_METHODS( PySalvoObject )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PySalvoObject )
PY_END_ATTRIBUTES()

// 0x8a6690: fewer than three arguments or a failed conversion returns None.
static PyObject * py_PySalvo( PyObject * args )
{
	if (PyTuple_Size( args ) < 3) Py_RETURN_NONE;
	uint capacity = 0, group = 0;
	float limit = 0.f;
	if (Script::setData( PyTuple_GET_ITEM( args, 0 ), capacity, "" ) != 0 ||
		Script::setData( PyTuple_GET_ITEM( args, 1 ), group, "" ) != 0 ||
		Script::setData( PyTuple_GET_ITEM( args, 2 ), limit, "" ) != 0)
	{
		// #1513 leaves this conversion error pending; contain it here.
		ScriptErrorPrint().handleError();
		Py_RETURN_NONE;
	}
	return new PySalvoObject();
}
PY_MODULE_FUNCTION( PySalvo, BigWorld )


void tickStockBallistics()
{
	// A Python callback may create or destroy simulators while they tick.
	BW::vector< std::shared_ptr< ballistics::Simulator > > live;
	BW::vector< std::weak_ptr< ballistics::Simulator > > & registered = simulators();
	BW::vector< std::weak_ptr< ballistics::Simulator > > kept;
	for (const std::weak_ptr< ballistics::Simulator > & entry : registered)
	{
		std::shared_ptr< ballistics::Simulator > simulator = entry.lock();
		if (!simulator) continue;
		kept.push_back( entry );
		live.push_back( simulator );
	}
	registered.swap( kept );
	for (const std::shared_ptr< ballistics::Simulator > & simulator : live)
		simulator->tick();
}

BW_END_NAMESPACE
