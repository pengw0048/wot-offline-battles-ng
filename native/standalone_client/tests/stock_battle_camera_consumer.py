"""Drive the production collider converters from the original ArcadeCamera PYC.

Runs in the CPython 2.7 host. argv: scripts.pkg path and a JSON file with the
exact arcade/postmortem advancedCollider volume-group sections. The original
CollisionVolumeGroup.fromSection and ArcadeCamera.__setModelsToCollideWith
produce the values; the production converters and wrappers consume them.
"""
import collections
import json
import marshal
import struct
import sys
import types
import zipfile

import _camera_binding_host as host


class MatrixProvider(object):
    pass


class GeometryLink(object):
    pass


class Vector2(object):
    def __init__(self, x=0.0, y=0.0):
        self.x, self.y = float(x), float(y)


host.setTypes(MatrixProvider, GeometryLink, Vector2)


def code_object(package, member, names):
    code = marshal.loads(zipfile.ZipFile(package).read(member)[8:])
    for name in names:
        code = [c for c in code.co_consts if hasattr(c, 'co_name') and c.co_name == name][0]
    return code


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class Section(object):
    """A DataSection over one packed-XML volume group."""

    def __init__(self, values):
        self.values = values

    def readFloat(self, name, default):
        return float(self.values[name]) if name in self.values else default

    def readVector2(self, name, default):
        if name not in self.values:
            return default
        x, y = self.values[name].split()
        return Vector2(float(x), float(y))

    def readBool(self, name, default):
        if name not in self.values:
            return default
        return self.values[name].strip().lower() == 'true'


def expect_error(text, function, *args):
    try:
        function(*args)
    except TypeError as error:
        assert str(error) == text, (str(error), text)
        return
    raise AssertionError('no TypeError: ' + text)


