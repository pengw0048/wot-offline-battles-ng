"""Real CPython 2.7 consumer of the production battle scene-query binding.

Executed by stock_battle_scene_queries_host.cpp. Exact #1513 bytecode runs when
STOCK_SCRIPTS_PKG names the client's scripts.pkg; engine owners are fixtures.
"""
import gc
import marshal
import math
import os
import StringIO
import sys
import types
import zipfile

import Host
import _BigWorld


class Vector3(object):
    def __init__(self, x=0.0, y=0.0, z=0.0):
        if isinstance(x, (Vector3, tuple, list)):
            x, y, z = x
        self.x, self.y, self.z = float(x), float(y), float(z)

    def __len__(self):
        return 3

    def __getitem__(self, index):
        return (self.x, self.y, self.z)[index]

    def __sub__(self, other):
        return Vector3(self.x - other[0], self.y - other[1], self.z - other[2])

    def __add__(self, other):
        return Vector3(self.x + other[0], self.y + other[1], self.z + other[2])

    def __eq__(self, other):
        return tuple(self) == tuple(other)

    def __ne__(self, other):
        return not self == other

    def __repr__(self):
        return 'Vector3(%r, %r, %r)' % (self.x, self.y, self.z)

    @property
    def length(self):
        return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)

    def dot(self, other):
        return self.x * other[0] + self.y * other[1] + self.z * other[2]

    def scale(self, value):
        return Vector3(self.x * value, self.y * value, self.z * value)


class Vector4(object):
    def __init__(self, x=0.0, y=0.0, z=0.0, w=0.0):
        self.x, self.y, self.z, self.w = float(x), float(y), float(z), float(w)

    def __getitem__(self, index):
        return (self.x, self.y, self.z, self.w)[index]

    def __len__(self):
        return 4

    @property
    def length(self):
        return math.sqrt(sum(v * v for v in self))


class Vector2(object):
    def __init__(self, x=0.0, y=0.0):
        self.x, self.y = float(x), float(y)


Math = types.ModuleType('Math')
Math.Vector2, Math.Vector3, Math.Vector4 = Vector2, Vector3, Vector4
sys.modules['Math'] = Math


class Player(object):
    spaceID = 1


BigWorld = types.ModuleType('BigWorld')
BigWorld.wg_collideSegment = _BigWorld.wg_collideSegment
BigWorld.wg_collideWater = _BigWorld.wg_collideWater
BigWorld.wg_getSpaceBounds = _BigWorld.wg_getSpaceBounds
BigWorld.player = lambda: Player
sys.modules['BigWorld'] = BigWorld

collide = BigWorld.wg_collideSegment
TRIANGLE = ((-1.0, 0.0, -1.0), (0.0, 0.0, 1.0), (1.0, 0.0, -1.0))  # faces +y
UP = (0.0, 10.0, 0.0)
DOWN = (0.0, -10.0, 0.0)
TERRAIN, NOCOLLIDE, WATER, PROJECTILENOCOLLIDE = 8, 16, 64, 128


def expect(kind, function, message=None):
    try:
        function()
    except kind as error:
        if message is not None:
            assert str(error) == message, (str(error), message)
        return error
    raise AssertionError('%s was not raised' % kind.__name__)


def stderr(function):
    saved, sys.stderr = sys.stderr, StringIO.StringIO()
    try:
        return function(), sys.stderr.getvalue()
    finally:
        sys.stderr = saved


def close(a, b, tolerance=1e-5):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def space(identity, *surfaces, **options):
    Host.removeSpace(identity)
    Host.addSpace(identity)
    Host.setPrune(identity, options.get('prune', True))
    for flags, distance in surfaces:
        Host.addSurface(identity, flags, distance, TRIANGLE, options.get('matrix'),
                        options.get('identity'))
    Host.visits()


def surface(identity, flags, distance, vertices=TRIANGLE, matrix=None, item=None):
    Host.addSurface(identity, flags, distance, vertices, matrix, item)


