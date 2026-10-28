"""Embedded CPython 2.7: exact #1513 FlockManager/FlockExotic PYC over the WGFlockManager core."""
from __future__ import print_function

import json
import marshal
import os
import sys
import types
import zipfile

import BigWorld


def report(name, value):
    print('%s %r' % (name, value))
    sys.stdout.flush()


class Vector2(object):
    def __init__(self, x, y):
        self.x, self.y = float(x), float(y)


class Vector3(object):
    def __init__(self, x, y, z):
        self.x, self.y, self.z = float(x), float(y), float(z)


Math = types.ModuleType('Math')
Math.Vector2, Math.Vector3 = Vector2, Vector3
sys.modules['Math'] = Math

package = zipfile.ZipFile(os.environ['WOT_SCRIPTS_PACKAGE'])


def code(name):
    return marshal.loads(package.read('scripts/client/%s.pyc' % name)[8:])


def find(owner, name):
    for constant in owner.co_consts:
        if hasattr(constant, 'co_code'):
            if constant.co_name == name:
                return constant
            found = find(constant, name)
            if found is not None:
                return found


FlockManager = types.ModuleType('FlockManager')
exec(code('FlockManager'), FlockManager.__dict__)
sys.modules['FlockManager'] = FlockManager


class Event(object):
    def __init__(self):
        self.handlers = []

    def __iadd__(self, handler):
        self.handlers.append(handler)
        return self

    def __isub__(self, handler):
        self.handlers.remove(handler)
        return self

    def __call__(self, *args):
        for handler in list(self.handlers):
            handler(*args)


class Player(object):
    def __init__(self):
        self.onVehicleEnterWorld = Event()
        self.onVehicleLeaveWorld = Event()


class Provider(object):
    def __init__(self, x, z):
        self.translation = (x, 0.0, z)


class Vehicle(object):
    def __init__(self, x, z):
        self.matrix = Provider(x, z)


# The exact FlockExotic.onEnterWorld body registers each authored flock.
enter_world = types.FunctionType(find(code('FlockExotic'), 'onEnterWorld'), {'FlockManager': FlockManager})


class Flock(object):
    triggers = []

    def __init__(self, name, x, z, radius, explosion, respawn):
        self.name = name
        self.position = Vector3(x, 0.0, z)
        self.triggerRadius = radius
        self.explosionRadius = Vector2(*explosion)
        self.respawnTime = respawn

    def _FlockExotic__loadModels(self, prereqs):
        pass

    def onTrigger(self):
        Flock.triggers.append(self.name)
        reenter = getattr(self, 'reenter', None)
        if reenter:
            reenter()


clock = [0.0, 0]


def tick(camera=(0.0, 0.0), seconds=1.0):
    clock[0] += seconds
    clock[1] += 1
    return BigWorld._tick(clock[0], clock[1], camera[0], camera[1])


# 2000 m x 1000 m camera space.
BIG = (-1000.0, -50.0, -500.0, 1000.0, 300.0, 500.0)
manager = FlockManager.getManager()
native = manager._FlockManager__flockManager
report('singleton', FlockManager.getManager() is manager)
report('callback-bound', native.onTriggerCallback == manager._FlockManager__onTrigger)
for args in ((), (1.0, 2.0)):
    try:
        native.start(*args)
        report('start-arity-%d' % len(args), 'accepted')
    except TypeError as error:
        report('start-arity-%d' % len(args), 'TypeError')
try:
    native.addFlock(Vector2(0, 0), 1, 2.0, Vector2(1, 2))
    report('addFlock-arity-4', 'accepted')
except TypeError:
    report('addFlock-arity-4', 'TypeError')
report('factory-ignores-args', type(BigWorld.WGFlockManager(1, 2, x=3)).__name__)

player = Player()
manager.start(player)
report('events-subscribed', (len(player.onVehicleEnterWorld.handlers), len(player.onVehicleLeaveWorld.handlers)))
near = Flock('near', 50.0, 50.0, 20.0, (8.0, 35.0), 400.0)
ring = Flock('ring', 150.0, 50.0, 20.0, (8.0, 35.0), 400.0)
for flock in (near, ring):
    enter_world(flock, {})
