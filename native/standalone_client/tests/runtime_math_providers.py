"""Diagnostic personality for the real Win32 client, before original game.init.

Select this module only in an isolated diagnostic resource overlay. It exercises
the production Python types, DirectX matrices, provider ticks and replay clock,
then requests normal application shutdown. It is not a gameplay acceptance test.
"""
from __future__ import print_function
import json
import math
import os
import sys
import time
import traceback

REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False}
STATE = {}
OUTPUT = os.path.join(os.getcwd(), 'math-providers-result.json')


def save():
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def values(provider):
    import Math
    matrix = Math.Matrix(provider)
    return tuple(matrix.get(row, column) for row in range(4) for column in range(4))


def close(first, second):
    assert len(first) == len(second)
    assert all(abs(a - b) <= 0.000002 * max(1, abs(a), abs(b))
               for a, b in zip(first, second)), (first, second)


def invalid(call):
    try:
        call()
    except TypeError:
        return
    raise AssertionError('Invalid provider input was accepted')


def finish(error=None):
    import BigWorld
    REPORT['status'] = 'failed' if error else 'passed'
    if error:
        REPORT['error'] = error
    STATE.clear()
    save()
    print('MATH_PROVIDERS_RUNTIME_' + REPORT['status'].upper())
    BigWorld.quit()


def init(*args):
    REPORT['lifecycle'].append('init')
    try:
        import BigWorld
        import Math
        import ResMgr
        identity = values(Math.Matrix())
        names = ('WGYawOnlyTransform', 'WGInverseTransform', 'WGBoundMatrixProvider',
                 'WGCombinedMP', 'WGTranslationOnlyMP', 'WGStrategicAreaViewMP',
                 'WGSmoothTranslationOnlyMP', 'WGReplayAwaredSmoothTranslationOnlyMP')
        for name in names:
            factory = getattr(Math, name)
            instance = factory()
            assert isinstance(instance, Math.MatrixProvider), name
            assert values(instance) == identity, name
            invalid(lambda: factory(1))
            if name != 'WGCombinedMP':
                assert instance.source is None
                invalid(lambda: setattr(instance, 'source', object()))
                instance.source = None
        assert issubclass(Math.WGReplayAwaredSmoothTranslationOnlyMP,
                          Math.WGSmoothTranslationOnlyMP)
        REPORT['checks'].append('eight real factories, defaults, inheritance and conversion failures')

        source = Math.Matrix()
        source.setScale((2, 3, 4))
        source.translation = (10, -20, 30)
        source.setElement(0, 3, 0.25)
        source.setElement(3, 3, 7)
        original = values(source)
        bound = Math.WGBoundMatrixProvider()
        refs = sys.getrefcount(source)
        bound.source = source
        assert bound.source is source and sys.getrefcount(source) == refs + 1
        bound.minBound = (-5, -5, -5)
        bound.maxBound = (5, 5, 5)
        expected = list(original)
        expected[12:16] = [5, -5, 5, 1]
        assert values(bound) == tuple(expected)
        assert values(source) == original
        bound.source = None
        assert sys.getrefcount(source) == refs
        assert values(bound) == identity

        combined = Math.WGCombinedMP()
        combined.rotationSrc = source
        assert values(combined) == identity
        translation = Math.Matrix()
        translation.translation = (-100, 200, 300)
        combined.translationSrc = translation
        expected = list(original)
        expected[12:16] = [-100, 200, 300, 1]
        assert values(combined) == tuple(expected)
        translation.translation = (11, 22, 33)
        expected[12:16] = [11, 22, 33, 1]
        assert values(combined) == tuple(expected)
        assert values(source) == original
        only = Math.WGTranslationOnlyMP()
        only.source = source
        expected = list(identity)
        expected[12:15] = [10, -20, 30]
        assert values(only) == tuple(expected)
        REPORT['checks'].append('live source identity, reference ownership, bounds and two-source composition')

        invertible = Math.Matrix()
        invertible.setScale((2, 4, 8))
        invertible.translation = (10, 20, 40)
        inverse = Math.WGInverseTransform()
        inverse.source = invertible
        expected = [0.5, 0, 0, 0, 0, 0.25, 0, 0, 0, 0, 0.125, 0, -5, -5, -5, 1]
        close(values(inverse), expected)
        singular = Math.Matrix()
        singular.setZero()
        inverse.source = singular
        assert values(inverse) == values(singular)
        rotation = Math.Matrix()
        rotation.setRotateYPR((0.7, -0.3, 0.2))
        rotation.translation = (17, 21, -9)
        yaw = Math.WGYawOnlyTransform()
        yaw.source = rotation
        expected = Math.Matrix()
        expected.setRotateY(math.atan2(rotation.get(2, 0), rotation.get(2, 2)))
        close(values(yaw), values(expected))
        area = Math.WGStrategicAreaViewMP()
        area.source = Math.Matrix()
        area.source.translation = (0, 100, 0)
        area.baseScale = (2, 3)
        first = values(area)
        assert first[0] == 2 and first[5] == 1 and first[10] > 0
        area.setBaseHeight(200)
        second = values(area)
        close((second[0], second[5], second[10]), (first[0] / 2, 1, first[10] / 2))
        assert second[12:16] == first[12:16]
        REPORT['checks'].append('real DirectX inverse, singular output, yaw and current-camera strategic scaling')

        directory = ResMgr.appDirectory()
        assert type(directory) is str and directory.endswith('/')
        assert directory.replace('\\', '/').rstrip('/').lower() == os.path.dirname(
            sys.executable).replace('\\', '/').rstrip('/').lower()
        invalid(lambda: ResMgr.appDirectory(1))
        REPORT['application_directory'] = directory
        REPORT['active_content_type'] = ResMgr.activeContentType()

        controller = BigWorld.WGReplayController()
        controller.playbackSpeed = 0
        moving = Math.Matrix()
        ordinary = Math.WGSmoothTranslationOnlyMP()
        paused = Math.WGReplayAwaredSmoothTranslationOnlyMP()
        ordinary.source = moving
        paused.source = moving
        moving.translation = (100, 0, 0)
        assert values(ordinary)[12] == 0 and values(paused)[12] == 0
        STATE.update(controller=controller, moving=moving, ordinary=ordinary,
                     paused=paused, phase='paused', deadline=time.time() + 15, callbacks=0)
        save()
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
        save()


