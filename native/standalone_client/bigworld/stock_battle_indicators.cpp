#include "pch.hpp"
#define WOT_STOCK_BATTLE_GUI_D3DX
#include "stock_battle_indicators.hpp"
#include "moo/render_context.hpp"

BW_BEGIN_NAMESPACE

namespace ScaleformBW
{

namespace gui = wot::stock_battle_gui;

namespace
{
gui::Vec3 matrixRow( const Matrix & matrix, int row )
{
	return gui::Vec3{ matrix( row, 0 ), matrix( row, 1 ), matrix( row, 2 ) };
}

gui::Vec3 providerRow( const MatrixProviderPtr & provider )
{
	Matrix sampled;
	sampled.setZero();
	provider->matrix( sampled );
	return matrixRow( sampled, 2 );
}
}

// --- WGHitIndicatorFlash -------------------------------------------------------

PY_TYPEOBJECT( WGHitIndicatorGUIComponent )

PY_BEGIN_METHODS( WGHitIndicatorGUIComponent )
	PY_METHOD( offsetRotationElementsInDegree )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGHitIndicatorGUIComponent )
PY_END_ATTRIBUTES()

PY_FACTORY_NAMED( WGHitIndicatorGUIComponent, "WGHitIndicatorFlash", GUI )

WGHitIndicatorGUIComponent::WGHitIndicatorGUIComponent( PyMovieView * pMovieView,
		PyTypePlus * pType ) :
	FlashGUIComponent( pMovieView, pType )
{
}

WGHitIndicatorGUIComponent::~WGHitIndicatorGUIComponent()
{
}

// 13e1300: the hit clip plus its extended animation and three state labels.
bool WGHitIndicatorGUIComponent::addElement( const BW::string & path )
{
	for (const Element & element : elements_)
	{
		if (element.path == path)
		{
			return false;
		}
	}
	GFx::Movie * pMovie = pMovieView_ ? pMovieView_->pMovieView() : NULL;
	if (pMovie == NULL)
	{
		return false;
	}
	Element element;
	element.path = path;
	element.globalYaw = 0.f;
	const BW::string animation = path + ".extendedAnimation";
	const BW::string state = animation + ".stateContainer.";
	const struct { GFx::Value * value; BW::string path; } clips[] = {
		{ &element.hit, path },
		{ &element.extendedAnimation, animation },
		{ &element.damage, state + "damage" },
		{ &element.tankName, state + "tankName" },
		{ &element.tankIcon, state + "tankIcon" },
	};
	bool missing = false;
	for (const auto & clip : clips)
	{
		if (!pMovie->GetVariable( clip.value, clip.path.c_str() ) || clip.value->IsNull())
		{
			missing = true;
		}
	}
	if (missing)
	{
		return false;
	}
	elements_.push_back( element );
	return true;
}

// 13e1780: GUI.WGHitIndicatorFlash(movie, (path, ...)).
PyObject * WGHitIndicatorGUIComponent::pyNew( PyObject * args )
{
	PyObject * pMovie = NULL;
	PyObject * pPaths = NULL;
	if (!PyArg_ParseTuple( args, "OO", &pMovie, &pPaths ))
	{
		PyErr_Format( PyExc_TypeError, "GUI.WGHitIndicatorFlash: Argument parsing error: "
			"Expected a PyMovieView object and Tuple of Strings object" );
		return NULL;
	}
	if (pMovie == NULL || !PyMovieView::Check( pMovie ))
	{
		PyErr_Format( PyExc_TypeError, "GUI.WGHitIndicatorFlash: Argument parsing error: "
			"Expected a PyMovieView object as first argument" );
		return NULL;
	}
	if (pPaths == NULL || !PyTuple_Check( pPaths ))
	{
		PyErr_Format( PyExc_TypeError, "GUI.WGHitIndicatorFlash: Argument parsing error: "
			"Expected a Tuple object as second argument" );
		return NULL;
	}
	BW::vector< BW::string > paths;
	for (Py_ssize_t i = 0; i != PyTuple_GET_SIZE( pPaths ); ++i)
	{
		PyObject * pPath = PyTuple_GET_ITEM( pPaths, i );
		const char * path = pPath != NULL && PyString_Check( pPath ) ?
			PyString_AsString( pPath ) : NULL;
		if (path == NULL)
		{
			PyErr_Format( PyExc_TypeError, "GUI.WGHitIndicatorFlash: Argument parsing error: "
				"Expected a String object as item of tuple into second argument" );
			return NULL;
		}
		paths.push_back( path );
	}
	WGHitIndicatorGUIComponent * pComponent =
		new WGHitIndicatorGUIComponent( static_cast< PyMovieView * >( pMovie ) );
	for (const BW::string & path : paths)
	{
		if (!pComponent->addElement( path ))
		{
			Py_DECREF( pComponent );
			PyErr_Format( PyExc_TypeError, "GUI.WGHitIndicatorFlash: Argument parsing error:"
				"Expected a PyMovieView object that has a '%s' movie clip", path.c_str() );
			return NULL;
		}
	}
	return pComponent;
}

