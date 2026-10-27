#include "pch.hpp"
#include "stock_glyph_cache.hpp"
#include "pyscript/script.hpp"
#include "scaleform/manager.hpp"

BW_BEGIN_NAMESPACE

/**
 *	#1513 00a9faf0: GUI.wg_setGlyphCacheParams(numTextures=1, textureWidth=1024,
 *	textureHeight=1024, maxSlotHeight=48). MonitorSettings.setGlyphCache passes
 *	(1, 1024 * ceil(scale), 1024 * ceil(scale)); the manager applies the latest
 *	values before its next frame.
 */
bool wg_setGlyphCacheParams( unsigned numTextures, unsigned textureWidth,
	unsigned textureHeight, unsigned maxSlotHeight )
{
	// The original dereferences the manager unconditionally.
	ScaleformBW::Manager * pManager = ScaleformBW::Manager::pInstance();
	if (pManager == NULL)
	{
		PyErr_SetString( PyExc_RuntimeError,
			"GUI.wg_setGlyphCacheParams: the Scaleform manager does not exist" );
		return false;
	}
	pManager->setGlyphCacheParams( wot::stock_glyph_cache::glyphCacheParams(
		numTextures, textureWidth, textureHeight, maxSlotHeight ) );
	return true;
}
PY_AUTO_MODULE_FUNCTION( RETOK, wg_setGlyphCacheParams, OPTARG( unsigned, 1,
	OPTARG( unsigned, 1024, OPTARG( unsigned, 1024, OPTARG( unsigned, 48, END ) ) ) ), GUI )

BW_END_NAMESPACE
