// The postmortem viewpoint: the server reattach and client callback transaction of #1513 viewpoint switching.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_feedback_internal.hpp"
#include <algorithm>

namespace native_battle {
namespace feedback {
namespace {
std::int64_t toInt(PyObject* v) { return p::wide(p::Ref(PyNumber_Int(v)).get()); }
std::int64_t localEngine(const Battle& b) {
    if (!b.engine.server) return 0;
    auto id = py::optional(b.engine.server.get(), "vehicle_id");
    return id.get() == Py_None ? 0 : toInt(id.get());
}
std::int64_t clientTeam(const Battle& b) {
    auto team = py::optional(b.engine.client.get(), "team");
    return p::flag(team.get()) ? toInt(team.get()) : 0;
}

// BattleRuntime._spectator_record: one server-valid postmortem vehicle target (vehicle, entity).
std::pair<Actor*, p::Ref> spectator(Battle& b, std::int64_t engine_id, bool allow_self) {
    for (auto& entry : b.vehicles->actors) {
        Actor& v = *entry.second;
        if (v.engine_id != engine_id) continue;
        const auto& team = field(v.presented.state, "team");
        if (v.tombstone || !v.ready || (team.truth() ? integer(team) : 0) != clientTeam(b)) return {nullptr, p::retain(Py_None)};
        auto entity = b.entity(engine_id);
        if (entity.get() == Py_None || py::optional(entity.get(), "matrix").get() == Py_None) return {nullptr, p::retain(Py_None)};
        if (v.local ? !allow_self : !vehicles::alive(b, v, entity.get())) return {nullptr, p::retain(Py_None)};
        return {&v, std::move(entity)};
    }
    return {nullptr, p::retain(Py_None)};
}
}

p::Ref spectatorEntity(Battle& b, std::int64_t engine_id) { return spectator(b, engine_id, false).second; }
std::optional<std::int64_t> spectated(const Battle& b) { return b.feedback->spectated_engine_id; }
void setSpectated(Battle& b, std::optional<std::int64_t> engine_id) { b.feedback->spectated_engine_id = engine_id; }

// BattleRuntime._switch_postmortem_viewpoint(is_viewpoint, engine_id)
bool switchPostmortemViewpoint(Battle& b, bool is_viewpoint, std::int64_t engine_id) {
    if (!b.running() || is_viewpoint) return false;
    auto avatar = b.engine.avatar.get();
    auto handler = py::optional(avatar, "inputHandler");
    auto modes = py::optional(py::optional(b.engine.modules.get(), "avatar_input_handler").get(), "_CTRL_MODE");
    if (handler.get() == Py_None || modes.get() == Py_None) return false;
    const int postmortem = PyObject_RichCompareBool(py::optional(handler.get(), "_AvatarInputHandler__ctrlModeName").get(),
                                                    py::attr(modes.get(), "POSTMORTEM").get(), Py_EQ);
    if (postmortem < 0) throw p::Error("control mode comparison failed");
    if (!postmortem) return false;
    auto control = py::optional(handler.get(), "_AvatarInputHandler__curCtrl");
    if (control.get() == Py_None || py::optional(control.get(), "curPostmortemDelay").get() != Py_None) return false;
    auto found = spectator(b, engine_id, engine_id == localEngine(b));
    Actor* vehicle = found.first;
    if (!vehicle) return false;
    PyObject* entity = found.second.get();
    auto matrices = py::optional(avatar, "consistentMatrices");
    auto attached = py::optional(matrices.get(), "attachedVehicleMatrix");
    auto setter = py::optional(matrices.get(), "_ConsistentMatrices__setTarget");
    auto camera = py::optional(control.get(), "_PostMortemControlMode__cam");
    auto callback = py::optional(avatar, "onSwitchViewpoint");
    if (attached.get() == Py_None || !py::callable(setter) || camera.get() == Py_None || !py::callable(callback))
        return false;
    PyObject* previous_target = PyObject_GetAttrString(attached.get(), "target");
    PyObject* previous_camera = previous_target ? PyObject_GetAttrString(camera.get(), "vehicleMProv") : nullptr;
    if (!previous_camera) {
        Py_XDECREF(previous_target);
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) { PyErr_Clear(); return false; }
        throw p::Error("postmortem matrix lookup failed");
    }
    p::Ref held_target(previous_target), held_camera(previous_camera);
    auto target_matrix = vehicle->local ? local::bodyPose(b) : py::attr(entity, "matrix");
    if (target_matrix.get() == Py_None) return false;
    std::optional<std::int64_t> previous_vehicle;
    try {
        // A retail cell attachment changes Avatar.vehicle first; copy its Python-visible result, then the stock callback.
        if (!vehicle->local) py::set(entity, "_postmortem_visible", Py_True);
        // #1513 BigWorld.entities has no membership slot; the public lookup is the authoritative identity check.
        if (py::call(b.engine.bigworld.get(), "entity", p::integer(engine_id).get()).get() != entity)
            throw p::Error("#1513 spectator entity lookup was rejected");
        previous_vehicle = compat::setPostmortemVehicle(b, engine_id);
        py::invoke(setter.get(), target_matrix.get(), Py_False);
        if (py::attr(attached.get(), "target").get() != target_matrix.get()) throw p::Error("#1513 spectator matrix attachment was rejected");
        py::set(camera.get(), "vehicleMProv", attached.get());
        if (py::attr(camera.get(), "vehicleMProv").get() != attached.get()) throw p::Error("#1513 spectator camera attachment was rejected");
        py::invoke(callback.get(), p::integer(engine_id).get(), py::call(b.engine.math.get(), "Vector3", p::number(0.).get(), p::number(0.).get(),
                                                                         p::number(0.).get()).get());
    } catch (...) {
        PyObject *type, *value, *trace;
        PyErr_Fetch(&type, &value, &trace);
        try {
            if (previous_vehicle) compat::setPostmortemVehicle(b, *previous_vehicle);
            py::invoke(setter.get(), held_target.get(), Py_False);
            py::set(camera.get(), "vehicleMProv", held_camera.get());
        } catch (...) { PyErr_Clear(); }
        if (!vehicle->local && PyObject_SetAttrString(entity, "_postmortem_visible", Py_False) < 0) PyErr_Clear();
        PyErr_Restore(type, value, trace);
        throw;
    }
    auto& fb = *b.feedback;
    if (fb.spectated_engine_id && *fb.spectated_engine_id != engine_id) {
        auto previous = b.entity(*fb.spectated_engine_id);
        if (previous.get() != Py_None && p::flag(py::optional(previous.get(), "_offlineLANPresentation").get()))
            py::set(previous.get(), "_postmortem_visible", Py_False);
    }
    fb.spectated_engine_id = engine_id;
    // The vehicle AOI is now centred elsewhere: re-evaluate spotting on the next frame.
    visibility::resetSpottingTimer(b);
    return true;
}

// BattleRuntime._fallback_postmortem_viewpoint: move off a dead or removed observed ally, nearest first.
bool fallbackPostmortemViewpoint(Battle& b, std::int64_t excluded) {
    if (b.feedback->spectated_engine_id != excluded) return false;
    const Point origin = b.local ? b.local->position : Point{};
    std::vector<std::pair<double, std::int64_t>> candidates;
    for (auto& entry : b.vehicles->actors) {
        const std::int64_t engine_id = entry.second->engine_id;
        if (!engine_id || engine_id == excluded) continue;
        auto found = spectator(b, engine_id, false);
        if (!found.first) continue;
        auto at = py::attr(found.second.get(), "position");
        const double dx = p::scalar(py::attr(at.get(), "x").get()) - origin[0], dz = p::scalar(py::attr(at.get(), "z").get()) - origin[2];
        candidates.emplace_back(dx * dx + dz * dz, engine_id);
    }
    std::sort(candidates.begin(), candidates.end());
    for (const auto& candidate : candidates)
        if (switchPostmortemViewpoint(b, false, candidate.second)) return true;
    const std::int64_t local_id = localEngine(b);
    if (local_id && local_id != excluded) return switchPostmortemViewpoint(b, false, local_id);
    releasePostmortemVisibility(b);
    return false;
}

// BattleRuntime._release_postmortem_visibility
bool releasePostmortemVisibility(Battle& b) {
    if (b.compat) compat::clearPostmortemVehicle(b);
    if (!b.feedback) return false;
    const auto engine_id = b.feedback->spectated_engine_id;
    b.feedback->spectated_engine_id.reset();
    if (!engine_id) return false;
    auto entity = b.entity(*engine_id);
    if (entity.get() == Py_None || !p::flag(py::optional(entity.get(), "_offlineLANPresentation").get())) return false;
    py::set(entity.get(), "_postmortem_visible", Py_False);
    return true;
}
}
}
