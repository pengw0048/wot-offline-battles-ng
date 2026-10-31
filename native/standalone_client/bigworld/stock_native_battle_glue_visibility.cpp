// Integration glue: stock collision hooks and turret presentation reach the visibility query owner.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_compat_internal.hpp"
#include "stock_native_battle_world_imports.hpp"
#include "stock_native_battle_visibility_core.hpp"

namespace native_battle {
namespace {
// One offline.client.query capsule call; None when this round has no query owner.
template<class... A> p::Ref queryCall(Battle& b, PyObject* (*method)(PyObject*, PyObject*), A... args) {
    PyObject* owner = visibility::queryOwner(b);
    if (!owner) return p::retain(Py_None);
    p::Ref arguments(PyTuple_Pack(Py_ssize_t(1 + sizeof...(A)), owner, static_cast<PyObject*>(args)...));
    return p::Ref(method(nullptr, arguments.get()));
}
}

namespace visibility {
// native_client_query.collisions
p::Ref collisions(Battle& b, PyObject* entity, PyObject* start, PyObject* end) {
    return queryCall(b, stock_native_client_query::collisions, entity, start, end);
}
// native_client_query.segment_may_hit
p::Ref segmentMayHit(Battle& b, PyObject* entity, PyObject* start, PyObject* end) {
    return queryCall(b, stock_native_client_query::segmentMayHit, entity, start, end);
}
// native_client_query.collidable: the query's candidates plus stock ProjectileAwareEntities.
p::Ref collidableEntities(Battle& b, PyObject* except_ids, PyObject* start, PyObject* end) {
    if (!queryOwner(b)) return p::retain(Py_None);
    p::Ref mover(PyImport_ImportModule("ProjectileMover"));
    auto extras = py::attr(py::attr(mover.get(), "ProjectileAwareEntities").get(), "entities");
    return queryCall(b, stock_native_client_query::candidates, except_ids, start, end, extras.get());
}
// spotting.VEHICLE_AOI_RADIUS: #1513 constants.AOI.VEHICLE_CIRCULAR_AOI_RADIUS.
double vehicleAoiRadius() { return 565.; }
// The postmortem camera's vehicle: one owner, the feedback postmortem state.
std::optional<std::int64_t> spectatedEngineId(const Battle& b) { return b.feedback ? feedback::spectated(b) : std::nullopt; }
}
}
