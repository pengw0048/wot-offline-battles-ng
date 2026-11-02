"""Replace data-plane loops while retaining the pinned transport barriers."""
import ast


def delta(source):
    retired = {'decode', 'materialize', '_apply_rows', '_PendingActors', '_PENDING_ACTORS', 'SnapshotDeltaError'}
    module = ast.parse(source)
    lines = source.splitlines(True)
    spans, found = [], set()
    for index, node in enumerate(module.body):
        name = getattr(node, 'name', None)
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
        if name not in retired:
            continue
        if name in found or getattr(node, 'decorator_list', []):
            raise ValueError('Pinned snapshot decoder declaration changed: ' + name)
        found.add(name)
        following = module.body[index + 1:]
        end = min([following[0].lineno] + [item.lineno for item in getattr(following[0], 'decorator_list', [])]) - 1 if following else len(lines)
        spans.append((node.lineno - 1, end))
    if found != retired:
        raise ValueError('Pinned snapshot decoder declarations changed: ' + repr(sorted(retired - found)))
    for start, end in reversed(spans):
        del lines[start:end]
    source = ''.join(lines).replace('# Private queue metadata survives dict(message) but cannot originate in JSON.\n', '')
    return source + '''

import offline_client_transport_native as _native_transport
_native_transport.configure(_static._StaticDict, _static._StaticList)
SnapshotDeltaError = _native_transport.SnapshotDeltaError

def decode(wire, baseline, return_pending_actors=False):
    return _native_transport.decode(wire, baseline, return_pending_actors)

materialize = _native_transport.materialize
'''


def _forward_method(source, name, forwarding):
    module = ast.parse(source)
    classes = [node for node in module.body
               if isinstance(node, ast.ClassDef) and node.name == 'LANClient']
    methods = [node for node in classes[0].body
               if isinstance(node, ast.FunctionDef) and node.name == name] if len(classes) == 1 else []
    if len(methods) != 1 or methods[0].decorator_list:
        raise ValueError('Pinned native transport forwarding method changed: ' + name)
    method = methods[0]
    members = classes[0].body
    position = members.index(method)
    following = members[position + 1:] or module.body[module.body.index(classes[0]) + 1:]
    end = min([following[0].lineno] + [node.lineno for node in getattr(following[0], 'decorator_list', [])]) - 1 if following else len(source.splitlines())
    lines = source.splitlines(True)
    first = method.body[0]
    # Keep the original multiline signature, leading comments and documentation.
    prefix = lines[:first.lineno - 1]
    if ast.get_docstring(method, clean=False) is not None:
        doc_end = method.body[1].lineno - 1 if len(method.body) > 1 else end
        prefix += lines[first.lineno - 1:doc_end]
    return ''.join(prefix) + forwarding + '\n' + ''.join(lines[end:])


def client(source):
    source = _forward_method(source, '_canonical_runtime_rows', '''        import offline_client_transport_native
        return offline_client_transport_native.canonical_rows(
            self, rows, field, message)
''')
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
    source = _forward_method(source, '_load_server_timing',
        '        return _native_transport.timing(self, message)\n')
    source = _forward_method(source, 'send_input', """        return _native_transport.send_input(
            self, forward, turn, aim_yaw, gun_pitch, position, yaw, fire_seq,
            speed, shell_index, next_shell_index, shell_change_pending,
            pose_time_us, ram_contacts, tank_pushes, turret_pushes,
            destructible_contacts, siege_enabled, pitch, roll,
            gun_checkpoint, up_cosine)
""")
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
        source = _forward_method(source, name, '        return ' + call + '\n')
    return source + """

import sys as _native_transport_sys
import offline_client_transport_native as _native_transport
from . import stock_snapshot_static as _native_transport_static
_native_transport.configure_admission(
    _native_transport_sys.modules[__name__], _native_transport_static)
_native_transport.configure_input(_native_transport_sys.modules[__name__])
_native_transport.landing_configure(_native_transport_sys.modules[__name__])
"""
