"""Exact #1513 producers/consumers against production battle animation types.

Runs inside the CPython 2.7 host. When STOCK_*_PYC variables name the exact
scripts.pkg members, their code objects execute unchanged with fixture
globals; otherwise only the direct native contract is exercised.
"""
import marshal
import math
import os
import types

import Host
import Svarog
import Vehicular

BUILTINS = __builtins__


def expect(kind, function, *args):
    try:
        function(*args)
    except kind:
        return
    raise AssertionError('%s did not raise %s' % (function, kind.__name__))


def find(code, name):
    if code.co_name == name:
        return code
    for child in code.co_consts:
        if isinstance(child, types.CodeType):
            found = find(child, name)
            if found is not None:
                return found
    return None


def load(variable, name, globals_):
    path = os.environ.get(variable)
    if not path:
        return None
    code = find(marshal.loads(open(path, 'rb').read()[8:]), name)
    assert code is not None, name
    scope = {'__builtins__': BUILTINS}
    scope.update(globals_)
    return types.FunctionType(code, scope)


def translated(x, y, z):
    return (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., x, y, z, 1.)


def close(first, second, tolerance=1e-5):
    return all(abs(a - b) <= tolerance for a, b in zip(first, second))


class Struct(object):
    def __init__(self, **values):
        self.__dict__.update(values)


class Lod(object):
    lodDistance = 10.


class TankPartNames(object):
    HULL = 'hull'


class MathUtils(object):
    @staticmethod
    def createIdentityMatrix():
        return Host.Matrix()


LOD = Lod()
NODES = [('', translated(0, 0, 0)), ('hull', translated(0, .8, .1)), ('Gun', translated(0, 0, 2)),
         ('static_joint', translated(0, .3, 1)), ('slave_a', translated(.5, 0, 0)), ('slave_b', translated(-.5, 0, 0)),
         ('slave_c', translated(0, .5, 0))]


def swinging_descriptor(pitch=(0.26, 36.0, 0.42, 1.0, 0.05, 0.32), roll=(1.0, 0.26, 36.0, 0.42, 1.0, 0.05, 0.32)):
    swinging = Struct(pitchParams=pitch, rollParams=roll, sensitivityToImpulse=0.4, lodDist=100.)
    return Struct(hull=Struct(swinging=swinging), physics={'speedLimits': (20., 10.)})


def check_swinging_producer():
    compound = Host.Compound(NODES)
    create = load('STOCK_MODEL_ASSEMBLER_PYC', 'createSwingingAnimator',
                  {'Vehicular': Vehicular, 'mathUtils': MathUtils})
    assemble = load('STOCK_VEHICLE_ASSEMBLER_PYC', '_assembleSwinging',
                    {'createSwingingAnimator': create, 'TankPartNames': TankPartNames})
    link = Host.floatLink(LOD, 'lodDistance')
    if create is not None:
        # The producer multiplies pitchParams once; native stores them as given.
        animator = create(swinging_descriptor(), compound.node('hull').localMatrix, None, link)
        assert Host.output(animator) == compound.local('hull')
        assert isinstance(animator.worldMatrix, type(Host.Matrix()))
        assert animator.maxMovementSpeed == 20. and animator.lodSetting == 100.
        compensation = Host.Matrix(translated(0, -.5, 0))
        filter_ = Struct(placingCompensationMatrix=compensation)
        appearance = Struct(compoundModel=compound, typeDescriptor=swinging_descriptor(), filter=filter_)
        assemble(appearance, link)
        animator = appearance.swingingAnimator
        assert animator.placingCompensationMatrix is compensation
        assert animator.worldMatrix is compound.matrix
    else:
        animator = Vehicular.SwingingAnimator()
        animator.basisMatrix = compound.node('hull').localMatrix
        animator.setupPitchSwinging(*[p * k for p, k in zip((0.26, 36., .42, 1., .05, .32), (.9, 1.88, .3, 4., 1., 1.))])
        animator.setupRollSwinging(1.0, 0.26, 36.0, 0.42, 1.0, 0.05, 0.32)
        animator.setupShotSwinging(0.4)
        animator.maxMovementSpeed, animator.lodSetting = 20., 100.
        animator.worldMatrix, animator.lodLink = compound.matrix, link
        compound.node('hull', animator)
    return compound, animator


