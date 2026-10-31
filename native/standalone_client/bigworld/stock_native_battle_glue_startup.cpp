// Startup integration: the worker session's round entry points and the BotRuntime resolver bundle.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_internal.hpp"
#include "stock_native_battle_bots_needs.hpp"

namespace native_battle {
namespace {
constexpr double WORKER_CONTROL_SECONDS = 0.10;

Battle& resolving() {
    Battle* b = current();
    if (!b || !b->startup) throw p::Error("Bot resolver called without a native round");
    return *b;
}
template<class Body> PyObject* guarded(Body body) {
    try { return body().release(); }
    catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyObject* descriptorResolver(PyObject*, PyObject* name) {
    return guarded([&] { return startup::descriptorFor(resolving(), p::string(name)); });
}
PyObject* playerDescriptorResolver(PyObject*, PyObject* state) {
    return guarded([&] { return startup::playerDescriptor(resolving(), startup::fromPython(state)); });
}
// The pinned _select_bot_vehicle: the requested type, else the round's (team, slot) assignment, else the config type.
PyObject* vehicleSelector(PyObject*, PyObject* raw) {
    return guarded([&] {
        auto requested = PyDict_Check(raw) ? PyDict_GetItemString(raw, "vehicle") : nullptr;
        if (requested && p::flag(requested)) return p::retain(requested);
        Battle& b = resolving();
        auto team = PyDict_Check(raw) ? PyDict_GetItemString(raw, "team") : nullptr;
        auto slot = PyDict_Check(raw) ? PyDict_GetItemString(raw, "slot") : nullptr;
        const auto& assigned = bots::vehicleAssignments(b);
        const auto found = assigned.find({team ? p::wide(team) : 1, slot ? p::wide(slot) : 0});
        if (found != assigned.end()) return p::text(found->second);
        return p::text(b.config.resolved().get("vehicle").resolved().text);
    });
}
PyObject* spawnResolver(PyObject*, PyObject* args) {
    PyObject *team, *slot;
    if (!PyArg_ParseTuple(args, "OO", &team, &slot)) return nullptr;
    return guarded([&] {
        const auto pose = startup::formationPose(resolving(), p::wide(team), p::wide(slot));
        return p::Ref(Py_BuildValue("((ddd)d)", pose.first[0], pose.first[1], pose.first[2], pose.second));
    });
}
PyObject* equipmentResolver(PyObject*, PyObject*) {
    return guarded([&] {
        Battle& b = resolving();
        p::Ref mechanics(PyImport_ImportModule("gui.mods.offline_lan_0922.equipment_mechanics"));
        auto consumables = py::call(mechanics.get(), "default_bot_consumables", py::attr(b.engine.vehicles.get(), "g_cache").get());
        return p::Ref(PySequence_Tuple(consumables.get()));
    });
}
PyMethodDef resolvers[] = {
    {"descriptor_resolver", descriptorResolver, METH_O, nullptr},
    {"player_descriptor_resolver", playerDescriptorResolver, METH_O, nullptr},
    {"vehicle_selector", vehicleSelector, METH_O, nullptr},
    {"spawn_resolver", spawnResolver, METH_VARARGS, nullptr},
    {"bot_equipment_resolver", equipmentResolver, METH_NOARGS, nullptr},
};
}

// The pinned BattleRuntime's BotRuntime(...) construction; only its round-start manifest still runs in Python.
p::Ref startup::createBotRuntime(Battle& b) {
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.bot_runtime"));
    auto kwargs = p::dict();
    for (auto& def : resolvers) p::set(kwargs.get(), def.ml_name, p::Ref(PyCFunction_NewEx(&def, nullptr, nullptr)));
    const auto& s = *b.startup;
    if (s.arena_bounds) {
        const auto& a = *s.arena_bounds;
        p::set(kwargs.get(), "arena_bounds", p::Ref(Py_BuildValue("(dddd)", a[0], a[1], a[2], a[3])));
    }
    if (s.navigation_graph) p::set(kwargs.get(), "baked_graph", p::retain(s.navigation_graph.get()));
    if (b.worker) p::set(kwargs.get(), "control_seconds", p::number(WORKER_CONTROL_SECONDS));
    p::Ref args(Py_BuildValue("(L)", static_cast<PY_LONG_LONG>(b.local_player)));
    auto type = py::attr(module.get(), "BotRuntime");
    p::Ref runtime(PyObject_Call(type.get(), args.get(), kwargs.get()));
    py::set(runtime.get(), "debug_logging", p::boolean(b.config.resolved().get("debug_logging").truth()).get());
    return runtime;
}

// The worker session's round: the same native start the visible client's shell makes, with no local leave callback.
bool startup::startRound(PyObject* config, PyObject* message, PyObject* client, std::string& error) {
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime"));
    auto shell = py::invoke(py::attr(module.get(), "BattleRuntime").get());
    auto runtime = py::invoke(py::attr(module.get(), "_load_runtime").get());
    const bool started = start(shell.get(), runtime.get(), config, message, client, Py_None);
    if (!started) {
        Battle* b = current();
        error = b ? b->error : std::string();
    }
    return started;
}

void startup::stopRound(bool restore_account) {
    if (Battle* b = current()) if (b->startup) stop(*b, restore_account);
}

// One worker LAN message for the current round, dispatched as the shell's on_* entry points do.
void startup::receive(Battle& b, const std::string& kind, PyObject* message) {
    if (kind == "snapshot") { snapshot(b, message); return; }
    const Value value = fromPython(message);
    if (kind == "events") {
        if (b.phase != Phase::Failed && b.phase != Phase::Stopped && b.phase != Phase::Leaving) events::receive(b, value);
    } else if (kind == "battle_live") battleLive(b, value);
    else if (kind == "bot_observation") visibility::observation(b, value);
    else if (kind == "player_destructible_contact") world::destructibleContact(b, value);
    else throw p::Error("unsupported worker round message: " + kind);
}
}
