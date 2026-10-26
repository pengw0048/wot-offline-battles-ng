"""Real CPython 2.7 consumer of the production WaterSensor binding.

Executed by stock_vehicle_water_sensor_host.cpp. The exact #1513 bytecode is
used when STOCK_*_PYC paths are provided; the engine owners are fixtures.
"""
import collections
import gc
import marshal
import os
import sys
import types

import DataLinks
import Host
import Svarog
import Vehicular

LodSettings = collections.namedtuple('LodSettings', ('maxLodDistance', 'maxPriority'))
IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)
FRAME = [0]


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
    return round(value, 5) if isinstance(value, float) else value


def tick(dt=0.25):
    FRAME[0] += 1
    Host.tick(dt, FRAME[0])


class V(tuple):
    def __add__(self, other):
        return V(a + b for a, b in zip(self, other))


class Value(object):
    pass


class Recorder(object):
    def __init__(self):
        self.events = []

    def onWaterSplash(self, point, heavy):
        self.events.append(('splash', tuple(round(v, 5) for v in point), heavy))

    def onUnderWaterSwitch(self, underWater):
        self.events.append(('switch', underWater))


def find(code, name):
    if code.co_name == name:
        return code
    for child in code.co_consts:
        if isinstance(child, types.CodeType):
            found = find(child, name)
            if found is not None:
                return found


def load(variable, name):
    path = os.environ.get(variable)
    if not path:
        return None
    return find(marshal.loads(open(path, 'rb').read()[8:]), name)


# Factory and introspection contracts of 007796b0 / 00779e00.
config = (V((0., 1.5, 0.)), (1.5, 3.), 3., 4., .5)
expect(TypeError, lambda: Vehicular.WaterSensor(),
       'PyWaterSensor() expects 1 argument of type WaterSensor::Config')
expect(TypeError, lambda: Vehicular.WaterSensor(config, 1),
       'PyWaterSensor() expects 1 argument of type WaterSensor::Config')
expect(TypeError, lambda: Vehicular.WaterSensor(list(config)),
       '() argument 1 must be set to a tuple with 5 arguments')
expect(TypeError, lambda: Vehicular.WaterSensor(config[:4]),
       '() argument 1 must be set to a tuple with 5 arguments')
for bad in ((V((0., 1.)),) + config[1:], config[:1] + ((1.,),) + config[2:],
            config[:2] + ('fast',) + config[3:], config[:4] + (None,)):
    expect(TypeError, lambda: Vehicular.WaterSensor(bad), 'failed to unpack () argument 1')


class Settings(tuple):
    pass


sensor = Vehicular.WaterSensor(Settings(config))
assert type(sensor).__name__ == 'PyWaterSensor' and sensor.isOwning is True
assert (sensor.isInWater, sensor.isUnderWater, sensor.isSplash, sensor.waterHeight) == (False, False, False, 0.)
assert type(sensor.onWaterSplash).__name__ == 'PyNativeEventWrapper'
assert type(sensor.onUnderWaterSwitch).__name__ == 'PyNativeEventWrapper'
assert sensor.speedLink is None
plane = sensor.sensorPlaneLink
assert type(plane).__name__ == 'DataLinks::PyDataLink<Matrix>'
expect(TypeError, plane)
# Registration order only: the host's __members__ is a fixture, not BigWorld's.
assert sensor.__members__[:8] == ['isUnderWater', 'waterHeight', 'speedLink', 'sensorPlaneLink',
                                  'isSplash', 'isInWater', 'onUnderWaterSwitch', 'onWaterSplash']
assert 'isOwning' in sensor.__members__
assert sensor.__methods__[:2] == ['setLodLink', 'setLodSettings']
assert set(('activate', 'deactivate', 'destroy')) <= set(sensor.__methods__)
for name in ('isUnderWater', 'waterHeight', 'isSplash', 'isInWater'):
    expect(TypeError, lambda: setattr(sensor, name, True),
           'Sorry, the attribute %s in PyWaterSensor is read-only' % name)
