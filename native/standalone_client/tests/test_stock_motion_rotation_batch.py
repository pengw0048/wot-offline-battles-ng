"""Fixed current-release rotation laws against a typed, Python-free loop.

The exact pinned Python functions are the oracle. Scripted complete-world
outcomes prove ordering, reentrant lazy proofs and early exits; a separate case
runs the real world_stage kernel with a native miss frontier. This does not
prove native Windows collision or the unimplemented catalog/tree adapters.
"""
import ast
import copy
import json
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import tempfile
import textwrap
import types
import unittest

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = {
    '_angle_delta', '_DestructibleSweepHitTester', '_trig_interval_extrema',
    '_destructible_rotation_interval_bbox', '_reference_rotation_interval_bbox',
    '_trig_linear_interval_minimum', '_rotation_departing_contact',
    '_reference_rotation_departing_contact', '_destructible_world_sweep_descriptor',
    '_native_world_rotation_is_clear',
}


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class Vector:
    def __init__(self, values):
        self.x, self.y, self.z = (f32(value) for value in values)


def selected(source, names):
    found = {}
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names:
            if node.name in found:
                raise AssertionError('Duplicated pinned rotation law: ' + node.name)
            found[node.name] = textwrap.dedent(ast.get_source_segment(source, node))
    if found.keys() != names:
        raise AssertionError('Pinned rotation law missing: ' + str(names - found.keys()))
    # Definitions are evaluated only after the complete global environment is
    # installed; only _drivable_surface has a non-literal required default.
    return '\n\n'.join(found[name] for name in sorted(found))


def snapshot(**changes):
    value = dict(position=(.123456789, 0., -.234567891),
        minimum=(-1., 0., -2.), maximum=(1., 2., 2.),
        start=0., end=.2, pitch=0., roll=0., pivot=0., translation=(0., 0.),
        step=math.pi / 36., gradient=1.28, maximum_steps=16,
        include_static=True, reader=True, replacement=False)
    value.update(changes)
    return value


def contact(point=(0., 1., 0.), normal=(0., 1., 0.)):
    return tuple(f32(x) for x in point), tuple(f32(x) for x in normal)


def script(status=0, trace=None, candidates=()):
    return dict(status=status, trace=trace, candidates=list(candidates))


def input_values(value):
    return list(value['position']) + list(value['minimum']) + list(value['maximum']) + [
        value[name] for name in ('start', 'end', 'pitch', 'roll', 'pivot')] + list(
        value['translation']) + [value['step'], value['gradient'], value['maximum_steps'],
        int(value['include_static']), int(value['reader']), int(value['replacement'])]


def encode(cases):
    lines = [str(len(cases))]
    for case in cases:
        lines.append(str(case['mode']) + ' ' + ' '.join(str(x) for x in input_values(case['input'])))
        if case['mode'] == 1:
            lines.append(' '.join(str(x) for vector in case['contact'] for x in vector))
        if case['mode'] == 2:
            records = case['scripts']
            lines.append(str(len(records)))
            for record in records:
                hit = record['trace'] or contact()
                lines.append(' '.join(str(x) for x in [record['status'], int(record['trace'] is not None)] +
                    [x for vector in hit for x in vector] + [len(record['candidates'])]))
                lines += [' '.join(str(x) for vector in row for x in vector)
                          for row in record['candidates']]
    return '\n'.join(lines) + '\n'


