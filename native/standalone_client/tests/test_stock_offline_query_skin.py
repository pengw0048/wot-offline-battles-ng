"""Differential native skin control laws against the unchanged pinned Python.

The host uses float32 vector operations and the production destructible numeric
core. This proves data/control contracts, not Windows native scene traversal.
"""
import ast
import copy
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import types
import unittest

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]
CXX = os.environ.get('CXX', 'clang++')
FUNCTIONS = {
    '_vector_dot', '_vector_cross', '_box_face_axes', '_point_in_world_box',
    '_segment_world_box_interval', '_projected_box_interval_1513',
    '_original_side_face_1513', '_MotionSkinWitnesses1513',
    '_instance_motion_envelope_1513', '_anonymous_original_surface_1513',
    '_owned_component_skin_1513', '_compiled_motion_skin_1513',
    '_destructible_bin_key', '_bin_keys_for_bounds', '_baked_bin_keys_for_bounds_1513',
    '_destructible_isolated_1513', 'is_excluded_1513',
    '_layout_repair_pending_1513', '_layout_generation_1513',
}
KINDS = {'structure': 0, 'fragile': 1, 'falling': 2, 'other': 3}


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class Vector:
    def __init__(self, x, y, z):
        self.x, self.y, self.z = (f32(v) for v in (x, y, z))

    def __sub__(self, other):
        return Vector(self.x - other.x, self.y - other.y, self.z - other.z)

    def __add__(self, other):
        return Vector(self.x + other.x, self.y + other.y, self.z + other.z)

    def scale(self, factor):
        factor = f32(factor)
        return Vector(self.x * factor, self.y * factor, self.z * factor)

    @property
    def length(self):
        total = f32(f32(f32(self.x * self.x) + f32(self.y * self.y)) + f32(self.z * self.z))
        return f32(math.sqrt(total))

    def xyz(self):
        return self.x, self.y, self.z


def box(center=(0., 0., 0.), half=(1., 1., 1.), material=None, angle=0.):
    sine, cosine = math.sin(angle), math.cos(angle)
    return (tuple(center), ((cosine * half[0], sine * half[0], 0.),
                           (-sine * half[1], cosine * half[1], 0.),
                           (0., 0., half[2])), material)


def base():
    return dict(point=(-1., 0., 0.), start=(-3., 0., 0.), end=(3., 0., 0.), normal=(-1., 0., 0.),
                surfaces=[(71, 0x80, 33, 99)], instances={(10, 1): {'kind': 'fragile', 'boxes': (box(),)}},
                baked_instances={}, tree_instances=set(), isolated_slots=set(), excluded_instances=set(),
                isolated_chunks=set(), layout_repairs=set(), layout_generations={},
                contact_bins={(-1, 0): {(10, 1)}, (0, 0): {(10, 1)}}, baked_shot_bins={},
                destroyed={(10, 1, None)}, speculative=set())


