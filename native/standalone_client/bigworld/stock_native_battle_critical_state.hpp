#pragma once
// Simulation consumers read the same native damage owner that accepts authority payloads.
#include "stock_native_battle_critical.hpp"
#include "stock_native_bot_step_state.hpp"

namespace native_battle {
namespace critical {
inline wot::offline_bot_step::CriticalState simulationState(const Actor& actor, PyObject* entity,
                                                          const std::map<std::string, double>& maxima) {
    const auto* b = current();
    if (!b || !b->running() || !b->vehicles || b->vehicles->retired || !b->critical ||
        actor.engine_id <= 0 || actor.tombstone || actor.missing || !actor.ready ||
        b->vehicleByEngine(actor.engine_id) != &actor || !entity || entity == Py_None ||
        p::wide(py::attr(entity, "id").get()) != actor.engine_id || b->entity(actor.engine_id).get() != entity)
        throw p::Error("Native simulation critical owner is stale");
    const auto at = b->critical->vehicles.find(actor.engine_id);
    if (at == b->critical->vehicles.end()) throw p::Error("Native simulation critical state is unavailable");
    const auto& damage = at->second;
    wot::offline_bot_step::CriticalState out;
    out.crew_ko = damage.crew_ko;
    out.fire = damage.fire;
    out.ammo_rack_death = damage.ammo_rack_death;
    for (const auto& entry : damage.devices) out.devices[entry.first].hp = entry.second;
    for (const auto& name : damage.destroyed) out.devices[name].destroyed = true;
    for (const auto& name : damage.critical)
        if (!damage.destroyed.count(name)) out.devices[name].critical = true;
    for (auto& entry : out.devices) {
        const auto maximum = maxima.find(entry.first);
        if (maximum != maxima.end()) entry.second.maximum = maximum->second;
    }
    return out;
}
}
}
