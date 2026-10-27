"""BigWorld.WGBspCollisionModel: production binding, exact consumers, x86 oracle.

The CPython host runs the production TU with exact ModelHitTester and
ClientSelectableObject PYC on real #1513 resources; a float32 brute-force
oracle over the raw BSPv2 arrays checks every hit. Original per-triangle
instructions run in Unicorn against the production core. Native BSP traversal,
D3DX and full-client picking/emblems still need the Windows client.
"""
import json
import math
import os
import random
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_stock_bigworld_ipc import FAKE_HEADERS, ROOT  # noqa: E402
import stock_bsp_collision_model_resources as resources  # noqa: E402
from stock_bsp_collision_model_resources import F32, f32  # noqa: E402

CLIENT = os.environ.get('WOT_0922_CLIENT')
ENGINE_HEADERS = ('cstdmf/bw_namespace.hpp', 'duplo/pymodel.hpp', 'math/boundbox.hpp', 'math/matrix.hpp',
                  'model/model.hpp', 'model/super_model.hpp', 'physics2/bsp.hpp',
                  'physics2/collision_callback.hpp', 'physics2/collision_obstacle.hpp',
                  'physics2/sweep_helpers.hpp', 'physics2/worldtri.hpp', 'pyscript/pyobject_plus.hpp',
                  'pyscript/script_math.hpp')
COMPONENTS = {
    'hull': 'vehicles/russian/R04_T-34/collision_client/Hull.model',
    'turret': 'vehicles/russian/R04_T-34/collision_client/Turret_01.model',
    'gun': 'vehicles/russian/R04_T-34/collision_client/Gun_01.model',
    'chassis': 'vehicles/russian/R04_T-34/collision_client/Chassis.model',
    'tigerHull': 'vehicles/german/G04_PzVI_Tiger_I/collision_client/Hull.model',
    # _VehicleAppearance.getEmblemPos/getSlotPositions load models.undamaged.
    'renderHull': 'vehicles/russian/R04_T-34/normal/lod0/Hull.model',
}
SELECTABLE = 'content/MilitaryEnvironment/mle011_GroupBoxes/normal/lod0/mle011_GroupBoxes1.model'
MISSING = 'vehicles/russian/R04_T-34/collision_client/Missing.model'
NO_BSP = 'vehicles/german/G16_PzVIB_Tiger_II/normal/lod0/Chassis.model'
WORLD = [[0.7, 0.1, -0.05], [100.0, 5.0, -40.0]]


def segments(box, rng, count=40):
    low, high = box[0], box[1]
    centre = [(a + b) * 0.5 for a, b in zip(low, high)]
    radius = math.sqrt(sum((b - a) ** 2 for a, b in zip(low, high))) * 0.5
    result = []
    for index in range(count):
        z, t = rng.uniform(-1, 1), rng.uniform(0, 2 * math.pi)
        d = [math.sqrt(1 - z * z) * math.cos(t), z, math.sqrt(1 - z * z) * math.sin(t)]
        offset = [rng.uniform(-0.5, 0.5) * (b - a) for a, b in zip(low, high)]
        origin = [c + o for c, o in zip(centre, offset)]
        result.append([[o + 2 * radius * v for o, v in zip(origin, d)], [o - 2 * radius * v for o, v in zip(origin, d)]])
    # A clear miss, a start inside the volume, a zero-length segment.
    result.append([[high[0] + 1, high[1] + 1, high[2] + 1], [high[0] + 2, high[1] + 3, high[2] + 1]])
    result.append([centre, [centre[0], centre[1] + 4 * radius, centre[2]]])
    result.append([centre, centre])
    return result


def faced(raw, direction):
    """#1513 00f45145..00f451de in float32 with the D3DX normalise model."""
    n = F32.normalise(raw)
    cosine = f32(f32(f32(n[0] * -direction[0]) + f32(n[1] * -direction[1])) + f32(n[2] * -direction[2]))
    if 0.0 > cosine:
        n, cosine = tuple(-c for c in n), -cosine
    cosine = -1.0 if -1.0 > cosine else (cosine if cosine < 1.0 else 1.0)
    return n, cosine


