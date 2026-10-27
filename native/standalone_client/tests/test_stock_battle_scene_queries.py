"""Battle scene queries: production kernels, real CPython 2.7 binding, x86 oracles.

The host replaces space traversal, Water and identity with recorded fixtures.
Only the exact Windows client can prove native traversal, rendering and feel.
"""
from pathlib import Path
import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / 'bigworld'))
CXX = os.environ.get('CXX', 'clang++')

FAKE_HEADERS = (
    'pch.hpp', 'cstdmf/bw_namespace.hpp', 'cstdmf/bw_string.hpp', 'cstdmf/stdmf.hpp',
    'math/vector3.hpp', 'math/vector4.hpp', 'physics2/worldtri.hpp',
    'physics2/collision_callback.hpp', 'physics2/collision_obstacle.hpp',
    'pyscript/script.hpp', 'pyscript/script_math.hpp', 'romp/water.hpp',
    'space/forward_declarations.hpp', 'space/client_space.hpp',
    'space/deprecated_space_helpers.hpp', 'space/space_manager.hpp')

KERNEL = r'''
#include "stock_battle_scene_queries_data.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace wot;
int main(int argc, char** argv) {
    StockSegmentFilter f;
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    assert(classifyStockSegmentTriangle(f, 0x0308, 1.f, inf) == StockSegmentStep::Accept);
    f.skipFlags = 0x0300;  // material bits participate in the skip mask
    assert(classifyStockSegmentTriangle(f, 0x0108, 1.f, inf) == StockSegmentStep::Skip);
    f.skipFlags = 0;
    assert(classifyStockSegmentTriangle(f, 0, 2.f, 1.f) == StockSegmentStep::Farther);
    assert(classifyStockSegmentTriangle(f, 0, 1.f, 1.f) == StockSegmentStep::Accept);
    assert(classifyStockSegmentTriangle(f, 0, nan, 1.f) == StockSegmentStep::Accept);
    assert(classifyStockSegmentTriangle(f, 0, 1.f, nan) == StockSegmentStep::Accept);
    f.skipFlags = 8;  // skip wins over a farther distance
    assert(classifyStockSegmentTriangle(f, 8, 2.f, 1.f) == StockSegmentStep::Skip);
    f.skipFlags = 0; f.selectMask = 8;
    assert(classifyStockSegmentTriangle(f, 8, 1.f, inf) == StockSegmentStep::Accept);
    assert(classifyStockSegmentTriangle(f, 0x0800, 1.f, inf) == StockSegmentStep::Skip);
    f.selectMask = 0x108;  // only the low byte selects
    assert(classifyStockSegmentTriangle(f, 0x0100, 1.f, inf) == StockSegmentStep::Skip);
    f.selectMask = 0; f.callable = true;
    assert(classifyStockSegmentTriangle(f, 0xff, 1.f, inf) == StockSegmentStep::Skip);
    f.selectMask = -1;
    assert(classifyStockSegmentTriangle(f, 0, 1.f, inf) == StockSegmentStep::Consult);
    assert(classifyStockSegmentTriangle(f, 0, 2.f, 1.f) == StockSegmentStep::Farther);
    assert(stockTruncate(nan) == std::numeric_limits<int>::min());
    assert(stockTruncate(2147483648.f) == std::numeric_limits<int>::min());
    assert(stockTruncate(-2147483648.f) == std::numeric_limits<int>::min());
    assert(stockTruncate(-1.9f) == -1 && stockTruncate(1.9f) == 1);
    if (argc > 1) {
        // Oracle cases: gx gz sx sz tess x z count alpha...
        std::FILE* input = std::fopen(argv[1], "r");
        unsigned gx, gz, count; float sx, sz, tess, x, z;
        while (std::fscanf(input, "%u %u %a %a %a %a %a %u", &gx, &gz, &sx, &sz, &tess, &x, &z, &count) == 8) {
            std::vector<std::uint32_t> alpha(count);
            for (auto& value : alpha) assert(std::fscanf(input, "%u", &value) == 1);
            std::printf("%d\n", stockWaterEdgeAlphaAccepts(alpha.data(), alpha.size(), gx, gz, sx, sz, tess, x, z));
        }
        std::fclose(input);
    }
}
'''


