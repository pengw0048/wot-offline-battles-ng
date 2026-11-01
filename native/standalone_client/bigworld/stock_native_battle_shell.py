"""Replace the staged battle_runtime module with the thin glue the native battle needs.

lan_session.py (visible client) and authority_worker.py (hidden worker) are the only callers of
BattleRuntime. Every battle decision lives in the native ``offline_native_battle`` module; this shell
only forwards their calls and exposes the state/error strings they read.
"""

SHELL = '''from __future__ import print_function
"""Thin glue between the LAN session and the native battle (offline_native_battle)."""

import offline_native_battle as _native


def _load_runtime():
    import AccountCommands
    import AreaDestructibles
    import ArenaType
    import AvatarInputHandler
    import BattleFeedbackCommon
    import BigWorld
    import DataLinks
    import Math
    import constants
    import game
    import material_kinds
    import nations
    from Avatar import ClientVisibilityFlags
    from OfflineMapCreator import g_offlineMapCreator
    from AvatarInputHandler import aih_constants, cameras, gun_marker_ctrl
    from helpers import EffectMaterialCalculation
    from gun_rotation_shared import encodeGunAngles
    from gui.app_loader import g_appLoader
    from gui.app_loader.settings import GUI_GLOBAL_SPACE_ID
    from gui.battle_control.battle_constants import FEEDBACK_EVENT_ID, VEHICLE_VIEW_STATE
    from gui.mods.offline_lan_0922 import native_mapping_mask
    from gui.mods.offline_lan_0922.compat import g_compatibility
    from gui.shared.utils import HangarSpace
    from items import vehicles
    from vehicle_systems.tankStructure import TankPartNames

    class Runtime(object):
        pass

    runtime = Runtime()
    runtime.account_commands = AccountCommands
    runtime.aih_constants = aih_constants
    runtime.app_loader = g_appLoader
    runtime.area_destructibles = AreaDestructibles
    runtime.arena_cache = ArenaType.g_cache
    runtime.arena_visibility_mask = ArenaType.getVisibilityMask
    runtime.avatar_input_handler = AvatarInputHandler
    runtime.battle_feedback_common = BattleFeedbackCommon
    runtime.bigworld = BigWorld
    runtime.call_with_standard_gameplay_mask = native_mapping_mask.call_with_standard_gameplay_mask
    runtime.cameras = cameras
    runtime.client_visibility_flags = ClientVisibilityFlags
    runtime.compatibility = g_compatibility
    runtime.constants = constants
    runtime.data_links = DataLinks
    runtime.effect_material_calculation = EffectMaterialCalculation
    runtime.encode_gun_angles = encodeGunAngles
    runtime.feedback_event_id = FEEDBACK_EVENT_ID
    runtime.game = game
    runtime.gui_global_space_id = GUI_GLOBAL_SPACE_ID
    runtime.gun_marker_ctrl = gun_marker_ctrl
    runtime.hangar_space = HangarSpace
    runtime.material_kinds = material_kinds
    runtime.math = Math
    runtime.nations = nations
    runtime.offline_map_creator = g_offlineMapCreator
    runtime.tank_part_names = TankPartNames
    runtime.vehicle_view_state = VEHICLE_VIEW_STATE
    runtime.vehicles = vehicles
    return runtime


class BattleRuntime(object):
    """Forward the LAN session's battle calls to the native round owner."""

    def __init__(self, runtime=None):
        self._runtime = runtime

    @property
    def state(self):
        return _native.state()

    @property
    def error(self):
        return _native.error()

    def start(self, config, message=None, lan_client=None, on_local_leave=None):
        if lan_client is None:
            raise ValueError('LAN client is required')
        self._runtime = self._runtime or _load_runtime()
        return _native.start(self, self._runtime, dict(config or {}), dict(message or {}),
                             lan_client, on_local_leave)

    def stop(self, show_login=False, restore_account=True):
        _native.stop(bool(restore_account))

    def lobby_restore_pending(self):
        return _native.lobby_restore_pending()

    def has_pending_presentation(self):
        return _native.has_pending_presentation()

    def apply_replay_local(self, message):
        _native.apply_replay_local(message)

    def on_snapshot(self, message):
        _native.on_snapshot(message)

    def on_events(self, message):
        return _native.on_events(message)

    def on_battle_live(self, message):
        return _native.on_battle_live(message)

    def on_bot_observation(self, message):
        return _native.on_bot_observation(message)

    def on_player_destructible_contact(self, message):
        return _native.on_player_destructible_contact(message)

    def on_player_destructible_contact_result(self, message):
        return _native.on_player_destructible_contact_result(message)

    def on_fire_intent_result(self, message):
        return _native.on_fire_intent_result(message)

    def on_roster(self, message):
        return _native.on_roster(message)

    def on_team_chat(self, message):
        return _native.on_team_chat(message)

    def on_team_chat_ack(self, message):
        return _native.on_team_chat_ack(message)

    def on_team_command(self, message):
        return _native.on_team_command(message)

    def on_team_command_ack(self, message):
        return _native.on_team_command_ack(message)

    def on_team_command_terminal(self, message):
        return _native.on_team_command_terminal(message)


g_battle_runtime = BattleRuntime()
'''


