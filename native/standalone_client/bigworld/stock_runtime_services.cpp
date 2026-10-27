#include "pch.hpp"
#include "app.hpp"
#include "moo/render_context.hpp"
#include "pyscript/script.hpp"
#include "urlrequest/manager.hpp"

BW_BEGIN_NAMESPACE

// 006529f0 -> 0061a860: freshly sampled App-relative seconds, narrowed to float.
static float timeExact()
{
	return float( App::instance().getRenderTimeNow() );
}
PY_AUTO_MODULE_FUNCTION( RETDATA, timeExact, END, BigWorld )

// 00653850: the render context frame counter, advanced once per nextFrame().
static uint wg_getFrameTimestamp()
{
	return Moo::rc().frameTimestamp();
}
PY_AUTO_MODULE_FUNCTION( RETDATA, wg_getFrameTimestamp, END, BigWorld )

// 006d8280 converts the message and discards it.
static void addUPLMessage( const BW::string & )
{
}
PY_AUTO_MODULE_FUNCTION( RETVOID, addUPLMessage, ARG( BW::string, END ), BigWorld )

// Battle and loading telemetry (006d8340, 006d8450, 006d8750) is not collected.
static void enableBattleStatisticCollector( bool )
{
}
PY_AUTO_MODULE_FUNCTION( RETVOID, enableBattleStatisticCollector,
	ARG( bool, END ), BigWorld )

static void enableLoadingTimer( bool )
{
}
PY_AUTO_MODULE_FUNCTION( RETVOID, enableLoadingTimer, ARG( bool, END ), BigWorld )

// statistics.getStatistics and Avatar.leaveArena treat None as unavailable.
static PyObject * wg_getClientStatistics()
{
	Py_RETURN_NONE;
}
PY_AUTO_MODULE_FUNCTION( RETOWN, wg_getClientStatistics, END, BigWorld )

// 006548d0 -> 0061e950; the default selects renderer/reducedFrameRate.
static void wg_setSpecialFPSMode( float fps )
{
	App::instance().setSpecialFPSMode( fps );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_setSpecialFPSMode,
	OPTARG( float, -1.f, END ), BigWorld )

// 006536a0 -> 0061e880.
static void wg_disableSpecialFPSMode()
{
	App::instance().disableSpecialFPSMode();
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_disableSpecialFPSMode, END, BigWorld )

// 00619890 writes App+0x614.
static void wg_setReducedFpsMode( bool enabled )
{
	App::instance().setReducedFpsMode( enabled );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_setReducedFpsMode, ARG( bool, END ), BigWorld )

// 006196a0 has the public clearTextureReuseList body.
static void wg_clearTextureReuseList()
{
	Moo::rc().clearTextureReuseList();
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_clearTextureReuseList, END, BigWorld )

// 0067d490 ignores its arguments. URL::Manager keeps no completed-response
// cache, so there is nothing to purge; in-flight requests are not cached.
static PyObject * py_purgeUrlRequestCache( PyObject *, PyObject * )
{
	if (!URL::Manager::instance())
	{
		PyErr_SetString( PyExc_EnvironmentError,
			"PyURLRequest has not been initialised" );
		return NULL;
	}
	Py_RETURN_NONE;
}
PY_MODULE_FUNCTION_WITH_KEYWORDS( purgeUrlRequestCache, BigWorld )

BW_END_NAMESPACE
