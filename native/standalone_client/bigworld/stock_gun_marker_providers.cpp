#include "pch.hpp"
#include "stock_gun_marker_providers.hpp"
#include "moo/render_context.hpp"

#include <cstddef>

BW_BEGIN_NAMESPACE

namespace
{
namespace gui = wot::stock_battle_gui;

static_assert( offsetof( gui::GunMarkerSize, maxSize ) ==
	offsetof( gui::GunMarkerSize, minSize ) + sizeof( float ),
	"sizeConstraint aliases the two limits as one Vector2" );
static_assert( sizeof( Vector2 ) == 2 * sizeof( float ), "Vector2 layout" );
static_assert( sizeof( Matrix ) == sizeof( gui::Mat4 ), "Matrix layout" );

gui::Vec3 guiVector( const Vector3 & v ) { return gui::Vec3{ v.x, v.y, v.z }; }
}

gui::Mat4 stockBattleGuiMatrix( const Matrix & matrix )
{
	gui::Mat4 result;
	std::memcpy( &result, &matrix, sizeof( result ) );
	return result;
}

// --- WGAbstractGunMarkerDataProvider -------------------------------------------

PY_TYPEOBJECT( WGAbstractGunMarkerDataProvider )

PY_BEGIN_METHODS( WGAbstractGunMarkerDataProvider )
	PY_METHOD( setStartSize )
	PY_METHOD( updateSize )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGAbstractGunMarkerDataProvider )
	PY_ATTRIBUTE( positionMatrixProvider )
	PY_ATTRIBUTE( sizeConstraint )
PY_END_ATTRIBUTES()

PY_FACTORY( WGAbstractGunMarkerDataProvider, GUI )

WGAbstractGunMarkerDataProvider::WGAbstractGunMarkerDataProvider( PyTypeObject * pType ) :
	PyObjectPlus( pType )
{
}

WGAbstractGunMarkerDataProvider::~WGAbstractGunMarkerDataProvider()
{
}

// 13eb270.
PyObject * WGAbstractGunMarkerDataProvider::pyNew( PyObject * )
{
	PyErr_SetString( PyExc_NotImplementedError,
		"GUI.WGAbstractGunMarkerDataProvider: Can't instantiate abstract class." );
	return NULL;
}

// --- WGGunMarkerDataProvider ----------------------------------------------------

PY_TYPEOBJECT( WGGunMarkerDataProvider )

PY_BEGIN_METHODS( WGGunMarkerDataProvider )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGGunMarkerDataProvider )
PY_END_ATTRIBUTES()

PY_FACTORY( WGGunMarkerDataProvider, GUI )

WGGunMarkerDataProvider::WGGunMarkerDataProvider( PyTypeObject * pType ) :
	WGAbstractGunMarkerDataProvider( pType )
{
}

// 13eb290 ignores its arguments.
PyObject * WGGunMarkerDataProvider::pyNew( PyObject * )
{
	return new WGGunMarkerDataProvider();
}

bool WGGunMarkerDataProvider::clipPosition( gui::Vec4 & clip ) const
{
	const MatrixProviderPtr provider = positionMatrixProvider_;
	if (!provider)
	{
		return false;
	}
	Matrix position;
	position.setZero();
	provider->matrix( position );
	clip = gui::transform( gui::Vec3{ position( 3, 0 ), position( 3, 1 ), position( 3, 2 ) },
		stockBattleGuiMatrix( Moo::rc().viewProjection() ) );
	return true;
}

// --- WGSPGGunMarkerDataProvider -------------------------------------------------

PY_TYPEOBJECT( WGSPGGunMarkerDataProvider )

PY_BEGIN_METHODS( WGSPGGunMarkerDataProvider )
	PY_METHOD( reset )
	PY_METHOD( update )
	PY_METHOD( setRelaxTime )
	PY_METHOD( setupConicDispersion )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGSPGGunMarkerDataProvider )
	PY_ATTRIBUTE( relaxTime )
	PY_ATTRIBUTE( maxTime )
	PY_ATTRIBUTE( sizeScaleRate )
	PY_ATTRIBUTE( serverTickLength )
	PY_ATTRIBUTE( enableSmoothFiltering )
PY_END_ATTRIBUTES()

PY_FACTORY( WGSPGGunMarkerDataProvider, GUI )

WGSPGGunMarkerDataProvider::WGSPGGunMarkerDataProvider( uint32 count, PyTypeObject * pType ) :
	WGAbstractGunMarkerDataProvider( pType ),
	state_( count )
{
}

// 13eb340: GUI.WGSPGGunMarkerDataProvider(elementsCount > 0).
PyObject * WGSPGGunMarkerDataProvider::pyNew( PyObject * args )
{
	int count = 0;
	if (!PyArg_ParseTuple( args, "i", &count ))
	{
		PyErr_Format( PyExc_TypeError,
			"GUI.WGSPGGunMarkerDataProvider: Argument parsing error: Number expected." );
		return NULL;
	}
	if (count <= 0)
	{
		PyErr_Format( PyExc_TypeError, "GUI.WGSPGGunMarkerDataProvider: Argument parsing "
			"error: Number parameter must be greater than zero." );
		return NULL;
	}
	return new WGSPGGunMarkerDataProvider( static_cast< uint32 >( count ) );
}

void WGSPGGunMarkerDataProvider::update( const Vector3 & position, const Vector3 & velocity,
	const Vector3 & gravity, float size )
{
	const gui::SegmentCollision collide = []( const gui::Vec3 & start, const gui::Vec3 & end )
	{
		return stockGunMarkerSegmentCollision( Vector3( start.x, start.y, start.z ),
			Vector3( end.x, end.y, end.z ) );
	};
	state_.update( guiVector( position ), guiVector( velocity ), guiVector( gravity ), size,
		collide, stockBattleGuiMatrix( Moo::rc().viewProjection() ) );
}

BW_END_NAMESPACE
