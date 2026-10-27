# Python 2.7 driver for stock_glyph_cache_host.cpp: argv[1] is pristine or patched,
# argv[2] the pinned scripts.pkg. Original PYC call sites run against the real binding.
import marshal
import math
import struct
import sys
import types
import zipfile

import GUI
import _host

MODE, PACKAGE = sys.argv[1], sys.argv[2]
GRAPHICS = 'scripts/client/gui/shared/utils/graphics.pyc'
OPTIONS = 'scripts/client/account_helpers/settings_core/options.pyc'
SCALE_MANAGER = 'scripts/client/account_helpers/settings_core/InterfaceScaleManager.pyc'
APPLICATION = 'scripts/client/gui/Scaleform/framework/application.pyc'


def code(path, *names):
    with zipfile.ZipFile(PACKAGE) as archive:
        current = marshal.loads(archive.read(path)[8:])
    for name in names:
        current = [c for c in current.co_consts if isinstance(c, types.CodeType) and c.co_name == name][0]
    return current


def function(path, owner, name, scope, defaults=None):
    return types.FunctionType(code(path, *((owner, name) if owner else (name,))), scope, name, defaults)


def raises(kind, text, call, *args):
    try:
        call(*args)
    except kind as error:
        assert str(error) == text, (str(error), text)
    else:
        raise AssertionError('no %s: %s' % (kind.__name__, text))


class Event(object):
    # Event.__call__: delegates run in subscription order; a failure stops the rest.
    def __init__(self):
        self.delegates = []

    def __iadd__(self, delegate):
        self.delegates.append(delegate)
        return self

    def __call__(self, *args):
        for delegate in self.delegates[:]:
            delegate(*args)


class Size(object):
    def __init__(self, width, height):
        self.width, self.height = width, height


log = []
screen = [(1920, 1080)]
GUI.screenResolution = lambda: screen[0]
fakeBigWorld = types.ModuleType('BigWorld')
fakeBigWorld.screenSize = lambda: screen[0]
graphicsScope = {'__builtins__': __builtins__, 'GUI': GUI, 'math': math, 'SCALE_PREFIX': ('auto', 'x%d'),
                 'getResolution': lambda: Size(1024, 768)}
graphics = types.ModuleType('graphics')
graphics.getInterfaceScalesList = function(GRAPHICS, None, 'getInterfaceScalesList', graphicsScope, (True,))


class MonitorSettings(object):
    setGlyphCache = function(GRAPHICS, 'MonitorSettings', 'setGlyphCache', graphicsScope, (1,))


g_monitorSettings = MonitorSettings()


class InterfaceScaleManager(object):
    getScaleOptions = staticmethod(function(SCALE_MANAGER, 'InterfaceScaleManager', 'getScaleOptions',
                                            {'__builtins__': __builtins__, 'graphics': graphics,
                                             'BigWorld': fakeBigWorld}))
    getScaleByIndex = function(SCALE_MANAGER, 'InterfaceScaleManager', 'getScaleByIndex',
                               {'__builtins__': __builtins__}, (True,))


class ConnectionManager(object):
    def __init__(self):
        self.onConnected, self.onDisconnected = Event(), Event()


class Options(object):
    def getSetting(self, name):
        return self.settings[name]


class SettingsCore(object):
    # getSetting returns the stored index; options.getSetting the setting object.
    def __init__(self):
        self.interfaceScale = InterfaceScaleManager()
        self.stored = {'interfaceScale': 0}
        self.options = Options()

    def getSetting(self, name):
        return self.stored[name]


class StorageSetting(object):
    def __init__(self, sectionName=None, isPreview=False):
        self.sectionName = sectionName

    def get(self):
        return settingsCore.stored[self.sectionName]


class EventDispatcher(object):
    def changeAppResolution(self, width, height, scale):
        log.append(('changeAppResolution', width, height, scale))


settingsCore, connectionMgr = SettingsCore(), ConnectionManager()
optionsScope = {'__builtins__': __builtins__, 'GUI': GUI, 'event_dispatcher': EventDispatcher(),
                'g_monitorSettings': g_monitorSettings}
