#include "pch.hpp"
#include "math/vector3.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <random>

BW_BEGIN_NAMESPACE

namespace
{
const float STOCK_TWO_PI = 6.28318548f;

// #1513 __ftoui3 (013d81d0): the low 32 bits of the truncated 64-bit value;
// a non-finite or out-of-range value yields 0.
std::uint32_t stockFloatToUint( float value )
{
	const double wide = value;
	if (!(wide < 9223372036854775808.0) || !(wide >= -9223372036854775808.0))
		return 0;
	return static_cast< std::uint32_t >(
		static_cast< std::uint64_t >( static_cast< std::int64_t >( wide ) ) );
}


/**
 *	#1513 IOscillator (vtable 014ee88c): deviation +4, velocity +0x10 and
 *	external force +0x1c. Python reads and writes deviation and velocity
 *	directly; constraints and external force go through the virtual slots.
 */
class StockOscillator
{
public:
	StockOscillator() :
		deviation_( 0.f, 0.f, 0.f ),
		velocity_( 0.f, 0.f, 0.f ),
		externalForce_( 0.f, 0.f, 0.f ) {}
	virtual ~StockOscillator() {}

	// 008a1680
	virtual void reset()
	{
		deviation_ = Vector3( 0.f, 0.f, 0.f );
		velocity_ = Vector3( 0.f, 0.f, 0.f );
		externalForce_ = Vector3( 0.f, 0.f, 0.f );
	}
	virtual void update( float deltaTime ) = 0;
	virtual void applyImpulse( const Vector3 & impulse ) = 0;
	// 008a1580 and 0060b6b0: no constraints by default.
	virtual Vector3 constraints() const { return Vector3( 0.f, 0.f, 0.f ); }
	virtual void constraints( const Vector3 & ) {}
	// 008a15a0 and 00adc050
	virtual void externalForce( const Vector3 & force ) { externalForce_ = force; }
	virtual const Vector3 & externalForce() const { return externalForce_; }

	Vector3 deviation_;
	Vector3 velocity_;
	Vector3 externalForce_;
};

typedef std::shared_ptr< StockOscillator > StockOscillatorPtr;


/**
 *	#1513 SimpleOscillator (vtable 014ef0b8, constructor 008ac610): a damped
 *	spring per axis, clamped to +/- constraints.
 */
class StockSimpleOscillator : public StockOscillator
{
public:
	StockSimpleOscillator( float mass, const Vector3 & stiffness,
			const Vector3 & drag, const Vector3 & constraints ) :
		stiffness_( stiffness ), mass_( mass ), drag_( drag ),
		constraints_( constraints ) {}

	// 008ac760: integrate at most 0.05 s per call in steps of up to 1/60 s.
	void update( float deltaTime ) override
	{
		float remaining = deltaTime;
		if (0.f > remaining)
			return;
		remaining = remaining < 0.05f ? remaining : 0.05f;
		if (!(remaining > 0.f))
			return;
		do
		{
			const float step = remaining < 0.0166666675f ?
				remaining : 0.0166666675f;
			remaining = remaining - step;
			const float inverseMass = 1.f / mass_;
			velocity_.x = velocity_.x + ((externalForce_.x -
				deviation_.x * stiffness_.x) - velocity_.x * drag_.x) *
				inverseMass * step;
			velocity_.y = velocity_.y + ((externalForce_.y -
				deviation_.y * stiffness_.y) - velocity_.y * drag_.y) *
				inverseMass * step;
			velocity_.z = velocity_.z + ((externalForce_.z -
				deviation_.z * stiffness_.z) - velocity_.z * drag_.z) *
				inverseMass * step;
			deviation_.x = velocity_.x * step + deviation_.x;
			deviation_.y = velocity_.y * step + deviation_.y;
			deviation_.z = velocity_.z * step + deviation_.z;

			// A clamped axis stops once it moved by at least 0.0004.
			const Vector3 integrated = deviation_;
			deviation_.x = clamp( deviation_.x, constraints_.x );
			deviation_.y = clamp( deviation_.y, constraints_.y );
			deviation_.z = clamp( deviation_.z, constraints_.z );
			if (std::fabs( integrated.x - deviation_.x ) >= 0.0004f)
				velocity_.x = 0.f;
			if (std::fabs( integrated.y - deviation_.y ) >= 0.0004f)
				velocity_.y = 0.f;
			if (std::fabs( integrated.z - deviation_.z ) >= 0.0004f)
				velocity_.z = 0.f;
		}
		while (remaining > 0.f);
	}