def material(kind, flags=0):
    return (kind << 8) | flags


# Missing space: None, with the original pending ValueError reported once.
result, text = stderr(lambda: collide(77, UP, DOWN, 0))
assert result is None and 'ValueError: No space ID 77' in text, text
assert sys.exc_info() == (None, None, None)

# No surface and a cleared space are both a plain miss.
space(1)
assert collide(1, UP, DOWN, 0) is None
Host.clearSpace(1)
assert collide(1, UP, DOWN, 0) is None and Host.visits() == []

# One terrain surface: the exact six-item tuple, traversed at the highest terrain LOD.
space(1, (material(3, TERRAIN), 4.0))
Host.highestLod()
hit = collide(1, UP, DOWN, 0)
assert Host.highestLod() == [True]
assert isinstance(hit, tuple) and len(hit) == 6, hit
assert isinstance(hit[0], Vector3) and hit[0] == Vector3(0.0, 6.0, 0.0), hit[0]
assert close(hit[1], (0.0, 1.0, 0.0)), hit[1]
assert hit[2] == 3 and type(hit[2]) is int
assert hit[3] == Vector3(0.0, 0.0, 0.0)
assert hit[4] == 0 and hit[5] == 0 and type(hit[4]) is int and type(hit[5]) is int

# The skip mask is the full 16-bit flag word, including material bits.
space(1, (material(3, TERRAIN), 2.0), (material(4, 0), 5.0))
assert collide(1, UP, DOWN, TERRAIN)[2] == 4
space(1, (material(3, TERRAIN), 2.0), (material(4, 0), 5.0))
assert collide(1, UP, DOWN, 3 << 8)[2] == 4
space(1, (material(3, PROJECTILENOCOLLIDE), 2.0), (material(5, TERRAIN), 3.0))
assert collide(1, UP, DOWN, PROJECTILENOCOLLIDE)[2] == 5
expect(TypeError, lambda: collide(1, UP, DOWN, 0x10000), 'Integer is out of range')
expect(TypeError, lambda: collide(1, UP, DOWN, -1), 'Integer is out of range')
expect(TypeError, lambda: collide(1, UP, DOWN))
expect(TypeError, lambda: collide(1, UP, DOWN, 0, None, None))

# An int fifth argument selects low collision-flag bits; bool is an int.
space(1, (material(9, 0), 1.0), (material(3, TERRAIN), 2.0))
assert collide(1, UP, DOWN, 128, 8)[2] == 3
space(1, (material(9, 0), 1.0), (material(3, TERRAIN), 2.0))
assert collide(1, UP, DOWN, 0, False) is None
space(1, (material(9, 0), 1.0), (material(3, 1), 2.0))
assert collide(1, UP, DOWN, 0, True)[2] == 3
space(1, (material(9, 0), 1.0), (material(3, TERRAIN), 2.0))
assert collide(1, UP, DOWN, 0, 8 << 8) is None
# A negative int and every non-int, non-callable object leave no selection.
for ignored in (-1, -8, None, 'flags', 8.0, 8L, (8,)):
    space(1, (material(9, 0), 1.0), (material(3, TERRAIN), 2.0))
    assert collide(1, UP, DOWN, 0, ignored)[2] == 9, ignored

# A callable sees (matKind, collFlags, itemIndex, chunkID) and only an int zero rejects.
calls = []


def reject_front(*args):
    calls.append(args)
    return len(calls) != 1 and None


