#pragma once

#include "scaleform/flash_gui_component.hpp"
#include "pyscript/script_math.hpp"
#include "math/vector2.hpp"
#include "stock_battle_gui_data.hpp"

BW_BEGIN_NAMESPACE

namespace ScaleformBW
{

/**
 *	#1513 GUI.WGHitIndicatorFlash (type 01ce3910, 13e1780): the damage
 *	indicator movie's hit clips, turned against the camera every frame.
 */
class WGHitIndicatorGUIComponent : public FlashGUIComponent
{
	Py_Header( WGHitIndicatorGUIComponent, FlashGUIComponent )

public:
	WGHitIndicatorGUIComponent( PyMovieView * pMovieView, PyTypePlus * pType = &s_type_ );
	~WGHitIndicatorGUIComponent();

	bool addElement( const BW::string & path );

	// This component owns battleDamageIndicatorApp.swf: draw and advance it.
	void update( float dTime, float relParentWidth, float relParentHeight ) override;

	void offsetRotationElementsInDegree( float upper, float lower )
	{
		ranges_.setOffsets( upper, lower );
	}

	PY_AUTO_METHOD_DECLARE( RETVOID, offsetRotationElementsInDegree,
		ARG( float, ARG( float, END ) ) )

	PY_FACTORY_DECLARE()

private:
	struct Element
	{
		BW::string path;
		GFx::Value hit;
		GFx::Value damage;
		GFx::Value tankName;
		GFx::Value tankIcon;
		GFx::Value extendedAnimation;
		float globalYaw;
	};

	void rotateElements();

	BW::vector< Element > elements_;
	wot::stock_battle_gui::HitLabelRanges ranges_;
};

/**
 *	#1513 GUI.WGTankIndicatorFlash (type 01ce3d70, 13e6330): the damage panel's
 *	hull and turret silhouettes, following live matrices relative to the camera.
 */
class WGTankIndicatorGUIComponent : public FlashGUIComponent
{
	Py_Header( WGTankIndicatorGUIComponent, FlashGUIComponent )

public:
	WGTankIndicatorGUIComponent( PyMovieView * pMovieView, PyTypePlus * pType = &s_type_ );
	~WGTankIndicatorGUIComponent();

	bool init( const BW::string & path );

	// 13e6670 updates as a SimpleGUIComponent; the battle movie is shared.
	void update( float dTime, float relParentWidth, float relParentHeight ) override;
	void drawSelf( bool, bool ) override {}

	PY_RW_ATTRIBUTE_DECLARE( hullMatrix_, wg_hullMatProv )
	PY_RW_ATTRIBUTE_DECLARE( turretMatrix_, wg_turretMatProv )
	PY_RW_ATTRIBUTE_DECLARE( turretYawConstraints_, wg_turretYawConstraints )

	PY_FACTORY_DECLARE()

private:
	MatrixProviderPtr hullMatrix_;
	MatrixProviderPtr turretMatrix_;
	Vector2 turretYawConstraints_;
	bool atConstraint_;
	GFx::Value indicator_;
	GFx::Value hull_;
	GFx::Value turret_;
};

} // namespace ScaleformBW

BW_END_NAMESPACE
