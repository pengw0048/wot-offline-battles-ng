"""Drive the production Math oscillators in the CPython 2.7 host.

scenario: run a shared operation list and report every read as float32 bits.
contract: report each factory/method argument-count and conversion outcome.
consumers: run the original createOscillatorFromSection (DynamicCameras),
readFloat/readVec3 (cameras), lerp/clamp (mathUtils) and
ArcadeCamera.__updateOscillators PYC over the exact gui avatar_input_handler
sections, tracing every oscillator call so Unicorn can replay it.
"""
import json
import marshal
import math
import struct
import sys
import types
import zipfile

import _oscillator_host as host

host.initScripts()
import Math


class Vector3(object):
    def __init__(self, *args):
        if len(args) == 1 and isinstance(args[0], (int, float)):
            args = (args[0],) * 3
        elif len(args) == 1:
            args = tuple(args[0])
        self.x, self.y, self.z = [float(v) for v in args]

    def tuple(self):
        return (self.x, self.y, self.z)

    def __iter__(self):
        return iter(self.tuple())

    def __getitem__(self, index):
        return self.tuple()[index]

    def __setitem__(self, index, value):
        setattr(self, 'xyz'[index], float(value))

    def set(self, *args):
        Vector3.__init__(self, *args)

    def __add__(self, other):
        return Vector3(self.x + other.x, self.y + other.y, self.z + other.z)

    def __mul__(self, scale):
        return Vector3(self.x * scale, self.y * scale, self.z * scale)


Math.Vector3 = Vector3
host.setVectorType(Vector3)


def f32bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def vector_bits(vector):
    return [f32bits(v) for v in (vector.x, vector.y, vector.z)]


def encode(value):
    if isinstance(value, dict):
        return objects[value['ref']]
    if isinstance(value, list):
        return Vector3(*value)
    return value


objects = {}


def run_scenario(operations):
    report = []
    for operation in operations:
        verb = operation[0]
        if verb == 'new':
            _, ident, kind, args = operation
            objects[ident] = getattr(Math, kind)(*[encode(a) for a in args])
            report.append(['new', ident, True])
        elif verb == 'update':
            objects[operation[1]].update(operation[2])
        elif verb == 'impulse':
            objects[operation[1]].applyImpulse(Vector3(*operation[2]))
        elif verb == 'reset':
            objects[operation[1]].reset()
        elif verb == 'set':
            setattr(objects[operation[1]], operation[2], Vector3(*operation[3]))
        elif verb == 'get':
            report.append(['get', operation[1], operation[2],
                           vector_bits(getattr(objects[operation[1]], operation[2]))])
    return report


def engine_arg_count_error(call):
    """The engine's message for an argCountError call the original made."""
    try:
        host.argCountError(*[str(value) if isinstance(value, unicode) else value for value in call[1:]])
    except TypeError as error:
        return 'TypeError: %s' % error


def run_contract(request):
    objects['a'] = Math.PyOscillator(1.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), (1.0, 1.0, 1.0))
    objects['b'] = Math.PyOscillator(1.0, (1.0, 1.0, 1.0), (1.0, 1.0, 1.0), (1.0, 1.0, 1.0))
    report = []
    expected = [engine_arg_count_error(call) for call in request['argCountErrors']]
    for verb, name, args, fail_at in request['cases']:
        args = [encode(a) for a in args]
        if fail_at is not None:
            args[fail_at] = 'bad'
        try:
            if verb == 'new':
                result = getattr(Math, name)(*args)
            else:
                result = getattr(objects['a'], name)(*args)
            outcome = 'None' if result is None else 'object'
            message = None
        except Exception as error:
            outcome, message = 'NULL', '%s: %s' % (type(error).__name__, error)
        report.append([verb, name, len(args), fail_at, outcome, message])
    for name in ('constraints', 'deviation', 'externalForce', 'velocity'):
        try:
            setattr(objects['a'], name, 'bad')
            report.append(['setter', name, 1, 0, 0, None])
        except TypeError as error:
            report.append(['setter', name, 1, 0, -1, 'TypeError: %s' % error])
    # The production compound rejects a non-oscillator instead of reading it.
    for args in ((None, objects['a']), (objects['a'], 5)):
        try:
            Math.PyCompoundOscillator(*args)
            report.append(['compound-type', None])
        except TypeError as error:
            report.append(['compound-type', str(error)])
    return {'report': report, 'expected': expected}


