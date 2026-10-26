"""Real CPython 2.7 consumer of the production TerrainMatKindSensor binding.

Executed by stock_vehicle_terrain_sensor_host.cpp. Exact #1513 bytecode is used
when the STOCK_*_PYC paths are provided; the engine owners are fixtures.
"""
import collections
import gc
import marshal
import os
import sys
import types

import Host
import Math
import Svarog
import Vehicular

LodSettings = collections.namedtuple('LodSettings', ('maxLodDistance', 'maxPriority'))
IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)
FRAME = [0]
NOT_WRITTEN = 0xffffffff


def expect(kind, function, message=None):
    try:
        function()
    except kind as error:
        if message is not None:
            assert str(error) == message, (str(error), message)
        return error
    raise AssertionError('%s was not raised' % kind.__name__)


def translated(x, y, z, rotation=None):
    values = list(rotation or IDENTITY)
    values[12:15] = [x, y, z]
    return values


def rounded(value):
    if isinstance(value, (tuple, list)):
        return type(value)(rounded(v) for v in value)
    return round(value, 4) if isinstance(value, float) else value


def tick(dt=0.25):
    FRAME[0] += 1
    Host.tick(dt, FRAME[0])


def find(code, name):
    if code.co_name == name:
        return code
    for child in code.co_consts:
        if isinstance(child, types.CodeType):
            found = find(child, name)
            if found is not None:
                return found


def module_code(variable):
    path = os.environ.get(variable)
    return marshal.loads(open(path, 'rb').read()[8:]) if path else None


def segment_points():
    return [(start, end) for space, start, end, results in rounded(Host.segments())]


class Value(object):
    pass


# Factory contract of 0077e9c0 and its readers 0069ef50, 00642450, 0068fba0.
root = Host.node()
points = ((1., 2., 3.), (-1., 2., 3.), (0., 2., 0.))
arity = 'PyTerrainMatKindSensor() expects 3 arguments of types MatrixProvider, BW::vector<Vector3>, float'
for args in ((), (root, points), (root, points, 4., 1)):
    expect(TypeError, lambda: Vehicular.TerrainMatKindSensor(*args), arity)
expect(TypeError, lambda: Vehicular.TerrainMatKindSensor(1, points, 4.),
       '() argument 1 must be set to a MatrixProvider or None')
expect(TypeError, lambda: Vehicular.TerrainMatKindSensor(IDENTITY, 'bad', 'bad'),
       '() argument 1 must be set to a MatrixProvider or None')
for bad in (4., None, {0: (1., 2., 3.)}):
    expect(TypeError, lambda: Vehicular.TerrainMatKindSensor(root, bad, 4.),
           '() argument 2 must be set to a sequence of class BW::Vector3')
element = '() argument 2 element must be set to a Vector3 or a tuple of 3 floats'
for bad in ([(1., 2.)], [[1., 2., 3.]], [(1., 2., 'z')], 'abc', [(1., 2., 3.), None]):
    expect(TypeError, lambda: Vehicular.TerrainMatKindSensor(root, bad, 4.), element)
expect(TypeError, lambda: Vehicular.TerrainMatKindSensor(root, points, 'far'),
       '() argument 3 must be set to a float')
expect(TypeError, lambda: Vehicular.TerrainMatKindSensor(root, [(1., 2.)], 'far'), element)


class Failing(object):
    def __len__(self):
        return 2

    def __getitem__(self, index):
        raise RuntimeError('point %d unavailable' % index)


class Unsized:
    def __getitem__(self, index):
        return (0., 0., 0.)


# 00642450 would pass a NULL item / resize to (size_t)-1; the error is kept.
expect(RuntimeError, lambda: Vehicular.TerrainMatKindSensor(root, Failing(), 4.), 'point 0 unavailable')
expect(AttributeError, lambda: Vehicular.TerrainMatKindSensor(root, Unsized(), 4.))

