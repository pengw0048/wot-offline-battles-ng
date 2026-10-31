"""On the Bot authority, SnapshotSync's Bot interpolation is gated off without changing what the runtime consumes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = 'src/res/scripts/client/gui/mods/offline_lan_0922/snapshot_sync.py'

FIXTURE = r'''
import copy, math, random, sys, types
for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    module = types.ModuleType(name)
    module.__path__ = []
    sys.modules[name] = module
replica = types.ModuleType('gui.mods.offline_lan_0922.native_client_replica')
replica.receive = lambda sync, message, manifest=False: None
replica.advance = lambda sync, now: None
replica.probes = lambda sync, now: None
sys.modules[replica.__name__] = replica
sys.modules['gui.mods.offline_lan_0922'].native_client_replica = replica

def load(path, name):
    module = types.ModuleType('gui.mods.offline_lan_0922.' + name)
    module.__package__ = 'gui.mods.offline_lan_0922'
    exec(compile(open(path).read(), path, 'exec'), module.__dict__)
    return module
# The pinned engine-free helpers it imports come from the pinned package itself.
sys.modules['gui.mods.offline_lan_0922'].__path__ = [sys.argv[4]]
pinned = load(sys.argv[1], 'pinned_sync')
gated = load(sys.argv[2], 'gated_sync')

class Consumer(object):
    """BattleRuntime._apply_sync_event, _queue_bot_create and the authority no-op paths."""
    def __init__(self, authority):
        self.authority = authority
        self.records = set()
        self.pending = {}
        self.effects = []
    def __call__(self, event):
        kind = event.get('type')
        if kind == 'create':
            if event.get('kind') == 'bot':
                self.queue(event)
            else:
                self.effects.append(('create_remote', event['entity'], sorted(event['state'].items())))
        elif kind == 'update':
            if event.get('kind') == 'bot' and event.get('entity') not in self.records:
                self.queue(event)
                return
            if event.get('kind') == 'bot' and self.authority:
                event = dict(event)
                event.pop('pose', None)
            if event.get('interpolated') and not (event.get('state') or {}) and event.get('pose') is None:
                return
            self.effects.append(('update', event['entity'], sorted((event.get('state') or {}).items()),
                                 sorted((event.get('pose') or {}).items())))
        else:
            self.effects.append((kind, sorted((k, repr(v)) for k, v in event.items())))
    def queue(self, event):
        key = event.get('entity')
        if not key or key in self.records:
            return
        state = dict(event.get('state') or {})
        pose = event.get('pose')
        if isinstance(pose, dict):
            for name in ('x', 'y', 'z', 'yaw', 'pitch', 'roll', 'aim_yaw', 'turret_yaw', 'gun_pitch', 'siege_state'):
                if name in pose:
                    state[name] = pose[name]
        if key in self.pending:
            self.pending[key].update(state)
        else:
            self.pending[key] = state
    def create_pending(self):
        for key in sorted(self.pending):
            self.effects.append(('create_bot', key, sorted(self.pending[key].items())))
            self.records.add(key)
        self.pending = {}

def bot(index, step, moving, alive=True, extra=None):
    row = {'id': index, 'x': 10.0 * index + (0.5 * step if moving else 0.0), 'y': 1.0, 'z': -3.0 * index,
           'yaw': 0.25 * index, 'alive': alive, 'health': 100 - step}
    row.update(extra or {})
    return row

def run(module, seed, authority, set_attribute):
    rng = random.Random(seed)
    consumer = Consumer(authority)
    clock = [100.0]
    sync = module.SnapshotSync(7, on_event=consumer, clock=lambda: clock[0])
    if set_attribute:
        sync._authority_bots = lambda: authority
    created_at = rng.randint(1, 4)
    # Production starts Bot motion only once the whole roster exists (battle_ready gate).
    moving_from = created_at + 1 + rng.randint(0, 3)
    bot_extra = rng.choice([None, {'aim_yaw': 0.1, 'pitch': 0.02, 'gun_pitch': -0.05}, {'roll': float('nan')}])
    for step in range(rng.randint(8, 30)):
        clock[0] += rng.choice([0.016, 0.033, 0.05])
        message = {'type': 'snapshot', 'round_id': 1, 'sequence': step,
                   'bot_state_revision': step, 'motion_time_us': 1000000 + step * 33000,
                   'bot_state_time_us': 1000000 + step * 33000 - rng.choice([0, 5000]),
                   'timing': {'phase': 'prebattle' if step < moving_from else 'battle'},
                   '_client_dispatch_delay': rng.choice([0.0, 0.004]),
                   'players': [{'id': 7, 'x': 1.0 + step, 'y': 0.0, 'z': 0.0}, {'id': 8, 'x': 5.0, 'y': 0.0, 'z': step * 0.1}],
                   'bots': [bot(index, step, step >= moving_from,
                                alive=not (index == 2 and step > moving_from + 3), extra=bot_extra)
                            for index in (1, 2, 3) if not (index == 3 and step > moving_from + 5)]}
        if step == moving_from:
            message['bot_orders'] = [{'bot': 1, 'order': 'attack'}]
            message['bot_order_revision'] = 3
        sync.snapshot(message)
        for unused in range(rng.randint(1, 3)):
            clock[0] += 0.016
            sync.advance(clock[0])
        if step == created_at:
            consumer.create_pending()
    return consumer.effects, sync

cases = 0
for seed in range(int(sys.argv[3])):
    for authority in (False, True):
        expected, reference = run(pinned, seed, authority, False)
        actual, sync = run(gated, seed, authority, True)
        if repr(expected) != repr(actual):
            for index, (a, b) in enumerate(zip(expected, actual)):
                if a != b:
                    print('seed %d authority %s first difference at %d\npinned: %r\ngated: %r' % (seed, authority, index, a, b))
                    break
            else:
                print('seed %d authority %s length %d != %d' % (seed, authority, len(expected), len(actual)))
            sys.exit(1)
        if not authority:
            assert repr(sorted(reference._entities.items())) == repr(sorted(sync._entities.items())), seed
        else:
            for key, record in sync._entities.items():
                if record['kind'] == 'bot':
                    # A wreck keeps the pinned chase of its shoved pose; only live Bots skip timing.
                    assert record['dead'] or (record['target'] is None and record['current'] is None), (seed, key)
                    assert not record['timed_samples'], (seed, key)
        # Without the attribute the gated module is the pinned module.
        assert repr(run(gated, seed, authority, False)[0]) == repr(expected), seed
        cases += len(expected)
print('authority bot sync ok %d' % cases)
'''


def sources(fork):
    import importlib.util
    import sys
    sys.path.insert(0, str(ROOT / 'bigworld'))
    spec = importlib.util.spec_from_file_location('prepare_offline_fork', ROOT / 'bigworld/prepare_offline_fork.py')
    prepare = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prepare)
    pinned = (Path(fork) / MODULE).read_text()
    return pinned, prepare.ADAPTERS[Path(MODULE)](pinned)


class WorkerAuthorityBotSync(unittest.TestCase):
    def test_gate_preserves_consumed_events(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                       env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        pinned, gated = sources(fork)
        with tempfile.TemporaryDirectory(prefix='authority-bot-sync-') as folder:
            folder = Path(folder)
            (folder / 'pinned.py').write_text(pinned)
            (folder / 'gated.py').write_text(gated)
            (folder / 'fixture.py').write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder / 'pinned.py'), str(folder / 'gated.py'), '300',
                                  str((Path(fork) / MODULE).parent)],
                                 capture_output=True, text=True, timeout=300)
            self.assertEqual(run.returncode, 0, run.stdout[-4000:] + run.stderr[-6000:])
            self.assertIn('authority bot sync ok', run.stdout)
            print(run.stdout)


if __name__ == '__main__':
    unittest.main()
