"""Exercise the production BWLC1 reader against all original static lights."""
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
STRIDES = (48, 64, 64, 80, 8)
DRIVER = r'''
#include "wot/stock_light_binary.hpp"
#include <algorithm>
#include <cassert>
#include <fstream>
template<class T> void encode(std::ostream& out,const std::vector<T>& records) {
    const std::uint32_t stride=sizeof(T),count=std::uint32_t(records.size());
    out.write(reinterpret_cast<const char*>(&stride),4);out.write(reinterpret_cast<const char*>(&count),4);
    if(count)out.write(reinterpret_cast<const char*>(records.data()),count*sizeof(T));
}
int main(int argc,char**argv) {
    assert(argc==3);std::ifstream input(argv[1],std::ios::binary);std::ofstream output(argv[2],std::ios::binary);
    std::uint32_t count;input.read(reinterpret_cast<char*>(&count),4);
    for(std::uint32_t index=0;index<count;++index) {
        std::uint32_t size;input.read(reinterpret_cast<char*>(&size),4);assert(size<10000000);
        std::vector<unsigned char> bytes(size);input.read(reinterpret_cast<char*>(bytes.data()),size);assert(input);
        wot::StockLightBinary owner;std::string error;const std::uint32_t ok=owner.read(bytes.data(),bytes.size(),error);
        output.write(reinterpret_cast<const char*>(&ok),4);
        if(ok) {
            assert(error.empty());std::fill(bytes.begin(),bytes.end(),0);
            encode(output,owner.omnis);encode(output,owner.spots);encode(output,owner.pulseOmnis);
            encode(output,owner.pulseSpots);encode(output,owner.frames);
            assert(!owner.read(nullptr,0,error));assert(!error.empty());
            assert(owner.omnis.empty()&&owner.spots.empty()&&owner.pulseOmnis.empty()&&owner.pulseSpots.empty()&&owner.frames.empty());
        } else {assert(!error.empty());assert(owner.omnis.empty()&&owner.spots.empty()&&owner.frames.empty());}
    }
}
'''


