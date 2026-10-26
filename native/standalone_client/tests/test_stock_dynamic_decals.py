"""Native dynamic provider publication, lifetime and original authored DDS data."""
from pathlib import Path
import importlib.util
import os
import random
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DynamicDecalTests(unittest.TestCase):
    def test_original_provider_packet_float_boundaries_and_expiration(self):
        executable = os.environ.get('WOT_0922_CLIENT')
        if not executable or importlib.util.find_spec('unicorn') is None:
            self.skipTest('The exact original executable and Unicorn are required')
        from stock_dynamic_decals_oracle import original_dynamic_records
        source = random.Random(1513)
        cases = bytearray()
        for index in range(4096):
            vectors = [source.uniform(-200, 200) for unused in range(10)]
            priority, material, influence = index, index % 8, source.randrange(0x100000000)
            created = source.uniform(0, 100)
            lifetime = source.choice((0., .1, 1., 8., 100., -1.))
            distance = source.choice((0., 9999., 10000., 16000., 22500., 25000.))
            time = created + source.choice((0., .1, 1., 8., -1.))
            cases.extend(struct.pack('<10fIII6f4HIi', *vectors, priority, material, influence,
                                     created, lifetime, distance, time, 10000., 1. / 12500.,
                                     0, 0, 255, 511, index % 4, source.choice((-1, 0, 1, 2))))
        expected = original_dynamic_records(Path(executable) / 'WorldOfTanks.exe', cases)
        with tempfile.TemporaryDirectory(prefix='stock-dynamic-decals-') as temporary:
            directory = Path(temporary)
            probe = directory / 'probe'
            subprocess.run(['c++', '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I'+str(ROOT / 'include'),
                            str(ROOT / 'tests/test_stock_dynamic_decals.cpp'), '-o', str(probe)], check=True)
            input_path, result = directory / 'input.bin', directory / 'output.bin'
            input_path.write_bytes(cases)
            subprocess.run([str(probe), str(input_path), str(result)], check=True)
            actual = result.read_bytes()
            # Arithmetic NaN signs are not portable across x86 and ARM; the
            # native admission decision and every finite packet byte are exact.
            self.assertEqual(len(actual), len(expected))
            for offset in range(0, len(actual), 132):
                a, e = actual[offset:offset + 132], expected[offset:offset + 132]
                self.assertEqual(a[:4], e[:4], 'Admission mismatch at case %d' % (offset // 132))
                for position in range(4, 132, 4):
                    x, y = struct.unpack_from('<f', a, position)[0], struct.unpack_from('<f', e, position)[0]
                    if position in (4, 96, 100, 104, 108) and x != x and y != y: continue
                    self.assertEqual(a[position:position + 4], e[position:position + 4],
                                     'Native dynamic record mismatch at case %d offset %d' % (offset // 132, position))
        print('Original a5f080: 4096 dynamic lifetime/fade/packet cases match')


if __name__ == '__main__':
    unittest.main()
