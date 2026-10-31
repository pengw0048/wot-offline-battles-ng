"""The native diagnostics match the pinned _FrameDiagnostics and compound reports on identical scripted fakes."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = 'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
BOT_RUNTIME = 'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py'
NAMES = ('_FrameDiagnostics', '_combat_log_lines', '_FRAME_STAGE_NAMES', '_FRAME_DETAIL_NAMES', '_LAN_POLL_STAGES',
         '_PROJECTILE_METRIC_NAMES', 'DIAGNOSTIC_WINDOW_SECONDS', 'DIAGNOSTIC_TOP_FRAMES')

FIXTURE = r'''
import collections, copy, gc, json, math, random, sys, weakref
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
_INTEGER_TYPES = (int, long)
exec(open(sys.argv[2]).read())
# A small percentile window exercises the bounded deques and the dropped count.
DIAGNOSTIC_PERCENTILE_SAMPLES = 16

class Clock(object):
    def __init__(self, log, name, values):
        self.log, self.name, self.values, self.count = log, name, values, 0
    def __call__(self):
        value = self.values(self.count)
        self.count += 1
        self.log.append((self.name, self.count))
        if isinstance(value, Exception):
            raise value
        return value

class Transport(object):
    def __init__(self, log, script):
        self.log, self.script, self.count = log, script, 0
    def snapshot(self):
        value = self.script(self.count)
        self.count += 1
        self.log.append(('lan', self.count))
        if isinstance(value, Exception):
            raise value
        return value

class Writer(object):
    def __init__(self, log, fail_at):
        self.log, self.fail_at, self.count = log, fail_at, 0
    def __call__(self, text):
        self.count += 1
        self.log.append(('write', text))
        if self.count in self.fail_at:
            raise IOError('disk full')

STAGES = _FRAME_STAGE_NAMES + _FRAME_DETAIL_NAMES

def lan_script(rng, variant):
    totals = dict(('%s_%s' % (name, suffix), 0 if suffix == 'calls' else 0.0)
                  for name in _LAN_POLL_STAGES for suffix in ('calls', 'wall_seconds'))
    rows = []
    generation = 1
    for index in range(400):
        step = rng.random()
        notify = rng.uniform(0, 0.0004)
        handle = notify + rng.uniform(0, 0.0004)
        materialize = rng.uniform(0, 0.0004)
        poll = handle + materialize + rng.uniform(0, 0.0004)
        if variant % 5 == 1 and step < 0.08:
            poll = 1.0
        elif variant % 5 == 2 and step < 0.3:
            # Child timers just past the 1e-6 roundoff allowance, and just inside it.
            poll = handle + materialize - rng.choice([5e-6, 5e-7])
        elif variant % 5 == 3 and step < 0.2:
            notify = -0.0005
        for name, value in (('poll', poll), ('materialize', materialize), ('handle', handle), ('notify', notify)):
            totals[name + '_wall_seconds'] += value
            totals[name + '_calls'] += rng.randint(0, 3)
        row = dict(totals, generation=generation)
        if step < 0.03:
            generation += 1
        elif step < 0.05 and variant % 3 == 0:
            row = 'not a dict'
        elif step < 0.07 and variant % 3 == 1:
            row = dict(row, generation=True)
        elif step < 0.09 and variant % 3 == 2:
            row = dict(row, poll_calls=-1)
        elif step < 0.10:
            row = dict(row, handle_wall_seconds='x')
        elif step < 0.11:
            row = dict(row)
            del row['notify_calls']
        elif step < 0.12:
            row = ValueError('transport')
        elif step < 0.13:
            row = dict(row, materialize_wall_seconds=float('inf'))
        elif step < 0.14:
            row = dict(row, generation=-1)
        elif step < 0.15:
            row = dict(row, poll_wall_seconds=row['poll_wall_seconds'] - 5.0)
        rows.append(row)
    return lambda count: copy.deepcopy(rows[count % len(rows)])

def value_of(rng, low, high, bad=0.0):
    pick = rng.random()
    if pick < bad:
        return rng.choice(['bad', [1], None])
    if pick < 0.08:
        return rng.randint(0, 3)
    if pick < 0.1:
        return True
    return rng.uniform(low, high)

def scenario(seed):
    rng = random.Random(seed)
    bad = 0.0 if seed % 4 else 0.002
    ops = []
    t = 50.0 + rng.random()
    for step in range(rng.randint(10, 70)):
        gap = rng.choice([rng.uniform(0.001, 0.03), rng.uniform(0.03, 0.12), rng.uniform(-0.01, 0.0)])
        if seed % 17 == 3 and step == 5:
            gap = float('nan')
        t += gap
        if seed % 19 == 7:
            # Callback entries whose differences are exactly the 50 ms and 100 ms thresholds.
            t = (0.0, 0.05, 0.1, 0.2, 0.25, 0.35)[step % 6] + 8.0 * (step // 6)
        if seed % 9 == 4 and step % 7 == 6:
            ops.append(('begin_kw', t, rng.uniform(-0.01, 0.15)))
        else:
            ops.append(('begin', t, rng.choice([rng.uniform(-0.01, 0.15), 0.1]), rng.choice([None, 0.0, rng.uniform(0, 0.004), -0.002])))
        stages = dict((name, value_of(rng, -0.001, 0.02, bad)) for name in STAGES if rng.random() < 0.7)
        if rng.random() < 0.2:
            stages['unlisted'] = 0.5
        probes = dict((name, rng.choice([rng.randint(-1, 9), rng.uniform(0, 5), '7'] + (['x'] if bad else [])))
                      for name in PROBE_KINDS if rng.random() < 0.6)
        role = rng.choice(['worker', 'worker', 'authority', 'player', None, u'worker'])
        context = {'role': role, 'round': rng.choice(['r1', 3, None]), 'map': 'm', 'phase': 'battle',
                   'probe_timing': 'on', 'pose_step': value_of(rng, 0, 1, bad), 'speed': rng.uniform(0, 10),
                   'camera_speed': rng.uniform(0, 10), 'airborne': rng.choice([True, False, None]),
                   'grind': rng.choice([0, 2, 2.7]), 'bot_count': rng.randint(0, 29), 'outgoing_count': 4,
                   'transitioned': rng.random() < 0.1, 'authority_time': rng.uniform(0, 100)}
        for key in list(context):
            if rng.random() < 0.15:
                del context[key]
        durations = (None if rng.random() < 0.3 else
                     dict((name, value_of(rng, 0, 0.003, bad)) for name in PROBE_KINDS if rng.random() < 0.5))
        projectile = rng.choice([None, {}, dict((name, value_of(rng, 0, 4, bad)) for name in _PROJECTILE_METRIC_NAMES
                                                if rng.random() < 0.8)])
        combat = rng.choice([None, {'shot': step}])
        frame = rng.choice(['last'] * 12 + ['text', 9])
        ops.append(('finish', frame, t, rng.uniform(0, 0.05), rng.uniform(0, 0.05), stages, probes, context,
                    durations, projectile, combat, rng.choice(['positional', 'keyword', 'short'])))
        pick = rng.random() * (0.5 if step % 5 == 4 else 1.0)
        if pick < 0.05:
            ops.append(('note_collections', rng.choice([{'records': 3, u'journal': 2.5, 7: '4'}, 'x', {}])))
        elif pick < 0.1:
            ops.append(('note_bot_load', rng.choice([{'busiest': [(3, 4), [5, 6]]}, {'busiest': None}, {'busiest': [(1,)]}, []])))
        elif pick < 0.15:
            ops.append(('note_worker_runtime', rng.choice([
                {'control': {'control_steps': 3, 'astar_failed': 1}, 'presentation': {'pose_writes': 2},
                 'bot_diagnostics': {'visibility_queue_depth': 5},
                 'combat_checkpoint': {'k': 'x' * rng.choice([9000, 7040, 7080, 7100])}},
                {'control': None, 'presentation': 0}, 'x'])))
        elif pick < 0.2:
            ops.append(('note_combat_captures', rng.choice([[{'c': 1}], ({'c': 2}, {'c': 3}), 5])))
        elif pick < 0.22:
            ops.append(('flush',))
        elif pick < 0.24:
            ops.append(('reset',))
        elif pick < 0.3:
            ops.append(('enable', rng.choice([True, True, False, 1, 0])))
    ops.append(('flush',))
    return ops

# The wall clock follows the driver's callback entry time, as _PROFILE_CLOCK does in the frame.
NOW = [0.0]

def make(impl, log, seed):
    rng = random.Random(seed * 31 + 7)
    clock = Clock(log, 'clock', lambda i: float('inf') if seed % 11 == 5 and i == 40 else NOW[0] + (i % 5) * 0.0002)
    cpu_variant = seed % 6
    cpu = Clock(log, 'cpu', lambda i: (
        None if cpu_variant == 0 else
        (ValueError('cpu') if i % 13 == 12 else -1.0 if i % 17 == 16 else 'x' if i % 19 == 18 else
         float('inf') if i % 23 == 22 else 10.0 + i * 0.0011 - (0.01 if i % 11 == 10 else 0.0))))
    windows = [None, (1, 0, 1), [0, -1, 0], (2, 0, 0), (1, 0), 'abc', ValueError('win'), (1.9, 0, -1)]
    window = Clock(log, 'window', lambda i: windows[(i * 3 + seed) % len(windows)])
    writer = Writer(log, set([2, 5]) if seed % 7 == 3 else set())
    kwargs = dict(clock=clock, writer=writer, window_seconds=rng.choice([0.05, 0.2, 0.4, 1.0]),
                  cpu_clock=cpu if cpu_variant != 1 else None,
                  window_clock=window if seed % 5 != 2 else None)
    if seed % 3 == 0:
        kwargs['initial_window_seconds'] = rng.choice([0.1, 0.3, 0.6])
    if seed % 13 == 6:
        del kwargs['clock']
        globals()['_PROFILE_CLOCK'] = clock
    if impl == 'pinned':
        return _FrameDiagnostics(**kwargs)
    return native.FrameDiagnostics(globals(), **kwargs)

def drive(impl, seed, ops):
    log = []
    diagnostics = make(impl, log, seed)
    transports = [Transport(log, lan_script(random.Random(seed), seed)), Transport(log, lan_script(random.Random(seed + 1), seed))]
    plain = lambda: transports[0].snapshot()
    last = [0]
    def record(name, call):
        try:
            result = call()
            if name in ('begin', 'begin_kw'):
                last[0] = result
            log.append((name, 'ok', repr(result)))
        except Exception as error:
            log.append((name, 'error', type(error).__name__))
        log.append(('state', repr(diagnostics.enabled), diagnostics.emit_due(), repr(diagnostics.snapshot())))
    for index, op in enumerate(copy.deepcopy(ops)):
        kind = op[0]
        if kind in ('begin', 'begin_kw'):
            NOW[0] = op[1]
        if kind == 'begin':
            getter = [transports[0].snapshot, transports[1].snapshot, plain, None, 5][(index // 9 + seed) % 5 if seed % 4 else 0]
            if op[3] is None:
                record(kind, lambda: diagnostics.begin(op[1], op[2], lan_poll_getter=getter))
            else:
                record(kind, lambda: diagnostics.begin(op[1], op[2], op[3], getter))
        elif kind == 'begin_kw':
            record(kind, lambda: diagnostics.begin(entry_wall=op[1], raw_dt=op[2]))
        elif kind == 'finish':
            frame = {'last': last[0], 'text': 'seven'}.get(op[1], op[1])
            if op[11] == 'positional':
                record(kind, lambda: diagnostics.finish(frame, *op[2:11]))
            elif op[11] == 'keyword':
                record(kind, lambda: diagnostics.finish(frame, op[2], op[3], op[4], op[5], op[6], op[7],
                                                        probe_durations=op[8], projectile=op[9], combat=op[10]))
            else:
                record(kind, lambda: diagnostics.finish(frame, *op[2:8]))
        elif kind == 'enable':
            diagnostics.enabled = op[1]
            if op[1]:
                record('reset', diagnostics.reset)
        elif len(op) == 1:
            record(kind, getattr(diagnostics, kind))
        else:
            record(kind, lambda: getattr(diagnostics, kind)(op[1]))
    return log

cases = 0
for seed in range(int(sys.argv[3])):
    ops = scenario(seed)
    pinned, native_log = drive('pinned', seed, ops), drive('native', seed, ops)
    if pinned != native_log:
        for index, (a, b) in enumerate(zip(pinned, native_log)):
            if a != b:
                print('seed %d first difference at %d' % (seed, index))
                print('pinned: %r' % (a,))
                print('native: %r' % (b,))
                break
        else:
            print('seed %d length %d != %d' % (seed, len(pinned), len(native_log)))
        sys.exit(1)
    cases += len(pinned)

# Default writer: sys.stdout.write of the stream current at construction.
outputs = []
for impl in ('pinned', 'native'):
    written = []
    stdout = sys.stdout
    sys.stdout = collections.namedtuple('Stream', 'write')(written.append)
    try:
        log = []
        clock = Clock(log, 'clock', lambda i: 3.0 + i * 0.01)
        diagnostics = (_FrameDiagnostics(clock=clock, window_seconds=0.01) if impl == 'pinned' else
                       native.FrameDiagnostics(globals(), clock=clock, window_seconds=0.01))
    finally:
        sys.stdout = stdout
    for frame in range(4):
        diagnostics.begin(3.0 + frame * 0.02, 0.02)
        diagnostics.finish(frame + 1, 3.0 + frame * 0.02, 0.0, 0.0, {}, {}, {'role': 'player'})
    diagnostics.flush()
    outputs.append(written)
assert outputs[-1] == outputs[-2] and outputs[-1], (outputs[-2], outputs[-1])

# A window clock closing over the owner forms a cycle the collector must break.
class Owner(object):
    pass
owner = Owner()
owner.diagnostics = native.FrameDiagnostics(globals(), window_clock=lambda: owner)
owner.diagnostics.begin(1.0, 0.0)
owner.diagnostics.finish(1, 1.0, 0.0, 0.0, {}, {}, {})
probe = weakref.ref(owner)
del owner
gc.collect()
assert probe() is None, 'frame diagnostics cycle was not collected'

print('frame diagnostics differential ok %d' % cases)
'''

COMPOUND_NAMES = ('_number', '_xyz', '_format_xyz', '_format_axes')
COMPOUND_METHODS = ('_report_local_compound', '_report_local_decals')

COMPOUND_FIXTURE = r'''
import copy, math, random, sys
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
exec(open(sys.argv[2]).read())

class Obj(object):
    """Fake engine object with logged attribute reads."""
    def __init__(self, log, name, **attrs):
        self.__dict__['_log'], self.__dict__['_name'] = log, name
        self.__dict__.update(attrs)
    def __getattribute__(self, attr):
        if not attr.startswith('_'):
            object.__getattribute__(self, '_log').append(('get', object.__getattribute__(self, '_name'), attr))
        return object.__getattribute__(self, attr)
    def __repr__(self):
        return '<%s>' % object.__getattribute__(self, '_name')

class Axis(object):
    def __init__(self, log, lengths):
        self.log, self.lengths = log, lengths
    def __call__(self, index):
        self.log.append(('axis', index))
        value = self.lengths[index]
        if isinstance(value, Exception):
            raise value
        return Obj(self.log, 'axis%d' % index, length=value)

class Seq(object):
    """An indexable value whose third item can be missing or raise."""
    def __init__(self, items):
        self.items = items
    def __getitem__(self, index):
        value = self.items[index]
        if isinstance(value, Exception):
            raise value
        return value

def translation(rng):
    return rng.choice([(1.234, -2.5, 3.0), [1, 2, 3], {'x': 1.5, 'y': 'q', 'z': None}, None, 'xyz',
                       Seq([1.0, 2.0]), Seq([1.0, KeyError('k'), 2.0]), float('nan'),
                       type('V', (object,), {'x': 4.0, 'y': 5.0, 'z': float('inf')})()])

def lengths(rng):
    return rng.choice([[1.0, 1.0, 1.0], [0.0, 2.5, 1e-9], [1.0, ValueError('axis'), 1.0], [1, 'x', None],
                       [1.0, 1.0, KeyboardInterrupt('stop')] if rng.random() < 0.02 else [1.0, 1.0, 1.0]])

def matrix(log, rng):
    pick = rng.random()
    if pick < 0.1:
        return None
    attrs = {'translation': translation(rng)}
    if pick > 0.15:
        attrs['applyToAxis'] = Axis(log, lengths(rng)) if pick > 0.2 else 'not callable'
    return Obj(log, 'matrix', **attrs)

def target(log, rng):
    if rng.random() < 0.1:
        return None
    attrs = dict((name, rng.choice([True, False, 30.0, 'x', None])) for name in
                 ('isEnabled', 'isFull', 'selectionFovDegrees', 'maxDistance', 'skeletonCheckEnabled')
                 if rng.random() < 0.85)
    entity = rng.choice([None, Obj(log, 'picked', id=7), Obj(log, 'anonymous')])
    if rng.random() < 0.7:
        class Target(Obj):
            def __call__(self):
                object.__getattribute__(self, '_log').append(('target()',))
                return entity
        return Target(log, 'target', **attrs)
    return Obj(log, 'target', **attrs)

def descriptor(log, rng):
    if rng.random() < 0.15:
        return None
    parts = {}
    for name in ('chassis', 'hull', 'turret'):
        if rng.random() < 0.85:
            decals = rng.choice([None, [], 0, [Obj(log, 'decal', translation=translation(rng), applyToAxis=Axis(log, lengths(rng)))
                                            for unused in range(rng.randint(1, 3))], (Obj(log, 'bare'),)])
            attrs = {'AODecals': decals} if decals is not None else {}
            if name == 'chassis' and rng.random() < 0.8:
                attrs['hullPosition'] = translation(rng)
            parts[name] = Obj(log, name, **attrs)
    return Obj(log, 'descriptor', **parts)

def battle(log, seed):
    rng = random.Random(seed)
    b = Pinned()
    b._next_compound_report = 0.0
    b._compound_reports = rng.choice([0, 0, 0, 7, 8])
    b._compound_report_signature = None
    state = {'rng': rng}
    b._local_body_pose = lambda: state['matrix']
    b._runtime = Obj(log, 'runtime', bigworld=Obj(log, 'bigworld'))
    b._server = rng.choice([None, Obj(log, 'server', vehicle_id=11)])
    entity_attrs = {}
    if rng.random() < 0.9:
        entity_attrs['typeDescriptor'] = descriptor(log, rng)
    if rng.random() < 0.6:
        entity_attrs['appearance'] = Obj(log, 'appearance', _CompoundAppearance__splodge=rng.choice([None, 'splodge']))
    entity = Obj(log, 'entity', **entity_attrs)
    def server_entity(engine):
        log.append(('server_entity', engine))
        return entity
    b._server_entity = server_entity
    return b, state

class Out(object):
    def __init__(self, log):
        self.log = log
    def write(self, text):
        self.log.append(('stdout', text))

NATIVE = {'_report_local_compound': lambda b, now: native.report_local_compound(b, now, globals()),
          '_report_local_decals': lambda b: native.report_local_decals(b, globals())}
cases = 0
for seed in range(int(sys.argv[3])):
    logs = []
    for impl in ('pinned', 'native'):
        log = []
        b, state = battle(log, seed)
        rng = random.Random(seed * 7 + 1)
        steady = matrix(log, rng), target(log, rng)
        now = 0.0
        stdout = sys.stdout
        sys.stdout = Out(log)
        try:
            for step in range(12):
                now += rng.choice([0.5, 5.0, 6.0])
                if rng.random() < 0.4:
                    steady = matrix(log, rng), target(log, rng)
                state['matrix'] = steady[0]
                if steady[1] is None:
                    b._runtime.__dict__.pop('target', None) if False else None
                    b._runtime.bigworld.__dict__.pop('target', None)
                else:
                    b._runtime.bigworld.__dict__['target'] = steady[1]
                name = '_report_local_decals' if rng.random() < 0.15 else '_report_local_compound'
                args = () if name == '_report_local_decals' else (rng.choice([now, now, 'late']),)
                try:
                    if impl == 'pinned':
                        result = ('ok', Pinned.__dict__[name](b, *args))
                    else:
                        result = ('ok', NATIVE[name](b, *args))
                except BaseException as error:
                    result = ('error', type(error).__name__)
                log.append((name, result, repr(b._compound_report_signature), b._next_compound_report, b._compound_reports))
        finally:
            sys.stdout = stdout
        logs.append(log)
    if logs[0] != logs[1]:
        for index, (a, c) in enumerate(zip(*logs)):
            if a != c:
                print('seed %d first difference at %d\npinned: %r\nnative: %r' % (seed, index, a, c))
                break
        else:
            print('seed %d length %d != %d' % (seed, len(logs[0]), len(logs[1])))
        sys.exit(1)
    cases += len(logs[0])
print('compound report differential ok %d' % cases)
'''


def compound_source(fork):
    """The pinned compound report methods with the module helpers they format through."""
    source = (Path(fork) / RUNTIME).read_text()
    lines = source.splitlines()
    helpers, methods = [], []
    for node in ast.parse(source).body:
        if isinstance(node, ast.FunctionDef) and node.name in COMPOUND_NAMES:
            helpers.append('\n'.join(lines[node.lineno - 1:node.end_lineno]))
        if isinstance(node, ast.ClassDef) and node.name == 'BattleRuntime':
            for item in node.body:
                if isinstance(item, ast.Assign) and any(getattr(t, 'id', '').startswith('_COMPOUND_REPORT_')
                                                        for t in item.targets):
                    methods.append('\n'.join(lines[item.lineno - 1:item.end_lineno]))
                if isinstance(item, ast.FunctionDef) and item.name in COMPOUND_METHODS:
                    methods.append('\n'.join(lines[item.lineno - 1:item.end_lineno]))
    return ('import math\n\n' + '\n\n'.join(helpers) + '\n\nclass Pinned(object):\n' + '\n\n'.join(methods) + '\n')


def pinned_source(fork):
    """_FrameDiagnostics and its module helpers as staged by every other adapter."""
    import importlib.util
    import sys
    sys.path.insert(0, str(ROOT / 'bigworld'))
    spec = importlib.util.spec_from_file_location('prepare_offline_fork', ROOT / 'bigworld/prepare_offline_fork.py')
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    prepare.client_lifecycle_bindings.battle = lambda source: source
    chunks = []
    for path, names in ((RUNTIME, NAMES), (BOT_RUNTIME, ('PROBE_KINDS',))):
        source = (Path(fork) / path).read_text()
        if path == RUNTIME:
            source = prepare.ADAPTERS[Path(RUNTIME)](source)
        lines = source.splitlines()
        for node in ast.parse(source).body:
            named = ([getattr(t, 'id', None) for t in node.targets] if isinstance(node, ast.Assign) else
                     [node.name] if isinstance(node, (ast.FunctionDef, ast.ClassDef)) else [])
            if any(name in names for name in named):
                chunks.append('\n'.join(lines[node.lineno - 1:node.end_lineno]))
    return '\n\n'.join(chunks) + '\n'


class FrameDiagnosticsDifferential(unittest.TestCase):
    def test_native_type_matches_pinned_class(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='frame-diagnostics-') as folder:
            folder = Path(folder)
            # Contraction would fuse the percentile interpolation; #1513 SSE2 rounds each step.
            subprocess.run(['c++', '-std=c++17', '-O1', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', '-Wno-register', '-Wno-missing-field-initializers', '-bundle',
                            '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                            '-I' + str(ROOT / 'bigworld'), str(ROOT / 'tests/stock_native_client_lifecycle_host.cpp'),
                            '-o', str(folder / 'offline_client_lifecycle_native.so')], check=True)
            (folder / 'pinned.py').write_text(pinned_source(fork))
            (folder / 'fixture.py').write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder), str(folder / 'pinned.py'), '400'],
                                 capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr[-6000:])
            self.assertIn('frame diagnostics differential ok', run.stdout)
            print(run.stdout)
            (folder / 'compound.py').write_text(compound_source(fork))
            (folder / 'compound_fixture.py').write_text(COMPOUND_FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'compound_fixture.py'), str(folder), str(folder / 'compound.py'),
                                  '600'], capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr[-6000:])
            self.assertIn('compound report differential ok', run.stdout)
            print(run.stdout)


if __name__ == '__main__':
    unittest.main()