expect(TypeError, lambda: setattr(sensor, 'speedLink', 1.),
       'speedLink must be set to a PyDataLink or callable')
expect(TypeError, lambda: setattr(sensor, 'sensorPlaneLink', IDENTITY),
       'sensorPlaneLink must be set to a PyDataLink or MatrixProvider')
expect(TypeError, lambda: setattr(sensor, 'onWaterSplash', 1), 'onWaterSplash must be callable')
expect(TypeError, lambda: setattr(sensor, 'onUnderWaterSwitch', 'x'), 'onUnderWaterSwitch must be callable')
link_type = 'setLodLink() expects 1 argument of type DataLinks::DataLink<LodProcessing::LodState>::Type'
expect(TypeError, lambda: sensor.setLodLink(), link_type)
expect(TypeError, lambda: sensor.setLodLink(None, None), link_type)
expect(TypeError, lambda: sensor.setLodLink(Host.node()), '() argument 1 must be set to a PyDataLink')
settings_type = 'setLodSettings() expects 1 argument of type LodProcessing::LodSettings'
expect(TypeError, lambda: sensor.setLodSettings(), settings_type)
expect(TypeError, lambda: sensor.setLodSettings((150., 15, 1)), 'Invalid args count')
expect(TypeError, lambda: sensor.setLodSettings(()))
expect(TypeError, lambda: sensor.setLodSettings(150.))
expect(TypeError, lambda: sensor.setLodSettings(('far',)))
expect((TypeError, ValueError), lambda: sensor.setLodSettings((150., -1)))
for value in ((150.,), (150., None), LodSettings(150., 15), (150, 15L)):
    assert sensor.setLodSettings(value) is None
assert sensor.setLodLink(None) is None
del sensor

# Exact model_assembler.assembleWaterSensor and vehicle_assembler consumers.
recorder = Recorder()
root = Host.node()
Host.setNode(root, translated(10., 2., 5.))
appearance = Value()
appearance.compoundModel = Value()
appearance.compoundModel.root = root
appearance.filter = Value()
appearance.filter.averageSpeed = 5.
appearance.onWaterSplash = recorder.onWaterSplash
appearance.onUnderWaterSwitch = recorder.onUnderWaterSwitch
descriptor = Value()
descriptor.chassis = Value()
descriptor.chassis.hullPosition = V((0., .5, 0.))
descriptor.chassis.topRightCarryingPoint = (1.5, 3.)
descriptor.hull = Value()
descriptor.hull.turretPositions = (V((0., 1., 0.)),)
descriptor.type = Value()
descriptor.type.collisionEffectVelocities = {'waterContact': 3.}
descriptor.type.heavyCollisionEffectVelocities = {'waterContact': 4.}
lod = Host.lodLink()
Host.setLod(lod, 1, 10., True)
assemble = load('STOCK_MODEL_ASSEMBLER_PYC', 'assembleWaterSensor')
if assemble is not None:
    shared = Value()
    shared.LodSettings = LodSettings
    sensor = types.FunctionType(assemble, {'Vehicular': Vehicular, 'DataLinks': DataLinks,
                                          'shared_components': shared})(descriptor, appearance, lod)
else:
    sensor = Vehicular.WaterSensor((descriptor.chassis.hullPosition + descriptor.hull.turretPositions[0],
                                    descriptor.chassis.topRightCarryingPoint, 3., 4., .5))
    sensor.sensorPlaneLink = root
    sensor.speedLink = DataLinks.createFloatLink(appearance.filter, 'averageSpeed')
    sensor.onWaterSplash = appearance.onWaterSplash
    sensor.onUnderWaterSwitch = appearance.onUnderWaterSwitch
    sensor.setLodLink(lod)
    sensor.setLodSettings(LodSettings(150., 15))
