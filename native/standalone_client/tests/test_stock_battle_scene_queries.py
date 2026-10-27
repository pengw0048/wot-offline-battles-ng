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


def build_host(test, path):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for header in FAKE_HEADERS:
        (path / 'include' / header).parent.mkdir(parents=True, exist_ok=True)
        (path / 'include' / header).write_text('#pragma once\n')
    (path / 'include/stock_ground_water_query.inc').write_text(ground_water_query())
    flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if f != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = path / 'host'
    subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-Wno-writable-strings',
                    '-Wno-unused-function', '-UNDEBUG', '-ffp-contract=off',
                    '-I', str(path / 'include'), '-I', str(ROOT / 'bigworld'),
                    str(ROOT / 'tests/stock_battle_scene_queries_host.cpp'),
                    *flags, *libs, '-o', str(binary)], check=True)
    return binary


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


if __name__ == '__main__':
    unittest.main()