class Oracle:
    def __init__(self, fork):
        scripts = fork / 'src/res/scripts/client/gui/mods/offline_lan_0922'
        world = types.SimpleNamespace()
        world_env = dict(_MAX_DRIVABLE_GRADIENT=1.28)
        exec(selected((scripts / 'world_collision.py').read_text(),
                      {'_hull_pose_y', '_drivable_surface'}), world_env)
        world._hull_pose_y = world_env['_hull_pose_y']
        world._drivable_surface = world_env['_drivable_surface']
        physics_env = dict(math=math)
        exec(selected((scripts / 'vehicle_physics.py').read_text(),
                      {'track_pivot_position'}), physics_env)
        native_math = types.SimpleNamespace(_load=lambda: None)
        self.env = dict(math=math, native_math=native_math, world_collision=world,
            vehicle_physics=types.SimpleNamespace(track_pivot_position=physics_env['track_pivot_position']))
        exec(selected((scripts / 'battle_runtime.py').read_text(), FUNCTIONS), self.env)
        self.world = world
        # Execute the exact pinned wrapper's native call / exception boundary.
        # Exclude only its unrelated Python fallback after native result None.
        wrapper = next(node for node in ast.walk(ast.parse(
            (scripts / 'world_collision.py').read_text()))
            if isinstance(node, ast.FunctionDef) and node.name == '_check_horizontal_collision')
        wrapper.decorator_list = []
        boundary = next(i for i, node in enumerate(wrapper.body) if isinstance(node, ast.Try))
        wrapper.body = [ast.Import(names=[ast.alias(name='sys')]),
                        wrapper.body[boundary], wrapper.body[boundary+1]]
        self.wrapper_code = compile(ast.fix_missing_locations(ast.Module(
            body=[wrapper], type_ignores=[])), 'fixed-world-exception-boundary', 'exec')

    def configure(self, value):
        self.env['DESTRUCTIBLE_POSE_MAX_ANGLE_STEP'] = value['step']
        self.env['DESTRUCTIBLE_POSE_MAX_SWEEP_STEPS'] = value['maximum_steps']
        # A changed exact gradient is an explicit input to the native primitive.
        self.world._drivable_surface.__defaults__ = (value['gradient'],)

    def proof(self, value, row):
        self.configure(value)
        args = (value['position'], (value['minimum'], value['maximum']),
                value['start'], value['end'], value['pitch'], value['roll'])
        previous = []
        fn = self.env['_reference_rotation_departing_contact'](*args,
            previous_contacts=lambda: previous, translation=value['translation'],
            pivot_offset=value['pivot'])
        result = fn(tuple(Vector(vector) for vector in row))
        # Observe inside independently, using the fixed law's exact local map.
        p, normal = row
        py = self.world._hull_pose_y(value['pitch'], value['roll'])
        if normal[1] < -.2 or abs(py[1]) < .1:
            inside = False
        else:
            dx, dy, dz = [p[i] - value['position'][i] for i in range(3)]
            sine, cosine = math.sin(value['start']), math.cos(value['start'])
            x, z = dx*cosine-dz*sine, dx*sine+dz*cosine
            y = (dy-x*py[0]-z*py[2])/py[1]
            inside = all(low-.001 <= component <= high+.001 for low, high, component in
                         zip(value['minimum'], value['maximum'], (x, y, z)))
        # The closure returns False for an improving outside witness without
        # a matching previous contact. A matching plane reveals improves.
        if not result and not inside:
            previous.append(tuple(Vector(vector) for vector in row))
            result = fn(tuple(Vector(vector) for vector in row))
        return [int(result), int(inside)]

    def run(self, value, records):
        self.configure(value)
        calls, verdicts = [], []
        owner = types.SimpleNamespace(
            _destructibles=types.SimpleNamespace(
                native_replacement_bsp_active=(lambda: value['replacement']) if value['reader'] else None,
                _vehicle_body_bbox=lambda unused: (value['minimum'], value['maximum'])),
            _local_pitch=value['pitch'], _local_roll=value['roll'],
            _runtime=types.SimpleNamespace(bigworld=None, math=None),
            _avatar=types.SimpleNamespace(spaceID=1), _vector=Vector)
        trace = {}

        def check(unused_bw, unused_math, unused_space, pos, yaw, speed, descriptor,
                  airborne, dt, return_status, allow, kinetic, **options):
            index = len(calls)
            box = descriptor['hull']['hitTester'].bbox
            motion = options.get('motion_yaw')
            calls.append([pos.x, pos.y, pos.z, yaw, speed, dt, options['pitch'],
                options['roll'], 0. if motion is None else motion, box[0][0], box[1][0],
                -box[0][2], box[1][2], box[0][1], box[1][1], int(motion is not None),
                int(airborne), int(options.get('exact_footprint', False)),
                int(options.get('departing_contact') is not None)])
            if index >= len(records):
                raise RuntimeError('World fixture exhausted')
            record = records[index]
            depart = options.get('departing_contact')
            accepted = True
            if depart is not None:
                for candidate_index, row in enumerate(record['candidates']):
                    valid = depart(tuple(Vector(vector) for vector in row))
                    verdicts.append([index, candidate_index, int(valid)])
                    if not valid:
                        accepted = False
                        break
            if record['trace'] is not None:
                options['trace'].update(hit=record['trace'][0], normal=record['trace'][1])
            status = (0 if accepted else 1) if record['status'] == -99 else record['status']
            if status < 0:
                raise RuntimeError('query')
            return ('clear', 'hard', 'kinetic')[status]

        failures = []

        def native_run(unused_module, space, pos, yaw, vel, descriptor,
                       airborne, dt, return_status, allow, kinetic, commit,
                       motion, pitch, roll, trace, footprint, departing, query_owner):
            return check(None, None, space, pos, yaw, vel, descriptor,
                         airborne, dt, return_status, allow, kinetic,
                         commit_enabled=commit, motion_yaw=motion, pitch=pitch,
                         roll=roll, trace=trace, exact_footprint=footprint,
                         departing_contact=departing, query_owner=query_owner)

        wrapper_env = dict(__name__=__name__,
            native_world=types.SimpleNamespace(run=native_run),
            native_math=types.SimpleNamespace(report_world_failure=failures.append))
        exec(self.wrapper_code, wrapper_env)
        wrapper = wrapper_env['_check_horizontal_collision']
        self.world.check_horizontal_collision = lambda bw, maths, *args, **kwargs: wrapper(*args, **kwargs)
        result = self.env['_native_world_rotation_is_clear'](owner, value['position'],
            value['start'], value['end'], {'physics': {}}, pitch=value['pitch'],
            roll=value['roll'], record_local=False, pivot_offset=value['pivot'],
            contact_trace=trace, include_static=value['include_static'], translation=value['translation'])
        main_calls = sum(row[-1] for row in calls)
        translating = bool(math.hypot(*value['translation']))
        slices = main_calls if translating else (main_calls+1)//2
        return dict(result=[int(result), int(not calls), slices, len(calls),
                            len(calls)-main_calls, len(verdicts), len(failures),
                            int(trace.get('reason') == 'native_world_error')],
                    trace=int('hit' in trace and 'normal' in trace),
                    trace_values=(list(trace['hit'])+list(trace['normal'])
                                  if 'hit' in trace and 'normal' in trace else None),
                    world=calls, verdicts=verdicts)


