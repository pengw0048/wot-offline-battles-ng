#include "pch.hpp"
#include "stock_destructible_falling_solver_data.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE
float wg_solveDestructibleFallPitch(float weight,float angularStiffness,float basePitch,float initialPitch)
{
    return wot::solveStockDestructibleFallPitch(weight,angularStiffness,basePitch,initialPitch);
}
PY_AUTO_MODULE_FUNCTION(RETDATA,wg_solveDestructibleFallPitch,
    ARG(float,ARG(float,ARG(float,ARG(float,END)))),BigWorld)
BW_END_NAMESPACE
