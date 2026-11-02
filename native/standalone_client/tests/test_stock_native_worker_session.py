"""Consecutive worker rounds retain native owners until Account restore finishes."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FORK = os.environ.get('WOT_FORK_0101_SOURCE')


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


FIXTURE = r'''
import sys, types
import worker_session_test as native
for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    module = types.ModuleType(name); module.__path__ = []; sys.modules[name] = module
config = types.ModuleType('gui.mods.offline_lan_0922.config')
config.CONFIG_PATH = sys.argv[1] + '/config.json'
sys.modules[config.__name__] = config
gc = types.ModuleType('gui.mods.offline_lan_0922.gc_sweep')
gc.sweep = lambda *args: None
sys.modules[gc.__name__] = gc

class World(object):
    def __init__(self):
        self.ready = True
        self.callbacks = {}
        self.next_id = 0
        self.draw = True
        self.restore_hook = None
        self.restores = 0
    def callback(self, delay, fn):
        self.next_id += 1
        self.callbacks[self.next_id] = (delay, fn)
        return self.next_id
    def cancelCallback(self, token):
        self.callbacks.pop(token, None)
    def worldDrawEnabled(self, *args):
        if args: self.draw = args[0]
        return self.draw
    def restoreLobbyAccount(self):
        self.restores += 1
        self.ready = True
        if self.restore_hook: self.restore_hook()
    def disconnect(self):
        raise AssertionError('unexpected native Account disconnect')
    def restore(self):
        rows = [(key, fn) for key, (delay, fn) in self.callbacks.items() if delay == 0.]
        assert len(rows) == 1, self.callbacks
        key, fn = rows[0]
        del self.callbacks[key]
        fn()
        return fn
    def monitor(self):
        rows = [(key, fn) for key, (delay, fn) in self.callbacks.items() if delay == .1]
        assert len(rows) == 1, self.callbacks
        key, fn = rows[0]
        del self.callbacks[key]
        fn()

def barrier(round_id):
    return dict(round_id=round_id, map='33_fjord', players=[dict(
        id=-1, x=0., y=-200., z=0., yaw=0., vehicle='ussr:R11_MS-1', name='Worker')])
def start(round_id):
    native.event('battle_start', barrier(round_id))
def waiting(round_id):
    native.event('roster', dict(round_id=round_id, phase='waiting'))
def fresh(timeout=30.):
    world = World()
    assert native.initialise(world, lambda: world.ready, dict(startupTimeoutSeconds=timeout))
    start(1)
    native.phase('running')
    world.ready = False
    return world
def healthy(starts):
    state = native.details()
    assert state['linked'] and not state['drops'] and not state['disconnects'], state
    assert state['starts'] == starts, state
    return state

# The normal waiting barrier retires round 1, but the native Battle remains
# installed before and after the real generation-fenced restore callback.
for order in ('start_before_restore', 'start_after_restore', 'lobby_ready_before_restore',
              'start_before_waiting', 'restore_reentrant_start'):
    world = fresh()
    if order == 'start_before_waiting':
        start(2)
        waiting(1) # An old waiting update cannot discard the admitted next start.
    else:
        waiting(1)
    state = healthy(1)
    assert state['round'] == 1 and state['active'] is None and state['restore_pending'], state
    assert state['cleanups'] == 1, state
    # Same/older starts cannot resurrect the completed native owner.
    start(1); start(0); waiting(1)
    assert healthy(1)['cleanups'] == 1
    if order in ('start_before_restore', 'lobby_ready_before_restore'):
        if order == 'lobby_ready_before_restore': world.ready = True
        start(2)
    if order == 'restore_reentrant_start':
        def reenter():
            assert native.details()['restore_pending']
            start(2)
            assert healthy(1)['pending'] == 2
            world.monitor()
            assert healthy(1)['starts'] == 1
        world.restore_hook = reenter
    if native.details()['pending'] == 2:
        deadline = native.details()['deadline']
        start(2); waiting(1); world.monitor()
        assert healthy(1)['pending'] == 2 and native.details()['deadline'] == deadline
    stale_restore = world.restore()
    assert not native.details()['restore_pending']
    assert native.details()['retained_owners'] == 0
    if order == 'start_after_restore': start(2)
    elif native.details()['active'] is None: world.monitor()
    state = healthy(2)
    assert state['round'] == 2 and state['active'] == 2 and state['pending'] is None, state
    assert world.restores == 1
    # A stale generation callback, old barrier and old payload cannot reach round 2.
    stale_restore(); start(1); start(2); waiting(1)
    native.event('snapshot', dict(round_id=1))
    assert healthy(2)['received'] == 0
    native.event('snapshot', dict(round_id=2))
    assert healthy(2)['received'] == 1
    assert world.restores == 1

# A duplicate active barrier is ignored; a new round while this one is still
# loading is a real conflict and must not replace its native space.
world = fresh()
native.phase('loading')
start(1); start(0)
assert healthy(1)['round'] == 1
start(2)
state = native.details()
assert state['starts'] == 1 and state['disconnects'] == 1 and state['drops'] > 0, state

# Two different admitted starts cannot silently replace one pending load.
world = fresh(); waiting(1); start(2); start(3)
state = native.details()
assert state['starts'] == 1 and state['disconnects'] == 1, state

# A retained native Battle cannot suppress the existing pending-start timeout.
world = fresh(timeout=0.); waiting(1); start(2); world.monitor()
state = native.details()
assert state['starts'] == 1 and state['disconnects'] == 1, state

# The real failure-completion report synchronously re-enters worker::failure
# and stopRound. Its restore flag must still prevent a second native cleanup.
world = fresh()
native.fail()
assert native.details()['restore_pending'] and native.details()['cleanups'] == 1
world.restore()
state = native.details()
assert world.restores == 1 and state['cleanups'] == 1 and not state['restore_pending'], state
assert not any(delay == 0. for delay, unused in world.callbacks.values()), world.callbacks
native.shutdown()
print('Consecutive production worker rounds, native restore callbacks and identity fences passed')
'''


class NativeWorkerSession(unittest.TestCase):
    def test_consecutive_rounds_and_restore_interleavings(self):
        if not FORK:
            self.skipTest('Pinned gameplay native headers are required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        source = (ROOT / 'bigworld/stock_native_battle_startup.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='worker-session-') as directory:
            folder = Path(directory)
            (folder / 'worker_session_startup.inc').write_text('\n\n'.join(function(source, name) for name in (
                'void stop(Battle& b, bool restore_account)',
                'void teardownAfterFailure(Battle& b)',
                'void reportFailure(Battle& b, bool lobby_restored)',
                'void scheduleLobbyRestore(Battle& b, bool report_failure, bool)')))
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD',
                       '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined',
                       'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(folder), '-I' + str(ROOT / 'bigworld'), '-I' + str(ROOT.parent),
                       '-I' + str(Path(FORK) / 'native'),
                       str(ROOT / 'tests/stock_native_worker_session_host.cpp'),
                       str(ROOT / 'bigworld/stock_native_client_state.cpp'),
                       '-o', str(folder / 'worker_session_test.so')]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(0, compiled.returncode, compiled.stdout + compiled.stderr)
            script = folder / 'fixture.py'
            script.write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(script), str(folder)], cwd=folder,
                                 capture_output=True, text=True)
            self.assertEqual(0, run.returncode, run.stdout + run.stderr)
            self.assertIn('Consecutive production worker rounds', run.stdout)


if __name__ == '__main__':
    unittest.main()
