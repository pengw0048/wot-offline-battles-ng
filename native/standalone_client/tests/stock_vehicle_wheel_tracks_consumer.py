"""Run the exact #1513 model_assembler bytecode against the native contracts.

CPython 2.7 only: argv is scripts.pkg and a JSON chassis inventory. The fake
native types enforce the reviewed parser contracts of WheelsAnimator,
TrackNodesAnimator, wg_createSplineTrack and WGVehicleFashion.setSplineTrack.
"""
from __future__ import print_function
import collections
import json
import marshal
import sys
import types
import zipfile

FIELDS = {
    'Wheel': ('isLeft', 'radius', 'nodeName', 'isLeading', 'leadingSyncAngle'),
    'WheelGroup': ('isLeft', 'template', 'count', 'startIndex', 'radius'),
    'WheelsConfig': ('lodDist', 'groups', 'wheels'),
    'TrackNode': ('name', 'isLeft', 'initialOffset', 'leftNodeName', 'rightNodeName', 'damping',
                  'elasticity', 'forwardElasticityCoeff', 'backwardElasticityCoeff'),
    'TrackParams': ('thickness', 'maxAmplitude', 'maxOffset', 'gravity'),
    'SplineConfig': ('segmentModelLeft', 'segmentModelRight', 'segmentLength', 'leftDesc', 'rightDesc',
                     'lodDist', 'segmentOffset', 'segment2ModelLeft', 'segment2ModelRight',
                     'segment2Offset', 'atlasUTiles', 'atlasVTiles'),
    'NodesAndGroups': ('nodes', 'groups'),
    'LodSettings': ('maxLodDistance', 'maxPriority'),
}
T = dict((name, collections.namedtuple(name, fields)) for name, fields in FIELDS.items())


class ContractError(Exception):
    pass


def require(condition, message):
    if not condition:
        raise ContractError(message)


def number(value):
    return isinstance(value, (int, long, float)) and not isinstance(value, bool)


def flag(value):
    # Script::setData(bool) accepts Python bool/int values.
    return isinstance(value, (bool, int, long))


def lod_settings(value):
    # 0077efe0: a tuple of at most two items; a None priority keeps the default.
    require(isinstance(value, tuple) and len(value) <= 2, 'LodSettings shape %r' % (value,))
    require(len(value) < 1 or number(value[0]), 'maxLodDistance %r' % (value,))
    require(len(value) < 2 or value[1] is None or isinstance(value[1], (int, long)), 'maxPriority %r' % (value,))


class Compound(object):
    def __init__(self):
        self.root = Node()


class Node(object):
    def attach(self, *args):
        raise ContractError('a spline track must not be attached without the spline manager')


class Model(object):
    pass


class Wheels(object):
    def __init__(self, compound):
        require(isinstance(compound, Compound), 'WheelsAnimator(compound)')
        self.calls = []
        self.lod = None

    def addWheelGroup(self, *args):
        require(len(args) == 3 and flag(args[0]) and number(args[1]), 'addWheelGroup %r' % (args,))
        require(isinstance(args[2], list) and all(isinstance(n, str) for n in args[2]), 'names %r' % (args,))
        self.calls.append(('group',) + args)

    def addWheel(self, *args):
        require(len(args) == 5 and flag(args[0]) and number(args[1]) and isinstance(args[2], str) and
                flag(args[3]) and number(args[4]), 'addWheel %r' % (args,))
        self.calls.append(('wheel',) + args)

    def setLodLink(self, *args):
        require(len(args) == 1 and args[0] is None, 'setLodLink %r' % (args,))

    def setLodSettings(self, *args):
        require(len(args) == 1, 'setLodSettings arity')
        lod_settings(args[0])
        self.lod = args[0]

    def setSplineTrackMovementData(self, *args):
        require(args == (None, None), 'setSplineTrackMovementData %r' % (args,))
        self.calls.append(('spline',))

    def setMovementInfo(self, *args):
        require(len(args) == 1, 'setMovementInfo arity')
        self.calls.append(('movement',))