	// 008ac6e0
	void applyImpulse( const Vector3 & impulse ) override
	{
		const float inverseMass = 1.f / mass_;
		velocity_.x = velocity_.x + impulse.x * inverseMass;
		velocity_.y = velocity_.y + impulse.y * inverseMass;
		velocity_.z = velocity_.z + impulse.z * inverseMass;
	}

	// 009f36f0 and 008ac740
	Vector3 constraints() const override { return constraints_; }
	void constraints( const Vector3 & value ) override { constraints_ = value; }

private:
	static float clamp( float value, float limit )
	{
		const float lower = -limit;
		if (lower > value)
			return lower;
		return limit < value ? limit : value;
	}

	Vector3 stiffness_;
	float mass_;
	Vector3 drag_;
	Vector3 constraints_;
};


/**
 *	#1513 CompoundOscillator (vtable 014ee8b0): the second child follows the
 *	first child's residual. Impulses and external force go to the first.
 */
class StockCompoundOscillator : public StockOscillator
{
public:
	StockCompoundOscillator( const StockOscillatorPtr & first,
			const StockOscillatorPtr & second ) :
		first_( first ), second_( second ) {}

	// 008ac9c0
	void reset() override
	{
		StockOscillator::reset();
		first_->reset();
		second_->reset();
	}

	// 008aca20
	void update( float deltaTime ) override
	{
		first_->update( deltaTime );
		second_->deviation_.x = deviation_.x - first_->deviation_.x;
		second_->deviation_.y = deviation_.y - first_->deviation_.y;
		second_->deviation_.z = deviation_.z - first_->deviation_.z;
		second_->update( deltaTime );
		deviation_.x = first_->deviation_.x + second_->deviation_.x;
		deviation_.y = first_->deviation_.y + second_->deviation_.y;
		deviation_.z = first_->deviation_.z + second_->deviation_.z;
		velocity_.x = second_->velocity_.x + first_->velocity_.x;
		velocity_.y = first_->velocity_.y + second_->velocity_.y;
		velocity_.z = first_->velocity_.z + second_->velocity_.z;
	}

	// 008ac990, 008ac9a0 and 008ac9b0
	void applyImpulse( const Vector3 & impulse ) override
	{
		first_->applyImpulse( impulse );
	}
	void externalForce( const Vector3 & force ) override
	{
		first_->externalForce( force );
	}
	const Vector3 & externalForce() const override
	{
		return first_->externalForce();
	}

private:
	StockOscillatorPtr first_;
	StockOscillatorPtr second_;
};


/**
 *	#1513 RandomNoiseOscillator (constructor 008a0470, vtable 014ee8f8): an
 *	impulse starts a decaying sine whose half-period samples scale random
 *	targets; deviation blends from the previous target to the current one.
 *	Each instance owns a default-seeded mt19937 and uniform_real<float>(0, 1).
 */
class StockRandomNoiseOscillator : public StockOscillator
{
public:
	StockRandomNoiseOscillator( float mass, float stiffness, float drag,
			float threshold ) :
		time_( 0.f ),
		threshold_( threshold ),
		amplitude_( 0.f ),
		halfPeriodIndex_( 0xffffffffu ),
		previousTarget_( 0.f, 0.f, 0.f ),
		target_( 0.f, 0.f, 0.f ),
		mass_( mass ),
		drag_( drag )
	{
		const float decay = 0.5f / mass * drag;
		frequency_ = std::sqrt( stiffness / mass - decay * decay );
		period_ = STOCK_TWO_PI / frequency_;
	}

