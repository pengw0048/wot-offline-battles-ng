"""Original #1513 ProjectileMover.pyc over the production ballistics binding.

Python 2.7, run inside stock_ballistics_host. The fixtures stand in only for
modules the mover imports (Math, BigWorld's player and Model, triggers,
flocks, effects); every native simulator call reaches the production code.
"""
import marshal
import math
import sys
import types
import zipfile

import _ballistics_host as host

PACKAGE = sys.argv[1]


class Vector3(object):
    def __init__(self, *args):
        if len(args) == 1:
            args = tuple(args[0])
        self.x, self.y, self.z = [float(v) for v in (args or (0.0, 0.0, 0.0))]

    def __add__(self, other):
        return Vector3(self.x + other.x, self.y + other.y, self.z + other.z)

    def __sub__(self, other):
        return Vector3(self.x - other.x, self.y - other.y, self.z - other.z)

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    def scale(self, k):
        return Vector3(self.x * k, self.y * k, self.z * k)

    @property
    def length(self):
        return math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)

    def distTo(self, other):
        return (self - other).length

    def __repr__(self):
        return 'Vector3(%.3f, %.3f, %.3f)' % (self.x, self.y, self.z)


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


LOG = []


def record(*entry):
    LOG.append(entry)


class Arena(object):
    boundary = 300.0
    raise_next = False

    def collideWithSpaceBB(self, start, end):
        if Arena.raise_next:
            Arena.raise_next = False
            raise RuntimeError('arena torn down')
        if end.z > Arena.boundary >= start.z:
            f = (Arena.boundary - start.z) / (end.z - start.z)
            return start + (end - start).scale(f)
        return None


class InputHandler(object):
    def __init__(self):
        self.onCameraChanged = Event()

    def onProjectileHit(self, position, caliber, own):
        record('inputHit', round(position.z, 2), caliber, own)


class TerrainEffects(object):
    def addNew(self, position, effects, keyPoints, callback, **kwargs):
        record('explosion', round(position.z, 2), effects, kwargs['attackerID'])


class Player(object):
    playerVehicleID = 11
    spaceID = 3

    def __init__(self):
        self.arena = Arena()
        self.inputHandler = InputHandler()
        self.terrainEffects = TerrainEffects()
        self.models = []

    def addModel(self, model):
        self.models.append(model)

    def delModel(self, model):
        self.models.remove(model)


PLAYER = Player()


class Entities(object):
    def get(self, key, default=None):
        return default


def install_modules():
    host.initScripts()
    bigworld = sys.modules['BigWorld']
    bigworld.player = lambda: PLAYER
    bigworld.entities = Entities()
    bigworld.Model = lambda name: host.model()
    bigworld.wg_collideWater = lambda start, end, mode: -1
    bigworld.wg_collideSegment = lambda *args: None
    bigworld.wg_addWaterRipples = lambda *args: None
    bigworld.callback = lambda delay, function: None
    modules = {
        'Math': {'Vector3': Vector3, 'Matrix': None},
        'constants': {'SERVER_TICK_LENGTH': 0.1},
        'TriggersManager': {'g_manager': types.ModuleType('g_manager'),
                            'TRIGGER_TYPE': type('T', (), {'PLAYER_SHOT_MISSED': 7})},
        'FlockManager': {'getManager': lambda: FLOCK},
        'vehicle_systems': {},
        'vehicle_systems.tankStructure': {'TankPartNames': type('P', (), {'GUN': 'gun'}),
                                          'TankNodeNames': type('N', (), {'TURRET_JOINT': 'tj'})},
        'helpers': {'gEffectsDisabled': lambda: False},
        'BattleReplay': {'g_replayCtrl': type('R', (), {'isTimeWarpInProgress': False})},
    }
    for name, attributes in modules.items():
        module = types.ModuleType(name)
        module.__dict__.update(attributes)
        sys.modules[name] = module
    sys.modules['TriggersManager'].g_manager.fireTrigger = lambda kind: record('trigger', kind)


class Flock(object):
    def onProjectile(self, position):
        record('flock', round(position.z, 2))


