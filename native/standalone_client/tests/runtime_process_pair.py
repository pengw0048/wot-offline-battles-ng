"""Two real independent clients acknowledge one externally observed live pair.

The runner admits only these isolated diagnostic paths. No game, server or
worker authority is started by this personality.
"""
from __future__ import print_function
import imp
import json
import os
import sys
import time
import traceback

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'process-pair-result.json')
REPORT = {'format': 1, 'status': 'starting', 'pid': os.getpid(),
          'executable': sys.executable, 'lifecycle': [],
          'gameplay_acceptance': False, 'original_game_init_called': False}
STATE = {}


def save():
    assert os.path.basename(ROOT).startswith('process-pair-'), ROOT
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    try:
        with open(os.path.join(ROOT, 'process-pair-input.json'), 'rb') as stream:
            specification = json.load(stream)
        assert specification['role'] in ('first', 'second')
        assert specification['token']
        STATE['input'] = specification
        REPORT['role'] = specification['role']
        REPORT['token'] = specification['token']
        consumer = imp.load_source('pair_instance_guard',
            os.path.join(ROOT, 'instance_guard_consumer.py'))
        bridge = consumer._load_native_bridge()
        assert not getattr(bridge, '__file__', None)
        assert consumer.release_if_requested({consumer.ALLOW_MULTIPLE_CLIENTS_ENV: '1'})
        assert consumer.release_if_requested({consumer.ALLOW_MULTIPLE_CLIENTS_ENV: '1'})
        STATE['consumer'] = consumer
        REPORT['policy_available'] = True
        REPORT['hidden_windows'] = consumer.hide_process_windows()
        assert REPORT['hidden_windows'] >= 0
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()


def init(*args):
    REPORT['lifecycle'].append('init')
    save()
    return True


def poll():
    import BigWorld
    try:
        if REPORT['status'] == 'failed':
            BigWorld.quit()
            return
        specification = STATE['input']
        assert time.time() - STATE['started'] < 180., 'The second live process was not acknowledged'
        path = specification['acknowledgement']
        if not os.path.isfile(path):
            BigWorld.callback(0.1, poll)
            return
        with open(path, 'rb') as stream:
            observed = json.load(stream)
        assert observed['token'] == specification['token']
        assert len(observed['pids']) == 2 and len(set(observed['pids'])) == 2
        assert os.getpid() in observed['pids']
        REPORT['observed_live_pair'] = observed
        REPORT['status'] = 'passed'
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()
    BigWorld.quit()


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    STATE['started'] = time.time()
    if REPORT['status'] != 'failed':
        REPORT['status'] = 'ready'
    save()
    BigWorld.callback(0., poll)


def onChangeEnvironments(environment):
    REPORT['environment'] = environment
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
