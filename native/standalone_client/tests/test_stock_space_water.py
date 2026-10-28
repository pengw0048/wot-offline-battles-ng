"""Exercise the production BWWa2 state/resource/cell owner on pinned maps."""
import math
import os
from pathlib import Path
import struct
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
DRIVER = r'''
#include "stock_space_water.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>
template<class T> void write(std::ostream& out,const T& v){out.write(reinterpret_cast<const char*>(&v),sizeof(v));}
template<class T> void array(std::ostream& out,const std::vector<T>& values){
    write(out,std::uint32_t(sizeof(T)));write(out,std::uint32_t(values.size()));
    if(!values.empty())out.write(reinterpret_cast<const char*>(values.data()),sizeof(T)*values.size());
}
int main(int argc,char** argv){
    assert(argc==3);std::ifstream input(argv[1],std::ios::binary);std::ofstream output(argv[2],std::ios::binary);
    std::uint32_t count;input.read(reinterpret_cast<char*>(&count),4);
    for(std::uint32_t row=0;row<count;++row){
        std::uint32_t size,strings;input.read(reinterpret_cast<char*>(&size),4);assert(size<10000000);
        std::vector<char> bytes(size);input.read(bytes.data(),size);
        input.read(reinterpret_cast<char*>(&strings),4);std::map<std::uint32_t,std::string> table;
        for(std::uint32_t n=0;n<strings;++n){
            std::uint32_t key,len;input.read(reinterpret_cast<char*>(&key),4);input.read(reinterpret_cast<char*>(&len),4);
            std::string value(len,'\0');input.read(&value[0],len);table[key]=value;
        }
        assert(input);wot::StockSpaceWaterData owner;std::string error;
        const std::uint32_t ok=owner.read(bytes.data(),bytes.size(),error);write(output,ok);
        if(!ok){assert(!error.empty()&&owner.definitions().empty()&&owner.cells().empty()&&!owner.resourcesResolved());continue;}
        assert(error.empty());std::fill(bytes.begin(),bytes.end(),'\0');
        array(output,owner.definitions());array(output,owner.bounds());array(output,owner.cells());
        const std::uint32_t resolved=owner.resolve([&](std::uint32_t key)->const char*{
            const auto found=table.find(key);return found==table.end()?nullptr:found->second.c_str();},error);
        write(output,resolved);assert(bool(resolved)==owner.resourcesResolved());
        if(resolved){
            for(auto& value:table)value.second="overwritten original table";
            for(const auto& instance:owner.instances()){
                write(output,instance.grid.x);write(output,instance.grid.z);write(output,instance.grid.vertices);
                for(const auto& name:instance.resources){write(output,std::uint32_t(name.size()));output.write(name.data(),name.size());}
            }
        }else assert(!error.empty());
        bool threw=false;
        if(!owner.definitions().empty()){
            try{owner.definitions().front().scalar(243);}catch(const std::out_of_range&){threw=true;}
            assert(threw);
        }
        owner.clear();owner.clear();assert(owner.definitions().empty()&&owner.cells().empty()&&!owner.resourcesResolved());
    }
}
'''


