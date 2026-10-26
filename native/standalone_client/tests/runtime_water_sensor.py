"""Exercise real WaterSensor ownership and frame updates in an empty space.

This personality does not prove authored-water collision or rendered splashes.
"""
from __future__ import print_function
import gc
import json
import os
import sys
import traceback
import weakref

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'water-sensor-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False, 'authored_water_tested': False,
          'rendered_splashes_tested': False}
STATE = {}


def save():
    assert os.path.basename(ROOT).startswith('water-sensor-'), ROOT
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def invalid(callback, kind=TypeError):
    try:
        callback()
    except kind:
        return
    raise AssertionError('Invalid operation was accepted')


def inspect_owner():
    import DataLinks
    import Math
    import Svarog
    import Vehicular
    config = ((0., 1.5, 0.), (1.5, 3.), 3., 4., .5)
    invalid(lambda: Vehicular.WaterSensor())
    invalid(lambda: Vehicular.WaterSensor(config[:4]))
    sensor = Vehicular.WaterSensor(config)
    assert sensor.isOwning
    assert sensor.waterHeight == 0.
    assert not sensor.isInWater and not sensor.isUnderWater and not sensor.isSplash
    invalid(lambda: setattr(sensor, 'isInWater', True))
    invalid(lambda: sensor.setLodSettings(()))
    sensor.setLodSettings((150., 15))
    sensor.setLodLink(None)
    plane = Math.Matrix()
    plane.setTranslate((10., 2., 5.))
    sensor.sensorPlaneLink = plane
    assert tuple(sensor.sensorPlaneLink().translation) == (10., 2., 5.)
    retained = sensor.sensorPlaneLink
    changed = Math.Matrix()
    changed.setTranslate((-4., 1., 3.))
    sensor.sensorPlaneLink = changed
    assert tuple(retained().translation) == (10., 2., 5.)
    assert tuple(sensor.sensorPlaneLink().translation) == (-4., 1., 3.)
    sensor.speedLink = lambda: 5.

    class Callback(object):
        def __call__(self, *args):
            REPORT.setdefault('unexpected_events', []).append(repr(args))
    callback = Callback()
    reference = weakref.ref(callback)
    sensor.onWaterSplash = callback
    sensor.onUnderWaterSwitch = callback
    del callback
    gc.collect()
    assert reference() is not None
    links = [DataLinks.createBoolLink(sensor, name)
             for name in ('isInWater', 'isUnderWater', 'isSplash')]
    assert all(link() is False for link in links)
    assert DataLinks.createBoolLink(sensor, 'waterHeight') is None
    system = Svarog.ComponentSystem()
    system.addComponent(sensor, 'waterSensor')
    assert not sensor.isOwning
    view = system.findComponent('waterSensor')
    assert not view.isOwning
    STATE.update(system=system, view=view, links=links, callback=reference)
    system.activate()
    REPORT['checks'].append('actual factory, matrix links, event ownership and component transfer')


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    try:
        inspect_owner()
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()


def init(*args):
    REPORT['lifecycle'].append('init')
    save()
    return True


def inspect_transition():
    import BigWorld
    try:
        if REPORT['status'] == 'failed':
            BigWorld.quit()
            return
        view = STATE['view']
        if view.waterHeight == 0.:
            assert BigWorld.time() - STATE['start'] < 20., 'Native water sensor never received a frame update'
            BigWorld.callback(0.05, inspect_transition)
            return
        assert view.waterHeight == -1.
        assert not view.isInWater and not view.isUnderWater and not view.isSplash
        assert not REPORT.get('unexpected_events')
        REPORT['checks'].append('actual component scheduler publishes no-water state')
        STATE['system'].destroy()
        STATE['system'].destroy()
        invalid(lambda: view.waterHeight, ValueError)
        invalid(lambda: view.activate(), ValueError)
        assert all(link() is False for link in STATE['links'])
        gc.collect()
        assert STATE['callback']() is None
        REPORT['checks'].append('destruction invalidates borrowed views and retires callback ownership')
        REPORT['status'] = 'passed'
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()
    BigWorld.quit()


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    STATE['start'] = BigWorld.time()
    save()
    BigWorld.callback(0., inspect_transition)


def onChangeEnvironments(environment):
    REPORT['environment'] = environment
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    if 'system' in STATE:
        STATE['system'].destroy()
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
