"""Battle vehicle animation: production kernels against exact #1513 x86 bodies.

The host reference library stands in for D3DX/UCRT on both sides, so these
tests prove the original arithmetic, branch and state contracts only. Only
the exact Windows client can prove rendered motion and Wwise playback.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
CXX = os.environ.get('CXX', 'clang++')
FLAGS = ['-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-O1',
         '-I', str(ROOT / 'bigworld'), '-I', str(ROOT / 'tests')]
EXPORTS = r'''
#include "stock_battle_vehicle_animation_reference.hpp"
#define X extern "C" __attribute__((visibility("default")))
X void xTransform(float* o, const float* v, const float* m) { refTransform(o, v, m); }
X void xTransformNormal(float* o, const float* v, const float* m) { refTransformNormal(o, v, m); }
X void xNormalize(float* o, const float* v) { refNormalize(o, v); }
X void xMultiply(float* o, const float* a, const float* b) { refMultiply(o, a, b); }
X void xInverse(float* m) { refInverse(m); }
X void xRotationYawPitchRoll(float* o, float y, float p, float r) { refRotationYawPitchRoll(o, y, p, r); }
X void xRotationX(float* o, float a) { refRotationX(o, a); }
X float xAsin(float x) { return refAsin(x); }
'''

KERNEL = r'''
#include "stock_battle_vehicle_animation_reference.hpp"
#include <cassert>
#include <limits>
using namespace wot;
int main() {
    const StockAnimationMath& math = referenceAnimationMath();
    auto none = [](StockAnimationMatrix&) { return false; };
    // dt == 0 inside LOD leaves the output untouched but arms a reset.
    StockSwingingState s; s.lodSetting = 100.f; s.reset = false; s.output[0] = 7.f;
    assert(!advanceStockSwinging(s, 0.f, 10.f, none, none, math) && s.reset && s.output[0] == 7.f);
    // Outside LOD publishes identity * compensation * basis without advancing.
    s.basis[13] = 2.f; s.pitchAngle = .5f;
    assert(advanceStockSwinging(s, .016f, 101.f, none, none, math));
    assert(s.output[0] == 1.f && s.output[13] == 2.f && s.pitchAngle == .5f && s.reset);
    // Unordered LOD is inside LOD; an unbound world provider samples identity.
    assert(advanceStockSwinging(s, .016f, std::numeric_limits<float>::quiet_NaN(), none, none, math));
    assert(!s.reset && s.pitchAngle == 0.f && s.position[2] == 0.f);
    // Impulses accumulate and are consumed by one normal update.
    StockSwingingState i; i.lodSetting = 100.f; i.shotSensitivity = 1.f;
    i.pitch[1] = 36.f * 1.88f; i.pitch[4] = .05f; i.roll[2] = 36.f; i.roll[5] = .05f;
    advanceStockSwinging(i, .016f, 0.f, none, none, math);
    receiveStockSwingingImpulse(i, {{1.f, 0.f, 0.f}}, 2.f);
    receiveStockSwingingImpulse(i, {{0.f, 0.f, 1.f}}, 3.f);
    assert(i.impulse[0] == 2.f && i.impulse[2] == 3.f);
    advanceStockSwinging(i, .016f, 0.f, none, none, math);
    assert(i.impulse[0] == 0.f && i.rollVelocity < 0.f && i.pitchVelocity > 0.f);
    // A clamped angle stops its leg.
    StockSwingingState c; c.lodSetting = 1.f; c.reset = false; c.shotSensitivity = 1000.f; c.pitch[4] = .01f;
    receiveStockSwingingImpulse(c, {{0.f, 0.f, 1.f}}, 1.f);
    advanceStockSwinging(c, .2f, 0.f, none, none, math);
    assert(c.pitchAngle == .01f && c.pitchVelocity == 0.f);

    // Linked nodes: knots stay ascending; clamp below/above; NaN recomputes.
    StockLinkedNodesGroup g = makeStockLinkedNodesGroup({{-.2f, 0.f}, {1.f, 2.f}, {3.f, 3.f}});
    assert(g.knots == 2 && g.rows == 3 && g.table.size() == 9);
    assert(g.table[3] == 1.f && g.table[4] == 2.f && g.table[6] == 3.f);
    assert(!interpolateStockLinkedNodes(g, 0.f));
    assert(interpolateStockLinkedNodes(g, -.1f) && std::fabs(g.table[5] - 1.5f) < 1e-6f);
    assert(interpolateStockLinkedNodes(g, -1.f) && g.table[5] == 1.f && g.table[8] == 3.f);
    assert(interpolateStockLinkedNodes(g, 1.f) && g.table[5] == 2.f);
    assert(interpolateStockLinkedNodes(g, std::numeric_limits<float>::quiet_NaN()));
    assert(interpolateStockLinkedNodes(g, std::numeric_limits<float>::quiet_NaN()));
    assert(stockLinkedNodesSlaveAngle(g, 9) == 0.f);
    StockLinkedNodesGroup ragged = makeStockLinkedNodesGroup({{0.f, 1.f, 2.f}, {5.f, 6.f}});
    assert(ragged.knots == 2 && ragged.table[0] == 0.f && ragged.table[3] == 5.f);
    StockLinkedNodesGroup wide = makeStockLinkedNodesGroup({{0.f, 1.f}, {5.f, 6.f, 7.f}});
    assert(wide.knots == 2 && wide.table[3] == 0.f);

    // Sound: start/stop pairs, over-limit split and the dt == 0 reset.
    StockSuspensionSoundState q; q.angleLimit = .1f; q.vehicleState = 2;
    float pitch = 0.f;
    auto angle = [&](float& v) { v = pitch; return true; };
    pitch = .02f;
    auto t = advanceStockSuspensionSound(q, .1f, angle);
    assert(t.changed && t.stop == 8 && t.start == 2 && q.motion == 3);
    pitch = .3f;
    t = advanceStockSuspensionSound(q, .1f, angle);
    assert(t.changed && t.stop == 3 && t.start == 6 && q.motion == 4);
    t = advanceStockSuspensionSound(q, 0.f, angle);
    assert(t.changed && t.stop == 7 && t.start == 8 && q.motion == 0 && q.angularSpeed == 0.f);
    assert(stockSuspensionSoundLodAllows(5.f, 5.f) && !stockSuspensionSoundLodAllows(5.1f, 5.f));
    assert(!stockSuspensionSoundLodAllows(std::numeric_limits<float>::quiet_NaN(), 5.f));
    return 0;
}
'''


def client():
    return os.environ.get('WOT_0922_CLIENT')


class StockBattleVehicleAnimationKernelTest(unittest.TestCase):
    def test_production_kernels(self):
        with tempfile.TemporaryDirectory(prefix='battle-animation-kernel-') as directory:
            path = Path(directory)
            (path / 'kernel.cpp').write_text(KERNEL)
            subprocess.run([CXX, *FLAGS, str(path / 'kernel.cpp'), '-o', str(path / 'kernel')], check=True)
            subprocess.run([str(path / 'kernel')], check=True)


FAKE_HEADERS = ('pch.hpp', 'cstdmf/bw_namespace.hpp', 'pyscript/script.hpp', 'pyscript/script_math.hpp',
                'pyscript/py_callback.hpp', 'romp/resource_ref.hpp', 'romp/py_resource_refs.hpp',
                'math/boundbox.hpp')
PYC_MEMBERS = (('vehicle_systems/model_assembler', 'STOCK_MODEL_ASSEMBLER_PYC'),
               ('vehicle_systems/vehicle_assembler', 'STOCK_VEHICLE_ASSEMBLER_PYC'),
               ('vehicle_systems/CompoundAppearance', 'STOCK_COMPOUND_APPEARANCE_PYC'))


class StockBattleVehicleAnimationHostTest(unittest.TestCase):
    def test_real_cpython_producers_and_lifecycle(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files unavailable')
        import shlex
        import zipfile
        with tempfile.TemporaryDirectory(prefix='battle-animation-cpython-') as directory:
            path = Path(directory)
            for header in FAKE_HEADERS:
                (path / 'include' / header).parent.mkdir(parents=True, exist_ok=True)
                (path / 'include' / header).write_text('#pragma once\n')
            visual = (ROOT / 'bigworld/stock_vehicle_visuals.cpp').read_text()
            # The shared production float-link owner, not a fixture copy.
            owner = visual[visual.index('class StockFloatLink :'):visual.index('class StockTranslationLink')]
            sample = visual[visual.index('bool isStockFloatLink('):visual.index('bool sampleStockTranslationLink(')]
            (path / 'include/stock_float_link_owner.inc').write_text(owner + sample)
            flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                     if f != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-Wno-writable-strings',
                            '-UNDEBUG', '-ffp-contract=off', '-I', str(path / 'include'),
                            '-I', str(ROOT / 'bigworld'), '-I', str(ROOT / 'tests'),
                            str(ROOT / 'tests/stock_battle_vehicle_animation_host.cpp'),
                            *flags, *libs, '-o', str(path / 'host')], check=True)
            environment = dict(os.environ)
            if client():
                with zipfile.ZipFile(Path(client()) / 'res/packages/scripts.pkg') as archive:
                    for member, variable in PYC_MEMBERS:
                        target = path / (variable + '.pyc')
                        target.write_bytes(archive.read('scripts/client/' + member + '.pyc'))
                        environment[variable] = str(target)
            subprocess.run([str(path / 'host'), str(ROOT / 'tests/stock_battle_vehicle_animation_consumer.py')],
                           env=environment, check=True)


class StockBattleVehicleAnimationOracleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not client():
            raise unittest.SkipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_battle_vehicle_animation_oracle as oracle
        except ImportError:
            raise unittest.SkipTest('pefile and Unicorn are required for the original instruction oracle')
        import stock_battle_vehicle_animation_descriptors as descriptors
        cls.oracle = oracle
        cls.descriptors = descriptors.collect(client())
        cls.directory = tempfile.TemporaryDirectory(prefix='battle-animation-oracle-')
        path = Path(cls.directory.name)
        (path / 'exports.cpp').write_text(EXPORTS)
        subprocess.run([CXX, *FLAGS, '-shared', '-fPIC', str(path / 'exports.cpp'),
                        '-o', str(path / 'reference.dylib')], check=True)
        subprocess.run([CXX, *FLAGS, str(ROOT / 'tests/stock_battle_vehicle_animation_probe.cpp'),
                        '-o', str(path / 'probe')], check=True)
        cls.probe = path / 'probe'
        cls.machine = oracle.Machine(Path(client()) / 'WorldOfTanks.exe',
                                     oracle.Reference(path / 'reference.dylib'))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def produce(self, lines):
        output = subprocess.run([str(self.probe)], input='\n'.join(lines) + '\n', text=True,
                                capture_output=True, check=True).stdout.splitlines()
        self.assertEqual(len(output), len(lines))
        return [list(map(int, line.split())) for line in output]

    @staticmethod
    def matrix_words(matrix):
        import stock_battle_vehicle_animation_oracle as oracle
        return [oracle.bits(value) for value in (matrix or [0.] * 16)]

    def test_census_matches_reviewed_scope(self):
        found = self.descriptors
        self.assertEqual({key: len(value) for key, value in found.items()},
                         {'swinging': 684, 'drivenJoints': 47, 'hullAiming': 12, 'leveredSuspension': 12})
        shapes = {(len(rows[0]) - 1, len(rows) - 1) for unused, groups in found['drivenJoints'] for rows in groups}
        self.assertEqual(len(shapes), 13)
        for unused, groups in found['drivenJoints']:
            for rows in groups:
                self.assertEqual({len(row) for row in rows}, {len(rows[0])})
        for unused, aiming in found['hullAiming']:
            self.assertEqual([state for state, under, over in aiming['sounds']],
                             ['upStart', 'upStop', 'downStart', 'downStop'])
        self.assertEqual(sorted({len(item['levers']) for unused, item in found['leveredSuspension']}), [8, 10])

    def test_swinging_update_matches_original(self):
        oracle = self.oracle
        unique = sorted({(tuple(p), tuple(r), s) for unused, p, r, s in self.descriptors['swinging']})
        scenarios = oracle.swing_scenarios(unique)
        lines = []
        for scenario in scenarios:
            words = ['swing'] + scenario['state'] + [scenario['reset'], len(scenario['steps'])]
            for step in scenario['steps']:
                impulse = step['impulse']
                words += [oracle.bits(step['dt']), oracle.bits(step['lod']),
                          int(step['world'] is not None)] + self.matrix_words(step['world'])
                words += [int(step['compensation'] is not None)] + self.matrix_words(step['compensation'])
                words += [int(impulse is not None)] + [oracle.bits(v) for v in (impulse[0] if impulse else (0., 0., 0.))]
                words += [oracle.bits(impulse[1] if impulse else 0.)]
            lines.append(' '.join(map(str, words)))
        produced = self.produce(lines)
        original = oracle.OriginalSwinging(self.machine)
        counts = {'steps': 0, 'reset': 0, 'outside': 0, 'zero': 0, 'substeps': 0, 'gated': 0}
        for number, (scenario, words) in enumerate(zip(scenarios, produced)):
            original.load(scenario['state'], scenario['reset'])
            for index, step in enumerate(scenario['steps']):
                expected = original.step(step)
                actual = words[index * 68:(index + 1) * 68]
                where = (number, index)
                self.assertFalse(expected['thrown'], where)
                self.assertEqual(actual[1:67], expected['state'], where)
                self.assertEqual(actual[67], expected['reset'], where)
                if not actual[0]:
                    self.assertFalse(expected['output_written'], where)
                counts['steps'] += 1
                counts['zero'] += step['dt'] == 0.
                counts['outside'] += step['lod'] > 100.
                counts['substeps'] += step['dt'] > .05
                counts['gated'] += step['dt'] >= .3
                counts['reset'] += expected['reset']
        for name, value in counts.items():
            self.assertGreater(value, 0, counts)
        print('Original 0077f7b0/0077f650 matched production:', counts, 'tuples', len(unique))

    def test_swinging_without_lod_link_throws_like_original(self):
        oracle = self.oracle
        original = oracle.OriginalSwinging(self.machine)
        original.load([0] * 66, 1, lod_bound=False)
        result = original.step({'dt': .016, 'lod': 0., 'world': None, 'compensation': None, 'impulse': None})
        self.assertTrue(result['thrown'])

    def test_linked_nodes_update_matches_original(self):
        oracle = self.oracle
        groups = [rows for unused, found in self.descriptors['drivenJoints'] for rows in found]
        scenarios = oracle.linked_scenarios([[row[1:] for row in rows] for rows in groups])
        lines = []
        for scenario in scenarios:
            words = ['linked', scenario['knots'], scenario['rows']] + scenario['table']
            words += self.matrix_words(scenario['master']) + [len(scenario['slaves'])]
            for slave in scenario['slaves']:
                words += self.matrix_words(slave)
            lines.append(' '.join(map(str, words)))
        produced = self.produce(lines)
        original = oracle.OriginalLinkedNodes(self.machine)
        for number, (scenario, words) in enumerate(zip(scenarios, produced)):
            expected = original.run(scenario['knots'], scenario['rows'], scenario['table'],
                                    scenario['master'], scenario['slaves'])
            size = len(scenario['table'])
            self.assertEqual(words[1:1 + size], expected['table'], number)
            poses = words[1 + size:]
            self.assertEqual([poses[16 * i:16 * (i + 1)] for i in range(len(scenario['slaves']))],
                             expected['poses'], number)
        print('Original 0078c070 matched production for', len(scenarios), 'samples of',
              len(groups), 'authored groups')

    def test_suspension_sound_update_matches_original(self):
        oracle = self.oracle
        limits = sorted({aiming['angleLimit'] for unused, aiming in self.descriptors['hullAiming']})
        scenarios = oracle.sound_scenarios([oracle.f32(limit) for limit in limits] + [0., .02])
        lines = []
        for s in scenarios:
            words = ['sound', s['lodSetting'], s['angleLimit'], s['angle'], s['speed'], s['start'], s['motion'],
                     s['vehicleState'], s['lod'], s['dt'], int(s['body'] is not None)]
            words += self.matrix_words(s['body']) + [int(s['vehicle'] is not None)] + self.matrix_words(s['vehicle'])
            lines.append(' '.join(map(str, words)))
        produced = self.produce(lines)
        original = oracle.OriginalSuspensionSound(self.machine)
        transitions = 0
        for number, (scenario, words) in enumerate(zip(scenarios, produced)):
            expected = original.run(scenario)
            self.assertEqual(words[0:3], [expected['angle'], expected['speed'], expected['start']], number)
            self.assertEqual(words[3], expected['motion'], number)
            # Slots post their event ID (slot + 1); slot 8 posts the zero ID.
            posted = [words[5] + 1 if words[5] < 8 else 0, words[6] + 1 if words[6] < 8 else 0] if words[4] else []
            self.assertEqual(posted, expected['posted'], number)
            transitions += bool(words[4])
        self.assertGreater(transitions, 20)
        print('Original 00789630/0078a310 matched production;', transitions, 'transitions')


if __name__ == '__main__':
    unittest.main()
