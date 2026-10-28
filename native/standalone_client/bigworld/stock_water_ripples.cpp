#ifndef WOT_STOCK_WATER_RIPPLES_PORTABLE
#include "pch.hpp"
#include "stock_water_ripples.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "romp/water.hpp"
#endif

BW_BEGIN_NAMESPACE
int StockWaterRipples_token = 1;

// #1513 f625e0: every registered Water receives a vertical ten-metre
// movement. Water::addMovement owns readiness, visibility, distance, volume
// clipping and cell selection; SimulationManager consumes its cell impulses.
static PyObject* py_wg_addWaterRipples(PyObject* args)
{
    PyObject *positionObject, *diameterObject;
    Vector3 position;
    float diameter;
    if (!PyArg_UnpackTuple(args, "wg_addWaterRipples", 2, 2,
            &positionObject, &diameterObject) ||
        Script::setData(positionObject, position, "argument 1") ||
        Script::setData(diameterObject, diameter, "argument 2"))
        return NULL;
    Waters::instance().addMovement(position + Vector3(0.f, 5.f, 0.f),
        position - Vector3(0.f, 5.f, 0.f), diameter);
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(wg_addWaterRipples, BigWorld)
BW_END_NAMESPACE