print('exact model_assembler.assembleWaterSensor bytecode: %s' % (assemble is not None))
assert sensor.onWaterSplash == recorder.onWaterSplash
assert sensor.onUnderWaterSwitch == recorder.onUnderWaterSwitch
assert type(sensor.speedLink).__name__ == 'HostFloatLink' and sensor.speedLink() == 5.
assert sensor.sensorPlaneLink()[12:15] == (10., 2., 5.)
underwater_link = DataLinks.createBoolLink(sensor, 'isUnderWater')
in_water_link = DataLinks.createBoolLink(sensor, 'isInWater')
assert underwater_link() is False and in_water_link() is False
assert DataLinks.createBoolLink(sensor, 'waterHeight') is None
assert DataLinks.createBoolLink(root, 'isInWater') is None

# Svarog ownership: the appearance's ComponentSystem owns and activates it.
system = Svarog.ComponentSystem()
system.addComponent(sensor, 'waterSensor')
assert sensor.isOwning is False
view = system.findComponent('waterSensor')
assert type(view).__name__ == 'PyWaterSensor' and view.isOwning is False
Host.setWaters([-1.])
Host.setSpace(7, True, [])
tick()
assert Host.waterQueries() == [] and Host.scheduled() == 0
system.activate()
assert Host.scheduled() == 1
tick(.125)
assert Host.waterQueries() == []
tick(.125)
assert Host.waterQueries() == [((10., 2., 5.), (10., 3., 5.))]
assert (view.waterHeight, view.isInWater, view.isUnderWater, view.isSplash) == (-1., False, False, False)
assert recorder.events == [] and Host.transforms() == []
Host.setWaters([])
tick()
assert Host.waterQueries() == [] and view.waterHeight == -1. and not view.isInWater

# Shallow entry: in water, turret above it, heavy splash in deep-enough water.
Host.setWaters([-1., 0., .3, .9])
tick()
assert len(Host.waterQueries()) == 3
assert abs(view.waterHeight - .3) < 1e-6 and view.isInWater and view.isSplash and not view.isUnderWater
assert in_water_link() is True and underwater_link() is False
assert Host.transforms() == [(0., 1.5, 0.)]
assert rounded(Host.segments()) == [(7, (10., 2.3, 5.), (10., 1.8, 5.))]
assert recorder.events == [('splash', (10., 2.3, 5.), True)]
del recorder.events[:]
tick()
assert recorder.events == [] and view.isSplash and Host.segments() == []

# Deep: underwater rising edge; leaving water: falling edge and no splash.
Host.setWaters([2.])
tick()
assert view.isUnderWater and underwater_link() and recorder.events == [('switch', True)]
del recorder.events[:]
Host.setWaters([-1.])
tick()
assert not view.isInWater and not view.isSplash and recorder.events == [('switch', False)]
del recorder.events[:]

# Light splash variants: ground within heavyDepth, flagged triangles, spaces, speed.
for triangles, registered, camera, speed, heavy in (
        ([0x00], True, 7, 5., False), ([0x02, 0x80, 0x82], True, 7, 5., True),
        ([0x01], True, 7, 5., False), ([], False, 7, 5., True), ([], True, None, 5., False),
        ([], True, 7, 3.5, False), ([], True, 7, 4., True)):
    Host.setSpace(camera, registered, triangles)
    appearance.filter.averageSpeed = speed
    Host.setWaters([.3])
    tick()
    assert recorder.events == [('splash', (10., 2.3, 5.), heavy)], (triangles, registered, camera, speed)
    assert len(Host.segments()) == (1 if camera is not None and registered else 0)
    del recorder.events[:]
    Host.setWaters([-1.])
    tick()
appearance.filter.averageSpeed = 2.9
Host.setWaters([.3])
tick()
assert recorder.events == [] and view.isSplash and Host.segments() == []
Host.setWaters([-1.])
tick()
appearance.filter.averageSpeed = 5.
Host.setSpace(7, True, [])

