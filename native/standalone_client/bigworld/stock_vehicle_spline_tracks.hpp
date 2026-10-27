#pragma once

#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE
extern int StockVehicleSplineTracks_token;

// #1513 WGSplineTracksManager (constructed by 009c2530, singleton 01d211bc)
// owns the instanced spline-track renderer and every WGSplineTrack
// (type 01a20bb8). This client does not construct that owner.
bool isStockSplineTrack( PyObject * object );
BW_END_NAMESPACE
