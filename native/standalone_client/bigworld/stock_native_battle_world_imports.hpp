#pragma once
// Calls the world makes into other subsystems. Each owner adopts its declaration into its own header.
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle.hpp"
#include "stock_native_client_drive_python.hpp"

namespace native_battle {
namespace local {
// The visible client's native drive step over world::sceneAccess (native_client_drive.frame without its own
// remapping/body update/ledger sync, which the world space now owns); the worker has none.
// Rolls one rejected local destructible prediction back; server_pose is (position, yaw) when the worker sent one.
bool destructibleRejection(Battle& b, std::int64_t sequence, const std::optional<std::pair<Point, double>>& server_pose);
}

namespace bots {
// The worker's native Bot runtime is installed (native_bot_runtime.enabled).
bool enabled(const Battle& b);
std::optional<bool> detachedIngress(Battle& b, const Value& message);
bool detachedQueue(Battle& b, Actor& vehicle, PyObject* entity, const Value* state);
std::size_t detachedAdvance(Battle& b, double now);
void prewarmBots(Battle& b, double now);
std::size_t playerContacts(Battle& b, const Value& players, double now);
// Foliage, motion and registry step of the worker Bot world (native_bot_runtime.frame) over world::sceneAccess,
// without its own events.mappingChanged / backend.runtime().update, which the world space now owns.
bool frame(Battle& b, double now, bool force);
// The Bot runtime's current state of one Bot (mass, collision_shape, ram_profile, vehicle, team), or null.
const Value* currentState(const Battle& b, std::int64_t bot_id);
}

namespace projectiles {
// Server time at `now` from the last snapshot's server stamp and its local receipt time.
std::optional<double> serverTimeMs(const Battle& b, double now);
}

namespace visibility {
// The engine id the postmortem camera follows, if any.
std::optional<std::int64_t> spectatedEngineId(const Battle& b);
// spotting.VEHICLE_AOI_RADIUS (565 m): the render/AOI range of vehicles and their detached turrets.
double vehicleAoiRadius();
}
}
