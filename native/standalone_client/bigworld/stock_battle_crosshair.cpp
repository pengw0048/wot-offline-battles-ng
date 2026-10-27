#include "pch.hpp"
#include "stock_battle_crosshair.hpp"
#include "moo/render_context.hpp"

#include <cstdio>
#include <cstring>

BW_BEGIN_NAMESPACE

namespace ScaleformBW
{

namespace gui = wot::stock_battle_gui;

/**
 *	#1513 CrosshairFunctionHandler (vtables 01943ff4/01944000): the AS3
 *	radius clip's invalidateCrosshair. The owner detaches it before dying.
 */
class CrosshairFunctionHandler : public GFx::FunctionHandler
{
public:
	explicit CrosshairFunctionHandler( WGCrosshairGUIComponent * owner ) : owner_( owner ) {}
	void detach() { owner_ = NULL; }
	void Call( const Params & ) override
	{
		if (owner_ != NULL)
		{
			owner_->invalidateCrosshair();
		}
	}

private:
	WGCrosshairGUIComponent * owner_;
};

// --- WGCrosshairFlash -------------------------------------------------------------

PY_TYPEOBJECT( WGCrosshairGUIComponent )

PY_BEGIN_METHODS( WGCrosshairGUIComponent )
	PY_METHOD( setDataProvider )
	PY_METHOD( clearDataProvider )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGCrosshairGUIComponent )
PY_END_ATTRIBUTES()

PY_FACTORY_NAMED( WGCrosshairGUIComponent, "WGCrosshairFlash", GUI )

WGCrosshairGUIComponent::WGCrosshairGUIComponent( PyMovieView * pMovieView,
		PyTypePlus * pType ) :
	FlashGUIComponent( pMovieView, pType ),
	radiusClipSize_( 512.f )
{
	gunMarkerInfo_.SetVisible( false );
}

WGCrosshairGUIComponent::~WGCrosshairGUIComponent()
{
	if (invalidateHandler_)
	{
		invalidateHandler_->detach();
	}
	if (provider_)
	{
		provider_->release();
	}
}

// 13dfaa0: the item, its radius child and the AS3 invalidateCrosshair slot.
bool WGCrosshairGUIComponent::init( const BW::string & gunMarkerPath,
	const BW::string & radiusName )
{
	GFx::Movie * pMovie = pMovieView_ ? pMovieView_->pMovieView() : NULL;
	if (pMovie == NULL || gunMarkerPath.empty() || radiusName.empty())
	{
		return false;
	}
	if (!pMovie->GetVariable( &gunMarker_, gunMarkerPath.c_str() ) || !gunMarker_.IsObject() ||
		!gunMarker_.GetMember( radiusName.c_str(), &radius_ ) || !radius_.IsObject())
	{
		return false;
	}
	GFx::Value existing;
	if (!radius_.GetMember( "invalidateCrosshair", &existing ))
	{
		return false;
	}
	invalidateHandler_ = *new CrosshairFunctionHandler( this );
	GFx::Value function;
	pMovie->CreateFunction( &function, invalidateHandler_.GetPtr() );
	radius_.SetMember( "invalidateCrosshair", function );
	gunMarker_.SetDisplayInfo( gunMarkerInfo_ );
	return true;
}

// 13dfce0: GUI.WGCrosshairFlash(movie, gunMarkerPath, radiusName).
PyObject * WGCrosshairGUIComponent::pyNew( PyObject * args )
{
	PyObject * pMovie = NULL;
	const char * gunMarkerPath = NULL;
	const char * radiusName = NULL;
	if (!PyArg_ParseTuple( args, "Oss", &pMovie, &gunMarkerPath, &radiusName ))
	{
		PyErr_SetString( PyExc_TypeError, "GUI.WGCrosshairFlash: Argument parsing error: "
			"Expected a PyMovieView object and two String Objects" );
		return NULL;
	}
	if (pMovie == NULL || !PyMovieView::Check( pMovie ))
	{
		PyErr_SetString( PyExc_TypeError,
			"GUI.WGCrosshairFlash: Argument parsing error: Expected a PyMovieView object" );
		return NULL;
	}
	WGCrosshairGUIComponent * pComponent =
		new WGCrosshairGUIComponent( static_cast< PyMovieView * >( pMovie ) );
	if (!pComponent->init( gunMarkerPath, radiusName ))
	{
		Py_DECREF( pComponent );
		const BW::string message = BW::string( "GUI.WGCrosshairFlash: " ) +
			"Failed to init WGCrosshairGUIComponent with params " +
			"gunMarkerPath='" + gunMarkerPath + "'" + "and radiusName='" + radiusName + "'";
		PyErr_SetString( PyExc_TypeError, message.c_str() );
		return NULL;
	}
	return pComponent;
}