# Tilted plane: roll by 90 degrees moves the turret point to the plane height.
roll = (0., 1., 0., 0., -1., 0., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)
Host.transforms()
Host.setNode(root, translated(10., 2., 5., roll))
Host.setWaters([.01])
tick()
assert view.isUnderWater and recorder.events == [('splash', (10., 2.01, 5.), True), ('switch', True)]
assert Host.transforms() == [(0., 1.5, 0.)]
del recorder.events[:]
pitch = (1., 0., 0., 0., 0., .70710677, .70710677, 0., 0., -.70710677, .70710677, 0., 0., 0., 0., 1.)
Host.setNode(root, translated(10., 2., 5., pitch))
Host.setWaters([1.06])
tick()
assert not view.isUnderWater and recorder.events == [('switch', False)]
del recorder.events[:]
Host.setWaters([1.07])
tick()
assert view.isUnderWater and recorder.events == [('switch', True)]
del recorder.events[:]
Host.setNode(root, translated(10., 2., 5.))
Host.setWaters([-1.])
tick()
del recorder.events[:]

# LOD: priority > 15 or distance > 150 skips the whole update; boundary passes.
Host.setWaters([.3])
Host.waterQueries()
for priority, distance, processed in ((16, 10., False), (1, 150.5, False), (15, 150., True)):
    Host.setLod(lod, priority, distance, True)
    tick()
    assert bool(Host.waterQueries()) == processed and view.isInWater == processed
Host.setLod(lod, 1, 10., True)
del recorder.events[:]
Host.segments()
Host.setWaters([-1.])
tick()
del recorder.events[:]

# Callback errors are printed and contained; replacement during a callback.
def failing(point, heavy):
    raise RuntimeError('splash handler failed')


view.onWaterSplash = failing
Host.setWaters([.3])
tick()
assert view.isInWater and Host.errors() == []
replacements = []


def replacing(point, heavy):
    view.onWaterSplash = None
    replacements.append(sys.getrefcount(replacing))


Host.setWaters([-1.])
tick()
view.onWaterSplash = replacing
Host.setWaters([.3])
tick()
assert len(replacements) == 1 and type(view.onWaterSplash).__name__ == 'PyNativeEventWrapper'
view.onWaterSplash = recorder.onWaterSplash
Host.setWaters([-1.])
tick()
del recorder.events[:]

# Plane provider replacement (damaged model) releases the previous provider.
damaged = Host.node()
Host.setNode(damaged, translated(-4., 1., 3.))
references = sys.getrefcount(root)
view.sensorPlaneLink = damaged
assert sys.getrefcount(root) == references - 1
copy = Vehicular.WaterSensor(config)
copy.sensorPlaneLink = view.sensorPlaneLink
assert copy.sensorPlaneLink()[12:15] == (-4., 1., 3.)
del copy
Host.waterQueries()
tick()
assert Host.waterQueries() == [((-4., 1., 3.), (-4., 2., 3.))]
view.sensorPlaneLink = root

# Five sensors become due together: three this frame, the remainder next frame.
others = []
for index in range(5):
    other = Vehicular.WaterSensor(config)
    node = Host.node()
    Host.setNode(node, translated(100. + index, 0., 0.))
    other.sensorPlaneLink = node
    other.speedLink = DataLinks.createFloatLink(appearance.filter, 'averageSpeed')
    other.activate()
    others.append((other, node))
view.deactivate()
Host.setWaters([-1.])
Host.waterQueries()
tick(.25)
first = [start[0] for start, end in Host.waterQueries()]
tick(0.)
second = [start[0] for start, end in Host.waterQueries()]
assert len(first) == 3 and len(second) == 2 and sorted(first + second) == [100., 101., 102., 103., 104.]
for other, node in others:
    other.destroy()
assert Host.scheduled() == 0
view.activate()
tick(.25)
assert len(Host.waterQueries()) == 1