def array(stride, data=b''):
    assert len(data) % stride == 0
    return struct.pack('<II', stride, len(data) // stride) + data


def definition(first, end, position=(1., 2., 3.), size=(100., 70.), tessellation=10., keys=(11, 12, 13, 14)):
    data = bytearray((n * 17 + 3) & 255 for n in range(268))
    struct.pack_into('<7f', data, 0, *position, *size, .3, tessellation)
    struct.pack_into('<6I', data, 244, *keys, first, end)
    return data


def expected(data, table):
    stride, count = struct.unpack_from('<II', data)
    assert stride == 268
    offset = 8 + count * stride
    stride2, cells = struct.unpack_from('<II', data, offset)
    assert stride2 == 24
    out = bytearray(struct.pack('<I', 1) + data)
    linked = [None] * cells
    for surface in range(count):
        first, end = struct.unpack_from('<II', data, 8 + 268 * surface + 260)
        for index in range(first, end):
            linked[index] = data[offset + 8 + index * 24:offset + 8 + (index + 1) * 24] + struct.pack('<IB3x', surface, index == first)
    out += array(32, b''.join(linked))
    ready = all(key in table for surface in range(count) for key in struct.unpack_from('<4I', data, 8 + 268 * surface + 244))
    out += struct.pack('<I', ready)
    if ready:
        f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
        for surface in range(count):
            base = 8 + surface * 268
            x, z, unused, tessellation = struct.unpack_from('<4f', data, base + 12)
            gx, gz = (int(f32(f32(math.ceil(f32(value / tessellation))) + 1.)) for value in (x, z))
            out += struct.pack('<IIQ', gx, gz, gx * gz)
            for key in struct.unpack_from('<4I', data, base + 244):
                value = table[key]
                out += struct.pack('<I', len(value)) + value
    return out


class StockSpaceWaterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        cls.temporary = tempfile.TemporaryDirectory(prefix='wot-space-water-')
        cls.work = Path(cls.temporary.name)
        (cls.work / 'probe.cpp').write_text(DRIVER)
        cls.probe = cls.work / 'probe'
        subprocess.run(['c++', '-std=c++17', '-DWOT_STOCK_SPACE_WATER_PORTABLE', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-ffp-contract=off', '-I', str(ROOT / 'bigworld'),
                        str(ROOT / 'bigworld/stock_space_water.cpp'), str(cls.work / 'probe.cpp'), '-o', str(cls.probe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_cases(self, cases):
        inputs, wanted = bytearray(struct.pack('<I', len(cases))), bytearray()
        for data, table, valid in cases:
            inputs += struct.pack('<I', len(data)) + data + struct.pack('<I', len(table))
            for key, value in table.items():
                inputs += struct.pack('<II', key, len(value)) + value
            wanted += expected(data, table) if valid else struct.pack('<I', 0)
        source, actual = self.work / 'inputs.bin', self.work / 'actual.bin'
        source.write_bytes(inputs)
        subprocess.run([str(self.probe), str(source), str(actual)], check=True)
        self.assertEqual(actual.read_bytes(), bytes(wanted))

    def test_multiple_surfaces_and_resource_ownership(self):
        data = array(268, definition(0, 2) + definition(2, 3, size=(0., 0.), keys=(14, 13, 12, 11)))
        data += array(24, struct.pack('<18f', -5, 0, -9, 1, .00001, 4, 2, 0, 3, 5, .00001, 8, 0, 0, 0, 0, 0, 0))
        table = {11: b'foam.dds', 12: b'normal_', 13: b'', 14: b'water.odata'}
        self.run_cases([(data, table, True), (data, {11: b'missing other keys'}, True), (array(268) + array(24), {}, True)])

    def test_malformed_sections_fail_without_partial_owners(self):
        data = array(268, definition(0, 1)) + array(24, struct.pack('<6f', 0, 0, 0, 1, .00001, 1))
        cases = [(data[:n], {}, False) for n in (0, 4, 7, 8, 250, len(data) - 1)]
        cases.append((data + b'\0', {}, False))
        for offset, value in ((0, 264), (4, 0xffffffff), (8 + 268, 32), (8 + 260, 2), (8 + 264, 2)):
            changed = bytearray(data)
            struct.pack_into('<I', changed, offset, value)
            cases.append((changed, {}, False))
        for offset, value in ((0, float('nan')), (12, -1.), (24, 0.), (24, float('inf'))):
            changed = bytearray(data)
            struct.pack_into('<f', changed, 8 + offset, value)
            cases.append((changed, {}, False))
        cases.append((array(268, definition(0, 1) + definition(0, 1)) + data[8 + 268:], {}, False))
        cases.append((array(268, definition(0, 0)) + data[8 + 268:], {}, False))
        self.run_cases(cases)

    def test_all_original_water_states_resources_and_cells(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0, str(REPOSITORY / 'tools'))
        from space_bin_0922 import CompiledSpace
        cases, surfaces, cells, keys = [], 0, 0, set()
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    space = CompiledSpace(archive.read(name))
                    self.assertEqual(space.sections['BWWa'].version, 2)
                    data, strings = space.section_data('BWWa'), space.section_data('BWST')
                    _, n = struct.unpack_from('<II', strings)
                    pool = 8 + 12 * n + 4
                    table = {key: strings[pool + offset:pool + offset + length]
                             for key, offset, length in (struct.unpack_from('<III', strings, 8 + 12 * i) for i in range(n))}
                    count = struct.unpack_from('<I', data, 4)[0]
                    surfaces += count
                    cells += struct.unpack_from('<I', data, 8 + 268 * count + 4)[0]
                    for i in range(count):
                        for key in struct.unpack_from('<4I', data, 8 + i * 268 + 244):
                            self.assertIn(key, table)
                            keys.add(key)
                    cases.append((data, table, True))
        self.assertEqual((len(cases), surfaces, cells, len(keys)), (52, 36, 1585, 74))
        self.run_cases(cases)
        print('Production BWWa2: 52 spaces, 36 full states, 1585 owned cells, 74 real resource keys')


if __name__ == '__main__':
    unittest.main()