def battle(source):
    """The staged battle_runtime.py: the pinned module is retired in favour of the native battle."""
    if 'class BattleRuntime(' not in source or 'g_battle_runtime = BattleRuntime()' not in source:
        raise ValueError('The pinned battle runtime seam changed')
    return SHELL


WORKER_SESSION = '''"""Bootstrap's hidden worker session: the native session and LAN link own it."""


class _LinkState(object):
    """What bootstrap's connection wait reads from the worker's link."""

    def __init__(self, connected, ready):
        self.connected = connected
        self.ready = ready


class WorkerSession(object):
    """Forward bootstrap's worker session calls to the native session owner."""

    def __init__(self, config, lobby_ready=None, callback=None, cancel_callback=None, bigworld=None):
        import offline_native_battle
        offline_native_battle.worker_session_install(dict(config or {}), lobby_ready, bigworld)

    def start(self):
        import offline_native_battle
        return offline_native_battle.worker_session_start()

    @property
    def client(self):
        import offline_native_battle
        return _LinkState(*offline_native_battle.worker_session_link())

    @property
    def state(self):
        import offline_native_battle
        return offline_native_battle.worker_session_state()

    def stop(self, show_login=False, restore_account=False, release_join=False):
        import offline_native_battle
        offline_native_battle.worker_session_stop(int(bool(restore_account)))
'''


def worker(source):
    """Return the replacement authority_worker module; the pinned LAN client subclass is not shipped."""
    if source.count('class WorkerSession(object):') != 1 or 'class AuthorityWorkerLANClient(LANClient):' not in source:
        raise ValueError('The pinned authority worker seam changed')
    return WORKER_SESSION


ENGINE_AUDIO_SHELL = '''"""Bootstrap's engine audio hook: the native relay owns audition assembly."""

import offline_native_battle as _native


def install():
    return _native.engine_audio_install()


def uninstall():
    _native.engine_audio_uninstall()
'''


def engine_audio(source):
    """Return the replacement engine_audio module; the pinned source is not used."""
    if 'def install(' not in source or 'def uninstall(' not in source:
        raise ValueError('The pinned engine audio seam changed')
    return ENGINE_AUDIO_SHELL


# The original callables/code objects remain the ABI input to compat::install.
# Only the Python implementations and their private, superseded state retire.
RETIRED_COMPAT_METHODS = frozenset('''
activate_map configure_battle synchronise_vehicle_marker_identity
assert_vehicle_marker_identity assert_vehicle_marker_damage_type
set_battle_network_client set_control_mode_listener _team_command_target
set_target_lock_candidate clear_target_focus _target_lock_holds
validate_target_lock release_target_lock set_vehicle_pose_overlay
set_postmortem_vehicle clear_postmortem_vehicle clear_postmortem_killer
bind_vehicle_pose_sources restore_vehicle_pose_sources clear_vehicle_pose_overlay
native_vehicle_attribute attach_avatar_server _prepare_avatar_properties prepare_avatar
'''.split())