InterfaceScaleSetting = type('InterfaceScaleSetting', (StorageSetting,), {
    'settingsCore': settingsCore, 'connectionMgr': connectionMgr,
    '__init__': function(OPTIONS, 'InterfaceScaleSetting', '__init__', optionsScope, (None, False)),
    'setSystemValue': function(OPTIONS, 'InterfaceScaleSetting', 'setSystemValue', optionsScope),
    'onConnected': function(OPTIONS, 'InterfaceScaleSetting', 'onConnected', optionsScope),
    'onDisconnected': function(OPTIONS, 'InterfaceScaleSetting', 'onDisconnected', optionsScope)})
optionsScope['InterfaceScaleSetting'] = InterfaceScaleSetting
interfaceScale = InterfaceScaleSetting('interfaceScale')
settingsCore.options.settings = {'interfaceScale': interfaceScale}


class Graphics(object):
    INTERFACE_SCALE = 'interfaceScale'


class Constants(object):
    GRAPHICS = Graphics


class Application(object):
    settingsCore = settingsCore
    updateScale = function(APPLICATION, 'SFApplication', 'updateScale',
                           {'__builtins__': __builtins__, 'settings_constants': Constants})


app = Application()
MISSING = 'GUI.wg_setGlyphCacheParams() is not defined'

if MODE == 'pristine':
    _host.createManager()
    # SFApplication.afterCreate L242 -> updateScale L449 -> setSystemValue L2734 -> L336.
    raises(AssertionError, MISSING, app.updateScale)
    assert log == [('changeAppResolution', 1920, 1080, 1.0)], log
    raises(AssertionError, MISSING, connectionMgr.onConnected)
    _host.addMovie('lobby')
    _host.draw()
    assert _host.calls() == ['glyph.initialize 1 1024 1024 48 vector=0', 'renderer.begin',
                             'renderer.display lobby', 'renderer.end'], 'stock owner'
    _host.destroyManager()
    print('pristine: MonitorSettings.setGlyphCache raises AssertionError at graphics.py:336')
    sys.exit(0)

assert sorted(name for name in dir(GUI) if not name.startswith('_')) == ['screenResolution',
                                                                          'wg_setGlyphCacheParams']
DEFAULTS = (2, 256, 512, 1.0, 500, 0.25, 0.045, 0.01, 1.0)


def stored(numTextures, width, height, slot, vector=1):
    # #1513 Manager+0xac..0xe2 (0x37 bytes; 0xe3 is padding the original leaves unset).
    return struct.pack('<7IfI4f3B', width, height, numTextures, slot, 2, 256, 512, 1.0, 500, 0.25, 0.045,
                       0.01, 1.0, 1, vector, 1)


def applied(numTextures, width, height, slot, initializations, vector=1):
    want = (width, height, numTextures, slot) + DEFAULTS[:3]
    got = _host.applied()
    assert got[:7] == want and got[8] == 500 and got[13:16] == (1, vector, 1), (got, want)
    assert [round(value, 6) for value in got[7:8] + got[9:13]] == [1.0, 0.25, 0.045, 0.01, 1.0], got
    assert got[16] == initializations, (got, initializations)


def frame():
    _host.draw()
    return _host.calls()


# Before the manager exists the original dereferences a null pointer; production raises.
NO_MANAGER = 'GUI.wg_setGlyphCacheParams: the Scaleform manager does not exist'
raises(RuntimeError, NO_MANAGER, g_monitorSettings.setGlyphCache, 1.0)

_host.createManager()
assert _host.calls() == ['glyph.initialize 1 1024 1024 48 vector=0']
assert _host.pending() == (0, stored(1, 1024, 1024, 48, vector=0))

# SFApplication.afterCreate L242 before any movie: the call only stores the values.
app.updateScale()
assert log.pop() == ('changeAppResolution', 1920, 1080, 1.0)
assert _host.calls() == [] and _host.pending() == (1, stored(1, 1024, 1024, 48))
# The next frame applies them once, before BeginFrame.
_host.addMovie('lobby')
assert frame() == ['glyph.initialize 1 1024 1024 48 vector=1', 'renderer.begin', 'renderer.display lobby',
                   'renderer.end']
assert _host.pending()[0] == 0
applied(1, 1024, 1024, 48, 2)
assert frame() == ['renderer.begin', 'renderer.display lobby', 'renderer.end']

