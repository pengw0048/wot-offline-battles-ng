"""Exercise real CPython sets and exact ordinary battle PYC producers."""
from __future__ import print_function
import gc
import __builtin__
import json
import marshal
import sys
import types
import weakref
import zipfile
from _flash_collections import convert, invoke, invokeCount


def find(code, names):
    for name in names:
        code = next(c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == name)
    return code


def dictionary_contracts():
    class Key(object):
        def __str__(self):
            return 'custom-key'
    values = {1: 'arcade', 2: 'sniper', u'ascii': 3, None: 4, (7, 8): 5, Key(): 6}
    assert convert(values) == ('object', {
        '1': ('string', 'arcade'), '2': ('string', 'sniper'), 'ascii': ('number', 3.),
        'None': ('number', 4.), '(7, 8)': ('number', 5.), 'custom-key': ('number', 6.)})
    class Text(str):
        def __str__(self):
            return self
    key = Text('borrowed-subclass')
    values = {key: {'nested': {False: 2}}}
    baseline = sys.getrefcount(key)
    for unused in range(100):
        assert convert(values) == ('object', {'borrowed-subclass': (
            'object', {'nested': ('object', {'False': ('number', 2.)})})})
    assert sys.getrefcount(key) == baseline
    class MutatingKey(object):
        def __str__(self):
            values.clear()
            return 'snapshot'
    values = {MutatingKey(): 7}
    assert convert(values) == ('object', {'snapshot': ('number', 7.)}) and values == {}
    class BrokenKey(object):
        def __str__(self):
            raise ValueError('key conversion failed')
    before = invokeCount()
    for value in ({BrokenKey(): 7}, {'nested': {BrokenKey(): 8}}):
        try:
            invoke(value)
        except ValueError as error:
            assert str(error) == 'key conversion failed'
        else:
            raise AssertionError('Key conversion exception escaped its call')
    assert invokeCount() == before
    invoke({1: 2})
    assert invokeCount() == before + 1
    print('CPython dictionary conversion: typed keys, nested values, snapshot, lifetime and call errors passed')


def crosshair_consumer(client):
    package = zipfile.ZipFile(client + '/res/packages/scripts.pkg')
    def code(member, names):
        return find(marshal.loads(package.read(member)[8:]), names)
    plugin_member = 'scripts/client/gui/Scaleform/daapi/view/battle/shared/crosshair/plugins.pyc'
    namespace = {'__name__': 'crosshair_fixture', '__builtins__': __builtin__,
                 'LOG_CURRENT_EXCEPTION': lambda: None}
    event = type('Event', (object,), types.FunctionType(code('scripts/common/Event.pyc', ('Event',)), namespace)())
    ids = types.FunctionType(code('scripts/client/gui/battle_control/battle_constants.pyc', ('CROSSHAIR_VIEW_ID',)), namespace)()
    aim = types.FunctionType(code('scripts/client/account_helpers/settings_core/settings_constants.pyc', ('AIM',)), namespace)()
    mapping = {aim['ARCADE']: ids['ARCADE'], aim['SNIPER']: ids['SNIPER']}
    assert set(mapping.values()) == set([1, 2])
    namespace['_SETTINGS_KEY_TO_VIEW_ID'] = mapping
    namespace['_SETTINGS_KEYS'] = set(mapping)
    namespace['_makeSettingsVO'] = types.FunctionType(code(plugin_member, ('_makeSettingsVO',)), namespace)
    class Settings(object):
        onSettingsChanged = event()
        def getSetting(self, mode):
            return dict((name, 50) for name in ('centralTag', 'centralTagType', 'net', 'netType',
                'reloader', 'condition', 'cassette', 'reloaderTimer', 'zoomIndicator', 'gunTag',
                'gunTagType', 'mixing', 'mixingType'))
    class Parent(object):
        def setSettings(self, vo):
            invoke(vo)
    class Existing(object):
        def onSettingsChanged(self, diff):
            pass
    class Plugin(object):
        settingsCore = Settings()
        _parentObj = Parent()
    plugin = Plugin()
    Plugin._SettingsPlugin__onSettingsChanged = types.FunctionType(code(
        plugin_member, ('SettingsPlugin', '__onSettingsChanged')), namespace)
    start = types.FunctionType(code(plugin_member, ('SettingsPlugin', 'start')), namespace)
    stop = types.FunctionType(code(plugin_member, ('SettingsPlugin', 'stop')), namespace)
    existing = Existing()
    plugin.settingsCore.onSettingsChanged += existing.onSettingsChanged
    before = invokeCount()
    start(plugin)
    start(plugin)
    assert len(plugin.settingsCore.onSettingsChanged._Event__delegates) == 2
    plugin.settingsCore.onSettingsChanged({'arcade': {}})
    assert invokeCount() == before + 3
    stop(plugin)
    plugin.settingsCore.onSettingsChanged({'sniper': {}})
    assert invokeCount() == before + 3
    print('Original #1513 crosshair start, Event subscribe/deduplicate/notify/unsubscribe passed')


