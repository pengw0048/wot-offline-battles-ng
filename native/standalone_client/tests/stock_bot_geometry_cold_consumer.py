"""CPython 2.7 consumer for the all-plan resource-backed cold geometry audit."""
from __future__ import print_function
import ast
import imp
import json
import marshal
import sys
import types
import zipfile

import BigWorld
import Host

sys.path.insert(0, sys.argv[4])
import bot_geometry_startup as native
from vehicle_physics_producer import Vector3

data = json.load(open(sys.argv[1]))
scripts = zipfile.ZipFile(sys.argv[2])


def loader(name):
    row = data['models'].get(name)
    return None if row is None else (row[0], row[1], row[2].decode('hex'), row[3])


Host.setLoader(loader)


def module(name, **values):
    result = types.ModuleType(name)
    result.__dict__.update(values)
    sys.modules[name] = result
    return result


module('Math', Vector3=Vector3, Vector2=lambda *x:x, Matrix=object)
module('constants', IS_DEVELOPMENT=False, IS_CLIENT=True, IS_BOT=False)
module('debug_utils', LOG_DEBUG=lambda *x:None)
hit = module('ModelHitTester')
bytecode = scripts.read('scripts/common/ModelHitTester.pyc')
assert bytecode[:4] == imp.get_magic()
exec(marshal.loads(bytecode[8:]), hit.__dict__)


def codes(code):
    yield code
    for value in code.co_consts:
        if isinstance(value, types.CodeType):
            for child in codes(value):
                yield child


vehicle_code = marshal.loads(scripts.read('scripts/common/items/vehicles.pyc')[8:])
getters = [code for code in codes(vehicle_code) if code.co_name == 'getHitTesters']
assert len(getters) == 1


class Object(object):
    def __init__(self, **values):
        self.__dict__.update(values)


class Descriptor(Object):
    # Use the exact four-component producer, including its turret/gun pairs.
    getHitTesters = types.FunctionType(getters[0], {}, 'getHitTesters')


class Section(object):
    def __init__(self, values):
        self.values = values

    def readString(self, key):
        return self.values.get(key, '')


source = open(sys.argv[5]).read()
tree = ast.parse(source)
names = ('_coord', '_value', '_bbox', 'chassis_shape')
functions = [node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in names]
assert len(functions) == len(names)
oracle = dict(_SHAPE_CACHE={}, DEFAULT_SHAPE=(1.5,3.5,-.8,2.))
exec(compile(ast.fix_missing_locations(ast.Module(body=functions)), 'retained-chassis-shape', 'exec'), oracle)


def make_descriptor(mode, testers):
    parts = {}
    for name, section in mode['parts'].items():
        key = tuple(sorted(section.items()))
        if key not in testers:
            tester = hit.ModelHitTester(Section(section))
            assert tester.bbox is None and not tester.isBspModelLoaded()
            testers[key] = tester
        parts[name] = Object(hitTester=testers[key])
    parts['chassis'].hullPosition = Vector3(mode['hull_position'])
    parts['hull'].turretPositions = (Vector3(mode['turret_position']),)
    parts['turret'].gunPosition = Vector3(mode['gun_position'])
    descriptor = Descriptor(**parts)
    descriptor.turrets = ((parts['turret'],parts['gun']),)
    return descriptor


def close(actual, expected):
    if isinstance(expected, (list,tuple)):
        assert len(actual) == len(expected), (actual, expected)
        for first, second in zip(actual,expected):
            close(first,second)
    else:
        assert abs(actual-expected) <= 1.e-6, (actual,expected)


counts = dict(descriptors=0, cold_testers=0, siege_descriptors=0)
for row in data['rows']:
    testers = {}
    descriptors = [make_descriptor(mode,testers) for mode in row['modes']]
    assert all(tester.bbox is None for tester in testers.values())
    # The failure must be real before the cold owner prepares any component.
    try:
        native.unprepared(descriptors[0])
    except TypeError as error:
        assert 'NoneType' in str(error)
    else:
        raise AssertionError(('Cold geometry unexpectedly succeeded',row['vehicle'],row['tier']))
    try:
        for mode, descriptor in zip(row['modes'],descriptors):
            result = native.prepare(descriptor)
            assert len(descriptor.getHitTesters()) == 4
            for tester in descriptor.getHitTesters():
                assert tester.isBspModelLoaded()
                close(tester.bbox[:2], data['models'][tester.bspModelName][:2])
                assert Host.nativeOwner(tester.getBspModel())[0] != 0
            for key, tester in testers.items():
                section = dict(key)
                for suffix in ('Down','Up'):
                    model = section.get('collisionModelClient' + suffix)
                    if model and tester.isBspModelLoaded():
                        close(getattr(tester,'bbox' + suffix)[:2], data['models'][model][:2])
            close(result['shape'], oracle['chassis_shape'](descriptor))
            low, high = descriptor.chassis.hitTester.bbox[:2]
            hull_low, hull_high = descriptor.hull.hitTester.bbox[:2]
            mount = mode['hull_position']
            close(result['body'], ([min(low[i],hull_low[i]+mount[i]) for i in range(3)],
                                   [max(high[i],hull_high[i]+mount[i]) for i in range(3)]))
            assert result['checkpoints'] is not None and len(result['checkpoints']) == 6
            counts['descriptors'] += 1
            counts['siege_descriptors'] += int(mode['mode'] == 'siege')
        # Repeated/alias descriptors share the same tester identities and owner.
        before = Host.liveModels()
        for descriptor in descriptors:
            native.prepare(descriptor)
        assert before == Host.liveModels()
        counts['cold_testers'] += len(testers)
    except Exception:
        print('Failed cold plan:',row['vehicle'],row['tier'],file=sys.stderr)
        raise
    finally:
        native.release()
        native.release()
        oracle['_SHAPE_CACHE'].clear()
    assert not Host.liveModels(), (row['vehicle'],row['tier'],Host.liveModels())
    assert all(not tester.isBspModelLoaded() and not hasattr(tester,'bbox') for tester in testers.values())
    assert all(not hasattr(tester,'bbox' + suffix) for tester in testers.values() for suffix in ('Down','Up'))

# An absent native model must fail through the true PYC load path.
missing = hit.ModelHitTester(Section({'collisionModelClient':'missing/bot-collision.model'}))
try:
    missing.loadBspModel()
except Exception as error:
    assert 'wrong collision model' in str(error)
else:
    raise AssertionError('Missing collision resource was accepted')
assert missing.bbox is None and not missing.isBspModelLoaded()
json.dump(counts,open(sys.argv[3],'w'))
