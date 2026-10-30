"""The C++ wire codec (Python 2.7 objects) and the schema's Python 3 codec agree byte for byte."""
import json
import os
from pathlib import Path
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
    return messages


@unittest.skipUnless(subprocess.run(['pyenv', 'which', 'python2.7'], capture_output=True,
                                    env=dict(os.environ, PYENV_VERSION='2.7.18')).returncode == 0,
                     'CPython 2.7.18 audit runtime is required')
class StockWireCodec(unittest.TestCase):
    def test_python27_native_matches_schema_codec(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
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
                            str(ROOT / 'bigworld/stock_wire_codec.cpp'), str(ROOT / 'tests/stock_wire_host_module.cpp'),
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


if __name__ == '__main__':
    unittest.main()
