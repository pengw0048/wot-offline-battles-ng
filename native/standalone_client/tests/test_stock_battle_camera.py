"""Production battle camera owners against the original #1513 instructions.

The host compiles the production collider, ThirdPersonProvider.update and
HomingCamera.update over scripted engine owners; Unicorn runs the original
x86 over the same scripted owners. Every float is compared bit for bit.
"""
from pathlib import Path
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def strip_includes(text):
    return '\n'.join(line for line in text.splitlines()
                     if not line.startswith('#include') and not line.startswith('#pragma once'))


def host_source():
    header = strip_includes((ROOT / 'bigworld/stock_camera_collider.hpp').read_text())
    collider = (ROOT / 'bigworld/stock_camera_collider.cpp').read_text()
    collider = strip_includes(collider[:collider.index('\n\nint setStockCameraDynamicColliders(')])
    camera = (ROOT / 'bigworld/stock_battle_camera.cpp').read_text()
    text = (ROOT / 'tests/stock_battle_camera_host.cpp').read_text()
    text = text.replace('// @COLLIDER@', header + '\n' + collider)
    text = text.replace('// @TPP_UPDATE@', block(camera, 'void ThirdPersonProvider::update( bool collide )'))
    text = text.replace('// @HOMING_UPDATE@', block(camera, 'void HomingCamera::update( float dTime )'))
    return text


def build_host(folder):
    source = folder / 'host.cpp'
    source.write_text(host_source())
    binary = folder / 'host'
    subprocess.run(['c++', '-std=c++14', '-O1', '-ffp-contract=off', '-Wall', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-variable', '-include',
                    str(ROOT / 'tests/stock_battle_camera_engine.hpp'), str(source), '-o', str(binary)],
                   check=True)
    return binary


