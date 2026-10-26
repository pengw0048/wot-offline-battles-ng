"""Exercise real CursorCamera Python bindings and matrix ownership on Win32.

Run only in an isolated diagnostic overlay. This does not prove rendered camera
motion, authored-model collision, tree hiding or garage/battle acceptance.
"""
from __future__ import print_function
import gc
import json
import os
import sys
import traceback
import weakref

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'cursor-camera-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False, 'rendered_motion_tested': False,
          'authored_model_collision_tested': False}


def save():
    assert os.path.basename(ROOT).startswith('cursor-camera-'), ROOT
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
    assert all(abs(a - b) < 0.00001 for a, b in zip(first, second)), (first, second)


def invalid(callback, kind):
    try:
        callback()
    except kind:
        return
    raise AssertionError('Invalid camera operation was accepted')


def inspect_camera():
    import BigWorld
    import Math
    camera = BigWorld.CursorCamera()
    assert camera.sptHiding is False
    invalid(lambda: camera.wg_applyParams(1), TypeError)
    invalid(lambda: camera.wg_setModelsToCollideWith(), TypeError)
    for item in (None, (), object(), [object()]):
        invalid(lambda: camera.wg_setModelsToCollideWith(item), RuntimeError)
    assert camera.wg_setModelsToCollideWith([]) is None
    REPORT['checks'].append('real factory, defaults, method widths and list errors')

    target = Math.Matrix()
    target.translation = (10., 20., 30.)
    source = Math.Matrix()
    source.setRotateYPR((0.3, 0.2, 0.))
    camera.target = target
    camera.source = source
    camera.pivotPosition = (0., 0., 0.)
    camera.pivotMaxDist = 10.
    camera.movementHalfLife = 0.
    camera.turningHalfLife = 0.
    camera.maxDistHalfLife = 0.
    assert camera.wg_applyParams() is None
    baseline = values(camera.matrix)
    inverse = values(camera.invViewMatrix)
    assert baseline != values(Math.Matrix())
    expected = Math.Matrix(camera.matrix)
    expected.invert()
    close(values(expected), inverse)
    provider = camera.matrix
    assert provider is not camera.matrix
    target.translation = (-5., 6., 7.)
    camera.wg_applyParams()
    assert values(provider) != baseline
    close(values(provider), values(camera.matrix))
    baseline = values(camera.matrix)
    inverse = values(camera.invViewMatrix)
    camera.shake(5., (0.3, 0.2, 0.1))
    camera.wg_applyParams()
    close(values(provider), baseline)
    close(values(camera.invViewMatrix), inverse)
    REPORT['checks'].append('live matrix getsets, update dispatch, DirectX inverse and stable shake pass')

    camera.sptHiding = True
    camera.wg_applyParams()
    camera.sptHiding = False
    camera.target = None
    camera.source = None
    reference = weakref.ref(camera)
    camera = None
    gc.collect()
    assert reference() is None
    close(values(provider), values(Math.Matrix()))
    REPORT['checks'].append('hiding publication/reset, weak provider lifetime and safe post-destruction read')


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    save()


def init(*args):
    REPORT['lifecycle'].append('init')
    try:
        inspect_camera()
        REPORT['status'] = 'passed'
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()
    return True


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    save()
    print('CURSOR_CAMERA_RUNTIME_' + REPORT['status'].upper())
    BigWorld.callback(0., BigWorld.quit)


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