def check_swinging_provider(compound, animator):
    # The animator is a MatrixProvider ticked by ProviderStore, not Svarog.
    assert Host.providerCount() >= 1
    hull = compound.local('hull')
    Host.tickProviders(1 / 60.)
    compound.evaluate()
    base = compound.local('hull')
    animator.receiveShotImpulse((1., 0., 0.), 2000.)
    for unused in range(5):
        Host.tickProviders(1 / 60.)
    compound.evaluate()
    swung = compound.local('hull')
    assert not close(base, swung, 1e-7), (base, swung)
    assert close(swung[12:15], hull[12:15], .6)
    # A live provider rebind is followed on the next tick (pinned fork path).
    moved = Host.Matrix(translated(5., 0., 3.))
    animator.worldMatrix = moved
    animator.placingCompensationMatrix = None
    assert animator.worldMatrix is moved and animator.placingCompensationMatrix is None
    Host.tickProviders(1 / 60.)
    # Retired-stock acceleration window is writable and decays with time.
    animator.accelSwingingPeriod, animator.accelSwingingDirection = 2., 1.
    Host.tickProviders(.5)
    assert abs(animator.accelSwingingPeriod - 1.5) < 1e-6 and animator.accelSwingingDirection == 1.
    # CompoundAppearance.stopSwinging writes the period to zero.
    stop = load('STOCK_COMPOUND_APPEARANCE_PYC', 'stopSwinging', {})
    if stop is not None:
        stop(Struct(swingingAnimator=animator))
        assert animator.accelSwingingPeriod == 0.
    # Original 0077f7dc requires a lodLink; the port contains it.
    animator.lodLink = None
    output = Host.output(animator)
    Host.tickProviders(1 / 60.)
    assert Host.output(animator) == output and Host.errors() == ['SwingingAnimator has no valid float LOD link\n']
    Host.tickProviders(1 / 60.)
    assert Host.errors() == []
    animator.lodLink = Host.floatLink(LOD, 'lodDistance')
    expect(TypeError, setattr, animator, 'lodLink', 3.)
    expect(TypeError, setattr, animator, 'worldMatrix', 'hull')
    expect(TypeError, animator.setupPitchSwinging, 1., 2.)
    expect(TypeError, animator.receiveShotImpulse, (1., 0.), 1.)


def check_receive_shot_impulse(animator):
    receive = load('STOCK_COMPOUND_APPEARANCE_PYC', 'receiveShotImpulse', {
        'BattleReplay': Struct(isPlaying=lambda: False), 'VehicleDamageState': Struct(isDamagedModel=lambda state: False)})
    if receive is None:
        return
    appearance = Struct(swingingAnimator=animator, _CompoundAppearance__currentDamageState=Struct(modelState=0),
                        _CompoundAppearance__crashedTracksCtrl=None)
    before = Host.output(animator)
    receive(appearance, (0., 0., 1.), 5000.)
    Host.tickProviders(1 / 30.)
    assert Host.output(animator) != before


def check_swinging_lifecycle(compound, animator):
    system = Svarog.ComponentSystem()
    system.addComponent(animator, 'swingingAnimator')
    system.activate()
    system.deactivate()
    # Removing a pure-script component reclaims it and destroys the native owner.
    assert system.removeComponent(animator) is animator
    output = Host.output(animator)
    expect(ValueError, getattr, animator, 'lodSetting')
    expect(ValueError, animator.receiveShotImpulse, (0., 0., 1.), 1.)
    Host.tickProviders(1 / 60.)
    compound.evaluate()
    # A compound that still binds the destroyed provider keeps its last pose.
    assert Host.output(animator) == output and compound.local('hull') == output
    animator.destroy()
    animator.activate()


