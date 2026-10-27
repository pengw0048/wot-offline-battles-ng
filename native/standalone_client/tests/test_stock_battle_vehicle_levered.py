"""LeveredSuspension: production kernels against exact #1513 x86 bodies.

The host reference library stands in for D3DX/UCRT on both sides, and
Unicorn's fpatan uses the host double atan2, so these tests prove the original
arithmetic, branch and layout contracts only. Only the exact Windows client
can prove x87/UCRT last-bit agreement, rendered lever motion and collider hits.
"""
from pathlib import Path
import math
import os
import random
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
#include "stock_battle_vehicle_levered_reference.hpp"
#define X extern "C" __attribute__((visibility("default")))
X void xTransform(float* o, const float* v, const float* m) { refTransform(o, v, m); }
X void xTransformNormal(float* o, const float* v, const float* m) { refTransformNormal(o, v, m); }
X void xNormalize(float* o, const float* v) { refNormalize(o, v); }
X void xMultiply(float* o, const float* a, const float* b) { refMultiply(o, a, b); }
X void xInverse(float* m) { refInverse(m); }
X void xRotationYawPitchRoll(float* o, float y, float p, float r) { refRotationYawPitchRoll(o, y, p, r); }
X void xRotationX(float* o, float a) { refRotationX(o, a); }
X float xAsin(float x) { return refAsin(x); }
X float xCos(float x) { return refCos(x); }
X float xSin(float x) { return refSin(x); }
'''

KERNEL = r'''
#include "stock_battle_vehicle_levered_reference.hpp"
#include <cassert>
#include <limits>
using namespace wot;
int main() {
    const StockLeveredMath& math = referenceLeveredMath();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // Limits: cos(min) < 0 swaps the pair and shifts both by pi; NaN is not forward.
    StockLeverLimits l = makeStockLeverLimits(-0.733f, 1.082f, math);
    assert(l.forward && l.cos1 == refCos(-0.733f) && l.sin2 == refSin(1.082f));
    l = makeStockLeverLimits(3.255f, 2.705f, math);
    assert(!l.forward && l.cos1 == refCos(2.705f + 3.14159274f) && l.sin2 == refSin(3.255f + 3.14159274f));
    assert(!makeStockLeverLimits(nan, 0.f, math).forward);
    // History shifts in the gate; byte truncation keeps the low eight bits.
    assert(advanceStockLeveredHistory(1, true) == 3 && advanceStockLeveredHistory(3, false) == 6);
    assert(advanceStockLeveredHistory(0x80, true) == 1);
    // Builder: unresolved or excess levers are rejected; speedMul clamps (NaN -> 1).
    std::vector<StockAnimationMatrix> locals(7, stockAnimationIdentity());
    StockLeveredSuspension s;
    assert(!buildStockLeveredSuspension(s, {}, locals, 10.f, math));
    assert(!buildStockLeveredSuspension(s, {{0, 1, 9, 0.f, 1.f}}, locals, 10.f, math));
    locals[0][12] = -1.f; locals[0][14] = 2.f;   // left start
    locals[3][12] = 1.f; locals[3][14] = -1.f;   // right start, sorts first
    locals[1][13] = 3.f; locals[1][14] = 4.f;    // joint length 5
    locals[2][12] = 2.f;                         // track length 2
    assert(buildStockLeveredSuspension(s, {{0, 1, 2, 0.f, 1.f}, {3, 1, 2, 0.f, 1.f}}, locals, nan, math));
    assert(s.authored[0].startNode == 3 && s.authored[1].startNode == 0 && s.split == 0);
    assert(s.levers[0].jointLength == 5.f && s.levers[0].trackLength == 2.f && s.rayLength == 10.5f);
    assert(s.interpolationSpeedMul == 1.f && s.history == 1 && s.contacts.size() == 2);
    assert(s.probes[1][0] == -1.f && s.probes[1][2] == 2.f && s.smoothed.empty());
    // Update: smoothed is seeded from contacts even outside LOD; nothing else moves.
    s.history = 2;
    StockLeverState before = s.levers[0];
    updateStockLevered(s, .016f, stockAnimationIdentity(), math);
    assert(s.smoothed.size() == 2 && s.levers[0].joint == before.joint);
    // LOD exit (history & 3 == 2) restores the authored locals in start/joint/track order.
    std::vector<StockLeveredNodeWrite> writes;
    std::vector<StockAnimationMatrix> worlds(7, stockAnimationIdentity());
    writeStockLeveredNodes(s, stockAnimationIdentity(), locals, worlds, writes, math);
    assert(writes.size() == 6 && writes[0].node == 3 && writes[1].node == 1 && writes[2].node == 2);
    return 0;
}
'''


def client():
    return os.environ.get('WOT_0922_CLIENT')


class StockBattleVehicleLeveredKernelTest(unittest.TestCase):
    def test_production_kernels(self):
        with tempfile.TemporaryDirectory(prefix='levered-kernel-') as directory:
            path = Path(directory)
            (path / 'kernel.cpp').write_text(KERNEL)
            subprocess.run([CXX, *FLAGS, str(path / 'kernel.cpp'), '-o', str(path / 'kernel')], check=True)
            subprocess.run([str(path / 'kernel')], check=True)


class StockBattleVehicleLeveredOracleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not client():
            raise unittest.SkipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_battle_vehicle_levered_oracle as oracle
            import stock_battle_vehicle_animation_oracle as base
        except ImportError:
            raise unittest.SkipTest('pefile and Unicorn are required for the original instruction oracle')
        import stock_battle_vehicle_animation_descriptors as descriptors
        cls.oracle, cls.base = oracle, base
        cls.sections = [item for unused, item in descriptors.collect(client())['leveredSuspension']]
        cls.directory = tempfile.TemporaryDirectory(prefix='levered-oracle-')
        path = Path(cls.directory.name)
        (path / 'exports.cpp').write_text(EXPORTS)
        subprocess.run([CXX, *FLAGS, '-shared', '-fPIC', str(path / 'exports.cpp'),
                        '-o', str(path / 'reference.dylib')], check=True)
        subprocess.run([CXX, *FLAGS, str(ROOT / 'tests/stock_battle_vehicle_levered_probe.cpp'),
                        '-o', str(path / 'probe')], check=True)
        cls.probe = path / 'probe'
        cls.machine = oracle.LeveredMachine(Path(client()) / 'WorldOfTanks.exe',
                                            base.Reference(path / 'reference.dylib'))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def produce(self, lines):
        output = subprocess.run([str(self.probe)], input='\n'.join(lines) + '\n', text=True,
                                capture_output=True, check=True).stdout.splitlines()
        self.assertEqual(len(output), len(lines))
        return [list(map(int, line.split())) for line in output]

    def b(self, value):
        return self.oracle.bits(value)

    def limit_pairs(self):
        pairs = {(minimum, maximum) for section in self.sections for _, _, _, minimum, maximum in section['levers']}
        f32 = self.oracle.f32
        pairs = sorted((f32(a), f32(b)) for a, b in pairs)
        edges = [(0., 0.), (-math.pi / 2, math.pi / 2), (math.pi / 2, math.pi), (f32(1.5707964), 0.),
                 (-3.5, 3.5), (float('nan'), 1.), (1., float('nan')), (100., -100.)]
        return pairs, [(f32(a), f32(b)) for a, b in edges]

    def test_limits_match_original(self):
        authored, edges = self.limit_pairs()
        self.assertEqual(len(authored), 8)
        pairs = authored + edges
        produced = self.produce(['limits %d %d' % (self.b(a), self.b(b)) for a, b in pairs])
        forward = 0
        for (minimum, maximum), words in zip(pairs, produced):
            expected = self.oracle.run_limits(self.machine, self.b(minimum), self.b(maximum))
            self.assertEqual(words, expected, (minimum, maximum))
            forward += words[4]
        self.assertTrue(0 < forward < len(pairs))
        print('Original 007860c0 matched production for %d limit pairs' % len(pairs))

    # -- builder / sort ---------------------------------------------------
    def scene_words(self, nodes, suspensions):
        words = [len(nodes)]
        for matrix in nodes:
            words += [self.b(v) for v in matrix]
        words.append(len(suspensions))
        for inputs, speed in suspensions:
            words.append(len(inputs))
            for start, joint, track, minimum, maximum in inputs:
                words += [start, joint, track, self.b(minimum), self.b(maximum)]
            words.append(self.b(speed))
        return words

    def build(self, nodes, suspensions):
        words = self.produce(['build ' + ' '.join(map(str, self.scene_words(nodes, suspensions)))])[0]
        built, at = [], 0
        for unused in suspensions:
            split, ray, speed, count = words[at:at + 4]
            at += 4
            item = {'split': split, 'rayLength': ray, 'speedMul': speed, 'authored': [], 'levers': [],
                    'anchors': [], 'probes': []}
            for unused_lever in range(count):
                record = []
                for unused_node in range(3):
                    record.append((words[at], words[at + 1:at + 17]))
                    at += 17
                item['authored'].append(record)
                item['levers'].append(words[at:at + 12])
                item['anchors'].append(words[at + 12:at + 15])
                item['probes'].append(words[at + 15:at + 18])
                at += 18
            built.append(item)
        self.assertEqual(at, len(words))
        return built

    def test_sort_and_builder_match_original(self):
        oracle, f32 = self.oracle, self.oracle.f32
        rng = random.Random(1513)
        layouts = []
        for section in self.sections:
            layouts.append(oracle.suspension_layout(rng, section['levers']))
        for count in (2, 3, 8, 10, 17, 32):  # ties, zero x and the 32-element insertion bound
            levers = [(None, 0., 1.)] * count
            nodes, inputs = oracle.suspension_layout(rng, levers)
            for start, unused_j, unused_t, unused_a, unused_b in inputs:
                nodes[start][12] = f32(rng.choice((0., -0., 1., -1., rng.uniform(-2, 2))))
                nodes[start][14] = f32(rng.choice((0., .5, -.5, rng.uniform(-2, 2))))
            layouts.append((nodes, inputs))
        checked = 0
        for nodes, inputs in layouts:
            speed = rng.choice((10., 0.5, 1., float('nan'), 25.))
            built = self.build(nodes, [(inputs, speed)])[0]
            starts = [(self.b(nodes[s][12]), self.b(nodes[s][14])) for s, unused_j, unused_t, _, _ in inputs]
            order = oracle.run_sort(self.machine, starts)
            self.assertEqual([record[0][0] for record in built['authored']], [inputs[k][0] for k in order])
            # 00788c20 split, 00786180 lengths, probes and ray length from the sorted levers.
            split = 0
            for position, k in enumerate(order):
                if nodes[inputs[k][0]][12] > 0:
                    split = position
            self.assertEqual(built['split'], split)
            for position, k in enumerate(order):
                start, joint, track = inputs[k][:3]
                for node, (index, matrix) in zip((start, joint, track), built['authored'][position]):
                    self.assertEqual((index, matrix), (node, [self.b(v) for v in nodes[node]]))
                lengths = []
                for node in (joint, track):
                    x, y, z = nodes[node][12:15]
                    lengths.append(self.b(f32(math.sqrt(f32(f32(f32(x * x) + f32(y * y)) + f32(z * z))))))
                self.assertEqual(built['levers'][position][5:7], lengths)
                limits = oracle.run_limits(self.machine, self.b(inputs[k][3]), self.b(inputs[k][4]))
                self.assertEqual(built['levers'][position][7:12], limits)
                self.assertEqual(built['levers'][position][:5], [0] * 5)
                self.assertEqual(built['anchors'][position], [self.b(v) for v in nodes[start][12:15]])
                self.assertEqual(built['probes'][position], built['anchors'][position])
            first = built['levers'][0]
            ray = f32(f32(oracle.from_bits(first[6]) + oracle.from_bits(first[5])) * 1.5)
            self.assertEqual(built['rayLength'], self.b(ray))
            self.assertEqual(built['speedMul'], self.b(speed if speed > 1. else 1.))
            checked += 1
        print('Original 00785910 sort matched production builder for %d layouts' % checked)

    # -- single bodies ------------------------------------------------------
    def frame(self, rng):
        oracle = self.oracle
        world = oracle.pose(rng.uniform(-3, 3), rng.uniform(-.4, .4), rng.uniform(-.4, .4),
                            [rng.uniform(-50, 50), rng.uniform(-5, 5), rng.uniform(-50, 50)])
        inverse = self.machine.ref.array(world, 16)
        self.machine.ref.lib.xInverse(inverse)
        return oracle.frame_words([self.b(v) for v in world], [self.b(v) for v in inverse])

    def contact(self, rng, up):
        oracle = self.oracle
        normal = oracle.unit(rng, up)
        return ([self.b(oracle.f32(rng.uniform(-2, 2))) for unused in range(3)] + [self.b(v) for v in normal] +
                [self.b(oracle.f32(rng.uniform(0., 1.5)))])

    def test_projection_matches_original(self):
        oracle, m = self.oracle, self.machine
        rng = random.Random(788830)
        m.cover([0x78890f])
        scenarios, lines = [], []
        for number in range(400):
            frame = self.frame(rng)
            anchor = [self.b(oracle.f32(rng.uniform(-2, 2))) for unused in range(3)]
            contact = self.contact(rng, rng.choice((0., 1., 3., -1.)))
            scenarios.append((frame, anchor, contact))
            lines.append('project ' + ' '.join(map(str, frame + anchor + contact)))
        for (frame, anchor, contact), words in zip(scenarios, self.produce(lines)):
            self.assertEqual(words, oracle.run_project(m, frame, anchor, contact))
        self.assertTrue(0 < m.coverage[0x78890f] < len(scenarios), m.coverage)
        print('Original 00788830 matched production; cone clamps:', m.coverage[0x78890f])

    def lever_words(self, rng, pairs):
        oracle = self.oracle
        minimum, maximum = rng.choice(pairs)
        limits = oracle.run_limits(self.machine, self.b(minimum), self.b(maximum))
        lengths = [self.b(oracle.f32(rng.uniform(.1, .6))), self.b(oracle.f32(rng.uniform(.05, .4)))]
        return [self.b(oracle.f32(rng.uniform(-1, 1))) for unused in range(5)] + lengths + limits

    def test_solve_matches_original(self):
        oracle, m = self.oracle, self.machine
        rng = random.Random(7878)
        authored, edges = self.limit_pairs()
        targets = [0x787b56, 0x787d00, 0x787d21, 0x787ce4, 0x787cd2]
        m.cover(targets)
        scenarios, lines = [], []
        for number in range(600):
            frame = self.frame(rng)
            anchor = [self.b(oracle.f32(rng.uniform(-2, 2))) for unused in range(3)]
            contact = self.contact(rng, rng.choice((0., 1., 3.)))
            lever = self.lever_words(rng, authored + edges[:5])
            scenarios.append((frame, lever, anchor, contact))
            lines.append('solve ' + ' '.join(map(str, frame + anchor + contact + lever)))
        for (frame, lever, anchor, contact), words in zip(scenarios, self.produce(lines)):
            self.assertEqual(words, oracle.run_solve(m, frame, lever, anchor, contact))
        for target in targets:
            self.assertGreater(m.coverage[target], 0, m.coverage)
        print('Original 007878d0 matched production; branch counts:', m.coverage)

    def test_chain_matches_original(self):
        oracle, m = self.oracle, self.machine
        rng = random.Random(7877)
        m.cover([0x787739, 0x787893])
        scenarios, lines = [], []
        for number in range(300):
            count = rng.choice((1, 2, 8, 10))
            contacts = [self.contact(rng, 1.)[:7] for unused in range(count)]
            i = rng.randrange(count)
            j = rng.choice((i, rng.randrange(count), count - 1, 0))
            fallback = [self.b(v) for v in oracle.pose(rng.uniform(-3, 3), .1, .1, (0., 0., 0.))]
            matrix = [self.b(v) for v in oracle.pose(rng.uniform(-3, 3), rng.uniform(-.3, .3),
                                                     rng.uniform(-.3, .3), (0., 0., 0.))]
            scenarios.append((contacts, i, j, fallback, matrix))
            words = [count] + sum(contacts, []) + [i, j] + fallback + matrix
            lines.append('chain ' + ' '.join(map(str, words)))
        for (contacts, i, j, fallback, matrix), words in zip(scenarios, self.produce(lines)):
            self.assertEqual(words, oracle.run_chain(m, contacts, i, j, fallback, matrix))
        self.assertTrue(m.coverage[0x787739] and m.coverage[0x787893], m.coverage)
        print('Original 00787710 matched production; computed/fallback:', m.coverage)

    # -- manager frames -------------------------------------------------------
    def test_manager_frames_match_original(self):
        oracle, m = self.oracle, self.machine
        f32 = oracle.f32
        eight = [s for s in self.sections if len(s['levers']) == 8]
        ten = [s for s in self.sections if len(s['levers']) == 10]
        targets = {'restore': 0x7890ad, 'alphaOne': 0x7881c2, 'alphaMin': 0x7881d7, 'keepZ': 0x7883f9,
                   'hit': 0x783f08, 'miss': 0x783f91, 'cone': 0x78890f, 'clampLow': 0x787d00,
                   'clampHigh': 0x787d21, 'chain': 0x787739, 'chainFallback': 0x787893, 'update': 0x788053,
                   'write': 0x789209}
        m.cover(list(targets.values()))
        histories, frames_run = set(), 0
        for seed in range(6):
            rng = random.Random(1513 + seed)
            sections = [rng.choice(eight), rng.choice(ten)]
            nodes = [oracle.pose(0., 0., 0., (0., 0., 0.))]
            suspensions = []
            for section in sections:
                local, inputs = oracle.suspension_layout(rng, section['levers'])
                if seed == 5:  # every start on the left: split 0 selects the 00787710 fallback
                    for start, unused_j, unused_t, unused_a, unused_b in inputs:
                        local[start][12] = -abs(local[start][12])
                shifted = [(s + len(nodes), j + len(nodes), t + len(nodes), a, b) for s, j, t, a, b in inputs]
                nodes += local
                suspensions.append((shifted, section['interpolationSpeedMul']))
            built = self.build(nodes, suspensions)
            original = oracle.OriginalLevered(m, built, [[self.b(v) for v in n] for n in nodes], len(nodes))
            frames, history = [], [1, 1]
            for number in range(rng.randint(10, 18)):
                gates = [rng.random() < .8, rng.random() < .75]
                history = [((h << 1) | g) & 0xff for h, g in zip(history, gates)]
                histories.update(h & 3 for h in history)
                answers = []
                for i, item in enumerate(built):
                    if history[i] & 1:
                        ray = oracle.from_bits(item['rayLength'])
                        for unused in item['probes']:
                            hit = rng.random() < .8
                            normal = oracle.unit(rng, rng.choice((3., 3., 1., 0.)))
                            answers.append((1 if hit else 0, self.b(f32(rng.uniform(0., ray))),
                                            [self.b(v) for v in normal]))
                frames.append({
                    'dt': self.b(f32(rng.choice((1 / 60., .033, .016, 0., .5, 1e-6, -.01, float('nan'))))),
                    'space': rng.random() < .9,
                    'worlds': [[self.b(v) for v in oracle.pose(rng.uniform(-3, 3), rng.uniform(-.3, .3),
                                                                rng.uniform(-.3, .3), [rng.uniform(-9, 9)] * 3)]
                               for unused in nodes],
                    'frames': [[self.b(v) for v in oracle.pose(rng.uniform(-3, 3), rng.uniform(-.3, .3),
                                                                rng.uniform(-.3, .3),
                                                                (rng.uniform(-50, 50), rng.uniform(0, 5),
                                                                 rng.uniform(-50, 50)))] for unused in built],
                    'boxes': [[self.b(f32(v)) for v in (-2, -.5, -3, 2, 2.5, 3)] for unused in built],
                    'gates': gates, 'answers': answers})
            words = [len(frames)]
            for frame in frames:
                words += [frame['dt'], int(frame['space'])] + sum(frame['worlds'], [])
                for i in range(len(built)):
                    words += frame['frames'][i] + frame['boxes'][i] + [int(frame['gates'][i])]
                words.append(len(frame['answers']))
                for hit, distance, normal in frame['answers']:
                    words += [hit, distance] + normal
            line = 'sequence ' + ' '.join(map(str, self.scene_words(nodes, suspensions) + words))
            produced = iter(self.produce([line])[0])
            for number, frame in enumerate(frames):
                expected = original.step(frame)
                where = (seed, number)
                calls = [c for c in expected['calls'] if c[0] != 'reset']
                self.assertEqual(next(produced), len(calls), where)
                for call in calls:
                    kind = next(produced)
                    if call[0] == 'prepare':
                        self.assertEqual(kind, 1, where)
                        self.assertEqual([next(produced) for unused in range(6)], [self.b(v) for v in call[1]], where)
                        self.assertEqual((call[2], call[3]), (original.space, 0xffffffff), where)
                    else:
                        self.assertEqual(kind, 2, where)
                        self.assertEqual([next(produced) for unused in range(7)], call[1], where)
                for i, entry in enumerate(expected['entries']):
                    n = len(built[i]['levers'])
                    self.assertEqual(next(produced), entry['history'], where)
                    self.assertEqual([[next(produced) for unused in range(8)] for unused in range(n)],
                                     entry['contacts'], where)
                    count = next(produced)
                    self.assertEqual([[next(produced) for unused in range(8)] for unused in range(count)],
                                     entry['smoothed'], where)
                    self.assertEqual([[next(produced) for unused in range(5)] for unused in range(n)],
                                     entry['levers'], where)
                    self.assertEqual([[next(produced) for unused in range(3)] for unused in range(n)],
                                     entry['probes'], where)
                self.assertEqual([[next(produced) for unused in range(16)] for unused in nodes],
                                 expected['locals'], where)
                next(produced)  # write count
                frames_run += 1
            self.assertIsNone(next(produced, None))
        self.assertEqual(histories, {0, 1, 2, 3})
        missing = [name for name, address in targets.items() if not m.coverage[address]]
        self.assertEqual(missing, [], m.coverage)
        print('Original 00783a60/00787f50/00789060 matched production over %d frames:' % frames_run,
              dict((name, m.coverage[address]) for name, address in targets.items()))


if __name__ == '__main__':
    unittest.main()
