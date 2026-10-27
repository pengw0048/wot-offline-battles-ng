# coding: utf-8
# Python 2.7 driver for stock_scaleform_localization_host.cpp: argv[1] is the
# exact #1513 client root. helpers.i18n and gettext are the original PYC over the
# original MO files through a read-only ResMgr path adapter.
import marshal
import os
import StringIO
import sys
import types
import zipfile

import _localization as host

ROOT = sys.argv[1]
PACKAGE = zipfile.ZipFile(os.path.join(ROOT, 'res/packages/scripts.pkg'))
WARNINGS = []
RAISE = []


def code_of(path):
    return marshal.loads(PACKAGE.read(path)[8:])


class Section(object):
    def __init__(self, path):
        self.asBinary = open(path, 'rb').read()


def open_section(path):
    full = os.path.join(ROOT, 'res', path)
    return Section(full) if os.path.isfile(full) else None


def log_warning(*args):
    WARNINGS.append(args)
    if RAISE:
        raise RuntimeError('LOG_WARNING raised for the test')


def stub(name, **values):
    module = types.ModuleType(name)
    module.__dict__.update(values)
    sys.modules[name] = module
    return module


stub('ResMgr', openSection=open_section)
stub('debug_utils', LOG_WARNING=log_warning,
     LOG_CURRENT_EXCEPTION=lambda: WARNINGS.append(('exception', str(sys.exc_info()[1]))))
gettext = stub('gettext')
exec code_of('scripts/common/Lib/gettext.pyc') in gettext.__dict__
helpers = stub('helpers', __path__=[])


class Finder(object):
    """Loads helpers.i18n from the original PYC only when the owner imports it."""

    def __init__(self):
        self.loads = 0
        self.fail = False

    def find_module(self, name, path=None):
        return self if name == 'helpers.i18n' else None

    def load_module(self, name):
        if name in sys.modules:
            return sys.modules[name]
        self.loads += 1
        if self.fail:
            raise ImportError('helpers.i18n withheld by the test')
        module = types.ModuleType(name)
        module.__file__ = 'scripts/client/helpers/i18n.pyc'
        sys.modules[name] = module
        exec code_of('scripts/client/helpers/i18n.pyc') in module.__dict__
        helpers.i18n = module
        return module


FINDER = Finder()
sys.meta_path.insert(0, FINDER)


def captured(function, *args):
    """Calls the owner and returns (result, stderr text, recorded ERROR_MSG lines)."""
    saved, sys.stderr = sys.stderr, StringIO.StringIO()
    try:
        result = function(*args)
    finally:
        text, sys.stderr = sys.stderr.getvalue(), saved
    return result, text, host.events()


def utf8(text):
    return text.encode('utf-8')


def report(label, value):
    print '%s: %r' % (label, value)


# Install and register as Manager::doInit and PyMovieView do; nothing imports.
assert host.install() == (1, 0)
host.register()
assert 'helpers.i18n' not in sys.modules and FINDER.loads == 0

# 00ab37c0 reads two arguments; one is contained without touching Python.
result, err, events = captured(host.callback, 'WG.getFormattedLocalizedString', ('#dialogs:EULA/title',))
assert result is None and err == '' and host.fallback_calls == [], (result, err)
assert events == ['ERROR WG.getFormattedLocalizedString: expected a key and JSON arguments\n'], events
assert FINDER.loads == 0

# A missing helpers.i18n is reported, leaves the text and no pending error, and is retried.
FINDER.fail = True
result, err, events = captured(host.translate, '#dialogs:EULA/title')
assert result == (False, False) and 'ImportError: helpers.i18n withheld by the test' in err, (result, err)
assert host.cache() == (None, None) and events == []
result, err, events = captured(host.callback, 'WG.getFormattedLocalizedString', ('#menu:x', '{}'))
assert result is None and 'ImportError' in err and host.fallback_calls == []
FINDER.fail = False
assert FINDER.loads == 2 and 'helpers.i18n' not in sys.modules

# Translator: only a str result replaces the text, always as HTML.
i18n = None
checks = (
    ('#dialogs:EULA/title', 0, (True, True, u'最终用户许可协议')),
    ('#dialogs:EULA/buttons/apply', 0, (True, True, u'继续')),
    ('#dialogs:EULA/labels/agree', 0, (True, True, u'我接受')),
    ('#menu:loading/game', 0, (True, True, u'游戏载入中，请稍候...')),
    ('#dialogs:EULA/title', 1, (True, True, u'最终用户许可协议')),
    ('Plain text', 0, (False, False)),
    ('', 0, (False, False)),
    ('<font color="#ff0000">#dialogs:EULA/title</font>', 1, (False, False)),
    (utf8(u'中文 #dialogs:EULA/title'), 0, (False, False)),
    ('#dialogs:__review_unknown__', 0, (False, False)),
    ('#unknown_review_domain:any', 0, (False, False)),
)
for key, html, expected in checks:
    result, err, events = captured(host.translate, key, html)
    i18n = sys.modules['helpers.i18n']
    if expected[0]:
        assert result[:3] == expected and result[3] == len(expected[2]) + 1, (key, result)
        # The str helper result is the same text the owner applied.
        assert i18n.makeString(key.decode('utf-8')).decode('utf-8') == expected[2]
    else:
        assert result == expected, (key, result)
        assert not isinstance(i18n.makeString(key.decode('utf-8')), str), key
    assert err == '' and events == [], (key, err, events)
    report('translate %r html=%d' % (key, html), result)
