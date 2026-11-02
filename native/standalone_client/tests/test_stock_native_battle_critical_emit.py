"""The real repair-edge emitter waits until the stock HUD accepts an edge."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CriticalEmitTests(unittest.TestCase):
    def test_repair_notification_retries_until_accepted(self):
        compiler = os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        source = (ROOT / 'bigworld/stock_native_battle_critical_local.cpp').read_text()
        start = source.index('void emit(Battle& b, LocalCriticalOwner& o) {')
        end = source.index('\nstruct Busy {', start)
        method = source[start:end]
        host = (ROOT / 'tests/stock_native_battle_critical_emit_host.cpp').read_text()
        old = method.replace(
            'if (!vehicles::damageInfoServiceable(b) || !vehicles::showDamageInfo(b, id, edge.index, edge.extra, id)) continue;',
            'if (vehicles::damageInfoServiceable(b)) vehicles::showDamageInfo(b, id, edge.index, edge.extra, id);')
        self.assertNotEqual(old, method)
        with tempfile.TemporaryDirectory(prefix='critical-emit-') as directory:
            folder = Path(directory)
            for name, body, expected in (('current', method, 0), ('lost-edge', old, 1)):
                cpp = folder / (name + '.cpp')
                binary = folder / name
                cpp.write_text(host.replace('@EMIT@', body))
                built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                        str(cpp), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, expected == 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