def compile_kernel(path):
    (path / 'kernel.cpp').write_text(KERNEL)
    subprocess.run([CXX, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-ffp-contract=off',
                    '-I', str(ROOT / 'bigworld'), str(path / 'kernel.cpp'), '-o', str(path / 'kernel')],
                   check=True)
    return path / 'kernel'


def ground_water_query():
    """The production Water::stockGroundRayDistance definition, unchanged."""
    source = (ROOT / 'bigworld/stock_ground_effects.cpp').read_text()
    begin = source.index('float Water::stockGroundRayDistance(')
    end = source.index('\nnamespace\n{', begin)
    return 'namespace BW {\n' + source[begin:end] + '\n}\n'


ENGINE_ARCHIVE = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
ENGINE_MEMBERS = ('client/script_bigworld.cpp', 'client/script_app.cpp',
                  'lib/compiled_space/particle_system_loader.hpp',
                  'lib/compiled_space/particle_system_loader.cpp',
                  'lib/space/client_space.hpp', 'lib/space/client_space.cpp', 'lib/physics2/bsp.cpp')
_ENGINE = {}


def pinned_engine(test):
    """Pristine engine members from the pinned public archive."""
    if not _ENGINE:
        if not ENGINE_ARCHIVE.exists():
            test.skipTest('The pinned public BigWorld archive is unavailable')
        import tarfile
        with tarfile.open(ENGINE_ARCHIVE) as archive:
            for member in archive:
                parts = member.name.split('/', 1)
                if len(parts) == 2 and parts[1].startswith('programming/bigworld/'):
                    relative = parts[1][len('programming/bigworld/'):]
                    if relative in ENGINE_MEMBERS:
                        _ENGINE[relative] = archive.extractfile(member).read()
                        if len(_ENGINE) == len(ENGINE_MEMBERS):
                            break
    return _ENGINE


def patched_engine(test, root, retirement=True):
    """Apply the real space-data, space-sound and retirement patchers in build order."""
    import patch_stock_geometry_retirement
    import patch_stock_space_data
    import patch_stock_space_sounds
    for relative, data in pinned_engine(test).items():
        (root / relative).parent.mkdir(parents=True, exist_ok=True)
        (root / relative).write_bytes(data)
    patch_stock_space_data.patch(root)
    patch_stock_space_sounds.patch(root)
    if retirement:
        patch_stock_geometry_retirement.apply(root)
    return root


def function(source, signature):
    begin = source.index(signature)
    return source[begin:source.index('\n}\n', begin) + 3]


def engine_includes(test, path, retirement=True):
    root = patched_engine(test, path / 'engine', retirement)
    bsp = (root / 'lib/physics2/bsp.cpp').read_text(encoding='utf-8-sig')
    (path / 'include/stock_sdk_bsp_node.inc').write_text(
        'namespace BW {\n' + function(bsp, 'bool BSPNode::intersectsThisNode(const Vector3 &') + '}\n')
    space = (root / 'lib/space/client_space.cpp').read_text(encoding='utf-8-sig')
    parts = []
    if retirement:
        begin = space.index('namespace\n{\n// #1513 scene queries')
        retire = space.index('void ClientSpace::retireDeferredGeometry()')
        parts.append(space[begin:space.index('\n}\n', retire) + 3])
    else:
        parts.append('bool ClientSpace::geometryQueryActive() { return false; }\n'
                     'void ClientSpace::retireDeferredGeometry() {}\n')
    for signature in ('void ClientSpace::delMapping( GeometryMappingID mappingID )',
                      'float ClientSpace::collide( const Vector3 & start',
                      'float ClientSpace::collide( const WorldTriangle & triangle',
                      'void ClientSpace::clear()', 'bool ClientSpace::cleared() const'):
        parts.append(function(space, signature))
    (path / 'include/stock_client_space.inc').write_text('namespace BW {\n' + '\n'.join(parts) + '}\n')


