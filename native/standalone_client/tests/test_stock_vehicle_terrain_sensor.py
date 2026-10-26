"""Vehicular.TerrainMatKindSensor: production kernel, real CPython 2.7 binding, x86 oracle.

The host replaces space collision, D3DX and LOD links with recorded fixtures.
Only the exact Windows client can prove native terrain queries and timing.
"""
from pathlib import Path
import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
CXX = os.environ.get('CXX', 'clang++')

KERNEL = r'''
#include "stock_vehicle_terrain_sensor_data.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace wot;
using V3 = std::array<float, 3>;
using M = std::array<float, 16>;
std::array<float, 4> transform(const V3& p, const M& m) {
    return {{p[0]*m[0]+p[1]*m[4]+p[2]*m[8]+m[12], p[0]*m[1]+p[1]*m[5]+p[2]*m[9]+m[13],
             p[0]*m[2]+p[1]*m[6]+p[2]*m[10]+m[14], p[0]*m[3]+p[1]*m[7]+p[2]*m[11]+m[15]}};
}
int main() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // 00781f70: inclusive limits; any unordered distance fails.
    assert(stockTerrainSensorLodAllows(100.f, 15, 100.f, 15));
    assert(!stockTerrainSensorLodAllows(100.f, 15, 100.0001f, 0));
    assert(!stockTerrainSensorLodAllows(100.f, 15, 0.f, 16));
    assert(!stockTerrainSensorLodAllows(100.f, 15, nan, 0));
    assert(!stockTerrainSensorLodAllows(nan, 15, 0.f, 0));
    assert(stockTerrainSensorLodAllows(100.f, 0xffffffffu, -INFINITY, 0xffffffffu));
    // 00f62400: masked triangles skip, farther ones narrow, ties keep the first.
    StockTerrainMatKindHit hit;
    assert(hit.offer(0x02, 7, 1.f) == 3 && hit.offer(0x10, 7, 1.f) == 3 && hit.offer(0x12, 7, 1.f) == 3);
    assert(!hit.found && hit.kind == 0);
    assert(hit.offer(0x08, 4, 3.f) == 1 && hit.found && hit.kind == 4);
    assert(hit.offer(0x81, 5, 3.f) == 1 && hit.kind == 4);
    assert(hit.offer(0x01, 6, 2.f) == 1 && hit.kind == 6 && hit.nearest == 2.f);
    assert(hit.offer(0x00, 9, 9.f) == 1 && hit.kind == 6);
    assert(hit.offer(0x00, 9, nan) == 1 && hit.kind == 6);
    StockTerrainMatKindHit far;
    assert(far.offer(0, 1, INFINITY) == 1 && !far.found);
    assert(far.offer(0, 2, FLT_MAX) == 1 && !far.found);
    // 00796410: transform each point, scan straight down, store 0 on a miss.
    std::vector<std::uint32_t> kinds{0xffffffffu, 0xffffffffu, 0xffffffffu};
    std::vector<V3> points{{{0, 2, 0}}, {{1, 0, 0}}, {{0, 0, 1}}};
    M m{}; m[2] = -1; m[5] = 1; m[8] = 1; m[15] = 1; m[12] = 10; m[13] = 1; m[14] = 5;
    std::vector<std::pair<V3, V3>> calls;
    sampleStockTerrainMatKinds(kinds, points, 4.f, m, transform,
        [&](const V3& a, const V3& b, std::uint32_t& kind) {
            calls.push_back({a, b}); kind = 40 + std::uint32_t(calls.size());
            return calls.size() != 2; });
    assert(calls.size() == 3 && kinds[0] == 41 && kinds[1] == 0 && kinds[2] == 43);
    assert((calls[0].first == V3{{10, 3, 5}}) && (calls[0].second == V3{{10, -1, 5}}));
    assert((calls[1].first == V3{{10, 1, 4}}) && (calls[2].first == V3{{11, 1, 5}}));
    std::vector<std::uint32_t> none;
    sampleStockTerrainMatKinds(none, std::vector<V3>(), 4.f, m, transform,
        [&](const V3&, const V3&, std::uint32_t&) { assert(false); return true; });
    return 0;
}
'''

FAKE_HEADERS = ('pch.hpp', 'cstdmf/bw_namespace.hpp', 'pyscript/script.hpp', 'pyscript/script_math.hpp',
                'moo/render_context.hpp', 'physics2/collision_callback.hpp', 'physics2/worldtri.hpp',
                'space/client_space.hpp', 'space/deprecated_space_helpers.hpp', 'space/space_manager.hpp')
PYC = (('vehicle_systems/model_assembler', 'STOCK_MODEL_ASSEMBLER_PYC'),
       ('vehicle_systems/CompoundAppearance', 'STOCK_COMPOUND_APPEARANCE_PYC'))
COMMON_PYC = (('svarog_script/auto_properties', 'STOCK_AUTO_PROPERTIES_PYC'),
              ('svarog_script/py_component_system', 'STOCK_COMPONENT_SYSTEM_PYC'))