for accepted in (list(points), tuple(Math.Vector3(*p) for p in points), (), []):
    sensor = Vehicular.TerrainMatKindSensor(root, accepted, 4)
    assert len(sensor.matKinds) == len(accepted)
sensor = Vehicular.TerrainMatKindSensor(None, points, 4L)
assert type(sensor).__name__ == 'PyTerrainMatKindSensor' and sensor.isOwning is True
# Registration order only: the host's __members__ is a fixture, not BigWorld's.
assert sensor.__members__[0] == 'matKinds' and 'isOwning' in sensor.__members__
assert sensor.__methods__[:2] == ['setLodLink', 'setLodSettings']
assert set(('activate', 'deactivate', 'destroy')) <= set(sensor.__methods__)
expect(TypeError, lambda: setattr(sensor, 'matKinds', []),
       'Sorry, the attribute matKinds in PyTerrainMatKindSensor is read-only')

# matKinds: a live, read-only PySTLSequence over BW::vector<uint32>.
kinds = sensor.matKinds
assert type(kinds).__name__ == 'PySTLSequence' and kinds is not sensor.matKinds
assert len(kinds) == 3 and kinds.length == 3 and list(kinds) == [0, 0, 0]
assert repr(kinds) == '[0, 0, 0]' and kinds[-1] == 0 and kinds[1:] == [0, 0] and kinds[2:1] == []
assert 0 in kinds and 1 not in kinds and 'x' not in kinds and -1 not in kinds and 0.5 in kinds
assert kinds + [7] == [0, 0, 0, 7] and kinds + (8,) == [0, 0, 0, 8] and kinds * 2 == [0] * 6 and kinds * 0 == []
expect(TypeError, lambda: kinds + 1, 'PySTLSequence: Argument to + must be a sequence')
expect(IndexError, lambda: kinds[3], 'PySTLSequence index out of range')
item_error = 'Cannot assign an item in a read-only PySTLSequence'
expect(TypeError, lambda: kinds.__setitem__(0, 1), item_error)
expect(TypeError, lambda: kinds.__delitem__(0), item_error)
expect(TypeError, lambda: kinds.__setslice__(0, 1, []), 'Cannot assign a slice in a read-only PySTLSequence')
expect(TypeError, lambda: kinds.__iadd__([1]), 'Cannot do += on a read-only PySTLSequence')
expect(TypeError, lambda: kinds.__imul__(2), 'Cannot do *= on a read-only PySTLSequence')
expect(TypeError, lambda: setattr(kinds, 'length', 1),
       'Sorry, the attribute length in PySTLSequence is read-only')

link_type = 'setLodLink() expects 1 argument of type DataLinks::DataLink<LodProcessing::LodState>::Type'
expect(TypeError, lambda: sensor.setLodLink(), link_type)
expect(TypeError, lambda: sensor.setLodLink(None, None), link_type)
expect(TypeError, lambda: sensor.setLodLink(root), '() argument 1 must be set to a PyDataLink')
settings_type = 'setLodSettings() expects 1 argument of type LodProcessing::LodSettings'
expect(TypeError, lambda: sensor.setLodSettings(), settings_type)
expect(TypeError, lambda: sensor.setLodSettings((100., 15, 1)), 'Invalid args count')
expect(TypeError, lambda: sensor.setLodSettings(()))
expect(TypeError, lambda: sensor.setLodSettings([100., 15]))
expect(TypeError, lambda: sensor.setLodSettings(('far',)), 'maxLodDistance must be set to a float')
expect(TypeError, lambda: sensor.setLodSettings((100., -1)), 'maxPriority must be set to an uint')
for value in ((100.,), (100., None), LodSettings(100., 15), (100, 15L)):
    assert sensor.setLodSettings(value) is None
assert sensor.setLodLink(None) is None

