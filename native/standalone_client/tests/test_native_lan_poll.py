"""The native LANClient poll chain matches the staged methods on identical scripted fakes."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = 'src/res/scripts/client/gui/mods/offline_lan_0922/lan_client.py'
METHODS = ('_poll', '_poll_messages', '_take_ready_messages', '_dispatch_message', '_notify', '_notify_message',
           'transport_performance_snapshot', '_report_snapshot_stall')
GLOBALS = ('_finite_float', 'PING_INTERVAL', 'WORKER_AUTHORITY_ID', 'SNAPSHOT_STALL_SECONDS', 'RUNTIME_DROP_LOG_INTERVAL')

FIXTURE = r'''
import copy, math, random, sys, threading, types
sys.path.insert(0, sys.argv[1])
import offline_client_lifecycle_native as native
for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    sys.modules[name] = types.ModuleType(name)
LOG = []
replay = types.ModuleType('gui.mods.offline_lan_0922.offline_replay')
replay.observe_wire = lambda client, message, hint: LOG.append(('observe_wire', repr(message), hint))
sys.modules[replay.__name__] = replay
sys.modules['gui.mods.offline_lan_0922'].offline_replay = replay
class SnapshotDeltaError(Exception):
    pass
snapshot_delta = types.ModuleType('snapshot_delta')
snapshot_delta.SnapshotDeltaError = SnapshotDeltaError
exec(open(sys.argv[2]).read())

class Performance(object):
    def __init__(self, script):
        self.script, self.poll_depth, self.handle_depth, self.valid, self.count = script, 0, 0, True, 0
    def clock(self):
        self.count += 1
        LOG.append(('perf.clock', self.count))
        return None if self.script(self.count) else 1.0 + self.count
    def finish(self, name, started):
        LOG.append(('perf.finish', name, started))
        if name == 'notify' and self.script(self.count + 100):
            raise RuntimeError('finish failed')
    def snapshot(self):
        LOG.append(('perf.snapshot',))
        if self.script(self.count + 7):
            raise ValueError('snapshot failed')
        return {'poll_calls': self.count}

class Generation(object):
    """A generation whose equality answers a non-bool value."""
    def __init__(self, value):
        self.value = value
    def __eq__(self, other):
        return [] if other != self.value else ['same']
    def __ne__(self, other):
        return other != self.value
    def __repr__(self):
        return 'Generation(%r)' % self.value

def build(impl, seed):
    rng = random.Random(seed)
    base = Pinned if impl == 'pinned' else Native
    class Client(base):
        pass
    client = Client()
    client._transport_performance = Performance(lambda n: rng.random() < 0.1)
    client._transport_generation = 1
    client._stopping = rng.random() < 0.05
    client._poll_callback = 'cb'
    client._pending_lock = threading.Lock()
    client._pending = []
    client.connected = rng.random() < 0.8
    client._last_ping = 0.0
    client._ping_seq = 3
    client.round_id = rng.choice([None, 4])
    client.authority_epoch = 2
    client._worker_ping_scope = rng.choice([None, (4, 2)])
    client.worker_rtt_ms = client.worker_frame_ms = 5.0
    client._worker_ping_started = rng.choice([None, 1.5])
    client._worker_pong_time = 2.0
    client.player_id = rng.choice([4, 999])
    client.last_error = None
    client.running = rng.random() < 0.9
    client.phase = rng.choice(['battle', 'waiting'])
    client._snapshot_accepted_time = rng.choice([None, 0.0, 9.0])
    client._snapshot_stall_log = rng.choice([None, 0.0])
    client._snapshot_drop_streak = rng.choice([0, 2.7])
    client._snapshot_drop_reason = rng.choice([None, '', 'gap'])
    client.on_event = (lambda kind, message: LOG.append(('on_event', kind, repr(message)))) if rng.random() < 0.9 else None
    def drain(generation):
        LOG.append(('drain', generation))
        for unused in range(rng.randint(0, 3)):
            client._pending.append(rng.choice([
                {'type': 'snapshot', 'n': rng.randint(0, 9)},
                {'type': 'events', '_client_received_time': rng.choice([4.0, 'x', float('nan'), 99.0])},
                {'type': 'bad'}]))
        if rng.random() < 0.05:
            client._transport_generation += 1
    client._drain_native_transport = drain
    def dispatch_pending(messages, generation):
        LOG.append(('dispatch_pending', len(messages), generation))
        for message in messages:
            result = client._dispatch_message(message, rng.choice([generation, None, generation + 1, Generation(generation)]))
            LOG.append(('dispatched', repr(result)))
            if not result:
                return
    client._dispatch_pending_messages = dispatch_pending
    def handle(message):
        LOG.append(('handle', repr(message)))
        if rng.random() < 0.05:
            raise KeyError('handler')
        if rng.random() < 0.05:
            client._stopping = True
        if rng.random() < 0.05:
            client._transport_generation += 1
        client._notify(rng.choice(['snapshot', None, 'events']), message, rng.choice([None, 'hint']))
    client._handle_message = handle
    client._ignore_runtime_payload = lambda *args: LOG.append(('ignore',) + args)
    client._send = lambda payload: LOG.append(('send', sorted(payload.items())))
    client._schedule_poll = lambda: LOG.append(('schedule',))
    def materialize(message):
        LOG.append(('materialize', repr(message)))
        if message.get('type') == 'bad':
            if rng.random() < 0.3:
                client._transport_generation += 1
            raise SnapshotDeltaError('bad')
        if rng.random() < 0.03:
            raise ValueError('other')
        return dict(message, materialized=True)
    snapshot_delta.materialize = materialize
    return client, rng

def run(impl, seed):
    del LOG[:]
    clock = [10.0]
    globals()['_monotonic_time'] = lambda: clock[0]
    client, rng = build(impl, seed)
    out = sys.stdout
    sys.stdout = types.ModuleType('stream')
    sys.stdout.write = lambda text: LOG.append(('stdout', text))
    try:
        for step in range(rng.randint(1, 12)):
            clock[0] += rng.choice([0.0, 0.5, 2.5, 7.0])
            if rng.random() < 0.15:
                client.last_error = 'boom'
            if rng.random() < 0.1:
                client._transport_performance.poll_depth = 1
            action = rng.choice(['_poll'] * 6 + ['transport_performance_snapshot', '_report_snapshot_stall'])
            try:
                result = (client._report_snapshot_stall(clock[0]) if action == '_report_snapshot_stall'
                          else getattr(client, action)())
                LOG.append((action, 'ok', repr(result)))
            except Exception as error:
                LOG.append((action, 'error', type(error).__name__))
            LOG.append(('state', client._poll_callback, client._ping_seq, client._last_ping, repr(client._worker_ping_scope),
                        client._worker_ping_started, client.last_error, client._snapshot_drop_streak,
                        client._snapshot_drop_reason, client._snapshot_stall_log, client._transport_performance.poll_depth,
                        client._transport_performance.handle_depth, client._transport_performance.valid, len(client._pending)))
    finally:
        sys.stdout = out
    return list(LOG)

cases = 0
for seed in range(int(sys.argv[3])):
    a, b = run('pinned', seed), run('native', seed)
    if a != b:
        for index, (x, y) in enumerate(zip(a, b)):
            if x != y:
                print('seed %d first difference at %d\npinned: %r\nnative: %r' % (seed, index, x, y))
                break
        else:
            print('seed %d length %d != %d' % (seed, len(a), len(b)))
        sys.exit(1)
    cases += len(a)
print('lan poll differential ok %d' % cases)
'''


def staged_source(fork):
    """The staged LANClient poll chain, as Pinned and as Native routed through the ports."""
    import importlib.util
    import sys
    sys.path.insert(0, str(ROOT / 'bigworld'))
    spec = importlib.util.spec_from_file_location('prepare_offline_fork', ROOT / 'bigworld/prepare_offline_fork.py')
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    prepare.client_lifecycle_bindings.lan_client = lambda source: source
    source = prepare.ADAPTERS[Path(MODULE)]((Path(fork) / MODULE).read_text())
    lines = source.splitlines()
    tree = ast.parse(source.replace('from __future__ import print_function\n', '', 1))
    offset = 1  # the removed future line
    helpers, methods = [], []
    for node in tree.body:
        named = ([getattr(t, 'id', None) for t in node.targets] if isinstance(node, ast.Assign) else
                 [node.name] if isinstance(node, ast.FunctionDef) else [])
        if any(name in GLOBALS for name in named):
            helpers.append('\n'.join(lines[node.lineno - 1 + offset:node.end_lineno + offset]))
        if isinstance(node, ast.ClassDef) and node.name == 'LANClient':
            for item in node.body:
                if isinstance(item, ast.FunctionDef) and item.name in METHODS:
                    methods.append('\n'.join(lines[item.lineno - 1 + offset:item.end_lineno + offset]))
    # Native mirrors the bindings: each method returns its port with the Python arguments and globals().
    native = ['    def %s:\n        return native.%s' % (definition, call.replace('globals()', '_G'))
              for definition, call in (
                  ('_poll(self)', 'lan_poll(self, globals())'),
                  ('_poll_messages(self)', 'lan_poll_messages(self, globals())'),
                  ('_take_ready_messages(self, generation)', 'lan_take_ready_messages(self, generation, globals())'),
                  ('_dispatch_message(self, message, generation=None)', 'lan_dispatch_message(self, message, generation, globals())'),
                  ('_notify(self, kind, message, replay_hint=None)', 'lan_notify(self, kind, message, replay_hint, globals())'),
                  ('_notify_message(self, kind, message, replay_hint)', 'lan_notify_message(self, kind, message, replay_hint, globals())'),
                  ('transport_performance_snapshot(self)', 'lan_performance_snapshot(self, globals())'),
                  ('_report_snapshot_stall(self, now)', 'lan_report_snapshot_stall(self, now, globals())'))]
    assert len(native) == len(prepare.client_lifecycle_bindings.LAN_POLL_PORTS)
    return ('from __future__ import print_function\nimport math\n' + '\n\n'.join(helpers) +
            '\n\nclass Pinned(object):\n' + '\n\n'.join(methods) +
            '\n\nclass Native(Pinned):\n' + '\n\n'.join(native) + '\n\n_G = globals()\n')


class LanPollDifferential(unittest.TestCase):
    def test_native_poll_chain_matches_staged_methods(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='lan-poll-') as folder:
            folder = Path(folder)
            subprocess.run(['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-register',
                            '-Wno-missing-field-initializers', '-bundle', '-undefined', 'dynamic_lookup',
                            '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                            str(ROOT / 'tests/stock_native_client_lifecycle_host.cpp'),
                            '-o', str(folder / 'offline_client_lifecycle_native.so')], check=True)
            (folder / 'staged.py').write_text(staged_source(fork))
            (folder / 'fixture.py').write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder), str(folder / 'staged.py'), '600'],
                                 capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr[-6000:])
            self.assertIn('lan poll differential ok', run.stdout)
            print(run.stdout)


if __name__ == '__main__':
    unittest.main()
