"""Fixed-stage timer accounting; no game, Python bridge or sleep is needed."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


class RotationStageTimingTests(unittest.TestCase):
    def test_bounded_stage_accounting(self):
        cxx = os.environ.get('CXX', 'c++')
        if not shutil.which(cxx):
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='rotation-stage-timing-') as folder:
            binary = Path(folder)/'host'
            subprocess.run([cxx,*native_test_compile_flags(cxx),'-std=c++17','-O2',
                '-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),
                str(ROOT/'tests/stock_rotation_stage_timing_host.cpp'),
                str(ROOT/'bigworld/stock_rotation_stage_timing.cpp'),'-o',str(binary)],check=True)
            result = subprocess.run([str(binary)],text=True,capture_output=True,
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout.strip())