# A Python speed producer (LinkFromPython<float>) is sampled only on the edge.
calls = []
view.speedLink = lambda: calls.append(1) or 6.
Host.setWaters([.3])
tick()
assert calls == [1] and recorder.events == [('splash', (10., 2.3, 5.), True)]
del recorder.events[:]
Host.setWaters([-1.])
tick()
view.speedLink = lambda: 1 / 0
Host.setWaters([.3])
tick()
assert view.isSplash and recorder.events == [] and Host.scheduled() == 1 and Host.errors() == []
Host.setWaters([-1.])
tick()
view.speedLink = DataLinks.createFloatLink(appearance.filter, 'averageSpeed')
Host.segments()

# Missing links are contained to this sensor after the original publish point.
view.speedLink = None
Host.setWaters([.3])
tick()
assert view.isInWater and Host.scheduled() == 0
assert Host.errors() == ['WaterSensor: has no speedLink\n']
view.speedLink = DataLinks.createFloatLink(appearance.filter, 'averageSpeed')
view.activate()
view.sensorPlaneLink = None
Host.setWaters([-1.])
tick()
assert view.isInWater and abs(view.waterHeight - .3) < 1e-6 and Host.scheduled() == 0
assert Host.errors() == ['WaterSensor: has no sensorPlaneLink\n']
view.sensorPlaneLink = root
view.activate()
Host.setLod(lod, 1, 10., False)
tick()
assert Host.scheduled() == 0 and Host.errors() == ['WaterSensor: lost its native LOD state\n']
Host.setLod(lod, 1, 10., True)
view.activate()
tick()
assert not view.isInWater and recorder.events == []
del recorder.events[:]

# A callback may destroy the owner: the remaining switch event is not delivered.
def destroying(point, heavy):
    system.destroy()


view.onWaterSplash = destroying
Host.setWaters([2.])
tick()
assert recorder.events == [] and Host.scheduled() == 0
expect(ValueError, lambda: view.isInWater, 'WaterSensor has been destroyed')
expect(ValueError, lambda: view.activate())
expect(TypeError, lambda: setattr(view, 'isInWater', 1),
       'Sorry, the attribute isInWater in PyWaterSensor is read-only')
assert underwater_link() is False and DataLinks.createBoolLink(view, 'isUnderWater') is None
gc.collect()
assert sys.getrefcount(destroying) == 2
assert sys.getrefcount(appearance.onUnderWaterSwitch) == 2
assert sys.getrefcount(appearance.filter) == 2
assert sys.getrefcount(root) == 3

# Exact CompoundAppearance consumers of the sensor and its event arguments.
appearance_code = os.environ.get('STOCK_COMPOUND_APPEARANCE_PYC')
if appearance_code:
    module = marshal.loads(open(appearance_code, 'rb').read()[8:])
    owner = Value()
    owner.waterSensor = Vehicular.WaterSensor(config)
    lambdas = [c for c in find(module, 'CompoundAppearance').co_consts
               if isinstance(c, types.CodeType) and c.co_name == '<lambda>' and 'waterSensor' in c.co_names]
    values = sorted((c.co_names[1], types.FunctionType(c, {})(owner)) for c in lambdas)
    assert values == [('isInWater', False), ('isUnderWater', False), ('waterHeight', 0.)]
    effects = []
    owner._CompoundAppearance__vehicle = Value()
    owner._CompoundAppearance__vehicle.showCollisionEffect = lambda *a: effects.append(a)

    class Math(object):
        Vector3 = staticmethod(lambda *a: a)
    splash = types.FunctionType(find(module, 'onWaterSplash'), {'Math': Math})
    splash(owner, (1., 2., 3.), True)
    splash(owner, (1., 2., 3.), False)
    assert effects == [((1., 2., 3.), 'waterCollisionHeavy', (0., 1., 0.)),
                       ((1., 2., 3.), 'waterCollisionLight', (0., 1., 0.))]
    print('exact CompoundAppearance water consumers: True')
print('WaterSensor consumer checks passed')
