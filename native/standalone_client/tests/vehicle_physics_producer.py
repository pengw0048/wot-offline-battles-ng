"""Run the pinned #1513 physics producers on real vehicle XML under CPython 2.7.

items/vehicles.pyc reads physics/detailed through items/_xml.pyc, then
physics_shared.pyc configurePhysics builds the dictionary and calls the
supplied physics object. Only engine imports with no role in that dictionary
are stubbed; descriptor bounding boxes and weights are authored test inputs.
"""
from __future__ import print_function
import json
import marshal
import sys
import types
import zipfile


class Section(object):
    """ResMgr DataSection subset used by items/_xml.pyc on packed XML."""
    def __init__(self, node):
        self.node = node

    @property
    def name(self):
        return str(self.node['n'])

    def _child(self, key):
        node = self.node
        for part in key.split('/'):
            node = next((child for child in node['c'] if child['n'] == part), None)
            if node is None:
                return None
        return Section(node)

    def has_key(self, key):
        return self._child(key) is not None

    def __getitem__(self, key):
        return self._child(key)

    def keys(self):
        return [str(child['n']) for child in self.node['c']]

    def items(self):
        return [(str(child['n']), Section(child)) for child in self.node['c']]

    def values(self):
        return [Section(child) for child in self.node['c']]

    @property
    def asString(self):
        value = self.node['v']
        if isinstance(value, list):
            return ' '.join(repr(item) for item in value)
        return str(value).strip()

    @property
    def asFloat(self):
        value = self.node['v']
        return float(value[0] if isinstance(value, list) else value)

    @property
    def asInt(self):
        return int(self.asFloat)

    @property
    def asBool(self):
        value = self.node['v']
        if isinstance(value, (bool, int)):
            return bool(value)
        return self.asString.lower() == 'true'

    def readFloat(self, key, default=0.0):
        child = self._child(key)
        return default if child is None else child.asFloat

    def readInt(self, key, default=0):
        child = self._child(key)
        return default if child is None else child.asInt

    def readString(self, key, default=''):
        child = self._child(key)
        return default if child is None else child.asString

    def readBool(self, key, default=False):
        child = self._child(key)
        return default if child is None else child.asBool


class Vector3(object):
    def __init__(self, *values):
        if len(values) == 1:
            values = tuple(values[0])
        self.x, self.y, self.z = (tuple(values) + (0.0, 0.0, 0.0))[:3]

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    def __getitem__(self, index):
        return (self.x, self.y, self.z)[index]

    def __len__(self):
        return 3

    def __add__(self, other):
        return Vector3(self.x + other[0], self.y + other[1], self.z + other[2])

    def __sub__(self, other):
        return Vector3(self.x - other[0], self.y - other[1], self.z - other[2])

    def __mul__(self, scale):
        return Vector3(self.x * scale, self.y * scale, self.z * scale)

    __rmul__ = __mul__


class Descriptor(object):
    def __init__(self, **values):
        self.__dict__.update(values)


def _stub(name, **values):
    module = types.ModuleType(name)
    module.__dict__.update(values)
    sys.modules[name] = module
    return module


def _code(archive, path):
    return marshal.loads(archive.read(path)[8:])


def load(client):
    archive = zipfile.ZipFile(client + '/res/packages/scripts.pkg')
    log = lambda *args, **kwargs: None
    _stub('BigWorld')
    _stub('ResMgr')
    _stub('Math', Vector3=Vector3, Vector2=lambda *values: tuple(values))
    _stub('debug_utils', LOG_ERROR=log, LOG_WARNING=log, LOG_DEBUG=log, LOG_CURRENT_EXCEPTION=log,
          LOG_DEBUG_DEV=log, LOG_NOTE=log)
    _stub('gun_rotation_shared', encodeRestrictedValueToUint=log, decodeRestrictedValueFromUint=log)
    items = _stub('items')
    items.__path__ = []
    items.vehicles = _stub('items.vehicles')
    items.vehicle_items = _stub('items.vehicle_items')
    components = _stub('items.components', gun_components=_stub('items.components.gun_components'))
    components.__path__ = []
    items.components = components
    constants = types.ModuleType('constants')
    sys.modules['constants'] = constants
    exec(_code(archive, 'scripts/common/constants.pyc'), constants.__dict__)
    # material_kinds._init needs ResMgr XML; the indexes are its constant prefix.
    material = types.ModuleType('material_kinds')
    sys.modules['material_kinds'] = material
    try:
        exec(_code(archive, 'scripts/common/material_kinds.pyc'), material.__dict__)
    except AttributeError:
        pass
    assert material.EFFECT_MATERIAL_INDEXES_BY_NAMES['ground'] == 0
    shared = types.ModuleType('physics_shared')
    exec(_code(archive, 'scripts/common/physics_shared.pyc'), shared.__dict__)

    xml = types.ModuleType('items._xml')
    exec(_code(archive, 'scripts/common/items/_xml.pyc'), xml.__dict__)
    constants_module = types.ModuleType('component_constants')
    exec(_code(archive, 'scripts/common/items/components/component_constants.pyc'),
         constants_module.__dict__)
    vehicles = _code(archive, 'scripts/common/items/vehicles.pyc')
    names = ('_readXPhysics', '_readXPhysicsMode', '_xphysicsParseEngine', '_xphysicsParseChassis',
             '_xphysicsParseGround', '_parseSectionList', '_readFakeGearBox', '_parseFloatList',
             '_parseFloatArrList', '_tryParseFloatList', '_readXPhysicsClient',
             '_xphysicsParseEngineClient', '_xphysicsParseChassisClient',
             '_xphysicsParseGroundClient')
    import math
    scope = dict((name, getattr(math, name)) for name in dir(math) if not name.startswith('_'))
    scope.update({'_xml': xml, 'component_constants': constants_module, '__builtins__': __builtins__})
    for item in vehicles.co_consts:
        if isinstance(item, types.CodeType) and item.co_name in names:
            scope[item.co_name] = types.FunctionType(item, scope, item.co_name)
    missing = [name for name in names if name not in scope]
    assert not missing, missing
    return shared, scope


