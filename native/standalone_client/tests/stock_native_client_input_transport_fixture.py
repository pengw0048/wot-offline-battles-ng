"""Production native input -> generated FIFO bridge -> real native TCP sender."""
from __future__ import print_function
import socket
import sys
import threading
import time
import types

sys.path.insert(0, sys.argv[1])
root = sys.argv[2]
for name in ('gui', 'gui.mods'):
    module = types.ModuleType(name)
    module.__path__ = [root + '/' + name.replace('.', '/')]
    sys.modules[name] = module
package = types.ModuleType('gui.mods.offline_lan_0922')
package.__path__ = [sys.argv[1] + '/pkg', root + '/gui/mods/offline_lan_0922']
sys.modules[package.__name__] = package
from gui.mods.offline_lan_0922 import lan_client as lan
import offline_client_input_test as native
import offline_client_wire_native as wire

listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(1)
port = listener.getsockname()[1]
acknowledged = threading.Condition()
closed = threading.Event()
received = []
errors = []


def receive():
    try:
        connection, unused = listener.accept()
        connection.settimeout(5.)
        data = b''
        while True:
            chunk = connection.recv(8192)
            if not chunk:
                break
            data += chunk
            while len(data) >= 8 and len(data) >= wire.frame_size(data[:8]):
                size = wire.frame_size(data[:8])
                with acknowledged:
                    received.append(wire.decode(data[:size]))
                    acknowledged.notifyAll()
                data = data[size:]
        connection.close()
    except Exception as error:
        with acknowledged:
            errors.append(str(error))
            acknowledged.notifyAll()
    finally:
        closed.set()


thread = threading.Thread(target=receive)
thread.daemon = True
thread.start()


def client(sock):
    value = lan.LANClient('127.0.0.1', port, 'P', 'ussr:R11_MS-1')
    value.phase = 'battle'
    value.ready = value.connected = value.running = value._outbound_accepting = True
    value.round_id = 1
    value.player_id = 1
    value.authority_epoch = 7
    value.sock = sock
    value.capabilities = [lan.HUMAN_RAM_TIMELINE_CAPABILITY, lan.PLAYER_PROJECTILE_OWNER_CAPABILITY,
                          lan.PLAYER_ENVIRONMENT_CAPABILITY]
    value.server_capabilities = list(value.capabilities)
    return value


live = client(lan._NativeSocket('127.0.0.1', port, 2.))
assert not hasattr(live, '_sender_thread') and not hasattr(live, '_outbound_event')
assert not hasattr(live, '_recv_buffer')
assert not hasattr(lan.LANClient, '_sender_worker')
assert not hasattr(lan.LANClient, '_send_payload')


def sent(kind, callback, preceding=()):
    count = len(received)
    assert callback() is not False, kind
    expected = count + 1 + len(preceding)
    deadline = time.time() + 3.
    with acknowledged:
        while len(received) < expected and not errors:
            remaining = deadline - time.time()
            assert remaining > 0., 'No TCP acknowledgement without ping: ' + kind
            acknowledged.wait(remaining)
    assert not errors, errors
    assert live._outbound_queue == [] and live._outbound_bytes == 0, kind
    assert [value['type'] for value in received[count:]] == list(preceding) + [kind], (kind, received)


checkpoint = dict(reload_time=0., reload_duration=5., clip=1, clip_size=1, dispersion=.01)
# A preceding admitted one-shot remains ahead of the new native input.
live._outbound_queue = [(1, dict(type='track_repair', round_id=1, critical_base_revision=0, repair_seq=1, tracks=[]), 1)]
live._outbound_seq = live._outbound_bytes = 1
sent('input', lambda: native.send_input(live, 1., 0., shell_index=0, next_shell_index=0,
     shell_change_pending=False, gun_checkpoint=checkpoint), ('track_repair',))
sent('landing_observation', lambda: native.landing_send_observation(live, 17., []))
messages = [dict(type='track_repair', round_id=1, critical_base_revision=0, repair_seq=1, tracks=[]),
            dict(type='equipment_intent', round_id=1, intent_seq=1, equipment_id=4,
                 activation_code=1, selected=None, requested_active=None)]
source = dict(speed=100., gravity=10., maxDistance=500., piercingPower=[100., 80.], deadeye=False,
              shell=dict(kind='ARMOR_PIERCING', caliber=100., damage=[200., 80.], explosionRadius=0.))
messages.append(dict(type='projectile_launch', round_id=1, authority_epoch=7, shooter_kind='player',
    shooter_id=1, shot_seq=1, shell_index=0, origin=[0., 0., 0.], velocity=[0., 0., 100.], gravity=10.,
    max_distance=500., max_time_ms=20000, is_he=False, splash_radius=0., penetration_factor=1.,
    source_shot=source, fire_intent_seq=1, fire_input_seq=1))
cursor = dict(projectile_id='1:p:1:1', base_checked_ms=0, checked_through_ms=10, checked_distance=1.,
              piercing_loss=0., penetration_factor=1., destructibles=[])
messages.append(dict(type='projectile_progress', round_id=1, authority_epoch=7, cursors=[cursor]))
messages.append(dict(type='projectile_ricochet', round_id=1, authority_epoch=7, projectile_id='1:p:1:1',
    base_checked_ms=10, resolved_time_ms=20, checked_distance=2., piercing_loss=0., penetration_factor=1.,
    impact=[0., 0., 2.], segment_origin=[0., 0., 2.], segment_velocity=[0., 0., -100.],
    base_penetration_multiplier=.75, direct={}, destructibles=[]))
messages.append(dict(type='projectile_resolve', round_id=1, authority_epoch=7, projectile_id='1:p:1:1',
    base_checked_ms=20, outcome='miss', resolved_time_ms=30, checked_distance=3., piercing_loss=0.,
    penetration_factor=1., impact=None, direct=None, splash=[], destructibles=[], hit_vehicle=False))
for message in messages:
    sent(message['type'], lambda: native.send(live, message))
assert len(received) == 9 and not any(value['type'] == 'ping' for value in received)
live.stop()
live.stop()
assert live.sock is None and not live.running and live.phase == 'disconnected'
assert closed.wait(3.) and not errors, errors
listener.close()


class FailingSocket(lan._NativeSocket):
    def __init__(self):
        self.closed = False

    def sendall(self, payload):
        raise socket.error('synchronous native send refusal')

    def close(self):
        self.closed = True


for injected in (False, True):
    socket_owner = FailingSocket()
    failed = client(socket_owner)
    if injected:
        def broken_handoff(generation):
            raise RuntimeError('handoff failed before dequeue')
        failed._flush_native_outbound = broken_handoff
    assert native.send(failed, messages[2]) is False
    assert failed._outbound_queue == [] and failed._outbound_bytes == 0
    assert failed.sock is None and not failed.running and not failed.connected and socket_owner.closed
    assert native.send(failed, messages[-1]) is False

# A retired generation neither drains nor clears its replacement's admitted FIFO.
replacement = FailingSocket()
current = client(replacement)
current._transport_generation = 2
current._outbound_queue = [(1, messages[0], 1)]
current._outbound_bytes = 1
old = FailingSocket()
current._release_native_transport(1, old)
current._flush_native_outbound(1)
assert old.closed and current.sock is replacement and current.running and current.connected
assert len(current._outbound_queue) == 1 and current._outbound_bytes == 1
current.stop()
current.stop()
assert replacement.closed and not current.running and current._outbound_queue == []

print('Eight native producers acknowledged over TCP without ping; failed sends cannot retain a queued launch')