# -- original PYC consumers ---------------------------------------------------

def code_objects(package, member):
    data = zipfile.ZipFile(package).read(member)
    return marshal.loads(data[8:])


def module_function(code, name):
    """The code object a module-level assignment binds to name."""
    import opcode
    raw, index, last = code.co_code, 0, None
    while index < len(raw):
        op = ord(raw[index])
        argument = ord(raw[index + 1]) | ord(raw[index + 2]) << 8 if op >= opcode.HAVE_ARGUMENT else None
        if op == opcode.opmap['LOAD_CONST'] and isinstance(code.co_consts[argument], types.CodeType):
            last = code.co_consts[argument]
        if op == opcode.opmap['STORE_NAME'] and code.co_names[argument] == name and last is not None:
            return last
        index += 3 if argument is not None else 1
    raise KeyError(name)


def find(code, *path):
    for name in path:
        code = [c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == name][0]
    return code


class Section(object):
    def __init__(self, name, values):
        self.name, self.values = name, values

    def readFloat(self, key, default):
        return float(self.values[key]) if key in self.values else default

    def readVector3(self, key, default):
        if key not in self.values:
            return Vector3(default)
        return Vector3(*[float(v) for v in self.values[key].split()])


trace = []


class Traced(object):
    def __init__(self, ident, oscillator):
        object.__setattr__(self, '_ident', ident)
        object.__setattr__(self, '_oscillator', oscillator)

    def __getattr__(self, name):
        if name in ('update', 'applyImpulse', 'reset'):
            def call(*args):
                verb = {'update': 'update', 'applyImpulse': 'impulse', 'reset': 'reset'}[name]
                trace.append([verb, self._ident] + [list(a) if isinstance(a, Vector3) else a for a in args])
                return getattr(self._oscillator, name)(*args)
            return call
        value = getattr(self._oscillator, name)
        trace.append(['get', self._ident, name, vector_bits(value)])
        return value

    def __setattr__(self, name, value):
        trace.append(['set', self._ident, name, list(Vector3(value))])
        setattr(self._oscillator, name, value)


class TracedMath(object):
    Vector3 = Vector3

    def __getattr__(self, kind):
        factory = getattr(Math, kind)

        def construct(*args):
            ident = 'o%d' % len([t for t in trace if t[0] == 'new'])
            native = [a._oscillator if isinstance(a, Traced) else a for a in args]
            encoded = [{'ref': a._ident} if isinstance(a, Traced) else
                       (list(a) if isinstance(a, Vector3) else a) for a in args]
            trace.append(['new', ident, kind, encoded])
            return Traced(ident, factory(*native))
        return construct


