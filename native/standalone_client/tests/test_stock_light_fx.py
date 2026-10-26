"""BigWorld.WGLightFx: real CPython 2.7 binding and the exact #1513 consumer.

The host records LightFX.dll calls; it proves binding and control flow only.
Alienware LightFX hardware behaviour needs a Windows machine with that runtime.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_bigworld_ipc import ROOT, build_host


class StockLightFxTest(unittest.TestCase):
    def test_real_cpython_binding_and_exact_light_manager(self):
        with tempfile.TemporaryDirectory(prefix='light-fx-cpython-') as directory:
            path = Path(directory)
            (path / 'include/math').mkdir(parents=True)
            (path / 'include/math/vector4.hpp').write_text('#pragma once\n')
            binary = build_host(self, path, ROOT / 'tests/stock_light_fx_host.cpp')
            environment = dict(os.environ)
            client = environment.get('WOT_0922_CLIENT')
            if client:
                with zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg') as archive:
                    pyc = path / 'LightManager.pyc'
                    pyc.write_bytes(archive.read('scripts/client/LightFx/LightManager.pyc'))
                environment['LIGHT_MANAGER_PYC'] = str(pyc)
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_light_fx_consumer.py')],
                                    env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
