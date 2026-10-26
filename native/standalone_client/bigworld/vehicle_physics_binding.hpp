#pragma once

#include "pyscript/pyobject_plus.hpp"

namespace wot
{
class VehiclePhysics;
struct VehiclePhysicsConfiguration;
struct VehicleBodyConfiguration;
struct VehicleShapeConfiguration;
struct VehicleChassisConfiguration;
struct VehicleTankGeometry;
}

BW_BEGIN_NAMESPACE

extern int StockVehiclePhysics_token;

// Strict source-side Reader for the native Tank configuration pipeline.
// Failure leaves output unchanged and preserves the Python exception. This
// does not expose a successful configure method before Tank construction.
bool readStockVehiclePhysicsConfiguration( PyObject * object,
    wot::VehiclePhysicsConfiguration & output );

// Actual D3DX-backed geometry construction, before physical Tank ownership.
// This is a native construction stage, not a Python configure entry point.
wot::VehicleTankGeometry buildStockVehicleTankGeometry(
    const wot::VehicleBodyConfiguration & body,
    const wot::VehicleShapeConfiguration & shape,
    const wot::VehicleChassisConfiguration & chassis );

// The returned core belongs to the Python object. A caller retaining it must
// also retain that object, as the stock filter and simulator do.
// A type mismatch sets TypeError and returns NULL.
wot::VehiclePhysics * unwrapStockVehiclePhysics( PyObject * object );

BW_END_NAMESPACE
