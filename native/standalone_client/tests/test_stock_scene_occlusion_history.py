"""Exercise production history and optionally compare the exact #1513 updater."""
import importlib.util
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROBE = r'''
#include "wot/stock_scene_occlusion_history.hpp"
#include <cstdint>
#include <iostream>
int main() {
  char operation;
  std::uint32_t frame, packed, current;
  unsigned first, second;
  while (std::cin >> operation >> frame >> packed >> current >> first >> second) {
    wot::StockSceneOcclusionHistory history(frame, packed);
    bool draw = true;
    if (operation == 'O') history.observe(current, first != 0, second != 0);
    else if (operation == 'D') draw = history.draw(current, first != 0, second != 0);
    else if (operation == 'R') history.reset();
    else return 2;
    std::cout << history.last_frame() << ' ' << history.packed() << ' ' << draw << '\n';
  }
  wot::StockSceneOcclusionHistory initial;
  if (initial.last_frame() || initial.packed() || !initial.draw(0, false, true))
    return 3;
}
'''


class StockSceneOcclusionHistoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++')
        if not compiler:
            raise RuntimeError('A C++ compiler is required for the history kernel tests')
        cls.directory = tempfile.TemporaryDirectory(prefix='wot-scene-occlusion-')
        cls.addClassCleanup(cls.directory.cleanup)
        source = Path(cls.directory.name) / 'history.cpp'
        cls.binary = Path(cls.directory.name) / 'history'
        source.write_text(PROBE)
        subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'include'), str(source), '-o', str(cls.binary)],
                       check=True, capture_output=True, text=True)

    def run_cases(self, cases):
        result = subprocess.run([str(self.binary)], input=''.join(
            ' '.join(map(str, case)) + '\n' for case in cases),
            check=True, capture_output=True, text=True)
        return [tuple(map(int, line.split())) for line in result.stdout.splitlines()]

    def test_observations_preserve_flag_clear_opposite_and_saturate(self):
        cases = [
            ('O', 10, 0, 10, 1, 1),
            ('O', 10, 0x80000005, 11, 1, 1),
            ('O', 10, 0x80070000, 11, 0, 1),
            ('O', 10, 0x7fff0000, 11, 1, 1),
            ('O', 10, 0xffff0000, 11, 1, 1),
            ('O', 10, 0x0000ffff, 11, 0, 1),
            ('O', 10, 0x8000ffff, 11, 0, 1),
            ('O', 10, 0x80070008, 12, 1, 1),
            ('O', 10, 0x80070008, 12, 0, 1),
            ('O', 10, 0x80070008, 12, 1, 0),
            ('O', 10, 0x80070008, 12, 0, 0),
            ('O', 0xffffffff, 0x80050000, 0, 1, 1),
            ('O', 0xffffffff, 0x80000004, 0, 0, 1),
            ('O', 0xffffffff, 0x80000004, 1, 0, 1),
        ]
        self.assertEqual(self.run_cases(cases), [
            (10, 0x10000, 1), (11, 0x80010000, 1), (11, 0x80000001, 1),
            (11, 0x7fff0000, 1), (11, 0xffff0000, 1),
            (11, 0xffff, 1), (11, 0x8000ffff, 1),
            (12, 0x80010000, 1), (12, 0x80000001, 1),
            (12, 0x80080000, 1), (12, 0x80000009, 1),
            (0, 0x80060000, 1), (0, 0x80000005, 1), (1, 0x80000001, 1),
        ])

    def test_pending_failure_capacity_and_camera_boundary_fail_open(self):
        cases = [
            ('D', 20, 3, 20, 0, 1),
            ('D', 20, 3, 21, 0, 1),
            ('D', 20, 3, 22, 0, 1),  # Pending S_FALSE may outlive the result.
            ('D', 20, 3, 21, 0, 0),  # Query failure or capacity rejection.
            ('D', 20, 3, 19, 0, 1),  # Future observation is not fresh evidence.
            ('D', 20, 0x80020000, 21, 0, 1),
            ('D', 20, 0, 21, 0, 1),
            ('D', 20, 0x80000003, 21, 1, 1),
            ('D', 20, 0x80000003, 25, 1, 0),
            ('D', 0xffffffff, 2, 0, 0, 1),
            ('D', 0xffffffff, 2, 1, 0, 1),
            ('R', 20, 0xffffffff, 99, 0, 0),
        ]
        self.assertEqual(self.run_cases(cases), [
            (20, 3, 0), (20, 3, 0), (20, 3, 1), (20, 3, 1), (20, 3, 1),
            (20, 0x80020000, 1), (20, 0, 1), (21, 0x80010000, 1),
            (25, 0x80010000, 1), (0xffffffff, 2, 0), (0xffffffff, 2, 1),
            (0, 0, 1),
        ])

    def test_production_observe_matches_exact_1513_machine_code(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or not (Path(client) / 'WorldOfTanks.exe').is_file():
            self.skipTest('WOT_0922_CLIENT is required for the optional exact #1513 oracle')
        if any(importlib.util.find_spec(name) is None for name in ('pefile', 'unicorn')):
            self.skipTest('pefile and unicorn are required for the optional exact oracle')
        import pefile
        import unicorn
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP

        pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
        self.assertEqual(pe.FILE_HEADER.Machine, 0x14c)
        base = pe.OPTIONAL_HEADER.ImageBase
        entry = 0xac20a0
        code = pe.get_memory_mapped_image()
        self.assertEqual(code[entry - base:entry - base + 7], b'\x55\x8b\xec\x80\x7d\x10\x00')
        machine = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        machine.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 0xfff) & ~0xfff)
        machine.mem_write(base, code)
        storage, stack, stop = 0x30000000, 0x30010000, 0x3001f000
        machine.mem_map(storage, 0x20000)
        rng = random.Random(1513)
        cases = []
        frames = (0, 1, 10, 0xfffffffe, 0xffffffff)
        packed_values = (0, 1, 0xffff, 0x10000, 0x7ffe0000, 0x7fff0000,
                         0x80000000, 0x8000ffff, 0xfffe0000, 0xffff0000, 0xffffffff)
        for frame in frames:
            for packed in packed_values:
                for gap in (0, 1, 2, 0xffffffff):
                    for visible in (0, 1):
                        for reset_gap in (0, 1):
                            cases.append(('O', frame, packed, (frame + gap) & 0xffffffff,
                                          visible, reset_gap))
        for unused in range(250):
            cases.append(('O', rng.getrandbits(32), rng.getrandbits(32),
                          rng.getrandbits(32), rng.randrange(2), rng.randrange(2)))
        expected = []
        for unused, frame, packed, current, visible, reset_gap in cases:
            machine.mem_write(storage, struct.pack('<II', frame, packed))
            machine.mem_write(stack, struct.pack('<IIII', stop, visible, current, reset_gap))
            machine.reg_write(UC_X86_REG_ECX, storage)
            machine.reg_write(UC_X86_REG_ESP, stack)
            machine.emu_start(entry, stop, count=100)
            expected.append((*struct.unpack('<II', machine.mem_read(storage, 8)), 1))
        self.assertEqual(self.run_cases(cases), expected)


if __name__ == '__main__':
    unittest.main()
