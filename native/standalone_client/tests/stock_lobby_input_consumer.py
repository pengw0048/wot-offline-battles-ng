# Python 2.7 driver for stock_lobby_input_host.cpp: argv[1] is pristine or patched,
# argv[2] the pinned scripts.pkg. Original PYC call sites run against the real binding.
import marshal
import sys
import types
import zipfile

import BigWorld
import _input

MODE, PACKAGE = sys.argv[1], sys.argv[2]
KEY_ESCAPE, KEY_RETURN, KEY_A, KEY_B, KEY_F2 = 0x01, 0x1C, 0x1E, 0x30, 0x3C
KEY_LSHIFT, KEY_RSHIFT, KEY_LCONTROL, KEY_RCONTROL = 0x2A, 0x36, 0x1D, 0x9D
KEY_LALT, KEY_RALT = 0x38, 0xB8
SHIFT, CTRL, ALT = 1, 2, 4
KEY_DOWN, KEY_UP = 5, 6


def code(path, *names):
    with zipfile.ZipFile(PACKAGE) as archive:
        current = marshal.loads(archive.read(path)[8:])
    for name in names:
        current = [c for c in current.co_consts if isinstance(c, types.CodeType) and c.co_name == name][0]
    return current


def function(path, owner, name, scope):
    return types.FunctionType(code(path, owner, name), scope)


def raises(kind, text, call, *args):
    try:
        call(*args)
    except kind as error:
        assert str(error) == text, (str(error), text)
    else:
        raise AssertionError('no %s: %s' % (kind.__name__, text))


def key(code, state=0, modifiers=0):
    return _input.translate(code, state, modifiers)[2]


class Recorder(object):
    def __init__(self, name, log):
        self.__dict__['_name'], self.__dict__['_log'] = name, log

    def __setattr__(self, name, value):
        self._log.append('%s.%s=%r' % (self._name, name, value))

    def __getattr__(self, name):
        if name == 'position':
            return Recorder(self._name + '.position', self._log)
        return lambda *args: self._log.append('%s.%s%r' % (self._name, name, args))


ENTRY = 'scripts/client/gui/Scaleform/'
log = []
lobby = function(ENTRY + 'lobby_entry.pyc', 'LobbyEntry', '_setup',
                 {'BigWorld': BigWorld, 'SCALEFORM_SWF_PATH_V3': 'gui/flash', 'True': True})
self = Recorder('lobby', log)
self.__dict__['movie'] = Recorder('movie', log)

if MODE == 'pristine':
    # The dbb0e3e0 failure at lobby_entry.py:145; the public names are not #1513 names.
    raises(AttributeError, "'module' object has no attribute 'wg_setRedefineKeysMode'", lobby, self)
    assert log == ["movie.backgroundAlpha=0.0", "movie.setFocussed('gui/flash',)"], log
    assert hasattr(BigWorld, 'setRedefineKeysMode') and hasattr(BigWorld, 'isCapsLockOn')
    print('pristine: lobby_entry.py:145 raises AttributeError for wg_setRedefineKeysMode')
    sys.exit(0)

assert sorted(name for name in dir(BigWorld) if not name.startswith('_')) == [
    'wg_getLangCode', 'wg_isCapsLockOn', 'wg_isNumLockOn', 'wg_isScrollLockOn', 'wg_setRedefineKeysMode']

# 00aaed30: exactly one argument through Script::setData(bool&); the stored byte starts false.
raises(TypeError, 'wg_setRedefineKeysMode() expects 1 argument of type bool', BigWorld.wg_setRedefineKeysMode)
raises(TypeError, 'wg_setRedefineKeysMode() expects 1 argument of type bool',
       BigWorld.wg_setRedefineKeysMode, True, False)
for bad in (1L, 1.0, None, 'yes', u'true', (True,)):
    raises(TypeError, '() argument 1 must be set to a bool', BigWorld.wg_setRedefineKeysMode, bad)
