"""Vehicular.WaterSensor: production kernel, real CPython 2.7 binding, x86 oracle.

The host replaces Water, space collision and D3DX with recorded fixtures. Only
the exact Windows client can prove rendering, timing and gameplay feel.
"""
from pathlib import Path
import json
import os
import shlex
import subprocess
import tempfile
import unittest
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
CXX = os.environ.get('CXX', 'clang++')

KERNEL = r'''
#include "stock_vehicle_water_sensor_data.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace wot;
using V3 = std::array<float, 3>;
using M = std::array<float, 16>;
M translated(float x, float y, float z) { M m{}; m[0] = m[5] = m[10] = m[15] = 1; m[12] = x; m[13] = y; m[14] = z; return m; }
std::array<float, 4> transform(const V3& p, const M& m) {
    return {{p[0]*m[0]+p[1]*m[4]+p[2]*m[8]+m[12], p[0]*m[1]+p[1]*m[5]+p[2]*m[9]+m[13],
             p[0]*m[2]+p[1]*m[6]+p[2]*m[10]+m[14], p[0]*m[3]+p[1]*m[7]+p[2]*m[11]+m[15]}};
}
StockWaterSensorSample step(StockWaterSensorState& s, const StockWaterSensorConfig& c, const M& m, float h,
                            int* calls = nullptr, V3* start = nullptr, V3* end = nullptr) {
    return measureStockWaterSensor(s, c, m, [&](const V3& a, const V3& b) {
        if (calls) ++*calls; if (start) *start = a; if (end) *end = b; return h; }, transform);
}
int main() {
    StockWaterSensorConfig c; c.turretOffset = {{0.f, 1.5f, 0.f}}; c.heavyDepth = .5f;
    StockWaterSensorState s;
    int calls = 0; V3 a, b;
    auto r = step(s, c, translated(1, 2, 3), -1.f, &calls, &a, &b);
    assert(calls == 1 && a == (V3{{1, 2, 3}}) && b == (V3{{1, 3, 3}}));
    assert(s.waterHeight == -1.f && s.inWater == 0 && s.splash == 0 && s.underWater == 0);
    assert(!r.splashEdge && !r.underWaterChanged && r.hitPoint == (V3{{0, 0, 0}}));
    // Shallow entry: rising splash edge, hit at the water surface above the plane.
    r = step(s, c, translated(1, 2, 3), .25f);
    assert(r.splashEdge && !r.underWaterChanged && r.hitPoint == (V3{{1, 2.25f, 3}}));
    assert((s.inWater & 1) && (s.splash & 1) && !(s.underWater & 1));
    // Still in water: no second edge. Boundary h == turret height is not underwater.
    r = step(s, c, translated(1, 2, 3), 1.5f);
    assert(!r.splashEdge && !r.underWaterChanged && !(s.underWater & 1));
    r = step(s, c, translated(1, 2, 3), std::nextafter(1.5f, 2.f));
    assert(r.underWaterChanged && (s.underWater & 1));
    r = step(s, c, translated(1, 2, 3), 3.f);
    assert(!r.underWaterChanged);
    // Leaving water clears in/splash bits and reports the falling underwater edge.
    r = step(s, c, translated(1, 2, 3), -1.f);
    assert(r.underWaterChanged && !r.splashEdge && !(s.inWater & 1) && !(s.splash & 1) && !(s.underWater & 1));
    r = step(s, c, translated(1, 2, 3), -1.f);
    assert(!r.underWaterChanged);
    // Only exactly -1 is dry: NaN and other negatives are "in water" (00794d81).
    StockWaterSensorState n;
    r = step(n, c, translated(0, 0, 0), std::numeric_limits<float>::quiet_NaN());
    assert((n.inWater & 1) && r.splashEdge && !(n.underWater & 1) && std::isnan(r.hitPoint[1]));
    StockWaterSensorState z;
    r = step(z, c, translated(0, 0, 0), 0.f);
    assert((z.inWater & 1) && r.splashEdge);
    // History bytes wrap like the original 8-bit shifts.
    StockWaterSensorState w; w.splash = 0x80; w.inWater = 0xff; w.underWater = 0x81;
    step(w, c, translated(0, 0, 0), -1.f);
    assert(w.splash == 0 && w.inWater == 0xfe && w.underWater == 2);
    // Tilted plane: turret transformed by the full matrix, height relative to the plane origin.
    M roll{}; roll[1] = 1; roll[4] = -1; roll[10] = 1; roll[15] = 1; roll[12] = 4; roll[13] = 2; roll[14] = 0;
    StockWaterSensorState t;
    step(t, c, roll, 1e-6f);
    assert(t.underWater & 1);
    // Heavy probe goes straight down by heavyDepth from the hit point.
    assert((stockWaterSensorHeavyProbeEnd({{1, 2, 3}}, c) == V3{{1, 1.5f, 3}}));
    // LOD gate 00781f70: NaN distance passes, strict limits fail.
    assert(stockWaterSensorLodAllows(150.f, 15, 150.f, 15));
    assert(!stockWaterSensorLodAllows(150.f, 15, 150.0001f, 0));
    assert(!stockWaterSensorLodAllows(150.f, 15, 0.f, 16));
    assert(stockWaterSensorLodAllows(150.f, 15, std::numeric_limits<float>::quiet_NaN(), 0));
    // LowFrequencyUpdater: first due after one interval, stamp on the crossing frame,
    // due entries first, older stamps first, at most `limit` sorted.
    std::vector<StockLowFrequencyEntry> e;
    for (uint64_t k = 1; k <= 5; ++k) e.push_back({.25f, .25f, 0, k});
    advanceStockLowFrequency(e, .2f, 10, 3);
    for (auto& x : e) assert(x.remaining > 0.f && x.stamp == 0);
    e[3].remaining = .01f;
    advanceStockLowFrequency(e, .02f, 11, 3);
    assert(e[0].token == 4 && e[0].stamp == 11 && e[0].remaining <= 0.f);
    advanceStockLowFrequency(e, .05f, 12, 3);
    assert(e[0].token == 4);
    for (int k = 1; k < 3; ++k) assert(e[k].remaining <= 0.f && e[k].stamp == 12);
    std::vector<StockLowFrequencyEntry> one{{.1f, .25f, 0, 9}};
    advanceStockLowFrequency(one, std::numeric_limits<float>::quiet_NaN(), 3, 3);
    assert(std::isnan(one[0].remaining) && one[0].stamp == 0);
    std::vector<StockLowFrequencyEntry> none;
    advanceStockLowFrequency(none, 1.f, 1, 3);
    return 0;
}
'''