def segment_less(a, b):
    """00f43e30 over (distance, raw normal, kind)."""
    if abs(f32(a[0] - b[0])) >= f32(0.0004):
        return b[0] > a[0]
    if a[1] == b[1]:
        return a[2] < b[2]
    for x, y in zip(a[1], b[1]):
        if y > x:
            return True
        if x > y:
            return False
    return False


def closest(p, a, b, c):
    """00f666e0 from its decompiled control flow, in float32."""
    s, m = F32.sub, lambda x, y: f32(x * y)
    d = lambda u, v, w, x, y, z: f32(f32(m(u, x) + m(v, y)) + m(w, z))
    ab, ac, ap = s(b, a), s(c, a), s(p, a)
    d1, d2 = d(ab[0], ab[1], ab[2], *ap), d(ac[0], ac[1], ac[2], *ap)
    if d1 <= 0 and d2 <= 0:
        return a
    bp = s(p, b)
    d3 = d(ab[1], ab[0], ab[2], bp[1], bp[0], bp[2])
    d4 = d(ac[1], ac[0], ac[2], bp[1], bp[0], bp[2])
    if 0 <= d3 and d4 <= d3:
        return b
    vc = f32(m(d4, d1) - m(d3, d2))
    lerp = lambda o, e, t: tuple(f32(oo + m(ee, t)) for oo, ee in zip(o, e))
    if 0 < vc or d1 < 0 or 0 < d3:
        cp = s(p, c)
        d5 = d(ab[1], ab[0], ab[2], cp[1], cp[0], cp[2])
        d6 = f32(f32(m(cp[1], ac[1]) + m(ac[0], cp[0])) + m(cp[2], ac[2]))
        if 0 <= d6 and d5 <= d6:
            return c
        vb = f32(m(d5, d2) - m(d6, d1))
        if 0 < vb or d2 < 0 or 0 < d6:
            va, bc = f32(m(d6, d3) - m(d5, d4)), f32(d4 - d3)
            if va <= 0 and 0 <= bc and 0 <= f32(d5 - d6):
                den = f32(f32(d5 - d6) + bc)
                return b if den == 0 else lerp(b, s(c, b), f32(bc / den))
            den = f32(f32(va + vb) + vc)
            if den == 0:
                return tuple(f32(f32(m(f32(x + y), 0.25)) + z) for x, y, z in zip(ab, ac, a))
            inv = f32(1.0 / den)
            w, v = m(inv, vc), m(inv, vb)
            return tuple(f32(f32(aa + m(x, v)) + m(y, w)) for aa, x, y in zip(a, ab, ac))
        if f32(d2 - d6) != 0:
            return lerp(a, ac, f32(d2 / f32(d2 - d6)))
    elif f32(d1 - d3) != 0:
        return lerp(a, ab, f32(d1 / f32(d1 - d3)))
    return a


def sphere_oracle(triangles, centre, radius, outside):
    centre = tuple(f32(c) for c in centre)
    r2, best = f32(f32(radius) * f32(radius)), {}
    for (v0, v1, v2), flags in triangles:
        if flags & 0xff == 0xff or flags & 0x50:
            continue
        if outside:
            n = resources.raw_normal((v0, v1, v2))
            dd = F32.sub(centre, v0)
            side = f32(f32(f32(n[1] * dd[1]) + f32(dd[0] * n[0])) + f32(dd[2] * n[2]))
            if f32(-1e-05) > side:
                continue
        p = closest(centre, v0, v1, v2)
        dy, dx, dz = f32(p[1] - centre[1]), f32(p[0] - centre[0]), f32(p[2] - centre[2])
        d2 = f32(f32(f32(dy * dy) + f32(dx * dx)) + f32(dz * dz))
        if d2 > r2:
            continue
        kind = (flags >> 8) & 0xff
        if kind not in best or best[kind][0] > d2:
            best[kind] = (d2, p)
    return best