space(1)
surface(1, material(87, TERRAIN | 2), 1.0, item=(0x81234567, 0x12345))
surface(1, material(4, 0), 3.0, item=(12, 7))
hit = collide(1, UP, DOWN, 0, reject_front)
assert calls == [(87, TERRAIN | 2, 0x2345, 0x81234567L), (4, 0, 7, 12)], calls
assert type(calls[0][3]) is long and type(calls[1][3]) is int
assert hit[2] == 4 and hit[4] == 7 and hit[5] == 12 and hit[0] == Vector3(0.0, 7.0, 0.0)
assert [v[3] for v in Host.visits()] == [3, 1]
for verdict, rejected in ((False, True), (0, True), (0L, False), (None, False), (1, False),
                          (True, False), ('', False), ([], False)):
    space(1, (material(87, 0), 1.0), (material(4, 0), 3.0))
    front = lambda *a: verdict if a[0] == 87 else None
    assert collide(1, UP, DOWN, 0, front)[2] == (4 if rejected else 87), verdict
# A raised error is printed, cleared and the surface accepted; the query goes on.
space(1, (material(87, 0), 1.0), (material(4, 0), 3.0))
result, text = stderr(lambda: collide(1, UP, DOWN, 0, lambda *a: 1 / 0))
assert result[2] == 87 and 'ZeroDivisionError' in text and sys.exc_info()[0] is None
# The int selection wins; the callable is never consulted.
space(1, (material(9, 0), 1.0), (material(3, TERRAIN), 2.0))
del calls[:]
assert collide(1, UP, DOWN, 0, 8)[2] == 3 and calls == []
# The skip mask runs before the callable.
space(1, (material(9, WATER), 1.0), (material(3, 0), 2.0))
assert collide(1, UP, DOWN, WATER, lambda *a: calls.append(a))[2] == 3 and calls == [(3, 0, 0, 0)]

# A surface farther than the accepted one returns COLLIDE_BEFORE without a call.
del calls[:]
space(1, (material(3, 0), 2.0), (material(4, 0), 5.0), prune=False)
assert collide(1, UP, DOWN, 0, lambda *a: calls.append(a))[2] == 3
assert len(calls) == 1 and [v[3] for v in Host.visits()] == [1, 1]
# A nearer surface reported later replaces the recorded hit.
space(1, (material(4, 0), 5.0), (material(3, 0), 2.0), prune=False)
hit = collide(1, UP, DOWN, 0)
assert hit[2] == 3 and hit[0] == Vector3(0.0, 8.0, 0.0)
# An equal distance keeps the first record.
space(1, (material(4, 0), 2.0), (material(3, 0), 2.0), prune=False)
assert collide(1, UP, DOWN, 0)[2] == 4

# Normal: normalised (v2-v0)x(v2-v1) through the obstacle transform; origin row.
rotation = [0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 5.0, 6.0, 7.0, 1.0]
space(1)
surface(1, 0, 1.0, ((0.0, 0.0, 0.0), (0.0, 3.0, 0.0), (0.0, 0.0, 3.0)), rotation, (3, 4))
hit = collide(1, UP, DOWN, 0)
assert close(hit[1], (0.0, 0.0, 1.0)) and abs(hit[1].length - 1.0) < 1e-6, hit[1]
assert hit[3] == Vector3(5.0, 6.0, 7.0) and hit[4:] == (4, 3)
space(1)
surface(1, 0, 1.0, ((0.0, 0.0, 0.0), (0.0, 0.0, 3.0), (0.0, 3.0, 0.0)))
assert close(collide(1, UP, DOWN, 0)[1], (-1.0, 0.0, 0.0))
space(1)
surface(1, 0, 1.0, ((-1.0, 0.0, -1.0), (1.0, 0.0, -1.0), (0.0, 0.0, 1.0)))
assert close(collide(1, UP, DOWN, 0)[1], (0.0, -1.0, 0.0))  # winding, not ray side
# Zero-length segment: 01172190 rejects a zero direction, so even a present
# surface at the point is not reported.
space(1, (0, 0.5))
assert collide(1, UP, UP, 0) is None and Host.visits() == []

# An identity-owner failure aborts with that error; an unknown identity is 0/0.
space(1)
surface(1, 0, 1.0, item='raise')
expect(RuntimeError, lambda: collide(1, UP, DOWN, 0), 'host identity owner failed')
space(1)
surface(1, 0, 1.0, item='raise')
expect(RuntimeError, lambda: collide(1, UP, DOWN, 0, lambda *a: None))

