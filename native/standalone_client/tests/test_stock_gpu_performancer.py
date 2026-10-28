"""Original hardware-score arithmetic, independent of future GPU draw integration."""
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GpuPerformancerTests(unittest.TestCase):
    def test_original_gpu_and_cpu_score_arithmetic(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client is required')
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        try:
            from stock_gpu_performancer_oracle import OriginalPerformanceScores
        except ImportError:
            self.skipTest('pefile and Unicorn are required')
        original = OriginalPerformanceScores(Path(client) / 'WorldOfTanks.exe')
        generator = random.Random(1513)
        gpu = [(j, r) for j in (-10., 0., 3., 4., 169., 170., 171., 1000.)
               for r in (0., 14., 15., 299., 300., 301., 1000.)]
        gpu += [(generator.uniform(3., 170.), generator.uniform(14., 300.)) for unused in range(400)]
        expected_gpu = [original.gpu(*values) for values in gpu]
        cpu = [(ticks, count, vendor) for ticks in (0, 499999, 500000, 123456789, 1250000000, 2 ** 40, 2 ** 62 - 1, 2 ** 62, 2 ** 63 - 1, 2 ** 63, 2 ** 63 + 1, 2 ** 64 - 1250000000, 2 ** 64 - 1)
               for count in (0, 1, 2, 3, 4, 16, 64, 0x80000000, 0xffffffff) for vendor in (1, 2, 3)]
        expected_cpu = [original.cpu(*values) for values in cpu]
        commands = []
        for j, r in gpu:
            commands.append('std::printf("%%d\\n",wot::stockGpuPerformanceScore(%r,%r));' % (j, r))
        for ticks, count, vendor in cpu:
            commands.append('std::printf("%%lld\\n",(long long)wot::stockCpuFrequencyScore(%dULL,%d,%d));' %
                            (ticks, count, vendor))
        source = '#include "wot/stock_gpu_performancer.hpp"\n#include <cstdio>\nint main(){\n' + '\n'.join(commands) + '\n}\n'
        with tempfile.TemporaryDirectory(prefix='gpu-performancer-scores-') as folder:
            path = Path(folder)
            (path / 'probe.cpp').write_text(source)
            result = subprocess.run([compiler, '-std=c++14', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(ROOT / 'include'), str(path / 'probe.cpp'), '-o', str(path / 'probe')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([int(value) for value in result.stdout.split()], expected_gpu + expected_cpu)