def build_host(test, path, source):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for header in FAKE_HEADERS + ENGINE_HEADERS:
        (path / 'include' / header).parent.mkdir(parents=True, exist_ok=True)
        (path / 'include' / header).write_text('#pragma once\n')
    flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if f != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = path / 'host'
    # One float32 rounding per operation, like SSE2 x86: never fuse into FMA.
    subprocess.run(['c++', '-std=c++14', '-ffp-contract=off', '-UNDEBUG', '-I', str(path / 'include'),
                    '-I', str(ROOT / 'bigworld'), '-I', str(ROOT / 'tests'), str(source), *flags,
                    '-Wno-deprecated-register', '-Wno-writable-strings', '-Wno-unused-private-field', *libs,
                    '-o', str(binary)], check=True)
    return binary


class StockBspCollisionModelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CLIENT:
            raise unittest.SkipTest('WOT_0922_CLIENT is required for exact #1513 resources and PYC')
        cls.resources = resources.Resources(CLIENT)

    def run_host(self, path, data, queries):
        binary = build_host(self, path, ROOT / 'tests/stock_bsp_collision_model_host.cpp')
        (path / 'resources.json').write_text(json.dumps(data))
        (path / 'resources.json.queries').write_text(json.dumps(queries))
        result = subprocess.run([str(binary), str(ROOT / 'tests/stock_bsp_collision_model_consumer.py'),
                                 str(path / 'resources.json'), str(Path(CLIENT) / 'res/packages/scripts.pkg'),
                                 str(path / 'results.json')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads((path / 'results.json').read_text())

    def test_exact_consumers_on_real_resources(self):
        rng = random.Random(1513)
        data, models, queries = {}, {}, {'components': [], 'segments': {}, 'spheres': {}, 'triangles': {},
                                          'world': WORLD, 'shared': COMPONENTS['hull'], 'missing': MISSING,
                                          'nobsp': NO_BSP, 'selectable': SELECTABLE}
        for name in list(COMPONENTS.values()) + [SELECTABLE, NO_BSP]:
            model = resources.load_model(self.resources, name)
            self.assertIsNotNone(model, name)
            models[name] = model
            low, high, bsp, flags = model
            data[name] = [low, high, None if bsp is None else bsp.hex(), flags]
        for component, name in list(COMPONENTS.items()) + [('selectable', SELECTABLE)]:
            low, high = models[name][0], models[name][1]
            queries['segments'][component] = segments((low, high), rng)
            if component == 'selectable':
                continue
            queries['components'].append([component, name])
            centre = [(a + b) * 0.5 for a, b in zip(low, high)]
            size = max(b - a for a, b in zip(low, high))
            queries['spheres'][component] = [[centre, 0.3 * size, False], [centre, 0.3 * size, True],
                                             [high, 0.4 * size, False], [[high[0] + 5, 0, 0], 0.5, False]]
            x = high[0] + 0.5
            square = [[x, centre[1] - 0.1, centre[2] - 0.1], [x, centre[1] + 0.1, centre[2] - 0.1],
                      [x, centre[1], centre[2] + 0.1]]
            queries['triangles'][component] = [[square, [-10.0, 0.0, 0.0]], [square, [10.0, 0.0, 0.0]],
                                               [[[v[0] + 20, v[1], v[2]] for v in square], [-1.0, 0.0, 0.0]]]
        with tempfile.TemporaryDirectory(prefix='bsp-collision-model-') as directory:
            results = self.run_host(Path(directory), data, queries)
        self.check_components(results, models, queries)
        self.check_lifetime(results)
        self.check_selectable(results, models, queries)
        print(json.dumps({'components': {k: {'segments': len(v['local']),
                                             'hitSegments': sum(1 for h in v['local'] if h),
                                             'hits': sum(len(h) for h in v['local'] if h),
                                             'sphereHits': [len(h) for h in v['sphere']],
                                             'triangles': v['triangles']}
                                         for k, v in results['components'].items()},
                          'selectablePicks': sum(1 for p in results['selectable']['picks'] if p)}))

    def check_components(self, results, models, queries):
        for component, name in queries['components']:
            entry = results['components'][component]
            low, high, bsp, flags = models[name]
            triangles = resources.bsp_triangles(bsp, flags)
            self.assertEqual(entry['triangles'], len(triangles))
            self.assertEqual(entry['bbox'][0], [f32(v) for v in low])
            self.assertEqual(entry['bbox'][1], [f32(v) for v in high])
            square = lambda v: f32(f32(f32(f32(v[0]) * f32(v[0])) + f32(f32(v[1]) * f32(v[1]))) +
                                   f32(f32(v[2]) * f32(v[2])))
            self.assertEqual(entry['bbox'][2], f32(math.sqrt(max(square(low), square(high)))))
            for (start, end), local, first in zip(queries['segments'][component], entry['local'], entry['any']):
                expected, direction = resources.segment_hits(triangles, start, end)
                if not expected:
                    self.assertIsNone(local, (component, start, end))
                    self.assertIsNone(first)
                    continue
                self.assertIsNotNone(local, (component, start, end))
                keys = []
                for distance, normal, cosine, kind in local:
                    matches = [h for h in expected if h[0] == distance and h[2] == kind and
                               faced(h[1], direction) == (tuple(normal), cosine)]
                    self.assertTrue(matches, (component, distance, normal, kind))
                    keys.append((distance, matches[0][1], kind))
                # Every authored hit survives unless it is the same seam contact.
                for distance, raw, kind in expected:
                    self.assertTrue(any(abs(distance - d) < f32(0.0004) and raw == n and kind == k
                                        for d, n, k in keys), (component, distance, raw, kind))
                for a, b in zip(keys, keys[1:]):
                    self.assertFalse(segment_less(b, a), (component, a, b))
                self.assertIn(first, [h[0] for h in expected])
            for (start, end), world, local in zip(queries['segments'][component], entry['world'], entry['local']):
                if local is None or world is None:
                    self.assertEqual(local is None, world is None)
                    continue
                matrix = entry['worldMatrix']
                for (wd, wn, wc, wk), (ld, ln, lc, lk) in zip(world, local):
                    self.assertAlmostEqual(wd, ld, places=4)
                    self.assertEqual(wk, lk)
                    expected_normal = [sum(ln[r] * matrix[r][c] for r in range(3)) for c in range(3)]
                    for a, b in zip(wn, expected_normal):
                        self.assertAlmostEqual(a, b, places=4)
            for (centre, radius, outside), hits in zip(queries['spheres'][component], entry['sphere']):
                best = sphere_oracle(triangles, centre, radius, outside)
                self.assertEqual(sorted(k for p, k in hits), sorted(best), (component, centre, radius))
                self.assertEqual(len(hits), len(set(k for p, k in hits)))
                for point, kind in hits:
                    self.assertEqual(tuple(point), tuple(best[kind][1]), (component, kind))
            self.assertEqual(entry['triangle'], [True, False, False], component)
            self.assertNotEqual(entry['native'][0], 0)
            self.assertEqual(entry['native'][2], name)
            self.assertEqual(entry['released'], [False, False])
        self.assertTrue(any(h for h in results['components']['renderHull']['local']))

    def check_lifetime(self, results):
        shared = results['shared']
        self.assertTrue(shared['sameTree'] and shared['sameModel'])
        self.assertEqual(shared['live'], {COMPONENTS['hull']: 2})
        self.assertEqual(shared['afterFirst'], {COMPONENTS['hull']: 1})
        self.assertEqual(shared['afterSecond'], {})
        self.assertEqual(shared['afterRepeat'], {})
        self.assertEqual(results['missing'], {'load': ['Exception', "wrong collision model '%s'" % MISSING],
                                              'loaded': False})
        empty = results['empty']
        self.assertEqual([empty['segment'], empty['any'], empty['bbox']], [None, None, None])
        self.assertEqual(empty['sphere'][0], 'RuntimeError')
        self.assertEqual(empty['triangle'][0], 'RuntimeError')
        # 00f455d0: a BSP-less model is held, and re-setting it reports True.
        self.assertEqual([results['nobsp']['first'], results['nobsp']['again']], [False, True])
        self.assertEqual(results['nobsp']['segment'][0], 'RuntimeError')
        self.assertTrue(results['nobsp']['bbox'])
        self.assertEqual(results['missingAfterGood'], [True, False, None, None])
        self.assertTrue(results['destructible'])
        errors = results['errors']
        self.assertEqual(errors['setModelName'], ['TypeError', 'setModelName() expects 1 argument of type BW::string'])
        self.assertEqual(errors['setDestructibleModelName'],
                         ['TypeError', 'setDestructibleModelName() expects 3 arguments of types BW::string, uint32, uint32'])
        self.assertEqual(errors['setModel'], ['TypeError', '() argument 1 must be set to a PyModel or None'])
        self.assertFalse(errors['setModelNone'])
        self.assertEqual(errors['collideSegment'], ['TypeError', 'collideSegment() expects 2 arguments of types Vector3, Vector3'])
        self.assertEqual(errors['collideSegmentAny'], ['TypeError', 'collideSegmentAny() expects 2 arguments of types Vector3, Vector3'])
        self.assertEqual(errors['collideSphere'], ['TypeError', 'collideSphere() expects 3 arguments of types Vector3, float, bool'])
        self.assertEqual(errors['collidesWithTriangle'], ['ValueError', 'Wrong argument for conversion to WorldTriangle'])
        self.assertEqual(errors['getBoundingBox'], ['TypeError', 'getBoundingBox() takes no arguments.'])
        self.assertEqual(errors['nameType'], ['TypeError', '() argument 1 must be set to a string.'])
        self.assertEqual(errors['methods'], sorted(['setModelName', 'setDestructibleModelName', 'setModel',
                                                    'collideSegment', 'collideSegmentAny', 'collideSphere',
                                                    'collidesWithTriangle', 'getBoundingBox']))

    def check_selectable(self, results, models, queries):
        selectable = results['selectable']
        low, high, bsp, flags = models[SELECTABLE]
        triangles = resources.bsp_triangles(bsp, flags)
        self.assertEqual(selectable['native'][2], SELECTABLE)
        self.assertEqual(selectable['logged'], 0)
        for (start, end), pick in zip(queries['segments']['selectable'], selectable['picks']):
            expected, direction = resources.segment_hits(triangles, start, end)
            if not expected:
                self.assertIsNone(pick)
                continue
            nearest = min(expected, key=lambda h: h[0])
            self.assertAlmostEqual(pick[0], nearest[0], places=4)
            self.assertAlmostEqual(pick[1], faced(nearest[1], direction)[1], places=4)
            self.assertEqual(pick[2], 0)
        self.assertEqual(selectable['failedLogged'], [[[MISSING]]])
        self.assertIsNone(selectable['failedPick'])


class OriginalInstructionTest(unittest.TestCase):
    def test_original_x86_core_matches_production(self):
        if not CLIENT:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_bsp_collision_model_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        with tempfile.TemporaryDirectory(prefix='bsp-collision-oracle-') as directory:
            report = oracle.compare(Path(CLIENT) / 'WorldOfTanks.exe', Path(directory))
        print('Original WGBspCollisionModel instructions matched production:', report)


if __name__ == '__main__':
    unittest.main()
