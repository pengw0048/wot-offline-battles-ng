#pragma once

#include "scaleform/flash_gui_component.hpp"
#include "stock_gun_marker_providers.hpp"

BW_BEGIN_NAMESPACE

namespace ScaleformBW
{

class CrosshairFunctionHandler;

/**
 *	#1513 GUI.WGCrosshairFlash (type 01ce37e8, 13dfce0): the arcade/sniper gun
 *	marker clip in the battle movie, driven by a WGGunMarkerDataProvider.
 */
class WGCrosshairGUIComponent : public FlashGUIComponent
{
	Py_Header( WGCrosshairGUIComponent, FlashGUIComponent )

public:
	WGCrosshairGUIComponent( PyMovieView * pMovieView, PyTypePlus * pType = &s_type_ );
	~WGCrosshairGUIComponent();

	bool init( const BW::string & gunMarkerPath, const BW::string & radiusName );

	void update( float dTime, float relParentWidth, float relParentHeight ) override;
	// The battle application component draws the shared movie (1347f90).
	void drawSelf( bool, bool ) override {}

	void setDataProvider( PyObjectPtr provider );
	void clearDataProvider();
	void invalidateCrosshair();

	PY_AUTO_METHOD_DECLARE( RETVOID, setDataProvider, ARG( PyObjectPtr, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, clearDataProvider, END )

	PY_FACTORY_DECLARE()

private:
	GFx::Value gunMarker_;
	GFx::Value radius_;
	GFx::Value::DisplayInfo gunMarkerInfo_;
	SmartPointer< WGGunMarkerDataProvider > provider_;
	Ptr< CrosshairFunctionHandler > invalidateHandler_;
	float radiusClipSize_;
};

/**
 *	#1513 GUI.WGSPGCrosshairFlash (type 01ce3e70, 13e7ab0): the strategic
 *	marker's elem_N clips placed from a WGSPGGunMarkerDataProvider.
 */
class WGSPGCrosshairGUIComponent : public FlashGUIComponent
{
	Py_Header( WGSPGCrosshairGUIComponent, FlashGUIComponent )

public:
	WGSPGCrosshairGUIComponent( PyMovieView * pMovieView, PyTypePlus * pType = &s_type_ );
	~WGSPGCrosshairGUIComponent();

	bool init( const BW::string & crosshairPath, uint32 elementCount );

	void update( float dTime, float relParentWidth, float relParentHeight ) override;
	void drawSelf( bool, bool ) override {}

	void setDataProvider( PyObjectPtr provider );
	void clearDataProvider();
	void setPointsBaseScale( float scale ) { pointsBaseScale_ = scale; }

	PY_AUTO_METHOD_DECLARE( RETVOID, setDataProvider, ARG( PyObjectPtr, END ) )
	PY_AUTO_METHOD_DECLARE( RETVOID, clearDataProvider, END )
	PY_AUTO_METHOD_DECLARE( RETVOID, setPointsBaseScale, ARG( float, END ) )

	PY_FACTORY_DECLARE()

private:
	void releaseProvider();
	void setCrosshairVisible( bool visible );

	float pointsBaseScale_;
	GFx::Value crosshair_;
	BW::vector< GFx::Value > elements_;
	SmartPointer< WGSPGGunMarkerDataProvider > provider_;
};

} // namespace ScaleformBW

BW_END_NAMESPACE
