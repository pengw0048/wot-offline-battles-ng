#pragma once
#include "stock_native_detached_turret_runtime.hpp"
#include "stock_native_detached_turret_launch.hpp"
struct _object;
typedef _object PyObject;
namespace stock_native_detached_turret {
// These functions run only at cold/event ingress and publication. Physics
// retains no Python objects or callbacks.
bool readRow(PyObject*,Row&);
bool readComponents(PyObject* descriptor,std::vector<Component>&);
bool readVehicleGeometry(PyObject* descriptor,VehicleGeometry&);
bool readLaunchGeometry(PyObject* descriptor,LaunchInput&);
bool readPlayerImpulses(PyObject* player,std::vector<PlayerImpulse>&);
PyObject* packRow(const Row&);
stock_native_bot_turret::Record obstacleRecord(const Row&);
}
