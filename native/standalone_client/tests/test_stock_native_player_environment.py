"""Exercise player water observations and callback-free entity ingress."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class EnvironmentTests(unittest.TestCase):
    def test_cadence_and_hull_geometry(self):
        with tempfile.TemporaryDirectory(prefix='native-player-environment-') as folder:
            binary = Path(folder) / 'environment'
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                       str(ROOT / 'tests/test_stock_native_player_environment.cpp'),
                       str(ROOT / 'bigworld/stock_native_player_environment.cpp'),
                       str(ROOT / 'bigworld/stock_native_bot_combat.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_python_boundary_and_resource_lifecycle(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source or not shutil.which('pyenv'):
            self.skipTest('Fixed gameplay source and CPython 2.7.18 are required')
        python = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
            env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        includes = shlex.split(subprocess.check_output([python + '-config', '--includes'], text=True))
        linking = shlex.split(subprocess.check_output([python + '-config', '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='native-player-ingress-') as folder:
            for name, units in (
                    ('stock_native_bot_resource_lifecycle', ['stock_native_bot_resource_lifecycle']),
                    ('stock_native_player_environment_python', ['stock_native_player_environment',
                        'stock_native_player_environment_python', 'stock_native_bot_descriptor', 'stock_native_bot_resource_lifecycle',
                        'stock_native_bot_combat'])):
                binary = Path(folder) / name
                command = ['c++', '-std=c++17', '-Wno-register',
                           '-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD', *includes,
                           '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(source) / 'native'),
                           str(ROOT / ('tests/test_' + name + '.cpp'))]
                command += [str(ROOT / ('bigworld/' + unit + '.cpp')) for unit in units]
                command += linking + ['-o', str(binary)]
                built = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                run = subprocess.run([str(binary)], capture_output=True, text=True,
                    env=dict(os.environ, WOT_GAMEPLAY_SOURCE=source, PYTHONDONTWRITEBYTECODE='1'))
                self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