class TrackNodes(object):
    def __init__(self, *args):
        require(len(args) == 2 and isinstance(args[0], Compound) and args[1] == 'V', 'TrackNodesAnimator %r' % (args,))
        self.nodes = []
        self.parameters = None
        self.provider = 'unset'

    def setParameters(self, *args):
        require(len(args) == 4 and all(number(a) for a in args), 'setParameters %r' % (args,))
        self.parameters = args

    def addTrackNode(self, *args):
        require(len(args) == 6 and isinstance(args[0], str) and flag(args[1]) and number(args[2]) and
                isinstance(args[3], str) and isinstance(args[4], str), 'addTrackNode %r' % (args,))
        require(isinstance(args[5], tuple) and len(args[5]) == 4 and all(number(a) for a in args[5]),
                'SpringData %r' % (args,))
        self.nodes.append(args)

    def setWheelsDataProvider(self, provider):
        require(provider is None or isinstance(provider, Wheels), 'setWheelsDataProvider')
        self.provider = provider

    def setLodLink(self, *args):
        require(len(args) == 1 and args[0] is None, 'setLodLink %r' % (args,))

    def setLodSettings(self, *args):
        require(len(args) == 1, 'setLodSettings arity')
        lod_settings(args[0])


class Fashion(object):
    def __init__(self):
        self.splines = 'unset'

    def setSplineTrack(self, *args):
        require(args == (None, None), 'setSplineTrack %r' % (args,))
        self.splines = args


class BigWorldModule(object):
    def __init__(self):
        self.created = []

    def wg_createSplineTrack(self, *args):
        # "OOsfOfOfsii": the caller shape is checked although the absent
        # manager returns None before parsing.
        require(len(args) == 11, 'wg_createSplineTrack arity %d' % len(args))
        require(isinstance(args[0], Fashion) and isinstance(args[1], Compound), 'O O')
        require(isinstance(args[2], str) and number(args[3]) and isinstance(args[4], Model), 's f O')
        require(number(args[5]) and (args[6] is None or isinstance(args[6], Model)), 'f O')
        require(number(args[7]) and args[8] == 'V', 'f s')
        require(isinstance(args[9], int) and isinstance(args[10], int), 'i i')
        self.created.append(args)
        return None


class VehicularModule(object):
    WheelsAnimator = Wheels
    TrackNodesAnimator = TrackNodes


class MathModule(object):
    class Matrix(object):
        def setIdentity(self):
            pass


class Log(object):
    errors = []

    @staticmethod
    def LOG_ERROR(*args):
        Log.errors.append(args)


def descriptor(row):
    wheels = row['wheels']
    sync = float(wheels['syncAngle'] or 60)
    groups = tuple(T['WheelGroup'](g['isLeft'] == 'true', g['template'], int(g['count']),
                                   int(g['startIndex']), float(g['radius'])) for g in wheels['groups'])
    items = tuple(T['Wheel'](w['isLeft'] == 'true', float(w['radius']), w['name'],
                             w['isLeading'] == 'true', float(w['syncAngle'] or sync))
                  for w in wheels['wheels'])
    config = T['WheelsConfig'](100.0, groups, items)
    nodes = ()
    source = row['trackNodes']
    if source:
        d = source['defaults']
        default = lambda key, fallback: float(d[key]) if d[key] is not None else fallback
        nodes = tuple(T['TrackNode'](
            n['name'], n['isLeft'] == 'true', float(n['offset'] or default('offset', 0.0)),
            n['leftSibling'], n['rightSibling'], float(n['damping'] or default('damping', 1.0)),
            float(n['elasticity'] or default('elasticity', 1500.0)),
            float(n['forwardElastK'] or default('forwardElastK', 1.0)),
            float(n['backwardElastK'] or default('backwardElastK', 1.0))) for n in source['nodes'])
    spline = None
    if row['splineDesc']:
        s = row['splineDesc']
        spline = T['SplineConfig'](s['segmentModelLeft'], s['segmentModelRight'], float(s['segmentLength']),
                                   s['leftDesc'], s['rightDesc'], 100.0, float(s['segmentOffset'] or 0),
                                   s['segment2ModelLeft'], s['segment2ModelRight'],
                                   float(s['segment2Offset'] or 0), int(s['atlasUTiles'] or 1),
                                   int(s['atlasVTiles'] or 1))
    # readTrackParams (chassis_readers 142-162).
    params = None
    thickness = float(row['trackThickness'] or 0)
    if source:
        d = source['defaults']
        params = T['TrackParams'](thickness, float(d['maxAmplitude']), float(d['maxOffset']), float(d['gravity']))
    elif spline is not None:
        params = T['TrackParams'](thickness, 0.0, 0.0, 0.0)
    chassis = types.ModuleType('chassis')
    chassis.wheels = config
    chassis.trackNodes = T['NodesAndGroups'](nodes, ())
    chassis.trackParams = params
    chassis.splineDesc = spline
    vDesc = types.ModuleType('vDesc')
    vDesc.chassis = chassis
    return vDesc