SCHEDULER = r'''
#include "stock_vehicle_water_sensor_data.hpp"
#include <cstring>
#include <iostream>
float f(unsigned bits) { float v; std::memcpy(&v, &bits, 4); return v; }
unsigned b(float v) { unsigned bits; std::memcpy(&bits, &v, 4); return bits; }
int main() {
    unsigned count, elapsed, frame, limit;
    while (std::cin >> count >> elapsed >> frame >> limit) {
        std::vector<wot::StockLowFrequencyEntry> entries(count);
        for (auto& e : entries) { unsigned r, s, stamp, token; std::cin >> r >> s >> stamp >> token;
            e = {f(r), f(s), stamp, token}; }
        wot::advanceStockLowFrequency(entries, f(elapsed), frame, limit);
        for (auto& e : entries) std::cout << b(e.remaining) << ' ' << b(e.reset) << ' ' << e.stamp << ' ' << e.token << ' ';
        std::cout << '\n';
    }
}
'''

FAKE_HEADERS = ('pch.hpp', 'cstdmf/bw_namespace.hpp', 'pyscript/script.hpp', 'pyscript/script_math.hpp',
                'moo/render_context.hpp', 'physics2/collision_callback.hpp', 'physics2/worldtri.hpp',
                'romp/water.hpp', 'space/client_space.hpp', 'space/deprecated_space_helpers.hpp',
                'space/space_manager.hpp')