def fixtures():
    cases = []

    def add(name, case, found=None):
        case['name'], case['expected_found'] = name, found
        cases.append(copy.deepcopy(case))

    default = base()
    add('anonymous accepted fragile', default, True)
    for field, value, name in [
            ('destroyed', set(), 'intact fragile'),
            ('tree_instances', {(99, 33)}, 'real tree wire'),
            ('isolated_slots', {(10, 1)}, 'isolated slot'),
            ('isolated_chunks', {10}, 'isolated chunk'),
            ('excluded_instances', {(10, 1)}, 'mode excluded slot'),
            ('contact_bins', {}, 'missing spatial owner'),
            ('surfaces', [(70, 0x80, 33, 99)], 'material below original range'),
            ('surfaces', [(87, 0x80, 33, 99)], 'damaged material'),
            ('surfaces', [(71, 0, 33, 99)], 'ordinary flags without repair'),
            ('surfaces', [(71, 0x80, 33, 99)] * 20, 'duplicate candidates')]:
        case = copy.deepcopy(default); case[field] = value
        add(name, case, name == 'duplicate candidates')
    for collection in ('instances', 'baked_instances'):
        case = copy.deepcopy(default)
        case[collection][(99, 33)] = {'kind': 'fragile', 'boxes': (box((20., 0., 0.)),)}
        add('registered anonymous wire in ' + collection, case, False)
    for material in (71, 73, 86):
        case = copy.deepcopy(default)
        case['instances'][(10, 1)] = {'kind': 'structure', 'boxes': (box(material=material),)}
        case['surfaces'] = [(material, 0x80, 33, 99)]
        case['destroyed'] = {(10, 1, material)}
        add('accepted structure material %s' % material, case, True)
        case['destroyed'] = set()
        add('intact structure material %s' % material, case, False)
        case['speculative'] = {(10, 1, material)}
        add('predicted exact structure material %s' % material, case, True)
        case['speculative'] = {(10, 1, None)}
        add('predicted None is not a structure wildcard %s' % material, case, False)
    case = copy.deepcopy(default)
    case['instances'][(10, 2)] = {'kind': 'fragile', 'boxes': (box(),)}
    for keys in case['contact_bins'].values(): keys.add((10, 2))
    add('overlapping intact owner veto', case, False)
    case['destroyed'].add((10, 2, None))
    add('overlapping unanimously accepted owners', case, True)
    case = copy.deepcopy(default); case['point'] = (-1., 3., 0.)
    add('side projection above model', case, True)
    case['normal'] = (0., 1., 0.)
    add('top face does not authorize side projection', case, False)
    case['normal'] = None
    add('no normal does not authorize projection', case, False)
    case['normal'] = (-1., 0., 0.)
    case['baked_instances'][(20, 1)] = {'kind': 'fragile', 'boxes': (box((0., 10., 0.)),)}
    case['baked_shot_bins'][(-1, 0)] = {(20, 1)}
    add('unstreamed stacked baked owner veto', case, False)
    case['instances'][(20, 1)] = case['baked_instances'][(20, 1)]
    case['destroyed'].add((20, 1, None))
    add('streamed stacked unanimous owner', case, True)
    case = copy.deepcopy(default)
    case['instances'][(10, 1)] = {'kind': 'structure', 'boxes': (box(material=71),)}
    case['destroyed'] = {(10, 1, 71)}
    case['surfaces'] = [(71, 0, 9, 10)]
    case['layout_generations'] = {10: 1}
    add('spatially proved repaired slot remap', case, True)
    case['layout_repairs'] = {10}
    add('pending repair cannot remap', case, False)
    case['layout_repairs'] = set(); case['surfaces'] = [(71, 0, 1, 10)]
    add('actual owner does not remap itself', case, False)
    case['surfaces'] = [(71, 0, 9, 11)]; case['layout_generations'][11] = 1
    add('different chunk cannot remap', case, False)
    case = copy.deepcopy(default)
    case['surfaces'] = [(73, 0, 1, 10)]
    case['instances'][(10, 1)] = {'kind': 'structure', 'boxes':
        (box(material=74), box((0., 3., 0.), material=73))}
    case['destroyed'] = {(10, 1, 74)}
    add('broken component owns stale different material', case, True)
    case['destroyed'] = set()
    add('intact containing component veto', case, False)
    case['speculative'] = {(10, 1, 74)}
    add('predicted containing component', case, True)
    case['instances'][(10, 1)]['boxes'] += (box(material=73),)
    add('actual queried material containing witness veto', case, False)
    case = copy.deepcopy(default)
    case.update(point=(-2., 0., 0.), start=(-4., 0., 0.), end=(4., 0., 0.))
    case['instances'][(10, 1)] = {'kind': 'fragile', 'boxes': (box(half=(2., 1., 1.)),)}
    case['instances'][(10, 2)] = {'kind': 'fragile', 'boxes': (box((1., 0., 0.), (.25, 1., 1.)),)}
    for keys in case['contact_bins'].values(): keys.add((10, 2))
    add('re-enable shared material before adjacent live owner', case, True)
    case['instances'][(10, 2)] = {'kind': 'fragile', 'boxes': (box((-1.95, 0., 0.), (.25, 1., 1.)),)}
    add('adjacent live begins before witness', case, False)
    for angle in (.2, -.6):
        sine, cosine = math.sin(angle), math.cos(angle)
        case = copy.deepcopy(default)
        case['instances'][(10, 1)]['boxes'] = (box(angle=angle),)
        case['point'] = (-.95 * cosine - 3 * sine, -.95 * sine + 3 * cosine, 0.)
        case['start'] = (-3 * cosine - 3 * sine, -3 * sine + 3 * cosine, 0.)
        case['end'] = (3 * cosine - 3 * sine, 3 * sine + 3 * cosine, 0.)
        case['normal'] = (-cosine, -sine, 0.)
        for x in range(-2, 2):
            case['contact_bins'][(x, 0)] = {(10, 1)}
        add('tilted authored-up side %s' % angle, case, True)
        case['normal'] = (-sine, cosine, 0.)
        add('tilted top normal %s' % angle, case, False)
    case = copy.deepcopy(default)
    case['instances'][(10, 1)]['boxes'] = (box(half=(0., 1., 1.)),)
    add('degenerate authored basis supplies no envelope', case, False)
    case = copy.deepcopy(default)
    case['instances'][(10, 1)]['boxes'] = (box((0., 0., 0.), (.1, .25, 2.), angle=.35),)
    case['point'] = (0., 0., 0.)
    add('small rotated volume witness', case, True)
    rng = random.Random(1513)
    for index in range(80):
        case = base(); material = rng.randint(71, 86)
        center = rng.uniform(-80, 80)
        half = rng.uniform(.2, 3.)
        key = (0xffffffff if index % 5 == 0 else 10, index + 1)
        case['instances'] = {key: {'kind': 'structure', 'boxes': (box((center, 0., 0.), (half, 1., 1.), material),)}}
        case['point'] = (center - half, 0., 0.)
        case['start'], case['end'] = (center - half - 3., 0., 0.), (center + half + 3., 0., 0.)
        case['surfaces'] = [(material, 0x80, 65535, 0xfffffffe)]
        case['destroyed'] = {(key[0], key[1], material)} if index % 2 else set()
        low = math.floor(case['start'][0] / 8.)
        high = math.floor(case['end'][0] / 8.)
        case['contact_bins'] = {(x, 0): {key} for x in range(low, high + 1)}
        add('seeded varied position/material %s' % index, case, bool(index % 2))
    return cases