// 13df670: the original reports a failed acquisition and still returns None.
void WGCrosshairGUIComponent::setDataProvider( PyObjectPtr provider )
{
	PyObject * pProvider = provider.get();
	if (pProvider == NULL || !WGGunMarkerDataProvider::Check( pProvider ) ||
		!static_cast< WGGunMarkerDataProvider * >( pProvider )->acquire())
	{
		PyErr_SetString( PyExc_Exception,
			"GUI.WGCrosshairFlash.setDataProvider: Failed to assign data provider." );
		return;
	}
	// The replaced provider stays acquired, as in 13df670.
	provider_ = static_cast< WGGunMarkerDataProvider * >( pProvider );
	gunMarkerInfo_.SetVisible( true );
	gunMarker_.SetDisplayInfo( gunMarkerInfo_ );
}

// 13df560.
void WGCrosshairGUIComponent::clearDataProvider()
{
	gunMarkerInfo_.SetVisible( false );
	gunMarker_.SetDisplayInfo( gunMarkerInfo_ );
	if (provider_)
	{
		provider_->release();
		provider_ = NULL;
	}
}

// 13df3c0: the AS3 crosshair reports a new mixing clip.
void WGCrosshairGUIComponent::invalidateCrosshair()
{
	GFx::Value mixing;
	if (!radius_.GetMember( "currMixingMC", &mixing ) || !mixing.IsDisplayObject() ||
		!mixing.IsDisplayObjectActive())
	{
		return;
	}
	GFx::Value width;
	GFx::Value height;
	if (mixing.GetMember( "width", &width ) && mixing.GetMember( "height", &height ) &&
		width.IsNumber() && height.IsNumber())
	{
		radiusClipSize_ = gui::crosshairClipSize( width.GetNumber(), height.GetNumber() );
	}
}

// 13e0080 -> 13df810. Unlike #1513 (aa8bd0), the shared battle movie is not
// advanced again by this child; the application component advances it.
void WGCrosshairGUIComponent::update( float dTime, float relParentWidth,
	float relParentHeight )
{
	const SmartPointer< WGGunMarkerDataProvider > provider = provider_;
	if (provider)
	{
		gui::Vec4 clip;
		// #1513 reads an uninitialised position without a matrix provider.
		if (provider->clipPosition( clip ))
		{
			const gui::ScreenPosition position = gui::crosshairScreenPosition( clip,
				Moo::rc().halfScreenWidth(), Moo::rc().halfScreenHeight() );
			gunMarkerInfo_.SetVisible( position.visible );
			gunMarkerInfo_.SetPosition( position.x, position.y );
			gunMarker_.SetDisplayInfo( gunMarkerInfo_ );
		}
		const double scale = provider->clipScale( dTime, radiusClipSize_ );
		GFx::Value::DisplayInfo radiusInfo;
		radiusInfo.SetScale( scale, scale );
		radius_.SetDisplayInfo( radiusInfo );
	}
	SimpleGUIComponent::update( dTime, relParentWidth, relParentHeight );
}

// --- WGSPGCrosshairFlash ----------------------------------------------------------

PY_TYPEOBJECT( WGSPGCrosshairGUIComponent )

PY_BEGIN_METHODS( WGSPGCrosshairGUIComponent )
	PY_METHOD( setDataProvider )
	PY_METHOD( clearDataProvider )
	PY_METHOD( setPointsBaseScale )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGSPGCrosshairGUIComponent )
PY_END_ATTRIBUTES()

PY_FACTORY_NAMED( WGSPGCrosshairGUIComponent, "WGSPGCrosshairFlash", GUI )

WGSPGCrosshairGUIComponent::WGSPGCrosshairGUIComponent( PyMovieView * pMovieView,
		PyTypePlus * pType ) :
	FlashGUIComponent( pMovieView, pType ),
	pointsBaseScale_( 1.f )
{
}

WGSPGCrosshairGUIComponent::~WGSPGCrosshairGUIComponent()
{
	this->releaseProvider();
}

// 13e7770: the crosshair clip and elem_1..elem_N, each placed at (0, 0).
bool WGSPGCrosshairGUIComponent::init( const BW::string & crosshairPath, uint32 elementCount )
{
	GFx::Movie * pMovie = pMovieView_ ? pMovieView_->pMovieView() : NULL;
	if (pMovie == NULL || crosshairPath.empty() || elementCount == 0)
	{
		return false;
	}
	if (!pMovie->GetVariable( &crosshair_, crosshairPath.c_str() ) || !crosshair_.IsObject())
	{
		return false;
	}
	elements_.resize( elementCount );
	for (uint32 i = 0; i != elementCount; ++i)
	{
		char name[32];
		// The SDK maps snprintf to MSVC's global _snprintf.
		::snprintf( name, sizeof( name ), "elem_%u", i + 1 );
		if (!crosshair_.GetMember( name, &elements_[i] ))
		{
			return false;
		}
		GFx::Value::DisplayInfo origin;
		origin.SetPosition( 0.0, 0.0 );
		elements_[i].SetDisplayInfo( origin );
	}
	return true;
}