assert FINDER.loads == 3

# A raising helper is reported by Script::ask and leaves the text.
RAISE.append(True)
result, err, events = captured(host.translate, '#unknown_raise_domain:any')
RAISE.pop()
assert result == (False, False) and 'RuntimeError: LOG_WARNING raised for the test' in err, (result, err)
assert any(line.startswith('ERROR  LOG_WARNING raised for the test') for line in events), events
report('translate raising', (result, events))

# WG.getFormattedLocalizedString: makeStringJSON's str result is the return value.
formatted = (
    (('#dialogs:barracksExpand/message', '{"price": "10", "count": "4"}'), u'您要花费10金币在兵营里增加4个空位么？'),
    (('#dialogs:EULA/title', '[]'), u'最终用户许可协议'),
    (('#dialogs:barracksExpand/message', 'not json'), u'#dialogs:barracksExpand/message'),
    (('#dialogs:barracksExpand/message', '[1,2]'), u'#dialogs:barracksExpand/message'),
    (('#dialogs:EULA/title', 'null'), u'#dialogs:EULA/title'),
    (('#unknown_review_domain:any', '{}'), u'#unknown_review_domain:any'),
    ((utf8(u'中文'), '{}'), u'中文'),
    (('#dialogs:EULA/title', '{}', 'extra'), u'最终用户许可协议'),
    ((1.5, None), u'1.5'),
)
for arguments, expected in formatted:
    result, err, events = captured(host.callback, 'WG.getFormattedLocalizedString', arguments)
    assert result == expected and type(result) is unicode, (arguments, result)
    assert err == '' and events == [], (arguments, err, events)
    report('formatted %r' % (arguments,), result)
assert host.fallback_calls == []
# The return value is the decoded helper str, not a re-translation.
assert i18n.makeStringJSON('#dialogs:barracksExpand/message', '{"price": "10", "count": "4"}').decode('utf-8') \
    == formatted[0][1]

# Every other name keeps the DAAPI/Python fallback route.
for name, arguments in (('WG.getGoldFormat', (1000,)), ('WG.getShortDateFormat', (1.5e9,)),
                        ('lobby.showWindow', ('x', 'y')), ('wg.getFormattedLocalizedString', ('a', 'b'))):
    result, err, events = captured(host.callback, name, arguments)
    assert result is None and err == '' and events == [], name
expected_fallback = [('WG.getGoldFormat', 1), ('WG.getShortDateFormat', 1), ('lobby.showWindow', 2),
                     ('wg.getFormattedLocalizedString', 2)]
assert host.fallback_calls == expected_fallback, host.fallback_calls
report('fallback', host.fallback_calls)

# SDK SetResult: the buffer holds the text and one counted terminator.
for value, html, expected in (('abc', 0, (True, False, u'abc', 4)), ('abc', 1, (True, True, u'abc', 4)),
                              ('', 0, (True, False, u'', 1)),
                              (utf8(u'\U0001f600x'), 0, (True, False, u'�x', 3)),
                              ('\xff\xfeA', 0, (True, False, u'��A', 4)),
                              (u'wide', 0, (True, False, u'wide', 5)), (None, 0, (False, False, None, 0))):
    result = host.set_result(value, html)
    assert result == expected, (value, result)
    report('set_result %r html=%d' % (value, html), result)

# FiniTimeJob releases both cached helpers while Python is alive.
make_string, make_json = host.cache()
assert make_string is i18n.makeString and make_json is i18n.makeStringJSON
counts = sys.getrefcount(i18n.makeString), sys.getrefcount(i18n.makeStringJSON)
del make_string, make_json
# The real fini loop also invokes a later-rung caption/formatted callback before
# Script::isFinalised becomes true. Neither may refill the released helper cache.
assert host.fini() == 1
assert host.cache() == (None, None)
assert (sys.getrefcount(i18n.makeString), sys.getrefcount(i18n.makeStringJSON)) == (counts[0] - 2, counts[1] - 2)
# After Script::fini nothing calls into Python or imports again.
del sys.modules['helpers.i18n']
loads = FINDER.loads
host.set_finalised(1)
result, err, events = captured(host.translate, '#dialogs:EULA/title')
assert result == (False, False) and err == '' and events == []
result, err, events = captured(host.callback, 'WG.getFormattedLocalizedString', ('#dialogs:EULA/title', '{}'))
assert result is None and err == '' and events == []
assert FINDER.loads == loads and 'helpers.i18n' not in sys.modules and host.cache() == (None, None)
host.set_finalised(0)

# PyMovieView teardown drops the handler with the delegate; the loader drops the translator.
live = host.live()
host.unregister()
assert host.live() == live - 2, (live, host.live())
host.uninstall()
assert host.live() == live - 3, (live, host.live())
print 'localization owner: pass'
