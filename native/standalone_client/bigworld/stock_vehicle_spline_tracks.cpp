#include "pch.hpp"
#include "stock_vehicle_spline_tracks.hpp"

BW_BEGIN_NAMESPACE
int StockVehicleSplineTracks_token = 1;

bool isStockSplineTrack( PyObject * )
{
    // Only the manager-owned factory produces a WGSplineTrack.
    return false;
}

static PyObject * py_wg_createSplineTrack( PyObject * )
{
    // Exact 0065834b: with no spline-track manager the function returns None
    // before parsing "OOsfOfOfsii". setupSplineTracks then attaches nothing
    // and hands (None, None) to WGVehicleFashion.setSplineTrack.
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION( wg_createSplineTrack, BigWorld )
BW_END_NAMESPACE