// 13e7ab0: GUI.WGSPGCrosshairFlash(movie, crosshairPath, elementsCount).
PyObject * WGSPGCrosshairGUIComponent::pyNew( PyObject * args )
{
	PyObject * pMovie = NULL;
	const char * crosshairPath = NULL;
	int elementCount = 0;
	if (!PyArg_ParseTuple( args, "Osi", &pMovie, &crosshairPath, &elementCount ))
	{
		PyErr_Format( PyExc_TypeError, "GUI.WGSPGCrosshairFlash: Argument parsing error: "
			"PyMovieView object, String and Number expected." );
		return NULL;
	}
	if (pMovie == NULL || !PyMovieView::Check( pMovie ))
	{
		PyErr_Format( PyExc_TypeError,
			"GUI.WGSPGCrosshairFlash: Argument parsing error: Expected a PyMovieView object" );
		return NULL;
	}
	if (elementCount <= 0)
	{
		// 13e7b4a returns NULL here without an exception.
		return NULL;
	}
	WGSPGCrosshairGUIComponent * pComponent =
		new WGSPGCrosshairGUIComponent( static_cast< PyMovieView * >( pMovie ) );
	if (!pComponent->init( crosshairPath, static_cast< uint32 >( elementCount ) ))
	{
		Py_DECREF( pComponent );
		PyErr_Format( PyExc_TypeError, "GUI.WGSPGCrosshairFlash: Argument parsing error:"
			"Expected a PyMovieView object that has a 'crosshairMC' movie clip." );
		return NULL;
	}
	return pComponent;
}

void WGSPGCrosshairGUIComponent::releaseProvider()
{
	if (provider_)
	{
		provider_->release();
		provider_ = NULL;
	}
}

void WGSPGCrosshairGUIComponent::setCrosshairVisible( bool visible )
{
	GFx::Value::DisplayInfo info;
	info.SetVisible( visible );
	crosshair_.SetDisplayInfo( info );
}

// 13e6dc0.
void WGSPGCrosshairGUIComponent::setDataProvider( PyObjectPtr provider )
{
	PyObject * pProvider = provider.get();
	if (pProvider == NULL || !WGSPGGunMarkerDataProvider::Check( pProvider ) ||
		!static_cast< WGSPGGunMarkerDataProvider * >( pProvider )->acquire())
	{
		PyErr_SetString( PyExc_Exception,
			"GUI.WGSPGCrosshairFlash.setDataProvider: Failed to assign data provider." );
		return;
	}
	provider_ = static_cast< WGSPGGunMarkerDataProvider * >( pProvider );
	if (provider_->elementCount() != elements_.size())
	{
		this->releaseProvider();
		PyErr_SetString( PyExc_Exception,
			"GUI.WGSPGCrosshairFlash.setDataProvider: inconsistent elements count." );
		return;
	}
	this->setCrosshairVisible( true );
}

// 13e6ca0.
void WGSPGCrosshairGUIComponent::clearDataProvider()
{
	this->releaseProvider();
	this->setCrosshairVisible( false );
}

// 13e7cd0 -> 13e7110, without advancing the shared battle movie again.
void WGSPGCrosshairGUIComponent::update( float dTime, float relParentWidth,
	float relParentHeight )
{
	const SmartPointer< WGSPGGunMarkerDataProvider > provider = provider_;
	if (provider && provider->advance( dTime ))
	{
		Moo::RenderContext & rc = Moo::rc();
		gui::SPGViewFrame frame;
		frame.viewProjection = stockBattleGuiMatrix( rc.viewProjection() );
		frame.view = stockBattleGuiMatrix( rc.view() );
		frame.halfWidth = rc.halfScreenWidth();
		frame.halfHeight = rc.halfScreenHeight();
		frame.screenWidth = frame.halfWidth + frame.halfWidth;
		frame.screenHeight = frame.halfHeight + frame.halfHeight;
		frame.nearPlane = rc.camera().nearPlane();
		frame.farPlane = rc.camera().farPlane();
		const float scale = gui::spgElementScale( pointsBaseScale_,
			provider->state().outputSize );
		const std::vector< gui::Vec3 > & points = provider->state().output;
		Render::Matrix3F view;
		view.SetIdentity();
		for (size_t i = 0; i != elements_.size(); ++i)
		{
			const gui::SPGElementView element = gui::spgElementView( points[i], frame, scale );
			Render::Matrix3F size;
			size.SetIdentity();
			size.M[0][0] = element.scale;
			size.M[1][1] = element.scale;
			elements_[i].SetMatrix3D( size );
			Render::Matrix4F projection;
			std::memcpy( projection.M, element.projection.m, sizeof( projection.M ) );
			GFx::Value::DisplayInfo info;
			info.SetViewMatrix3D( &view );
			info.SetProjectionMatrix3D( &projection );
			elements_[i].SetDisplayInfo( info );
		}
	}
	SimpleGUIComponent::update( dTime, relParentWidth, relParentHeight );
}

} // namespace ScaleformBW

BW_END_NAMESPACE
