"""Isolated full-client check of builtin registration and the fixed consumer."""
from __future__ import print_function
import imp
import json
import os
import sys
import traceback

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'offline-fork-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'lifecycle': [], 'checks': [],
          'original_game_init_called': False, 'gameplay_acceptance': False}


def save():
    assert os.path.basename(ROOT).startswith('offline-fork-'), ROOT
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def inspect_builtin():
    with open(os.path.join(ROOT, 'offline-fork-input.json'), 'rb') as stream:
        expected = json.load(stream)
    assert sys.version_info[:3] == (2, 7, 7), sys.version
    import offline_math_batch_native as backend
    assert backend is sys.modules['offline_math_batch_native']
    assert not getattr(backend, '__file__', None)
    assert backend._wot_offline_fork_revision == expected['revision']
    methods = sorted(name for name in dir(backend) if not name.startswith('_'))
    assert len(methods) == 103 and methods == sorted(expected['methods']), methods
    assert all(callable(getattr(backend, name)) for name in methods)
    REPORT['checks'].append('all 103 builtin methods registered before personality')
    first = backend.thread_cpu_seconds()
    second = backend.thread_cpu_seconds()
    assert type(first) is float and first >= 0.0 and second >= first
    window = backend.window_state()
    assert type(window) is tuple and len(window) == 3, window
    assert all(type(value) is int and value in (-1, 0, 1) for value in window), window
    REPORT['window_state'] = window
    REPORT['thread_cpu_seconds'] = [first, second]
    REPORT['checks'].append('actual Win32 thread clock and window query')
    consumer = imp.load_source('independent_native_math_consumer',
                               os.path.join(ROOT, 'native_math_consumer.py'))
    assert consumer._load() is backend
    assert consumer._load() is backend
    REPORT['checks'].append('fixed fork consumer reuses the registered builtin')


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    try:
        inspect_builtin()
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()


def init(*args):
    REPORT['lifecycle'].append('init')
    save()
    return True


def complete():
    import BigWorld
    REPORT['lifecycle'].append('callback')
    if REPORT['status'] != 'failed':
        REPORT['status'] = 'passed'
    save()
    BigWorld.quit()


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    save()
    BigWorld.callback(0.0, complete)


def onChangeEnvironments(environment):
    REPORT['environment'] = environment
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
