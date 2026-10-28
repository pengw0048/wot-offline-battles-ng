"""Verify exact terrain bytes/streams against #1513 producers and DX9 consumers."""
import importlib.util
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

from native_test_compile_flags import native_test_compile_flags
from stock_terrain_vertices_oracle import native_grid, shader_inputs

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
DIMENSIONS = ((5, 5), (9, 5), (6, 5), (5, 6), (3, 3), (2, 2))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


patch = load('terrain_vertices_patch', ROOT / 'bigworld/patch_stock_terrain_vertices.py')
space_patch = load('terrain_space_patch', ROOT / 'bigworld/patch_stock_space_terrain.py')
packed = load('terrain_format_packed', REPOSITORY / 'tools/packed_xml.py')


def block(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start)
    depth = 1
    while depth:
        cursor += 1
        depth += (source[cursor] == '{') - (source[cursor] == '}')
    return source[start:cursor + 1]


def harness(height, grid):
    helper = (ROOT / 'bigworld/stock_terrain_vertices.hpp').read_text()
    helper = '\n'.join(line for line in helper.splitlines() if not line.startswith(('#include', '#pragma')))
    host = (ROOT / 'tests/stock_terrain_vertices_host.hpp').read_text()
    for key, value in {
        'HELPER': helper,
        'GRID': block(grid, 'bool GridVertexBuffer::init('),
        'GENERATE': block(height, 'void TerrainVertexBuffer::generate('),
        'HEIGHT': block(height, 'bool TerrainVertexBuffer::init('),
        'SET': block(height, 'bool TerrainVertexBuffer::set()'),
    }.items():
        host = host.replace('@' + key + '@', value)
    return host


def client(test):
    configured = os.environ.get('WOT_0922_CLIENT')
    if not configured:
        test.skipTest('WOT_0922_CLIENT required for exact #1513 evidence')
    result = Path(configured)
    if not (result / 'WorldOfTanks.exe').is_file() or not (result / 'res/packages/shaders.pkg').is_file():
        raise AssertionError('Configured WOT_0922_CLIENT is missing exact client inputs: ' + configured)
    return result


class StockTerrainVerticesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured_client = os.environ.get('WOT_0922_CLIENT')
        if configured_client:
            value = Path(configured_client)
            if not (value / 'WorldOfTanks.exe').is_file() or not (value / 'res/packages/shaders.pkg').is_file():
                raise AssertionError('Configured WOT_0922_CLIENT is missing exact client inputs: ' + configured_client)
        configured_compiler = os.environ.get('CXX')
        cls.compiler = shutil.which(configured_compiler or 'c++')
        if not cls.compiler:
            if configured_compiler:
                raise AssertionError('Configured CXX compiler is unavailable: ' + configured_compiler)
            raise unittest.SkipTest('C++ compiler required')
        configured_archive = os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE') or os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE')
        archive = Path(configured_archive or '/tmp/wot-bigworld-engine-14.4.1.tar.gz')
        if not archive.is_file():
            if configured_archive:
                raise AssertionError('Configured engine archive is missing: ' + str(archive))
            raise unittest.SkipTest('Pinned public engine archive required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='terrain-vertices-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        files = set(patch.FILES) | {
            'lib/compiled_space/terrain2_types.hpp', 'lib/compiled_space/terrain2_scene_provider.hpp',
            'lib/compiled_space/terrain2_scene_provider.cpp', 'lib/terrain/terrain_settings.hpp',
            'lib/terrain/terrain_settings.cpp', 'lib/terrain/terrain2/terrain_block2.cpp',
            'lib/terrain/terrain2/terrain_renderer2.cpp', 'lib/terrain/terrain2/terrain_renderer2.hpp',
            'lib/terrain/terrain2/terrain_index_buffer.cpp', 'lib/terrain/terrain_graphics_options.cpp',
        }
        cls.original = {}
        with tarfile.open(archive) as source:
            members = source.getnames()
            for name in files:
                matches = [entry for entry in members if entry.endswith('/programming/bigworld/' + name)]
                if len(matches) != 1:
                    raise AssertionError('Pinned archive member is not unique: ' + name)
                raw = source.extractfile(matches[0]).read()
                cls.original[name] = raw.decode('utf-8-sig')
                destination = cls.root / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(raw)
        # Exercise the requested production order, not a separate resource copy.
        space_patch.patch(cls.root)
        patch.patch(cls.root)
        before = {p: p.read_bytes() for p in cls.root.rglob('*') if p.is_file()}
        patch.patch(cls.root)
        if before != {p: p.read_bytes() for p in cls.root.rglob('*') if p.is_file()}:
            raise AssertionError('Terrain vertex patch is not idempotent')
        cls.binaries = {}
        for label, height, grid in (
            ('before', cls.original[patch.FILES[0]], cls.original[patch.FILES[1]]),
            ('after', (cls.root / patch.FILES[0]).read_text(), (cls.root / patch.FILES[1]).read_text()),
        ):
            source = cls.root / (label + '.cpp')
            source.write_text(harness(height, grid))
            binary = cls.root / (label + ('.exe' if os.name == 'nt' else ''))
            result = subprocess.run([cls.compiler, *native_test_compile_flags(cls.compiler), '-std=c++17',
                '-Wall', '-Wextra', '-Werror', '-UNDEBUG', str(source), '-o', str(binary)], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.binaries[label] = binary

    def upload(self, label, x, z):
        result = subprocess.run([str(self.binaries[label]), str(x), str(z)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        rows = result.stdout.splitlines()
        return {row.split(' ', 1)[0]: bytes.fromhex(row.split(' ', 1)[1])
                for row in rows if row.startswith(('HEIGHT ', 'GRID '))}, [row for row in rows if row.startswith('BIND ')]

    def test_real_uploads_match_half_declaration_and_correct_stream_owners(self):
        for x, z in DIMENSIONS:
            with self.subTest(resolution=(x, z)):
                rows, binds = self.upload('after', x, z)
                expected_heights = b''.join(struct.pack('<ee', -100 + i * 1.25, 70 - i * .5) for i in range(x * z))
                self.assertEqual(rows['HEIGHT'], expected_heights)
                self.assertEqual(len(rows['GRID']), x * z * 8)
                self.assertEqual(binds, ['BIND 0 8 grid', 'BIND 1 4 height'])
                phase, expected = (0, 1, 0, -1), bytearray()
                morph = (x - 1) % 4 == 0 and (z - 1) % 4 == 0
                for row in range(z):
                    for column in range(x):
                        expected += struct.pack('<4e', column / (x - 1), row / (z - 1),
                            phase[column % 4] if morph else 0, phase[row % 4] if morph else 0)
                self.assertEqual(rows['GRID'], expected)
                before, old_binds = self.upload('before', x, z)
                self.assertNotEqual(before['HEIGHT'], rows['HEIGHT'])
                self.assertNotEqual(before['GRID'], rows['GRID'])
                self.assertEqual(old_binds, ['BIND 0 8 height', 'BIND 1 8 grid'])

    def test_original_native_grid_producer_matches_every_uploaded_byte(self):
        location = client(self)
        try:
            import pefile  # noqa: F401
            import unicorn  # noqa: F401
        except ImportError:
            self.skipTest('pefile and unicorn required for the exact x86 producer oracle')
        for x, z in DIMENSIONS:
            with self.subTest(resolution=(x, z)):
                rows, unused = self.upload('after', x, z)
                self.assertEqual(rows['GRID'], native_grid(location, x, z))

    def test_exact_forward_and_deferred_vertex_shader_consumers(self):
        location = client(self)
        with zipfile.ZipFile(location / 'res/packages/shaders.pkg') as package:
            elements = dict(packed.read_packed_xml(package.read('shaders/formats/terrain.xml')).children)[b'elements'].value
            values = {name: dict(value.value.children) for name, value in elements.children}
            self.assertEqual(values[b'TEXCOORD'][b'type'].value, b'FLOAT16_4')
            self.assertEqual(values[b'POSITION'][b'type'].value, b'FLOAT16_2')
            self.assertEqual(values[b'POSITION'][b'stream'].value, 1)
            ordinary = grid_only = 0
            for infix in ('10', '11'):
                for name in ('terrain2', 'terrain2_lod_texture', 'terrain2_z_pass', 'terrain2_shadow_cast'):
                    member = 'shaders/terrain/%s.%s.dx9.fxo' % (name, infix)
                    with zipfile.ZipFile(io.BytesIO(package.read(member))) as archive:
                        rows = shader_inputs(archive.read('effect'))
                    self.assertTrue(rows, member)
                    for shader, declarations, uses in rows:
                        with self.subTest(member=member, shader=shader):
                            self.assertEqual(declarations[0], (5, 0))  # TEXCOORD0
                            if 1 in declarations:
                                ordinary += 1
                                self.assertEqual(declarations[1], (0, 0))  # POSITION0
                                self.assertEqual(uses, {0: set('xy'), 1: set('xy')})
                            else:
                                grid_only += 1
                                self.assertEqual(uses, {0: set('xyzw')})
            self.assertGreater(ordinary, 0)
            self.assertGreater(grid_only, 0)
            print('Exact terrain VS inputs: %d ordinary and %d GridOnly across forward/deferred' % (ordinary, grid_only))

    def test_upload_failure_paths_and_unchanged_cpu_geomorph_generation(self):
        result = subprocess.run([str(self.binaries['after'])], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('later upload succeeds', result.stdout)
        generated = subprocess.run([str(self.binaries['after']), 'generated'], capture_output=True, text=True)
        self.assertEqual(generated.returncode, 0, generated.stdout + generated.stderr)
        actual = bytes.fromhex(generated.stdout.strip().split(' ', 1)[1])
        bias = struct.unpack('<f', struct.pack('<f', .007))[0]
        expected = b''.join(struct.pack('<ee', -3.5, bias if row % 2 or column % 2 else 0)
                            for row in range(5) for column in range(5))
        self.assertEqual(actual, expected)
        source = (self.root / patch.FILES[0]).read_text()
        self.assertEqual(block(source, 'void TerrainVertexBuffer::generate('),
                         block(self.original[patch.FILES[0]], 'void TerrainVertexBuffer::generate('))
        self.assertIn('static float s_lodGeomorphingFix = 0.007f;', source)
        watched = (self.root / patch.FILES[2]).read_text()
        self.assertIn('sizeof( StockTerrainHeightVertex ) * vGridSize * vGridSize;', watched)


if __name__ == '__main__':
    unittest.main()
