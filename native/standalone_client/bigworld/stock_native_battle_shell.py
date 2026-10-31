"""Replace the staged battle_runtime module with the thin glue the native battle needs.

lan_session.py (visible client) and authority_worker.py (hidden worker) are the only callers of
BattleRuntime. Every battle decision lives in the native ``offline_native_battle`` module; this shell
only forwards their calls and exposes the state/error strings they read.
"""

SHELL = '''from __future__ import print_function
"""Thin glue between the LAN session and the native battle (offline_native_battle)."""

import offline_native_battle as _native


def _load_runtime():
    from gui.mods.offline_lan_0922 import compat
    return compat.load_battle_runtime_modules()


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
