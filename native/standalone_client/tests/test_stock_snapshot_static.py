"""Run snapshot ownership against the pinned payload in both Python runtimes."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]

NATIVE_FIXTURE = '''
import sys
sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[2])
from gui.mods.offline_lan_0922 import stock_snapshot_static as static
import static_snapshot_native as native
params = static.freeze({'physics': {'mass': 5730., 'terrainResist': [1., 2., 3.]},
                        'loadout': {'reload_factor': .96}, 'ramming': {'ramming_bonus': 0.},
                        'crew': {'members': [1]}, 'gun': {'source_shot': {'deadeye': False}}})
outfits = static.freeze({'1': 'YWJj'})
row = dict(id=1, effective_params=params, outfits=outfits, pose={'x': 1.})
owner = native.capture([row])
assert native.versions(owner) == 2
first = native.read(owner)
assert first == [row]
# The real C++ reader owns dynamic fields and shares immutable static versions.
row['pose']['x'] = 2.
assert native.read(owner)[0]['pose']['x'] == 1.
native.refresh(owner, [row])
assert native.versions(owner) == 2
assert native.read(owner)[0]['pose']['x'] == 2.
# Every Python result is detached from the accepted native snapshot and source.
first[0]['effective_params']['physics']['mass'] = 1.
first[0]['effective_params']['physics']['terrainResist'].append(4.)
first[0]['effective_params']['loadout']['reload_factor'] = 2.
first[0]['effective_params']['ramming']['ramming_bonus'] = .1
first[0]['effective_params']['crew']['members'].append(2)
first[0]['effective_params']['gun']['source_shot']['deadeye'] = True
first[0]['outfits']['1'] = 'changed'
assert native.read(owner)[0]['effective_params'] == params
assert native.read(owner)[0]['outfits'] == outfits
assert params['physics']['mass'] == 5730.
changed = static.detach(params)
changed['physics']['mass'] = 6000.
row['effective_params'] = static.freeze(changed)
native.refresh(owner, [row])
assert native.versions(owner) == 3
assert native.read(owner)[0]['effective_params']['physics']['mass'] == 6000.
assert params['physics']['mass'] == 5730.
print('Native replica static ownership passed')
'''


class SnapshotStaticTests(unittest.TestCase):
    def test_pinned_snapshot_ownership_in_python2_and_python3(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        spec = importlib.util.spec_from_file_location('snapshot_prepare',
            ROOT / 'bigworld/prepare_offline_fork.py')
        prepare = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(prepare)
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
            env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        with tempfile.TemporaryDirectory(prefix='stock-snapshot-static-') as temporary:
            staged = Path(temporary)
            prepare.stage_runtime(Path(source), staged)
            target = staged / 'offline-res/scripts/client'
            package = target / 'gui/mods/offline_lan_0922'
            for name in ('snapshot_delta', 'lan_client'):
                original = Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922' / (name + '.py')
                (package / ('_stock_snapshot_original_' + name + '.py')).write_text(original.read_text(), encoding='utf-8')
                # This fixture exercises the immutable Python value contract
                # independently of the native decoder tested by its CAPI host.
                adapter = prepare.snapshot_bindings.delta if name == 'snapshot_delta' else prepare.snapshot_bindings.client
                (package / (name + '.py')).write_text(adapter(original.read_text()), encoding='utf-8')
            for name in ('gui/__init__.py', 'gui/mods/__init__.py'):
                path = target / name
                if not path.exists():
                    path.write_text('')
            for executable in (sys.executable, py27):
                run = subprocess.run([executable, '-B',
                    str(ROOT / 'tests/stock_snapshot_static_fixture.py'), str(target),
                    str(REPO / 'tests')], text=True, capture_output=True,
                    env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                self.assertIn('recursive_comparisons=', run.stdout)
                print(executable + ': ' + run.stdout.strip())
            # The battle shell no longer contains the retired Python gameplay
            # methods. Exercise its actual C++ static-version consumer instead.
            replica = (ROOT / 'bigworld/stock_native_battle_remote_replica.cpp').read_text(encoding='utf-8')
            readers = replica[replica.index('Value readValue('):replica.index('Pose blend(')]
            host = (ROOT / 'tests/stock_snapshot_native_host.cpp').read_text(encoding='utf-8')
            host = host.replace('// PRODUCTION_STATIC_READERS', readers)
            (staged / 'host.cpp').write_text(host, encoding='utf-8')
            prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
            link = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-register',
                       '-Wno-unused-parameter', '-DWOT_CLIENT_REPLICA_HOST_BUILD'] + link + [
                       '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(ROOT / 'bigworld'), str(staged / 'host.cpp'),
                       str(ROOT / 'bigworld/stock_native_client_state.cpp'),
                       '-o', str(staged / 'static_snapshot_native.so')]
            compiled = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            run = subprocess.run([py27, '-B', '-c', NATIVE_FIXTURE, str(target), str(staged)],
                                 text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('Native replica static ownership passed', run.stdout)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
