#pragma once
// Vehicle subsystem internals shared by its translation units.
#include "stock_native_battle.hpp"
#include "stock_native_battle_vehicles.hpp"
#include <utility>

namespace native_battle {
namespace vehicles {
// Descriptors: the donated mounted descriptor of a human, else one shared prepared descriptor per type.
p::Ref playerDescriptor(Battle& b, const Value& state);
p::Ref descriptorFor(Battle& b, const std::string& vehicle_name);
// Spawn placement on the ground: (position, yaw).
std::pair<Point, double> worldPose(Battle& b, const Value& state);
std::optional<double> groundY(Battle& b, double x, double z, double hint, bool allow_wide);
std::optional<std::int64_t> outfitSeason(Battle& b);
bool botAuthorityIsLocal(Battle& b, const Value& player_id);
bool botInitiallyVisible(Battle& b, std::int64_t team);
p::Ref vector(Battle& b, const Point& at);
p::Ref rotation(double yaw, double pitch, double roll);
std::int64_t attackReason(Battle& b, const char* member, std::int64_t fallback);


// Lifecycle.
void apply(Battle& b, const Value& event);
void retire(Battle& b, Actor& v);
void create(Battle& b, Actor& v, const Value& event);
bool ready(Battle& b, Actor& v);
void update(Battle& b, Actor& v, const Value& event);
void destroy(Battle& b, const std::string& key, const Value& event);
void flushTombstone(Battle& b, Actor& v);
bool queueBotCreate(Battle& b, const Value& event);

// Presentation edges.
void applyPose(Battle& b, Actor& v, const Pose& pose);
bool applySiege(Battle& b, Actor& v, const Value& state);
bool applyStatistics(Battle& b, Actor& v, const Value& state);
bool applyStun(Battle& b, Actor& v, const Value& state);
bool reconcileCriticalAuthority(Battle& b, Actor& v, const Value& source, Value* canonical);
bool presentCritical(Battle& b, Actor& v, const Value& events, std::int64_t attacker);
// Retry only undelivered current critical HUD edges, after the frame's repair step.
void flushCriticalPresentations(Battle& b);
std::int64_t criticalExtraIndex(PyObject* descriptor, const std::string& name);
bool damageInfoServiceable(Battle& b);
bool showDamageInfo(Battle& b, std::int64_t engine_id, std::int64_t damage_index, std::int64_t extra_index, std::int64_t attacker);
bool finalizeDeadWreck(Battle& b, Actor& v, PyObject* entity);
std::int64_t deathAttackerEngineId(Battle& b, const Value& state);
bool suppressPostmortemKiller(Battle& b, const Actor& victim, const Value& state, std::int64_t attacker);
bool syncFireEffect(Battle& b, PyObject* entity, std::optional<bool> burning = {});
bool stopRemoteVisual(Battle& b, Actor& v);
bool syncVisualComponents(Battle& b, Actor& v, PyObject* vehicle, bool team_visible, bool world_visible);
}
}
