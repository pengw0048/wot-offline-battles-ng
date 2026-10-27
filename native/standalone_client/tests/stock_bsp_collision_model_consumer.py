"""Exact #1513 ModelHitTester and ClientSelectableObject over the production binding.

Runs inside the CPython 2.7 host. argv: resource JSON, scripts.pkg, result JSON.
Math is a float64 stand-in for the PyMatrix/PyVector the consumers call.
"""
import imp
import json
import marshal
import math
import sys
import types
import zipfile

import BigWorld
import Host

resources = json.load(open(sys.argv[1]))
scripts = zipfile.ZipFile(sys.argv[2])
results = {}


def loader(name):
    entry = resources.get(name)
    if entry is None:
        return None
    low, high, bsp, flags = entry
    return low, high, None if bsp is None else bsp.decode('hex'), flags


Host.setLoader(loader)


class Vector3(object):
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x, self.y, self.z = float(x), float(y), float(z)

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    def __add__(self, o):
        return Vector3(self.x + o.x, self.y + o.y, self.z + o.z)

    def __sub__(self, o):
        return Vector3(self.x - o.x, self.y - o.y, self.z - o.z)

    def __mul__(self, s):
        return Vector3(self.x * s, self.y * s, self.z * s)

    def dot(self, o):
        return self.x * o.x + self.y * o.y + self.z * o.z


class Vector2(object):
    def __init__(self, x=0.0, y=0.0):
        self.x, self.y = float(x), float(y)


class Matrix(object):
    """Row-vector 4x4 like BigWorld Math.Matrix: p' = p * M."""

    def __init__(self, other=None):
        self.m = [row[:] for row in other.m] if other is not None else \
            [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]

    def setRotateYPR(self, ypr):
        yaw, pitch, roll = ypr
        sy, cy, sp, cp, sr, cr = (math.sin(yaw), math.cos(yaw), math.sin(pitch), math.cos(pitch),
                                  math.sin(roll), math.cos(roll))
        self.m = [[cr * cy + sr * sp * sy, sr * cp, -cr * sy + sr * sp * cy, 0.0],
                  [-sr * cy + cr * sp * sy, cr * cp, sr * sy + cr * sp * cy, 0.0],
                  [cp * sy, -sp, cp * cy, 0.0], [0.0, 0.0, 0.0, 1.0]]

    def translate(self, v):
        self.m[3][0] += v[0]
        self.m[3][1] += v[1]
        self.m[3][2] += v[2]

    def invert(self):
        r = [row[:3] for row in self.m[:3]]
        t = self.m[3][:3]
        det = (r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
               r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
               r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]))
        inv = [[(r[(j + 1) % 3][(i + 1) % 3] * r[(j + 2) % 3][(i + 2) % 3] -
                 r[(j + 1) % 3][(i + 2) % 3] * r[(j + 2) % 3][(i + 1) % 3]) / det for j in range(3)]
               for i in range(3)]
        ti = [-sum(t[k] * inv[k][c] for k in range(3)) for c in range(3)]
        self.m = [inv[0] + [0.0], inv[1] + [0.0], inv[2] + [0.0], ti + [1.0]]

    def applyPoint(self, p):
        p = list(p)
        return Vector3(*[p[0] * self.m[0][c] + p[1] * self.m[1][c] + p[2] * self.m[2][c] + self.m[3][c]
                         for c in range(3)])

    def applyVector(self, v):
        v = list(v)
        return Vector3(*[v[0] * self.m[0][c] + v[1] * self.m[1][c] + v[2] * self.m[2][c] for c in range(3)])


def module(name, **values):
    result = types.ModuleType(name)
    result.__dict__.update(values)
    sys.modules[name] = result
    return result


logged = []
module('Math', Vector3=Vector3, Vector2=Vector2, Matrix=Matrix)
module('constants', IS_DEVELOPMENT=False, IS_CLIENT=True, IS_BOT=False)
module('debug_utils', LOG_DEBUG=lambda *a: None, LOG_ERROR=lambda *a: logged.append(a))
module('SoundGroups')