def reference(source):
    path = source / 'src/res/scripts/client/gui/mods/offline_lan_0922/destructibles_sensor.py'
    tree = ast.parse(path.read_text())
    selected = [node for node in tree.body if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in FUNCTIONS]
    if len(selected) != len(FUNCTIONS):
        raise AssertionError('Pinned skin law source seam changed')
    no_native = types.SimpleNamespace(box_probe=lambda *a, **k: None, box_envelope=lambda *a, **k: None,
                                    motion_witnesses=lambda *a, **k: None)
    env = dict(_INTEGER_TYPES=(int,), _native_geometry=no_native, _native_sensor=None,
               _CATALOG_POINT_EPSILON=.075, _SHOT_RAY_EPSILON=.0001, _DESTRUCTIBLE_BIN_METRES=8., xrange=range)
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(path), 'exec'), env)
    env['_DEFAULT_POINT_BOX_PROBE_1513'] = env['_point_in_world_box']
    env['_DEFAULT_SEGMENT_BOX_PROBE_1513'] = env['_segment_world_box_interval']
    env['_MOTION_SKIN_LAWS_1513'] = tuple(env[name] for name in (
        '_point_in_world_box', '_segment_world_box_interval', '_projected_box_interval_1513', '_original_side_face_1513'))

    def run(case):
        # Each call gets fresh instance dicts, exactly as a refreshed typed view.
        instance = copy.deepcopy(case['instances']); baked = copy.deepcopy(case['baked_instances'])
        env.update(g_offh_destr_instances=instance, g_offh_destr_contact_bins=case['contact_bins'],
                   g_offh_destr_isolated_chunks=case['isolated_chunks'], g_offh_destr_isolated_slots=case['isolated_slots'],
                   g_offh_destr_speculative=case['speculative'], _destructible_catalog={
                       'baked_instances': baked, 'tree_instances': case['tree_instances'],
                       'baked_shot_bins': case['baked_shot_bins'], 'layout_repairs': case['layout_repairs'],
                       'layout_generations': case['layout_generations'], 'excluded_instances': case['excluded_instances']})
        destroyed = case['destroyed']
        env['_get_destr_authority'] = lambda: types.SimpleNamespace(is_destroyed=lambda chunk, item, material:
            (chunk, item, material) in destroyed or (chunk, item, None) in destroyed)
        point, start, end = [Vector(*case[name]) for name in ('point', 'start', 'end')]
        normal = None if case['normal'] is None else Vector(*case['normal'])
        result = env['_compiled_motion_skin_1513'](point, start, end, set(case['surfaces']), normal)
        return None if result is None else (result[0], set(result[1]))
    return run