def contracts():
    dictionary_contracts()
    assert convert(set()) == ('array', [])
    assert sorted(convert(set([3, 1, 2]))[1]) == [('number', 1.), ('number', 2.), ('number', 3.)]
    assert convert({'tags': set(), 'nested': [set([7])]}) == (
        'object', {'tags': ('array', []), 'nested': ('array', [('array', [('number', 7.)])])})
    class Child(set):
        pass
    assert convert(Child([9])) == ('array', [('number', 9.)])
    assert convert((1, [2], {'three': 3})) == ('array', [
        ('number', 1.), ('array', [('number', 2.)]), ('object', {'three': ('number', 3.)})])
    class Iterable(object):
        def __iter__(self):
            return iter([1])
    for unsupported in (frozenset([1]), iter([1]), (v for v in [1]), Iterable(), object()):
        assert convert(unsupported) == ('undefined',), type(unsupported)
    # The original takes the subclass iterator, rather than silently reading its base table.
    class Ordered(set):
        def __iter__(self):
            return iter([4, 2, 8])
    assert convert(Ordered()) == ('array', [('number', 4.), ('number', 2.), ('number', 8.)])
    class Item(object):
        pass
    item = Item()
    baseline = sys.getrefcount(item)
    values = Ordered()
    class RetainingIterator(object):
        def __init__(self):
            self.pending = True
        def __iter__(self):
            return self
        def next(self):
            if not self.pending:
                raise StopIteration
            self.pending = False
            return item
    references = []
    class Lifetime(set):
        def __iter__(self):
            it = RetainingIterator()
            references.append(weakref.ref(it))
            return it
    for unused in range(100):
        assert convert(Lifetime()) == ('array', [('undefined',)])
    gc.collect()
    assert sys.getrefcount(item) == baseline
    assert all(ref() is None for ref in references)
    class Broken(set):
        def __iter__(self):
            yield item
            raise ValueError('iteration failed')
    class BrokenStart(set):
        def __iter__(self):
            raise RuntimeError('cannot iterate')
    for kind, error, message in ((Broken, ValueError, 'iteration failed'),
                                 (BrokenStart, RuntimeError, 'cannot iterate')):
        try:
            convert(kind())
        except error as caught:
            assert str(caught) == message
        else:
            raise AssertionError('Iterator error was swallowed')
    gc.collect()
    assert sys.getrefcount(item) == baseline
    print('CPython collection conversion: positive, negative, recursive and lifetime contracts passed')


def consumers(client):
    crosshair_consumer(client)
    package = zipfile.ZipFile(client + '/res/packages/scripts.pkg')
    def code(member, names):
        return find(marshal.loads(package.read('scripts/client/' + member + '.pyc')[8:]), names)
    class Page(object):
        def as_setComponentsVisibilityS(self, visible, hidden):
            self.values = convert(visible), convert(hidden)
    page = Page()
    update = types.FunctionType(code('gui/Scaleform/daapi/view/battle/shared/page',
        ('SharedPage', '_setComponentsVisibility')), {'LOG_DEBUG': lambda *a: None, '__builtins__': __builtin__}, None, (None, None))
    update(page, hidden=set(['playersPanel', 'minimap']))
    assert page.values[0] == ('array', [])
    assert sorted(page.values[1][1]) == [('string', 'minimap'), ('string', 'playersPanel')]
    update(page, visible=set(['minimap']))
    assert page.values == (('array', [('string', 'minimap')]), ('array', []))
    class IGR(object):
        BASE, PREMIUM = 1, 2
    class Tags(object):
        IGR_BASE, IGR_PREMIUM = 'igrBase', 'igrPremium'
    class Storage(object):
        def getUser(self, accountDBID):
            return None
    class Context(object):
        usersStorage = Storage()
    context = Context()
    g = {'IGR_TYPE': IGR, 'USER_TAG': Tags, '__builtins__': __builtin__}
    context.addTagByIGRType = types.FunctionType(code(
        'gui/Scaleform/daapi/view/battle/shared/stats_exchage/broker',
        ('ExchangeCtx', 'addTagByIGRType')), g)
    tags = types.FunctionType(code('gui/Scaleform/daapi/view/battle/shared/stats_exchage/broker',
        ('ExchangeCtx', 'getUserTags')), g)
    for igr, expected in ((0, []), (1, [('string', 'igrBase')]), (2, [('string', 'igrPremium')])):
        assert convert(tags(context, 42, igr)) == ('array', expected)
    print('Original #1513 SharedPage and stats ExchangeCtx PYC collection producers passed')


if len(sys.argv) > 1 and sys.argv[1] == '--crosshair-only':
    crosshair_consumer(sys.argv[2])
elif len(sys.argv) > 1 and sys.argv[1] == '--dictionary-only':
    dictionary_contracts()
else:
    contracts()
    if len(sys.argv) > 1:
        consumers(sys.argv[1])