report('python-list', [f.name for f in manager._FlockManager__flocks])

# No camera space yet: start leaves the owner stopped; the stock caller only
# starts from Avatar.onSpaceLoaded.
manager.onSpaceLoaded()
report('no-space-sweeps', tick())
BigWorld._setCameraSpace(BIG)
manager.onSpaceLoaded()
report('same-frame-sweeps', BigWorld._tick(clock[0], clock[1], 0.0, 0.0))

vehicle = Vehicle(500.0, 500.0)
player.onVehicleEnterWorld(vehicle)
tick((50.0, 50.0))
report('far-vehicle', list(Flock.triggers))
vehicle.matrix.translation = (60.0, 0.0, 50.0)
tick((50.0, 50.0))
report('dynamic-provider', list(Flock.triggers))
tick((50.0, 50.0))
report('inside-no-repeat', list(Flock.triggers))

# ProjectileMover.add/__notifyProjectileHit -> wrapper.onProjectile.
manager.onProjectile(Vector3(150.0, 3.0, 70.0))
tick((50.0, 50.0))
report('projectile-ring', list(Flock.triggers))
manager.onProjectile(Vector3(150.0, 3.0, 55.0))
tick((50.0, 50.0))
report('projectile-inner-and-cooldown', list(Flock.triggers))

# vehicleLeave erases the provider by identity; a fresh flock under it stays quiet.
player.onVehicleLeaveWorld(vehicle)
enter_world(Flock('after-leave', 60.0, 50.0, 20.0, (8.0, 35.0), 1.0), {})
tick((50.0, 50.0))
report('vehicle-left', list(Flock.triggers))
other = Vehicle(60.0, 50.0)
player.onVehicleEnterWorld(other)
tick((50.0, 50.0))
report('other-vehicle', list(Flock.triggers))
player.onVehicleLeaveWorld(other)

# Callback reentry: stop from inside onTrigger ends the sweep.
manager.stop(player)
report('stopped-handlers', (len(player.onVehicleEnterWorld.handlers), len(player.onVehicleLeaveWorld.handlers)))
report('stopped-list', manager._FlockManager__flocks)
report('callback-kept', native.onTriggerCallback is not None)
report('stopped-sweeps', tick((50.0, 50.0)))

del Flock.triggers[:]
manager.start(player)
first = Flock('first', 50.0, 50.0, 20.0, (8.0, 35.0), 1.0)
second = Flock('second', 52.0, 50.0, 20.0, (8.0, 35.0), 1.0)
first.reenter = lambda: manager.stop(player)
for flock in (first, second):
    enter_world(flock, {})
manager.onSpaceLoaded()
player.onVehicleEnterWorld(Vehicle(51.0, 50.0))
tick((50.0, 50.0))
report('reenter-stop', list(Flock.triggers))
report('reenter-stop-sweeps-after', tick((50.0, 50.0)))

# A trigger callback that removes a later vehicle revokes its registration.
del Flock.triggers[:]
manager.start(player)
d1 = Flock('d1', 50.0, 50.0, 20.0, (8.0, 35.0), 1.0)
d2 = Flock('d2', 150.0, 50.0, 20.0, (8.0, 35.0), 1.0)
first_tank, second_tank = Vehicle(50.0, 50.0), Vehicle(150.0, 50.0)
d1.reenter = lambda: player.onVehicleLeaveWorld(second_tank)
for flock in (d1, d2):
    enter_world(flock, {})
manager.onSpaceLoaded()
player.onVehicleEnterWorld(first_tank)
player.onVehicleEnterWorld(second_tank)
tick((50.0, 50.0))
report('leave-in-callback', list(Flock.triggers))
manager.stop(player)