def hexf(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def host_text(command):
    """Render one shared command for the host's whitespace protocol."""
    def emit(value):
        if value is None:
            return []
        if isinstance(value, bool):
            return [str(int(value))]
        if isinstance(value, int):
            return [str(value)]
        if isinstance(value, float):
            return [hexf(value)]
        if isinstance(value, (list, tuple)) and value and isinstance(value[0], tuple) and \
                len(value[0]) == 5 and isinstance(value[0][0], int):
            out = [str(len(value))]
            for identifier, matrix, triangle, flags, distance in value:
                out += [str(identifier)] + [hexf(c) for row in matrix for c in row]
                out += [hexf(c) for v in triangle for c in v] + [str(flags), hexf(distance)]
            return out
        if isinstance(value, (list, tuple)) and not value:
            return ['0']
        if isinstance(value, (list, tuple)) and isinstance(value[0], (list, tuple)):
            return [hexf(c) for row in value for c in row]
        return [hexf(c) for c in value]
    name = command[0]
    out = [name]
    args = list(command[1:])
    if name == 'tpp':
        base = args[0]
        out += ['1'] + emit(base) if base is not None else ['0']
        args = args[1:]
    if name == 'homing':
        for provider in args[:3]:
            out += ['0'] if provider is None else ['1'] + emit(provider)
        args = args[3:]
    for value in args:
        out += emit(value)
    return ' '.join(out)


def rnd(r, scale=10.0):
    return f32(r.uniform(-scale, scale))


def vec(r, scale=10.0):
    return tuple(rnd(r, scale) for unused in range(3))


def rotation(yaw, translation):
    import math
    c, s = f32(math.cos(yaw)), f32(math.sin(yaw))
    return [[c, 0.0, -s, 0.0], [0.0, 1.0, 0.0, 0.0], [s, 0.0, c, 0.0],
            [translation[0], translation[1], translation[2], 1.0]]


def leaf_commands(r):
    commands = [('camera', 0.5, 1000.0, f32(1.2), f32(1.6))]
    for unused in range(120):
        commands.append(('segseg', vec(r), vec(r), vec(r), vec(r)))
    # Degenerate and parallel segments exercise every epsilon branch.
    p = vec(r)
    commands += [('segseg', p, p, vec(r), vec(r)), ('segseg', vec(r), vec(r), p, p),
                 ('segseg', p, p, p, p),
                 ('segseg', (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (1.0, 1.0, 0.0)),
                 ('segseg', (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (2.0, 1.0, 0.0), (3.0, 1.0, 0.0)),
                 ('segseg', (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (-3.0, 1.0, 0.0), (-2.0, 1.0, 0.0))]
    for unused in range(120):
        a, b, c = vec(r), vec(r), vec(r)
        inside = tuple(f32((a[i] + b[i] + c[i]) / 3.0 + rnd(r, 0.5)) for i in range(3))
        commands += [('inside', vec(r), a, b, c), ('inside', inside, a, b, c),
                     ('closest', vec(r, 20.0), a, b, c), ('closest', inside, a, b, c),
                     ('segtri', vec(r), vec(r), a, b, c), ('segtri', inside, vec(r), a, b, c),
                     ('triray', a, b, c, vec(r), vec(r, 1.0), f32(r.uniform(0.0, 30.0)))]
        # Rays and segments aimed through the face reach the hit branches.
        origin = tuple(f32(inside[i] + rnd(r, 6.0)) for i in range(3))
        aim = tuple(f32(inside[i] - origin[i]) for i in range(3))
        far = tuple(f32(inside[i] + aim[i]) for i in range(3))
        commands += [('triray', a, b, c, origin, aim, f32(r.uniform(0.5, 3.0))),
                     ('segtri', origin, far, a, b, c)]
    a, b = (0.0, 0.0, 0.0), (1.0, 0.0, 0.0)
    commands += [('closest', (0.3, 0.0, 0.0), a, b, b), ('closest', (2.0, 1.0, 0.0), a, a, a),
                 ('closest', (0.5, 0.5, 0.0), a, b, (2.0, 0.0, 0.0)),
                 ('inside', (0.0, 0.0, 0.0), a, b, (0.0, 1.0, 0.0)),
                 ('segtri', (0.2, 0.2, -1.0), (0.2, 0.2, 1.0), a, b, (0.0, 1.0, 0.0))]
    for unused in range(80):
        normal = tuple(f32(v) for v in (r.choice([0.0, 1.0, -1.0]), 0.0, r.choice([0.0, 1.0, -1.0])))
        commands.append(('plane', normal, rnd(r), vec(r), vec(r)))
    commands.append(('plane', (1.0, 0.0, 0.0), 2.0, (0.0, 0.0, 0.0), (0.0, 3.0, 0.0)))
    return commands


def bounds_commands(r):
    commands = [('space', 1)]
    for unused in range(40):
        low = (rnd(r, 50.0), -10.0, rnd(r, 50.0))
        high = (f32(low[0] + r.uniform(1.0, 80.0)), 30.0, f32(low[2] + r.uniform(1.0, 80.0)))
        commands.append(('spacebounds', low, high))
        for unused in range(4):
            commands.append(('spacebox', vec(r, 120.0), vec(r, 40.0), vec(r, 1.0)))
    commands += [('spacebounds', (1.0, 1.0, 1.0), (0.0, 0.0, 0.0)),
                 ('spacebox', (5.0, 0.0, 5.0), (0.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
                 ('space', 0), ('spacebox', (500.0, 0.0, 5.0), (0.0, 0.0, 0.0), (0.0, 0.0, 1.0)),
                 ('space', 1)]
    return commands


GROUPS = (
    # (minVolume, lowSpeedLimit, vehicleVisibilityLimit, approachSpeed, cameraSpeedFactor,
    #  criticalDistance squared, canSkip) from the exact arcade configuration.
    (0.0, 1.0, 10.0, 1.5, 10.0, 0.4, 25.0, True),
    (100.0, 0.75, 10.0, 1.5, 10.0, 0.4, 25.0, True),
    (300.0, 0.5, 10.0, 1.5, 10.0, 0.4, 25.0, True),
    (500.0, 0.0, 0.0, 1.5, 10000.0, 0.1, 25.0, False),
)


def obstacle(r, identifier, near, material=0, flags=0):
    yaw = f32(r.uniform(-3.0, 3.0))
    matrix = rotation(yaw, tuple(f32(near[i] + r.uniform(-2.0, 2.0)) for i in range(3)))
    triangle = tuple(vec(r, 4.0) for unused in range(3))
    return (identifier, matrix, triangle, (material << 8) | flags, f32(r.uniform(0.0, 5.0)))


def advanced_commands(r):
    commands = [('camera', 0.5, 1000.0, f32(1.2), f32(1.6)), ('space', 1)]
    for scene in range(120):
        commands.append(('advinit', 2.0, 1.0, f32(r.choice([2.0, 3.5, 6.0])), 0.1,
                         r.choice([50.0, 200.0])))
        order = list(GROUPS)
        r.shuffle(order)
        chosen = [GROUPS[3]] if scene % 6 == 5 else order[:r.choice([1, 4, 4])]
        for group in chosen:
            commands.append(('advgroup',) + tuple(f32(v) for v in group[:7]) + (group[7],))
        pivot = (0.0, f32(2.0 + r.uniform(0.0, 1.0)), 0.0)
        position = vec(r, 8.0)
        hits = []
        for identifier in range(r.choice([0, 1, 3, 6])):
            size = f32(r.choice([1.0, 4.0, 7.0, 12.0, r.uniform(2.0, 12.0)]))
            low = (0.0, 0.0, 0.0) if r.random() < 0.9 else (1.0, 1.0, 1.0)
            commands.append(('objbounds', identifier, low,
                             (size, f32(r.choice([2.0, 9.0, r.uniform(3.0, 7.0)])), size)))
            # Obstacles between the pivot and the camera, and at the camera itself.
            weight = r.choice([0.5, 0.5, 0.95])
            near = tuple(f32(pivot[i] * (1.0 - weight) + position[i] * weight) for i in range(3))
            hits.append(obstacle(r, identifier, near, r.choice([0, 0, 0x50]),
                                 r.choice([0, 0, 0, 1, 4])))
        commands.append(('sweep', hits))
        behind = [obstacle(r, identifier, position) for identifier in range(len(hits))]
        commands.append(('rear', behind if r.random() < 0.7 else []))
        commands.append(('segment', [obstacle(r, 99, pivot, r.choice([0, 0x50]))]
                         if r.random() < 0.6 else []))
        if r.random() < 0.3:
            commands.append(('advoffset', f32(r.uniform(-0.5, 3.0)), f32(r.uniform(0.0, 3.0))))
        for frame in range(r.choice([1, 3, 8])):
            step = tuple(f32(position[i] + r.uniform(-0.3, 0.3) * frame) for i in range(3))
            dt = f32(r.choice([0.016, 0.033, 0.1]))
            commands.append(('advcollide', dt, step, pivot, (0.0, 1.0, 0.0)))
            if r.random() < 0.3:
                commands.append(('scroll', f32(r.uniform(-1.0, 1.0))))
            if r.random() < 0.3:
                commands.append(('sweep', []))
            if r.random() < 0.2:
                commands.append(('sweep', hits))
    commands += [('space', 0), ('advcollide', 0.016, (1.0, 1.0, 1.0), (0.0, 2.0, 0.0), (0.0, 1.0, 0.0)),
                 ('space', 1)]
    return commands


def camera_commands(r):
    commands = [('camera', 0.5, 1000.0, f32(1.2), f32(1.6)), ('space', 1)]
    for unused in range(30):
        base = rotation(f32(r.uniform(-3.0, 3.0)), vec(r, 50.0)) if r.random() < 0.85 else None
        result = r.random() < 0.5
        commands.append(('segment', [obstacle(r, 7, (0.0, 0.0, 0.0), r.choice([0, 0x47, 0x64, 0x65]))]
                         if r.random() < 0.6 else []))
        commands.append(('tpp', base, f32(r.uniform(0.0, 3.0)), f32(r.uniform(0.0, 2.0)),
                         f32(r.uniform(2.0, 20.0)), rnd(r, 7.0), rnd(r, 1.5), (0.0, 1.0, 0.0),
                         result, vec(r, 20.0), r.choice([1, 2]), r.random() < 0.8))
    for unused in range(30):
        camera = vec(r, 20.0) if r.random() < 0.9 else None
        aim = vec(r, 20.0) if r.random() < 0.9 else camera
        pivot = vec(r, 5.0) if r.random() < 0.8 else None
        clip = (f32(r.uniform(-1.4, 1.4)), f32(r.uniform(-1.4, 1.4)))
        commands.append(('homing', camera, aim if aim is not None else (0.0, 0.0, 0.0), pivot,
                         (0.0, 1.0, 0.0), clip, r.random() < 0.5, vec(r, 20.0), 0.016))
    commands.append(('homing', (1.0, 2.0, 3.0), (1.0, 2.0, 3.0), None, (0.0, 1.0, 0.0),
                     (0.0, 0.15), False, (0.0, 0.0, 0.0), 0.016))
    return commands


def requirements(test):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client:
        test.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
    try:
        import stock_battle_camera_oracle as oracle
    except ImportError:
        test.skipTest('pefile and Unicorn are required for the original instruction oracle')
    return oracle, Path(client) / 'WorldOfTanks.exe'


class BattleCameraOracleTests(unittest.TestCase):
    coverage = None

    def compare(self, commands):
        oracle, executable = requirements(self)
        with tempfile.TemporaryDirectory(prefix='battle-camera-') as folder:
            binary = build_host(Path(folder))
            text = '\n'.join(host_text(c) for c in commands) + '\n'
            host = subprocess.run([str(binary)], input=text, capture_output=True, text=True, check=True)
        produced = host.stdout.splitlines()
        original = oracle.Scenario(executable, self.coverage).run(commands)
        self.assertEqual(len(produced), len(original))
        for index, (mine, theirs) in enumerate(zip(produced, original)):
            self.assertEqual(mine, theirs, 'command %d %r' % (index, commands[index][0]))
        return len(commands)

    def test_collision_geometry_matches_original(self):
        count = self.compare(leaf_commands(random.Random(1513)))
        print('Collider geometry matched the original x86: %d commands' % count)

    def test_space_bounds_match_original(self):
        count = self.compare(bounds_commands(random.Random(1514)))
        print('Space-bounds clamp matched the original x86: %d commands' % count)

    def test_advanced_collider_matches_original(self):
        count = self.compare(advanced_commands(random.Random(1515)))
        print('Advanced volume-group collider matched the original x86: %d commands' % count)

    def test_camera_updates_match_original(self):
        count = self.compare(camera_commands(random.Random(1516)))
        print('ThirdPersonProvider and HomingCamera updates matched the original x86: %d commands'
              % count)

    def test_scenarios_reach_every_original_branch(self):
        oracle, executable = requirements(self)
        reached = set()
        for commands in (advanced_commands(random.Random(1515)), camera_commands(random.Random(1516))):
            oracle.Scenario(executable, reached).run(commands)
        missing = sorted(name for address, name in oracle.BRANCHES.items() if address not in reached)
        self.assertEqual(missing, [])
        print('Scenarios reached %d original branches' % len(oracle.BRANCHES))


if __name__ == '__main__':
    unittest.main()
