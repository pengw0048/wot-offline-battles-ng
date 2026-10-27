#pragma once

#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "math/vector2.hpp"
#include "stock_battle_gui_data.hpp"
#include "stock_gun_marker_collision.hpp"

BW_BEGIN_NAMESPACE

wot::stock_battle_gui::Mat4 stockBattleGuiMatrix( const Matrix & matrix );

/**
 *	#1513 GUI.WGAbstractGunMarkerDataProvider (type 01ce4240): the provider
 *	state a crosshair view acquires. Its C++ interface precedes PyObject.
 */
class WGAbstractGunMarkerDataProvider : public PyObjectPlus
{
	Py_Header( WGAbstractGunMarkerDataProvider, PyObjectPlus )

public:
	WGAbstractGunMarkerDataProvider( PyTypeObject * pType = &s_type_ );
	~WGAbstractGunMarkerDataProvider();

	// DataProviderHolder ownership: one view at a time (13ebdb0, 13dfa70).
	bool acquire() { return size_.acquire(); }
	void release() { size_.acquired = false; }
	bool acquired() const { return size_.acquired; }

	// Slot 5: the SPG blend; the default provider always reports ready.
	virtual bool advance( float /*dTime*/ ) { return true; }
	virtual void setStartSize( float /*size*/ ) {}
	virtual void updateSize( float /*size*/, float /*relaxTime*/ ) {}

	Vector2 & sizeConstraint()
	{
		return *reinterpret_cast< Vector2 * >( &size_.minSize );
	}

	PY_AUTO_METHOD_DECLARE( RETVOID, setStartSize, ARG( float, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, updateSize, ARG( float, ARG( float, END ) ) )
	PY_RW_ATTRIBUTE_DECLARE( positionMatrixProvider_, positionMatrixProvider )
	PY_RW_ATTRIBUTE_DECLARE( sizeConstraint(), sizeConstraint )

	PY_FACTORY_DECLARE()

protected:
	wot::stock_battle_gui::GunMarkerSize size_;
	MatrixProviderPtr positionMatrixProvider_;
};

/**
 *	#1513 GUI.WGGunMarkerDataProvider (type 01ce4308, 13eb290): the arcade and
 *	sniper marker position and size.
 */
class WGGunMarkerDataProvider : public WGAbstractGunMarkerDataProvider
{
	Py_Header( WGGunMarkerDataProvider, WGAbstractGunMarkerDataProvider )

public:
	WGGunMarkerDataProvider( PyTypeObject * pType = &s_type_ );

	void setStartSize( float size ) override { size_.setStartSize( size ); }
	void updateSize( float size, float relaxTime ) override
	{
		size_.updateSize( size, relaxTime );
	}

	// 13ec580: the marker matrix translation in clip space.
	bool clipPosition( wot::stock_battle_gui::Vec4 & clip ) const;
	// 13ec360.
	float clipScale( float dTime, float clipSize ) { return size_.clipScale( dTime, clipSize ); }

	PY_FACTORY_DECLARE()
};

/**
 *	#1513 GUI.WGSPGGunMarkerDataProvider (type 01ce43d0, 13eb340): the strategic
 *	marker's dispersion cone traced to the scene.
 */
class WGSPGGunMarkerDataProvider : public WGAbstractGunMarkerDataProvider
{
	Py_Header( WGSPGGunMarkerDataProvider, WGAbstractGunMarkerDataProvider )

public:
	WGSPGGunMarkerDataProvider( uint32 count, PyTypeObject * pType = &s_type_ );

	bool advance( float dTime ) override
	{
		return state_.advance( dTime, size_.minSize, size_.maxSize );
	}

	void reset() { state_.reset(); }
	void update( const Vector3 & position, const Vector3 & velocity,
		const Vector3 & gravity, float size );
	void setRelaxTime( float relaxTime ) { state_.setRelaxTime( relaxTime ); }
	void setupConicDispersion( float angle ) { state_.setupConicDispersion( angle ); }

	uint32 elementCount() const { return state_.count(); }
	const wot::stock_battle_gui::SPGGunMarkerState & state() const { return state_; }

	PY_AUTO_METHOD_DECLARE( RETVOID, reset, END )
	PY_AUTO_METHOD_DECLARE( RETVOID, update, ARG( Vector3, ARG( Vector3,
		ARG( Vector3, ARG( float, END ) ) ) ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, setRelaxTime, ARG( float, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, setupConicDispersion, ARG( float, END ) )

	PY_RO_ATTRIBUTE_DECLARE( state_.relaxRate, relaxTime )
	PY_RW_ATTRIBUTE_DECLARE( state_.maxTime, maxTime )
	PY_RW_ATTRIBUTE_DECLARE( state_.sizeScaleRate, sizeScaleRate )
	PY_RW_ATTRIBUTE_DECLARE( state_.serverTickLength, serverTickLength )
	PY_RW_ATTRIBUTE_DECLARE( state_.smoothFiltering, enableSmoothFiltering )

	PY_FACTORY_DECLARE()

private:
	wot::stock_battle_gui::SPGGunMarkerState state_;
};

typedef SmartPointer< WGAbstractGunMarkerDataProvider > WGAbstractGunMarkerDataProviderPtr;

BW_END_NAMESPACE
