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


if __name__ == '__main__':
    unittest.main()
