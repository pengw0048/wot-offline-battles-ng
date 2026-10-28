#include "pch.hpp"
#include "stock_timed_warplane_motor.hpp"

#include "app.hpp"
#include "duplo/motor.hpp"
#include "duplo/pymodel.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"

#include <cstring>
#include <map>

BW_BEGIN_NAMESPACE
int StockTimedWarplaneMotor_token = 1;

namespace
{
// BigWorld.time (006529a0) and the motor (00733b20) read the same clock.
float clientTime()
{
    return float( App::instance().getGameTimeFrameStart() );
}

wot::StockWarplaneVector portable( const Vector3 & value )
{
    return {{ value.x, value.y, value.z }};
}

// 00733700: (Vector3 position, Vector3 velocity, float time).
bool parsePoint( PyObject * tuple, wot::StockWarplanePoint & point )
{
    Vector3 position;
    Vector3 velocity;
    float time;
    if (Script::setData( PyTuple_GET_ITEM( tuple, 0 ), position, "controlPoint position" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( tuple, 1 ), velocity, "controlPoint velocity" ) != 0 ||
        Script::setData( PyTuple_GET_ITEM( tuple, 2 ), time, "controlPoint time" ) != 0)
        return false;
    point.position = portable( position );
    point.velocity = portable( velocity );
    point.time = time;
    return true;
}
}

/**
 *	#1513 BigWorld.PyTimedWarplaneMotor (type 01a0fd78, vtable 014d280c,
 *	0x94 bytes). rev 00733b20 advances the timed trajectory at +0x10 with
 *	BigWorld.time and the model's dTime, then sets the owner's world transform.
 */
class PyTimedWarplaneMotor : public Motor
{
	Py_Header( PyTimedWarplaneMotor, Motor )

public:
	explicit PyTimedWarplaneMotor( float rollSpeed ) : Motor( &s_type_ )
	{
		state_.rollSpeed = rollSpeed;
	}

	void init( float now, const wot::StockWarplanePoint & first, const wot::StockWarplanePoint & second )
	{
		wot::initStockTimedWarplane( state_, now, first, second, stockTimedWarplaneMath() );
	}

	virtual void rev( float dTime )
	{
		// A point callback may detach or release this motor synchronously.
		PyObjectPtr pin( this );
		wot::advanceStockTimedWarplane( state_, clientTime(), dTime,
			[this]( uint32 index, const wot::StockWarplaneVector & position,
				const wot::StockWarplaneVector & velocity )
			{
				this->notify( index, position, velocity );
			},
			stockTimedWarplaneMath() );
		if (pOwner_ != NULL)
		{
			Matrix world;
			std::memcpy( &world, state_.transform.data(), sizeof( world ) );
			pOwner_->worldTransform( world );
		}
	}

	PY_METHOD_DECLARE( py_addTrajectoryPoint )
	PY_FACTORY_DECLARE()

private:
	// 00733230: callable(position, velocity); failures print and clear.
	void notify( uint32 index, const wot::StockWarplaneVector & position,
		const wot::StockWarplaneVector & velocity )
	{
		std::map<uint32, PyObjectPtr>::const_iterator found = callbacks_.find( index );
		if (found == callbacks_.end())
			return;
		PyObjectPtr callback = found->second;
		PyObjectPtr args( PyTuple_New( 2 ), PyObjectPtr::STEAL_REFERENCE );
		PyTuple_SET_ITEM( args.get(), 0,
			Script::getData( Vector3( position[0], position[1], position[2] ) ) );
		PyTuple_SET_ITEM( args.get(), 1,
			Script::getData( Vector3( velocity[0], velocity[1], velocity[2] ) ) );
		PyObject * result = NULL;
		if (!PyCallable_Check( callback.get() ))
			PyErr_SetString( PyExc_TypeError, "Function is not callable" );
		else
			result = PyObject_Call( callback.get(), args.get(), NULL );
		if (result == NULL)
		{
			PyErr_PrintEx( 0 );
			PyErr_Clear();
		}
		Py_XDECREF( result );
	}

	wot::StockTimedWarplane state_;
	// 00734230 keeps one std::function per accepted point index.
	std::map<uint32, PyObjectPtr> callbacks_;
};

// 00733890: at least two (Vector3, Vector3, float) tuples, optional rollSpeed
// (default 0x3eb2b8c3, 20 degrees per second); extra arguments are ignored.
PyObject * PyTimedWarplaneMotor::pyNew( PyObject * args )
{
	const float now = clientTime();
	if (PyTuple_GET_SIZE( args ) < 2)
	{
		PyErr_SetString( PyExc_TypeError, "Invalid argument count" );
		return NULL;
	}
	PyObject * first = PyTuple_GET_ITEM( args, 0 );
	PyObject * second = PyTuple_GET_ITEM( args, 1 );
	if (!PyTuple_Check( first ) || PyTuple_GET_SIZE( first ) != 3 ||
		!PyTuple_Check( second ) || PyTuple_GET_SIZE( second ) != 3)
	{
		PyErr_SetString( PyExc_TypeError, "Invalid controlPoint arguments - must be tuples" );
		return NULL;
	}
	uint32 bits = 0x3eb2b8c3u;
	float rollSpeed;
	std::memcpy( &rollSpeed, &bits, sizeof( rollSpeed ) );
	if (PyTuple_GET_SIZE( args ) > 2 &&
		Script::setData( PyTuple_GET_ITEM( args, 2 ), rollSpeed, "PyTimedWarplaneMotor() argument 3" ) != 0)
		return NULL;
	wot::StockWarplanePoint firstPoint;
	wot::StockWarplanePoint secondPoint;
	if (!parsePoint( first, firstPoint ) || !parsePoint( second, secondPoint ))
		return NULL;
	PyTimedWarplaneMotor * motor = new PyTimedWarplaneMotor( rollSpeed );
	motor->init( now, firstPoint, secondPoint );
	return motor;
}

// 007333f0 -> 00733590: three or four arguments; a point that is not after
// both the motor clock and the last point is ignored without an error.
PyObject * PyTimedWarplaneMotor::py_addTrajectoryPoint( PyObject * args )
{
	PyObject * positionObject;
	PyObject * velocityObject;
	PyObject * timeObject;
	PyObject * callback = NULL;
	if (!PyArg_ParseTuple( args, "OOO|O:addTrajectoryPoint", &positionObject, &velocityObject,
			&timeObject, &callback ))
		return NULL;
	Vector3 position;
	Vector3 velocity;
	float time;
	if (Script::setData( positionObject, position, "addTrajectoryPoint() argument 1" ) != 0 ||
		Script::setData( velocityObject, velocity, "addTrajectoryPoint() argument 2" ) != 0 ||
		Script::setData( timeObject, time, "addTrajectoryPoint() argument 3" ) != 0)
		return NULL;
	wot::StockWarplanePoint point;
	point.position = portable( position );
	point.velocity = portable( velocity );
	point.time = time;
	const long index = wot::addStockTimedWarplanePoint( state_, point );
	if (index >= 0 && callback != NULL)
		callbacks_[static_cast<uint32>( index )] = callback;
	Py_RETURN_NONE;
}

PY_TYPEOBJECT( PyTimedWarplaneMotor )

PY_BEGIN_METHODS( PyTimedWarplaneMotor )
	PY_METHOD( addTrajectoryPoint )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyTimedWarplaneMotor )
PY_END_ATTRIBUTES()

PY_FACTORY_NAMED( PyTimedWarplaneMotor, "PyTimedWarplaneMotor", BigWorld )
BW_END_NAMESPACE