// 13e0ed0: hit clips face their hits; damage and labels stay readable.
void WGHitIndicatorGUIComponent::rotateElements()
{
	const float cameraYaw = gui::rowYaw( matrixRow( Moo::rc().view(), 2 ) );
	for (Element & element : elements_)
	{
		const float rotation = gui::hitRotation( element.globalYaw, cameraYaw );
		GFx::Value::DisplayInfo info;
		info.SetRotation( rotation );
		element.hit.SetDisplayInfo( info );
		if (!element.extendedAnimation.GetDisplayInfo( &info ) || !info.GetVisible())
		{
			continue;
		}
		const float total = static_cast< float >( info.GetRotation() ) + rotation;
		if (element.damage.GetDisplayInfo( &info ) && info.GetVisible())
		{
			info.SetRotation( static_cast< double >( -total ) );
			element.damage.SetDisplayInfo( info );
		}
		GFx::Value::DisplayInfo name;
		if (!element.tankName.GetDisplayInfo( &name ))
		{
			continue;
		}
		const double target = gui::hitLabelRotation( total, ranges_, name.GetRotation() );
		if (name.GetVisible() && gui::hitLabelNeedsRotation( name.GetRotation(), target ))
		{
			name.SetRotation( target );
			element.tankName.SetDisplayInfo( name );
		}
		GFx::Value::DisplayInfo icon;
		if (element.tankIcon.GetDisplayInfo( &icon ) && icon.GetVisible() &&
			gui::hitLabelNeedsRotation( icon.GetRotation(), target ))
		{
			icon.SetRotation( target );
			element.tankIcon.SetDisplayInfo( icon );
		}
	}
}

// 13e1ad0.
void WGHitIndicatorGUIComponent::update( float dTime, float relParentWidth,
	float relParentHeight )
{
	this->rotateElements();
	FlashGUIComponent::update( dTime, relParentWidth, relParentHeight );
}

// --- WGTankIndicatorFlash --------------------------------------------------------

PY_TYPEOBJECT( WGTankIndicatorGUIComponent )

PY_BEGIN_METHODS( WGTankIndicatorGUIComponent )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGTankIndicatorGUIComponent )
	PY_ATTRIBUTE( wg_hullMatProv )
	PY_ATTRIBUTE( wg_turretMatProv )
	PY_ATTRIBUTE( wg_turretYawConstraints )
PY_END_ATTRIBUTES()

PY_FACTORY_NAMED( WGTankIndicatorGUIComponent, "WGTankIndicatorFlash", GUI )

WGTankIndicatorGUIComponent::WGTankIndicatorGUIComponent( PyMovieView * pMovieView,
		PyTypePlus * pType ) :
	FlashGUIComponent( pMovieView, pType ),
	turretYawConstraints_( 0.f, 0.f ),
	atConstraint_( false )
{
}

WGTankIndicatorGUIComponent::~WGTankIndicatorGUIComponent()
{
}

// 13e6330: the indicator clip and its hull and turret children.
bool WGTankIndicatorGUIComponent::init( const BW::string & path )
{
	GFx::Movie * pMovie = pMovieView_ ? pMovieView_->pMovieView() : NULL;
	if (pMovie == NULL || !pMovie->GetVariable( &indicator_, path.c_str() ) ||
		!indicator_.IsObject())
	{
		return false;
	}
	const bool hull = indicator_.GetMember( "hull", &hull_ );
	const bool turret = indicator_.GetMember( "turret", &turret_ );
	return hull && turret;
}

// 13e6330: GUI.WGTankIndicatorFlash(movie, path).
PyObject * WGTankIndicatorGUIComponent::pyNew( PyObject * args )
{
	PyObject * pMovie = NULL;
	const char * path = NULL;
	if (!PyArg_ParseTuple( args, "Os", &pMovie, &path ))
	{
		PyErr_Format( PyExc_TypeError, "GUI.WGTankIndicatorFlash: Argument parsing error: "
			"Expected a PyMovieView object and Strings object" );
		return NULL;
	}
	if (pMovie == NULL || !PyMovieView::Check( pMovie ))
	{
		PyErr_Format( PyExc_TypeError, "GUI.WGTankIndicatorFlash: Argument parsing error: "
			"Expected a PyMovieView object as first argument" );
		return NULL;
	}
	WGTankIndicatorGUIComponent * pComponent =
		new WGTankIndicatorGUIComponent( static_cast< PyMovieView * >( pMovie ) );
	if (!pComponent->init( path ))
	{
		Py_DECREF( pComponent );
		PyErr_Format( PyExc_TypeError, "GUI.WGTankIndicatorFlash: Argument parsing error:"
			"Expected a PyMovieView object that has '%s' movie clip.", path );
		return NULL;
	}
	return pComponent;
}

// 13e6670 -> 13e5e70.
void WGTankIndicatorGUIComponent::update( float dTime, float relParentWidth,
	float relParentHeight )
{
	SimpleGUIComponent::update( dTime, relParentWidth, relParentHeight );
	// #1513 samples both providers once either is set; it never sets only one.
	const MatrixProviderPtr hullMatrix = hullMatrix_;
	const MatrixProviderPtr turretMatrix = turretMatrix_;
	if (!hullMatrix || !turretMatrix)
	{
		return;
	}
	const gui::TankIndicatorAngles angles = gui::tankIndicatorAngles(
		matrixRow( Moo::rc().invView(), 2 ), providerRow( hullMatrix ),
		providerRow( turretMatrix ) );
	GFx::Value::DisplayInfo info;
	info.SetRotation( angles.hullRotation );
	hull_.SetDisplayInfo( info );
	info.SetRotation( angles.turretRotation );
	turret_.SetDisplayInfo( info );
	const int edge = gui::updateTurretConstraint( atConstraint_, angles.turretYaw,
		turretYawConstraints_.x, turretYawConstraints_.y );
	if (edge != 0)
	{
		const GFx::Value arrived( edge > 0 );
		indicator_.Invoke( "onArrivedConstraint", NULL, &arrived, 1 );
	}
}

} // namespace ScaleformBW

BW_END_NAMESPACE
