"""NativeRecorder output passes launcher preflight and the production NativeReader."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
BIGWORLD = ROOT / 'bigworld'
NATIVE_TESTS = ROOT / 'tests'
sys.path.insert(0, str(REPO / 'launcher'))

FIXTURE = r'''
import copy, os, sys, types
sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[2])
for name in ('gui', 'gui.mods'):
    module = types.ModuleType(name)
    module.__path__ = [sys.argv[2] + '/' + name.replace('.', '/')]
    sys.modules[name] = module
sys.modules['offline_client_lifecycle_native'] = types.ModuleType('offline_client_lifecycle_native')
from gui.mods.offline_lan_0922 import native_replay
import offline_client_wire_native as wire
directory = sys.argv[3]
path = directory + '/replay_last_battle.wotlanreplay'
if sys.argv[4] == 'write':
    header = dict(welcome={'type': 'welcome'}, config={'vehicle': 'ussr:R11_MS-1'},
                  start={'type': 'battle_start', 'round_id': 1, 'map': '01_karelia', 'players': [], 'bots': []},
                  snapshot={'type': 'snapshot', 'round_id': 1, 'players': [], 'bots': []})
    recorder = native_replay.NativeRecorder(directory, 1, header, lambda: 100.)
    recorder.append('local', {'value': 'x' * (3 * 1024 * 1024)}, 1.)
    wire.replay_write(recorder._writer, 1, 1., {'type': 'team_chat', 'text': 'native frame'})
    wire.replay_write(recorder._writer, 1, 1., {'type': 'battle_receipt', 'ignored': True})
    replica = dict(type='replica_snapshot', protocol=5, server_tick=1, round_id=1,
        state_revision=0, map='01_karelia', bot_authority_id=-1, bot_state_revision=0,
        bot_order_revision=0, rules={'bases': {}}, destructible_revision=0,
        timing=dict(phase='battle', start_in_ms=0, remaining_ms=1000, duration_ms=1000),
        authority_epoch=1, server_time_ms=1, motion_time_us=1, bot_state_time_us=1,
        projectile_revision=0, worker_status='connected', full=False,
        player_motion=[], bot_motion=[], player_states=[], bot_states=[],
        removed_players=[], removed_bots=[])
    stale = copy.deepcopy(replica)
    stale['player_motion'] = [dict(id=99, **dict((name, 0.) for name in
        ('x','y','z','yaw','pitch','roll','up_cosine','aim_yaw','gun_pitch','forward','turn','speed')))]
    full = dict(replica, full=True)
    # A delta before a full frame, then a stale actor motion invalidates the
    # baseline until another full frame. The native reader skips these rows.
    for message in (replica, full, stale, replica, full):
        assert wire.record_frame(recorder._writer, wire.encode(message), 1.)
    assert recorder.close('leave') == path
else:
    reader = native_replay.NativeReader(path)
    rows = []
    while True:
        row = reader.next()
        if row is None:
            break
        rows.append(row)
    assert [row['type'] for row in rows] == ['local', 'wire', 'wire', 'wire', 'end']
    assert len(rows[0]['data']['value']) == 3 * 1024 * 1024
    assert [row['data']['type'] for row in rows[1:4]] == ['team_chat', 'snapshot', 'snapshot']
    assert rows[-1]['records'] == 8
    assert reader.count == 8
    print('NativeReader boundary passed: 8 stored, 4 playable')
'''


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class NativeReplayPreflightTests(unittest.TestCase):
    def test_native_writer_launcher_and_native_reader(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Pinned gameplay source is required')
        pyenv = shutil.which('pyenv')
        if not pyenv:
            self.skipTest('CPython 2.7 audit runtime is required')
        runtime = subprocess.run([pyenv, 'which', 'python2.7'], text=True, capture_output=True,
                                 env=dict(os.environ, PYENV_VERSION='2.7.18'))
        if runtime.returncode:
            self.skipTest('CPython 2.7 audit runtime is required')
        py27 = runtime.stdout.strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        prepare = load('replay_preflight_prepare', BIGWORLD / 'prepare_offline_fork.py')
        stage_server = load('replay_preflight_server', ROOT / 'wire/stage_server.py')
        with tempfile.TemporaryDirectory(prefix='native-replay-preflight-') as directory:
            folder = Path(directory)
            prepare.stage_runtime(Path(source), folder / 'prepared')
            stage_server.stage(str(Path(source) / 'server'), str(folder / 'server'))
            client = folder / 'prepared/offline-res/scripts/client'
            files = [BIGWORLD / name for name in ('stock_wire_codec.cpp', 'stock_wire_replay.cpp',
                                                  'stock_wire_replica_store.cpp')]
            files += [NATIVE_TESTS / 'stock_native_client_state_host.cpp',
                      Path(__file__).with_name('stock_replay_preflight_host.cpp')]
            link = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-DWOT_WIRE_HOST_BUILD']
            command += link + ['-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(BIGWORLD)]
            command += [str(path) for path in files] + ['-lz', '-o', str(folder / 'offline_client_wire_native.so')]
            compiled = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            script = folder / 'fixture.py'
            script.write_text(FIXTURE, encoding='utf-8')
            args = [py27, '-B', str(script), str(folder), str(client), str(folder / 'replays')]
            made = subprocess.run(args + ['write'], text=True, capture_output=True)
            self.assertEqual(made.returncode, 0, made.stdout + made.stderr)
            import core
            import replay_launch
            with mock.patch.object(core, 'server_script', return_value=str(folder / 'server/windows_server.py')):
                result = replay_launch.validate(folder / 'replays/replay_last_battle.wotlanreplay')
            self.assertEqual(8, result['records'])
            self.assertGreater(result['logical_bytes'], 3 * 1024 * 1024)
            played = subprocess.run(args + ['read'], text=True, capture_output=True)
            self.assertEqual(played.returncode, 0, played.stdout + played.stderr)
            self.assertIn('NativeReader boundary passed', played.stdout)


if __name__ == '__main__':
    unittest.main()