FLOCK = Flock()


class Effects(object):
    def attachTo(self, model, data, name, **kwargs):
        data['attached'] = name
        record('attach', name, kwargs['isPlayerVehicle'])

    def detachFrom(self, data, name):
        record('detach', name)

    def detachAllFrom(self, data):
        record('detachAll')


EFFECTS = Effects()
DESCR = {'projectile': ('tracer.model', 'own.model', EFFECTS), 'caliber': 85,
         'waterParams': (1.0, 2.0), 'groundHit': ('keys', 'groundEffects', None)}


def load_mover():
    member = zipfile.ZipFile(PACKAGE).read('scripts/client/ProjectileMover.pyc')
    module = types.ModuleType('ProjectileMover')
    sys.modules['ProjectileMover'] = module
    exec(marshal.loads(member[8:]), module.__dict__)
    return module


def expect(condition, message):
    if not condition:
        raise AssertionError(message + '\nlog: %r' % LOG)


CLOCK = [100.0]


def advance(t):
    CLOCK[0] = t
    host.setTime(t)
    host.tick()
    return [model.rev() for model in PLAYER.models]


def later(dt):
    return advance(CLOCK[0] + dt)


def fire(mover, shot, attacker=11, start=None, max_distance=1000.0):
    reference = Vector3(0, 1, 0)
    mover.add(shot, DESCR, 9.81, reference, Vector3(0, 0, 500), start or reference, max_distance,
              attacker, Vector3(0, 2, -5))


