#pragma once
#include "stock_native_detached_turret_runtime.hpp"
#include "stock_native_detached_turret_launch.hpp"
#include "stock_native_client_state.hpp"
#include <string>
struct _object;
typedef _object PyObject;
namespace stock_native_detached_turret {
// These functions run only at cold/event ingress and publication. Physics
// retains no Python objects or callbacks. Wire readers set `error` when a row is malformed;
// descriptor readers read stock Python descriptors and report through the Python error.
bool readRow(const stock_native_client_state::Value&,Row&,std::string* error=nullptr);
bool readComponents(PyObject* descriptor,std::vector<Component>&);
bool readVehicleGeometry(PyObject* descriptor,VehicleGeometry&);
bool readLaunchGeometry(PyObject* descriptor,LaunchInput&);
bool readPlayerImpulses(const stock_native_client_state::Value& player,std::vector<PlayerImpulse>&,std::string* error=nullptr);
// One player's turret_pushes checkpoints (absent, null or a list) attributed to that player.
bool readPlayerImpulses(std::int64_t player,const stock_native_client_state::Value* pushes,std::vector<PlayerImpulse>&,std::string* error=nullptr);
stock_native_client_state::Value packRow(const Row&);
stock_native_bot_turret::Record obstacleRecord(const Row&);
}