class RotationBatchTest(unittest.TestCase):
    def test_fixed_rotation_laws_and_world_order(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        fork = Path(source)
        oracle = Oracle(fork)
        cases = []
        rng = random.Random(1513)
        for unused in range(80):
            value = snapshot(start=rng.uniform(-math.pi, math.pi),
                end=rng.uniform(-math.pi, math.pi), pitch=rng.uniform(-.6, .6),
                roll=rng.uniform(-.6, .6), pivot=rng.choice((0., -1.1, 1.1)),
                translation=(rng.uniform(-.3, .3), rng.uniform(-.3, .3)),
                minimum=(rng.uniform(-2.4, -.3), rng.uniform(-.5, .4),
                         rng.uniform(-4.2, -.4)),
                maximum=(rng.uniform(.2, 2.), rng.uniform(1., 3.),
                         rng.uniform(.6, 3.9)), step=rng.uniform(.01, .2))
            # Geometry-only laws vary asymmetric body bounds and analytical
            # half-angle as well as pivot/pose/contact. Full-loop bounded
            # slice admission is exercised separately below.
            value['end'] = value['start'] + rng.uniform(-.7, .7)
            normal = tuple(rng.uniform(-1., 1.) for unused_axis in range(3))
            normal_length = math.sqrt(sum(value*value for value in normal))
            row = contact((rng.uniform(-5., 5.), rng.uniform(-1., 3.), rng.uniform(-5., 5.)),
                          tuple(value/normal_length for value in normal))
            cases.append(dict(mode=1, input=value, contact=row))
            cases.append(dict(mode=0, input=value))
        cases.append(dict(mode=1, input=snapshot(position=(0., 0., 0.),
            start=1.e12, end=1.e12+.2), contact=contact((-2.2, 1., 4.), (1., 0., 0.))))
        for value in (
            snapshot(), snapshot(start=math.pi-.02, end=-math.pi+.04),
            snapshot(start=-math.pi+.02, end=math.pi-.04),
            snapshot(pivot=1.2, translation=(.3, -.2), pitch=.25, roll=-.2),
            snapshot(end=0.), snapshot(include_static=False),
            snapshot(include_static=False, reader=False),
            snapshot(include_static=False, replacement=True),
            snapshot(position=(16777217., .123456789, -16777217.), end=.01)):
            cases.append(dict(mode=2, input=value, scripts=[script() for unused in range(40)]))
        base = snapshot(position=(0., 0., 0.), start=1.2, end=1.4)
        outside = contact((-2.2, 1., 4.), (1., 0., 0.))
        same_plane = contact((-2.2, 1., .2), (1., 0., 0.))
        backing_wall = contact((3., 1., 0.), (-1., 0., 0.))
        for first, second in ((same_plane, None), (None, same_plane),
                              (contact((-2.202, 1., .2), (1., 0., 0.)), None),
                              (None, None)):
            records = [script(-99, candidates=[outside]), script(trace=first), script(trace=second)]
            records += [script(-99, candidates=[outside]) for unused in range(40)]
            cases.append(dict(mode=2, input=base, scripts=records))
        cases.append(dict(mode=2, input=base, scripts=[
            script(-99, candidates=[outside, backing_wall]), script(trace=same_plane), script()]))
        slope = contact((9., .5, 9.), (0., 1., 0.))
        cases.append(dict(mode=2, input=snapshot(pitch=.2, roll=-.15), scripts=[
            script(-99, candidates=[slope]), script(trace=contact((0., .5, 0.))), script()] +
            [script(-99, candidates=[slope]) for unused in range(40)]))
        # An empty first proof remains memoized even if a later world outcome
        # could otherwise offer a new plane. No extra hidden query is allowed.
        cases.append(dict(mode=2, input=base, scripts=[
            script(0, candidates=[outside]), script(), script()] +
            [script(0, candidates=[outside]) for unused in range(40)]))
        for change in ({'position': (float('nan'), 0., 0.)}, {'step': 0.},
                       {'maximum_steps': 0}, {'minimum': (2., 0., -2.)},
                       {'end': float('inf')}, {'maximum_steps': 1}):
            cases.append(dict(mode=2, input=snapshot(**change), scripts=[], error='invalid'))
        # Query errors pass through the actual fixed wrapper above: they are
        # contained per world, retain an existing hit, and do not abort the
        # other lazy old-footprint direction or replay any entered world.
        for status in (-1, -2):
            for retained in (None, same_plane):
                cases.append(dict(mode=2, input=snapshot(),
                                  scripts=[script(status, trace=retained)]))
        for first, second in ((script(-1), script(trace=same_plane)),
                              (script(trace=same_plane), script(-2)),
                              (script(-1), script(-2)),
                              (script(-1, trace=same_plane), script(-2))):
            cases.append(dict(mode=2, input=base, scripts=[
                script(-99, candidates=[outside]), first, second] +
                [script(-99, candidates=[outside]) for unused in range(40)]))
        cases.append(dict(mode=2, input=snapshot(), scripts=[script(3)],
                          error='invalid', engine_called=True))
        cases.append(dict(mode=3, input=snapshot()))
        cases.append(dict(mode=4, input=snapshot()))
        cases.append(dict(mode=5, input=snapshot()))
        cxx = os.environ.get('CXX', 'c++')
        if not shutil.which(cxx):
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='motion-rotation-batch-') as folder:
            binary = Path(folder) / 'host'
            subprocess.run([cxx, *native_test_compile_flags(cxx), '-std=c++17', '-O2',
                '-Werror', '-Wall', '-Wextra', '-ffp-contract=off',
                '-I'+str(ROOT/'bigworld'), '-I'+str(fork/'native'),
                str(ROOT/'tests/stock_motion_rotation_batch_host.cpp'),
                str(ROOT/'bigworld/stock_motion_rotation_batch.cpp'),
                str(fork/'native/world_stage.cpp'), '-o', str(binary)], check=True)
            run = subprocess.run([str(binary)], input=encode(cases), text=True,
                                 capture_output=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            rows = [json.loads(line) for line in run.stdout.splitlines()]
        self.assertEqual(len(rows), len(cases))
        for case, actual in zip(cases, rows):
            if 'error' in case:
                self.assertEqual(actual['error'], case['error'], case)
                self.assertEqual(len(actual['world']), int(case.get('engine_called', False)), case)
            elif case['mode'] == 0:
                value = case['input']
                expected = oracle.env['_reference_rotation_interval_bbox'](
                    (value['minimum'], value['maximum']), value['step'], value['pivot'])
                for a, b in zip(sum(actual['bounds'], []), sum((list(v) for v in expected), [])):
                    self.assertAlmostEqual(a, b, places=12)
            elif case['mode'] == 1:
                self.assertEqual(actual['proof'], oracle.proof(case['input'], case['contact']), case)
            elif case['mode'] == 2:
                expected = oracle.run(case['input'], case['scripts'])
                for key in ('result', 'trace', 'trace_values', 'verdicts'):
                    self.assertEqual(actual[key], expected[key], (case, key, actual, expected))
                self.assertEqual(len(actual['world']), len(expected['world']))
                for a, b in zip(actual['world'], expected['world']):
                    self.assertEqual(len(a), len(b))
                    for x, y in zip(a, b):
                        self.assertAlmostEqual(x, y, places=11, msg=str(case))
            elif case['mode'] == 3:
                self.assertEqual(actual['core'], [1, 6])
            else:
                self.assertEqual(actual['failed_core'][:4], [0, 1, 1, 1])
                self.assertEqual(actual['failed_core'][4], int(case['mode'] == 4))
        print('Current rotation primitive: %d fixed-source oracle cases passed' % len(cases))


if __name__ == '__main__':
    unittest.main()
