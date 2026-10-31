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


WORKER_SESSION = '''class WorkerSession(object):
    """Forward bootstrap's worker session calls to the native session owner."""

    def __init__(self, config, lobby_ready=None, callback=None, cancel_callback=None, bigworld=None):
        import offline_native_battle
        offline_native_battle.worker_session_install(dict(config or {}), lobby_ready, bigworld)

    def start(self):
        import offline_native_battle
        return offline_native_battle.worker_session_start()

    def stop(self, show_login=False, restore_account=False, release_join=False):
        import offline_native_battle
        offline_native_battle.worker_session_stop(int(bool(restore_account)))
'''


def worker(source):
    """authority_worker.py keeps its LAN client; the session class forwards to the native worker session."""
    marker = 'class WorkerSession(object):'
    if source.count(marker) != 1 or 'class AuthorityWorkerLANClient(LANClient):' not in source:
        raise ValueError('The pinned authority worker seam changed')
    return source[:source.index(marker)] + WORKER_SESSION


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
