"""Exercise integrated AA settings, frame transitions and private persistence.

This empty-space diagnostic does not compare pixels or prove AA image quality.
Use a private antialiasing-<revision> runtime with an absolute preferences path.
"""
from __future__ import print_function
import json
import os
import sys
import traceback
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'antialiasing-result.json')
PREFERENCES = os.path.join(ROOT, 'antialiasing-test.xml')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'states': [], 'original_game_init_called': False,
          'render_output_validated': False, 'device_reset_parameters_inspected': False}
STATE = {}


def save():
    assert os.path.basename(ROOT).startswith('antialiasing-'), ROOT
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def canonical(path):
    return os.path.normcase(os.path.realpath(os.path.abspath(path)))


def raises(kind, callback):
    try:
        callback()
    except kind:
        return
    raise AssertionError('Expected ' + kind.__name__)


def fail():
    import BigWorld
    REPORT['status'] = 'failed'
    REPORT['error'] = traceback.format_exc()
    save()
    BigWorld.quit()


def inspect_ipc():
    import BigWorldIPC
    mutex = BigWorldIPC.Mutex()
    raises(SystemError, lambda: mutex.acquire(0.))
    assert mutex.create('wot_integrated_ipc_%d' % os.getpid()) is None
    assert mutex.acquire(0.) is True
    assert mutex.acquire(0.) is True
    assert mutex.release() is None
    assert mutex.release() is None
    raises(SystemError, mutex.release)
    assert mutex.close() is None and mutex.close() is None
    REPORT['checks'].append('integrated IPC module, recursion, immediate errors and repeated close')


def inspect_initial():
    import BigWorld
    actual = BigWorld.wg_getPreferencesFilePath().decode('utf-8')
    assert canonical(actual) == canonical(PREFERENCES), actual
    samples = BigWorld.getSupportedMultisamplingTypes()
    modes = BigWorld.getSupportedCustomAAModes()
    assert type(samples) is list and all(type(value) is int for value in samples), samples
    assert modes in ([0], [0, 1, 2, 3]), modes
    assert BigWorld.getMultisamplingType() == 0
    assert BigWorld.getCustomAAMode() == 0
    for name in ('getSupportedMultisamplingTypes', 'getSupportedCustomAAModes',
                 'getMultisamplingType', 'getCustomAAMode'):
        raises(TypeError, lambda: getattr(BigWorld, name)(1))
    for name in ('setMultisamplingType', 'setCustomAAMode'):
        raises(TypeError, lambda: getattr(BigWorld, name)())
        raises(TypeError, lambda: getattr(BigWorld, name)(None))
    REPORT['supported_multisampling'] = samples
    REPORT['supported_custom_modes'] = modes
    REPORT['checks'].append('actual device-backed capability exports and Python argument boundaries')
    # The native device probe covers 0/2/4 on this adapter. Use a supported
    # ordinary mode for integrated transitions, not a vendor extension enum.
    selected = next((value for value in (2, 4) if value in samples), 0)
    STATE['steps'] = [(0, mode) for mode in modes]
    if selected:
        STATE['steps'].extend([(selected, 0), (0, 0)])
    STATE['index'] = 0
    inspect_ipc()


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    try:
        inspect_initial()
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()


def init(*args):
    REPORT['lifecycle'].append('init')
    save()
    return True


def set_next():
    import BigWorld
    try:
        if STATE['index'] == len(STATE['steps']):
            assert len(REPORT['states']) == len(STATE['steps'])
            REPORT['checks'].append('AA requests retained across actual engine callback transitions')
            REPORT['checks'].append('native preference writer persisted every requested AA state')
            REPORT['status'] = 'passed'
            save()
            BigWorld.quit()
            return
        samples, mode = STATE['steps'][STATE['index']]
        assert BigWorld.setMultisamplingType(samples) is None
        assert BigWorld.setCustomAAMode(mode) is None
        STATE['after'] = BigWorld.time()
        BigWorld.callback(0., inspect_transition)
    except Exception:
        fail()


def inspect_transition():
    import BigWorld
    try:
        # callback(0) may execute within one admitted callback batch. Observe
        # actual engine time advance before checking the state/persistence.
        if BigWorld.time() <= STATE['after']:
            BigWorld.callback(0., inspect_transition)
            return
        samples, mode = STATE['steps'][STATE['index']]
        assert BigWorld.getMultisamplingType() == samples
        assert BigWorld.getCustomAAMode() == mode
        assert BigWorld.savePreferences() is True
        root = ET.parse(PREFERENCES).getroot()
        assert int(root.find('devicePreferences/multisamplingType').text) == samples
        assert int(root.find('devicePreferences/multisamplingQuality').text) == 0
        if len(REPORT['supported_custom_modes']) > 1:
            assert int(root.find('graphicsPreferences/customAAMode').text) == mode
        REPORT['states'].append({'samples': samples, 'mode': mode, 'time': BigWorld.time()})
        STATE['index'] += 1
        save()
        BigWorld.callback(0., set_next)
    except Exception:
        fail()


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    save()
    if REPORT['status'] == 'failed':
        BigWorld.quit()
    else:
        BigWorld.callback(0., set_next)


def onChangeEnvironments(environment):
    REPORT['environment'] = environment
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