# None is a legal provider; 00757cbd would call through it. It is contained.
Host.setSpace(5, True, [])
assert Host.update(sensor) is False
assert Host.errors() == ['TerrainMatKindSensor: has no MatrixProvider\n'] and Host.scheduled() == 0
assert Host.segments() == [] and list(kinds) == [0, 0, 0]
Host.setSpace(None, False, [])
assert Host.update(sensor) is True and Host.errors() == []
sensor.deactivate()

# Owning wrapper destruction releases the component; old views read empty.
references = sys.getrefcount(root)
held = Vehicular.TerrainMatKindSensor(root, points, 4.)
assert sys.getrefcount(root) == references + 1
view = held.matKinds
held.activate()
assert Host.scheduled() == 1
del held
gc.collect()
assert Host.scheduled() == 0 and sys.getrefcount(root) == references
assert len(view) == 0 and list(view) == [] and repr(view) == '[]' and 0 not in view
expect(IndexError, lambda: view[0])
held = Vehicular.TerrainMatKindSensor(root, points, 4.)
held.destroy()
held.destroy()
assert held.isOwning is False and len(held.matKinds) == 0
expect(ValueError, lambda: held.activate(), 'Component has been destroyed')
expect(TypeError, lambda: held.setLodLink(1), '() argument 1 must be set to a PyDataLink')
expect(ValueError, lambda: held.setLodLink(None), 'TerrainMatKindSensor has been destroyed')
expect(ValueError, lambda: held.setLodSettings((1.,)), 'TerrainMatKindSensor has been destroyed')
del held, view

# Exact svarog_script ComponentDescriptor/ComponentSystem and model_assembler.
auto_properties = module_code('STOCK_AUTO_PROPERTIES_PYC')
component_system = module_code('STOCK_COMPONENT_SYSTEM_PYC')
if auto_properties is not None and component_system is not None:
    package = types.ModuleType('svarog_script')
    sys.modules['svarog_script'] = package
    for name, code in (('auto_properties', auto_properties), ('py_component_system', component_system)):
        module = types.ModuleType('svarog_script.' + name)
        sys.modules[module.__name__] = module
        setattr(package, name, module)
        exec code in module.__dict__
    from svarog_script.py_component_system import ComponentDescriptor, ComponentSystem
else:
    class ComponentDescriptor(object):
        def __init__(self, fieldName=None):
            self.fieldName = fieldName

        def __get__(self, instance, owner):
            return self if instance is None else instance.__dict__.get('_' + self.fieldName)

        def __set__(self, instance, value):
            previous = instance.__dict__.get('_' + self.fieldName)
            if previous is not None:
                instance._nativeSystem.removeComponent(previous)
            if value is not None:
                instance._nativeSystem.addComponent(value, self.fieldName)
            instance.__dict__['_' + self.fieldName] = value

    class ComponentSystem(object):
        def __init__(self):
            self._nativeSystem = Svarog.ComponentSystem()

        def activate(self):
            self._nativeSystem.activate()

        def destroy(self):
            self._nativeSystem.destroy()
    ComponentDescriptor.fieldName = None
print('exact svarog_script component system: %s' % (component_system is not None))


class Appearance(ComponentSystem):
    terrainMatKindSensor = ComponentDescriptor()


# AutoPropertyInitMetaclass names the native component '_<class>__<attribute>'.
if getattr(Appearance.__dict__['terrainMatKindSensor'], 'fieldName', None) is None:
    Appearance.__dict__['terrainMatKindSensor'].fieldName = '_Appearance__terrainMatKindSensor'
FIELD = Appearance.__dict__['terrainMatKindSensor'].fieldName
assert FIELD == '_Appearance__terrainMatKindSensor'


def multiply(a, b):
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


class Matrix(object):
    """Math.Matrix as model_assembler uses it: copy, postMultiply, translation."""
    def __init__(self, source):
        self.values = list(Host.nodeValue(source))

    def postMultiply(self, other):
        self.values = multiply(self.values, other.values)

    @property
    def translation(self):
        return Math.Vector3(*self.values[12:15])


