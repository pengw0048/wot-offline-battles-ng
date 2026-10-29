"""Replace data-plane loops while retaining the pinned transport barriers."""
import re


def delta(source):
    return source + '''

import offline_client_transport_native as _native_transport
_native_transport.configure(_static._StaticDict, _static._StaticList)
SnapshotDeltaError = _native_transport.SnapshotDeltaError

def decode(wire, baseline, return_pending_actors=False):
    return _native_transport.decode(wire, baseline, return_pending_actors)

materialize = _native_transport.materialize
'''


def client(source):
    pattern = r'(^    def _canonical_runtime_rows\(self, rows, field, message=None\):\n)'
    found = list(re.finditer(pattern, source, re.M))
    if len(found) != 1:
        raise ValueError('The pinned runtime actor admission seam changed')
    index = found[0].end()
    source = source[:index] + '''        import offline_client_transport_native
        return offline_client_transport_native.canonical_rows(
            self, rows, field, message)
''' + source[index:]
    needle = "        kind = message.get('type')\n        if kind == 'battle_receipt':\n"
    if source.count(needle) != 1:
        raise ValueError('The pinned snapshot admission entry changed')
    source = source.replace(needle, """        kind = message.get('type')
        if kind == 'snapshot':
            message = _native_transport.accept(
                self, message, trusted_player_static)
            if message is not None:
                message = self._adopt_detached_turrets(message)
                self.last_snapshot = message
                self._notify(kind, message, replay_hint=replay_hint)
            return
        if kind == 'battle_receipt':
""", 1)
    needle = '    def _load_server_timing(self, message):\n'
    if source.count(needle) != 1:
        raise ValueError('The pinned server timing entry changed')
    source = source.replace(needle, needle +
        '        return _native_transport.timing(self, message)\n', 1)
    pattern = r'(^    def send_input\(.*?\):\n)'
    matches = list(re.finditer(pattern, source, re.M | re.S))
    if len(matches) != 1:
        raise ValueError('The pinned player input seam changed')
    index = matches[0].end()
    source = source[:index] + """        return _native_transport.send_input(
            self, forward, turn, aim_yaw, gun_pitch, position, yaw, fire_seq,
            speed, shell_index, next_shell_index, shell_change_pending,
            pose_time_us, ram_contacts, tank_pushes, turret_pushes,
            destructible_contacts, siege_enabled, pitch, roll,
            gun_checkpoint, up_cosine)
""" + source[index:]
    for name, arguments, call in (
            ('_send_pending_landing_observation', 'self, rebind=False',
             '_native_transport.landing_send_pending(self, rebind)'),
            ('_start_queued_landing_observation', 'self',
             '_native_transport.landing_start_queued(self)'),
            ('send_landing_observation', 'self, impact_speed, module_contacts=None',
             '_native_transport.landing_send_observation(self, impact_speed, module_contacts)')):
        needle = '    def %s(%s):\n' % (name, arguments)
        if source.count(needle) != 1:
            raise ValueError('The pinned landing entry changed: ' + name)
        source = source.replace(needle, needle + '        return ' + call + '\n', 1)
    return source + """

import sys as _native_transport_sys
import offline_client_transport_native as _native_transport
from . import stock_snapshot_static as _native_transport_static
_native_transport.configure_admission(
    _native_transport_sys.modules[__name__], _native_transport_static)
_native_transport.configure_input(_native_transport_sys.modules[__name__])
_native_transport.landing_configure(_native_transport_sys.modules[__name__])
"""