def run_consumers(package, sections):
    traced_math = TracedMath()
    math_utils = find(code_objects(package, 'scripts/client/AvatarInputHandler/mathUtils.pyc'))
    utils = types.ModuleType('mathUtils')
    for name in ('clamp', 'lerp'):
        setattr(utils, name, types.FunctionType(module_function(math_utils, name), utils.__dict__))
    cameras = code_objects(package, 'scripts/client/AvatarInputHandler/cameras.pyc')
    camera_globals = {'Math': traced_math, 'mathUtils': utils, 'xrange': xrange}
    dynamic = code_objects(package, 'scripts/client/AvatarInputHandler/DynamicCameras/__init__.pyc')
    dynamic_globals = {'Math': traced_math, 'math': math, 'Vector3': Vector3}
    for name in ('readVec3', 'readFloat'):
        dynamic_globals[name] = types.FunctionType(find(cameras, name), camera_globals)
    for name in ('createOscillatorFromSection', '__getOscillatorParams', '__getNoiseOscillatorParams',
                 '__getRandomNoiseOscillatorFlatParams', '__getRandomNoiseOscillatorSphericalParams'):
        dynamic_globals[name] = types.FunctionType(find(dynamic, name), dynamic_globals)
    create = dynamic_globals['createOscillatorFromSection']

    built = {}
    for path, (name, values, as_angle) in sorted(sections.items()):
        built[path] = create(Section(name, values), as_angle)

    # ArcadeCamera.__readCfg L696 wraps the movement oscillator in a compound.
    arcade = code_objects(package, 'scripts/client/AvatarInputHandler/DynamicCameras/ArcadeCamera.pyc')
    update_code = find(arcade, 'ArcadeCamera', '__updateOscillators')

    class ArcadeStub(object):
        _DYNAMIC_ENABLED = True

        @staticmethod
        def isCameraDynamic():
            return ArcadeStub._DYNAMIC_ENABLED

    class CameraState(object):
        def __init__(self, prefix):
            self._ArcadeCamera__impulseOscillator = built[prefix + '/impulseOscillator']
            self._ArcadeCamera__movementOscillator = traced_math.PyCompoundOscillator(
                built[prefix + '/movementOscillator'],
                traced_math.PyOscillator(1.0, Vector3(50), Vector3(20), Vector3(0.01, 0.0, 0.01)))
            self._ArcadeCamera__noiseOscillator = built[prefix + '/randomNoiseOscillatorSpherical']
            self._ArcadeCamera__dynamicCfg = {'zoomExposure': 0.25}
            self.acceleration = Vector3(0.0)
            self.relativeDistance = 0.5

        def _ArcadeCamera__calcCurOscillatorAcceleration(self, deltaTime):
            return self.acceleration

        def _ArcadeCamera__calcRelativeDist(self):
            return self.relativeDistance

    update = types.FunctionType(update_code, {'ArcadeCamera': ArcadeStub, 'Vector3': Vector3,
                                              'mathUtils': utils})
    results = []
    for prefix in ('/arcadeMode/camera/dynamics', '/postMortemMode/camera/dynamics'):
        state = CameraState(prefix)
        state._ArcadeCamera__impulseOscillator.applyImpulse(Vector3(0.4, 0.0, -0.3))
        state._ArcadeCamera__noiseOscillator.applyImpulse(Vector3(0.02, 0.01, -0.03))
        for frame in range(240):
            state.acceleration = Vector3(math.sin(frame * 0.1) * 3.0, 0.0, math.cos(frame * 0.07) * 2.0)
            state.relativeDistance = (frame % 60) / 60.0
            deltaTime = (0.016, 0.033, 0.0, 0.07, 0.008)[frame % 5]
            impulse, movement, zoom = update(state, deltaTime)
            results.append([vector_bits(impulse), vector_bits(movement), f32bits(zoom)])
            if frame == 120:
                ArcadeStub._DYNAMIC_ENABLED = False
            if frame == 140:
                ArcadeStub._DYNAMIC_ENABLED = True
                state._ArcadeCamera__noiseOscillator.applyImpulse(Vector3(0.0, 0.05, 0.0))
    for path in sorted(sections):
        if path.endswith('randomNoiseOscillatorFlat') or path.endswith('/oscillator'):
            oscillator = built[path]
            oscillator.applyImpulse(Vector3(1.0, 0.5, -0.25))
            for frame in range(90):
                oscillator.update((0.016, 0.033, 0.002)[frame % 3])
                oscillator.deviation
    return {'trace': trace, 'arcade': results,
            'kinds': dict((path, type(built[path]._oscillator).__name__) for path in built)}


def main():
    mode = sys.argv[1]
    if mode == 'scenario':
        result = run_scenario(json.load(open(sys.argv[2])))
    elif mode == 'contract':
        result = run_contract(json.load(open(sys.argv[2])))
    else:
        result = run_consumers(sys.argv[2], json.load(open(sys.argv[3])))
    result = {'result': result, 'state': host.state()}
    json.dump(result, open(sys.argv[-1], 'w'))


main()
