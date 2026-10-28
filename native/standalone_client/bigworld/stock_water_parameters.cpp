#include "pch.hpp"
#include "romp/water.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE
// 654a60, 654960 and 6549e0 each convert one float and assign a public Water
// static. Water::setupRenderState publishes s_fTexScale as "texScale"; #1513
// has no reader of the two frequencies, which use per-surface state instead.
void wg_setWaterTexScale(float value)
{
    Water::s_fTexScale = value;
}
PY_AUTO_MODULE_FUNCTION(RETVOID, wg_setWaterTexScale, ARG(float, END), BigWorld)

void wg_setWaterFreqX(float value)
{
    Water::s_fFreqX = value;
}
PY_AUTO_MODULE_FUNCTION(RETVOID, wg_setWaterFreqX, ARG(float, END), BigWorld)

void wg_setWaterFreqZ(float value)
{
    Water::s_fFreqZ = value;
}
PY_AUTO_MODULE_FUNCTION(RETVOID, wg_setWaterFreqZ, ARG(float, END), BigWorld)
BW_END_NAMESPACE
