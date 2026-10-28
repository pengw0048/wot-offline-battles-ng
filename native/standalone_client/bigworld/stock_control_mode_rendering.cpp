#include "pch.hpp"
#include "stock_control_mode_rendering.hpp"
#include "pyscript/script.hpp"
#include "romp/flora.hpp"

BW_BEGIN_NAMESPACE
namespace
{
// #1513 01d21158; 00636ba5 copies this into the primary scene draw context.
bool s_lowDetailedMode = false;
}

bool stockLowDetailedMode()
{
    return s_lowDetailedMode;
}

static void wg_setLowDetailedMode( bool enabled )
{
    // 006368e0: this selects authored decoration suppression, not a guessed
    // distance/LOD multiplier. Models and trees consume their own flag bits.
    s_lowDetailedMode = enabled;
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_setLowDetailedMode, ARG( bool, END ), BigWorld )

static void setFloraEnabled( bool enabled )
{
    // 00a3a470 writes the same Flora owner read by draw and population/update.
    // Keep this independent of the sniper grass preference/binoculars gate.
    Flora::enabled( enabled );
}
PY_AUTO_MODULE_FUNCTION( RETVOID, setFloraEnabled, ARG( bool, END ), BigWorld )
BW_END_NAMESPACE