def check_linked_nodes():
    compound = Host.Compound(NODES)
    joints = [[['Gun', math.radians(-14.95), 0., math.radians(7.)],
               ['static_joint', math.radians(-4.044), math.radians(.43), math.radians(.43)]],
              [['Gun', -.2, .2], ['slave_a', -.1, .1], ['slave_b', .3, -.3]]]
    descriptor = Struct(gun=Struct(drivenJoints=joints))
    assemble = load('STOCK_MODEL_ASSEMBLER_PYC', 'assembleGunLinkedNodesAnimator', {'Vehicular': Vehicular})
    appearance = Struct(typeDescriptor=descriptor, compoundModel=compound)
    if assemble is not None:
        assemble(appearance)
        animator = appearance.gunLinkedNodesAnimator
        # drivenJoints is tested with `is not None`, so an empty list still builds.
        empty = Struct(typeDescriptor=Struct(gun=Struct(drivenJoints=[])), compoundModel=compound)
        assemble(empty)
        assert empty.gunLinkedNodesAnimator is not None
        absent = Struct(typeDescriptor=Struct(gun=Struct(drivenJoints=None)), compoundModel=compound)
        assemble(absent)
        assert not hasattr(absent, 'gunLinkedNodesAnimator')
    else:
        animator = Vehicular.LinkedNodesPitchAnimator(compound, joints)
    authored = compound.local('static_joint')
    Host.tickLinked()
    assert compound.writes('static_joint') == 0  # inactive until the system activates it
    system = Svarog.ComponentSystem()
    system.addComponent(animator, 'gunLinkedNodesAnimator')
    system.activate()
    # The gun owner pitches the master node; slaves follow the authored table.
    pitch = math.radians(3.)
    compound.node('Gun', Host.Matrix((1., 0., 0., 0., 0., math.cos(pitch), math.sin(pitch), 0.,
                                      0., -math.sin(pitch), math.cos(pitch), 0., 0., 0., 2., 1.)))
    compound.evaluate()
    Host.tickLinked()
    assert compound.writes('static_joint') == 1 and compound.writes('slave_a') == 1
    assert compound.writes('Gun') == 0
    joint = compound.local('static_joint')
    assert joint[12:15] == authored[12:15] and joint != authored
    Host.tickLinked()
    assert compound.writes('static_joint') == 2
    expect(TypeError, Vehicular.LinkedNodesPitchAnimator, compound, [[['Gun', 'x']]])
    expect(TypeError, Vehicular.LinkedNodesPitchAnimator, compound, (('Gun', 0., 1.),))
    # 0076d0c0 keeps at most five list groups and skips non-list groups.
    capped = [[['Gun', 0., 1.], ['slave_c', 0., 1.]] if index == 5 else
              ((('Gun', 0., 1.), ('slave_c', 0., 1.)) if index == 0 else [['Gun', 0., 1.]]) for index in range(6)]
    extra = Vehicular.LinkedNodesPitchAnimator(compound, capped)
    extra_system = Svarog.ComponentSystem()
    extra_system.addComponent(extra, 'extra')
    extra_system.activate()
    Host.tickLinked()
    assert compound.writes('slave_c') == 0
    extra_system.destroy()
    expect(TypeError, Vehicular.LinkedNodesPitchAnimator, 'compound', joints)
    missing = Vehicular.LinkedNodesPitchAnimator(compound, [[['missing', 0., 1.], ['slave_a', 0., 1.]]])
    assert Host.errors() == ['LinkedNodesPitchAnimator::linkNodes Cannot find node: missing in compound: HostCompound\n']
    del missing
    system.deactivate()
    written = compound.writes('static_joint')
    Host.tickLinked()
    assert compound.writes('static_joint') == written
    system.activate()
    compound.reset()
    Host.tickLinked()
    assert Host.errors() == ['LinkedNodesPitchAnimator lost its compound node Gun\n']
    system.destroy()
    expect(ValueError, system.activate)


