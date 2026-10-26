"""Replay independently verified math cases inside the full native client.

Run only in a private python-math-specials-<revision> diagnostic directory.
The case and expected-outcome files come from the original-instruction and
Win32 control probes; this personality contains no replacement math logic.
"""
from __future__ import print_function
import json
import math
import os
import struct
import sys
import traceback

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'python-math-specials-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False, 'executed_cases': 0,
          'stale_errno_cases': 0, 'mismatches': []}


def save():
    assert os.path.basename(ROOT).startswith('python-math-specials-'), ROOT
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def from_word(word):
    return struct.unpack('<d', struct.pack('<Q', long(word, 16)))[0]


def to_word(value):
    return '%016x' % struct.unpack('<Q', struct.pack('<d', value))[0]


def arguments(kind, words):
    if kind == 'f1':
        return (from_word(words[0]),)
    if kind == 'f2':
        return tuple(from_word(word) for word in words)
    if kind in ('c1', 'c1r'):
        return (complex(from_word(words[0]), from_word(words[1])),)
    if kind == 'fi':
        return (from_word(words[0]), long(words[1], 10))
    if kind == 'fs':
        value = float(words[1])
        assert to_word(value) == words[0]
        return (value,)
    raise AssertionError('Unknown input kind: ' + kind)


def describe(value):
    if type(value) is float:
        return 'F:' + to_word(value)
    if type(value) is complex:
        return 'C:' + to_word(value.real) + ',' + to_word(value.imag)
    if type(value) is tuple and len(value) == 2:
        return 'T:' + ','.join(to_word(item) for item in value)
    raise AssertionError('Unexpected result type: ' + repr(type(value)))


def run():
    import BigWorld
    try:
        assert sys.version_info[:3] == (2, 7, 7), sys.version
        # The actual math_1 owner clears errno before sqrt and sets ERANGE
        # before reporting finite exp overflow. Use those public operations
        # so this diagnostic needs no extra native module in the game.
        reset_errno, seed_erange = math.sqrt, math.exp
        REPORT['errno_setup'] = 'math.sqrt(1.0) clears errno; math.exp(1000.0) seeds ERANGE'
        with open(os.path.join(ROOT, 'expected-outcomes.txt'), 'rb') as stream:
            expected = dict(line.strip().split(' ', 1) for line in stream if line.strip())
        with open(os.path.join(ROOT, 'cases.txt'), 'rb') as stream:
            cases = [line.split() for line in stream if line.strip()]
        assert len(cases) == len(expected) == 2805
        with open(os.path.join(ROOT, 'actual-outcomes.txt'), 'wb') as output:
            for case in cases:
                identity, module, name, kind = case[:4]
                function = getattr(__import__(module), name)
                args = arguments(kind, case[4:])
                if kind == 'c1r':
                    try:
                        seed_erange(1000.)
                    except OverflowError:
                        pass
                    else:
                        raise AssertionError('The ERANGE preparation did not overflow')
                else:
                    reset_errno(1.)
                try:
                    value = function(*args)
                except Exception as error:
                    result = 'E:' + type(error).__module__ + '.' + type(error).__name__ + ':' + str(error)
                else:
                    result = describe(value)
                output.write(identity + ' ' + result + '\n')
                REPORT['executed_cases'] += 1
                REPORT['stale_errno_cases'] += kind == 'c1r'
                if result != expected[identity]:
                    REPORT['mismatches'].append({'case': case, 'expected': expected[identity], 'actual': result})
        assert not REPORT['mismatches'], '%d output mismatches' % len(REPORT['mismatches'])
        REPORT['checks'].append('All 2805 full-client outputs match independently verified Win32 outcomes bit for bit')
        REPORT['checks'].append('NaN payloads, infinity, signed zero, complex abs dispatch and stale ERANGE are exercised')
        REPORT['status'] = 'passed'
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()
    BigWorld.quit()


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    save()


def init(*args):
    REPORT['lifecycle'].append('init')
    save()
    return True


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    save()
    BigWorld.callback(0., run)


def onChangeEnvironments(environment):
    REPORT['environment'] = environment
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