def read_xphysics(scope, node):
    # The #1513 client strips the server physics fields; VehicleType reads
    # them with _readXPhysicsClient, which yields only engines and chassis.
    vehicle = Section({'n': 'vehicle', 't': 1, 'v': '', 'c': [node]})
    return scope['_readXPhysicsClient']((None, 'vehicle'), vehicle, 'physics')


def descriptor(name, xphysics, siege=None, hull_box=((-1.5, 0.0, -3.0), (1.5, 1.2, 3.0)),
               chassis_box=((-1.6, -0.4, -3.1), (1.6, 0.6, 3.1)),
               turret_box=((-1.0, 0.0, -1.0), (1.0, 0.8, 1.0)), weight=20000.0):
    """Authored geometry/weight inputs with the attribute shape the producer reads."""
    detailed = xphysics['detailed']
    hit = lambda box: Descriptor(bbox=(Vector3(box[0]), Vector3(box[1]), 0.0))
    aiming = {'pitch': {'isAvailable': siege is not None, 'wheelCorrectionCenterZ': -0.5,
                        'wheelsCorrectionSpeed': 0.25,
                        'wheelsCorrectionAngles': {'pitchMin': -5.0, 'pitchMax': 7.0}},
              'yaw': {'isAvailable': siege is not None}}
    vehicle_type = Descriptor(name=name, xphysics=xphysics, useHullZ=False, hullAimingParams=aiming)
    result = Descriptor(
        type=vehicle_type, hasSiegeMode=siege is not None,
        engine=Descriptor(name=sorted(detailed['engines'])[0]),
        chassis=Descriptor(name=sorted(detailed['chassis'])[0], hitTester=hit(chassis_box),
                           hullPosition=Vector3(0.0, 0.3, 0.0)),
        hull=Descriptor(hitTester=hit(hull_box), turretPositions=(Vector3(0.0, 1.2, 0.2),)),
        turret=Descriptor(hitTester=hit(turret_box)),
        physics={'weight': weight})
    if siege is not None:
        result.defaultVehicleDescr = result
        result.siegeVehicleDescr = siege
    return result


def nodes(path):
    return json.load(open(path))


GROUND_FIELDS = ('dirtCumulationRate', 'dirtReleaseRate', 'dirtSideVelocity', 'maxDirt',
                 'hbComSideFriction', 'sideFriction', 'fwdFriction', 'rollingFriction',
                 'hbSideFrictionAddition', 'rotationFactor')


def server_base(client_xphysics, gravity_factor=1.0):
    """Server-shaped physics/detailed: client XML values plus authored ground rows.

    The #1513 client package strips the server ground tables, so the firm,
    medium and soft rows are explicit test inputs; client rollingFriction wins.
    """
    def row(seed, client=None):
        values = dict((name, 0.05 * (index + 1) + seed) for index, name in enumerate(GROUND_FIELDS))
        values.update(client or {})
        return values
    chassis = {}
    for name, client in client_xphysics['chassis'].items():
        grounds = dict((material, {'firm': row(0.01), 'medium': row(0.02, values)})
                       for material, values in client['grounds'].items())
        grounds['soft'] = row(0.03)
        chassis[name] = {'grounds': grounds}
    return {'gravityFactor': gravity_factor, 'engines': dict(client_xphysics['engines']),
            'chassis': chassis}