# Teardown inside the filter. Two triangles share one BSP node: the SDK node
# loop reads the second after the visitor stops, so the node must outlive the
# clear until the outermost collide returns. Logical clearing is immediate.
def node(identity, kinds, mapping=0):
    Host.addSurface(identity, material(kinds[0], 0), 1.0, TRIANGLE, None, (9, 0), mapping)
    for index, kind in enumerate(kinds[1:], 1):
        Host.addSurface(identity, material(kind, 0), 1.0 + index, TRIANGLE, None, None, mapping, 0)


space(2)
node(2, (87, 4))
Host.retirements()
live = Host.liveSpaces()
seen = []


def teardown(*args):
    seen.append(args[0])
    Host.clearSpace(2)
    Host.removeSpace(2)
    assert Host.liveSpaces() == live and Host.queryActive()
    names, retired = Host.retirements()
    assert names == ['sounds', 'spaceData'] and retired == 0, (names, retired)
    return False


assert collide(2, UP, DOWN, 0, teardown) is None and seen == [87]
# The node loop still visits the second triangle; the query answers STOP.
assert [v[3] for v in Host.visits()] == [0, 0]
r = Host.retirements()
assert r == (['clear'], 1) and Host.liveSpaces() == live - 1, r
assert not Host.queryActive()

# A filter that clears and then raises: printed, no surface, one retirement.
space(2)
node(2, (87, 4))
Host.retirements()


def clear_and_raise(*args):
    Host.clearSpace(2)
    raise RuntimeError('filter failed after clearing')


result, text = stderr(lambda: collide(2, UP, DOWN, 0, clear_and_raise))
assert result is None and 'filter failed after clearing' in text and sys.exc_info()[0] is None
assert Host.retirements() == (['sounds', 'spaceData', 'clear'], 1) and not Host.queryActive()

# Repeated clear and clearAllSpaces inside a filter retire each space once.
for unused in range(8):
    Host.removeSpace(unused)
space(2)
node(2, (87, 4))
space(4)
node(4, (5, 6))
Host.retirements()


def clear_all(*args):
    Host.clearSpace(2)
    Host.clearAllSpaces()
    Host.clearSpace(2)
    return False


assert collide(2, UP, DOWN, 0, clear_all) is None
assert Host.retirements() == (['sounds', 'spaceData', 'sounds', 'spaceData', 'sounds',
                               'spaceData', 'sounds', 'spaceData', 'clear', 'clear'], 2)
Host.clearSpace(2)  # Outside a query the original repeats doClear at once.
assert Host.retirements() == (['sounds', 'spaceData', 'clear'], 0)

# A nested query that clears the outer space waits for the outer collide.
space(2)
node(2, (87, 4))
space(5, (7, 1.0))
Host.retirements()
order = []


def inner(*args):
    Host.clearSpace(2)
    return True


def outer(*args):
    if not order:
        collide(5, UP, DOWN, 0, inner)
        order.append(Host.retirements())
    return True


assert collide(2, UP, DOWN, 0, outer) is None
assert order == [(['sounds', 'spaceData'], 0)], order
assert Host.retirements() == (['clear'], 1)

# Unmapping inside a filter is deferred the same way; the space stays live.
space(6)
node(6, (87, 4), mapping=1)
Host.addSurface(6, material(3, 0), 5.0, TRIANGLE, None, (9, 9), 2)
Host.retirements()


def unmap(*args):
    if args[0] == 87:
        Host.delMapping(6, 1)
        assert Host.retirements() == ([], 0)
    return args[0] == 3


