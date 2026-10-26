"""Exercise the production original compiled-model wire owner."""
import importlib.util
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
STRIDES = {'BSMI': (64, 8, 4, 4, 4, 16, 4, 12, 4, 20),
           'BSMO': (8, 36, 8, 24, 8, 4, 4, 8, 28, 4, 36, 4, 68, 64, 44, 36, 8),
           'BSMA': (12, 4, 12, 64, 16)}
FIELDS = ('transforms flags masks models poseOffsets animations poseNodes triples auxiliary parameters',
          'lods colliders bspMaterials bounds info sounds lodDistancesSquared lodRenders renders renderNodes animations animationNodes nodes hardpoints falling fragile vertexSizes',
          'records effects properties matrices vectors')
DRIVER = r'''
#include "wot/stock_static_model_binary.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
template<class T> void encode(std::ostream& out,const std::vector<T>& data) {
    const std::uint32_t stride=sizeof(T),count=std::uint32_t(data.size());
    out.write(reinterpret_cast<const char*>(&stride),4);out.write(reinterpret_cast<const char*>(&count),4);
    if(count)out.write(reinterpret_cast<const char*>(data.data()),count*sizeof(T));
}
int main(int argc,char**argv){
    assert(argc==3);std::ifstream input(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);
    std::uint32_t count;input.read(reinterpret_cast<char*>(&count),4);
    for(std::uint32_t i=0;i<count;++i){
        std::vector<unsigned char> bytes[3];
        for(auto& b:bytes){std::uint32_t size;input.read(reinterpret_cast<char*>(&size),4);assert(size<20000000);
            b.resize(size);if(size)input.read(reinterpret_cast<char*>(b.data()),size);}
        assert(input);wot::StockStaticModelBinary owner;std::string error;
        const std::uint32_t ok=owner.read(bytes[0].data(),bytes[0].size(),bytes[1].data(),bytes[1].size(),bytes[2].data(),bytes[2].size(),error);
        out.write(reinterpret_cast<const char*>(&ok),4);
        if(!ok){assert(owner.instances.transforms.empty()&&owner.models.renders.empty()&&owner.materials.records.empty());
            const std::uint32_t n=std::uint32_t(error.size());out.write(reinterpret_cast<const char*>(&n),4);out.write(error.data(),n);continue;}
        assert(error.empty());
        for(std::size_t n=0;n<owner.instances.flags.size();++n){
            const auto flags=owner.instances.flags[n];
            for(std::uint32_t mask: {0u,1u,2u,0x40000000u,0xffffffffu})
                assert(owner.active(n,mask)==bool((flags.activity&1u)&&(owner.instances.masks[n]&mask)));
        }
        assert(!owner.active(owner.instances.flags.size(),0xffffffffu));
        using B=wot::StockStaticModelBinary;
        B::Bounds box{{-1,-1,-1},{1,1,1}};
        B::Matrix identity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
        auto world=identity;world[14]=20;
        const float threshold=B::lodThreshold(box,400.f);
        const float metric=B::lodMetric(box,world,identity,1.f,1.5f,1.5f);
        assert(std::fabs(threshold-metric)<1e-8f);
        assert(B::lodThreshold(box,FLT_MAX)==0.f&&B::lodThreshold(box,-1.f)==-1.f);
        assert(B::lodMetric(box,world,identity,0.f,1.5f,1.5f)==1.f);
        const float scaled=B::lodMetric(box,world,identity,.5f,1.5f,1.5f);
        assert(std::fabs(scaled-metric*.25f)<1e-8f);
        const float aspect=B::lodMetric(box,world,identity,1.f,.75f,1.5f);
        assert(std::fabs(aspect-metric*.5f)<1e-8f);
        world[14]=40;
        assert(B::lodMetric(box,world,identity,1.f,1.5f,1.5f)<metric);

        // Closing the original mapping must not invalidate any owned record.
        for(auto& b:bytes)std::fill(b.begin(),b.end(),0);
        @ENCODE@
    }
}
'''


