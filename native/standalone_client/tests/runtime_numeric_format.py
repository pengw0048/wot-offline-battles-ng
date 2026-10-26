"""Check the 18 formatting exports in an isolated, fully linked client.

Copy the three exact original PYC consumers into numeric-format-input beside
the EXE. This diagnostic does not start the original GUI or change the locale.
"""
from __future__ import print_function
import json
import marshal
import os
import struct
import sys
import traceback
import types

ROOT = os.path.dirname(os.path.abspath(sys.executable))
OUTPUT = os.path.join(ROOT, 'numeric-format-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'checks': [], 'lifecycle': [],
          'original_game_init_called': False, 'locale_changed': False}


def save():
    assert os.path.basename(ROOT).startswith('numeric-format-'), ROOT
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def raises(callback):
    try:
        callback()
    except TypeError:
        return
    raise AssertionError('Invalid formatter argument was accepted')


def original_function(filename, name, scope):
    with open(os.path.join(ROOT, 'numeric-format-input', filename), 'rb') as stream:
        code = marshal.loads(stream.read()[8:])
    pending = [code]
    while pending:
        code = pending.pop()
        if code.co_name == name:
            return types.FunctionType(code, scope)
        pending.extend(value for value in code.co_consts if isinstance(value, types.CodeType))
    raise AssertionError('Original consumer is missing: ' + name)


def inspect():
    import BigWorld
    groups = ('Integral', 'Gold', 'NiceNumber', 'Fractional',
              'LongDate', 'ShortDate', 'YMDate', 'LongTime', 'ShortTime')
    values = {}
    for group in groups:
        name = 'wg_get%sFormat' % group
        byte = getattr(BigWorld, name)
        wide = getattr(BigWorld, name + 'U')
        arguments = ((0, -1, 1234, 1234.5, 2 ** 40) if group in ('Integral', 'Gold') else
                     (0, -0.5, 0.5, 1234.5) if group in ('NiceNumber', 'Fractional') else
                     (0, 1709687229, 1709687229.9, 1710053999, 1710054000, 2 ** 31))
        for value in arguments:
            a, b = byte(value), wide(value)
            assert type(a) is str and type(b) is unicode
            assert a == b.encode('utf-8'), (name, value, a, b)
        for function in (byte, wide):
            raises(lambda: function())
            raises(lambda: function(1, 2))
            raises(lambda: function(None))
            raises(lambda: function('1'))
        values[name] = wide(1709687229 if group not in ('Integral', 'Gold', 'NiceNumber', 'Fractional') else 1234.5)
    REPORT['checks'].append('18 real module registrations, UTF-8/unicode agreement and native argument errors')
    nan = struct.unpack('<d', struct.pack('<Q', 0x7ff8000000000000))[0]
    infinity = struct.unpack('<d', struct.pack('<Q', 0x7ff0000000000000))[0]
    for group in ('LongDate', 'ShortDate', 'YMDate', 'LongTime', 'ShortTime'):
        for suffix in ('', 'U'):
            function = getattr(BigWorld, 'wg_get%sFormat%s' % (group, suffix))
            for value in (-1, nan, infinity, -infinity, 32536800000.0):
                assert function(value) == '', (group, suffix, value)
            assert function(1709687229.9) == function(1709687229)
            raises(lambda: function(-1L))
            raises(lambda: function(1L << 32))
    REPORT['checks'].append('actual UCRT failure outputs, float truncation and original Win32 long boundary')
    scope = {'BigWorld': BigWorld}
    date_time = original_function('time_utils.pyc', 'getDateTimeFormat', scope)
    header = original_function('CalendarComponent.pyc', 'formatYMHeader', scope)
    fractional = original_function('style.pyc', 'getFractionalFormatIfNoEmpty',
        {'BigWorld': BigWorld, 'markValueAsEmpty': lambda value: 'empty'})
    stamp = 1709687229
    actual = date_time(stamp)
    assert actual == '%s %s' % (BigWorld.wg_getLongDateFormat(stamp), BigWorld.wg_getShortTimeFormat(stamp))
    assert header(None, stamp + .9) == BigWorld.wg_getYMDateFormat(stamp)
    assert fractional(.5) == BigWorld.wg_getFractionalFormat(.5)
    assert fractional(0) == 'empty'
    REPORT['consumer_outputs'] = {'date_time': actual, 'year_month': header(None, stamp),
                                  'fractional': fractional(.5)}
    REPORT['samples'] = values
    REPORT['checks'].append('three exact original bytecode consumers call the real BigWorld module')


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')
    try:
        inspect()
        REPORT['status'] = 'passed'
    except Exception:
        REPORT['status'] = 'failed'
        REPORT['error'] = traceback.format_exc()
    save()


def init(*args):
    REPORT['lifecycle'].append('init')
    save()
    return True


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    save()
    BigWorld.quit()


def onChangeEnvironments(environment):
    REPORT['environment'] = environment
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