def check_suspension_sound():
    hull_aiming = Struct(angleLimitValue=math.radians(5.), lodDist=50., sounds=[
        Struct(state=state, underLimitSounds=Struct(PC='%s_small_PC' % state, NPC='%s_small_NPC' % state),
               overLimitSounds=Struct(PC='%s_high_PC' % state, NPC='%s_high_NPC' % state))
        for state in ('upStart', 'upStop', 'downStart', 'downStop')])
    body, ground = Host.Matrix(), Host.Matrix()
    filter_ = Struct(groundPlacingMatrix=ground, bodyMatrix=body)

    def appearance(compound, player=True, wwise=True, siege=True):
        descriptor = Struct(hasSiegeMode=siege, siegeVehicleDescr=Struct(chassis=Struct(hullAimingSound=hull_aiming)))
        return Struct(id=42 if player else 43, typeDescriptor=descriptor, compoundModel=compound, filter=filter_)

    assemble = load('STOCK_MODEL_ASSEMBLER_PYC', 'assembleSuspensionSound', {
        'WWISE': Struct(WW_isInitialised=lambda: True), 'TankPartNames': TankPartNames, 'Vehicular': Vehicular})
    link = Host.floatLink(LOD, 'lodDistance')
    compound = Host.Compound(NODES)
    if assemble is not None:
        for player in (True, False):
            subject = appearance(compound, player)
            assemble(subject, link, player)
            sound = subject.suspensionSound
            assert sound.vehicleMatrix is ground and sound.bodyMatrix is body and sound.lodSetting == 50.
            assert abs(sound.angleLimitValue - math.radians(5.)) < 1e-7
            Host.audio()
        silent = appearance(compound)
        load('STOCK_MODEL_ASSEMBLER_PYC', 'assembleSuspensionSound', {
            'WWISE': Struct(WW_isInitialised=lambda: False), 'TankPartNames': TankPartNames,
            'Vehicular': Vehicular})(silent, link, True)
        assert not hasattr(silent, 'suspensionSound')
        subject = appearance(compound, True)
        assemble(subject, link, True)
        sound = subject.suspensionSound
    else:
        sound = Vehicular.SuspensionSound(42)
        for item in hull_aiming.sounds:
            sound.setSoundsForState(item.state, item.underLimitSounds.PC, item.overLimitSounds.PC)
        sound.bodyMatrix, sound.vehicleMatrix = body, ground
        sound.lodLink, sound.lodSetting, sound.angleLimitValue = link, 50., math.radians(5.)
    assert Host.audio()[0] == 'create suspensionSound_42'
    system = Svarog.ComponentSystem()
    system.addComponent(sound, 'suspensionSound')
    system.activate()
    change = load('STOCK_COMPOUND_APPEARANCE_PYC', 'onSiegeStateChanged', {})
    owner = Struct(engineAudition=None, suspensionController=None, suspensionSound=sound,
                   _CompoundAppearance__suspensionSound=sound, siegeEffects=None)

    def siege(state):
        if change is not None:
            change(owner, state)
        else:
            sound.vehicleState = state

    Host.tickSounds(.05)
    assert Host.audio() == []  # only vehicleState 2 animates
    siege(2)
    assert sound.vehicleState == 2
    for angle in (.0, .02, .04, .06, .2, .3, .3, .3, .3):
        ground.value = (1., 0., 0., 0., 0., math.cos(angle), math.sin(angle), 0.,
                        0., -math.sin(angle), math.cos(angle), 0., 0., 0., 0., 1.)
        Host.tickSounds(.05)
    for unused in range(30):
        Host.tickSounds(.05)
    events = Host.audio()
    assert all(event.startswith('play suspensionSound_42 ') for event in events), events
    assert any('Start_small_PC' in event for event in events), events
    assert any('Start_high_PC' in event for event in events), events
    assert 'Stop_' in events[-1], events
    LOD.lodDistance = 60.
    ground.value = Host.Matrix().value
    Host.tickSounds(.05)
    assert Host.audio() == []  # outside lodSetting
    LOD.lodDistance = 10.
    siege(0)
    assert Host.audio()[-1] == 'stop suspensionSound_42'
    expect(Exception, setattr, sound, 'vehicleState', 70000)
    sound.lodLink = None
    siege(2)
    Host.tickSounds(.05)
    assert Host.errors() == ['SuspensionSound has no valid float LOD link\n']
    # Damaged conversion drops the descriptor: remove, destroy, never replay.
    assert system.removeComponent('suspensionSound') is not None
    assert Host.audio()[-2:] == ['stop suspensionSound_42', 'matrix suspensionSound_42 none']
    Host.tickSounds(.05)
    assert Host.audio() == []
    expect(ValueError, getattr, sound, 'vehicleState')


