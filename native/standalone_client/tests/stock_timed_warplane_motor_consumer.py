"""Exact FlockExotic bytecode against the production PyTimedWarplaneMotor.

Runs inside the CPython 2.7 host. STOCK_FLOCK_EXOTIC_PYC names the exact
scripts.pkg member; the authored Erlenberg record drives the properties.
"""
import marshal
import math
import os
import random
import types

import BigWorld
import Host
import Math

BUILTINS = __builtins__


class Vector3(object):
    """Arithmetic subset of Math.Vector3 used by FlockExotic."""

    def __init__(self, x=0., y=0., z=0.):
        self.x, self.y, self.z = float(x), float(y), float(z)

    def __len__(self):
        return 3

    def __getitem__(self, index):
        return (self.x, self.y, self.z)[index]

    def __add__(self, other):
        return Vector3(self.x + other[0], self.y + other[1], self.z + other[2])

    def __sub__(self, other):
        return Vector3(self.x - other[0], self.y - other[1], self.z - other[2])

    def __mul__(self, scale):
        return Vector3(self.x * scale, self.y * scale, self.z * scale)

    __rmul__ = __mul__

    def __div__(self, scale):
        return Vector3(self.x / scale, self.y / scale, self.z / scale)

    @property
    def length(self):
        return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)

    def normalise(self):
        length = self.length
        if length:
            self.x, self.y, self.z = self.x / length, self.y / length, self.z / length


Math.Vector3 = Vector3
BigWorld.time = Host.time


def expect(kind, function, *args):
    try:
        function(*args)
    except kind as error:
        return str(error)
    raise AssertionError('%s did not raise %s' % (function, kind.__name__))


def close(a, b, tolerance=1e-3):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def check_native_contract():
    Host.setTime(100.)
    point = (Vector3(0, 10, 0), Vector3(0, 0, 10), 100.)
    assert expect(TypeError, BigWorld.PyTimedWarplaneMotor, point) == 'Invalid argument count'
    assert expect(TypeError, BigWorld.PyTimedWarplaneMotor, [Vector3(), Vector3(), 1.], point) == \
        'Invalid controlPoint arguments - must be tuples'
    assert expect(TypeError, BigWorld.PyTimedWarplaneMotor, (Vector3(), Vector3()), point) == \
        'Invalid controlPoint arguments - must be tuples'
    expect(TypeError, BigWorld.PyTimedWarplaneMotor, ('a', Vector3(), 1.), point)
    expect(TypeError, BigWorld.PyTimedWarplaneMotor, point, (Vector3(0, 10, 20), Vector3(0, 0, 10), 102.), 'x')
    # The roll speed is optional and extra arguments are ignored.
    motor = BigWorld.PyTimedWarplaneMotor(point, (Vector3(0, 10, 20), Vector3(0, 0, 10), 102.), 0., 'extra')
    assert motor.owner is None
    expect(TypeError, motor.addTrajectoryPoint, Vector3(), Vector3())
    expect(TypeError, motor.addTrajectoryPoint, Vector3(), Vector3(), 1., None, 2)
    model, other = Host.Model(), Host.Model()
    model.addMotor(motor)
    assert motor.owner is model
    expect(ValueError, other.addMotor, motor)
    # Linear segment at constant velocity: position, +Z heading, boundary times.
    for now, z in ((100., 0.), (101., 10.), (102., 20.)):
        Host.setTime(now)
        model.tick(1. if now > 100. else 0.)
        world = model.world()
        assert close(world[12:15], (0., 10., z)), (now, world[12:15])
        assert close(world[8:11], (0., 0., 1.)), world[8:11]
    # Beyond the last point it extrapolates along the last velocity.
    Host.setTime(103.)
    model.tick(1.)
    assert close(model.world()[12:15], (0., 10., 30.))
    # Callbacks fire once per crossed point; late/stale points are ignored.
    seen = []
    motor.addTrajectoryPoint(Vector3(0, 10, 40), Vector3(0, 0, 10), 104., lambda p, v: seen.append((p.z, v.z)))
    motor.addTrajectoryPoint(Vector3(0, 10, 99), Vector3(0, 0, 10), 103.5, lambda p, v: seen.append('stale'))
    motor.addTrajectoryPoint(Vector3(0, 10, 50), Vector3(0, 0, 10), 105., None)
    # A point fires when its segment is entered, i.e. once time passes it.
    Host.setTime(104.)
    model.tick(1.)
    assert seen == [], seen
    Host.setTime(105.)
    model.tick(1.)
    assert seen == [(50., 10.)], seen
    Host.setTime(106.)
    model.tick(1.)  # the None callback prints "Function is not callable" and is cleared
    assert seen == [(50., 10.)]
    # Detach stops publication; a callback that clears the motors is safe.
    model.motors = ()
    assert motor.owner is None and model.motors == ()
    transforms = model.world()
    model.tick(1.)
    assert model.world() == transforms
    Host.setTime(105.)
    bomb = BigWorld.PyTimedWarplaneMotor((Vector3(), Vector3(1, 0, 0), 105.), (Vector3(1, 0, 0), Vector3(1, 0, 0), 106.))
    other.addMotor(bomb)
    bomb.addTrajectoryPoint(Vector3(2, 0, 0), Vector3(1, 0, 0), 107., lambda p, v: setattr(other, 'motors', ()))
    del bomb
    Host.setTime(107.5)
    other.tick(2.5)
    assert other.motors == ()
    other.tick(1.)
    expect(ValueError, other.delMotor, motor)