assert key(KEY_B) == 0 and key(KEY_A) == 65 and key(KEY_F2) == 0
for value, expected in ((True, 66), (False, 0), (2, 66), (0, 0), (-1, 66), ('TRUE', 66), ('false', 0),
                        ('True', 66), ('FALSE', 0)):
    assert BigWorld.wg_setRedefineKeysMode(value) is None
    assert key(KEY_B) == expected, (value, key(KEY_B))
# A rejected conversion leaves the previous mode in place.
BigWorld.wg_setRedefineKeysMode(True)
raises(TypeError, '() argument 1 must be set to a bool', BigWorld.wg_setRedefineKeysMode, 0.0)
assert key(KEY_B) == 66
BigWorld.wg_setRedefineKeysMode(False)

# lobby_entry.py:145 selects the redefine table for every later key event.
assert lobby(self) is None
assert log == ["movie.backgroundAlpha=0.0", "movie.setFocussed('gui/flash',)"], log
assert key(KEY_B) == 66 and key(KEY_F2) == 113 and key(KEY_LALT) == 18
# Right-hand modifiers keep their own codes. Shift/Ctrl suppress only their exact
# generic code; Alt never suppresses AltPressed.
assert _input.translate(KEY_LSHIFT, 0, SHIFT) == (KEY_DOWN, 0, 16, 0, 0, 0)
assert _input.translate(KEY_RSHIFT, 0, SHIFT) == (KEY_DOWN, SHIFT, 161, 0, 0, 0)
assert _input.translate(KEY_LCONTROL, 0, CTRL) == (KEY_DOWN, 0, 17, 0, 0, 0)
assert _input.translate(KEY_RCONTROL, 0, CTRL) == (KEY_DOWN, CTRL, 163, 0, 0, 0)
assert _input.translate(KEY_LALT, 0, ALT) == (KEY_DOWN, ALT, 18, 0, 0, 0)
assert _input.translate(KEY_RALT, 0, ALT) == (KEY_DOWN, ALT, 165, 0, 0, 0)
# Down, auto-repeat and release use the event state; held modifiers come from the event.
assert _input.translate(KEY_B, 0, SHIFT | CTRL | ALT)[:3] == (KEY_DOWN, 7, 66)
assert _input.translate(KEY_B, 7, 0)[:3] == (KEY_DOWN, 0, 66)
assert _input.translate(KEY_B, -1, SHIFT)[:3] == (KEY_UP, SHIFT, 66)
# Lock toggles are read from the OS state on every translated event.
_input.calls()
_input.setKeyState(0x90, 1)
_input.setKeyState(0x14, -127)
_input.setKeyState(0x91, -128)
assert _input.translate(KEY_A, 0, 0)[1] == 0x10 | 0x08
assert _input.calls() == ['GetKeyState:144', 'GetKeyState:20', 'GetKeyState:145']
for vk in (0x90, 0x14, 0x91):
    _input.setKeyState(vk, 0)

# battle_entry.py:165 returns to the default table; Flash's wg_inputKeyMode is another owner.
battle = function(ENTRY + 'battle_entry.pyc', 'BattleEntry', '_setup',
                  {'BigWorld': BigWorld, 'SCALEFORM_SWF_PATH_V3': 'gui/flash', 'DEPTH_OF_Battle': 0.1,
                   'False': False})
log[:] = []
entry = Recorder('battle', log)
entry.__dict__.update(component=Recorder('component', log), movie=Recorder('movie', log))
assert battle(entry) is None
assert log[0] == 'component.wg_inputKeyMode=1', log
assert key(KEY_B) == 0 and key(KEY_LALT) == 0
assert _input.translate(KEY_RSHIFT, 0, SHIFT) == (KEY_DOWN, SHIFT, 161, 0, 0, 0)
assert _input.translate(KEY_RCONTROL, 0, CTRL) == (KEY_DOWN, CTRL, 163, 0, 0, 0)

