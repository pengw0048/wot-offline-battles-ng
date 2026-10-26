"""Real-client diagnostic for detached filter types and Python ownership.

Run only in an isolated personality overlay. Attached motion, scene collision,
physics configuration and gameplay remain separate acceptance boundaries.
"""
from __future__ import print_function
import gc
import json
import os
import sys
import traceback
import weakref

OUTPUT = os.path.join(os.getcwd(), 'entity-filters-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False, 'attached_motion_tested': False}


def save():
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def invalid(call, expected=TypeError):
    try:
        call()
    except expected:
        return
    raise AssertionError('Invalid filter operation was accepted')


def checks():
    import BigWorld
    import Math
    base = BigWorld.WGEntityFilter()
    vehicle = BigWorld.WGVehicleFilter()
    assert isinstance(vehicle, type(base))
    assert type(vehicle).__base__ is type(base)
    assert base.isLaggingNow and vehicle.isLaggingNow
    for factory in (BigWorld.WGEntityFilter, BigWorld.WGVehicleFilter):
        invalid(lambda: factory(1))
    for owner in (base, vehicle):
        assert not owner.ignoreInputs and not owner.ignoreNextReset
        assert owner.maxTimeBetweenInputs == 2
        owner.ignoreInputs = True
        owner.ignoreNextReset = True
        owner.maxTimeBetweenInputs = 0.125
        owner.maxMove = 13.5
        assert owner.ignoreInputs and owner.ignoreNextReset
        assert owner.maxTimeBetweenInputs == 0.125 and owner.maxMove == 13.5
        assert not owner.segmentMayHitEntity((0, 0, 0), (1, 2, 3), 0)
        invalid(lambda owner=owner: owner.segmentMayHitEntity((0, 0, 0), (1, 2, 3)))
        invalid(lambda owner=owner: owner.latestInput, ValueError)
    REPORT['checks'].append('actual factories, base inheritance, detached lag/settings and argument guards')

    names = ('canExtrapolateOnLag', 'allowLagCollisions', 'enableClientFilters', 'allowCustomResets')
    previous = dict((name, getattr(base, name)) for name in names)
    default_time = base.defaultMaxTimeBetweenInputs
    try:
        for name in names:
            assert previous[name] is True
            setattr(base, name, False)
            assert getattr(vehicle, name) is False
            setattr(vehicle, name, True)
            assert getattr(base, name) is True
        base.defaultMaxTimeBetweenInputs = 0.75
        assert vehicle.defaultMaxTimeBetweenInputs == 0.75
        fresh = BigWorld.WGEntityFilter()
        assert fresh.maxTimeBetweenInputs == 0.75
        assert base.maxTimeBetweenInputs == 0.125
    finally:
        for name, value in previous.items():
            setattr(base, name, value)
        base.defaultMaxTimeBetweenInputs = default_time
    REPORT['checks'].append('shared original global controls and per-owner default snapshot')

    owner = BigWorld.WGEntityFilter()
    identity = weakref.ref(owner)
    proxy = owner.movementExtrapolationData
    for name in ('accumLinearVelocity', 'accumAngularVelocity',
                 'accumLinearAcceleration', 'accumAngularAcceleration'):
        invalid(lambda proxy=proxy, name=name: getattr(proxy, name), ValueError)
        invalid(lambda proxy=proxy, name=name: setattr(proxy, name, (1, 2, 3)), ValueError)
    del owner
    gc.collect()
    assert identity() is not None
    del proxy
    gc.collect()
    assert identity() is None
    REPORT['checks'].append('actual extrapolation proxy strong owner and detached failure containment')

    source = BigWorld.WGEntityFilter()
    identity = weakref.ref(source)
    target = BigWorld.WGVehicleFilter()
    invalid(lambda target=target, source=source: target.transferInput(source))
    invalid(lambda target=target: target.transferInput(object(), None))
    invalid(lambda target=target, source=source: target.transferInput(source, object()))
    target.transferInput(source, None)
    target.transferInputAsVehicle(source, Math.Matrix())
    del source
    gc.collect()
    assert identity() is not None
    del target
    gc.collect()
    assert identity() is None
    assert vehicle.clearTriangles() is None
    invalid(lambda: vehicle.clearTriangles(1))
    invalid(lambda: vehicle.groundDeformationRate, ValueError)
    REPORT['checks'].append('pending transfer references, nullable/typed providers and original empty triangle method')

    class Callback(object):
        pass
    callback = Callback()
    identity = weakref.ref(callback)
    owner = BigWorld.WGEntityFilter()
    owner.setScriptInputCallback(callback)
    del callback
    gc.collect()
    assert identity() is not None
    owner.setScriptInputCallback(None)
    gc.collect()
    assert identity() is None
    identity = weakref.ref(owner)
    owner.enableLagDetection(True)
    del owner
    gc.collect()
    assert identity() is None
    REPORT['checks'].append('arbitrary callback ownership, None replacement and weak lag manager registration')


def init(*args):
    REPORT['lifecycle'].append('init')
    try:
        checks()
        REPORT['status'] = 'passed'
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    save()
    print('ENTITY_FILTERS_RUNTIME_' + REPORT['status'].upper())
    BigWorld.quit()


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')


def onChangeEnvironments(environment):
    REPORT['lifecycle'].append('onChangeEnvironments')
    REPORT['environment'] = environment


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
