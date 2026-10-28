"""Original WTCP2 records, native laws and the actual flag scene consumer."""
import importlib.util
import io
import math
import os
from pathlib import Path
import random
import re
import shlex
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


class ControlPointTest(unittest.TestCase):
    def test_production_projected_radius_geometry_queue_and_reset(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists(): self.skipTest('Pinned public engine archive required')
        from test_stock_vector4_basic import block
        with tarfile.open(archive) as tar:
            member = next(m for m in tar if m.name.endswith('/programming/bigworld/lib/moo/device_callback.cpp'))
            dispatcher = tar.extractfile(member).read().decode('utf-8-sig')
        dispatcher = '\n'.join(block(dispatcher, 'void DeviceCallback::' + name + '(')
                               for name in ('deleteAllUnmanaged', 'createAllUnmanaged',
                                            'deleteAllManaged', 'createAllManaged'))
        with tempfile.TemporaryDirectory(prefix='control-point-radius-') as folder:
            root = Path(folder)
            production = (ROOT / 'bigworld/stock_control_point_radius.cpp').read_text()
            production += (ROOT / 'bigworld/stock_control_point_radius.hpp').read_text()
            for include in re.findall(r'^#include "([^"]+)"', production, re.MULTILINE):
                if include.startswith('stock_control_point_radius'): continue
                path = root / include
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('')
            host = (ROOT / 'tests/stock_control_point_radius_host.cpp').read_text()
            (root / 'host.cpp').write_text(host.replace('@SDK_DEVICE_CALLBACK_DISPATCH@', dispatcher))
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       *native_test_compile_flags(), '-I', str(root), '-I', str(ROOT / 'bigworld'),
                       str(root / 'host.cpp'), '-o', str(root / 'probe')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            client = os.environ.get('WOT_0922_CLIENT')
            if client and all(importlib.util.find_spec(name) for name in ('pefile', 'unicorn')):
                sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
                from prepare_native_client import OriginalResources
                from prepare_native_vehicle import xml_root
                from bw_primitives_geometry import _sections
                from stock_space_control_points_oracle import OriginalRadiusProjection
                resources = OriginalResources(client)
                name = 'content/Interface/CheckPoint/CheckPoint'
                sections = _sections(resources.read(name + '.primitives_processed'))
                source = sections['vertices']
                self.assertEqual(source[:4], b'BPVT')
                count = struct.unpack_from('<I', source, 132)[0]
                vertices = source[136:]
                self.assertEqual(len(vertices) % count, 0)
                stride = len(vertices) // count
                indices_data = sections['indices']
                indices_count, groups = struct.unpack_from('<II', indices_data, 64)
                self.assertEqual(groups, 1)
                indices = struct.unpack_from('<%dH' % indices_count, indices_data, 72)
                visual = xml_root(resources.read(name + '.visual_processed'))
                bounds = [float(v) for tag in ('min', 'max')
                          for v in visual.find('boundingBox/' + tag).text.split()]
                f = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
                sx, sz = f(100. / f(bounds[3] - bounds[0])), f(100. / f(bounds[5] - bounds[2]))
                ix, iz = f(1. / sx), f(1. / sz)
                world = [sx, 0, 0, 0, 0, 1, 0, 0, 0, 0, sz, 0, 100, 0, 200, 1]
                inverse = [ix, 0, 0, 0, 0, 1, 0, 0, 0, 0, iz, 0, f(-100 * ix), 0, f(-200 * iz), 1]
                oracle = OriginalRadiusProjection(Path(client) / 'WorldOfTanks.exe')
                for slope in (0., .001, .25, 1., 2.):
                    expected_vertices, expected_indices = oracle.project(vertices, stride, indices,
                                                                         world, inverse, .2, slope)
                    (root / 'mesh').write_bytes(struct.pack('<III7f', stride, count, indices_count, *bounds, slope) +
                        vertices + struct.pack('<%dI' % indices_count, *indices))
                    result = subprocess.run([str(root / 'probe'), str(root / 'mesh'), str(root / 'projected')],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    actual = (root / 'projected').read_bytes()
                    self.assertEqual(actual[len(vertices):], struct.pack('<%dI' % len(expected_indices), *expected_indices))
                    for index in range(count):
                        offset = index * stride
                        self.assertEqual(actual[offset + 12:offset + stride], vertices[offset + 12:offset + stride])
                        for left, right in zip(struct.unpack_from('<3f', actual, offset),
                                               struct.unpack_from('<3f', expected_vertices, offset)):
                            self.assertAlmostEqual(left, right, delta=max(.0001, abs(right)*.000002))

    def test_original_shader_consumers_and_private_bindings(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required')
        with tempfile.TemporaryDirectory(prefix='control-point-shaders-') as folder:
            root = Path(folder)
            result = subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                *native_test_compile_flags(), '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/stock_space_control_points_effect.cpp'),
                *[str(ROOT / 'src' / name) for name in ('stock_shader_binary.cpp',
                    'stock_effect_binding.cpp', 'stock_effect_reflection.cpp')], '-o', str(root / 'probe')],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with zipfile.ZipFile(Path(client) / 'res/packages/shaders.pkg') as shaders:
                for tier in ('10', '11'):
                    for name, kind in (('Flag', 'flag'), ('FlagAlpha', 'alpha'), ('Flagstaff', 'staff'), ('Radius', 'radius')):
                        path = 'shaders/controlPoint/wg_controlPoint%s.%s.dx9.fxo' % (name, tier)
                        with zipfile.ZipFile(io.BytesIO(shaders.read(path))) as effect:
                            (root / 'effect').write_bytes(effect.read('effect'))
                        result = subprocess.run([str(root / 'probe'), str(root / 'effect'), kind],
                                                capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, path + '\n' + result.stdout + result.stderr)

    def test_production_owner_original_pyc_and_sdk_draw(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists(): self.skipTest('CPython 2.7 development files required')
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists(): self.skipTest('Pinned public engine archive required')
        names = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/model/super_model.cpp')
        values = {}
        with tarfile.open(archive) as tar:
            for member in tar:
                for name in names:
                    if member.name.endswith('/programming/bigworld/' + name):
                        values[name] = tar.extractfile(member).read().decode('utf-8-sig')
                if len(values) == len(names): break
        from test_stock_hangar_presentation import between
        from test_stock_vector4_basic import block
        header, source = values[names[0]], values[names[1]]
        macros = between(header, '// Section: Macros: Module Links', '// Section: Macros: Enums', False)
        macros += between(header, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False)
        converters = ''.join(block(source, 'int Script::setData( PyObject * pObject, ' + name)
                             for name in ('int & rInt,', 'BW::string & rString,', 'Vector4 & rVector,'))
        converters += block(source, 'PyObject * Script::argCountError(')
        host = (ROOT / 'tests/stock_space_control_points_host.cpp').read_text()
        host = host.replace('@SDK_SUPER_MODEL_DRAW@', block(values[names[2]], 'void SuperModel::draw('))
        host = host.replace('@SCRIPT_BODIES@', converters).replace('@PYAUTO_MACROS@', macros)
        flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='control-point-production-') as folder:
            root = Path(folder)
            production = (ROOT / 'bigworld/stock_space_control_points.cpp').read_text()
            production += (ROOT / 'bigworld/stock_space_control_points.hpp').read_text()
            production += (ROOT / 'bigworld/stock_control_point_radius.hpp').read_text()
            production += (ROOT / 'bigworld/stock_space_sounds.hpp').read_text()
            for include in re.findall(r'^#include "([^"]+)"', production, re.MULTILINE):
                if include.startswith(('stock_space_control_points', 'stock_control_point_radius', 'stock_space_sounds')): continue
                path = root / include
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('')
            (root / 'host.cpp').write_text(host)
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                       *native_test_compile_flags(),
                       *[f for f in flags if f != '-Wstrict-prototypes' and f != '-DNDEBUG'],
                       '-I', str(root), '-I', str(ROOT / 'bigworld'), str(root / 'host.cpp'),
                       '-o', str(root / 'host'), *libs]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            environment = dict(os.environ)
            environment['ASAN_OPTIONS'] = 'detect_leaks=0'
            result = subprocess.run([str(root / 'host'), str(ROOT / 'tests/stock_space_control_points_consumer.py'),
                                     str(Path(client) / 'res/packages/scripts.pkg')], capture_output=True, text=True,
                                    env=environment)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def compile_run(self, root, source, args=()):
        (root / 'probe.cpp').write_text(source)
        command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                   *native_test_compile_flags(), '-I', str(ROOT / 'bigworld'), str(root / 'probe.cpp'),
                   '-o', str(root / 'probe')]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        result = subprocess.run([str(root / 'probe'), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_copy_patch_is_idempotent(self):
        spec = importlib.util.spec_from_file_location('flag_patch', ROOT / 'bigworld/patch_stock_space_control_points.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory(prefix='control-point-patch-') as folder:
            root = Path(folder)
            patch.apply(root)
            first = {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            patch.apply(root)
            self.assertEqual(first, {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()})
            self.assertEqual(len(first), 5)

    def test_all_original_wtcp2_sections_and_rejected_extents(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from space_bin_0922 import CompiledSpace
        from prepare_native_client import OriginalResources
        resources = OriginalResources(client)
        cases, names, paths = [], [], set()
        count = 0
        for package in (Path(client) / 'res/packages').glob('*.pkg'):
            with zipfile.ZipFile(package) as archive:
                for name in archive.namelist():
                    if not name.endswith('/space.bin'): continue
                    space = CompiledSpace(archive.read(name))
                    self.assertEqual(space.sections['WTCP'].version, 2)
                    data = space.section_data('WTCP')
                    stride, length = struct.unpack_from('<II', data)
                    self.assertEqual((stride, len(data)), (124, 8 + length * 124))
                    table = space.section_data('BWST')
                    strings_count = struct.unpack_from('<I', table, 4)[0]
                    pool = 8 + strings_count * 12 + 4
                    strings = {h: table[pool + offset:pool + offset + size].decode()
                               for h, offset, size in (struct.unpack_from('<III', table, 8 + i * 12)
                                                       for i in range(strings_count))}
                    for i in range(length):
                        paths.update(strings[h] for h in struct.unpack_from('<III', data, 8 + i * 124 + 0x60))
                    cases.append(data); names.append(name); count += length
        self.assertEqual((len(cases), count), (52, 157))
        self.assertEqual(paths, {
            'content/MilitaryEnvironment/mle000_BaseFlagstaff/normal/lod0/flag.model',
            'content/MilitaryEnvironment/mle000_BaseFlagstaff/normal/lod0/mle000_BaseFlagstaff01.model',
            'content/Interface/CheckPoint/CheckPoint.visual'})
        for resource in paths:
            self.assertTrue(resources.read(resource if resource.endswith('.model') else resource + '_processed'))
        good = next(data for data in cases if len(data) > 8)
        bad = [good[:i] for i in (0, 1, 4, 7, 8, 9, len(good) - 1)]
        bad += [struct.pack('<II', 123, 1) + good[8:132], good + b'x', struct.pack('<II', 124, 0xffffffff)]
        with tempfile.TemporaryDirectory(prefix='control-point-wire-') as folder:
            root = Path(folder)
            (root / 'input').write_bytes(b''.join(struct.pack('<II', len(data), ok) + data
                                                for ok, items in ((1, cases), (0, bad)) for data in items))
            self.compile_run(root, r'''#include "stock_space_control_points_data.hpp"
#include <cassert>
#include <fstream>
int main(int argc,char**argv){assert(argc==2);std::ifstream in(argv[1],std::ios::binary);unsigned size,expected;
wot::StockControlPointData owner;std::string error;while(in.read(reinterpret_cast<char*>(&size),4)){
in.read(reinterpret_cast<char*>(&expected),4);std::vector<char> data(size);in.read(data.data(),size);assert(in);
assert(owner.read(data.data(),size,error)==bool(expected));if(expected){assert(error.empty());
assert(data.size()==8+owner.definitions.size()*124);if(!owner.definitions.empty())assert(std::memcmp(data.data()+8,owner.definitions.data(),data.size()-8)==0);}
else assert(owner.definitions.empty()&&!error.empty());}}
''', (root / 'input',))

    def test_original_colour_and_emblem_transform(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required')
        if not all(importlib.util.find_spec(name) for name in ('pefile', 'unicorn')):
            self.skipTest('Unicorn and pefile required')
        from stock_space_control_points_oracle import colour_cases, uv_cases
        rng = random.Random(0x1513)
        colours = [tuple(rng.uniform(-2, 3) for _ in range(4)) for _ in range(250)]
        colours += [(v, v, v, v) for v in (-math.inf, -1., -0., 0., .1, .5, 1., math.inf, math.nan)]
        uv = [(0., .1, .5, .9), (0., 0., 1., 1.), (1., 1., 0., 0.), (0., 0., 0., 0.)]
        uv += [tuple(rng.uniform(-2, 3) for _ in range(4)) for _ in range(250)]
        executable = Path(client) / 'WorldOfTanks.exe'
        expected_colours = colour_cases(executable, colours)
        expected_uv = uv_cases(executable, uv)
        with tempfile.TemporaryDirectory(prefix='control-point-native-law-') as folder:
            root = Path(folder)
            (root / 'input').write_bytes(struct.pack('<II', len(colours), len(uv)) +
                b''.join(struct.pack('<4f', *v) for v in colours + uv))
            self.compile_run(root, r'''#include "stock_space_control_points_data.hpp"
#include <cassert>
#include <fstream>
int main(int argc,char**argv){assert(argc==3);std::ifstream in(argv[1],std::ios::binary);std::ofstream out(argv[2],std::ios::binary);
unsigned nc,nu;in.read(reinterpret_cast<char*>(&nc),4);in.read(reinterpret_cast<char*>(&nu),4);float value[4],output[4];
for(unsigned i=0;i<nc;++i){in.read(reinterpret_cast<char*>(value),16);auto result=wot::StockControlPointData::colour(value);out.write(reinterpret_cast<char*>(&result),4);}
for(unsigned i=0;i<nu;++i){in.read(reinterpret_cast<char*>(value),16);wot::StockControlPointData::emblemTransform(value,output);out.write(reinterpret_cast<char*>(output),16);}}
''', (root / 'input', root / 'output'))
            actual = (root / 'output').read_bytes()
            self.assertEqual(actual[:len(colours) * 4], struct.pack('<%dI' % len(colours), *expected_colours))
            for left, right in zip(struct.iter_unpack('<f', actual[len(colours) * 4:]),
                                   struct.iter_unpack('<f', b''.join(expected_uv))):
                self.assertTrue((math.isnan(left[0]) and math.isnan(right[0])) or left == right, (left, right))

    def test_original_bind_registers_sound_at_authored_bone(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required')
        if not all(importlib.util.find_spec(name) for name in ('pefile', 'unicorn')):
            self.skipTest('Unicorn and pefile required')
        from stock_space_control_points_oracle import sound_positions
        rng = random.Random(0x736f90)
        f = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
        cases = [((100., 20., 200.), (1., 17., 0.)), ((0., 0., 0.), (1., 18., 0.))]
        cases += [(tuple(f(rng.uniform(-1000, 1000)) for _ in range(3)),
                   tuple(f(rng.uniform(-20, 20)) for _ in range(3))) for _ in range(80)]
        actual = sound_positions(Path(client) / 'WorldOfTanks.exe', cases)
        self.assertEqual(actual, [tuple(f(a + b) for a, b in zip(translation, bone))
                                  for translation, bone in cases])


if __name__ == '__main__':
    unittest.main()