# SettingsWindow toggles the same byte on populate/dispose when its context allows it.
SETTINGS = ENTRY + 'daapi/view/common/settings/SettingsWindow.pyc'


class Base(object):
    def _populate(self):
        log.append('super._populate')

    def _dispose(self):
        log.append('super._dispose')


class Event(object):
    def __iadd__(self, handler):
        log.append('event+=')
        return self

    def __isub__(self, handler):
        log.append('event-=')
        return self


class Vibro(object):
    onConnect = Event()
    onDisconnect = Event()


class Resetters(object):
    def add(self, handler):
        log.append('resetters.add')

    def discard(self, handler):
        log.append('resetters.discard')


def neighbour(*args):
    log.append('wg_setAdapterOrdinalNotifyCallback')


BigWorld.wg_setAdapterOrdinalNotifyCallback = neighbour  # Graphics-settings owner, not this task.
scope = {'BigWorld': BigWorld, 'True': True, 'False': False, 'super': super,
         'VibroManager': types.ModuleType('VibroManager'), 'g_guiResetters': Resetters()}
scope['VibroManager'].g_instance = Vibro()


class SettingsWindow(Base):
    def __init__(self, enabled):
        self._SettingsWindow__redefinedKeyModeEnabled = enabled
        self.params = self

    def getMonitorSettings(self):
        return 'monitor'

    def _update(self):
        log.append('update')

    def onVibroManagerConnect(self):
        pass

    onVibroManagerDisconnect = onRecreateDevice = onVibroManagerConnect

    def __getattr__(self, name):
        return lambda *args: log.append(name)


scope['SettingsWindow'] = SettingsWindow
populate = function(SETTINGS, 'SettingsWindow', '_populate', scope)
dispose = function(SETTINGS, 'SettingsWindow', '_dispose', scope)
for enabled, during in ((True, 66), (False, 0)):
    window = SettingsWindow(enabled)
    assert key(KEY_B) == 0
    populate(window)
    assert key(KEY_B) == during, (enabled, key(KEY_B))
    dispose(window)
    assert key(KEY_B) == 0
del BigWorld.wg_setAdapterOrdinalNotifyCallback

# Lock getters: argument count and the low (toggle) bit only.
raises(TypeError, 'wg_isCapsLockOn() takes no arguments.', BigWorld.wg_isCapsLockOn, 1)
for state, expected in ((0, False), (1, True), (-128, False), (-127, True), (2, False)):
    for vk, getter in ((0x14, BigWorld.wg_isCapsLockOn), (0x90, BigWorld.wg_isNumLockOn),
                       (0x91, BigWorld.wg_isScrollLockOn)):
        _input.setKeyState(vk, state)
        _input.calls()
        assert getter() is expected, (vk, state)
        assert _input.calls() == ['GetKeyState:%d' % vk]
        _input.setKeyState(vk, 0)

# LoginView.__checkUserInputState polls caps lock and the live input language.
raises(TypeError, 'wg_getLangCode() takes no arguments.', BigWorld.wg_getLangCode, 1)
_input.setLocaleName(0x0409, u'ENU')
_input.setLocaleName(0x0419, u'RUS')
_input.setLocaleName(0x0804, u'CHS')
_input.setLocaleName(0x0c0c, u'FRC')
_input.setLocaleName(0x0001, u'A')
_input.setLocaleName(0x0002, u'\u00e9\u00e9')
_input.calls()
assert BigWorld.wg_getLangCode() == 'EN' and type(BigWorld.wg_getLangCode()) is str
assert _input.calls()[:5] == ['GetKeyboardLayout:0', 'GetLocaleInfoW:1033:3:0', 'GetLocaleInfoW:1033:3:4',
                              'WideCharToMultiByte:0', 'WideCharToMultiByte:4']
for layout, expected in ((0x04190419, 'RU'), (0xf0080804, 'CH'), (0x040c0c0c, 'FR'), (0x0001, 'A'),
                         (0x0002, '\xc3\xa9')):
    _input.setLayout(layout)
    assert BigWorld.wg_getLangCode() == expected, (hex(layout), BigWorld.wg_getLangCode())