def array(stride, values=b''):
    assert len(values) % stride == 0
    return struct.pack('<II', stride, len(values) // stride) + values


def fixture():
    matrix = struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4, 5, 6, 1)
    bounds = struct.pack('<6f', -1, -2, -3, 1, 2, 3)
    i = [matrix, struct.pack('<II', 33, 1), struct.pack('<I', 2), struct.pack('<I', 0),
         struct.pack('<i', -1), b'', b'', bytes(12), b'', bytes(20)]
    o = [struct.pack('<II', 0, 0), bounds + struct.pack('<III', 7, 0xffffffff, 0xffffffff),
         b'', bounds, bytes(8), struct.pack('<I', 8), struct.pack('<f', 3.4028234663852886e38),
         struct.pack('<II', 0, 0), struct.pack('<7I', 0xffffffff, 0xffffffff, 0, 0, 9, 10, 0),
         b'', b'', b'', b'', b'', b'', b'', b'']
    a = [struct.pack('<3i', 0, 0, 0), struct.pack('<I', 11), struct.pack('<III', 12, 1, 1), b'', b'']
    return tuple(b''.join(array(stride, rows) for stride, rows in zip(STRIDES[tag], arrays))
                 for tag, arrays in zip(STRIDES, (i, o, a)))


def offsets(data):
    result, position = [], 0
    while position < len(data):
        result.append(position)
        stride, count = struct.unpack_from('<II', data, position)
        position += 8 + stride * count
    assert position == len(data)
    return result


class StockStaticModelBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-static-model-binary-')
        cls.root = Path(cls.temporary.name)
        statements = []
        for prefix, names in zip(('instances', 'models', 'materials'), FIELDS):
            statements.extend('encode(section,owner.%s.%s);' % (prefix, field) for field in names.split())
            statements.append('size=std::uint32_t(section.str().size());out.write(reinterpret_cast<const char*>(&size),4);out<<section.str();section.str("");')
        driver = cls.root / 'probe.cpp'
        driver.write_text(DRIVER.replace('@ENCODE@', 'std::ostringstream section;std::uint32_t size;\n' + '\n'.join(statements)))
        cls.binary = cls.root / 'probe'
        command = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(driver), '-o', str(cls.binary)]
        if sys.platform != 'win32':
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        compiled = subprocess.run(command, capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_cases(self, cases):
        input_file, output_file = self.root / 'input', self.root / 'output'
        with input_file.open('wb') as output:
            output.write(struct.pack('<I', len(cases)))
            for sections, unused in cases:
                for data in sections:
                    output.write(struct.pack('<I', len(data)))
                    output.write(data)
        result = subprocess.run([str(self.binary), str(input_file), str(output_file)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        data, position = output_file.read_bytes(), 0
        for sections, expected in cases:
            ok = struct.unpack_from('<I', data, position)[0]
            position += 4
            if not ok:
                length = struct.unpack_from('<I', data, position)[0]
                position += 4
                error = data[position:position + length].decode()
                position += length
                self.assertFalse(expected, error)
            else:
                self.assertTrue(expected)
                for original in sections:
                    length = struct.unpack_from('<I', data, position)[0]
                    position += 4
                    self.assertEqual(data[position:position + length], original)
                    position += length
        self.assertEqual(position, len(data))

    def test_owned_records_mapping_mask_and_absent_effect_slot(self):
        source = fixture()
        absent = list(source)
        absent[2] = absent[2][:8] + struct.pack('<3i', -1, -1, -1) + absent[2][20:]
        self.run_cases([(source, True), (tuple(absent), True)])

    def test_truncation_stride_overflow_and_cross_section_references(self):
        original = fixture()
        cases = []
        for index, data in enumerate(original):
            for end in range(len(data)):
                changed = list(original)
                changed[index] = data[:end]
                cases.append((tuple(changed), False))
            for offset in offsets(data):
                for stride, count in ((0, 0), (0xffffffff, 0), (struct.unpack_from('<I', data, offset)[0], 0xffffffff)):
                    changed = list(original)
                    changed[index] = data[:offset] + struct.pack('<II', stride, count) + data[offset + 8:]
                    cases.append((tuple(changed), False))
            changed = list(original)
            changed[index] += b'\0'
            cases.append((tuple(changed), False))
        # References are checked against the actual owning arrays, not only the section byte count.
        for section, field, word, value in ((0, 3, 0, 1), (0, 4, 0, 0xfffffffe),
                                           (1, 0, 1, 1), (1, 8, 2, 1), (1, 8, 6, 2),
                                           (2, 0, 0, 1), (2, 0, 2, 1), (2, 2, 1, 7)):
            changed = list(original)
            offset = offsets(changed[section])[field] + 8 + word * 4
            changed[section] = changed[section][:offset] + struct.pack('<I', value) + changed[section][offset + 4:]
            cases.append((tuple(changed), False))
        self.run_cases(cases)

    def test_original_projected_lod_instruction_oracle(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client or importlib.util.find_spec('unicorn') is None or importlib.util.find_spec('pefile') is None:
            self.skipTest('Original client and Unicorn/pefile required')
        sys.path.insert(0, str(REPOSITORY / 'tools'))
        from space_bin_0922 import CompiledSpace
        from stock_static_model_oracle import original_lod_outputs
        thresholds = {}
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    data = CompiledSpace(archive.read(name)).section_data('BSMO')
                    starts = offsets(data)
                    count = struct.unpack_from('<I', data, 4)[0]
                    for model in range(count):
                        box = struct.unpack_from('<6f', data, starts[3] + 8 + model * 24)
                        if not all(box[axis] < box[axis + 3] for axis in range(3)):
                            continue
                        first, last = struct.unpack_from('<II', data, 8 + model * 8)
                        if first == 0xffffffff:
                            continue
                        for lod in range(first, last + 1):
                            squared = struct.unpack_from('<f', data, starts[6] + 8 + lod * 4)[0]
                            if 0 <= squared < 3.4028234663852886e38:
                                thresholds[(box, squared)] = None
        thresholds = list(thresholds)
        metrics = []
        identity = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)
        for box, unused in thresholds[::31]:
            extent = max(box[axis + 3] - box[axis] for axis in range(3))
            for scale, projection, offset in ((0., (1.5, 1.5), 0.), (.5, (.75, 1.5), 10.),
                                               (1., (1.5, 1.5), 0.), (2., (3., 2.), 50.)):
                world, view = list(identity), list(identity)
                world[0], world[5], world[10] = 2., -1., .5
                world[12], world[14] = offset, 100. + extent * 5
                view[12], view[13], view[14] = -3., 2., -30.
                metrics.append((box, tuple(world), tuple(view), scale, *projection))
        expected = original_lod_outputs(Path(client) / 'WorldOfTanks.exe', thresholds, metrics)
        data = b''.join(struct.pack('<I7f', 0, *box, squared) for box, squared in thresholds)
        data += b''.join(struct.pack('<I41f', 1, *box, *world, *view, scale, x, y)
                         for box, world, view, scale, x, y in metrics)
        input_file, output_file = self.root / 'lod-input', self.root / 'lod-output'
        input_file.write_bytes(data)
        binary = self.root / 'lod-probe'
        compiled = subprocess.run(['c++', '-std=c++14', '-ffp-contract=off', '-I', str(ROOT / 'include'),
            str(ROOT / 'tests/stock_static_model_lod_probe.cpp'), '-o', str(binary)], capture_output=True, text=True)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        result = subprocess.run([str(binary), str(input_file), str(output_file)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        actual = output_file.read_bytes()
        self.assertEqual(len(actual), len(expected))
        mismatches = [(index, actual[index * 4:index * 4 + 4].hex(), expected[index * 4:index * 4 + 4].hex())
            for index in range(len(expected) // 4) if actual[index * 4:index * 4 + 4] != expected[index * 4:index * 4 + 4]]
        self.assertEqual(len(mismatches), 0, repr(mismatches[:8]))
        print('Original projected LOD instruction match: %d unique authored thresholds and %d transformed metrics' %
              (len(thresholds), len(metrics)))

    def test_all_original_spaces_full_byte_roundtrip(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0, str(REPOSITORY / 'tools'))
        from space_bin_0922 import CompiledSpace
        cases, instances, renders = [], 0, 0
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    space = CompiledSpace(archive.read(name))
                    sections = tuple(space.section_data(tag) for tag in STRIDES)
                    self.assertEqual([space.sections[tag].version for tag in STRIDES], [2, 1, 1])
                    instances += struct.unpack_from('<I', sections[0], 4)[0]
                    renders += struct.unpack_from('<I', sections[1], offsets(sections[1])[8] + 4)[0]
                    cases.append((sections, True))
        self.assertTrue(cases)
        self.run_cases(cases)
        print('Production static-model reader round-tripped %d original spaces, %d instances and %d renders' % (len(cases), instances, renders))


if __name__ == '__main__':
    unittest.main()