class MathModule(object):
    Matrix = Matrix
    Vector3 = staticmethod(Math.Vector3)


class TankNodeNames(object):
    TRACK_LEFT_MID = 'HP_TrackUp_LMid'
    TRACK_RIGHT_MID = 'HP_TrackUp_RMid'


origin, left, right = Host.node(), Host.node(), Host.node()
Host.setNode(origin, translated(0., 0., 0.))
Host.setNode(left, translated(-1.5, .25, .5))
Host.setNode(right, translated(1.5, .25, .5))
Host.setNode(root, translated(100., 10., 50.))
appearance = Appearance()
appearance.compoundModel = Value()
appearance.compoundModel.matrix = origin
appearance.compoundModel.root = root
appearance.compoundModel.node = {TankNodeNames.TRACK_LEFT_MID: left, TankNodeNames.TRACK_RIGHT_MID: right}.get
lod = Host.lodLink()
Host.setLod(lod, 1, 10., True)
assembler = module_code('STOCK_MODEL_ASSEMBLER_PYC')
shared = Value()
shared.LodSettings = LodSettings
if assembler is not None:
    code = find(assembler, 'assembleTerrainMatKindSensor')
    assert code.co_firstlineno == 368
    types.FunctionType(code, {'Math': MathModule, 'TankNodeNames': TankNodeNames, 'Vehicular': Vehicular,
                              'shared_components': shared, '__builtins__': __builtins__})(appearance, lod)
else:
    offset = Math.Vector3(0., 2., 0.)
    sensor = Vehicular.TerrainMatKindSensor(root, (Matrix(left).translation + offset, Matrix(right).translation + offset,
                                                   Math.Vector3(0., 0., 0.) + offset), 4.)
    appearance.terrainMatKindSensor = sensor
    sensor.setLodLink(lod)
    sensor.setLodSettings(LodSettings(100., 15))
print('exact model_assembler.assembleTerrainMatKindSensor bytecode: %s' % (assembler is not None))
sensor = appearance.terrainMatKindSensor
assert type(sensor).__name__ == 'PyTerrainMatKindSensor' and sensor.isOwning is False
view = appearance._nativeSystem.findComponent(FIELD)
assert type(view).__name__ == 'PyTerrainMatKindSensor' and view.isOwning is False
kinds = sensor.matKinds
assert list(kinds) == [0, 0, 0] and list(view.matKinds) == [0, 0, 0]
assert Host.scheduled() == 0
tick()
assert Host.segments() == []

# The appearance activates its system; the first update is one interval later.
space = [[(0x0108, 3.), (0x2a00, 1.5)], [(0x1702, 1.), (0x0510, .5), (0x2300, 2.)], []]
Host.setSpace(7, True, space)
appearance.activate()
assert Host.scheduled() == 1
tick(.125)
assert Host.segments() == []
tick(.125)
assert rounded(Host.transforms()) == [(-1.5, 2.25, .5), (1.5, 2.25, .5), (0., 2., 0.)]
expected = [((98.5, 12.25, 50.5), (98.5, 8.25, 50.5)), ((101.5, 12.25, 50.5), (101.5, 8.25, 50.5)),
            ((100., 12., 50.), (100., 8., 50.))]
segments = rounded(Host.segments())
assert [(s[0], s[1], s[2]) for s in segments] == [(7,) + e for e in expected]
# Transparent (0x02) and no-collide (0x10) triangles are skipped; the closest wins.
assert [s[3] for s in segments] == [[1, 1], [3, 3, 1], []]
assert list(kinds) == [0x2a, 0x23, 0] and list(view.matKinds) == list(kinds)
assert Host.nodeReads(root) >= 1

