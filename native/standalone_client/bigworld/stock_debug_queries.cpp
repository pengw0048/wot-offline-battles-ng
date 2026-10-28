#include "pch.hpp"
#include "stock_debug_queries.hpp"

#include "connection_control.hpp"
#include "debug_app.hpp"
#include "connection/server_connection.hpp"
#include "connection_model/bw_server_connection.hpp"
#include "cstdmf/main_loop_task.hpp"
#include "math/vector4.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE

namespace
{

// #1513 statistics owner fields +0x2618 (ping) and +0x2642 (loading timer).
float s_ping = 0.f;
bool s_loadingTimer = false;

// 006da4f6..006da56d: the server round trip in ms, or 999 without one.
float sampleStockPing()
{
	ConnectionControl * pControl = ConnectionControl::pInstance();
	BWServerConnection * pConnection = pControl ? pControl->pServerConnection() : NULL;
	if (pConnection == NULL || !pConnection->isOnline())
	{
		return 999.f;
	}

	float ping = float( double( pConnection->pServerConnection()->latency() ) * 1000.0 );
	ping = ping < 999.f ? ping : 999.f;
	if (999.f > ping)
	{
		ping -= 0.1f * 500.f;
		ping = ping > 1.f ? ping : 1.f;
	}
	return ping;
}

// 006da4c0 runs after every active frame's main-loop tick.
class StockClientStatistics : public MainLoopTask
{
public:
	StockClientStatistics()
	{
		MainLoopTasks::root().add( this, "StockClientStatistics", ">Finale", NULL );
	}

	void tick( float, float ) override
	{
		if (!s_loadingTimer)
		{
			s_ping = sampleStockPing();
		}
	}
};

StockClientStatistics s_clientStatistics;

} // namespace

// 006d8700 reads the statistics owner's ping.
static float statPing()
{
	return s_ping;
}
PY_AUTO_MODULE_FUNCTION( RETDATA, statPing, END, BigWorld )

// 006188d0 -> 00631fa0: DebugApp's fps, 50-frame average, minimum, maximum.
static Vector4 getFPS()
{
	Vector4 result;
	DebugApp::instance.fpsStatistics( result.x, result.y, result.z, result.w );
	return result;
}
PY_AUTO_MODULE_FUNCTION( RETDATA, getFPS, END, BigWorld )

BW_END_NAMESPACE

void wot::resetStockStatPing()
{
	BW_NAMESPACE s_ping = 0.f;
}

void wot::setStockLoadingTimer( bool enabled )
{
	BW_NAMESPACE s_loadingTimer = enabled;
}