class Entity(object):
    def __init__(self):
        pass


BigWorld.Entity = Entity
BigWorld.DumbFilter = lambda: 'DumbFilter'
BigWorld.Servo = lambda matrix: ('Servo', matrix)


def load_pyc(name, path):
    data = scripts.read(path)
    assert data[:4] == imp.get_magic()
    result = types.ModuleType(name)
    result.__file__ = path
    sys.modules[name] = result
    exec(marshal.loads(data[8:]), result.__dict__)
    return result


ModelHitTester = load_pyc('ModelHitTester', 'scripts/common/ModelHitTester.pyc')
ClientSelectableObject = load_pyc('ClientSelectableObject', 'scripts/client/ClientSelectableObject.pyc')


class Section(object):
    def __init__(self, values):
        self.values = values

    def readString(self, key):
        return self.values.get(key, '')


def hits(value):
    if value is None:
        return None
    return [[d, list(n), c, k] for d, n, c, k in value]


def raises(call):
    try:
        call()
    except Exception as error:
        return [type(error).__name__, str(error)]
    return None


queries = json.load(open(sys.argv[1] + '.queries'))

# Every vehicle component through the exact ModelHitTester constructor/load.
for component, name in queries['components']:
    tester = ModelHitTester.ModelHitTester(Section({'collisionModelClient': name}))
    tester.loadBspModel()
    bbox = tester.bbox
    entry = results.setdefault('components', {}).setdefault(component, {})
    entry['bbox'] = [list(bbox[0]), list(bbox[1]), bbox[2]]
    entry['local'] = [hits(tester.localHitTest(Vector3(*s), Vector3(*e))) for s, e in queries['segments'][component]]
    entry['any'] = [tester.localAnyHitTest(Vector3(*s), Vector3(*e)) for s, e in queries['segments'][component]]
    world = Matrix()
    world.setRotateYPR(queries['world'][0])
    world.translate(queries['world'][1])
    entry['world'] = [hits(tester.worldHitTest(world.applyPoint(s), world.applyPoint(e), world))
                      for s, e in queries['segments'][component][:8]]
    entry['worldMatrix'] = world.m
    entry['sphere'] = [[[list(p), k] for p, k in tester.localSphericHitTest(Vector3(*c), r, outside)]
                       for c, r, outside in queries['spheres'][component]]
    entry['triangle'] = [bool(tester.localCollidesWithTriangle(tuple(Vector3(*v) for v in tri), Vector3(*d)))
                         for tri, d in queries['triangles'][component]]
    entry['native'] = list(Host.nativeOwner(tester.getBspModel()))
    entry['triangles'] = len(Host.treeTriangles(tester.getBspModel()))
    tester.releaseBspModel()
    entry['released'] = [tester.isBspModelLoaded(), hasattr(tester, 'bbox')]

# Shared resource ownership and repeated load/destruction.
first, second = [ModelHitTester.ModelHitTester(Section({'collisionModelClient': queries['shared']}))
                 for _ in range(2)]
first.loadBspModel()
second.loadBspModel()
owners = [Host.nativeOwner(first.getBspModel()), Host.nativeOwner(second.getBspModel())]
results['shared'] = {'sameTree': owners[0][0] == owners[1][0] and owners[0][0] != 0,
                     'sameModel': owners[0][1] == owners[1][1], 'live': Host.liveModels()}
first.releaseBspModel()
results['shared']['afterFirst'] = Host.liveModels()
second.releaseBspModel()
results['shared']['afterSecond'] = Host.liveModels()
for _ in range(64):
    tester = ModelHitTester.ModelHitTester(Section({'collisionModelClient': queries['shared']}))
    tester.loadBspModel()
    tester.localHitTest(Vector3(*queries['segments']['hull'][0][0]), Vector3(*queries['segments']['hull'][0][1]))
    tester.releaseBspModel()
results['shared']['afterRepeat'] = Host.liveModels()