# Containment of the original's unfilled-buffer path: an unavailable locale is ''.
_input.setLayout(0x04091000)
_input.calls()
assert BigWorld.wg_getLangCode() == ''
assert _input.calls() == ['GetKeyboardLayout:0', 'GetLocaleInfoW:4096:3:0']
_input.setLayout(0x04090409)
for failure, calls in ((1, ['GetLocaleInfoW:1033:3:0']),
                       (2, ['GetLocaleInfoW:1033:3:0', 'GetLocaleInfoW:1033:3:4']),
                       (3, ['GetLocaleInfoW:1033:3:0', 'GetLocaleInfoW:1033:3:4', 'WideCharToMultiByte:0'])):
    _input.setLocaleFailure(failure)
    assert BigWorld.wg_getLangCode() == '', failure
    assert _input.calls() == ['GetKeyboardLayout:0'] + calls, (failure, _input.calls())
    _input.setLocaleFailure(0)
    assert BigWorld.wg_getLangCode() == 'EN'
    _input.calls()

LOGIN = 'scripts/client/gui/Scaleform/daapi/view/login/LoginView.pyc'
callbacks = []
BigWorld.callback = lambda delay, handler: callbacks.append((delay, handler)) or len(callbacks)
check = function(LOGIN, 'LoginView', '__checkUserInputState', {'BigWorld': BigWorld})


class Login(object):
    _LoginView__capsLockState = None
    _LoginView__lang = None

    def as_setCapsLockStateS(self, value):
        log.append(('caps', value))

    def as_setKeyboardLangS(self, value):
        log.append(('lang', value))

    def _LoginView__checkUserInputState(self):
        check(self)


login = Login()
log[:] = []
_input.setLayout(0x04090409)
check(login)
_input.setKeyState(0x14, 1)
_input.setLayout(0x04190419)
callbacks[-1][1]()
callbacks[-1][1]()
_input.setKeyState(0x14, 0)
callbacks[-1][1]()
assert log == [('caps', False), ('lang', 'EN'), ('caps', True), ('lang', 'RU'), ('caps', False)], log
assert [delay for delay, unused in callbacks] == [0.1] * 4 and login._LoginView__capsLockCallbackID == 4

# Each failed language query delivers '' and schedules the next poll; later
# Caps Lock and language changes still arrive.
steps = (
    ('unavailable LCID', lambda: _input.setLayout(0x04091000), 1, ('lang', '')),
    ('recover', lambda: _input.setLayout(0x04090409), 0, ('lang', 'EN')),
    ('length query', None, 1, ('lang', '')),
    ('recover', None, 0, ('lang', 'EN')),
    ('text query', None, 2, ('lang', '')),
    ('recover', lambda: _input.setLayout(0x04190419), 0, ('lang', 'RU')),
    ('conversion', None, 3, ('lang', '')),
    ('recover', lambda: _input.setLayout(0x04090409), 0, ('lang', 'EN')))
for index, (name, change, failure, delivered) in enumerate(steps):
    log[:] = []
    if change:
        change()
    _input.setLocaleFailure(failure)
    caps = index % 2 == 0
    _input.setKeyState(0x14, int(caps))
    before = len(callbacks)
    callbacks[-1][1]()
    assert len(callbacks) == before + 1 and callbacks[-1][0] == 0.1, name
    assert login._LoginView__capsLockCallbackID == len(callbacks), name
    assert log == [('caps', caps), delivered], (name, log)
_input.setLocaleFailure(0)
_input.setKeyState(0x14, 1)
log[:] = []
callbacks[-1][1]()
assert log == [('caps', True)] and len(callbacks) == 13, (log, len(callbacks))
_input.setKeyState(0x14, 0)
print('patched: wg_ names, setter-to-translator tables, right-hand codes, lock bits, live language '
      'and LoginView polling across failed language queries')