	// 008acd00 keeps the half-period index, targets and generator state.
	void reset() override
	{
		StockOscillator::reset();
		time_ = 0.f;
		amplitude_ = 0.f;
	}

	// 008acd50
	void update( float deltaTime ) override
	{
		const float time = deltaTime + time_;
		const float halfPeriod = period_ * 0.5f;
		time_ = time;
		const std::uint32_t index = stockFloatToUint( time / halfPeriod );
		const float start = static_cast< float >(
			static_cast< double >( index ) ) * halfPeriod;
		const float quarterPeriod = halfPeriod * 0.5f;
		const float end = start + halfPeriod;
		const float from = start - quarterPeriod;
		const float lower = 0.f > from ? 0.f : this->sample( from );
		const float to = end - quarterPeriod;
		const float upper = 0.f > to ? 0.f : this->sample( to );

		const float span = upper - lower;
		float weight = 1.f;
		if (static_cast< double >( std::fabs( span ) ) > 0.001)
		{
			const float at = time > halfPeriod ?
				time - quarterPeriod : time * 0.5f;
			const float value = 0.f > at ? 0.f : this->sample( at );
			weight = 1.f - (upper - value) / span;
		}

		if (halfPeriodIndex_ != index)
		{
			previousTarget_ = target_;
			halfPeriodIndex_ = index;
			target_ = this->generate( upper );
		}
		deviation_.x = (target_.x - previousTarget_.x) * weight +
			previousTarget_.x;
		deviation_.y = (target_.y - previousTarget_.y) * weight +
			previousTarget_.y;
		deviation_.z = (target_.z - previousTarget_.z) * weight +
			previousTarget_.z;
	}

	// 008acc70: only a settled oscillator accepts a new impulse.
	void applyImpulse( const Vector3 & impulse ) override
	{
		const float settled = deviation_.x * deviation_.x +
			deviation_.y * deviation_.y + deviation_.z * deviation_.z;
		if (settled > threshold_ * threshold_)
			return;
		time_ = 0.f;
		const float magnitude = std::sqrt( impulse.x * impulse.x +
			impulse.y * impulse.y + impulse.z * impulse.z );
		amplitude_ = magnitude * (1.f / mass_);
	}

protected:
	virtual Vector3 generate( float amplitude ) = 0;

	// 008a01f0: generate_canonical<float, 24> over one mt19937 draw.
	float canonical()
	{
		return static_cast< float >( engine_() ) / 4294967296.f;
	}

private:
	float sample( float time ) const
	{
		const float envelope = std::exp( drag_ * time * (-0.5f / mass_) ) *
			amplitude_;
		return envelope * std::sin( frequency_ * time ) / frequency_;
	}

	float time_;
	float threshold_;
	float amplitude_;
	std::uint32_t halfPeriodIndex_;
	Vector3 previousTarget_;
	Vector3 target_;
	float mass_;
	float drag_;
	float frequency_;
	float period_;
	std::mt19937 engine_;
};


// #1513 RandomNoiseOscillatorFlat (vtable 014ee91c, generator 008a03e0).
class StockRandomNoiseOscillatorFlat : public StockRandomNoiseOscillator
{
public:
	StockRandomNoiseOscillatorFlat( float mass, float stiffness, float drag,
			float threshold ) :
		StockRandomNoiseOscillator( mass, stiffness, drag, threshold ) {}

private:
	Vector3 generate( float amplitude ) override
	{
		const float angle = this->canonical() * STOCK_TWO_PI;
		return Vector3( std::sin( angle ) * amplitude, 0.f,
			std::cos( angle ) * amplitude );
	}
};


// #1513 RandomNoiseOscillatorSpherical (vtable 014ee940, generator 008a0790).
class StockRandomNoiseOscillatorSpherical : public StockRandomNoiseOscillator
{
public:
	StockRandomNoiseOscillatorSpherical( float mass, float stiffness,
			float drag, const Vector3 & scale, float threshold ) :
		StockRandomNoiseOscillator( mass, stiffness, drag, threshold ),
		scale_( scale ) {}

private:
	Vector3 generate( float amplitude ) override
	{
		const float azimuth = this->canonical() * STOCK_TWO_PI;
		const float height = this->canonical();
		const float polar = std::acos( height + height - 1.f );
		const float ring = std::sin( polar );
		return Vector3(
			scale_.x * (std::sin( azimuth ) * ring * amplitude),
			scale_.y * (std::cos( polar ) * amplitude),
			scale_.z * (std::cos( azimuth ) * ring * amplitude) );
	}

