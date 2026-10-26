"""Isolated full-client check of builtin registration and the fixed consumer."""
from __future__ import print_function
import imp
import json
import os
import struct
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
    for source, bits in (('inf', 0x7ff0000000000000), ('-inf', 0xfff0000000000000),
                         ('nan', 0xfff8000000000000), ('-nan', 0x7ff8000000000000)):
        assert struct.unpack('<Q', struct.pack('<d', float(source)))[0] == bits
        hex_bits = 0xfff8000000000000 if 'nan' in source else bits
        assert struct.unpack('<Q', struct.pack('<d', float.fromhex(source)))[0] == hex_bits
    REPORT['checks'].append('actual embedded special-value parsing matches original bits')

    import offline_instance_guard_native as platform
    assert not getattr(platform, '__file__', None)
    assert platform._wot_offline_fork_revision == expected['revision']
    methods = sorted(name for name in dir(platform) if not name.startswith('_'))
    assert methods == sorted(('install_exception_trail', 'hide_process_windows',
        'show_process_windows', 'apply_standard_gameplay_mask',
        'restore_standard_gameplay_mask')), methods
    for name in methods:
        try:
            getattr(platform, name)(1)
        except TypeError:
            pass
        else:
            raise AssertionError('Unexpected argument accepted: ' + name)
    policy = imp.load_source('independent_process_consumer',
                             os.path.join(ROOT, 'instance_guard_consumer.py'))
    assert policy._load_native_bridge() is platform
    requested = {policy.ALLOW_MULTIPLE_CLIENTS_ENV: '1'}
    assert policy.release_if_requested(requested)
    assert policy.release_if_requested(requested)
    assert platform.restore_standard_gameplay_mask() == 102
    assert platform.apply_standard_gameplay_mask() == 0
    assert platform.apply_standard_gameplay_mask() == 101
    assert platform.restore_standard_gameplay_mask() == 0
    scope = imp.load_source('independent_mapping_consumer',
                            os.path.join(ROOT, 'mapping_mask_consumer.py'))
    def failure():
        assert platform.apply_standard_gameplay_mask() == 101
        raise ValueError('diagnostic mapping callback')
    try:
        scope.call_with_standard_gameplay_mask(failure, native_bridge=platform)
    except ValueError:
        pass
    else:
        raise AssertionError('Mapping callback failure was lost')
    assert platform.restore_standard_gameplay_mask() == 102
    hidden = policy.hide_process_windows()
    assert hidden >= 0
    assert policy.hide_process_windows() == 0
    restored = policy.show_process_windows()
    assert 0 <= restored <= hidden
    REPORT['process_windows'] = {'hidden': hidden, 'restored': restored}
    REPORT['checks'].append('actual process services and reversible mapping scope')


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