class StockVehicleWaterSensorTest(unittest.TestCase):
    def test_production_kernel(self):
        with tempfile.TemporaryDirectory(prefix='water-sensor-kernel-') as directory:
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
        visual = (ROOT / 'bigworld/stock_vehicle_visuals.cpp').read_text()
        # Exercise the shared production owner rather than recreating its
        # cache-copy rules in a fixture that could always succeed.
        owner = visual[visual.index('class StockFloatLink :'):visual.index('class StockTranslationLink')]
        sample = visual[visual.index('bool isStockFloatLink('):visual.index('bool sampleStockTranslationLink(')]
        (path / 'include/stock_float_link_owner.inc').write_text(owner + sample)
        flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                 if f != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        binary = path / 'host'
        subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-Wno-writable-strings',
                        '-UNDEBUG', '-ffp-contract=off', '-I', str(path / 'include'),
                        '-I', str(ROOT / 'bigworld'), str(ROOT / 'tests/stock_vehicle_water_sensor_host.cpp'),
                        *flags, *libs, '-o', str(binary)], check=True)
        return binary

    def test_real_cpython_binding_and_consumers(self):
        with tempfile.TemporaryDirectory(prefix='water-sensor-cpython-') as directory:
            path = Path(directory)
            binary = self.build_host(path)
            environment = dict(os.environ)
            client = environment.get('WOT_0922_CLIENT')
            if client:
                with zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg') as archive:
                    for member, variable in (
                            ('vehicle_systems/model_assembler', 'STOCK_MODEL_ASSEMBLER_PYC'),
                            ('vehicle_systems/CompoundAppearance', 'STOCK_COMPOUND_APPEARANCE_PYC')):
                        target = path / (variable + '.pyc')
                        target.write_bytes(archive.read('scripts/client/' + member + '.pyc'))
                        environment[variable] = str(target)
            subprocess.run([str(binary), str(ROOT / 'tests/stock_vehicle_water_sensor_consumer.py')],
                           env=environment, check=True)

    def test_original_x86_update_matches_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_vehicle_water_sensor_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        with tempfile.TemporaryDirectory(prefix='water-sensor-oracle-') as directory:
            path = Path(directory)
            binary = self.build_host(path)
            sequences = oracle.scenarios()
            (path / 'scenarios.json').write_text(json.dumps(sequences))
            environment = dict(os.environ, WATER_SENSOR_SCENARIOS=str(path / 'scenarios.json'),
                               WATER_SENSOR_TRACES=str(path / 'traces.json'))
            subprocess.run([str(binary), str(ROOT / 'tests/stock_vehicle_water_sensor_trace.py')],
                           env=environment, check=True)
            production = json.loads((path / 'traces.json').read_text())
            original = oracle.OriginalWaterSensor(Path(client) / 'WorldOfTanks.exe')
            counts = {'steps': 0, 'splash': 0, 'heavy': 0, 'switch': 0, 'corners': 0, 'thrown': 0, 'shifted': 0}
            for number, (sequence, produced) in enumerate(zip(sequences, production)):
                original.load(sequence['config'], sequence['initial'])
                for index, (step, actual) in enumerate(zip(sequence['steps'], produced)):
                    expected = original.run(step)
                    where = (number, index)
                    counts['steps'] += 1
                    # A carrying-point hit shifts the splash byte twice (00794f41). Only
                    # bit 0 (isSplash) and the (bits & 3) == 1 edge are ever read.
                    self.assertEqual(actual['history'][1:], expected['history'][1:], where)
                    self.assertEqual(actual['history'][0] & 1, expected['history'][0] & 1, where)
                    self.assertEqual(actual['history'][0] & 3 == 1, expected['history'][0] & 3 == 1, where)
                    counts['shifted'] += actual['history'][0] != expected['history'][0]
                    self.assertEqual(actual['planeReads'], expected['planeReads'], where)
                    self.assertEqual(actual['speedReads'], expected['speedReads'], where)
                    if expected['bad_function_call']:
                        counts['thrown'] += 1
                        missing = 'sensorPlaneLink' if not step['plane'] else 'speedLink'
                        self.assertEqual(actual['errors'], ['WaterSensor: has no %s\n' % missing], where)
                        self.assertFalse(actual['active'], where)
                    else:
                        self.assertEqual(actual['errors'], [], where)
                        self.assertTrue(actual['active'], where)
                    centre = [[start, end] for start, end, flag in expected['waterCalls'] if not flag]
                    corners = [call for call in expected['waterCalls'] if call[2]]
                    self.assertEqual(actual['queries'], centre, where)
                    if not centre or step['height'] != -1.:
                        self.assertEqual(corners, [], where)
                        self.assertEqual(actual['transforms'], expected['transforms'], where)
                    else:
                        # Carrying-point probes (sign table 01a130b0, stop at the first hit)
                        # run only out of water; 00794f41 discards their result.
                        counts['corners'] += 1
                        hits = [n for n, value in enumerate(step['corners']) if value != -1.]
                        self.assertEqual(len(corners), hits[0] + 1 if hits else 4, where)
                        signs = ((1., 1.), (1., -1.), (-1., -1.), (-1., 0.))[:len(corners)]
                        carrying = sequence['config'][3:5]
                        self.assertEqual(expected['transforms'], [
                            [oracle.f32(carrying[0] * x), 0., oracle.f32(z * carrying[1])] for x, z in signs], where)
                        self.assertEqual(actual['transforms'], [], where)
                    registered = [segment[:3] for segment in expected['segments'] if step['camera'][1]]
                    self.assertTrue(all(segment[3] == 0x82 for segment in expected['segments']), where)
                    self.assertEqual(actual['segments'], registered, where)
                    self.assertEqual(actual['events'], expected['events'], where)
                    for event in expected['events']:
                        counts[event[0]] += 1
                        counts['heavy'] += event[0] == 'splash' and event[2]
            for name in ('splash', 'heavy', 'switch', 'corners', 'thrown'):
                self.assertGreater(counts[name], 0, counts)
            print('Original 00794cb0 instructions matched production:', counts)

    def test_original_x86_scheduler_matches_kernel(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_vehicle_water_sensor_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        cases = oracle.updater_cases()
        with tempfile.TemporaryDirectory(prefix='water-sensor-scheduler-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(SCHEDULER)
            subprocess.run([CXX, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                            '-I', str(ROOT / 'bigworld'), str(path / 'probe.cpp'), '-o', str(path / 'probe')],
                           check=True)
            lines = []
            for entries, elapsed, frame, limit in cases:
                words = [len(entries), oracle.bits(elapsed), frame, limit]
                for remaining, reset, stamp, token in entries:
                    words += [oracle.bits(remaining), oracle.bits(reset), stamp, token]
                lines.append(' '.join(map(str, words)))
            output = subprocess.run([str(path / 'probe')], input='\n'.join(lines) + '\n', text=True,
                                    capture_output=True, check=True).stdout.split('\n')
        original = oracle.OriginalLowFrequencyUpdater(oracle.OriginalWaterSensor(Path(client) / 'WorldOfTanks.exe'))
        for case, line in zip(cases, output):
            expected = original.run(*case)
            words = list(map(int, line.split()))
            actual = [words[n:n + 4] for n in range(0, len(words), 4)]
            self.assertEqual(actual, [[oracle.bits(e[0]), oracle.bits(e[1]), e[2], e[3]] for e in expected], case)
        print('Original 013efb40 partial sort matched the kernel for %d cases' % len(cases))

    def test_original_float_link_copy_cache_and_unsafe_null_boundary(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_vehicle_water_sensor_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for original instructions')
        original = oracle.OriginalFloatLink(Path(client) / 'WorldOfTanks.exe')
        for cached in (0, 0x80000000, 0x40c00000, 0xc0c00000, 0x7fc01234):
            copied, owner = original.clone(cached)
            self.assertEqual(copied, cached)
            self.assertEqual(owner, original.owner)
        original.clone(0x40c00000)  # independent cached6 in the cloned function
        self.assertEqual(original.sample(original.source, 8.), 0x41000000)
        self.assertEqual(original.sample(original.allocation, None), 0x40c00000)
        self.assertEqual(original.errors, 1)
        self.assertEqual(original.sample(original.source, None), 0x41000000)
        self.assertEqual(original.errors, 1)
        # NULL follows the producer's fatal/failed call boundary, not the
        # valid-object conversion-failure path that returns the cached float.
        self.assertIsNone(original.sample(original.source, 'NULL'))
        self.assertEqual(original.fault, 4)
        print('Original float-link value copy/cache/NULL boundary instructions matched')


if __name__ == '__main__':
    unittest.main()