	Vector3 scale_;
};
}


/**
 *	#1513 Math.PyOscillator (type 01a16950). Every oscillator type shares its
 *	reset/update/applyImpulse methods and four Vector3 attributes. The original
 *	never frees the native oscillator, so a compound child outlives its
 *	wrapper; shared ownership keeps that lifetime without the leak.
 */
class PyOscillator : public PyObjectPlus
{
	Py_Header( PyOscillator, PyObjectPlus )

public:
	PyOscillator( const StockOscillatorPtr & oscillator,
			PyTypeObject * pType = &s_type_ ) :
		PyObjectPlus( pType ), oscillator_( oscillator ) {}

	const StockOscillatorPtr & oscillator() const { return oscillator_; }

	void reset() { oscillator_->reset(); }
	void update( float deltaTime ) { oscillator_->update( deltaTime ); }
	void applyImpulse( const Vector3 & impulse )
	{
		oscillator_->applyImpulse( impulse );
	}

	const Vector3 & externalForce() const
	{
		return oscillator_->externalForce();
	}
	void externalForce( const Vector3 & value )
	{
		oscillator_->externalForce( value );
	}
	const Vector3 & velocity() const { return oscillator_->velocity_; }
	void velocity( const Vector3 & value ) { oscillator_->velocity_ = value; }
	const Vector3 & deviation() const { return oscillator_->deviation_; }
	void deviation( const Vector3 & value ) { oscillator_->deviation_ = value; }
	Vector3 constraints() const { return oscillator_->constraints(); }
	void constraints( const Vector3 & value )
	{
		oscillator_->constraints( value );
	}

	PY_AUTO_METHOD_DECLARE( RETVOID, reset, END )
	PY_AUTO_METHOD_DECLARE( RETVOID, update, ARG( float, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, applyImpulse, ARG( Vector3, END ) )

	PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector3, externalForce, externalForce )
	PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector3, velocity, velocity )
	PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector3, deviation, deviation )
	PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector3, constraints, constraints )

	// 008a0d60
	static PyOscillator * New( float mass, const Vector3 & stiffness,
		const Vector3 & drag, const Vector3 & constraints )
	{
		return new PyOscillator( std::make_shared< StockSimpleOscillator >(
			mass, stiffness, drag, constraints ) );
	}
	PY_AUTO_FACTORY_DECLARE( PyOscillator, ARG( float, ARG( Vector3,
		ARG( Vector3, ARG( Vector3, END ) ) ) ) )

private:
	StockOscillatorPtr oscillator_;
};

PY_TYPEOBJECT( PyOscillator )

PY_BEGIN_METHODS( PyOscillator )
	PY_METHOD( reset )
	PY_METHOD( update )
	PY_METHOD( applyImpulse )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyOscillator )
	PY_ATTRIBUTE( externalForce )
	PY_ATTRIBUTE( velocity )
	PY_ATTRIBUTE( deviation )
	PY_ATTRIBUTE( constraints )
PY_END_ATTRIBUTES()

PY_FACTORY( PyOscillator, Math )


/**
 *	#1513 Math.PyCompoundOscillator (type 01a16a18, 008a0900). The original
 *	takes two ScriptObjects and reads each one's native oscillator unchecked;
 *	a non-oscillator argument raises TypeError here instead.
 */
class PyCompoundOscillator : public PyOscillator
{
	Py_Header( PyCompoundOscillator, PyOscillator )

public:
	PyCompoundOscillator( const StockOscillatorPtr & oscillator ) :
		PyOscillator( oscillator, &s_type_ ) {}