def main():
    package, sections_path = sys.argv[1], sys.argv[2]
    sections = json.load(open(sections_path))
    member = 'scripts/client/AvatarInputHandler/DynamicCameras/ArcadeCamera.pyc'
    math_module = types.ModuleType('Math')
    math_module.Vector2 = Vector2
    namespace = {'__name__': 'ArcadeCamera', '__builtins__': __builtins__, 'Math': math_module,
                 'namedtuple': collections.namedtuple}
    base = collections.namedtuple('CollisionVolumeGroup', (
        'minVolume', 'lowSpeedLimit', 'vehicleVisibilityLimit', 'approachSpeed',
        'cameraSpeedFactor', 'criticalDistance', 'canSkip'))
    body = code_object(package, member, ['CollisionVolumeGroup'])
    # A class body returns its namespace, as BUILD_CLASS consumes it.
    attributes = types.FunctionType(body, namespace)()
    group_type = type('CollisionVolumeGroup', (base,), attributes)
    namespace['CollisionVolumeGroup'] = group_type

    checked = 0
    for mode in sorted(sections):
        before = len(host.state()['groups'])
        for name in ('tiny', 'small', 'medium', 'large'):
            group = group_type.fromSection(Section(sections[mode][name]))
            assert isinstance(group, tuple) and len(group) == 7
            assert host.addVolumeGroup(group) is None
            installed = host.state()['groups'][-1]
            critical = f32(group.criticalDistance)
            expected = (f32(group.minVolume), f32(group.lowSpeedLimit),
                        f32(group.vehicleVisibilityLimit), f32(group.approachSpeed.x),
                        f32(group.approachSpeed.y), f32(group.cameraSpeedFactor),
                        f32(critical * critical), 0.0, int(group.canSkip))
            assert installed == expected, (mode, name, installed, expected)
            checked += 1
        assert len(host.state()['groups']) == before + 4

    # The wrapper and converter contract around a valid group.
    group = group_type.fromSection(Section(sections['arcadeMode']['small']))
    count = len(host.state()['groups'])
    expect_error('addVolumeGroup() expects 1 argument of type ColliderVolumeGroup', host.addVolumeGroup)
    expect_error('addVolumeGroup() expects 1 argument of type ColliderVolumeGroup', host.addVolumeGroup,
                 group, group)
    expect_error('ColliderVolumeGroup must be set to a tuple of 7 elements', host.addVolumeGroup, list(group))
    expect_error('ColliderVolumeGroup must be set to a tuple of 7 elements', host.addVolumeGroup, tuple(group)[:6])
    names = ('m_minVolume', 'm_lowSpeedLimit', 'm_vehicleVisibilityLimit', 'm_approachSpeed',
             'm_cameraSpeedFactor', 'm_criticalDistanceSq', 'm_canSkip')
    for index in range(7):
        broken = list(group)
        broken[index] = object()
        expect_error('error during converting %d elem into volumeGroup.%s field of CollisionPredictor'
                     % (index, names[index]), host.addVolumeGroup, tuple(broken))
    assert host.addVolumeGroup(tuple(group)[:3] + ((1.5, 10.0),) + tuple(group)[4:]) is None
    assert len(host.state()['groups']) == count + 1

    # Original ArcadeCamera.__setModelsToCollideWith produces the colliders.
    produce = types.FunctionType(code_object(package, member,
                                             ['ArcadeCamera', '__setModelsToCollideWith']),
                                 {'TankPartNames': types.ModuleType('TankPartNames'),
                                  'TankPartIndexes': types.ModuleType('TankPartIndexes'), 'None': None,
                                  '__builtins__': __builtins__})
    produce.func_globals['TankPartNames'].TURRET, produce.func_globals['TankPartNames'].HULL = 'turret', 'hull'
    produce.func_globals['TankPartIndexes'].TURRET, produce.func_globals['TankPartIndexes'].HULL = 3, 1

    class Compound(object):
        def __init__(self):
            self.nodes = {'turret': MatrixProvider(), 'hull': MatrixProvider()}
            self.bounds = {3: MatrixProvider(), 1: MatrixProvider()}
            self.links = {3: GeometryLink(), 1: GeometryLink()}

        def node(self, name):
            return self.nodes[name]

        def getBoundsForPart(self, index):
            return self.bounds[index]

        def getPartGeometryLink(self, index):
            return self.links[index]

    class Vehicle(object):
        def __init__(self, detached):
            self.isTurretDetached = detached
            self.appearance = types.ModuleType('appearance')
            self.appearance.compoundModel = Compound()

    class Forward(object):
        def setDynamicColliders(self, colliders):
            return host.setDynamicColliders(colliders)

    camera = types.ModuleType('camera')
    camera._ArcadeCamera__cam = Forward()
    camera._ArcadeCamera__aimingSystem = Forward()
    vehicles = [Vehicle(False), Vehicle(True)]
    replacements = host.state()['replacements']
    produce(camera, vehicles)
    state = host.state()
    assert state['replacements'] == replacements + 2
    compound = vehicles[0].appearance.compoundModel
    detached = vehicles[1].appearance.compoundModel
    assert state['colliders'] == [
        (compound.nodes['turret'], compound.bounds[3], compound.links[3]),
        (compound.nodes['hull'], compound.bounds[1], compound.links[1]),
        (detached.nodes['hull'], detached.bounds[1], detached.links[1])], state['colliders']
    produce(camera, [])
    assert host.state()['colliders'] == []
    produce(camera, vehicles[:1])

    # Failures leave the installed list untouched.
    installed = host.state()['colliders']
    provider, link = MatrixProvider(), GeometryLink()
    expect_error('setDynamicColliders() expects 1 argument of type CameraCollider::DynamicColliders',
                 host.setDynamicColliders)
    expect_error('() argument 1 must be set to a sequence of '
                 'struct BW::CameraCollider::DynamicCollisionInfo', host.setDynamicColliders, 5)
    expect_error('Tuple of 3 arguments required', host.setDynamicColliders, [(provider, provider)])
    expect_error('Tuple of 3 arguments required', host.setDynamicColliders, [5])
    expect_error(' must be set to a MatrixProvider or None', host.setDynamicColliders,
                 [(provider, object(), link)])
    expect_error(' must be set to a PyDataLink', host.setDynamicColliders, [(provider, provider, 5)])
    assert host.state()['colliders'] == installed
    assert host.setDynamicColliders(((None, provider, None),)) is None
    assert host.state()['colliders'] == [(None, provider, None)]
    print('Original ArcadeCamera producers drove %d exact volume groups and the collider list' % checked)


main()