# A run belongs to the camera space assignment it started under.
del Flock.triggers[:]
manager.start(player)
enter_world(Flock('a1', 50.0, 50.0, 20.0, (8.0, 35.0), 1.0), {})
manager.onSpaceLoaded()
tank = Vehicle(500.0, 500.0)
player.onVehicleEnterWorld(tank)
report('space-same-sweeps', tick((50.0, 50.0)))
# Same bounds, different space object: the queued timer must not sweep A.
replacement = tuple(list(BIG))
BigWorld._setCameraSpace(replacement)
tank.matrix.translation = (50.0, 0.0, 50.0)
report('space-replaced-sweeps', tick((50.0, 50.0)))
report('space-replaced', list(Flock.triggers))
enter_world(Flock('b1', 52.0, 50.0, 20.0, (8.0, 35.0), 1.0), {})
manager.onSpaceLoaded()
player.onVehicleEnterWorld(Vehicle(50.0, 50.0))
BigWorld._setCameraSpace(replacement)
report('space-reassigned-sweeps', tick((50.0, 50.0)))
report('space-next', list(Flock.triggers))
# The same space object again after None is a new assignment.
enter_world(Flock('b2', 150.0, 50.0, 20.0, (8.0, 35.0), 1.0), {})
BigWorld._setCameraSpace(None)
BigWorld._setCameraSpace(replacement)
manager.onProjectile(Vector3(150.0, 3.0, 70.0))
report('space-reused-sweeps', tick((50.0, 50.0)))
report('space-reused', list(Flock.triggers))
manager.stop(player)

# A callback that replaces the camera space ends the old sweep.
del Flock.triggers[:]
manager.start(player)
c1 = Flock('c1', 50.0, 50.0, 20.0, (8.0, 35.0), 1.0)
c2 = Flock('c2', 52.0, 50.0, 20.0, (8.0, 35.0), 1.0)
c1.reenter = lambda: BigWorld._setCameraSpace(tuple(list(BIG)))
for flock in (c1, c2):
    enter_world(flock, {})
manager.onSpaceLoaded()
player.onVehicleEnterWorld(Vehicle(51.0, 50.0))
tick((50.0, 50.0))
report('space-callback', list(Flock.triggers))
report('space-callback-sweeps-after', tick((50.0, 50.0)))
manager.stop(player)

# An empty flock list keeps its timer under an unchanged space.
manager.start(player)
manager.onSpaceLoaded()
report('empty-sweeps', (tick(), tick()))
manager.stop(player)

# Authored FlockExotic records and space.settings bounds of exact #1513 maps.
for path in filter(None, os.environ.get('WOT_FLOCK_AUTHORED', '').split(os.pathsep)):
    space = json.load(open(path))
    del Flock.triggers[:]
    manager.start(player)
    flocks = []
    for index, record in enumerate(space['records']):
        p = record['properties']
        t = record['transform']
        flocks.append(Flock(index, t[9], t[11], p['triggerRadius'][0],
                            p['explosionRadius'], p['respawnTime'][0]))
        enter_world(flocks[-1], {})
    BigWorld._setCameraSpace(tuple(space['bounds']))
    manager.onSpaceLoaded()
    car = Vehicle(5000.0, 5000.0)
    player.onVehicleEnterWorld(car)
    for flock in flocks:
        car.matrix.translation = (flock.position.x, 0.0, flock.position.z)
        tick((flock.position.x, flock.position.z))
        car.matrix.translation = (5000.0, 0.0, 5000.0)
        tick((flock.position.x, flock.position.z))
    name = space['name']
    report(name + '-count', len(flocks))
    report(name + '-triggered', sorted(set(Flock.triggers)))
    report(name + '-respawn', sorted(set(f.respawnTime for f in flocks)))
    if flocks:
        car.matrix.translation = (flocks[0].position.x, 0.0, flocks[0].position.z)
        tick((flocks[0].position.x, flocks[0].position.z))
        report(name + '-cooldown-repeat', Flock.triggers.count(0))
    report(name + '-sweeps', tick())
    manager.stop(player)