# A resolution with a x2 option: the "auto" index 0 resolves to 2.0, then 4.0.
screen[0] = (2048, 1536)
interfaceScale.setSystemValue(0)
screen[0] = (4096, 3072)
interfaceScale.setSystemValue(0)
assert log == [('changeAppResolution', 2048, 1536, 2.0), ('changeAppResolution', 4096, 3072, 4.0)], log
del log[:]
# Superseded values coalesce: one SetParams with the latest values.
assert frame() == ['glyph.initialize 1 4096 4096 48 vector=1', 'renderer.begin', 'renderer.display lobby',
                   'renderer.end']
applied(1, 4096, 4096, 48, 3)

# connect -> onConnected -> setSystemValue(stored index); disconnect -> index 0.
settingsCore.stored['interfaceScale'] = 1
connectionMgr.onConnected()
assert log.pop() == ('changeAppResolution', 4096, 3072, 1.0)
assert _host.pending() == (1, stored(1, 1024, 1024, 48))
# A lost device keeps the values pending; the next good frame applies them.
_host.setDevice(0)
assert frame() == [] and _host.pending()[0] == 1
_host.setDevice(1)
assert frame()[0] == 'glyph.initialize 1 1024 1024 48 vector=1'
connectionMgr.onDisconnected()
assert log.pop() == ('changeAppResolution', 4096, 3072, 4.0)
assert frame()[0] == 'glyph.initialize 1 4096 4096 48 vector=1'
applied(1, 4096, 4096, 48, 5)

# A device reset re-creates the cache from the applied values without a new request.
_host.resetDevice()
assert _host.calls() == ['hal.prepare', 'glyph.destroy', 'hal.restore', 'glyph.initialize 1 4096 4096 48 vector=1']
assert _host.pending()[0] == 0
applied(1, 4096, 4096, 48, 6)
# The same values again still re-initialise, as SetParams always does.
interfaceScale.setSystemValue(0)
log.pop()
assert frame()[0] == 'glyph.initialize 1 4096 4096 48 vector=1'

# The engine PY_AUTO arguments: four optional unsigned values through setData(uint&).
GUI.wg_setGlyphCacheParams()
assert _host.pending() == (1, stored(1, 1024, 1024, 48))
GUI.wg_setGlyphCacheParams(0, 64, 32, 16)
assert _host.pending() == (1, stored(0, 64, 32, 16))
GUI.wg_setGlyphCacheParams(True, 2.9, 1024L, 0xffffffffL)
assert _host.pending() == (1, stored(1, 2, 1024, 0xffffffff))
GUI.wg_setGlyphCacheParams(3, -2.5)
assert _host.pending() == (1, stored(3, 0xfffffffe, 1024, 48))
COUNT = 'wg_setGlyphCacheParams() expects between 0 and 4 arguments of types unsigned, unsigned, unsigned, unsigned'
for args, error, text in (((1, 2, 3, 4, 5), TypeError, COUNT),
                          ((-1,), TypeError, '() argument 1 optionally must be set to an uint'),
                          ((1, 'x'), TypeError, '() argument 2 optionally must be set to an uint'),
                          ((1, 2, None), TypeError, '() argument 3 optionally must be set to an uint'),
                          ((1, 2, 3, 2 ** 32), TypeError, '() argument 4 optionally must be set to an uint'),
                          ((1, 2, 3, -1L), TypeError, '() argument 4 optionally must be set to an uint')):
    raises(error, text, GUI.wg_setGlyphCacheParams, *args)
    # A rejected call leaves the previous request pending and unchanged.
    assert _host.pending() == (1, stored(3, 0xfffffffe, 1024, 48)), args
assert _host.calls() == []
GUI.wg_setGlyphCacheParams(2, 512, 256, 64)
assert frame()[0] == 'glyph.initialize 2 512 256 64 vector=1'

# Teardown: a pending request dies with the manager; a new manager starts clean.
GUI.wg_setGlyphCacheParams(1, 2048, 2048)
_host.destroyManager()
raises(RuntimeError, NO_MANAGER, g_monitorSettings.setGlyphCache, 2.0)
_host.createManager()
assert _host.calls() == ['glyph.initialize 1 1024 1024 48 vector=0'] and _host.pending()[0] == 0
_host.addMovie('battle')
assert frame() == ['renderer.begin', 'renderer.display battle', 'renderer.end']
g_monitorSettings.setGlyphCache()
assert frame()[0] == 'glyph.initialize 1 1024 1024 48 vector=1'
_host.destroyManager()
print('patched: original setGlyphCache/setSystemValue/onConnected/onDisconnected/updateScale reach '
      'GlyphCache::SetParams before the next frame')
