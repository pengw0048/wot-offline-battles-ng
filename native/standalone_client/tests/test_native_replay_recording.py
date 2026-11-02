"""Recording starts at an accepted round barrier; unchanged recording and timing remain differential."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = 'src/res/scripts/client/gui/mods/offline_lan_0922/offline_replay.py'
LAN_CLIENT = 'src/res/scripts/client/gui/mods/offline_lan_0922/lan_client.py'
NAMES = ('log', 'poll_process_status', 'ensure_recording', 'local_state', 'record_local', 'LOCAL_SCALARS', 'CONFIG_KEYS')

FIXTURE = r'''
import copy, random, sys, types
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
SOURCE = open(sys.argv[2]).read()

class Obj(object):
    """Fake object whose method calls are logged; listed attributes are plain values."""
    def __init__(self, log, name, calls=None, missing=(), **attrs):
        self.__dict__.update(_log=log, _name=name, _calls=calls or {}, _missing=missing)
        self.__dict__.update(attrs)
    def __repr__(self):
        return '<%s>' % self._name
    def __getattr__(self, attr):
        if attr.startswith('__') or attr in self._missing or attr not in self._calls:
            raise AttributeError(attr)
        answer = self._calls[attr]
        def call(*args, **kwargs):
            self._log.append((self._name, attr) + tuple(repr(a) for a in args) + tuple(sorted((k, repr(v)) for k, v in kwargs.items())))
            if isinstance(answer, Exception):
                raise answer
            return answer(*args, **kwargs) if callable(answer) else answer
        return call
    def __setattr__(self, attr, value):
        self._log.append(('set', self._name, attr, repr(value)))
        self.__dict__[attr] = value

class Out(object):
    def __init__(self, log):
        self.log = log
    def write(self, text):
        self.log.append(('stdout', text))

def namespace(log, v, impl):
    g = {'__name__': 'offline_replay', 'copy': copy, 'sys': sys, 'MAX_DURATION': 14400.0}
    exec(SOURCE, g)
    g['_FINISHING'] = [] if v % 5 else ['other']
    g['_drain_process'] = lambda recorder: log.append(('drain', repr(recorder)))
    g['_schedule_finishing'] = lambda: log.append(('schedule_finishing',))
    g['capture_message'] = lambda message, hint=None: (log.append(('capture', repr(message))) or
                                                       (dict(message) if isinstance(message, dict) else {'raw': repr(message)}))
    g['native_recording_mode'] = lambda: log.append(('mode',)) or (0, 1, 2)[v % 3]
    g['os'] = Obj(log, 'os', {'getcwd': 'C:/game', 'getcwdu': u'C:/game', 'getpid': 1},
                  missing=() if v % 2 else ('getcwdu',), path=Obj(log, 'path', {'join': lambda *parts: '/'.join(parts)}))
    # The native writer must attach after the accepted live barrier. Keep
    # the pinned body as the reference for everything past this new boundary.
    original_start = g['ensure_recording']
    def guarded_start(value):
        client = value.client
        start = value._start_message or {}
        round_id = start.get('round_id')
        live = getattr(client, '_offline_replay_live', None) if client is not None else None
        snapshot = getattr(client, 'last_snapshot', None) if client is not None else None
        eligible = (value.state == 'running' and round_id is not None and
                    isinstance(live, dict) and live.get('type') == 'battle_live' and live.get('round_id') == round_id and
                    isinstance(snapshot, dict) and snapshot.get('type') == 'snapshot' and snapshot.get('round_id') == round_id)
        if eligible:
            return original_start(value)
        if client is not None:
            g['poll_process_status'](client)
    g['ensure_recording'] = guarded_start
    if impl == 'native':
        for name in ('poll_process_status', 'ensure_recording', 'local_state', 'record_local'):
            g[name] = (lambda name: lambda value: getattr(native, name)(value, g))(name)
    return g

def recorder(log, v, name='recorder'):
    calls = {'append': ValueError('disk') if v % 11 == 10 else True, 'abort': None, 'attach': None}
    if v % 4 == 1:
        calls['poll_messages'] = None
    # v % 31 == 30 lands exactly on the 1/30 s cadence boundary.
    return Obj(log, name, calls, closed=v % 13 == 12, started=0.0 if v % 31 == 30 else 2.0,
               last_pose_at=0.0 if v % 31 == 30 else (-1.0, 7.99, 7.9)[v % 3],
               failed=v % 3 == 0, path='/replays/x')

def runtime(log, v):
    client = Obj(log, 'client', missing=() if v % 7 else ('_offline_replay_recorder',),
                 _offline_replay_recorder=None if v % 6 == 5 else recorder(log, v), is_offline_replay=v % 17 == 16,
                 _offline_replay_attempted=(None, 'r1')[v % 9 == 8], _offline_replay_welcome=({'w': 1}, 'bad')[v % 10 == 9],
                 _offline_replay_live=(None, {'type': 'battle_live', 'round_id': 'r0'},
                                       {'type': 'battle_live', 'round_id': 'r1'}, {'type': 'other', 'round_id': 'r1'})[v % 4],
                 last_snapshot=(None, {'type': 'snapshot', 'round_id': 'r1', 'actors': []},
                                {'type': 'snapshot', 'round_id': 'r0'}, {'players': []})[v % 7 % 4],
                 player_id=(4, '4', 'x')[v % 19 // 17 * 2 if v % 19 >= 17 else 0])
    records = {'bot:1': {'kind': 'bot', 'spot_visible': v % 2 == 0, 'spot_marker_visible': 0},
               'bot:2': {'kind': 'bot'}, 'player:4': {'kind': 'player'}, 'bot:3': {'kind': u'bot', 'spot_visible': 'yes'}}
    rotator = Obj(log, 'rotator', turretYaw=0.5, gunPitch=-0.1) if v % 3 else None
    entity = Obj(log, 'entity', health=(300, 299.7, None)[v % 3] if v % 23 != 22 else 'x', gunAngles=12)
    gun = Obj(log, 'gun', ammo={'shell': [1, 2]}, shot_index=3, clip=[1, 2], reload_time=4.5) if v % 4 else None
    attrs = dict(client=client, state='stopped' if v % 13 == 12 else 'running', _worker_mode=v % 29 == 28, _client_ready_received=v % 8 != 7, _ready_sent=True,
                 _start_message=({'round_id': 'r1'}, None)[v % 14 == 13], _config={'map': 'm', 'vehicle': 'v', 'other': 1},
                 _local_descriptor='desc', _local_position=(1.0, 2.0, 3.0), _local_yaw=0.25, _local_speed=3,
                 _server=Obj(log, 'server', vehicle_id=4) if v % 5 else None, _avatar=Obj(log, 'avatar', gunRotator=rotator),
                 _gun_state=gun, _records=records)
    if v % 16 == 15:
        del attrs['_local_yaw']
    return Obj(log, 'runtime', {'_clock': lambda: 1.0 / 30.0 if v % 31 == 30 else (10.0, 10, 'x')[v % 37 // 36 * 2 if v % 37 == 36 else 0],
                                '_server_entity': lambda engine: entity,
                                '_garage_loadout_snapshot': lambda: {'crew_group': v % 3}}, **attrs)

def modules(log, v):
    def recorder_type(directory, mode, header, clock=None):
        log.append(('NativeRecorder', directory, mode, sorted(header.items()), callable(clock)))
        if v % 12 == 11:
            raise IOError('no writer')
        return recorder(log, v, 'new_recorder')
    for name, attrs in (('native_replay', {'NativeRecorder': recorder_type}),
                        ('replay_presentation', {'descriptor_contract': lambda d: log.append(('contract', d)) or 'contract'}),
                        ('replay_resources', {'active_signature': lambda root: 'sig:' + root})):
        module = types.ModuleType('gui.mods.offline_lan_0922.' + name)
        module.__dict__.update(attrs)
        sys.modules[module.__name__] = module
        setattr(sys.modules['gui.mods.offline_lan_0922'], name, module)

for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    sys.modules.setdefault(name, types.ModuleType(name))
cases = 0
for v in range(int(sys.argv[3])):
    for function in ('poll_process_status', 'ensure_recording', 'local_state', 'record_local'):
        logs = []
        for impl in ('pinned', 'native'):
            log = []
            modules(log, v)
            g = namespace(log, v, impl)
            target = runtime(log, v)
            if v % 5 == 3 and target.client.__dict__.get('_offline_replay_recorder') is not None:
                g['_FINISHING'].append(target.client.__dict__['_offline_replay_recorder'])
            argument = target.client if function == 'poll_process_status' else target
            stdout, sys.stdout = sys.stdout, Out(log)
            try:
                result = ('ok', repr(g[function](argument)))
            except Exception as error:
                result = ('error', type(error).__name__)
            finally:
                sys.stdout = stdout
            log.append(('result', result, repr(g['_FINISHING'])))
            logs.append(log)
        if logs[0] != logs[1]:
            for index, (a, b) in enumerate(zip(*logs)):
                if a != b:
                    print('%s v=%d first difference at %d\npinned: %r\nnative: %r' % (function, v, index, a, b))
                    break
            else:
                print('%s v=%d length %d != %d' % (function, v, len(logs[0]), len(logs[1])))
            sys.exit(1)
        cases += len(logs[0])
print('replay recording differential ok %d' % cases)
'''

PERFORMANCE_FIXTURE = r'''
import random, sys
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
exec(open(sys.argv[2]).read())
cases = 0
for seed in range(int(sys.argv[3])):
    rng = random.Random(seed)
    clock_values = [rng.choice([rng.uniform(0, 100), 5, -1.0, 0.0, float('inf'), float('nan'), 'x', ValueError('clock')])
                    if rng.random() < 0.15 else 10.0 + i * 0.001 for i in range(400)]
    ops = []
    for step in range(60):
        kind = rng.choice(['clock', 'finish', 'finish', 'snapshot', 'depth', 'valid'])
        if kind == 'finish':
            ops.append((kind, rng.choice(['poll', 'materialize', 'handle', 'notify', 'other', u'poll', 3]),
                        rng.choice([None, 'clock', 9.0, 11.0, 'x'])))
        elif kind == 'depth':
            ops.append((kind, rng.choice(['poll_depth', 'handle_depth']), rng.choice([0, 1, 2, -1])))
        elif kind == 'valid':
            ops.append((kind, rng.choice([True, False, 1, 0, None])))
        else:
            ops.append((kind,))
    logs = []
    for impl in ('pinned', 'native'):
        log = []
        count = [0]
        def clock():
            value = clock_values[count[0] % len(clock_values)]
            count[0] += 1
            log.append(('clock', count[0]))
            if isinstance(value, Exception):
                raise value
            return value
        globals()['_TRANSPORT_PROFILE_CLOCK'] = clock
        timing = (PinnedPerformance(seed % 3) if impl == 'pinned' else native.TransportPerformance(globals(), seed % 3))
        for op in ops:
            try:
                if op[0] == 'clock':
                    result = timing.clock()
                elif op[0] == 'finish':
                    started = timing.clock() if op[2] == 'clock' else op[2]
                    result = timing.finish(op[1], started)
                elif op[0] == 'snapshot':
                    result = timing.snapshot()
                elif op[0] == 'depth':
                    result = setattr(timing, op[1], getattr(timing, op[1]) + op[2])
                else:
                    result = setattr(timing, 'valid', op[1])
                log.append(('ok', repr(result)))
            except Exception as error:
                log.append(('error', type(error).__name__))
            log.append((repr(timing.values), timing.poll_depth, timing.handle_depth, repr(timing.valid), timing.generation))
        logs.append(log)
    if logs[0] != logs[1]:
        for index, (a, b) in enumerate(zip(*logs)):
            if a != b:
                print('seed %d first difference at %d\npinned: %r\nnative: %r' % (seed, index, a, b))
                break
        sys.exit(1)
    cases += len(logs[0])
print('transport performance differential ok %d' % cases)
'''


def performance_source(fork):
    """The pinned lan_client._TransportPerformance class."""
    source = (Path(fork) / LAN_CLIENT).read_text()
    lines = source.splitlines()
    node = next(n for n in ast.parse(source).body if isinstance(n, ast.ClassDef) and n.name == '_TransportPerformance')
    text = '\n'.join(lines[node.lineno - 1:node.end_lineno])
    return text.replace('class _TransportPerformance(object):', 'class PinnedPerformance(object):') + '\n'


def staged_source(fork):
    """The staged offline_replay functions before the lifecycle routes."""
    import importlib.util
    import sys
    sys.path.insert(0, str(ROOT / 'bigworld'))
    spec = importlib.util.spec_from_file_location('prepare_offline_fork', ROOT / 'bigworld/prepare_offline_fork.py')
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    source = prepare.wire_bindings.offline_replay((Path(fork) / MODULE).read_text())
    lines = source.splitlines()
    chunks = []
    for node in ast.parse(source).body:
        named = ([getattr(t, 'id', None) for t in node.targets] if isinstance(node, ast.Assign) else
                 [node.name] if isinstance(node, ast.FunctionDef) else [])
        if any(name in NAMES for name in named):
            chunks.append('\n'.join(lines[node.lineno - 1:node.end_lineno]))
    return '\n\n'.join(chunks) + '\n'


class ReplayRecordingDifferential(unittest.TestCase):
    def test_native_ports_match_staged_functions(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='replay-recording-') as folder:
            folder = Path(folder)
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-register',
                            '-Wno-missing-field-initializers', '-bundle', '-undefined', 'dynamic_lookup',
                            '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                            str(ROOT / 'tests/stock_native_client_lifecycle_host.cpp'),
                            '-o', str(folder / 'offline_client_lifecycle_native.so')], check=True)
            (folder / 'staged.py').write_text(staged_source(fork))
            (folder / 'fixture.py').write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder), str(folder / 'staged.py'), '400'],
                                 capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr[-6000:])
            self.assertIn('replay recording differential ok', run.stdout)
            print(run.stdout)
            (folder / 'performance.py').write_text(performance_source(fork))
            (folder / 'performance_fixture.py').write_text(PERFORMANCE_FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'performance_fixture.py'), str(folder), str(folder / 'performance.py'),
                                  '400'], capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr[-6000:])
            self.assertIn('transport performance differential ok', run.stdout)
            print(run.stdout)


if __name__ == '__main__':
    unittest.main()