def rotation_x(angle, x, y, z):
    c, s_ = math.cos(angle), math.sin(angle)
    return (1., 0., 0., 0., 0., c, s_, 0., 0., -s_, c, 0., x, y, z, 1.)


def levered_compound():
    nodes = [('', translated(0, 0, 0)), ('hull', translated(0, .9, 0), '')]
    for side, x in (('L', -1.4), ('R', 1.4)):
        for index, z in enumerate((2., 1., -1., -2.)):
            suffix = '%s%d' % (side, index)
            nodes += [('Lever_Start_' + suffix, translated(x, -.2, z), 'hull'),
                      ('Lever_Joint_' + suffix, rotation_x(.3, 0, -.4, -.3), 'Lever_Start_' + suffix),
                      ('Track_' + suffix, translated(0, -.3, 0), 'Lever_Joint_' + suffix),
                      ('W_' + suffix, translated(0, 0, 0), 'Track_' + suffix)]
    return Host.Compound(nodes)


def check_levered_suspension():
    import collections
    Lever = collections.namedtuple('SuspensionLever', 'startNodeName jointNodeName trackNodeName minAngle maxAngle')
    levers = [Lever('Lever_Start_%s%d' % (side, index), 'Lever_Joint_%s%d' % (side, index),
                    'Track_%s%d' % (side, index), math.radians(lo), math.radians(hi))
              for side in 'LR' for index, (lo, hi) in enumerate(((-42., 62.), (186.5, 155.), (-6.5, 25.), (222., 118.)))]
    config = Struct(levers=levers, interpolationSpeedMul=10., lodSettings=(100., None))
    compound = levered_compound()
    lod = Struct(distance=10., priority=1)
    create = load('STOCK_MODEL_ASSEMBLER_PYC', 'createLeveredSuspension', {'Vehicular': Vehicular})
    if create is not None:
        assert create(compound, Struct(chassis=Struct(leveredSuspension=None)), Host.lodLink(lod)) is None
        suspension = create(compound, Struct(chassis=Struct(leveredSuspension=config)), Host.lodLink(lod))
    else:
        suspension = Vehicular.LeveredSuspension(compound, levers, 10.)
        suspension.setupLodLink(Host.lodLink(lod))
        suspension.setupLodSettings((100., None))
    post = load('STOCK_VEHICLE_ASSEMBLER_PYC', '__postSetupFilter', {})
    if post is not None:
        # A non-None levered owner replaces the filter's ground placement.
        for owner, expected in ((suspension, False), (None, True)):
            appearance = Struct(suspension=None, leveredSuspension=owner, filter=Struct())
            post(appearance)
            assert appearance.filter.placingOnGround is expected
    expect(TypeError, Vehicular.LeveredSuspension, compound, [('a', 'b')], 10.)
    expect(TypeError, Vehicular.LeveredSuspension, compound, 'levers', 10.)
    expect(ValueError, Vehicular.LeveredSuspension, compound, [Lever('missing', 'b', 'c', 0., 1.)], 10.)
    expect(ValueError, Vehicular.LeveredSuspension, compound, [], 10.)
    expect(TypeError, suspension.setupLodSettings, (1., 2, 3))
    expect(TypeError, suspension.setupLodLink, 3.)
    authored = dict((name, compound.local(name)) for name in ('Lever_Start_L0', 'Track_L0', 'W_L0'))
    Host.tickLevered(1 / 60.)
    assert compound.writes('Track_L0') == 0  # inactive until the system activates it
    system = Svarog.ComponentSystem()
    system.addComponent(suspension, 'leveredSuspension')
    system.activate()
    Host.setGround(-1.2, True)
    prepares = Host.prepares()
    Host.tickLevered(1 / 60.)
    assert Host.prepares() == prepares + 1
    assert compound.writes('Lever_Start_L0') == 1 and compound.writes('Track_L0') == 1
    assert compound.writes('Lever_Joint_L0') == 0 and compound.writes('W_L0') == 0
    start = compound.local('Lever_Start_L0')
    assert start[12:15] == authored['Lever_Start_L0'][12:15] and start != authored['Lever_Start_L0']
    # A separate wheel writer owns W_L0; both contributions survive one evaluate.
    spin = Host.Matrix(rotation_x(1.1, 0, 0, 0))
    compound.node('W_L0', spin)
    compound.evaluate()
    Host.tickLevered(1 / 60.)
    compound.evaluate()
    assert compound.local('W_L0') == spin.value and compound.local('Track_L0') != authored['Track_L0']
    wheel_world = compound.world('W_L0')
    track_world = compound.world('Track_L0')
    assert close(wheel_world[12:15], track_world[12:15], 1e-5)
    # Leaving LOD restores the authored lever locals exactly once.
    lod.distance = 500.
    restored = compound.writes('Track_L0')
    Host.tickLevered(1 / 60.)
    assert compound.local('Track_L0') == authored['Track_L0'] and compound.writes('Track_L0') == restored + 1
    Host.tickLevered(1 / 60.)
    assert compound.writes('Track_L0') == restored + 1
    lod.distance = 10.
    # First prove the retained provider can hit this ground, then remove its
    # camera without destroying the previous space. No stale-space ray may run.
    Host.setGround(0., True)
    hit_count = Host.rayHits()
    Host.tickLevered(1 / 60.)
    assert Host.rayHits() > hit_count
    query_count = Host.rayQueries()
    prepare_count = Host.prepares()
    Host.setGround(0., False)
    Host.tickLevered(1 / 60.)
    assert Host.errors() == ['LeveredSuspension cannot prepare its ground-query space\n']
    assert Host.prepares() == prepare_count and Host.rayQueries() == query_count
    # A camera with a failed prepare has the same no-query outcome.
    Host.setGround(0., True, False)
    Host.tickLevered(1 / 60.)
    assert Host.errors() == ['LeveredSuspension cannot prepare its ground-query space\n']
    assert Host.prepares() == prepare_count + 1 and Host.rayQueries() == query_count
    # A later valid prepare resumes queries on the same component.
    Host.setGround(0., True)
    hit_count = Host.rayHits()
    Host.tickLevered(1 / 60.)
    assert Host.rayQueries() > query_count and Host.rayHits() > hit_count
    Host.setGround(-1.2, True)
    system.deactivate()
    written = compound.writes('Track_L0')
    Host.tickLevered(1 / 60.)
    assert compound.writes('Track_L0') == written
    system.activate()
    compound.reset()
    Host.tickLevered(1 / 60.)
    assert Host.errors() == ['LeveredSuspension lost its compound nodes\n']
    system.destroy()
    expect(ValueError, suspension.setupLodSettings, (1., 2))


compound, animator = check_swinging_producer()
check_swinging_provider(compound, animator)
check_receive_shot_impulse(animator)
check_swinging_lifecycle(compound, animator)
count = Host.providerCount()
del animator
compound.node('hull', Host.Matrix())
assert Host.providerCount() == count - 1
check_linked_nodes()
check_suspension_sound()
check_levered_suspension()
assert Host.errors() == [], Host.errors()
