"""The C++ wire codec (Python 2.7 objects) and the schema's Python 3 codec agree byte for byte."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'wire'))
from codec import same  # noqa: E402
from schema import SCHEMA  # noqa: E402
import gen_cpp  # noqa: E402

FIXTURE = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
import wire_test
frames = json.load(open(sys.argv[2]))
def text(value):
    # Native values write UTF-8 str; the Python decoder returns unicode.
    if isinstance(value, str): return value.decode('utf-8')
    if isinstance(value, list): return [text(v) for v in value]
    if isinstance(value, dict): return dict((text(k), text(v)) for k, v in value.items())
    return value
out = []
for item in frames:
    data = item['frame'].decode('hex')
    try:
        message = wire_test.decode(data)
    except ValueError as error:
        raise ValueError('%s: %s' % (item['type'], error))
    assert wire_test.frame_size(data[:8]) == len(data)
    again = wire_test.encode(message)
    assert wire_test.decode(again) == message, message.get('type')
    # Map keys may encode in another order; the decoded message must be identical.
    assert wire_test.decode(wire_test.encode_native(message)) == message, message.get('type')
    assert text(wire_test.decode_native(data)) == message, message.get('type')
    out.append(message)
json.dump(out, open(sys.argv[3], 'w'))
for bad in (frames[0]['frame'].decode('hex')[:-1], '\x00' * 8):
    try:
        wire_test.decode(bad)
    except ValueError:
        pass
    else:
        raise AssertionError('malformed frame accepted')
try:
    wire_test.encode({u'type': u'pong', u'x': object()})
except ValueError:
    pass
else:
    raise AssertionError('unencodable value accepted')
print('python27 wire codec ok %d' % len(out))
'''


def samples():
    path = os.environ.get('WIRE_FIXTURES')
    messages = []
    if path and os.path.exists(path):
        with open(path) as source:
            messages = [json.loads(line) for line in source]
    messages.append({'type': 'events', 'protocol': 5, 'round_id': 1, 'server_tick': 9, 'authority_epoch': 2,
                     'server_time_ms': 300, 'worker_status': 'connected', 'worker_failure_reason': '',
                     'bot_authority_id': -1, 'events': [
                         {'kind': 'bot_bot_hit', 'event_id': '1:9:0', 'attacker_bot': 3, 'target_bot': 17,
                          'damage': 120, 'health': 300, 'dead': False, 'attack_reason': 0,
                          'death_reason': 0, 'source': 'shot', 'damage_sticker': 2 ** 64 - 1,
                          'x': 1.25, 'y': -0.5, 'z': 1e-6, 'critical': {}},
                         {'kind': 'stun', 'event_id': '1:9:1', 'active': False, 'target_kind': 'bot',
                          'target_id': 4, 'stun_end_server_time_ms': 0},
                         {'kind': 'projectile_impact', 'event_id': '1:9:2', 'projectile_id': '1:b:3:7',
                          'outcome': 'expired', 'resolved_time_ms': 20000, 'checked_distance': 0.0,
                          'piercing_loss': 0.0, 'penetration_factor': 1.0, 'hit_vehicle': False,
                          'shooter_kind': 'bot', 'shooter_id': 3, 'shot_seq': 7,
                          'wreck_hit': {'target_kind': 'bot', 'target_id': 2 ** 63 + 5}}]})
    messages.append({'type': 'track_repair', 'round_id': 1, 'critical_base_revision': 0, 'repair_seq': 1,
                     'tracks': [{'name': 'leftTrackHealth', 'hp': 12.5, 'max_hp': 100.0, 'state': 'critical'}]})
    # The post-result detached-turret tail is not a new Bot motion sample.
    messages.append({'type': 'bot_state', 'round_id': 1, 'authority_epoch': 2,
                     'rows': [], 'detached_turrets': []})
    messages.append({'type': 'bot_state', 'round_id': 1, 'authority_epoch': 2,
                     'rows': [], 'sample_time_us': 1000, 'source_batch_horizon_us': 2000})
    messages.extend([
        {'type': 'select_vehicle', 'vehicle': 'germany:G04_PzVI_Tiger_I', 'max_health': 1500,
         'vehicle_compact_descr': 'AQID', 'effective_params': {'camouflage': {'camouflage_id': None}},
         'marks_on_gun': 2, 'outfits': {}},
        {'type': 'select_team', 'team': 0},
        {'type': 'set_team_size', 'team': 2, 'size': 15},
        {'type': 'set_bot_tier_mode', 'mode': 'random'},
        {'type': 'set_bot_skill_mode', 'mode': 'mixed'},
    ])
    import replica_split
    tick = next((m for m in messages if m.get('type') == 'snapshot' and 'client_build' not in m
                 and m.get('bots') and m.get('players')), None)
    if tick is not None:
        messages.append(replica_split.encode(tick, None, tick['players'][0]['id'], True)[0])
    return messages


def python27_audit_runtime():
    pyenv = shutil.which('pyenv')
    if pyenv:
        result = subprocess.run([pyenv, 'which', 'python2.7'], capture_output=True, text=True,
                                env=dict(os.environ, PYENV_VERSION='2.7.18'))
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    raise unittest.SkipTest('CPython 2.7.18 audit runtime is required')


class StockWireCodec(unittest.TestCase):
    def test_python27_native_matches_schema_codec(self):
        py27 = python27_audit_runtime()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        messages = samples()
        with tempfile.TemporaryDirectory(prefix='wire-codec-') as folder:
            folder = Path(folder)
            tables = folder / 'stock_wire_schema.inc'
            gen_cpp.emit(SCHEMA, str(tables))
            committed = (ROOT / 'bigworld/stock_wire_schema.inc').read_text()
            self.assertEqual(tables.read_text(), committed, 'regenerate stock_wire_schema.inc')
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-missing-field-initializers', '-bundle', '-undefined', 'dynamic_lookup',
                            '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                            str(ROOT / 'bigworld/stock_wire_codec.cpp'), str(ROOT / 'tests/stock_native_client_state_host.cpp'), str(ROOT / 'tests/stock_wire_host_module.cpp'),
                            '-o', str(folder / 'wire_test.so')], check=True)
            frames = [{'frame': SCHEMA.encode(m).hex(), 'type': m['type']} for m in messages]
            (folder / 'frames.json').write_text(json.dumps(frames))
            script = folder / 'fixture.py'
            script.write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(script), str(folder), str(folder / 'frames.json'),
                                  str(folder / 'decoded.json')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            decoded = json.loads((folder / 'decoded.json').read_text())
            for original, native in zip(messages, decoded):
                self.assertTrue(same(native, original), original.get('type'))


CLIENT_FIXTURE = r'''
import json, socket, sys, threading, time, types
sys.path.insert(0, sys.argv[1])
root = sys.argv[2]
for name in ('gui', 'gui.mods'):
    module = types.ModuleType(name); module.__path__ = [root + '/' + name.replace('.', '/')]; sys.modules[name] = module
package = types.ModuleType('gui.mods.offline_lan_0922')
package.__path__ = [sys.argv[3]] + [root + '/gui/mods/offline_lan_0922']
sys.modules[package.__name__] = package
from gui.mods.offline_lan_0922 import lan_client
import offline_client_wire_native as wire
frames = [item['frame'].decode('hex') for item in json.load(open(sys.argv[4]))]
listener = socket.socket(); listener.bind(('127.0.0.1', 0)); listener.listen(1)
port = listener.getsockname()[1]
seen = {}
def serve():
    conn, unused = listener.accept()
    data = b''
    while len(data) < 8 or len(data) < wire.frame_size(data[:8]):
        data += conn.recv(4096)
    seen['hello'] = wire.decode(data[:wire.frame_size(data[:8])])
    stream = b''.join(frames)
    for start in range(0, len(stream), 7):
        conn.sendall(stream[start:start + 7])
    rest = b''
    conn.settimeout(3.0)
    try:
        while True:
            chunk = conn.recv(4096)
            if not chunk:
                break
            rest += chunk
    except socket.timeout:
        pass
    seen['tail'] = wire.decode(rest) if rest else None
    conn.close()
thread = threading.Thread(target=serve); thread.start()
sock = lan_client._NativeSocket('127.0.0.1', port, 3.0)
writer = wire.replay_writer(sys.argv[1] + '/frames.wotlanreplay')
wire.net_record(sock._handle, writer, 2.0)
sock.sendall(wire.encode({'type': 'hello', 'name': u'T'}))
received, state = [], 'connecting'
deadline = time.time() + 10
while len(received) < len(frames) and time.time() < deadline:
    batch, state, error = sock.receive()
    received.extend(batch)
    time.sleep(0.01)
assert [item[0] for item in received] == frames, (len(received), len(frames))
assert all(item[1] >= 0.0 for item in received)
replicas = 0
for frame, age, native in received:
    if wire.decode(frame)['type'] != 'replica_snapshot':
        assert native is None, native
        continue
    replicas += 1
    view, complete = wire.replica_message(native), wire.complete_snapshot(native)
    assert view['type'] == 'snapshot' and view['bots'] == [] and len(view['players']) == 1, view
    assert view['_native_snapshot'] is native and complete['bots'], complete
assert replicas == sum(wire.decode(frame)['type'] == 'replica_snapshot' for frame in frames), replicas
wire.net_record(sock._handle, None)
assert wire.replay_count(writer) == len(frames) and wire.replay_finish(writer)[0]
reader = wire.replay_reader(sys.argv[1] + '/frames.wotlanreplay')
played = [wire.replay_read(reader) for unused in frames]
assert wire.replay_read(reader) is None
assert all(kind == 1 and t >= 2.0 for kind, t, value in played), played
assert [value['type'] for kind, t, value in played] == [
    'snapshot' if wire.decode(frame)['type'] == 'replica_snapshot' else wire.decode(frame)['type'] for frame in frames]
sock.sendall(wire.encode({'type': 'leave'}))
sock.close()
thread.join(10)
assert seen['hello'] == {u'type': u'hello', u'name': u'T'} and seen['tail'] == {u'type': u'leave'}, seen
listener.close()
dead = lan_client._NativeSocket('127.0.0.1', port, 0.5)
for unused in range(300):
    batch, state, error = dead.receive()
    if state == 'failed':
        break
    time.sleep(0.01)
assert state == 'failed' and error, (state, error)
try:
    dead.sendall(b'x' * 8)
except socket.error:
    pass
else:
    raise AssertionError('send after failure accepted')
print('native transport ok %d' % len(received))
'''


class StagedClientTransport(unittest.TestCase):
    def test_native_transport_frames_hello_leave_and_failure(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = python27_audit_runtime()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        client = Path(fork) / 'src/res/scripts/client'
        import importlib.util
        bindings = {}
        for name in ('stock_snapshot_bindings', 'stock_native_client_transport_bindings',
                     'stock_native_client_wire_bindings'):
            spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
            bindings[name] = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(bindings[name])
        source = (client / 'gui/mods/offline_lan_0922/lan_client.py').read_text()
        staged = bindings['stock_native_client_wire_bindings'].client(
            bindings['stock_native_client_transport_bindings'].client(
                bindings['stock_snapshot_bindings'].client(source)))
        with tempfile.TemporaryDirectory(prefix='wire-client-') as folder:
            folder = Path(folder)
            (folder / 'pkg').mkdir()
            (folder / 'pkg/lan_client.py').write_text(staged)
            (folder / 'offline_client_transport_native.py').write_text(
                'def _ignore(*args, **kwargs):\n    return None\n'
                'configure_admission = configure_input = landing_configure = _ignore\n')
            (folder / 'pkg/stock_snapshot_static.py').write_text(
                (ROOT / 'bigworld/stock_snapshot_static.py').read_text())
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-missing-field-initializers', '-bundle', '-undefined', 'dynamic_lookup',
                            '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                            '-DWOT_WIRE_HOST_BUILD', str(ROOT / 'bigworld/stock_wire_codec.cpp'), str(ROOT / 'tests/stock_native_client_state_host.cpp'),
                            str(ROOT / 'bigworld/stock_wire_replay.cpp'), str(ROOT / 'bigworld/stock_wire_net.cpp'), str(ROOT / 'bigworld/stock_wire_replica_store.cpp'),
                            str(ROOT / 'tests/stock_wire_host_client_module.cpp'), '-lz',
                            '-o', str(folder / 'offline_client_wire_native.so')], check=True)
            frames = [{'frame': SCHEMA.encode(m).hex()} for m in samples()]
            (folder / 'frames.json').write_text(json.dumps(frames))
            (folder / 'fixture.py').write_text(CLIENT_FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder), str(client),
                                  str(folder / 'pkg'), str(folder / 'frames.json')],
                                 capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('native transport ok', run.stdout)


REPLAY_FIXTURE = r'''
import os, sys, types
sys.path.insert(0, sys.argv[1])
root = sys.argv[2]
for name in ('gui', 'gui.mods'):
    module = types.ModuleType(name); module.__path__ = [root + '/' + name.replace('.', '/')]; sys.modules[name] = module
package = types.ModuleType('gui.mods.offline_lan_0922')
package.__path__ = [sys.argv[3], root + '/gui/mods/offline_lan_0922']
sys.modules[package.__name__] = package
sys.modules['offline_client_lifecycle_native'] = types.ModuleType('offline_client_lifecycle_native')
from gui.mods.offline_lan_0922 import native_replay, offline_replay
assert not hasattr(offline_replay, 'Recorder') and not hasattr(offline_replay, 'Reader')
clock = [100.0]
header = {'welcome': {'type': u'welcome'}, 'start': {'type': u'battle_start', 'round_id': 3},
          'snapshot': {'type': u'snapshot', 'round_id': 3, 'players': [], 'bots': []}, 'config': {u'map': u'33_fjord'},
          'player_id': 4, 'live': None}
recorder = native_replay.NativeRecorder(sys.argv[4], 1, header, lambda: clock[0])
import json
events = json.load(open(sys.argv[5]))
rows = []
# Wire frames are recorded by the native receive thread; a Python wire append is a no-op.
assert recorder.append('wire', events) is True
for i in range(200):
    clock[0] += 0.033
    wire = {'type': u'snapshot', 'round_id': 3, 'server_tick': i, 'x': 1.5 * i, 'bots': [{'id': 1, 'hp': 2 ** 63 + i}]}
    local = {'position': [0.5, 1.0, i * 0.25], 'health': 500 - i, 'visibility': {'bot:1': [True, False]}}
    recorder.append('wire', wire); recorder.append('local', local)
    rows.append(('local', local))
path = recorder.close('round_finished')
assert path and os.path.exists(path) and not os.path.exists(recorder.temp)
reader = native_replay.NativeReader(path)
assert reader.header['config'] == {u'map': u'33_fjord'} and reader.header['magic'] == offline_replay.MAGIC
for kind, data in rows:
    row = reader.next()
    assert row['type'] == kind and row['data'] == data, (row, kind)
end = reader.next()
assert end['type'] == 'end' and end['records'] == 200 and end['reason'] == 'round_finished'
assert reader.next() is None
data = open(path, 'rb').read()
open(path + '.cut.wotlanreplay', 'wb').write(data[:len(data) // 2])
cut = native_replay.NativeReader(path + '.cut.wotlanreplay')
try:
    while cut.next() is not None:
        pass
except ValueError:
    pass
else:
    raise AssertionError('truncated replay accepted')
try:
    recorder.append('wire', {'type': u'snapshot'})
except ValueError:
    raise AssertionError('closed recorder raised instead of refusing')
print('native replay ok %d' % len(rows))
'''


class NativeReplay(unittest.TestCase):
    def test_python27_native_replay_round_trip(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = python27_audit_runtime()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='wire-replay-') as folder:
            folder = Path(folder)
            sys.path.insert(0, str(ROOT / 'bigworld'))
            from prepare_offline_fork import NATIVE_MODULE_ROOT, RUNTIME_ROOT, stage_runtime
            stage_runtime(Path(fork), folder / 'prepared')
            runtime = folder / 'prepared' / RUNTIME_ROOT
            self.assertFalse(list((folder / 'prepared').rglob('*.wotmod')))
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                            '-Wno-missing-field-initializers', '-DWOT_WIRE_HOST_BUILD', '-bundle',
                            '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                            '-I' + str(ROOT / 'bigworld'), str(ROOT / 'bigworld/stock_wire_codec.cpp'), str(ROOT / 'tests/stock_native_client_state_host.cpp'),
                            str(ROOT / 'bigworld/stock_wire_replay.cpp'), str(ROOT / 'bigworld/stock_wire_net.cpp'), str(ROOT / 'bigworld/stock_wire_replica_store.cpp'),
                            str(ROOT / 'tests/stock_wire_host_client_module.cpp'), '-lz',
                            '-o', str(folder / 'offline_client_wire_native.so')], check=True)
            (folder / 'fixture.py').write_text(REPLAY_FIXTURE)
            events = next(m for m in samples() if m['type'] == 'events')
            (folder / 'events.json').write_text(json.dumps(events))
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder),
                                  str(runtime / 'scripts/client'), str(runtime / NATIVE_MODULE_ROOT),
                                  str(folder / 'replays'), str(folder / 'events.json')],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('native replay ok', run.stdout)


if __name__ == '__main__':
    unittest.main()