def functions(package):
    code = marshal.loads(zipfile.ZipFile(package).read('scripts/client/vehicle_systems/model_assembler.pyc')[8:])
    shared = types.ModuleType('shared_components')
    shared.LodSettings = T['LodSettings']
    names = types.ModuleType('TankNodeNames')
    names.HULL_SWINGING = 'V'
    debug = types.ModuleType('debug_utils')
    debug.LOG_ERROR = Log.LOG_ERROR
    namespace = {'Vehicular': VehicularModule, 'BigWorld': BIGWORLD, 'TankNodeNames': names,
                 'shared_components': shared, 'DEFAULT_MAX_LOD_PRIORITY': None, 'Math': MathModule,
                 'debug_utils': debug, '_ROOT_NODE_NAME': 'V',
                 'SplineTracks': collections.namedtuple('SplineTracks', ('left', 'right')),
                 '__builtins__': __builtins__}
    defaults = {'createWheelsAnimator': (None, None), 'createTrackNodesAnimator': (None, None)}
    result = {}
    for const in code.co_consts:
        if isinstance(const, types.CodeType) and const.co_name in (
                'createWheelsAnimator', 'createTrackNodesAnimator', 'setupSplineTracks',
                '_createNodeNameListByTemplate'):
            result[const.co_name] = types.FunctionType(const, namespace, const.co_name,
                                                       defaults.get(const.co_name))
    namespace['_createNodeNameListByTemplate'] = result['_createNodeNameListByTemplate']
    return result


BIGWORLD = BigWorldModule()


def native(value):
    # Descriptor strings are byte strings in the #1513 runtime.
    if isinstance(value, unicode):
        return value.encode('latin1')
    if isinstance(value, list):
        return [native(item) for item in value]
    if isinstance(value, dict):
        return dict((native(key), native(item)) for key, item in value.items())
    return value


def main(package, inventory):
    fn = functions(package)
    partitions = collections.Counter()
    for row in native(json.load(open(inventory))):
        vDesc = descriptor(row)
        model = Compound()
        fashion = Fashion()
        prereqs = {}
        spline = vDesc.chassis.splineDesc
        if spline is not None:
            for name in (spline.segmentModelLeft, spline.segmentModelRight,
                         spline.segment2ModelLeft, spline.segment2ModelRight):
                if name is not None:
                    prereqs[name] = Model()
        # Hangar order: ClientHangarSpace._VehicleAppearance.__assembleModel 1151-1157.
        tracks = fn['setupSplineTracks'](fashion, vDesc, model, prereqs)
        wheels = fn['createWheelsAnimator'](model, vDesc, None)
        nodes = fn['createTrackNodesAnimator'](model, vDesc, wheels)
        require(isinstance(wheels, Wheels) and isinstance(nodes, TrackNodes), row['vehicle'])
        require(len(nodes.nodes) == len(vDesc.chassis.trackNodes.nodes) and nodes.provider is wheels, row['vehicle'])
        expected = sum(g.count for g in vDesc.chassis.wheels.groups) + len(vDesc.chassis.wheels.wheels)
        require(sum(len(c[3]) if c[0] == 'group' else 1 for c in wheels.calls if c[0] in ('group', 'wheel')) == expected,
                'wheel count ' + row['vehicle'])
        require(wheels.lod == (100.0, None), 'LodSettings value %r' % (wheels.lod,))
        if spline is None:
            require(tracks is None and fashion.splines == 'unset', 'no spline branch ' + row['vehicle'])
        else:
            require(tracks == (None, None) and fashion.splines == (None, None), 'spline branch ' + row['vehicle'])
        # Battle order: CompoundAppearance passes the spline tracks and a filter.
        motion = types.ModuleType('filter')
        motion.movementInfo = object()
        battle = fn['createWheelsAnimator'](Compound(), vDesc, tracks, motion, None)
        require(('movement',) in battle.calls, 'battle movementInfo ' + row['vehicle'])
        require((('spline',) in battle.calls) == (tracks is not None), 'battle spline data ' + row['vehicle'])
        siblings = frozenset((n[3] != '', n[4] != '') for n in nodes.nodes)
        partitions[(bool(nodes.nodes), nodes.parameters is not None, spline is not None,
                    spline is not None and spline.segment2ModelLeft is not None,
                    spline is not None and spline.segmentModelLeft != spline.segmentModelRight,
                    len(vDesc.chassis.wheels.groups) > 0,
                    any(w.isLeading for w in vDesc.chassis.wheels.wheels), siblings)] += 1
    require(not Log.errors, 'unexpected LOG_ERROR %r' % (Log.errors[:3],))
    print(json.dumps({'chassis': sum(partitions.values()), 'partitions': len(partitions),
                      'spline_calls': len(BIGWORLD.created)}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
