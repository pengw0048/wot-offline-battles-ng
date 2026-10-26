#pragma once

#include "pyscript/pyobject_plus.hpp"

namespace wot
{
class VehiclePhysics;
struct VehiclePhysicsConfiguration;
struct VehicleNativeMath;
}

BW_BEGIN_NAMESPACE

extern int StockVehiclePhysics_token;

// Strict source-side Reader for the native Tank configuration pipeline.
// Failure leaves output unchanged and preserves the Python exception.
bool readStockVehiclePhysicsConfiguration( PyObject * object,
    wot::VehiclePhysicsConfiguration & output );

// WGVehiclePhysics.configure without the Python method shell. On failure a
// Python exception is set and the previous Tank graph remains live.
bool configureStockVehiclePhysics( wot::VehiclePhysics & core, PyObject * object,
    const wot::VehicleNativeMath & math );

// The returned core belongs to the Python object. A caller retaining it must
// also retain that object, as the stock filter and simulator do.
// A type mismatch sets TypeError and returns NULL.
wot::VehiclePhysics * unwrapStockVehiclePhysics( PyObject * object );

BW_END_NAMESPACE
