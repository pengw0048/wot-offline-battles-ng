// Integration glue: the calls startup and the stock compat hooks make into remote presentation.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_needs.hpp"
#include "stock_native_battle_compat_internal.hpp"

namespace native_battle {
namespace remote {
// The round's native remote factory: visible remotes keep their presentation pose, the worker owns authority geometry.
void start(Battle& b, PyObject* local_descriptor) {
    install(b, false, b.worker);
    prepareDescriptor(b, local_descriptor);
}
// The native factory (NativeRemoteVehicleFactory) never prewarms wreck resources: Vehicle.onHealthChanged owns them.
bool prewarmWrecksEnabled(Battle&) { return false; }
std::size_t wreckPrewarmPending(Battle&) { return 0; }
void abandonWreckPrewarm(Battle&) {}

void closeStockPresentationExtras(Battle&, PyObject* appearance, bool visible) { closePresentationExtras(appearance, visible); }

// compat.visible_native_remote_collisions after the query owner declined: collide a native remote at its drawn pose.
p::Ref visibleCollisions(Battle& b, PyObject* vehicle, PyObject* start, PyObject* end) {
    const Overlay* row = overlay(vehicle, false);
    PyObject* drawn = row && row->pose_active ? row->field("matrix") : nullptr;
    if (!drawn) return p::retain(Py_None);
    auto id = py::optional(vehicle, "id");
    const Actor* actor = id.get() == Py_None ? nullptr : b.vehicleByEngine(p::wide(id.get()));
    if (!actor || !actor->native_remote) return p::retain(Py_None);
    return collideAtMatrix(b, vehicle, drawn, start, end);
}
}
}
