"""Exercise TerrainMatKindSensor against a real #1513 compiled terrain space.

World drawing is disabled. A passing result covers native queries and component
updates, not rendered terrain, vehicle suspension, or complete battles.
"""
from __future__ import print_function
import json
import os
import sys
import time
import traceback

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'terrain-sensor-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False, 'world_drawing_enabled': False,
          'authored_terrain_tested': False, 'render_acceptance': False,
          'resource': 'spaces/01_karelia'}
STATE = {'phase': 'not-started'}


def save():
    assert os.path.basename(ROOT).startswith('terrain-sensor-'), ROOT
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


def cleanup():
    import BigWorld
    if 'system' in STATE:
        STATE.pop('system').destroy()
    if 'space' in STATE:
        space = STATE.pop('space')
        STATE['camera'].spaceID = 0
        BigWorld.cameraSpaceID(0)
        BigWorld.clearLocalSpace(space)
        REPORT['lifecycle'].append('clearLocalSpace')


def finish(error=None):
    import BigWorld
    if STATE.get('finished'):
        return
    STATE['finished'] = True
    REPORT['status'] = 'failed' if error else 'passed'
    if error:
        REPORT['error'] = error
    try:
        cleanup()
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['cleanup_error'] = traceback.format_exc()
    save()
    BigWorld.quit()


def inspect_factory():
    import Math
    import Vehicular
    invalid(lambda: Vehicular.TerrainMatKindSensor())
    invalid(lambda: Vehicular.TerrainMatKindSensor(1, (), 4.))
    sensor = Vehicular.TerrainMatKindSensor(Math.Matrix(), ((0., 2., 0.),), 4.)
    kinds = sensor.matKinds
    assert sensor.isOwning and list(kinds) == [0]
    assert type(kinds).__name__ == 'PySTLSequence'
    assert kinds is not sensor.matKinds and kinds.length == 1
    invalid(lambda: kinds.__setitem__(0, 5))
    invalid(lambda: setattr(sensor, 'matKinds', []))
    invalid(lambda: sensor.setLodSettings(()))
    sensor.setLodSettings((150., 15))
    sensor.setLodLink(None)
    sensor.destroy()
    sensor.destroy()
    assert list(kinds) == [] and list(sensor.matKinds) == []
    invalid(sensor.activate, ValueError)
    REPORT['checks'].append('real factory, read-only live sequence and idempotent destruction')


def prepare_samples():
    import BigWorld
    import Math
    import Svarog
    import Vehicular
    samples = []
    by_kind = {}
    for x in range(-450, 451, 100):
        for z in range(-450, 451, 100):
            hit = BigWorld.collide(STATE['space'], (float(x), 1000., float(z)),
                                   (float(x), -1000., float(z)), BigWorld.TRIANGLE_TERRAIN)
            if hit is None or hit[2] == 0:
                continue
            point = (hit[0].x, hit[0].y + 2., hit[0].z)
            end = (point[0], point[1] - 4., point[2])
            nearest = BigWorld.collide(STATE['space'], point, end)
            if nearest is None or nearest[2] != hit[2] or abs(nearest[0].y - hit[0].y) > .01:
                continue
            sample = (point, hit[2])
            samples.append(sample)
            by_kind.setdefault(hit[2], sample)
    REPORT['sample_count'] = len(samples)
    REPORT['material_kinds'] = sorted(by_kind)
    assert len(samples) >= 3, ('Real terrain has insufficient nonzero material samples', samples)
    selected = [by_kind[kind] for kind in sorted(by_kind)[:3]]
    selected.extend(samples[:3 - len(selected)])
    points = tuple(sample[0] for sample in selected)
    expected = [sample[1] for sample in selected]
    provider = Math.Matrix()
    sensor = Vehicular.TerrainMatKindSensor(provider, points, 4.)
    kinds = sensor.matKinds
    assert list(kinds) == [0, 0, 0]
    system = Svarog.ComponentSystem()
    system.addComponent(sensor, 'terrainSensor')
    assert not sensor.isOwning
    borrowed = system.findComponent('terrainSensor')
    assert not borrowed.isOwning
    STATE.update(system=system, sensor=borrowed, kinds=kinds, provider=provider,
                 expected=expected, phase='await-terrain', transition_start=BigWorld.time())
    REPORT['samples'] = selected
    REPORT['checks'].append('independent native terrain and nearest-triangle queries agree at selected points')
    system.activate()
    save()


