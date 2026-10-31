#pragma once
// Remote vehicle presentation: the remote factory, replica interpolation, and pose/aim/track feeds of stock entities.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Remote {};

namespace remote {
// Accepted authority messages into the replica (cs::State) and the vehicle lifecycle.
void receiveManifest(Battle& b, const Value& message);
void receiveSnapshot(Battle& b, const Value& message, double now);
// Per frame: interpolation and the native pose/aim/track/motion writes of replica-owned vehicles.
void advance(Battle& b, double now);
// Whether the replica owns this vehicle's pose (the lifecycle must not write it).
bool owns(const Battle& b, const Actor& vehicle);
// Remote factory: create/readiness/destroy of presentation entities.
p::Ref create(Battle& b, PyObject* descriptor, PyObject* properties, const Point& position, double yaw);
bool isReady(Battle& b, std::int64_t engine_id);
void destroy(Battle& b, std::int64_t engine_id);
p::Ref get(const Battle& b, std::int64_t engine_id);
p::Ref prepareDescriptor(Battle& b, PyObject* descriptor);
void requestWreck(Battle& b, std::int64_t engine_id);
void setDrawVisibility(Battle& b, PyObject* vehicle, bool visible);
}
}
