"""Prepared replay consumers use native owners without a legacy process package."""
import ast
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
from prepare_offline_fork import NATIVE_MODULE_ROOT, RUNTIME_ROOT, stage_runtime


FIXTURE = r'''
import os, sys, types
root = sys.argv[1]
for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    module = types.ModuleType(name)
    module.__path__ = [os.path.join(root, *name.split('.'))]
    sys.modules[name] = module
for name in ('offline_client_wire_native', 'offline_client_lifecycle_native'):
    sys.modules[name] = types.ModuleType(name)
name = 'gui.mods.offline_lan_0922.lan_client'
lan = types.ModuleType(name)
class LANClient(object):
    def __init__(self, *args, **kwargs):
        assert not kwargs
        self.running = False
        self.bigworld = None
lan.LANClient = LANClient
sys.modules[name] = lan
name = 'gui.mods.offline_lan_0922.replay_resources'
resources = types.ModuleType(name)
resources.verify = lambda signature, root: False
sys.modules[name] = resources
setattr(sys.modules['gui.mods.offline_lan_0922'], 'replay_resources', resources)
from gui.mods.offline_lan_0922 import native_replay, offline_replay, replay_transport

class Reader(object):
    def __init__(self, path):
        self.path, self.closed, self.reads = path, 0, 0
        self.header = {'config': {}, 'start': {'round_id': 9, 'players': [{'id': 1}], 'bots': [{'id': 2}]},
                       'snapshot': {'round_id': 9, 'players': [{'id': 1}], 'bots': [{'id': 2}]}}
        self.rows = [native_replay.Pending(), {'type': 'wire', 't': 0., 'data': {'type': 'events'}},
                     {'type': 'local', 't': 1., 'data': {}}, {'type': 'end', 't': 2.}]
    def next(self):
        self.reads += 1
        row = self.rows.pop(0)
        if isinstance(row, Exception):
            raise row
        return row
    def close(self):
        self.closed += 1

# The real staged constructor selects NativeReader by default; no retired
# module is imported. Explicit async injection keeps the same lookahead barrier.
native_replay.NativeReader = Reader
client = replay_transport.ReplayClient(replay_path='default.wotlanreplay')
assert isinstance(client.reader, Reader) and client.reader.path == 'default.wotlanreplay'
injected = []
def reader_factory(path):
    injected.append(Reader(path))
    return injected[-1]
custom = replay_transport.ReplayClient(replay_path='custom.wotlanreplay', replay_reader_factory=reader_factory)
assert custom.reader is injected[0]
custom._prefetch(0.)
assert not custom._replay_queue and custom.reader.reads == 1
custom._prefetch(0.)
assert [row['type'] for row in custom._replay_queue] == ['wire', 'local']
reads = custom.reader.reads
custom._prefetch(0.)
assert custom.reader.reads == reads
custom._replay_queue.clear()
custom._prefetch(1.)
assert custom._replay_eof_read and [row['type'] for row in custom._replay_queue] == ['end']
for item in (client, custom):
    item.stop(); item.stop()
    assert item.reader.closed == 1

# Native recording still publishes atomically, and custom asynchronous owners
# may finish via the existing foreground callback drain after battle teardown.
target = os.path.join(sys.argv[2], 'published.wotlanreplay')
part = target + '.part'
open(target, 'wb').write(b'old')
open(part, 'wb').write(b'new')
offline_replay._publish(part, target)
assert open(target, 'rb').read() == b'new' and not os.path.exists(part)
callbacks = []
engine = types.ModuleType('BigWorld')
engine.callback = lambda delay, callback: callbacks.append((delay, callback))
sys.modules['BigWorld'] = engine
class Finishing(object):
    done = False
    def __init__(self): self.drained = 0
    def poll_messages(self):
        self.drained += 1
        return []
    def stats(self): return {'closed': True}
owner = Finishing()
offline_replay._FINISHING.append(owner)
offline_replay._schedule_finishing(); offline_replay._schedule_finishing()
assert len(callbacks) == 1 and callbacks[0][0] == .25
callbacks.pop()[1]()
assert owner.drained == 1 and len(callbacks) == 1 and owner in offline_replay._FINISHING
owner.done = True
callbacks.pop()[1]()
assert owner.drained == 2 and not callbacks and not offline_replay._FINISHING
assert not offline_replay._FINAL_POLL_SCHEDULED
assert not any(name.endswith(('.replay_process', '.replay_reader', '.stock_replay_codec')) for name in sys.modules)
print('native replay preparation lifecycle passed')
'''


class NativeReplayPreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            raise unittest.SkipTest('The fixed v0.10.1 source input is required')
        cls.source = Path(source)
        cls.temporary = tempfile.TemporaryDirectory(prefix='native-replay-preparation-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.output = Path(cls.temporary.name)
        stage_runtime(Path(source), cls.output)
        cls.runtime = cls.output / RUNTIME_ROOT
        cls.package = cls.runtime / NATIVE_MODULE_ROOT

    def test_staged_consumers_have_no_process_implementation_or_package(self):
        source = (self.package / 'offline_replay.py').read_text()
        declarations = {node.name for node in ast.parse(source).body
                        if isinstance(node, (ast.ClassDef, ast.FunctionDef))}
        self.assertFalse(declarations.intersection(('Recorder', 'Reader', '_json_line')))
        self.assertTrue({'_publish', '_drain_process', '_schedule_finishing', '_poll_finishing'} <= declarations)
        for name in ('replay_process.py', 'replay_reader.py', 'stock_replay_codec.py'):
            self.assertFalse((self.package / name).exists())
        for path in self.runtime.rglob('*.py'):
            text = path.read_text()
            for retired in ('ProcessRecorder', 'ProcessReader', 'prepare_runtime(', '_replay_plain',
                            'recorder-runtime.zip'):
                self.assertNotIn(retired, text, str(path))
        self.assertFalse(list(self.output.rglob('*.wotmod')))
        self.assertFalse(list(self.output.rglob('recorder-runtime.zip')))
        builder = (ROOT.parents[1] / 'tools/build_bigworld_client.ps1').read_text()
        self.assertNotIn('offline-fork-release.zip', builder)
        self.assertNotIn('/releases/download/v0.10.1/', builder)

    def test_legacy_declaration_removal_rejects_missing_or_duplicate_owners(self):
        from stock_native_client_wire_bindings import offline_replay
        original = (self.source / 'src/res/scripts/client/gui/mods/offline_lan_0922/offline_replay.py').read_text()
        for name, prefix in (('Recorder', 'class '), ('Reader', 'class '), ('_json_line', 'def ')):
            with self.subTest(name=name, variant='missing'):
                changed = original.replace(prefix + name + '(', prefix + 'changed_' + name + '(', 1)
                with self.assertRaisesRegex(ValueError, 'legacy replay declaration changed'):
                    offline_replay(changed)
            with self.subTest(name=name, variant='duplicate'):
                changed = original + '\n' + prefix + name + '(object):\n    pass\n'
                with self.assertRaisesRegex(ValueError, 'legacy replay declaration changed'):
                    offline_replay(changed)

    def test_python27_default_injected_reader_and_finishing_lifecycle(self):
        py27 = os.environ.get('WOT_TEST_PY27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7')
        if not Path(py27).is_file():
            self.skipTest('CPython 2.7 audit runtime is required')
        fixture = self.output / 'fixture.py'
        fixture.write_text(FIXTURE)
        completed = subprocess.run([py27, '-B', str(fixture), str(self.runtime / 'scripts/client'),
                                    str(self.output)], text=True, capture_output=True, timeout=30)
        self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
        self.assertIn('native replay preparation lifecycle passed', completed.stdout)


if __name__ == '__main__':
    unittest.main()