def tick():
    import BigWorld
    try:
        assert time.time() < STATE['deadline'], ('Native transition timed out', STATE['phase'])
        phase = STATE['phase']
        if phase == 'loading':
            status = BigWorld.spaceLoadStatus()
            REPORT['space_load_status'] = status
            if status >= 1.:
                prepare_samples()
        elif phase == 'await-terrain':
            if list(STATE['kinds']) == STATE['expected']:
                REPORT['first_update_seconds'] = BigWorld.time() - STATE['transition_start']
                REPORT['authored_terrain_tested'] = True
                REPORT['checks'].append('real frame scheduler publishes authored terrain material kinds')
                STATE['provider'].translation = (0., 4000., 0.)
                STATE['phase'] = 'await-miss'
                save()
        elif phase == 'await-miss':
            if list(STATE['kinds']) == [0, 0, 0]:
                REPORT['checks'].append('retained live sequence changes to zero after provider moves above scan range')
                STATE['provider'].translation = (0., 0., 0.)
                STATE['phase'] = 'await-return'
                save()
        elif phase == 'await-return':
            if list(STATE['kinds']) == STATE['expected']:
                STATE['system'].destroy()
                STATE['system'].destroy()
                assert list(STATE['kinds']) == []
                assert list(STATE['sensor'].matKinds) == []
                invalid(STATE['sensor'].activate, ValueError)
                REPORT['checks'].append('provider reset restores real materials; destruction empties retained and borrowed views')
                finish()
                return
        BigWorld.callback(.05, tick)
    except Exception:
        finish(traceback.format_exc())


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    try:
        import ResMgr
        REPORT['configured_space_type'] = ResMgr.openSection('diagnostic_engine_config.xml').readString('spaceType')
        inspect_factory()
    except Exception:
        STATE['failure'] = traceback.format_exc()
    save()


def init(*args):
    import BigWorld
    REPORT['lifecycle'].append('init')
    try:
        # Replay the native graphics publication normally owned by game.init
        # and SettingsCore before the first authored terrain load.
        assert BigWorld.wg_initCustomSettings() is None
        BigWorld.generateGfxSettings()
        REPORT['native_graphics_published'] = True
        REPORT['graphics_settings'] = dict((label, BigWorld.graphicSetting(label)[1])
            for label in ('RENDER_PIPELINE', 'SHADOWS_QUALITY', 'DECALS_QUALITY',
                          'LIGHTING_QUALITY', 'TERRAIN_QUALITY'))
    except Exception:
        STATE['failure'] = traceback.format_exc()
    save()
    return True


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    try:
        if STATE.get('failure'):
            finish(STATE['failure'])
            return
        BigWorld.worldDrawEnabled(False)
        STATE['camera'] = BigWorld.camera()
        STATE['space'] = BigWorld.createLocalSpace()
        STATE['camera'].spaceID = STATE['space']
        BigWorld.cameraSpaceID(STATE['space'])
        STATE['phase'] = 'loading'
        STATE['deadline'] = time.time() + 120.
        save()
        STATE['mapping'] = BigWorld.addSpaceGeometryMapping(STATE['space'], None, REPORT['resource'])
        BigWorld.callback(0., tick)
    except Exception:
        finish(traceback.format_exc())


def onGeometryMapped(space, path):
    REPORT['geometry_mapped'] = {'space': space, 'path': path}
    save()


def onChangeEnvironments(environment):
    REPORT['environment'] = environment
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    cleanup()
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