RETIRED_COMPAT_STATE = frozenset('''
_battle_active _native_battle _battle_gui_type _battle_bonus_type
_battle_player_name _battle_player_team _battle_arena_type_id
_battle_network_client _battle_server_time_origin _battle_clock_origin
_battle_player_vehicle_id _postmortem_vehicle_id _control_mode_listener
_target_lock_candidate _target_lock_input_pending _target_lock_input_avatar
_target_focus_clear_attempted _vehicle_property_overlays _vehicle_marker_plugins
_vehicle_starting_visual _vehicle_starting_wg_physics _vehicle_syncing_gun_angles
_avatar_syncing_aux_physics _avatar_entering_vehicle _compound_refreshing_models
_strategic_camera_failure_reported
'''.split())

_NATIVE_DESCRIPTORS = frozenset('''
avatar_getattribute avatar_get_speeds vehicle_getattribute vehicle_setattr
vehicle_get_speed compound_getattribute
'''.split())
_RETIRED_COMPAT_HOOKS = frozenset('''
set_server_marker use_client_gun_marker use_server_gun_marker shell_tooltip
avatar_init control_mode_changed rollback_vehicle_marker_start
vehicle_marker_start vehicle_marker_stop consistent_link_own_vehicle
steady_relink_sources avatar_become_player avatar_enter_world avatar_leave_world
prime_initial_remote_enemy hide_initial_remote_enemy avatar_vehicle_enter
vehicle_enter_world vehicle_start_visual avatar_prereqs_loaded avatar_aux_physics
visible_native_remote_collisions vehicle_collide_segment vehicle_collide_segment_ext
undrawn_lan_remote projectile_collidable_entities projectile_segment_may_hit
avatar_auto_aim ammo_change_setting handle_target_lock_input strategic_camera_update
arcade_handle_key_event sniper_handle_key_event vehicle_leave_world
vehicle_start_wg_physics vehicle_set_gun_angles compound_deactivate
compound_effects_lod compound_models_refresh server_time debug_update
show_player_message
'''.split())
_RETIRED_COMPAT_WRAPPERS = frozenset(
    '_' + name + '_wrapper' for name in _RETIRED_COMPAT_HOOKS) | frozenset('''
_gun_server_marker_property _player_message_show _player_message_show_owned
_postmortem_populate _effect_update_wrapper
'''.split())
_RETIRED_COMPAT_HELPERS = frozenset('''
_track_effect_update_with_receipt _show_lan_death_message _DeferredAvatarServer
_CopiedVehicleSpeedInfo _OfflineVehicleFilterSyncProxy
_OfflineInitialVehicleVisualProvider _OfflineCameraColliderHandler _OfflineEventSink
'''.split())