def main():
    install_modules()
    ProjectileMover = load_mover().ProjectileMover
    host.setTime(100.0)
    state = host.state()
    expect(state['simulatorType'] == 'PyBallisticsSimulator' and state['motorType'] ==
           'PyProjectileMotor' and state['salvoType'] == 'PySalvo', 'type names')

    mover = ProjectileMover()
    state = host.state()
    expect([round(v, 3) for v in state['fixed']] == [0.05, 2.0, 180.0, 0.1], 'fixed params %r' % (state,))
    expect(type(mover.salvo).__name__ == 'PySalvo', 'unconditional PySalvo')
    mover.setSpaceID(42)
    expect(host.state()['spaceID'] == 42, 'setVariableBallisticsParams')
    PLAYER.inputHandler.onCameraChanged('sniper')
    expect(host.state()['autoScaleOff'] == 0, 'sniper camera keeps distance auto-scale')
    PLAYER.inputHandler.onCameraChanged('arcade')
    expect(host.state()['autoScaleOff'] == 1, 'arcade camera disables distance auto-scale')

    # Ordinary own shot: model, real motor, hidden until hidingTime, flies, stops at
    # the arena boundary, kills its effect, then deletes after timeAfterDeath.
    fire(mover, 1)
    projectiles = mover._ProjectileMover__projectiles
    expect(1 in projectiles and len(PLAYER.models) == 1, 'add registers one model')
    motor = projectiles[1]['motor']
    expect(type(motor).__name__ == 'PyProjectileMotor', 'returned native motor')
    expect(host.state()['sceneQueries'] > 0, 'prediction queried the bound scene')
    expect(('attach', 'flying', True) in LOG and ('flock', 0.0) in LOG, 'flying effect attached')
    first = advance(100.03)[0]
    expect(first['visible'] == 0 and first['transforms'] == 1, 'hidden before hidingTime %r' % first)
    flying = advance(100.2)[0]
    expect(flying['visible'] == 1 and 99.0 < flying['position'][2] < 101.0, 'flying %r' % flying)
    expect(abs(flying['forward'][2] - 1.0) < 1e-3, 'faces along velocity')
    del LOG[:]
    stopped = advance(100.7)[0]
    expect(LOG == [('detach', 'stopFlying')], 'kill callback retires flying effect')
    expect(stopped['visible'] == 0 and abs(stopped['position'][2] - 299.99) < 0.05, 'stops at boundary')
    advance(102.69)
    expect(1 in projectiles and len(PLAYER.models) == 1, 'tail retained until timeAfterDeath')
    advance(102.71)
    expect(1 not in projectiles and not PLAYER.models, 'delete retires model and motor')
    expect(('detachAll',) in LOG and ('trigger', 7) in LOG, 'own-shot miss trigger after delete')

    # Duplicate active shot: native refusal means no model is created.
    del LOG[:]
    fire(mover, 2)
    fire(mover, 2)
    expect(len(PLAYER.models) == 1 and ('attach', 'flying', True) in LOG, 'duplicate refused')

    # explode on an alive shot: None, so the explosion is deferred to the kill.
    del LOG[:]
    mover.explode(2, DESCR, 'ground', Vector3(0, 1, 150), Vector3(0, 0, 1))
    expect(projectiles[2]['showExplosion'] and not [e for e in LOG if e[0] == 'explosion'],
           'alive explode defers effect')
    later(0.2)
    later(0.2)
    expect([e for e in LOG if e[0] == 'explosion'] == [('explosion', 149.99, 'groundEffects', 11)],
           'explosion at the authoritative endpoint')
    # explode on a dead shot returns (position, direction): immediate effect.
    del LOG[:]
    fire(mover, 3)
    later(0.8)
    mover.explode(3, DESCR, 'ground', Vector3(0, 1, 250), Vector3(0, 0, 1))
    expect([e for e in LOG if e[0] == 'explosion'] == [('explosion', 299.99, 'groundEffects', 11)],
           'dead explode plays at native terminal %r' % LOG)

    # hide moves the shot to -id, which keeps flying to its endpoint, then retires.
    del LOG[:]
    fire(mover, 4, attacker=22)
    mover.hide(4, Vector3(0, 1, 120))
    expect(-4 in projectiles and 4 not in projectiles, 'hide renames')
    later(0.3)
    expect(('detach', 'stopFlying') in LOG, 'hidden shot terminal')
    later(2.1)
    expect(-4 not in projectiles and ('trigger', 7) not in LOG, 'hidden tail retired without miss')

    # hold (ricochet) delays a shot by hidingTime.
    fire(mover, 5)
    fire(mover, 6)
    mover.hold(5)
    start = CLOCK[0]
    CLOCK[0] = start + 0.2
    host.setTime(CLOCK[0])
    host.tick()
    held = projectiles[5]['model'].rev()
    free = projectiles[6]['model'].rev()
    expect(abs(free['position'][2] - held['position'][2] - 25.0) < 0.1, 'hold %r %r' % (held, free))

    # Distance auto-scale applies to attributed shots when not disabled.
    PLAYER.inputHandler.onCameraChanged('sniper')
    host.setCamera(0, 1, -260)
    CLOCK[0] += 0.1
    host.setTime(CLOCK[0])
    host.tick()
    scaled = projectiles[6]['model'].rev()
    expect(abs(scaled['scale'] - (scaled['position'][2] + 260.0) / 180.0) < 1e-3, 'scale %r' % scaled)
    PLAYER.inputHandler.onCameraChanged('arcade')

    # A boundary callback error is printed and treated as no boundary hit.
    printed = host.state()['printed']
    Arena.raise_next = True
    fire(mover, 7, max_distance=60.0)
    expect(7 in projectiles and host.state()['printed'] == printed + 1, 'boundary error contained')

    # Python-visible argument contract.
    simulator = mover._ProjectileMover__ballistics
    for call, text in (
            (lambda: simulator.addProjectile(1), 'addProjectile() expects 9 arguments of types int32_t, float, '
                                                  'Vector3, Vector3, Vector3, float, bool, Vector3, Vector3'),
            (lambda: simulator.hideProjectile('x', Vector3()), 'must be set to an int'),
            (lambda: simulator.explodeProjectile(1, 2), 'must be set to a Vector3'),
            (lambda: simulator.setVariableBallisticsParams(), 'setVariableBallisticsParams() expects 1 argument of type SpaceID'),
            (lambda: simulator.setFixedBallisticsParams(1, 2, 3), 'expects 4 arguments of types float, float, float, float'),
            (lambda: simulator.holdProjectile(), 'holdProjectile() expects 1 argument of type int32_t'),
            (lambda: simulator.setBallisticsAutoScale(None), 'must be set to a bool')):
        try:
            call()
        except TypeError as error:
            expect(text in str(error), 'error text %r' % str(error))
        else:
            raise AssertionError('no TypeError for ' + text)
    simulator = None
    import BigWorld
    expect(BigWorld.PySalvo(1) is None and BigWorld.PySalvo(1, 2) is None, 'short PySalvo is None')
    expect(BigWorld.PySalvo(-1, 0, 0) is None, 'bad PySalvo is None')
    expect(type(BigWorld.PySalvo(1000, 0, -100, 'extra')).__name__ == 'PySalvo', 'extra PySalvo args')
    try:
        BigWorld.PyBallisticsSimulator(None, None)
    except TypeError as error:
        expect('PyBallisticsSimulator() expects 3 arguments of types ScriptObject, ScriptObject, '
               'ScriptObject' in str(error), str(error))
    else:
        raise AssertionError('two-argument factory accepted')
    lonely = BigWorld.PyBallisticsSimulator(None, None, None)
    try:
        lonely.addProjectile(1, 9.81, Vector3(), Vector3(0, 0, 500), Vector3(), 1000.0, True,
                             Vector3(), Vector3())
    except TypeError as error:
        expect('boundary callback is not callable' in str(error), str(error))
    else:
        raise AssertionError('missing boundary callback accepted')
    expect(lonely.explodeProjectile(1, Vector3()) is None, 'unknown explode is None')
    lonely.hideProjectile(1, Vector3())
    lonely.holdProjectile(1)
    del lonely

    # destroy with live shots releases the simulator, then retires each model.
    kept = projectiles[6]['motor']
    mover.destroy()
    expect(not projectiles and not PLAYER.models and mover._ProjectileMover__ballistics is None,
           'destroy retires every live shot')
    expect(not PLAYER.inputHandler.onCameraChanged.handlers, 'camera listener removed')
    probe = host.model()
    probe.addMotor(kept)
    expect(probe.rev()['transforms'] == 0, 'motor outliving its simulator is inert')
    probe.delMotor(kept)
    del kept, probe, motor
    later(3.0)

    # A delete callback that destroys its own mover re-enters the simulator tick.
    original_delete = ProjectileMover._ProjectileMover__deleteProjectile

    def destroying_delete(self, shotID):
        original_delete(self, shotID)
        self.destroy()

    ProjectileMover._ProjectileMover__deleteProjectile = destroying_delete
    doomed = ProjectileMover()
    ProjectileMover._ProjectileMover__deleteProjectile = original_delete
    fire(doomed, 1)
    fire(doomed, 2)
    later(1.0)
    later(2.1)
    expect(not doomed._ProjectileMover__projectiles and not PLAYER.models, 'destroy inside delete')
    expect(doomed._ProjectileMover__ballistics is None, 'simulator released inside its own tick')
    later(3.0)

    # CanonicalProjectileMover-style override keeps stock calls and suppresses
    # only the duplicated hit notification.
    class Canonical(ProjectileMover):
        def _ProjectileMover__notifyProjectileHit(inner, position, projectile):
            if projectile.get('_offlineLANCanonical'):
                return
            return ProjectileMover._ProjectileMover__notifyProjectileHit(inner, position, projectile)

    canonical = Canonical()
    canonical.setSpaceID(3)
    del LOG[:]
    fire(canonical, 1)
    canonical._ProjectileMover__projectiles[1]['_offlineLANCanonical'] = True
    canonical.hide(1, Vector3(0, 1, 50))
    expect(not [e for e in LOG if e[0] == 'inputHit'], 'canonical suppresses duplicated hit')
    fire(canonical, 2, attacker=33)
    canonical.hide(2, Vector3(0, 1, 50))
    expect(('inputHit', 50.0, 85, False) in LOG, 'stock hit notification preserved')
    canonical.destroy()
    expect(not PLAYER.models, 'reset destroys canonical presenter')
    later(10.0)
    print('ProjectileMover.pyc over production binding: %d log events, %d deallocations' %
          (len(LOG), host.state()['deallocations']))


main()
