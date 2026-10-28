"""Exercise the #1513 timestamp sampling policy on native integer results."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GpuSamplingTests(unittest.TestCase):
    def test_sampling_boundaries(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        source = r'''
#include "wot/stock_gpu_sampling.hpp"
#include <cassert>
using wot::StockGpuSampling;
int main() {
    StockGpuSampling normal(3, 500.);
    normal.timestamps(1000, 0, 800);
    normal.timestamps(1000, 0, 200);
    normal.timestamps(1000, 0, 900);
    assert(normal.passed() && normal.minimumMilliseconds() == 200.);
    StockGpuSampling slow(60, 500.);
    for (int i = 0; i < 3; ++i) slow.timestamps(1000, 0, 500);
    assert(!slow.needsSample() && !slow.passed());
    StockGpuSampling recovery(4, 500.);
    recovery.timestamps(1000, 0, 500);
    recovery.timestamps(1000, 0, 500);
    recovery.timestamps(1000, 0, 499);
    recovery.timestamps(1000, 0, 600);
    assert(recovery.passed() && recovery.minimumMilliseconds() == 499.);
    StockGpuSampling failed(1, 500.);
    failed.timestamps(0, 0, 1);
    failed.timestamps(1000, 2, 1);
    failed.failure(); failed.failure(); failed.failure();
    assert(!failed.needsSample() && !failed.passed());
    StockGpuSampling zero(0, 500.);
    assert(!zero.needsSample() && !zero.passed());
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'probe.cpp').write_text(source)
            subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'include'), str(path / 'probe.cpp'),
                            '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def test_original_pe_sampling_instructions(self):
        import os
        import random
        import struct
        client = os.environ.get('WOT_0922_CLIENT')
        compiler = shutil.which('c++')
        if not client or not compiler:
            self.skipTest('Exact #1513 client and C++ compiler are required')
        try:
            from stock_debug_queries_oracle import OriginalDebugQueries
            import stock_runtime_services_oracle as base
            from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ESP
        except ImportError:
            self.skipTest('pefile and Unicorn are required')
        original = OriginalDebugQueries(Path(client) / 'WorldOfTanks.exe')
        u = original.u
        frame = base.STACK + 0x80000
        result = base.MARKS + 0x7000
        def word(addr, val):
            u.mem_write(addr, struct.pack('<I', val))
        def readword(addr):
            return struct.unpack('<I', u.mem_read(addr, 4))[0]
        def retail(target, limit, samples):
            u.reg_write(UC_X86_REG_EBP, frame)
            u.reg_write(UC_X86_REG_ESP, frame - 0x2000)
            u.mem_write(frame + 0x14, struct.pack('<d', limit))
            word(frame + 0x1c, target)
            word(frame - 0x158, result)
            u.mem_write(result, struct.pack('<d', limit))
            for off in (0x14c, 0x150, 0x154): word(frame - off, 0)
            for valid, frequency, start, end in samples:
                if readword(frame - 0x14c) >= target or readword(frame - 0x150) >= 5 or readword(frame - 0x154) >= 3:
                    break
                if not valid or not frequency or end < start:
                    u.emu_start(0x9c1d32, 0x9c1d3f, count=20)
                else:
                    u.mem_write(frame - 0x164, struct.pack('<Q', frequency))
                    u.mem_write(frame - 0x178, struct.pack('<Q', start))
                    u.mem_write(frame - 0x198, struct.pack('<Q', end))
                    u.emu_start(0x9c1c93, 0x9c1d3f, count=200)
            u.reg_write(UC_X86_REG_EBX, 0)
            u.emu_start(0x9c1d78, 0x9c1dac, count=50)
            return u.reg_read(UC_X86_REG_EBX) & 255, struct.unpack('<d', u.mem_read(result, 8))[0]
        rng = random.Random(1513)
        cases = []
        for _ in range(150):
            target = rng.choice((0, 1, 3, 10, 30, 60))
            limit = rng.choice((500., 1000.))
            samples = []
            for unused in range(80):
                kind = rng.randrange(8)
                frequency = 0 if kind == 0 else rng.choice((1000, 3001, 1000000))
                start = 20 if kind == 1 else 0
                end = rng.choice((0, 499, 500, 501, 999, 1000, 1001))
                samples.append((kind != 2, frequency, start, end))
            cases.append((target, limit, samples))
        expected = [retail(*case) for case in cases]
        lines = ['#include "wot/stock_gpu_sampling.hpp"', '#include <cstdio>', 'int main(){']
        for target, limit, samples in cases:
            lines.append('{wot::StockGpuSampling s(%d, %.1f);' % (target, limit))
            for valid, frequency, start, end in samples:
                lines.append('s.%s;' % ('timestamps(%dULL,%dULL,%dULL)' % (frequency, start, end) if valid else 'failure()'))
            lines.append('std::printf("%d %.17g\\n", s.passed(), s.minimumMilliseconds());}')
        lines.append('}')
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'probe.cpp').write_text('\n'.join(lines))
            subprocess.run([compiler, '-std=c++14', '-O0', '-I', str(ROOT / 'include'),
                            str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            output = subprocess.check_output([str(path / 'probe')], text=True)
        actual = [(int(a), float(b)) for a, b in (line.split() for line in output.splitlines())]
        self.assertEqual(actual, expected)