def build_host(test, path, retirement=True):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for header in FAKE_HEADERS:
        (path / 'include' / header).parent.mkdir(parents=True, exist_ok=True)
        (path / 'include' / header).write_text('#pragma once\n')
    (path / 'include/stock_ground_water_query.inc').write_text(ground_water_query())
    engine_includes(test, path, retirement)
    flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if f != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = path / 'host'
    # AddressSanitizer reads every geometry access after a retirement.
    subprocess.run(['c++', '-std=c++14', '-g', '-fsanitize=address', '-fno-omit-frame-pointer',
                    '-Wno-deprecated-register', '-Wno-writable-strings',
                    '-Wno-unused-function', '-UNDEBUG', '-ffp-contract=off',
                    '-I', str(path / 'include'), '-I', str(ROOT / 'bigworld'),
                    str(ROOT / 'tests/stock_battle_scene_queries_host.cpp'),
                    *flags, *libs, '-o', str(binary)], check=True)
    return binary


TEARDOWN = r'''
import types, sys
import Host, _BigWorld
Math = types.ModuleType('Math')
Math.Vector3 = lambda *a: a
sys.modules['Math'] = Math
TRIANGLE = ((-1.0, 0.0, -1.0), (0.0, 0.0, 1.0), (1.0, 0.0, -1.0))
Host.addSpace(2)
Host.addSurface(2, 87 << 8, 1.0, TRIANGLE, None, (9, 0), 0)
Host.addSurface(2, 4 << 8, 2.0, TRIANGLE, None, None, 0, 0)
def teardown(*args):
    Host.clearSpace(2)
    return False
_BigWorld.wg_collideSegment(2, (0.0, 10.0, 0.0), (0.0, -10.0, 0.0), 0, teardown)
'''