# Exact CompoundAppearance.__updateCurrTerrainMatKinds reads len() and items.
appearance_code = module_code('STOCK_COMPOUND_APPEARANCE_PYC')
if appearance_code is not None:
    owner = Value()
    owner.terrainMatKindSensor = sensor
    owner._CompoundAppearance__currTerrainMatKind = [-1] * 3
    owner._CompoundAppearance__terrainEffectMaterialNames = [''] * 3
    traces = []
    owner.vehicleTraces = Value()
    owner.vehicleTraces.setCurrTerrainMatKinds = lambda a, b: traces.append((a, b))
    material_kinds = Value()
    material_kinds.EFFECT_MATERIALS = ('ground', 'stone', 'wood')
    update_kinds = types.FunctionType(find(appearance_code, '__updateCurrTerrainMatKinds'),
                                      {'_MATKIND_COUNT': 3, 'material_kinds': material_kinds,
                                       'calcEffectMaterialIndex': {0x2a: 1, 0x23: 2}.get,
                                       '__builtins__': __builtins__})
    update_kinds(owner)
    assert owner._CompoundAppearance__currTerrainMatKind == [0x2a, 0x23, 0]
    assert owner._CompoundAppearance__terrainEffectMaterialNames == ['stone', 'wood', '']
    assert traces == [(0x2a, 0x23)]
    owner.terrainMatKindSensor = None
    update_kinds(owner)
    assert traces == [(0x2a, 0x23)]
print('exact CompoundAppearance.__updateCurrTerrainMatKinds: %s' % (appearance_code is not None))

# Kinds persist across intervals until the next scheduled sample.
Host.setSpace(7, True, [[(0x0100, 1.)], [(0x0200, 1.)], [(0x0300, 1.)]])
tick(.125)
assert Host.segments() == [] and list(kinds) == [0x2a, 0x23, 0]
tick(.125)
assert list(kinds) == [1, 2, 3] and len(Host.segments()) == 3
# Equal distances keep the first triangle; later farther triangles are ignored.
Host.setSpace(7, True, [[(0x0400, 2.), (0x0500, 2.), (0x0600, 3.)]])
tick()
assert list(kinds) == [4, 4, 4]
assert [s[3] for s in Host.segments()] == [[1, 1, 1]] * 3
# No camera space: nothing is queried and the published kinds stay.
Host.setSpace(None, False, [])
Host.transforms()
tick()
assert Host.segments() == [] and Host.transforms() == [] and list(kinds) == [4, 4, 4]
# A camera space unknown to SpaceManager: collideSegment misses, kinds become 0.
Host.setSpace(9, False, [[(0x0700, 1.)]])
tick()
assert Host.segments() == [] and list(kinds) == [0, 0, 0] and len(Host.transforms()) == 3

# LOD gate 00781f70: strict limits, and an unordered distance fails.
Host.setSpace(7, True, [[(0x0800, 1.)]])
for priority, distance, processed in ((16, 10., False), (1, 100.5, False), (15, 100., True),
                                      (0, float('nan'), False), (0, float('-inf'), True)):
    Host.setLod(lod, priority, distance, True)
    tick()
    assert bool(Host.segments()) == processed, (priority, distance)
sensor.setLodSettings((float('nan'),))
Host.setLod(lod, 0, 0., True)
tick()
assert Host.segments() == []
sensor.setLodSettings((100., None))
Host.setLod(lod, 0xffffffff, 1., True)
tick()
assert len(Host.segments()) == 3
sensor.setLodLink(None)
Host.setLod(lod, 99, 1e9, True)
tick()
assert len(Host.segments()) == 3
sensor.setLodLink(lod)
sensor.setLodSettings(LodSettings(100., 15))
Host.setLod(lod, 1, 10., False)
tick()
assert Host.errors() == ['TerrainMatKindSensor: lost its native LOD state\n'] and Host.scheduled() == 0
Host.setLod(lod, 1, 10., True)
appearance.activate()
assert Host.scheduled() == 1