	PY_FACTORY_DECLARE()
};

PyObject * PyCompoundOscillator::pyNew( PyObject * args )
{
	if (PyTuple_Size( args ) != 2)
	{
		return Script::argCountError( "PyCompoundOscillator", 2, 2,
			"ScriptObject", "ScriptObject" );
	}
	PyObject * pFirst = PyTuple_GetItem( args, 0 );
	PyObject * pSecond = PyTuple_GetItem( args, 1 );
	if (!PyOscillator::Check( pFirst ) || !PyOscillator::Check( pSecond ))
	{
		PyErr_Format( PyExc_TypeError,
			"() argument %d must be set to a PyOscillator",
			PyOscillator::Check( pFirst ) ? 2 : 1 );
		return NULL;
	}
	return new PyCompoundOscillator(
		std::make_shared< StockCompoundOscillator >(
			static_cast< PyOscillator * >( pFirst )->oscillator(),
			static_cast< PyOscillator * >( pSecond )->oscillator() ) );
}

PY_TYPEOBJECT( PyCompoundOscillator )

PY_BEGIN_METHODS( PyCompoundOscillator )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyCompoundOscillator )
PY_END_ATTRIBUTES()

PY_FACTORY( PyCompoundOscillator, Math )


// #1513 Math.PyRandomNoiseOscillatorFlat (type 01a16ae0, 008a0ef0).
class PyRandomNoiseOscillatorFlat : public PyOscillator
{
	Py_Header( PyRandomNoiseOscillatorFlat, PyOscillator )

public:
	PyRandomNoiseOscillatorFlat( const StockOscillatorPtr & oscillator ) :
		PyOscillator( oscillator, &s_type_ ) {}

	static PyRandomNoiseOscillatorFlat * New( float mass, float stiffness,
		float drag, float threshold )
	{
		return new PyRandomNoiseOscillatorFlat(
			std::make_shared< StockRandomNoiseOscillatorFlat >(
				mass, stiffness, drag, threshold ) );
	}
	PY_AUTO_FACTORY_DECLARE( PyRandomNoiseOscillatorFlat, ARG( float,
		ARG( float, ARG( float, OPTARG( float, 0.01f, END ) ) ) ) )
};

PY_TYPEOBJECT( PyRandomNoiseOscillatorFlat )

PY_BEGIN_METHODS( PyRandomNoiseOscillatorFlat )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyRandomNoiseOscillatorFlat )
PY_END_ATTRIBUTES()

PY_FACTORY( PyRandomNoiseOscillatorFlat, Math )


// #1513 Math.PyRandomNoiseOscillatorSpherical (type 01a16ba8, 008a1140).
class PyRandomNoiseOscillatorSpherical : public PyOscillator
{
	Py_Header( PyRandomNoiseOscillatorSpherical, PyOscillator )

public:
	PyRandomNoiseOscillatorSpherical( const StockOscillatorPtr & oscillator ) :
		PyOscillator( oscillator, &s_type_ ) {}

	static PyRandomNoiseOscillatorSpherical * New( float mass,
		float stiffness, float drag, const Vector3 & scale, float threshold )
	{
		return new PyRandomNoiseOscillatorSpherical(
			std::make_shared< StockRandomNoiseOscillatorSpherical >(
				mass, stiffness, drag, scale, threshold ) );
	}
	PY_AUTO_FACTORY_DECLARE( PyRandomNoiseOscillatorSpherical, ARG( float,
		ARG( float, ARG( float, OPTARG( Vector3, Vector3( 1.f, 1.f, 1.f ),
		OPTARG( float, 0.01f, END ) ) ) ) ) )
};

PY_TYPEOBJECT( PyRandomNoiseOscillatorSpherical )

PY_BEGIN_METHODS( PyRandomNoiseOscillatorSpherical )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( PyRandomNoiseOscillatorSpherical )
PY_END_ATTRIBUTES()

PY_FACTORY( PyRandomNoiseOscillatorSpherical, Math )

BW_END_NAMESPACE