def compat(source):
    """Retire the pinned Python battle owner after all reviewed seam adapters.

    Preserve source text and account/lobby methods. The build-time AST only
    selects complete statements; it never emits Python 3 syntax into #1513.
    Original stock captures, frame-code identities and the process-wide native
    attribute descriptors must survive independently of the per-round hooks.
    """
    import ast

    # The pinned capture uses the later closure's name for a stock callable.
    # Keep that early capture distinct from the retired closure registrations.
    start = source.index('        projectile_segment_may_hit = getattr(')
    end = source.index('        projectile_collidable_entities_original = getattr(', start)
    capture = source[start:end]
    import re
    capture = re.sub(r'\bprojectile_segment_may_hit\b',
                     'stock_projectile_segment_may_hit', capture)
    source = source[:start] + capture + source[end:]
    tree = ast.parse(source)
    classes = [node for node in tree.body
               if isinstance(node, ast.ClassDef) and node.name == 'OfflineCompatibility']
    if len(classes) != 1:
        raise ValueError('The pinned compatibility class changed')
    owner = classes[0]
    methods = {node.name: node for node in owner.body if isinstance(node, ast.FunctionDef)}
    if not RETIRED_COMPAT_METHODS.issubset(methods):
        raise ValueError('The pinned compatibility battle methods changed')
    install = methods['install']
    hooks = {node.name for node in ast.walk(install) if isinstance(node, ast.FunctionDef)}
    if not (_RETIRED_COMPAT_HOOKS | _NATIVE_DESCRIPTORS).issubset(hooks):
        raise ValueError('The pinned compatibility hook implementations changed')

    lines = source.splitlines(True)
    edits = []
    retired_attributes = RETIRED_COMPAT_STATE | _RETIRED_COMPAT_WRAPPERS
    retired_names = _RETIRED_COMPAT_HOOKS | _RETIRED_COMPAT_HELPERS | frozenset((
        'effect_wrapper', 'vehicle_overlay_names', 'vehicle_special_names'))

    def remove(node):
        edits.append((node.lineno - 1, node.end_lineno, ''))

    def references(node, attributes=retired_attributes, names=retired_names):
        for child in ast.walk(node):
            if isinstance(child, ast.Attribute) and child.attr in attributes:
                return True
            if isinstance(child, ast.Name) and child.id in names:
                return True
            if (isinstance(child, ast.Call) and isinstance(child.func, ast.Name)
                    and child.func.id in ('getattr', 'setattr', 'hasattr')
                    and len(child.args) > 1 and isinstance(child.args[1], ast.Constant)
                    and child.args[1].value in attributes):
                return True
        return False

    def prune(node):
        if isinstance(node, ast.FunctionDef):
            if node.name in _RETIRED_COMPAT_HOOKS | _NATIVE_DESCRIPTORS:
                remove(node)
                return False
            # Retained account/retirement closures are not subject to pruning.
            return True
        if isinstance(node, ast.If):
            if references(node.test):
                remove(node)
                return False
            body = [child for child in node.body if prune(child)]
            other = [child for child in node.orelse if prune(child)]
            if not body and not other:
                remove(node)
                return False
            if not body:
                raise ValueError('Retired hook left an unsupported empty branch')
            return True
        if isinstance(node, (ast.Try, ast.For, ast.With)):
            # These retained lifecycle/installation containers must keep their
            # exception, rollback and team-reader ownership boundaries.
            for field in ('body', 'orelse', 'finalbody'):
                body = getattr(node, field, [])
                kept = [child for child in body if prune(child)]
                if body and not kept and field == 'body':
                    raise ValueError('Retired hook emptied a lifecycle container')
            for handler in getattr(node, 'handlers', []):
                for child in handler.body:
                    prune(child)
            return True
        if references(node):
            remove(node)
            return False
        return True

    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in _RETIRED_COMPAT_HELPERS:
            remove(node)
        if isinstance(node, ast.ImportFrom) and node.module == 'gui.mods.offline_lan_0922.entities.remote_vehicle':
            remove(node)
    for name in RETIRED_COMPAT_METHODS:
        remove(methods[name])
    for name in ('__init__', 'install', '_rollback_install'):
        for node in methods[name].body:
            prune(node)
    deactivate = methods['deactivate_map']
    edits.append((deactivate.lineno - 1, deactivate.end_lineno,
                  '    def deactivate_map(self):\n'
                  '        if self._runtime is not None:\n'
                  '            self._runtime.offline_map_creator.SetActive(False)\n'))

    # The reader remains a stock frame-scoped target override. Native install
    # binds its picker to teamCommandTarget; the process-only fallback has no
    # battle target and cannot retain stale round state.
    before = 'team_command_reader(original, self._team_command_target)'
    if source.count(before) != 1:
        raise ValueError('The pinned team command reader changed')
    # Discard edits covered by a whole enclosing statement removal.
    selected = []
    for begin, end, replacement in sorted(edits, key=lambda item: (item[0], -item[1])):
        if selected and begin < selected[-1][1]:
            continue
        selected.append((begin, end, replacement))
    for begin, end, replacement in reversed(selected):
        lines[begin:end] = [replacement]
    result = ''.join(lines).replace(before, 'team_command_reader(original, lambda: None)', 1)
    # Query aliases are installed/restored by the same native attribute owner
    # as ProjectileMover; the retired Python bridge must not retain a registry.
    obsolete_lines = frozenset((
        'from . import native_client_query',
        'native_client_query.restore_aliases(self)',
        'from gui.mods.offline_lan_0922 import postmortem_causes'))
    result = ''.join(line for line in result.splitlines(True)
                     if line.strip() not in obsolete_lines)
    # Fail closed if an unreviewed consumer of the retired owner survives.
    final = ast.parse(result)
    if references(final, retired_attributes | RETIRED_COMPAT_METHODS, _RETIRED_COMPAT_HELPERS):
        raise ValueError('A retained compatibility consumer reads retired battle state')
    return re.sub(r'\n(?:[ \t]*\n){3,}', '\n\n\n', result)