def encode(cases):
    values = [len(cases)]
    def seq(rows, write):
        values.append(len(rows))
        for row in rows: write(row)
    def keys(rows): seq(sorted(rows), lambda row: values.extend(row))
    def instances(rows):
        def entry(row):
            key, instance = row; values.extend(key); values.append(KINDS[instance['kind']])
            def write_box(box):
                center, axes, material = box; values.extend(center)
                seq(axes, lambda axis: values.extend(axis))
                values.append(-1 if material is None else material)
            seq(instance['boxes'], write_box)
        seq(sorted(rows.items()), entry)
    for case in cases:
        values.extend((.0001, .075, 8.))
        for name in ('point', 'start', 'end'): values.extend(Vector(*case[name]).xyz())
        values.append(int(case['normal'] is not None))
        values.extend((0., 0., 0.) if case['normal'] is None else Vector(*case['normal']).xyz())
        seq(case['surfaces'], lambda row: values.extend(row))
        instances(case['instances']); instances(case['baked_instances'])
        for name in ('tree_instances', 'isolated_slots', 'excluded_instances'): keys(case[name])
        for name in ('isolated_chunks', 'layout_repairs'): seq(sorted(case[name]), values.append)
        seq(sorted(case['layout_generations'].items()), lambda row: values.extend(row))
        for name in ('contact_bins', 'baked_shot_bins'):
            def entry(row): values.extend(row[0]); keys(row[1])
            seq(sorted(case[name].items()), entry)
        for name in ('destroyed', 'speculative'):
            seq(sorted(case[name], key=repr), lambda row: values.extend((row[0], row[1], -1 if row[2] is None else row[2])))
    return ' '.join(map(str, values)) + '\n'


class NativeSkinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source: raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        if not shutil.which(CXX): raise unittest.SkipTest('C++ compiler is required')
        cls.source = Path(source)
        cls.temporary = tempfile.TemporaryDirectory(prefix='native-query-skin-')
        cls.binary = Path(cls.temporary.name) / 'skin-host'
        subprocess.run([CXX, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                        *native_test_compile_flags(CXX), '-I' + str(ROOT / 'bigworld'), '-I' + str(cls.source / 'native'),
                        str(ROOT / 'bigworld/stock_offline_query_skin.cpp'),
                        str(cls.source / 'native/offline_destructible_geometry.cpp'),
                        str(ROOT / 'tests/stock_offline_query_skin_host.cpp'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'temporary'): cls.temporary.cleanup()

    def test_native_matches_unchanged_pinned_control_laws(self):
        cases = fixtures()
        oracle = reference(self.source)
        process = subprocess.run([str(self.binary)], input=encode(cases), text=True,
                                 capture_output=True, check=True)
        output = process.stdout.splitlines()
        self.assertEqual(len(output), len(cases))
        positives = negatives = 0
        for case, line in zip(cases, output):
            with self.subTest(case=case['name']):
                expected = oracle(case)
                values = line.split(); found, distance, size = bool(int(values[0])), float(values[1]), int(values[2])
                excluded = {tuple(map(int, values[i:i + 4])) for i in range(3, len(values), 4)}
                self.assertEqual(size, len(excluded))
                self.assertEqual(found, expected is not None)
                self.assertEqual(found, case['expected_found'])
                if found:
                    positives += 1
                    self.assertAlmostEqual(distance, expected[0], delta=1.e-11 * max(1., abs(expected[0])))
                    self.assertEqual(excluded, expected[1])
                else:
                    negatives += 1
                    self.assertEqual(excluded, set())
        self.assertGreaterEqual(positives, 50)
        self.assertGreaterEqual(negatives, 50)


if __name__ == '__main__':
    unittest.main()