# Five sensors become due together: three this frame, the remainder next frame.
others = []
for index in range(5):
    node = Host.node()
    Host.setNode(node, translated(200. + index, 0., 0.))
    other = Vehicular.TerrainMatKindSensor(node, [(0., 0., 0.)], 1.)
    other.activate()
    others.append((other, node))
sensor.deactivate()
Host.setSpace(7, True, [])
Host.segments()
tick(.25)
first = [start[0] for space, start, end, results in Host.segments()]
tick(0.)
second = [start[0] for space, start, end, results in Host.segments()]
assert len(first) == 3 and len(second) == 2 and sorted(first + second) == [200., 201., 202., 203., 204.]
for other, node in others:
    other.destroy()
assert Host.scheduled() == 0
del others, other

# Empty points still read the provider, then query nothing.
empty = Vehicular.TerrainMatKindSensor(root, (), 4.)
reads = Host.nodeReads(root)
assert Host.update(empty) is True and Host.nodeReads(root) == reads + 1 and Host.segments() == []
empty.destroy()

# Rotated/translated providers, negative and NaN scan lengths pass through.
tilted = Vehicular.TerrainMatKindSensor(root, [(0., 1., 0.), (2., 0., 0.)], -2.)
yaw = (0., 0., -1., 0., 0., 1., 0., 0., 1., 0., 0., 0., 0., 0., 0., 1.)
Host.setNode(root, translated(10., 1., 5., yaw))
Host.setSpace(7, True, [[(0x0901, 1.)]])
Host.update(tilted)
assert segment_points() == [((10., 2., 5.), (10., 4., 5.)), ((10., 1., 3.), (10., 3., 3.))]
assert Host.kinds(tilted) == [9, 9]
nan = Vehicular.TerrainMatKindSensor(root, [(0., 0., 0.)], float('nan'))
Host.update(nan)
start, end = segment_points()[0]
assert start == (10., 1., 5.) and end[0] == 10. and end[1] != end[1] and end[2] == 5.
tilted.destroy()
nan.destroy()
Host.setNode(root, translated(100., 10., 50.))
Host.transforms()

# A reentrant provider destroys the whole appearance during its matrix read.
reentrant = Host.node()
victim = Appearance()
Host.setNode(reentrant, IDENTITY, lambda: victim.destroy())
victim.terrainMatKindSensor = Vehicular.TerrainMatKindSensor(reentrant, [(0., 0., 0.)], 1.)
victim_kinds = victim.terrainMatKindSensor.matKinds
victim.activate()
Host.setSpace(7, True, [[(0x0b00, 1.)]])
sensor.activate()
tick(.25)
assert Host.scheduled() == 1 and len(victim_kinds) == 0 and list(kinds) == [11, 11, 11]
assert Host.nodeReads(reentrant) == 1 and Host.errors() == []
# The destroyed victim issued no query after its provider returned.
assert [start for space, start, end, results in rounded(Host.segments())] == [(98.5, 12.25, 50.5), (101.5, 12.25, 50.5), (100., 12., 50.)]
del victim, victim_kinds

# Removal through the descriptor destroys the removed owner and its schedule.
references = sys.getrefcount(root)
appearance.terrainMatKindSensor = None
gc.collect()
assert Host.scheduled() == 0 and len(kinds) == 0 and appearance.terrainMatKindSensor is None
assert sys.getrefcount(root) == references - 1, (sys.getrefcount(root), references)
expect(ValueError, lambda: sensor.activate(), 'Component has been destroyed')
tick()
assert Host.segments() == []
appearance.destroy()
appearance.destroy()

# Script shutdown clears the scheduler; a later tick never calls into Python.
late = Vehicular.TerrainMatKindSensor(root, [(0., 0., 0.)], 1.)
late.activate()
Host.fini()
assert Host.scheduled() == 0
tick()
assert Host.segments() == []
print('TerrainMatKindSensor consumer checks passed')