hit = collide(6, UP, DOWN, 0, unmap)
assert hit[2] == 3 and hit[5] == 9
assert Host.retirements() == (['delMapping'], 1)
assert collide(6, UP, DOWN, 0)[2] == 3
Host.delMapping(6, 2)  # Outside a query the mapping retires at once.
assert Host.retirements() == (['delMapping'], 1) and collide(6, UP, DOWN, 0) is None

# Re-entrant queries from a filter own separate state.
space(3, (material(87, 0), 1.0), (material(4, 0), 3.0))
nested = []


def reenter(*args):
    if not nested:
        nested.append(collide(3, UP, DOWN, 0))
    return args[0] != 87


hit = collide(3, UP, DOWN, 0, reenter)
assert hit[2] == 4 and nested[0][2] == 87
# The callable stays alive even when its only owner releases it mid-query.
holder = {}


class Filter(object):
    def __call__(self, *args):
        holder.clear()
        gc.collect()
        return True


holder['f'] = Filter()
space(3, (material(87, 0), 1.0))
assert collide(3, UP, DOWN, 0, holder['f'])[2] == 87

# Native f642b0 seam (ClosestCcbNative): composite identity, bool filter.
native = Host.nativeCollide
KNOWN = 1 << 48


def identity(chunk, item):
    return chunk | (item << 32) | KNOWN


space(7)
surface(7, material(9, TERRAIN), 1.0)                      # absent identity
surface(7, material(80, 2), 2.0, item=(0, 0))               # known (0, 0)
surface(7, material(81, 0), 3.0, item=(0x81234567, 0x12345))
surface(7, material(82, 0), 4.0, item=(0x81234567, 3))
surface(7, material(83, 0), 5.0, item=(5, 0x2345))
Host.setPrune(7, False)
calls = []
hit = native(7, UP, DOWN, 0, lambda *a: calls.append(a) or True)
assert hit[2] == 9 and hit[4] == 0 and hit[0] == (0.0, 9.0, 0.0), hit
assert close(hit[1], (0.0, 1.0, 0.0)) and hit[3] == (0.0, 0.0, 0.0)
# Farther surfaces are not offered once a nearer one is recorded.
assert calls == [(9, TERRAIN, 0)], calls
del calls[:]
assert native(7, UP, DOWN, TERRAIN, lambda *a: calls.append(a) or a[0] == 83)[4] == identity(5, 0x2345)
assert calls == [(80, 2, KNOWN), (81, 0, identity(0x81234567, 0x2345)),
                 (82, 0, identity(0x81234567, 3)), (83, 0, identity(5, 0x2345))], calls
# Known (0, 0) differs from absent; equality covers both halves (008aa7b0).
for wanted, kind in ((KNOWN, 80), (identity(0x81234567, 0x2345), 81), (identity(0x81234567, 3), 82),
                     (identity(5, 0x2345), 83)):
    hit = native(7, UP, DOWN, 0, lambda m, f, i: i == wanted)
    assert hit[2] == kind and hit[4] == wanted, (kind, hit)
assert native(7, UP, DOWN, 0, lambda m, f, i: i == identity(0, 0x2345)) is None
assert native(7, UP, DOWN, TERRAIN, None)[4] == KNOWN         # empty filter: nearest
assert native(7, UP, DOWN, 0xffff, None) is None              # full 16-bit skip
# A rejected front destructible reveals the farther wall.
space(8, (material(87, 0), 1.0), (material(4, 0), 3.0))
assert native(8, UP, DOWN, 0x80, lambda m, f, i: m != 87)[2] == 4
assert Host.highestLod()[-1] is True
# The equal-point candidate recheck never re-hits, present or removed.
Host.visits()
assert native(8, (0, 9, 0), (0, 9, 0), 0x80, None) is None and Host.visits() == []
# Missing, cleared and torn-down spaces are plain misses with no Python error.
assert native(99, UP, DOWN, 0, None) is None
space(9, (material(87, 0), 1.0), (material(4, 0), 3.0))
Host.retirements()
assert native(9, UP, DOWN, 0, ('clear', 9)) is None
assert Host.retirements() == (['sounds', 'spaceData', 'clear'], 2)
assert native(9, UP, DOWN, 0, None) is None
# An identity-owner failure is reported and cleared, never left pending.
space(10)
surface(10, 0, 1.0, item='raise')
result, text = stderr(lambda: native(10, UP, DOWN, 0, None))
assert result is None and 'host identity owner failed' in text and sys.exc_info()[0] is None

