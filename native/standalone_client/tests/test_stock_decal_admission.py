"""Provider admission against original x86 instructions; no GPU-render claim."""
from pathlib import Path
import importlib.util
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def record(group, material, projection, priority, distance):
    result = bytearray(124)
    struct.pack_into('<iI', result, 0, material, priority)
    struct.pack_into('<bf', result, 11, projection, distance)
    struct.pack_into('<I', result, 112, group)
    return bytes(result)


def group_bytes(groups):
    return b''.join(struct.pack('<I', len(group)) + b''.join(struct.pack('<I', i) for i in group)
                    for group in groups)


class DecalAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-decal-admission-')
        cls.directory = Path(cls.temporary.name)
        cls.probe = cls.directory / 'probe'
        subprocess.run(['c++', '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                        '-I' + str(ROOT / 'bigworld'), str(ROOT / 'tests/test_stock_decal_admission.cpp'),
                        '-o', str(cls.probe)], check=True)
        client = os.environ.get('WOT_0922_CLIENT')
        cls.executable = Path(client) / 'WorldOfTanks.exe' if client else None

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def original(self):
        if self.executable is None or importlib.util.find_spec('unicorn') is None:
            self.skipTest('Pinned original executable, pefile and Unicorn are required')
        sys.path.insert(0, str(ROOT / 'tests'))
        from stock_decal_admission_oracle import OriginalDecalAdmission
        return OriginalDecalAdmission(self.executable)

    def test_key_width_and_cap_boundaries(self):
        subprocess.run([str(self.probe)], check=True)

    def test_original_sort_cap_and_group_admission(self):
        oracle = self.original()
        source = random.Random(1513)
        cases = [(0, 0, b''), (1, 1, b''), (0, 1, record(0, 7, 0, 0, 1)),
                 (1, 1, record(0, 7, 0, 0, 1) + record(0, 0, 0, 0, 100)),
                 (2, 1, record(0, 0, 0, 0, 1) + record(1, 0, 0, 0, 2) + record(2, 0, 0, 0, 3))]
        for c in range(240):
            count = source.randrange(1, 121)
            groups = source.randrange(1, 5)
            payload = b''.join(record(source.randrange(groups + 2), source.choice((-1, 0, 1, 2, 7)),
                                      source.choice((-128, -1, 0, 1, 3, 127)),
                                      source.choice((0, 1, 0x7fffffff, 0x80000000, 0xffffffff)),
                                      float(i * 128 + c)) for i in range(count))
            cases.append((source.choice((0, 1, 2, 8, 32, 64, 0xffffffff)), groups, payload))
        data = bytearray(struct.pack('<I', len(cases)))
        expected = bytearray()
        for cap, groups, payload in cases:
            data.extend(struct.pack('<III', cap, groups, len(payload) // 124) + payload)
            expected.extend(group_bytes(oracle.provider(cap, groups, payload)))
        source_path, output_path = self.directory / 'input.bin', self.directory / 'output.bin'
        source_path.write_bytes(data)
        subprocess.run([str(self.probe), str(source_path), str(output_path)], check=True)
        self.assertEqual(output_path.read_bytes(), expected)
        destination = os.environ.get('WOT_DECAL_ADMISSION_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'input.bin').write_bytes(data)
            (destination / 'expected.bin').write_bytes(expected)
        print('Actual a91380/a8efb0/a91010 prefix: %d providers, sort/cap/group admission EXACT' % len(cases))

    def test_actual_static_provider_fade_precedes_shared_cap(self):
        oracle = self.original()
        # Include adjacent binary32 distances at the real 1/255 fade boundary.
        threshold_distance = 1.0 - struct.unpack('<f', struct.pack('<f', .003921569))[0]
        bits = struct.unpack('<I', struct.pack('<f', threshold_distance))[0]
        distances = [struct.unpack('<f', struct.pack('<I', bits + delta))[0] for delta in range(-4, 5)]
        source = random.Random(1513)
        inputs = [(distance, 0.0, 1.0) for distance in distances]
        inputs += [(source.uniform(0, 100), source.uniform(0, 20), source.choice((0.0, .01, .1, 1.0)))
                   for unused in range(120)]
        data = struct.pack('<I', len(inputs)) + b''.join(struct.pack('<3f', *values) for values in inputs)
        input_path, output_path = self.directory / 'fade.bin', self.directory / 'fade-out.bin'
        input_path.write_bytes(data)
        subprocess.run([str(self.probe), '--fade', str(input_path), str(output_path)], check=True)
        actual = output_path.read_bytes()
        for index, (distance, start, inverse) in enumerate(inputs):
            # A second surviving record ensures an early cap cannot mask fade rejection.
            payload = oracle.static_provider([(distance, 0, 0), (-1.0, 7, 0)], start, inverse)
            native = [(struct.unpack_from('<f', payload, offset + 72)[0],
                       struct.unpack_from('<f', payload, offset + 88)[0])
                      for offset in range(0, len(payload), 124)]
            survived = next((fade for identity, fade in native if identity == 0), None)
            fade, keep = struct.unpack_from('<fI', actual, index * 8)
            self.assertEqual(bool(keep), survived is not None)
            if survived is not None:
                self.assertEqual(struct.pack('<f', survived), struct.pack('<f', fade))
            admitted = oracle.provider(1, 1, payload)[0]
            self.assertEqual(len(admitted), 1)
            winner = struct.unpack_from('<f', payload, admitted[0] * 124 + 72)[0]
            self.assertEqual(winner, 0 if keep else 1)
        print('Actual a4bcb0: %d fade edge/providers, raw survivors then shared cap EXACT' % len(inputs))

    def test_actual_five_provider_facade_and_phase_reuse(self):
        oracle = self.original()
        cases = [(2, 1, b''.join(record(0, material, 0, 0, distance)
                                for material, distance in ((7, 1), (0, 100), (2, 2), (1, 3)))) for i in range(5)]
        results = oracle.facade_collect_and_phases(cases)
        self.assertEqual(oracle.prepared, list(range(5)))
        self.assertEqual(oracle.emitted, list(range(5)))
        self.assertEqual(results, [[[1, 3]]] * 5)
        self.assertEqual(sum(len(group) for provider in results for group in provider), 10)
        self.assertEqual([p[2] for p in oracle.phases], list(range(4)))
        self.assertEqual({p[1] for p in oracle.phases}, {oracle.facade + 0x4c})
        self.assertEqual(len(oracle.results), 5, 'Draw phases must not repeat admission')
        print('Actual a49990 + a49a50: 5 independent caps, scratch reset, 4 phases reuse EXACT')
        # a90760 reads already-admitted material counters. Its cap*5 is a
        # buffer capacity, while the phase draw count is the full counter sum.
        counts = [{0: 1, 5: 1} for unused in range(5)]
        for phase, materials, total in [(0, [7, 0, 1, 4, 6], 5), (1, [2], 0),
                                        (2, [3], 0), (3, [5], 5)]:
            count, capacity, actual_materials = oracle.phase_budget(2, counts, phase)
            self.assertEqual((count, capacity, actual_materials), (total, 10, materials))
        print('Actual a90760 phase/count prefix: sums 5 providers; buffer budget = cap*5 EXACT')


if __name__ == '__main__':
    unittest.main()
