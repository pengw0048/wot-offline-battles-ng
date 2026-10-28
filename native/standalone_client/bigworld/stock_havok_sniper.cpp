#include "pch.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE

// #1513 00653dd0 validates one bool through Script::setData (00690570),
// then returns None at 00653e10. There is no Havok call or state write in
// this build. In particular, this does not own binoculars, particle quality,
// vehicle physics or tree hiding; SniperControlMode updates those separately.
// Preserve this boundary so SniperControlMode.destroy can finish and the
// enclosing AvatarInputHandler.stop can retire its events and callbacks.
static void wg_havokSetSniperMode( bool /* enabled */ )
{
}
PY_AUTO_MODULE_FUNCTION( RETVOID, wg_havokSetSniperMode, ARG( bool, END ), BigWorld )

BW_END_NAMESPACE