class StockVehicleTerrainSensorTest(unittest.TestCase):
    def test_production_kernel(self):
        with tempfile.TemporaryDirectory(prefix='terrain-sensor-kernel-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(KERNEL)
            subprocess.run([CXX, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                            '-I', str(ROOT / 'bigworld'), str(path / 'probe.cpp'), '-o', str(path / 'probe')],
                           check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def build_host(self, path):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files unavailable')
        for header in FAKE_HEADERS:
            (path / 'include' / header).parent.mkdir(parents=True, exist_ok=True)
            (path / 'include' / header).write_text('#pragma once\n')
        flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                 if f != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        binary = path / 'host'
        subprocess.run(['c++', '-std=c++14', '-Wall', '-Werror', '-Wno-deprecated-register',
                        '-Wno-writable-strings', '-Wno-unused-function', '-UNDEBUG', '-ffp-contract=off',
                        '-I', str(path / 'include'), '-I', str(ROOT / 'bigworld'),
                        str(ROOT / 'tests/stock_vehicle_terrain_sensor_host.cpp'), *flags, *libs,
                        '-o', str(binary)], check=True)
        return binary

    def pyc_environment(self, path):
        environment = dict(os.environ)
        client = environment.get('WOT_0922_CLIENT')
        if client:
            with zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg') as archive:
                for prefix, members in (('scripts/client/', PYC), ('scripts/client_common/', COMMON_PYC)):
                    for member, variable in members:
                        target = path / (variable + '.pyc')
                        target.write_bytes(archive.read(prefix + member + '.pyc'))
                        environment[variable] = str(target)
        return environment

    def test_real_cpython_binding_and_consumers(self):
        with tempfile.TemporaryDirectory(prefix='terrain-sensor-cpython-') as directory:
            path = Path(directory)
            binary = self.build_host(path)
            subprocess.run([str(binary), str(ROOT / 'tests/stock_vehicle_terrain_sensor_consumer.py')],
                           env=self.pyc_environment(path), check=True)

    def test_original_x86_update_matches_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_vehicle_terrain_sensor_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        with tempfile.TemporaryDirectory(prefix='terrain-sensor-oracle-') as directory:
            path = Path(directory)
            binary = self.build_host(path)
            cases = oracle.scenarios()
            (path / 'scenarios.json').write_text(json.dumps(cases))
            environment = dict(os.environ, TERRAIN_SENSOR_SCENARIOS=str(path / 'scenarios.json'),
                               TERRAIN_SENSOR_TRACES=str(path / 'traces.json'))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_vehicle_terrain_sensor_trace.py')],
                           env=environment, check=True)
            production = json.loads((path / 'traces.json').read_text())
            original = oracle.OriginalTerrainSensor(Path(client) / 'WorldOfTanks.exe')
            counts = {'cases': 0, 'gated': 0, 'cameraless': 0, 'segments': 0, 'hits': 0, 'misses': 0}
            for number, (case, produced) in enumerate(zip(cases, production)):
                expected = original.run(case)
                self.assertEqual(expected, produced, number)
                counts['cases'] += 1
                counts['gated'] += not expected['matrixReads']
                counts['cameraless'] += case['camera'] is None
                counts['segments'] += len(expected['segments'])
                counts['hits'] += sum(1 for k in expected['kinds'] if k not in (0, 0xffffffff))
                counts['misses'] += expected['kinds'].count(0)
            for key in counts:
                self.assertGreater(counts[key], 0, key)
            print('TerrainMatKindSensor x86 oracle coverage: %s' % json.dumps(counts, sort_keys=True))

    def test_original_x86_lod_gate_and_updater(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_vehicle_terrain_sensor_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        original = oracle.OriginalTerrainSensor(Path(client) / 'WorldOfTanks.exe')
        self.assertEqual(oracle.argument_errors(original), oracle.ARGUMENT_ERRORS)
        with tempfile.TemporaryDirectory(prefix='terrain-sensor-updater-') as directory:
            path = Path(directory)
            (path / 'scheduler.cpp').write_text(oracle.SCHEDULER_PROBE)
            subprocess.run([CXX, '-std=c++14', '-Wall', '-Werror', '-ffp-contract=off', '-I', str(ROOT / 'bigworld'),
                            str(path / 'scheduler.cpp'), '-o', str(path / 'scheduler')], check=True)
            cases = oracle.updater_cases()
            stdin = ''.join(oracle.encode_updater_case(case) for case in cases)
            lines = subprocess.run([str(path / 'scheduler')], input=stdin, text=True, check=True,
                                   capture_output=True).stdout.splitlines()
            updater = oracle.OriginalLowFrequencyUpdater(original)
            for case, line in zip(cases, lines):
                self.assertEqual(oracle.encode_entries(updater.run(*case)), line, case)
            gates = oracle.lod_cases()
            (path / 'lod.cpp').write_text(oracle.LOD_PROBE)
            subprocess.run([CXX, '-std=c++14', '-Wall', '-Werror', '-I', str(ROOT / 'bigworld'),
                            str(path / 'lod.cpp'), '-o', str(path / 'lod')], check=True)
            stdin = ''.join('%d %d %d %d\n' % tuple(case) for case in gates)
            produced = subprocess.run([str(path / 'lod')], input=stdin, text=True, check=True,
                                      capture_output=True).stdout.split()
            self.assertEqual([str(int(original.lod_gate(*case))) for case in gates], produced)


if __name__ == '__main__':
    unittest.main()
