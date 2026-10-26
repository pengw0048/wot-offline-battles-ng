"""WGSD2 wire boundaries against all original map sections; no render claim."""
from pathlib import Path
import os
import io
import importlib.util
import random
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class StockSpaceDecalsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-space-decals-')
        cls.directory = Path(cls.temporary.name)
        cls.probe = cls.directory / 'probe'
        subprocess.run(['c++', '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                        '-I'+str(ROOT / 'bigworld'), str(ROOT / 'tests/test_stock_space_decals.cpp'),
                        '-o', str(cls.probe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_truncation_ranges_exact_eof_and_atomic_replacement(self):
        subprocess.run([str(self.probe)], check=True)

    def test_gpu_pack_against_unmodified_original_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or importlib.util.find_spec('unicorn') is None or importlib.util.find_spec('pefile') is None:
            self.skipTest('Pinned executable, Unicorn and pefile are required')
        sys.path.insert(0, str(ROOT / 'tests'))
        from stock_space_decals_oracle import original_pack
        random_source = random.Random(1513)
        inputs = bytearray()
        for index in range(4096):
            widths = [64, 256, 1024, 4096]
            material = []
            for channel in range(4):
                width, height = random_source.choice(widths), random_source.choice(widths)
                material.extend((width, height, 1.0 / width, 1.0 / height))
            material.extend((0, 0, 0, 0, 0))
            border = random_source.choice((0, 1, 2, 4))
            source = bytearray(124)
            struct.pack_into('<III', source, 0, index % 8, index % 256,
                             random_source.choice((0, 2, 6, 10, 18, 30, 50, 0xffffff)) | (index % 4) << 24)
            for offset in range(12, 112, 4):
                struct.pack_into('<f', source, offset, random_source.uniform(-2, 2))
            struct.pack_into('<I', source, 104, random_source.choice((0, 1, 2, 127, 255, 0x80000000, 0xffffffff)))
            struct.pack_into('<4H', source, 116, 4, 8, random_source.randrange(16, 64), random_source.randrange(16, 64))
            inputs.extend(struct.pack('<21fIf', *material, border, 0) + source)
        expected = original_pack(Path(client) / 'WorldOfTanks.exe', bytes(inputs))
        source, output = self.directory / 'pack.bin', self.directory / 'pack-out.bin'
        source.write_bytes(inputs)
        subprocess.run([str(self.probe), str(source), str(output), '--pack'], check=True)
        actual = output.read_bytes()
        if actual != expected:
            first = next(index for index, values in enumerate(zip(actual, expected)) if values[0] != values[1])
            offset = (first // 96) * 96
            self.fail('GPU mismatch at case %d, float %d: actual %r expected %r' %
                      (first // 96, first % 96 // 4, struct.unpack_from('<24f', actual, offset),
                       struct.unpack_from('<24f', expected, offset)))
        destination = os.environ.get('WOT_SPACE_DECALS_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'pack.bin').write_bytes(inputs)
            (destination / 'pack-expected.bin').write_bytes(expected)
        print('Actual a8fd60 instructions: 4096 normal-render inputs, %d bytes exact; no imported bridges' % len(expected))

    def test_distance_producer_against_unmodified_original_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or importlib.util.find_spec('unicorn') is None or importlib.util.find_spec('capstone') is None:
            self.skipTest('Pinned executable, Unicorn and Capstone are required')
        sys.path.insert(0, str(ROOT / 'tests'))
        from stock_space_decals_oracle import original_distance
        source = random.Random(1513)
        inputs = b''.join(struct.pack('<7f', *(source.uniform(-10000, 10000) for unused in range(6)),
                                    source.choice((0, .25, .5, .8, 1, 1.25, 2, 4, -1))) for index in range(4096))
        expected = original_distance(Path(client) / 'WorldOfTanks.exe', inputs)
        input_path, output_path = self.directory / 'distance.bin', self.directory / 'distance-out.bin'
        input_path.write_bytes(inputs)
        subprocess.run([str(self.probe), str(input_path), str(output_path), '--distance'], check=True)
        self.assertEqual(output_path.read_bytes(), expected)
        destination = os.environ.get('WOT_SPACE_DECALS_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'distance.bin').write_bytes(inputs)
            (destination / 'distance-expected.bin').write_bytes(expected)
        print('Actual a4c810 distance store: 4096 inputs / %d bytes EXACT; no imported bridges' % len(expected))

    def test_atlas_mip_count_writer_against_unmodified_original_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or importlib.util.find_spec('unicorn') is None:
            self.skipTest('Pinned executable and Unicorn are required')
        sys.path.insert(0, str(ROOT / 'tests'))
        from stock_space_decals_oracle import original_mip_levels
        inputs = b''.join(struct.pack('<II', anisotropy, skip)
                          for anisotropy in (0, 1, 2, 3, 4, 8, 16, 32, 256) for skip in range(5))
        expected = original_mip_levels(Path(client) / 'WorldOfTanks.exe', inputs)
        input_path, output_path = self.directory / 'levels.bin', self.directory / 'levels-out.bin'
        input_path.write_bytes(inputs)
        subprocess.run([str(self.probe), str(input_path), str(output_path), '--levels'], check=True)
        self.assertEqual(output_path.read_bytes(), expected)
        destination = os.environ.get('WOT_SPACE_DECALS_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'levels.bin').write_bytes(inputs)
            (destination / 'levels-expected.bin').write_bytes(expected)
            import pefile
            pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
            mapped = pe.get_memory_mapped_image()
            self.assertEqual(struct.unpack_from('<f', mapped, 0x14ae8e4 - pe.OPTIONAL_HEADER.ImageBase)[0], .5)
            block = mapped[0xa91199 - pe.OPTIONAL_HEADER.ImageBase:0xa9123c - pe.OPTIONAL_HEADER.ImageBase]
            self.assertEqual(len(block), 0xa9123c - 0xa91199)
            self.assertEqual(block[0x76], 0xe8)
            (destination / 'camera-block.bin').write_bytes(block)
        print('Actual 13f5450 mip writer: 45 inputs / %d bytes EXACT; no imported bridges' % len(expected))

    def test_all_original_maps_roundtrip_every_authored_byte(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original WGSD2 data')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from space_bin_0922 import CompiledSpace
        maps, decals, groups = 0, 0, 0
        fixture = bytearray()
        for package in sorted((Path(client) / 'res/packages').glob('*.pkg')):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    space = CompiledSpace(archive.read(name))
                    self.assertEqual(space.sections['WGSD'].version, 2)
                    data = space.section_data('WGSD')
                    stride, count = struct.unpack_from('<II', data)
                    self.assertEqual(stride, 116)
                    end = 8 + count * 116
                    group_stride, group_count = struct.unpack_from('<II', data, end)
                    self.assertEqual(group_stride, 32)
                    self.assertEqual(len(data), end + 8 + group_count * 32)
                    first_expected = 0
                    for index in range(group_count):
                        first, last = struct.unpack_from('<II', data, end + 8 + index * 32 + 24)
                        self.assertEqual(first, first_expected)
                        self.assertLessEqual(first, last)
                        self.assertLess(last, count)
                        first_expected = last + 1
                    self.assertEqual(first_expected, count)
                    fixture.extend(struct.pack('<I', len(data)) + data)
                    maps += 1; decals += count; groups += group_count
        self.assertGreater(maps, 0)
        source, output = self.directory / 'sections.bin', self.directory / 'out.bin'
        source.write_bytes(fixture)
        subprocess.run([str(self.probe), str(source), str(output)], check=True)
        self.assertEqual(output.read_bytes(), fixture)
        destination = os.environ.get('WOT_SPACE_DECALS_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'sections.bin').write_bytes(fixture)
        print('Actual WGSD2: %d maps, %d decals, %d inclusive groups, %d bytes exact' %
              (maps, decals, groups, len(fixture)))

    def test_actual_required_texture_channels_and_authored_atlas_constraints(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original resources')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from space_bin_0922 import CompiledSpace
        import packed_xml
        import base64
        root = Path(client) / 'res'
        def text(value):
            return (base64.b64encode(value.value) if value.value_type == packed_xml.TYPE_COMPRESSED_STRING
                    else value.value).decode('ascii')
        def child(element, name):
            return next(value for key, value in element.children if key == name.encode())
        resources = packed_xml.read_packed_xml((root / 'resources.xml').read_bytes())
        configured = text(child(child(resources, 'system').value, 'decalsConfig'))
        settings = packed_xml.read_packed_xml((root / configured).read_bytes())
        scene = child(settings, 'scene').value
        self.assertEqual(child(scene, 'anisotropy').value, 4)
        profiles = []
        for key, option in child(scene, 'quality').value.children:
            self.assertEqual(key, b'option')
            profiles.append((text(option.value.value), child(option.value, 'mip').value))
        self.assertEqual(profiles, [('MAX', 0), ('HIGH', 1), ('MEDIUM', 1), ('LOW', 2), ('OFF', -1)])
        constraints = {}
        for key, value in scene.children:
            if key != b'type': continue
            name = text(child(value.value, 'name'))
            constraints[name] = []
            for tag, atlas in value.value.children:
                if tag != b'atlas': continue
                rule = child(atlas.value, 'constrains').value
                constraints[name].append((child(rule, 'minWidth').value, child(rule, 'minHeight').value,
                    child(rule, 'maxWidth').value, child(rule, 'maxHeight').value, text(child(rule, 'format')).encode()))
        required = set()
        counts = (1, 2, 3, 2, 1, 1, 1)
        packages = []
        index = {}
        try:
            for path in sorted((root / 'packages').glob('*.pkg')):
                archive = zipfile.ZipFile(path); packages.append(archive)
                for name in archive.namelist(): index[name.lower()] = (archive, name)
            for archive in packages:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'): continue
                    space = CompiledSpace(archive.read(name))
                    table = space.section_data('BWST')
                    stride, count = struct.unpack_from('<II', table)
                    self.assertEqual(stride, 12)
                    text_size_offset = 8 + count * stride
                    strings = table[text_size_offset + 4:]
                    self.assertEqual(len(strings), struct.unpack_from('<I', table, text_size_offset)[0])
                    names = {}
                    for item in range(count):
                        hash_value, offset, size = struct.unpack_from('<III', table, 8 + item * 12)
                        names[hash_value] = strings[offset:offset + size].decode('utf-8')
                    data = space.section_data('WGSD')
                    count = struct.unpack_from('<I', data, 4)[0]
                    for item in range(count):
                        record = data[8 + item * 116:8 + (item + 1) * 116]
                        paths = [names[hash_value] if hash_value else '' for hash_value in struct.unpack_from('<4I', record, 0x44)]
                        material = record[0x56]
                        self.assertLess(material, len(counts))
                        family = 'OtherDecals' if material in (4, 5) else 'GBufferDecals'
                        self.assertLessEqual(counts[material], len(constraints[family]))
                        dimensions = set()
                        for channel in range(counts[material]):
                            path = paths[channel]
                            self.assertTrue(path)
                            archive, exact = index[path.lower()]
                            with archive.open(exact) as file: header = file.read(128)
                            self.assertEqual(header[:4], b'DDS ')
                            height, width = struct.unpack_from('<II', header, 12)
                            mip_count = struct.unpack_from('<I', header, 28)[0]
                            minimum_w, minimum_h, maximum_w, maximum_h, format_name = constraints[family][channel]
                            self.assertEqual(header[84:88], format_name)
                            self.assertTrue(minimum_w <= width <= maximum_w and minimum_h <= height <= maximum_h)
                            self.assertFalse(width & (width - 1) or height & (height - 1))
                            self.assertGreaterEqual(mip_count, 5)
                            dimensions.add((width, height)); required.add((family, channel, path))
                        self.assertEqual(len(dimensions), 1)
            self.assertTrue(required)
            print('Actual WGSD2 required channel references: %d unique, all DDS formats/dimensions/mips satisfy authored atlas constraints' % len(required))
            destination = os.environ.get('WOT_SPACE_DECALS_FIXTURE_OUT')
            if destination:
                paths = sorted({path.lower() for family, channel, path in required})
                destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
                with (destination / 'gpu-textures.bin').open('wb') as output:
                    output.write(struct.pack('<I', len(paths)))
                    for path in paths:
                        archive, exact = index[path]
                        name, data = path.encode('utf-8'), archive.read(exact)
                        output.write(struct.pack('<II', len(name), len(data)) + name + data)
        finally:
            for archive in packages: archive.close()

    def test_original_effect_requires_six_instance_float4s_and_real_deferred_pipeline(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for original effects')
        source = self.directory / 'effect.cpp'
        source.write_text(r'''
#include "wot/stock_shader_binary.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <set>
int main(int argc, char** argv) {
 assert(argc==3);
 for(int variant=0;variant!=2;++variant) {
  std::ifstream file(argv[variant+1],std::ios::binary);
  std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
  wot::StockEffectBinary effect;std::string error;
  assert(wot::decode_stock_dx9_effect(bytes.data(),bytes.size(),effect,error));
  if(variant==1) {assert(effect.techniques.size()==1&&effect.techniques[0].name=="DummyTechnique");continue;}
  assert(effect.techniques.size()==12);
  bool found=false;
  for(const auto& shader:effect.shaders) {
   if(shader.stage!=1)continue;
   std::set<unsigned> texcoords;
   const auto& code=shader.bytecode;
   for(unsigned index=1;index<code.size();) {
    const unsigned op=code[index]&0xffff;
    if(op==0xffff)break;
    if(op==0xfffe) {index+=1+((code[index]>>16)&0x7fff);continue;}
    const unsigned length=(code[index]>>24)&15;
    if(op==31&&length==2&&(code[index+1]&15)==5)texcoords.insert((code[index+1]>>16)&15);
    index+=1+length;
   }
   if(texcoords.count(4)&&texcoords.count(5)&&texcoords.count(6)&&texcoords.count(7)&&texcoords.count(8)&&texcoords.count(9))found=true;
  }
  assert(found);
 }
}
''')
        binary = self.directory / 'effect'
        subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                        '-I'+str(ROOT / 'include'), str(source), str(ROOT / 'src/stock_shader_binary.cpp'),
                        '-o', str(binary)], check=True)
        paths = []
        with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as archive:
            for variant in (10, 11):
                name = 'shaders/std_effects/wg_deferred_decal.%d.dx9.fxo' % variant
                with zipfile.ZipFile(io.BytesIO(archive.read(name))) as outer:
                    path = self.directory / ('decal-%d.effect' % variant)
                    path.write_bytes(outer.read('effect')); paths.append(str(path))
        subprocess.run([str(binary)]+paths, check=True)


if __name__ == '__main__':
    unittest.main()