# Missing and BSP-less resources.
missing = ModelHitTester.ModelHitTester(Section({'collisionModelClient': queries['missing']}))
results['missing'] = {'load': raises(missing.loadBspModel), 'loaded': missing.isBspModelLoaded()}
raw = BigWorld.WGBspCollisionModel()
results['empty'] = {'segment': raw.collideSegment((0, 0, 0), (0, 0, 1)),
                    'any': raw.collideSegmentAny((0, 0, 0), (0, 0, 1)), 'bbox': raw.getBoundingBox(),
                    'sphere': raises(lambda: raw.collideSphere((0, 0, 0), 1.0, False)),
                    'triangle': raises(lambda: raw.collidesWithTriangle(((0, 0, 0), (1, 0, 0), (0, 1, 0)), (0, 0, 1)))}
results['nobsp'] = {'first': raw.setModelName(queries['nobsp']), 'again': raw.setModelName(queries['nobsp']),
                    'segment': raises(lambda: raw.collideSegment((0, 0, 0), (0, 0, 1))),
                    'bbox': raw.getBoundingBox() is not None}
results['missingAfterGood'] = [raw.setModelName(queries['shared']), raw.setModelName(queries['missing']),
                               raw.collideSegment((0, 0, 0), (0, 0, 1)), raw.getBoundingBox()]
results['destructible'] = raw.setDestructibleModelName(queries['shared'], 7, 0x12345)
del raw

# Exact argument contracts generated by Script::argCountError and setData.
probe = BigWorld.WGBspCollisionModel(1, 2, 3)
results['errors'] = {
    'setModelName': raises(lambda: probe.setModelName()),
    'setDestructibleModelName': raises(lambda: probe.setDestructibleModelName('a')),
    'setModel': raises(lambda: probe.setModel(1)), 'setModelNone': probe.setModel(None),
    'collideSegment': raises(lambda: probe.collideSegment((0, 0, 0))),
    'collideSegmentAny': raises(lambda: probe.collideSegmentAny()),
    'collideSphere': raises(lambda: probe.collideSphere((0, 0, 0), 1.0)),
    'collidesWithTriangle': raises(lambda: probe.collidesWithTriangle([(0, 0, 0)] * 3, (0, 0, 1))),
    'getBoundingBox': raises(lambda: probe.getBoundingBox(1)),
    'nameType': raises(lambda: probe.setModelName(3)),
    'methods': sorted(name for name in dir(probe) if not name.startswith('__'))}

# ClientSelectableObject: PyModel prerequisite, world->model transform, nearest hit.
selectable = ClientSelectableObject.ClientSelectableObject()
selectable.modelName = queries['selectable']
model = BigWorld.Model(str(queries['selectable']))
model.addMotor = lambda motor: None
selectable.matrix = 'entity-matrix'


class Prereqs(dict):
    failedIDs = ()


selectable.onEnterWorld(Prereqs({queries['selectable']: model}))
world = Matrix()
world.setRotateYPR(queries['world'][0])
world.translate(queries['world'][1])
model.matrix = world
picks = []
for s, e in queries['segments']['selectable']:
    result = selectable.collideSegment(world.applyPoint(s), world.applyPoint(e))
    picks.append(None if result is None else [result.dist, result.hitAngleCos, result.armor])
results['selectable'] = {'picks': picks, 'logged': len(logged),
                         'native': list(Host.nativeOwner(selectable._ClientSelectableObject__bspModel))}
failed = ClientSelectableObject.ClientSelectableObject()
failed.modelName = queries['missing']
failed.matrix = 'entity-matrix'
failed_model = BigWorld.Model(str(queries['missing']))
failed_model.addMotor = lambda motor: None
failed.onEnterWorld(Prereqs({queries['missing']: failed_model}))
results['selectable']['failedLogged'] = [list(entry[1:]) for entry in logged]
results['selectable']['failedPick'] = failed.collideSegment(Vector3(0, 0, 0), Vector3(0, 0, 1))
del selectable, model, failed, failed_model, first, second, missing, tester, probe

json.dump(results, open(sys.argv[3], 'w'))
