"""Native battle owners retire before engine/Python teardown, even on cleanup failure."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get("WOT_FORK_0101_SOURCE")


@unittest.skipUnless(SOURCE, "set WOT_FORK_0101_SOURCE to the fixed gameplay checkout")
class BattleShutdownTests(unittest.TestCase):
    def test_worker_link_closes_and_releases_owners_even_when_leave_fails(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='battle-link-shutdown-') as directory:
            target = Path(directory) / 'battle_link_shutdown_test.so'
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                       '-Wl,-exported_symbol,_initbattle_link_shutdown_test', '-isystem', str(Path(prefix)/'include/python2.7'),
                       '-I'+str(ROOT/'bigworld'), '-I'+str(ROOT.parent), '-I'+str(Path(SOURCE)/'native'),
                       str(ROOT/'tests/stock_native_battle_link_shutdown_host.cpp'),
                       str(ROOT/'bigworld/stock_native_client_state.cpp'), '-o', str(target)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            script = '''import battle_link_shutdown_test as native
class World(object):
 def cancelCallback(self,token):
  assert token==42
  raise RuntimeError('callback already retired')
for fail in (0,1):
 rows=native.run(World(),fail)
 assert rows==(['leave','transport-destroy'] if fail else ['leave','close','transport-destroy']),rows
print('Worker-link detach, callback failure, failed leave, transport-before-script release and repeated shutdown passed')
'''
            result = subprocess.run([py27, '-B', '-c', script], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_live_handles_are_released_after_partial_and_failed_startup(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='battle-shutdown-') as directory:
            target = Path(directory) / 'battle_shutdown_test.so'
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                       '-Wl,-exported_symbol,_initbattle_shutdown_test', '-isystem', str(Path(prefix)/'include/python2.7'),
                       '-I'+str(ROOT/'bigworld'), '-I'+str(ROOT.parent), '-I'+str(Path(SOURCE)/'native'),
                       str(ROOT/'tests/stock_native_battle_shutdown_host.cpp'),
                       str(ROOT/'bigworld/stock_native_battle_startup_module.cpp'), str(ROOT/'bigworld/stock_native_client_state.cpp'), '-o', str(target)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            script = '''import battle_shutdown_test as native
for fail in (0, 1):
 rows = native.run(object(), fail)
 assert rows == ['worker','stop','link-release','hooks','presentation','destroy','worker-release','stock-release',
                 'worker','stop','link-release','presentation','worker-release','stock-release'], rows
print('Live-owner release, failed cleanup and repeated shutdown passed')
'''
            result = subprocess.run([py27, '-B', '-c', script], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_shutdown_precedes_all_engine_tasks(self):
        module = (ROOT/'bigworld/stock_native_battle_startup_module.cpp').read_text()
        self.assertIn('"NativeBattleLifetime", ">Finale", NULL', module)
        self.assertIn('void fini() override { native_battle::startup::shutdown(); }', module)


if __name__ == '__main__':
    unittest.main()