class StockBattleSceneQueriesTest(unittest.TestCase):
    def test_production_kernel(self):
        with tempfile.TemporaryDirectory(prefix='scene-query-kernel-') as directory:
            subprocess.run([str(compile_kernel(Path(directory)))], check=True)

    def test_real_cpython_binding_and_consumers(self):
        with tempfile.TemporaryDirectory(prefix='scene-query-cpython-') as directory:
            path = Path(directory)
            binary = build_host(self, path)
            environment = dict(os.environ)
            client = environment.get('WOT_0922_CLIENT')
            if client:
                environment['STOCK_SCRIPTS_PKG'] = str(Path(client) / 'res/packages/scripts.pkg')
            output = subprocess.run([str(binary), str(ROOT / 'tests/stock_battle_scene_queries_consumer.py')],
                                    env=environment, check=True, capture_output=True, text=True)
            self.assertIn('scene-query binding and consumer contract passed', output.stdout)
            if client:
                self.assertIn('Exact #1513 AimingSystems', output.stdout)

    def test_patcher_declares_alpha_query_once(self):
        import patch_stock_battle_scene_queries as patch
        with tempfile.TemporaryDirectory(prefix='scene-query-patch-') as directory:
            root = Path(directory)
            header = root / 'lib/romp/water.hpp'
            header.parent.mkdir(parents=True)
            original = ('class Water {\n\tfloat stockGroundRayDistance(const Vector3& start, '
                        'const Vector3& end) const;\n' + patch.ANCHOR + '\n};\n')
            header.write_text(original)
            patch.apply(root)
            once = header.read_text()
            self.assertEqual(once.count(patch.DECLARATION), 1)
            self.assertEqual(once.count('stockGroundRayDistance('), 1)
            patch.apply(root)
            self.assertEqual(header.read_text(), once)
            header.write_text(original.replace(patch.ANCHOR, ''))
            with self.assertRaises(ValueError):
                patch.apply(root)

    def test_geometry_retirement_patch_is_exact_and_ordered(self):
        import patch_stock_geometry_retirement as patch
        import patch_stock_space_data
        with tempfile.TemporaryDirectory(prefix='scene-query-retirement-patch-') as directory:
            root = patched_engine(self, Path(directory))
            space = (root / 'lib/space/client_space.cpp').read_text()
            header = (root / 'lib/space/client_space.hpp').read_text()
            self.assertEqual(space.count('GeometryQueryScope scope;'), 2)
            clear = function(space, 'void ClientSpace::clear()')
            self.assertLess(clear.index('isCleared_ = true;'), clear.index('deferGeometry( this, true, 0 )'))
            self.assertLess(clear.index('retireStockSpaceData( id_, this );'), clear.index('this->doClear();'))
            self.assertNotIn('\tdoClear();', clear)
            self.assertIn('deferGeometry( this, false, mappingID )',
                          function(space, 'void ClientSpace::delMapping( GeometryMappingID mappingID )'))
            self.assertEqual(header.count('static void retireDeferredGeometry();'), 1)
            patch.apply(root)
            self.assertEqual((root / 'lib/space/client_space.cpp').read_text(), space)
            self.assertEqual((root / 'lib/space/client_space.hpp').read_text(), header)
        with tempfile.TemporaryDirectory(prefix='scene-query-retirement-order-') as directory:
            root = Path(directory)
            for relative, data in pinned_engine(self).items():
                (root / relative).parent.mkdir(parents=True, exist_ok=True)
                (root / relative).write_bytes(data)
            patch.apply(root)
            # The space-data patcher must run first; reversed order fails loudly.
            with self.assertRaises(ValueError):
                patch_stock_space_data.patch(root)

    def test_original_x86_water_alpha_matches_kernel(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_battle_scene_queries_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        with tempfile.TemporaryDirectory(prefix='scene-query-water-oracle-') as directory:
            path = Path(directory)
            emulator = oracle.OriginalSceneQueries(Path(client) / 'WorldOfTanks.exe')
            cases = oracle.water_cases()
            original, edges = [], []
            for case in cases:
                original.append(emulator.water_alpha(case))
                edges.append(emulator.last_edge)
            lines = []
            for case in cases:
                lines.append('%d %d %s %s %s %s %s %d %s' % (
                    case['grid'][0], case['grid'][1], float(case['size'][0]).hex(),
                    float(case['size'][1]).hex(), float(case['tess']).hex(), float(case['x']).hex(),
                    float(case['z']).hex(), len(case['alpha']), ' '.join(map(str, case['alpha']))))
            (path / 'cases.txt').write_text('\n'.join(lines) + '\n')
            kernel = compile_kernel(path)
            produced = subprocess.check_output([str(kernel), str(path / 'cases.txt')], text=True).split()
            binary = build_host(self, path)
            (path / 'waters.json').write_text(json.dumps(cases))
            environment = dict(os.environ, SCENE_QUERY_WATERS=str(path / 'waters.json'),
                               SCENE_QUERY_TRACES=str(path / 'distances.json'))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_battle_scene_queries_trace.py')],
                           env=environment, check=True, capture_output=True)
            distances = json.loads((path / 'distances.json').read_text())
            compared = 0
            for case, native, ours, edge, distance in zip(cases, original, produced, edges, distances):
                if native is None:
                    continue  # the original reads outside wAlpha_ for this case
                self.assertEqual(native, ours == '1', case)
                # The whole production Water member returns the original value.
                self.assertEqual(edge, distance, case)
                compared += 1
            accepted = sum(1 for value in original if value)
            rejected = sum(1 for value in original if value is False)
            self.assertGreater(compared, 3000)
            self.assertGreater(accepted, 500)
            self.assertGreater(rejected, 500)
            print('Original a302e0 edge-alpha branch: %d cases (%d accepted, %d rejected, %d out of table) PASS'
                  % (compared, accepted, rejected, original.count(None)))

    def test_original_x86_hit_record_and_filter_classification(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_battle_scene_queries_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        emulator = oracle.OriginalSceneQueries(Path(client) / 'WorldOfTanks.exe')
        cases = oracle.record_cases()
        with tempfile.TemporaryDirectory(prefix='scene-query-record-oracle-') as directory:
            path = Path(directory)
            binary = build_host(self, path)
            (path / 'cases.json').write_text(json.dumps(cases))
            environment = dict(os.environ, SCENE_QUERY_RECORDS=str(path / 'cases.json'),
                               SCENE_QUERY_TRACES=str(path / 'traces.json'))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_battle_scene_queries_trace.py')],
                           env=environment, check=True, capture_output=True)
            production = json.loads((path / 'traces.json').read_text())
        for case, ours in zip(cases, production):
            native = emulator.hit_record(case)
            self.assertEqual(native['distance'], case['distance'], case)
            self.assertEqual(ours['point'], [0.0, oracle.f32(10.0 - case['distance']), 0.0], case)
            for key in ('normal', 'translation', 'materialKind', 'chunkID', 'itemIndex'):
                self.assertEqual(native[key], ours[key], (key, case))
        self.assertEqual(len(production), len(cases))
        for argument, expected in oracle.filter_cases():
            self.assertEqual(emulator.classify_filter(argument), expected, argument)


    def test_unpatched_space_reads_geometry_retired_inside_a_filter(self):
        """Negative control: without the retirement patch the SDK node loop reads freed storage."""
        with tempfile.TemporaryDirectory(prefix='scene-query-unpatched-') as directory:
            path = Path(directory)
            binary = build_host(self, path, retirement=False)
            (path / 'teardown.py').write_text(TEARDOWN)
            run = subprocess.run([str(binary), str(path / 'teardown.py')], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn('heap-use-after-free', run.stderr)
            self.assertIn('BSPNode::intersectsThisNode', run.stderr)

    def test_original_x86_gun_marker_callback_matches_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_battle_scene_queries_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        emulator = oracle.OriginalSceneQueries(Path(client) / 'WorldOfTanks.exe')
        cases = oracle.gun_marker_cases()
        with tempfile.TemporaryDirectory(prefix='scene-query-gun-marker-') as directory:
            path = Path(directory)
            binary = build_host(self, path)
            (path / 'cases.json').write_text(json.dumps(cases))
            environment = dict(os.environ, SCENE_QUERY_GUN_MARKER=str(path / 'cases.json'),
                               SCENE_QUERY_TRACES=str(path / 'traces.json'))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_battle_scene_queries_trace.py')],
                           env=environment, check=True, capture_output=True)
            production = json.loads((path / 'traces.json').read_text())
        accepted = 0
        for case, ours in zip(cases, production):
            verdicts, distance = emulator.gun_marker(case['skip'], case['required'], case['surfaces'])
            self.assertEqual([verdicts, distance], ours, case)
            accepted += verdicts.count(1)
        self.assertEqual(len(production), len(cases))
        self.assertGreater(accepted, 200)
        print('Original f5c1d0 Ccb_: %d sequences, %d accepted surfaces PASS' % (len(cases), accepted))

    def test_original_x86_zero_direction_and_identity_word(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_battle_scene_queries_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        emulator = oracle.OriginalSceneQueries(Path(client) / 'WorldOfTanks.exe')
        zero = control = 0
        for vertices, point in oracle.zero_direction_cases():
            zero += emulator.triangle_hit(vertices, point, (0., 0., 0.))[0]
            control += emulator.triangle_hit(vertices, (point[0], point[1] + .5, point[2]), (0., -1., 0.))[0]
        self.assertEqual((zero, control), (0, 500))
        for chunk, item in ((0, 0), (0x81234567, 0x12345), (5, 3), (0xffffffff, 0xffff)):
            self.assertEqual(emulator.identity_record(chunk, item),
                             chunk | ((item & 0xffff) << 32) | (1 << 48))
        print('Original 01172190: 0/500 zero-direction hits, 500/500 control; f63970 identity word PASS')


if __name__ == '__main__':
    unittest.main()
