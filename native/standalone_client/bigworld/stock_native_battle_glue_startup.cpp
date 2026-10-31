// Startup integration: the worker session's round entry points.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_internal.hpp"
#include "stock_native_battle_bots_needs.hpp"

namespace native_battle {
// The worker session's round: the same native start the visible client's shell makes, with no local leave callback.
bool startup::startRound(PyObject* config, const Value& message, std::string& error) {
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime"));
    auto shell = py::invoke(py::attr(module.get(), "BattleRuntime").get());
    auto runtime = py::invoke(py::attr(module.get(), "_load_runtime").get());
    // The worker has no Python LAN client; its native link answers the round's transport reads.
    p::Ref start_message(cs::write(message));
    const bool started = start(shell.get(), runtime.get(), config, start_message.get(), Py_None, Py_None);
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
void startup::receive(Battle& b, const std::string& kind, const std::shared_ptr<const Value>& message) {
    if (kind == "snapshot") { snapshot(b, message); return; }
    const Value& value = *message;
    if (kind == "events") {
        if (b.phase != Phase::Failed && b.phase != Phase::Stopped && b.phase != Phase::Leaving) events::receive(b, value);
    } else if (kind == "battle_live") battleLive(b, value);
    else if (kind == "bot_observation") visibility::observation(b, value);
    else if (kind == "player_destructible_contact") world::destructibleContact(b, value);
    else throw p::Error("unsupported worker round message: " + kind);
}

std::optional<std::array<double, 4>> startup::arenaBounds(const Battle& b) {
    return b.startup ? b.startup->arena_bounds : std::nullopt;
}
PyObject* startup::navigationGraph(const Battle& b) {
    return b.startup && b.startup->navigation_graph ? b.startup->navigation_graph.get() : Py_None;
}
}