def find(code, name):
    if code.co_name == name:
        return code
    for child in code.co_consts:
        if isinstance(child, types.CodeType):
            found = find(child, name)
            if found is not None:
                return found
    return None


class Stub(object):
    @staticmethod
    def __init__(self):
        pass


class LiveModels(object):
    """#1513 Entity.models: a live PySTLSequence over the embodiment vector
    (getter 00662710 -> 006a9490, no tp_iter), indexed per access."""

    def __init__(self, attached):
        self.attached = attached

    def __len__(self):
        return len(self.attached)

    def __getitem__(self, index):
        return self.attached[index]


def check_flock_exotic():
    path = os.environ.get('STOCK_FLOCK_EXOTIC_PYC')
    if not path:
        return
    module = marshal.loads(open(path, 'rb').read()[8:])
    scope = {'__builtins__': BUILTINS, 'BigWorld': BigWorld, 'Math': Math, 'math': math, 'random': random,
             'FlockLike': Stub, 'CallbackDelayer': Stub}
    BigWorld.Entity = Stub

    def function(name):
        return types.FunctionType(find(module, name), scope)

    class Flock(object):
        # Authored 13_erlenberg FlockExotic record (SparrowsTakeoff.model).
        speed, initSpeedRandom, speedRandom, accelerationTime = 10., (1., 2.), (.8, 1.2), 2.
        spawnRadius, spawnHeight, flightRadius, flightHeight = 1., 1., 75., 15.
        flightAngleMin, flightAngleMax, flightOffsetFromOrigin, lifeTime = 60., -60., 20., 13.
        flightSound = 'birds_flight'

        def __init__(self):
            self.position = Vector3(-305.7865, 7.5686, -353.3289)
            self.attached = []
            self.delayed = []
            self.sounds = []

        @property
        def models(self):
            return LiveModels(self.attached)

        def addModel(self, model):
            self.attached.append(model)

        # 006614e0 -> 006784c0 erases the embodiment immediately (0067854a/0067858e).
        def delModel(self, model):
            self.attached.remove(model)

        def delayCallback(self, delay, callback):
            self.delayed.append((delay, callback))

        def _addSound(self, model, name):
            self.sounds.append(name)

        def _delSound(self):
            self.sounds.append(None)

    def flight(count):
        flock = Flock()
        function('__init__')(flock)
        for name in ('__createMotor', '__getRandomSpawnPos', '__getRandomTargetPos', '__onFlightEnd'):
            setattr(flock, '_FlockExotic' + name, types.MethodType(function(name), flock))
        birds = [Host.Model() for unused in range(count)]
        for bird in birds:
            bird.visible = False
        flock._FlockExotic__models = birds
        random.seed(1513 + count)
        Host.setTime(431.25)
        function('onTrigger')(flock)
        assert flock.attached == birds and all(bird.visible for bird in birds)
        assert all(bird.actions() == ['FlockAnimAction'] for bird in birds)
        assert all(len(bird.motors) == 1 and bird.motors[0].owner is bird for bird in birds)
        assert len(flock.delayed) == 1 and flock.sounds == ['birds_flight']
        motors = [bird.motors[0] for bird in birds]
        starts = [bird.world()[12:15] for bird in birds]
        # Model-driven frames: birds leave their spawn and face their velocity.
        clock = 431.25
        while clock < 431.25 + flock.delayed[0][0] + 1.:
            clock += 1 / 30.
            Host.setTime(clock)
            for bird in birds:
                bird.tick(1 / 30.)
        for bird, start in zip(birds, starts):
            world = bird.world()
            assert not close(world[12:15], start, 1.), (world[12:15], start)
            assert abs(math.sqrt(sum(v * v for v in world[8:11])) - 1.) < 1e-3
        flock.delayed[0][1]()
        return flock, birds, motors, function

    # One bird: flight end detaches, hides and releases it; a retrigger
    # builds a fresh owned motor.
    flock, birds, motors, function = flight(1)
    assert flock.attached == [] and not birds[0].visible and birds[0].motors == ()
    assert motors[0].owner is None and flock.sounds == ['birds_flight', None]
    function('onTrigger')(flock)
    assert len(birds[0].motors) == 1 and birds[0].motors[0] is not motors[0]
    assert birds[0].motors[0].owner is birds[0]

    # Several birds: the stock loop deletes from the live sequence it is
    # iterating, so every second bird is skipped exactly as on #1513. Each
    # processed bird is fully torn down; each skipped bird keeps flying.
    for count, skipped in ((3, [1]), (4, [1, 3])):
        flock, birds, motors, unused = flight(count)
        assert flock.attached == [birds[i] for i in skipped], (count, flock.attached)
        for index, (bird, motor) in enumerate(zip(birds, motors)):
            if index in skipped:
                assert bird.visible and bird.motors == (motor,) and motor.owner is bird
                before = bird.world()[12:15]
                Host.setTime(500.)
                bird.tick(1.)
                assert bird.world()[12:15] != before
            else:
                assert not bird.visible and bird.motors == () and motor.owner is None


check_native_contract()
check_flock_exotic()
