"""Opaque tree batches against #1513 producer instructions and actual Moo selection.

The host executes production collection/upload/draw code with guarded device
boundaries. Unicorn executes the original packing and collection instructions;
neither proves Windows driver throughput or visible rendering.
"""
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function

ROOT = Path(__file__).resolve().parents[1]


class StockTreeInstancingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not archive.is_file() or not compiler:
            raise unittest.SkipTest('Pinned public SDK and host compiler required')
        with tarfile.open(archive) as sdk:
            def read(suffix):
                member = next(name for name in sdk.getnames() if name.endswith(suffix))
                return sdk.extractfile(member).read().decode('utf-8-sig')
            cpp = read('/lib/moo/complex_effect_material.cpp')
            header = read('/lib/moo/complex_effect_material.hpp')
        tree = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
        instance = '\n'.join(function(tree, signature) + (';' if signature.startswith('struct ') else '')
                             for signature in ('float addTreeScalar(', 'float subTreeScalar(',
                                               'float mulTreeScalar(', 'float divTreeScalar(',
                                               'struct TreeInstanceConstants', 'struct PackedTreeInstance',
                                               'std::int32_t truncateTreeInstanceValue(', 'PackedTreeInstance packTreeInstance('))
        complex_methods = '\n'.join((function(cpp, 'bool ComplexEffectMaterial::finishInit()'),
                                    function(header, 'inline const EffectMaterialPtr& ComplexEffectMaterial::pass('),
                                    function(header, 'inline bool\tComplexEffectMaterial::checkEffectRecompiled()')))
        draws = '\n'.join(function(tree, signature) + (';' if signature.startswith(('struct ', 'class ')) else '')
                          for signature in ('bool selectPartLods(', 'struct TreePartInstance',
                                            'bool collectTreePartInstances(', 'bool uploadTreeInstances(',
                                            'class TreeInstanceStreams', 'bool drawTreePart('))
        host = (ROOT / 'tests/stock_tree_instancing_host.hpp').read_text()
        host = host.replace('@INSTANCE@', instance).replace('@COMPLEX@', complex_methods)
        host = host.replace('@INSTANCED@', function(header, 'bool\t\t\t\t\t\tinstanced()'))
        host = host.replace('@RESET@', '\n'.join(function(tree, name) for name in
                                               ('void deleteGpuObjects()', 'virtual void deleteUnmanagedObjects()')))
        host = host.replace('@TREE@', draws)
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-tree-instancing-')
        cls.directory = Path(cls.temporary.name)
        source = cls.directory / 'host.cpp'
        cls.binary = cls.directory / 'host'
        source.write_text(host)
        subprocess.run([compiler, '-std=c++17', '-O1', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                        '-UNDEBUG', *native_test_compile_flags(compiler), '-I'+str(ROOT / 'bigworld'),
                        str(source), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def original(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        return OriginalTreeInstructionOracle(Path(client) / 'WorldOfTanks.exe')

    def test_all_parts_lods_effect_passes_and_real_technique_fallback(self):
        subprocess.run([str(self.binary), 'draw'], check=True)

    def test_each_device_failure_restores_streams_declaration_and_fvf(self):
        subprocess.run([str(self.binary), 'failure'], check=True)

    def test_dynamic_buffer_reuse_and_both_device_reset_paths(self):
        subprocess.run([str(self.binary), 'reset'], check=True)

    def test_packing_matches_original_instructions(self):
        oracle = self.original()
        rng = random.Random(1513)
        rows = [struct.pack('<16f', *(rng.uniform(-100, 100) for unused in range(16))) for unused in range(256)]
        # CVTTSS2SI, signed short narrowing, byte wrapping and float32 rounding.
        for bits in (0, 0x80000000, 0x3effffff, 0x3f000000, 0x3f000001, 0x3f800000,
                     0xbf800000, 0x477fff00, 0x47000000, 0x4f000000, 0xcf000001,
                     0x7f800000, 0xff800000, 0x7fc12345, 0x7f7fffff):
            rows.append(struct.pack('<16I', *([bits]*16)))
        source = b''.join(rows)
        input_address, output_address = oracle.allocate(len(source)), oracle.allocate(len(rows)*40)
        count, pointer, descriptor, owner = (oracle.allocate(16) for unused in range(4))
        u = oracle.emulator
        u.mem_write(input_address, source)
        u.mem_write(count, struct.pack('<I', len(rows)))
        u.mem_write(pointer, struct.pack('<I', input_address))
        u.mem_write(descriptor, struct.pack('<I', output_address))
        u.mem_write(owner, struct.pack('<3I', 0, count, pointer))
        oracle.call(0xafd8d0, (0, descriptor), ecx=owner)
        expected = bytes(u.mem_read(output_address, len(rows)*40))
        inputs, outputs = self.directory / 'pack-input', self.directory / 'pack-output'
        inputs.write_bytes(source)
        subprocess.run([str(self.binary), 'pack', str(inputs), str(outputs)], check=True)
        actual = outputs.read_bytes()
        for i in range(len(rows)):
            self.assertEqual(actual[i*40:(i+1)*40], expected[i*40:(i+1)*40], 'row %d' % i)

    def test_collection_matches_original_type_part_and_lod_buckets(self):
        oracle = self.original()
        u = oracle.emulator
        owners = [oracle.allocate(0x300), oracle.allocate(0x300)]
        for number, owner in enumerate(owners):
            u.mem_write(owner+0x144, struct.pack('<I', (3, 8)[number]))
            for bucket in range(19):
                data = oracle.allocate(4*64)
                u.mem_write(owner+bucket*16, struct.pack('<4I', data, data+4*64, data+4*64, 0))
            for offset in (0x184, 0x19c, 0x1b4, 0x1cc):
                data = oracle.allocate(24)
                u.mem_write(data, struct.pack('<6I', 1, 1, 0, 0, 0, 0))
                u.mem_write(owner+offset, struct.pack('<2I', data, data+24))
        u.mem_write(0x1a20ec8, b'\x01'*4)
        u.mem_write(0x1a210c0, struct.pack('<2f', 84, 255))
        fixture = ((0, 84, 1, 90, (0, 1), (91, 92), .5),
                   (0, 85, 1, 93, (1, -1), (94, 0), 1),
                   (1, 86, -1, 0, (-1, -1), (0, 0), 0),
                   (0, 87, 0, 95, (0, -1), (96, 0), 1))
        for i, (branch, ba, frond, fa, leaves, la, fade) in enumerate(fixture):
            data = bytearray(128)
            struct.pack_into('<5f', data, 4, ba, fa, *la, fade)
            struct.pack_into('<5i', data, 0x18, *leaves, branch, frond, sum(lod >= 0 for lod in leaves))
            data[0x2d:0x31] = bytes((branch >= 0, frond >= 0, any(lod >= 0 for lod in leaves), fade > 0))
            struct.pack_into('<16f', data, 0x40, 0, 0, 0, 1, i+1, 0, 0, 0, 1, 1, 1, 0, 0, .5, 0, 1)
            address = oracle.allocate(len(data))
            u.mem_write(address, bytes(data))
            oracle.call(0xb05560, (address, 1, 0), ecx=owners[0 if i < 3 else 1])
        expected = bytearray()
        for part in range(4):
            for type_index, owner in enumerate(owners):
                for lod in range(1 if part == 3 else 6):
                    bucket = owner+(part*6+lod)*16
                    begin, count = oracle.word(bucket), oracle.word(bucket+12)
                    for row in range(count):
                        expected.extend(struct.pack('<3I', part, type_index, lod))
                        expected.extend(u.mem_read(begin+row*64, 64))
        output = self.directory / 'collect-output'
        subprocess.run([str(self.binary), 'collect', str(output)], check=True)
        self.assertEqual(output.read_bytes(), expected)


if __name__ == '__main__':
    unittest.main()