def check_tick():
    import BigWorld
    import Math
    try:
        STATE['callbacks'] += 1
        assert time.time() < STATE['deadline'], ('Native provider tick timed out', STATE['phase'])
        ordinary, paused = STATE['ordinary'], STATE['paused']
        x, y = values(ordinary)[12], values(paused)[12]
        phase = STATE['phase']
        if phase == 'paused':
            assert y == 0, 'Replay-aware provider advanced while the real controller was paused'
            if x > 0:
                assert x < 100
                STATE['controller'].playbackSpeed = 1
                STATE['phase'] = 'resumed'
        elif phase == 'resumed' and y > 0:
            assert y < 100
            ordinary.source = None
            paused.source = None
            STATE['held'] = (x, y)
            STATE['moving'].translation = (400, 0, 0)
            STATE['phase'] = 'cleared'
        elif phase == 'cleared':
            assert (x, y) == STATE['held']
            STATE['controller'].playbackSpeed = 0
            replacement = Math.Matrix()
            replacement.translation = (20, 0, 0)
            paused.source = replacement
            assert values(paused)[12] == 20
            replacement.translation = (80, 0, 0)
            del STATE['controller']
            STATE['phase'] = 'owner-deallocated'
        elif phase == 'owner-deallocated' and y > 20:
            assert y < 80
            REPORT['callbacks'] = STATE['callbacks']
            REPORT['checks'].append('real auto-tick, paused replay clock, resume, source clear/replacement and controller destruction')
            finish()
            return
        BigWorld.callback(0.0, check_tick)
    except Exception:
        finish(traceback.format_exc())


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    if REPORT['status'] == 'failed':
        finish(REPORT['error'])
    else:
        BigWorld.callback(0.0, check_tick)


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