def array(stride, data=b''):
    assert len(data) % stride == 0
    return struct.pack('<II', stride, len(data) // stride) + data


def fixture():
    omni = struct.pack('<9fIfI', 2, 3, 4, 5, 6, .2, .3, .4, .5, 99, 25, 1)
    spot = struct.pack('<13fIfI', 2, 3, 4, 0, 0, 1, 5, 6, .8, .2, .3, .4, .5, 98, 50, 0)
    pulse = struct.pack('<IIIf', 123, 0, 2, 1)
    return b''.join(array(stride, data) for stride, data in zip(STRIDES,
        (omni, spot, omni + pulse, spot + pulse, struct.pack('<4f', 0, 1, 1, .5))))


class StockLightBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-light-binary-')
        cls.root = Path(cls.temporary.name)
        (cls.root / 'probe.cpp').write_text(DRIVER)
        cls.binary = cls.root / 'probe'
        command = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'),
                   str(cls.root / 'probe.cpp'), '-o', str(cls.binary)]
        if sys.platform != 'win32':
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        compiled = subprocess.run(command, capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_cases(self, cases):
        source, target = self.root / 'input', self.root / 'output'
        source.write_bytes(struct.pack('<I', len(cases)) + b''.join(struct.pack('<I', len(data)) + data for data, expected in cases))
        result = subprocess.run([str(self.binary), str(source), str(target)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        data, cursor = target.read_bytes(), 0
        for original, expected in cases:
            ok, = struct.unpack_from('<I', data, cursor)
            cursor += 4
            self.assertEqual(bool(ok), expected)
            if ok:
                self.assertEqual(data[cursor:cursor + len(original)], original)
                cursor += len(original)
        self.assertEqual(cursor, len(data))

    def test_full_wire_and_empty_original_scene(self):
        self.run_cases([(fixture(), True), (b''.join(array(s) for s in STRIDES), True)])

    def test_malformed_spans_sizes_and_nonfinite(self):
        original = fixture()
        cases = [(original[:length], False) for length in (0, 4, 7, 8, 55, len(original) - 1)]
        cases.append((original + b'\0', False))
        cursor = 0
        for stride in STRIDES:
            bad = bytearray(original)
            struct.pack_into('<I', bad, cursor, stride + 4)
            cases.append((bad, False))
            count = struct.unpack_from('<I', original, cursor + 4)[0]
            bad = bytearray(original)
            struct.pack_into('<I', bad, cursor + 4, 0xffffffff)
            cases.append((bad, False))
            cursor += 8 + stride * count
        # Third array's frame index/count may not escape its real fifth array.
        for offset, value in ((128 + 8 + 48 + 4, 0xffffffff), (128 + 8 + 48 + 8, 0xffffffff)):
            bad = bytearray(original)
            struct.pack_into('<I', bad, offset, value)
            cases.append((bad, False))
        bad = bytearray(original)
        struct.pack_into('<f', bad, 8, float('nan'))
        cases.append((bad, False))
        self.run_cases(cases)

    def test_all_original_spaces_full_byte_roundtrip(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0, str(REPOSITORY / 'tools'))
        from space_bin_0922 import CompiledSpace
        cases, totals = [], [0] * 5
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    space = CompiledSpace(archive.read(name))
                    self.assertEqual(space.sections['BWLC'].version, 1)
                    data, cursor = space.section_data('BWLC'), 0
                    for index, stride in enumerate(STRIDES):
                        actual, count = struct.unpack_from('<II', data, cursor)
                        self.assertEqual(actual, stride)
                        totals[index] += count
                        cursor += 8 + stride * count
                    self.assertEqual(cursor, len(data))
                    cases.append((data, True))
        self.assertEqual(len(cases), 52)
        self.run_cases(cases)
        print('Production BWLC1 reader round-tripped %d original spaces; four light classes and keys: %s' % (len(cases), totals))

    def test_original_curve_instruction_match(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        try:
            import unicorn  # noqa: F401
            import pefile  # noqa: F401
        except ImportError:
            self.skipTest('Unicorn and PE reader required for original instruction audit')
        sys.path.insert(0, str(REPOSITORY / 'tools'))
        from space_bin_0922 import CompiledSpace
        from stock_light_curve_oracle import original_curve_outputs
        cases = []
        authored = []
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'):
                        continue
                    data, cursor, arrays = CompiledSpace(archive.read(name)).section_data('BWLC'), 0, []
                    for stride in STRIDES:
                        actual, count = struct.unpack_from('<II', data, cursor)
                        self.assertEqual(actual, stride)
                        arrays.append(data[cursor + 8:cursor + 8 + stride * count])
                        cursor += 8 + stride * count
                    for kind, base in ((2, 48), (3, 64)):
                        for offset in range(0, len(arrays[kind]), STRIDES[kind]):
                            animation, first, count, duration = struct.unpack_from('<IIIf', arrays[kind], offset + base)
                            self.assertNotEqual(animation, 0)
                            frames = [struct.unpack_from('<ff', arrays[4], index * 8) for index in range(first, first + count)]
                            authored.append((frames, duration))
        # Include empty/single-key, duplicate replacement, asymmetric wrap,
        # tiny intervals, and negative/non-looped timelines.
        authored.extend([([], -1), ([(3., 2.)], -1),
                         ([(1., 1.), (3., 5.), (1., 2.)], -1),
                         ([(1., 1.), (3., 5.)], 4),
                         ([(0., 0.), (.0002, 5.)], -1)])
        for frames, requested in authored:
            keys = sorted(dict(frames))
            span = keys[-1] - keys[0] if keys else 0.
            duration = (0. if abs(span) < .0004 else span) if requested == 0 else requested
            times = [-2., -.0001, 0., .0001, .25, 1.5, 2., 7.]
            for time in keys:
                times.extend((time, time + .0002, time + .0005))
            # A zero-period multi-key loop has no finite native result.
            if requested >= 0 and not duration and len(keys) > 1:
                continue
            cases.append((frames, requested >= 0, duration, times))
        expected = original_curve_outputs(Path(client) / 'WorldOfTanks.exe', cases)
        driver = self.root / 'curve.cpp'
        driver.write_text(r'''
#include "wot/stock_light_binary.hpp"
#include <cassert>
#include <fstream>
int main(int argc,char**argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);unsigned cases;in.read(reinterpret_cast<char*>(&cases),4);
for(unsigned i=0;i<cases;++i){unsigned count,looping,times;float duration;in.read(reinterpret_cast<char*>(&count),4);in.read(reinterpret_cast<char*>(&looping),4);in.read(reinterpret_cast<char*>(&duration),4);in.read(reinterpret_cast<char*>(&times),4);wot::StockLightCurve curve;
for(unsigned n=0;n<count;++n){float key,value;in.read(reinterpret_cast<char*>(&key),4);in.read(reinterpret_cast<char*>(&value),4);curve.addKey(key,value);}curve.loop(looping!=0,duration);float result=curve.getTotalTime();out.write(reinterpret_cast<char*>(&result),4);
for(unsigned n=0;n<times;++n){float time;in.read(reinterpret_cast<char*>(&time),4);result=curve.animate(time);out.write(reinterpret_cast<char*>(&result),4);}assert(in);}}
''')
        binary = self.root / 'curve'
        compiled = subprocess.run(['c++', '-std=c++14', '-ffp-contract=off', '-I', str(ROOT / 'include'),
            str(driver), '-o', str(binary)], capture_output=True, text=True)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        source, target = self.root / 'curves', self.root / 'curve-output'
        source.write_bytes(struct.pack('<I', len(cases)) + b''.join(
            struct.pack('<IIfI', len(frames), loop, duration, len(times)) +
            b''.join(struct.pack('<ff', *frame) for frame in frames) +
            struct.pack('<%df' % len(times), *times) for frames, loop, duration, times in cases))
        result = subprocess.run([str(binary), str(source), str(target)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        actual = target.read_bytes()
        self.assertEqual(len(actual), len(expected))
        mismatches = [(index, actual[index:index + 4].hex(), expected[index:index + 4].hex())
                      for index in range(0, len(actual), 4) if actual[index:index + 4] != expected[index:index + 4]]
        self.assertFalse(mismatches, repr(mismatches[:12]))
        print('Original pulse curve instructions matched %d timelines / %d scalar outputs bitwise' % (len(cases), len(actual) // 4))


if __name__ == '__main__':
    unittest.main()
