"""Run unchanged #1513 PYC against the production accessors in CPython 2.7."""
import marshal
import os
import types
import Host
import Vehicular


def code(variable, name):
    root = marshal.loads(open(os.environ[variable], 'rb').read()[8:])
    def find(c):
        if c.co_name == name:
            return c
        for child in c.co_consts:
            if isinstance(child, types.CodeType):
                match = find(child)
                if match is not None:
                    return match
    return find(root)


def expect(kind, action, *args):
    try:
        action(*args)
    except kind:
        return
    raise AssertionError('expected ' + kind.__name__)


class Record(object):
    def __init__(self, **values):
        self.__dict__.update(values)


post = types.FunctionType(code('STOCK_VEHICLE_ASSEMBLER', '__postSetupFilter'), {'__builtins__': __builtins__})
for count in (0, 1, 4, 12):
    owner = Host.suspension(count)
    assert owner.hasGroundNodes is bool(count)
    for levered in (None, object()):
        appearance = Record(suspension=owner, leveredSuspension=levered, filter=Record())
        post(appearance)
        assert appearance.filter.placingOnGround is (not (count or levered is not None))
    Host.setNodes(owner, 0)
    assert owner.hasGroundNodes is False
    Host.setNodes(owner, 3)
    assert owner.hasGroundNodes is True
    expect(TypeError, setattr, owner, 'hasGroundNodes', True)
    owner.destroy()
    owner.destroy()
    expect(ValueError, getattr, owner, 'hasGroundNodes')
# The original caller fails without the newly restored property.
expect(AttributeError, post, Record(suspension=Record(), leveredSuspension=None, filter=Record()))


class Event(object):
    def __iadd__(self, callback):
        return self


def recorded(*args):
    events.append(args)


class Base(object):
    def activate(self):
        recorded('base')


class CompoundAppearance(Base):
    pass


events = []
player = Record(handleVehicleCollidedVehicle=recorded,
                arena=Record(onPeriodChange=Event()),
                inputHandler=Record(onCameraChanged=Event()))
world = Record(player=lambda: player, callback=lambda *args: 9)
activate = types.FunctionType(code('STOCK_COMPOUNDAPPEARANCE', 'activate'), {
    '__builtins__': __builtins__,
    'super': super, 'CompoundAppearance': CompoundAppearance,
    'BigWorld': world, 'DataLinks': Record(linkMatrixTranslation=Host.link),
    '_PERIODIC_TIME': 1.})


def appearance(lod, position):
    a = CompoundAppearance()
    f = Record(enableStabilisedMatrix=recorded, enableLagDetection=recorded)
    a._CompoundAppearance__activated = False
    a._CompoundAppearance__vehicle = Record(isPlayerVehicle=False, isStrafing=False, filter=f)
    a._CompoundAppearance__typeDesc = Record(type=Record(tags=[]))
    a._CompoundAppearance__filter = f
    a.filter = f
    a._CompoundAppearance__compoundModel = Record(matrix=position)
    for name in ('linkCompound', 'createAndAttachStickers', 'startSystems',
                 'onPeriodicTimer', 'arenaPeriodChanged', 'onCameraChanged'):
        setattr(a, '_CompoundAppearance__' + name, recorded)
    a._CompoundAppearance__chassisDecal = Record(attach=recorded)
    a._CompoundAppearance__currentDamageState = Record(isCurrentModelDamaged=False)
    a._CompoundAppearance__periodicTimerID = None
    a.setupGunMatrixTargets = recorded
    a.onModelChanged = recorded
    a.lodCalculator = lod
    a.detailedEngineState = None
    a.fashion = None
    return a


# No retained owner from the first battle may stop rebinding in the second.
for battle in range(2):
    old = [3., 0., 4.]
    current = [0., 0., 20.]
    lod = Vehicular.LodCalculator(Host.link(old), False, 1, False)
    lod.activate()
    Host.tick()
    assert lod.lodDistance == 5.
    expect(TypeError, lod.setupPosition)
    expect(TypeError, lod.setupPosition, 1, 2)
    expect(TypeError, lod.setupPosition, object())
    Host.tick()
    assert lod.lodDistance == 5.
    activate(appearance(lod, current))
    Host.tick()
    assert lod.lodDistance == 20.
    current[2] = 30.
    old[0] = 1000.
    Host.tick()
    assert lod.lodDistance == 30.
    # A callable is another legal Vector3 DataLink producer in #1513.
    result = [0., 0., 7.]
    assert lod.setupPosition(lambda: tuple(result)) is None
    Host.tick()
    assert lod.lodDistance == 7.
    result[:] = [0., 0., 8.]
    Host.tick()
    assert lod.lodDistance == 8.
    result[:] = ['invalid', 0., 0.]
    Host.tick()
    assert lod.lodDistance == 8.
    result[:] = [6., 'invalid', 0.]
    Host.tick()
    assert lod.lodDistance == 10.  # PyArg_ParseTuple preserves the parsed x.
    assert lod.setupPosition(None) is None
    before = Host.errorCount()
    Host.tick()
    assert Host.errorCount() == before + 1 and lod.lodDistance == 10.
    lod.setupPosition(Host.link([0., 0., 9.]))
    Host.tick()
    assert lod.lodDistance == 9.
    # A producer may synchronously replace itself. The old sampled position
    # must not overwrite the new binding's state or its previous LOD result.
    def replace():
        lod.setupPosition(Host.link([0., 0., 11.]))
        return (0., 0., 1000.)
    lod.setupPosition(replace)
    Host.tick()
    assert lod.lodDistance == 9.
    Host.tick()
    assert lod.lodDistance == 11.
    def destroy():
        lod.destroy()
        return (0., 0., 1000.)
    lod.setupPosition(destroy)
    Host.tick()
    expect(ValueError, getattr, lod, 'lodDistance')
    lod.destroy()
    lod.destroy()
    expect(ValueError, lod.setupPosition, Host.link([0., 0., 1.]))
# Missing-method negative control uses the exact original activate body.
expect(AttributeError, activate, appearance(Record(), [0., 0., 1.]))
print('Original #1513 suspension post-setup/appearance activation, live LOD rebinding and second owner passed')
