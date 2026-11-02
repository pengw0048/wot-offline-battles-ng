#pragma once
// Stock-hook internals shared by the compat translation units.
#include "stock_native_battle_compat.hpp"
#include <array>

namespace native_battle {
namespace compat {
// Process lifetime: stock owners, the originals the lobby-wide install captured, and stock code objects.
struct Stock {
    bool installed = false;
    Object compatibility;   // the lobby-wide Python OfflineCompatibility (account/login/garage hooks stay with it)
    Object runtime, bigworld, math, constants;
    Object avatar_type, vehicle_type, compound_type, marker_type, ammo_controller_type, input_handler_type;
    Object arcade_type, sniper_type, strategic_camera_type, consistent_type, steady_type, gun_rotator_type;
    Object gun_marker_module, projectile_mover, debug_panel_type, consumables_panel_type, player_messages_type;
    Object postmortem_panel_type, effect_manager_type, offline_map_creator, chat_manager, avatar_getter, avatar_module;
    // Stock originals.
    Object avatar_init, avatar_become_player, avatar_become_non_player, avatar_enter_world, avatar_leave_world;
    Object avatar_vehicle_enter, avatar_prereqs_loaded, avatar_aux_physics, avatar_auto_aim, avatar_getattribute;
    Object ammo_change_setting, arcade_key_event, sniper_key_event, strategic_camera_update, control_mode_changed;
    Object server_marker_property, use_client_marker, use_server_marker, marker_start, marker_stop;
    Object link_own_vehicle, relink_sources;
    Object vehicle_getattribute, vehicle_enter_world, vehicle_start_visual, vehicle_leave_world, vehicle_start_wg_physics;
    Object vehicle_set_gun_angles, vehicle_collide_segment, vehicle_collide_segment_ext;
    Object segment_may_hit, collidable_entities, segment_collision_result;
    Object compound_getattribute, compound_deactivate, compound_models_refresh, compound_effects_lod;
    Object server_time, debug_update, shell_tooltip, player_message_show, postmortem_populate, effect_update;
    Object message_color_format, nice_number_format;
    std::array<Object, std::size_t(Code::Count)> codes;
    // Every class/module attribute this layer replaced: (owner, name, value before the native install).
    struct Patch { Object owner; std::string name; Object previous; };
    std::vector<Patch> patches;
};
Stock& stock();
void replaceCollidableEntities(PyObject* replacement);
Scopes& mutableScopes();
// The configured battle of this round, or null when hooks must behave as stock.
Battle* battle();

// A native callable; when installed on a class it binds like a Python function.
using Body = PyObject* (*)(PyObject* args, PyObject* kwargs);
p::Ref hook(Body body, const char* name);
// A callable that ignores its arguments and returns one object.
p::Ref constant(PyObject* value);
// A no-op stand-in for late #1513 teardown owners (event sinks, camera collider, arena).
p::Ref surrogate(int kind);
enum { SurrogateSink = 0, SurrogateCollider = 1, SurrogateArena = 2, SurrogateOwner = 3 };
// The deferred Avatar server proxy.
p::Ref deferredServer();
void attachDeferredServer(PyObject* proxy, PyObject* server);

// Shared helpers.
PyObject* callerCode();
p::Ref call(const Object& fn, std::initializer_list<PyObject*> args, PyObject* kwargs = nullptr);
p::Ref argument(PyObject* args, Py_ssize_t index);
bool flagAttribute(PyObject* o, const char* name, bool fallback);
p::Ref player();
// compat.py undrawn_lan_remote: a LAN remote the client must not draw.
bool undrawnLanRemote(PyObject* entity);
// compat.py prime/hide_initial_remote_enemy.
bool primeInitialRemoteEnemy(PyObject* avatar, PyObject* vehicle);
bool hideInitialRemoteEnemy(PyObject* avatar, PyObject* vehicle);
void prepareAvatarProperties(PyObject* avatar);

// Presentation hooks.
PyObject* shellTooltip(PyObject* args, PyObject* kwargs);
PyObject* showPlayerMessage(PyObject* args, PyObject* kwargs);
PyObject* postmortemPopulate(PyObject* args, PyObject* kwargs);
PyObject* effectUpdate(PyObject* args, PyObject* kwargs);
PyObject* debugUpdate(PyObject* args, PyObject* kwargs);

extern const char* const init_complete;
extern const char* const player_ready;
extern const char* const retire_pending;
}

// Calls owned by other subsystems.
namespace remote {
// The player's live pose overlay of one stock Vehicle: matrix and its steady/stabilised providers.
struct PoseOverlay { Object matrix, steady_rotation, stabilised; };
std::optional<PoseOverlay> poseOverlay(const Battle& b, PyObject* vehicle);
// The drawn pose of a native remote for stock collision; None when the vehicle has no live overlay.
p::Ref visibleCollisions(Battle& b, PyObject* vehicle, PyObject* start, PyObject* end);
void closeStockPresentationExtras(Battle& b, PyObject* appearance, bool visible);
void clearGroundDecalVisibilityState(PyObject* appearance);
bool setGroundDecalVisibility(PyObject* appearance, bool visible);
bool stopGroundEffects(PyObject* appearance);
bool stopBoundVisualEffects(PyObject* appearance);
}
namespace visibility {
// The native query owner's stock-collision answers; None when no owner exists.
p::Ref collisions(Battle& b, PyObject* entity, PyObject* start, PyObject* end);
p::Ref collidableEntities(Battle& b, PyObject* except_ids, PyObject* start, PyObject* end);
p::Ref segmentMayHit(Battle& b, PyObject* entity, PyObject* start, PyObject* end);
}
}