# Water: first positive distance wins; default third argument is True.
water = BigWorld.wg_collideWater
Host.clearWaters()
assert water(UP, DOWN) == -1.0 and water(UP, DOWN, False) == -1.0
expect(TypeError, lambda: water(UP))
expect(TypeError, lambda: water(UP, DOWN, True, 1))
TRANSLATE = lambda x, y, z: [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, x, y, z, 1.0]
# 4x4 vertex grid, 3x3 m cells, origin (0,0,0): x in [-1.5, 1.5].
GRID = [0xff000000] * 16
GRID[0] = 0x00000000          # vertex (0,0) is a dry edge
Host.addWater(TRANSLATE(0, 2.0, 0), (3.0, 3.0), 1.0, True, (4, 4), GRID,
              (-1.5, -1.5, 1.5, 1.5), (1, 1))
assert abs(water((0, 10, 0), (0, -10, 0), False) - 8.0) < 1e-6
assert abs(water((0, 10, 0), (0, -10, 0)) - 8.0) < 1e-6
corner = ((-1.45, 10, -1.45), (-1.45, -10, -1.45))
assert abs(water(corner[0], corner[1], False) - 8.0) < 1e-6
assert water(corner[0], corner[1]) == -1.0 and water(corner[0], corner[1], True) == -1.0
# Distance is measured along the segment from its start, not as a height.
assert abs(water((0, 22, 0), (0, -3, 0), False) - 20.0) < 1e-6
assert abs(water((0, 10, 0), (0, 2.5, 0), False) - 8.0) < 1e-6
assert water((0, 1, 0), (0, -10, 0), False) == -1.0
assert water((0, 2, 0), (0, -10, 0), False) == -1.0  # zero distance is not a hit
assert water((2.0, 10, 0), (2.0, -10, 0), False) == -1.0
# An edge-alpha water that is not alpha-enabled ignores its table.
Host.clearWaters()
Host.addWater(TRANSLATE(0, 2.0, 0), (3.0, 3.0), 1.0, False, (4, 4), [0] * 16,
              (-1.5, -1.5, 1.5, 1.5), (1, 1))
assert abs(water(*corner) - 8.0) < 1e-6
# Order: an uninitialised or invisible water yields to the next one.
Host.clearWaters()
Host.addWater(TRANSLATE(0, 5.0, 0), (3.0, 3.0), 1.0, False, (4, 4), [], (-1.5, -1.5, 1.5, 1.5), (0, 1))
Host.addWater(TRANSLATE(0, 4.0, 0), (3.0, 3.0), 1.0, False, (4, 4), [], (-1.5, -1.5, 1.5, 1.5), (1, 0))
Host.addWater(TRANSLATE(0, 3.0, 0), (3.0, 3.0), 1.0, False, (4, 4), [], (-1.5, -1.5, 1.5, 1.5), (1, 1))
Host.addWater(TRANSLATE(0, 6.0, 0), (3.0, 3.0), 1.0, False, (4, 4), [], (-1.5, -1.5, 1.5, 1.5), (1, 1))
assert abs(water(UP, DOWN) - 7.0) < 1e-6
Host.clearWaters()

