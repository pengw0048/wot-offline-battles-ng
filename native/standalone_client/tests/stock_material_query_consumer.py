"""Exercise the complete material-near-point native operation and original PYC."""
import collections
import json
import marshal
import os
import sys
import types
import zipfile

import Host
import _BigWorld


class Vector3(tuple):
    def __new__(cls, *value):
        if len(value) == 1:
            value = value[0]
        return tuple.__new__(cls, value)
    def __add__(self, other):
        return Vector3(*(a + b for a, b in zip(self, other)))
    def __sub__(self, other):
        return Vector3(*(a - b for a, b in zip(self, other)))
    def __mul__(self, other):
        return Vector3(*(a * other for a in self))


math_module = types.ModuleType('Math')
math_module.Vector3 = Vector3
sys.modules['Math'] = math_module
TRIANGLE = ((-1., 0., -1.), (0., 0., 1.), (1., 0., -1.))
START, END = Vector3(0., 10., 0.), Vector3(0., -10., 0.)
query = _BigWorld.wg_getMatInfoNearPoint


def prepare(surfaces):
    Host.addSpace(1)
    for surface in surfaces:
        flags, distance = surface[:2]
        identity = surface[2] if len(surface) > 2 else None
        Host.addSurface(1, flags, distance, TRIANGLE, None, tuple(identity) if identity is not None else None)


def expect(error, callback):
    try:
        callback()
    except error:
        return
    raise AssertionError('Expected ' + error.__name__)


if os.environ.get('MATERIAL_QUERY_CASES'):
    cases = json.load(open(os.environ['MATERIAL_QUERY_CASES']))
    out = []
    for case in cases:
        prepare(case['surfaces'])
        calls = []
        def broken(*args):
            calls.append(args)
            return case['reject']
        result = query(1, START, END, Vector3(*case['point']), broken) if case['callback'] else query(1, START, END, Vector3(*case['point']))
        out.append({'result': list(result), 'calls': calls, 'visits': Host.visits()})
    json.dump(out, open(os.environ['MATERIAL_QUERY_OUTPUT'], 'w'))
    sys.exit(0)

prepare([(1 << 8, 1.), (2 << 8, 10.), (3 << 8, 18.), ((4 << 8) | 128, 19.)])
r = query(1, START, END, Vector3(0., -9., 0.))
assert r == (True, (0., -8., 0.), (0., 1., 0.), 3, '', 0, 0), r
assert all(v[-1] == 3 for v in Host.visits()), 'Nearest-to-point must not prune farther ray hits'
prepare([(5 << 8, 4.), (6 << 8, 16.)])
assert query(1, START, END, Vector3(0., 0., 0.))[3] == 5, 'Equal distance preserves first'

for kind in (70, 71, 86, 87, 100, 101):
    prepare([(kind << 8, 5., (0xffffffff, 0x10003))])
    r = query(1, START, END, Vector3(0., 5., 0.))
    assert r[0] == (kind < 71 or kind > 100), (kind, r)
    assert r[3] == kind, (kind, r)

for value, accepts in ((False, True), (True, False), (0, True), (2, False), ('fAlSe', True), ('TrUe', False)):
    prepare([(71 << 8, 4., (0xffffffff, 0x10003)), (72 << 8, 8., (12, 7))])
    calls = []
    def broken(*args):
        calls.append(args)
        return value
    r = query(1, START, END, Vector3(0., 2., 0.), broken)
    assert r[0] == accepts, (value, r)
    assert calls[0] == (0xffffffff, 3, 71, ''), calls
    assert calls[1] == (12, 7, 72, 'fixture.model' if accepts else ''), calls
    if accepts:
        assert r == (True, (0., 2., 0.), (0., 1., 0.), 72, 'fixture.model', 7, 12), r
prepare([(71 << 8, 4.)])
expect(TypeError, lambda: query(1, START, END, (0., 0., 0.), lambda *a: object()))
# Damaged material updates the kind while preserving the last accepted hit.
prepare([(3 << 8, 4.), (87 << 8, 8.)])
r = query(1, START, END, (0., 0., 0.), lambda *a: False)
assert r[0] and r[1] == (0., 6., 0.) and r[3] == 87, r
# Real patched ClientSpace ownership keeps BSP triangles alive during callback teardown.
prepare([(71 << 8, 4.), (2 << 8, 8.)])
def retire(*args):
    Host.clearSpace(1)
    return False
assert not query(1, START, END, (0., 0., 0.), retire)[0]
assert not Host.queryActive()
# Even a supported int result can re-enter Python while its last reference drops.
prepare([(3 << 8, 2.), (71 << 8, 5.), (72 << 8, 8.)])
destructors = []
class RetireOnRelease(int):
    def __del__(self):
        destructors.append('retired')
        Host.clearSpace(1)
def reject_and_retire(*args):
    return RetireOnRelease(1)
assert not query(1, START, END, (0., 0., 0.), reject_and_retire)[0]
assert destructors == ['retired']
assert not Host.queryActive()

expect(ValueError, lambda: query(999, START, END, (0., 0., 0.)))
expect(TypeError, lambda: query(1, START, END))
expect(TypeError, lambda: query(1, START, END, (0., 0., 0.), None, 8))

package = os.environ.get('STOCK_SCRIPTS_PKG')
if package:
    with zipfile.ZipFile(package) as archive:
        root = marshal.loads(archive.read('scripts/client/helpers/EffectMaterialCalculation.pyc')[8:])
    code = next(c for c in root.co_consts if isinstance(c, types.CodeType) and c.co_name == 'calcSurfaceMaterialNearPoint')
    material = types.ModuleType('material_kinds')
    material.EFFECT_MATERIAL_INDEXES_BY_NAMES = {'ground': 9}
    bounds = types.ModuleType('DESTRUCTIBLE_MATKIND')
    bounds.MIN, bounds.MAX, bounds.DAMAGED_MIN, bounds.DAMAGED_MAX = 71, 100, 87, 100
    surface = collections.namedtuple('SurfaceMaterial', 'hitPoint surfNormal matKind effectIdx')
    namespace = {'__builtins__': __builtins__, 'BigWorld': _BigWorld, 'isDestructibleBroken': lambda *a: False,
                 'DESTRUCTIBLE_MATKIND': bounds, 'material_kinds': material,
                 'calcEffectMaterialIndex': lambda kind: kind + 1000, 'SurfaceMaterial': surface}
    original = types.FunctionType(code, namespace)
    prepare([(3 << 8, 2.), (4 << 8, 4.)])
    point, normal = Vector3(0., 0., 0.), Vector3(0., 1., 0.)
    r = original(point, normal, 1, 'ground')
    assert r == surface((0., -1., 0.), (0., 1., 0.), 3, 1003), r
    prepare([(87 << 8, 3.)])
    r = original(point, normal, 1, 'ground')
    assert r == surface(point, normal, 101, 9), r
    saved = _BigWorld.wg_getMatInfoNearPoint
    del _BigWorld.wg_getMatInfoNearPoint
    expect(AttributeError, lambda: original(point, normal, 1, 'ground'))
    _BigWorld.wg_getMatInfoNearPoint = saved
    print('Exact #1513 EffectMaterialCalculation consumer and missing-export control passed')
print('Material-near-point traversal, callback and retirement passed')
