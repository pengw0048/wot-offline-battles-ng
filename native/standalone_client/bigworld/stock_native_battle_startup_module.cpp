// offline_native_battle: the Python entry points the BattleRuntime shell forwards lan_session/authority_worker calls to.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#include "pyscript/script.hpp"
#endif
#include "stock_native_battle_startup_internal.hpp"
#include "stock_native_battle_events.hpp"
#include "stock_native_battle_projectiles.hpp"
#include "stock_native_battle_visibility.hpp"
#include "stock_native_battle_world.hpp"
#include "stock_native_battle_bots.hpp"
#include "stock_native_battle_remote.hpp"

namespace native_battle {
namespace startup {
namespace {
template<class Body> PyObject* entry(Body body) {
    try { return body(); }
    catch (const std::exception& e) { if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, e.what()); return nullptr; }
}
PyObject* boolean(bool v) { return p::boolean(v).release(); }
// The current round when it may take a LAN message for `message`'s round.
Battle* round(PyObject* message) {
    Battle* b = current();
    if (!b || !b->startup) return nullptr;
    return b;
}
// A message entry point: f(battle, Value) -> bool, False when no round is active.
template<class Fn> PyObject* message(PyObject* args, Fn fn) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return entry([&] {
        Battle* b = round(raw);
        if (!b) return boolean(false);
        return boolean(fn(*b, fromPython(PyDict_Check(raw) ? raw : p::dict().get())));
    });
}

PyObject* startPy(PyObject*, PyObject* args) {
    PyObject *shell, *runtime, *config, *message_raw, *client, *on_leave;
    if (!PyArg_ParseTuple(args, "OOOOOO", &shell, &runtime, &config, &message_raw, &client, &on_leave)) return nullptr;
    return entry([&] { return boolean(start(shell, runtime, config, message_raw, client, on_leave)); });
}
PyObject* stopPy(PyObject*, PyObject* args) {
    PyObject* restore;
    if (!PyArg_ParseTuple(args, "O", &restore)) return nullptr;
    return entry([&] {
        if (Battle* b = current()) if (b->startup) stop(*b, p::flag(restore));
        Py_RETURN_NONE;
    });
}
PyObject* statePy(PyObject*, PyObject*) { return p::text(stateName(current())).release(); }
PyObject* errorPy(PyObject*, PyObject*) {
    Battle* b = current();
    if (!b || b->error.empty()) Py_RETURN_NONE;
    return p::text(b->error).release();
}
PyObject* restorePendingPy(PyObject*, PyObject*) {
    Battle* b = current();
    return boolean(b && b->startup && b->startup->restore_pending);
}
PyObject* pendingPresentationPy(PyObject*, PyObject*) {
    return entry([&] { Battle* b = current(); return boolean(b && b->startup && events::hasPendingPresentation(*b)); });
}
PyObject* snapshotPy(PyObject*, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return entry([&] {
        Battle* b = round(raw);
        if (b) snapshot(*b, raw);
        Py_RETURN_NONE;
    });
}
PyObject* eventsPy(PyObject*, PyObject* args) {
    return message(args, [](Battle& b, const Value& v) {
        if (b.phase == Phase::Failed || b.phase == Phase::Stopped || b.phase == Phase::Leaving) return false;
        events::receive(b, v);
        return true;
    });
}
PyObject* battleLivePy(PyObject*, PyObject* args) {
    return message(args, [](Battle& b, const Value& v) { const bool before = b.prebattle_deadline >= 0.; battleLive(b, v); return !before && b.prebattle_deadline >= 0.; });
}
PyObject* observationPy(PyObject*, PyObject* args) { return message(args, [](Battle& b, const Value& v) { return visibility::observation(b, v); }); }
PyObject* contactPy(PyObject*, PyObject* args) {
    return message(args, [](Battle& b, const Value& v) { world::destructibleContact(b, v); return true; });
}
PyObject* contactResultPy(PyObject*, PyObject* args) {
    return message(args, [](Battle& b, const Value& v) { world::destructibleContactResult(b, v); return true; });
}
PyObject* fireResultPy(PyObject*, PyObject* args) {
    return message(args, [](Battle& b, const Value& v) { projectiles::fireIntentResult(b, v); return true; });
}
PyObject* rosterPy(PyObject*, PyObject* args) { return message(args, [](Battle& b, const Value& v) { return roster(b, v); }); }
PyObject* chatPy(PyObject*, PyObject* args) { return message(args, [](Battle& b, const Value& v) { return teamChat(b, v); }); }
PyObject* chatAckPy(PyObject*, PyObject* args) { return message(args, [](Battle& b, const Value& v) { return teamChatAck(b, v); }); }
PyObject* commandPy(PyObject*, PyObject* args) { return message(args, [](Battle& b, const Value& v) { return teamCommand(b, v); }); }
PyObject* commandAckPy(PyObject*, PyObject* args) { return message(args, [](Battle& b, const Value& v) { return teamCommandAck(b, v); }); }
PyObject* commandTerminalPy(PyObject*, PyObject* args) { return message(args, [](Battle& b, const Value& v) { return teamCommandTerminal(b, v); }); }
PyObject* engineAudioInstallPy(PyObject*, PyObject*) { return entry([&] { return boolean(remote::installEngineAudio()); }); }
PyObject* engineAudioUninstallPy(PyObject*, PyObject*) { return entry([&] { remote::uninstallEngineAudio(); Py_RETURN_NONE; }); }
PyObject* replayLocalPy(PyObject*, PyObject* args) {
    PyObject* raw;
    if (!PyArg_ParseTuple(args, "O", &raw)) return nullptr;
    return entry([&] { if (Battle* b = round(raw)) replayLocal(*b, raw); Py_RETURN_NONE; });
}

PyMethodDef methods[] = {
    {"start", startPy, METH_VARARGS, "Start one LAN round: (shell, runtime, config, message, lan_client, on_local_leave)."},
    {"stop", stopPy, METH_VARARGS, "Stop the round; restore the lobby Account when asked."},
    {"state", statePy, METH_NOARGS, "The round state name."},
    {"error", errorPy, METH_NOARGS, "The round failure text, or None."},
    {"lobby_restore_pending", restorePendingPy, METH_NOARGS, "Whether teardown waits for its GUI boundary."},
    {"has_pending_presentation", pendingPresentationPy, METH_NOARGS, "Whether received combat is still unpresented."},
    {"on_snapshot", snapshotPy, METH_VARARGS, nullptr},
    {"on_events", eventsPy, METH_VARARGS, nullptr},
    {"on_battle_live", battleLivePy, METH_VARARGS, nullptr},
    {"on_bot_observation", observationPy, METH_VARARGS, nullptr},
    {"on_player_destructible_contact", contactPy, METH_VARARGS, nullptr},
    {"on_player_destructible_contact_result", contactResultPy, METH_VARARGS, nullptr},
    {"on_fire_intent_result", fireResultPy, METH_VARARGS, nullptr},
    {"on_roster", rosterPy, METH_VARARGS, nullptr},
    {"on_team_chat", chatPy, METH_VARARGS, nullptr},
    {"on_team_chat_ack", chatAckPy, METH_VARARGS, nullptr},
    {"on_team_command", commandPy, METH_VARARGS, nullptr},
    {"on_team_command_ack", commandAckPy, METH_VARARGS, nullptr},
    {"on_team_command_terminal", commandTerminalPy, METH_VARARGS, nullptr},
    {"apply_replay_local", replayLocalPy, METH_VARARGS, nullptr},
    {"engine_audio_install", engineAudioInstallPy, METH_NOARGS, "Wrap stock audition assembly with the native engine audio relay."},
    {"engine_audio_uninstall", engineAudioUninstallPy, METH_NOARGS, "Restore stock audition assembly."},
    {nullptr, nullptr, 0, nullptr}};
}

PyMethodDef* moduleMethods() { return methods; }
}
}

#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
namespace {
class NativeBattleModule final : public BW::Script::InitTimeJob {
public:
    NativeBattleModule() : BW::Script::InitTimeJob(1) {}
    void init() override {
        PyObject* module = Py_InitModule("offline_native_battle", native_battle::startup::moduleMethods());
        // The hidden worker's session entry points live in the same module.
        for (PyMethodDef* def = module ? native_battle::bots::worker::methods() : nullptr; def && def->ml_name; ++def) {
            PyObject* function = PyCFunction_NewEx(def, nullptr, nullptr);
            if (!function || PyModule_AddObject(module, def->ml_name, function) < 0) { module = nullptr; break; }
        }
        if (!module) {
            PyErr_Print();
            CRITICAL_MSG("Native battle module initialization failed\n");
        }
    }
};
NativeBattleModule nativeBattleModule;
}
#endif
