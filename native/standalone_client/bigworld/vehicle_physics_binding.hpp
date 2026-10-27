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

// Source-side Reader with the #1513 catch regions: a missing or mistyped
// field inside one keeps constructor or previous values. A failure outside
// every region, or a CPython error from a conversion callback, returns false
// with a Python exception and leaves output unchanged. configured says whether
// the target Tank already holds mode settings (f84e80 reads differently).
bool readStockVehiclePhysicsConfiguration( PyObject * object,
    wot::VehiclePhysicsConfiguration & output, bool configured = true );

// BigWorld.wg_setupGroundMaterialsMap without the Python function shell.
bool setupStockGroundMaterialsMap( PyObject * groups );

// WGVehiclePhysics.configure without the Python method shell. On failure a
// Python exception is set and the previous Tank graph remains live.
bool configureStockVehiclePhysics( wot::VehiclePhysics & core, PyObject * object,
    const wot::VehicleNativeMath & math );

// The returned core belongs to the Python object. A caller retaining it must
// also retain that object, as the stock filter and simulator do.
// A type mismatch sets TypeError and returns NULL.
wot::VehiclePhysics * unwrapStockVehiclePhysics( PyObject * object );

BW_END_NAMESPACE
