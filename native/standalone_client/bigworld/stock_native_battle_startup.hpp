#pragma once
// Round startup and teardown, LAN session entry points, replay recording and frame diagnostics.
#include "stock_native_battle.hpp"

namespace native_battle {
struct Startup {
    Object frame_callback, ammo_callback;
    Object arena_type;              // the stock ArenaType of this round's map
    std::map<std::string, Object> descriptors;   // one prepared descriptor per vehicle type (or donated compact descr)
    std::vector<std::string> prepared_names;     // types proven loadable this round, offered as substitutes
};
struct Diagnostics {};

namespace startup {
// LAN session entry points (lan_session.py battle dispatch) and the round lifecycle.
void start(PyObject* shell, PyObject* start_message, PyObject* client, PyObject* runtime_modules, PyObject* config);
void stop(Battle& b, const std::string& reason);
void snapshot(Battle& b, const Value& message);
void battleLive(Battle& b, const Value& message);
// Replay recording and frame diagnostics.
void recordFrame(Battle& b);
void frame(Battle& b);
void beginBattle(Battle& b);
bool prebattleTransitionReady(Battle& b, double now);
void startTeamChatWhenRosterReady(Battle& b);
void maybeSendBattleReady(Battle& b);
}
}