# Bounds: camera space x/z extents, a zero Vector4 without one.
bounds = BigWorld.wg_getSpaceBounds
assert tuple(bounds()) == (0.0, 0.0, 0.0, 0.0) and bounds().length == 0.0
expect(TypeError, lambda: bounds(1))
space(5)
Host.setBounds(5, -400.0, -50.0, -300.0, 500.0, 90.0, 600.0)
Host.setCamera(5)
assert tuple(bounds()) == (-400.0, -300.0, 500.0, 600.0)
Host.removeSpace(5)
assert tuple(bounds()) == (-400.0, -300.0, 500.0, 600.0)  # camera still owns it
Host.setCamera(None)
assert tuple(bounds()) == (0.0, 0.0, 0.0, 0.0)


# Exact #1513 consumers.
def code(archive, member, path):
    value = marshal.loads(archive.read('scripts/' + member + '.pyc')[8:])
    for name in path:
        value = [c for c in value.co_consts
                 if isinstance(c, types.CodeType) and c.co_name == name][0]
    return value


package = os.environ.get('STOCK_SCRIPTS_PKG')
if package:
    archive = zipfile.ZipFile(package)
    aiming = 'client/AvatarInputHandler/AimingSystems/__init__'
    scope = {'BigWorld': BigWorld, 'Math': Math, 'Vector3': Vector3, '__builtins__': __builtins__}
    terrain_only = types.FunctionType(code(archive, aiming, ['__collideTerrainOnly']), scope)
    static_only = types.FunctionType(code(archive, aiming, ['__collideStaticOnly']), scope)
    space(1, (material(9, PROJECTILENOCOLLIDE), 1.0), (material(9, 0), 2.0),
          (material(3, TERRAIN), 5.0))
    point, normal = static_only(Vector3(UP), Vector3(DOWN))
    assert point == Vector3(0.0, 8.0, 0.0) and normal is None
    space(1, (material(9, 0), 2.0), (material(3, TERRAIN), 5.0))
    Host.clearWaters()
    point = terrain_only(Vector3(UP), Vector3(DOWN))
    assert point == Vector3(0.0, 5.0, 0.0), point
    Host.addWater(TRANSLATE(0, 7.0, 0), (3.0, 3.0), 1.0, False, (4, 4), [],
                  (-1.5, -1.5, 1.5, 1.5), (1, 1))
    # The water surface 3 m below start beats terrain 2 m under it.
    assert terrain_only(Vector3(UP), Vector3(DOWN)) == Vector3(0.0, 7.0, 0.0)
    Host.clearWaters()

    sniper = code(archive, 'client/AvatarInputHandler/DynamicCameras/SniperCamera',
                  ['SniperCamera', '__isPositionUnderwater'])
    under = types.FunctionType(sniper, scope)
    Host.addWater(TRANSLATE(0, 7.0, 0), (3.0, 3.0), 1.0, False, (4, 4), [],
                  (-1.5, -1.5, 1.5, 1.5), (1, 1))
    assert under(None, Vector3(0.0, 6.5, 0.0)) and not under(None, Vector3(0.0, 7.5, 0.0))
    Host.clearWaters()

    arena = 'client_common/ClientArena'
    arena_scope = {'BigWorld': BigWorld, 'Math': Math, '__builtins__': __builtins__}
    arena_scope['_convertToList'] = types.FunctionType(code(archive, arena, ['_convertToList']),
                                                       arena_scope)
    colliders = []
    arena_scope['_BBCollider'] = lambda box, heights: colliders.append((box, heights)) or box
    setup = types.FunctionType(code(archive, arena, ['ClientArena', '__setupBBColliders']),
                               arena_scope)

    class Arena(object):
        class arenaType(object):
            boundingBox = ((-1.0, -1.0), (1.0, 1.0))

    assert setup(Arena()) is False and colliders == []
    space(6)
    Host.setBounds(6, -400.0, -50.0, -300.0, 500.0, 90.0, 600.0)
    Host.setCamera(6)
    assert setup(Arena()) is True
    assert colliders[1] == (((-400.0, -300.0), (500.0, 600.0)), (-500.0, 500.0)), colliders
    Host.setCamera(None)
    print('Exact #1513 AimingSystems, SniperCamera and ClientArena consumers passed')
