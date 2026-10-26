"""Original SpTr3 section, material-handle lifecycle and reflection math probes.

The x86 instruction oracle bridges only the CRT powf boundary. These tests do
not create a D3D device and do not prove native tree rendering or collision.
"""
from pathlib import Path
import importlib.util
import itertools
import math
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def original_reflection_values(executable, cases):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_EBP, UC_X86_REG_EDI,
                                   UC_X86_REG_ESP, UC_X86_REG_EIP,
                                   UC_X86_REG_XMM0, UC_X86_REG_XMM1)
    sys.path.insert(0, str(ROOT / 'tests'))
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    oracle = OriginalTreeInstructionOracle(executable)
    emulator = oracle.emulator
    parameters = oracle.allocate(16)
    frame = oracle.allocate(256) + 128
    power_calls = 0

    def power_boundary(uc, address, size, context):
        nonlocal power_calls
        if address != 0x13dad2b:
            return
        power_calls += 1
        value = lambda register: struct.unpack('<f', struct.pack('<I', uc.reg_read(register) & 0xffffffff))[0]
        # __libm_sse2_powf takes and returns binary32 in XMM0/XMM1. The guest
        # arithmetic/control flow around it is the unmodified original code.
        result = struct.unpack('<I', struct.pack('<f', math.pow(value(UC_X86_REG_XMM0), value(UC_X86_REG_XMM1))))[0]
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_XMM0, result)
        uc.reg_write(UC_X86_REG_EIP, oracle.word(esp))
        uc.reg_write(UC_X86_REG_ESP, esp+4)

    emulator.hook_add(UC_HOOK_CODE, power_boundary, begin=0x13dad2b, end=0x13dad2b)
    outputs = bytearray()
    for case in cases:
        # The range starts after original code obtains the real category's
        # pointer; EAX/EDI and EBP are supplied as explicit ABI inputs.
        from unicorn.x86_const import UC_X86_REG_EAX
        emulator.mem_write(parameters, struct.pack('<4f', *case))
        emulator.reg_write(UC_X86_REG_EAX, parameters-4)
        emulator.reg_write(UC_X86_REG_EDI, parameters-4)
        emulator.reg_write(UC_X86_REG_EBP, frame)
        emulator.reg_write(UC_X86_REG_ESP, oracle.stack)
        emulator.emu_start(0xb01561, 0xb016ca, count=100000)
        outputs.extend(struct.pack('<I', emulator.reg_read(UC_X86_REG_XMM0) & 0xffffffff))
    return bytes(outputs), power_calls


class StockTreeSceneTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-tree-scene-')
        cls.directory = Path(cls.temporary.name)
        cls.binary = cls.directory / 'probe'
        subprocess.run(['c++', '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                        '-I'+str(ROOT / 'bigworld'), str(ROOT / 'tests/test_stock_tree_scene.cpp'),
                        '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_section_failures_atomic_publication_and_material_handle_reuse(self):
        subprocess.run([str(self.binary)], check=True)

    def publish_fixture(self, files):
        destination = os.environ.get('WOT_TREE_SCENE_FIXTURE_OUT')
        if destination:
            destination = Path(destination)
            destination.mkdir(parents=True, exist_ok=True)
            for name, data in files.items():
                (destination / name).write_bytes(data)

    def test_all_original_sections_preserve_exact_definitions_and_eof(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original map sections')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from space_bin_0922 import CompiledSpace
        sections, definitions = [], 0
        for package in sorted((Path(client) / 'res/packages').glob('*.pkg')):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    section = CompiledSpace(archive.read(name)).section_data('SpTr')
                    stride, count = struct.unpack_from('<II', section)
                    self.assertEqual(stride, 80)
                    self.assertEqual(len(section), 36+count*80)
                    self.assertEqual(struct.unpack_from('<I', section, 8+count*80)[0], 24)
                    sections.append(section)
                    definitions += count
        self.assertTrue(sections)
        source = b''.join(struct.pack('<I', len(section))+section for section in sections)
        inputs, output = self.directory / 'sections.bin', self.directory / 'sections-out.bin'
        inputs.write_bytes(source)
        subprocess.run([str(self.binary), str(inputs), str(output)], check=True)
        self.assertEqual(output.read_bytes(), source)
        self.publish_fixture({'sections.bin': source, 'sections-expected.bin': source})
        print('Actual SpTr3 sections: %d maps, %d definitions, all bytes preserved to EOF' % (len(sections), definitions))

    def test_reflection_correction_against_original_sse_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or importlib.util.find_spec('unicorn') is None or importlib.util.find_spec('pefile') is None:
            self.skipTest('Pinned executable, Unicorn and pefile are required for the instruction oracle')
        cases = list(itertools.product((-.2, 0, .05, .3, .30000004, .7, 1, 1.5),
                                       (0, .00001, .1, .599999964, .6, .60000008, .95, 1, 2),
                                       (-.1, 0, .49999997, .5, .50000006, .7, 1, 1.5)))
        cases = [(*values, 0) for values in cases]
        source = b''.join(struct.pack('<4f', *case) for case in cases)
        expected, power_calls = original_reflection_values(Path(client) / 'WorldOfTanks.exe', cases)
        inputs, output = self.directory / 'reflection.bin', self.directory / 'reflection-out.bin'
        inputs.write_bytes(source)
        subprocess.run([str(self.binary), str(inputs), str(output), '--reflection'], check=True)
        self.assertEqual(output.read_bytes(), expected)
        self.publish_fixture({'reflection.bin': source, 'reflection-expected.bin': expected})
        print('Original SSE reflection range: %d cases, %d explicit CRT powf bridges, exact float output' % (len(cases), power_calls))


if __name__ == '__main__':
    unittest.main()
