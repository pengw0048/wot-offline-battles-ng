# Python 2.7, run inside stock_screener_host: argv = [this, scripts.pkg, mode]
# mode 'absent' runs the original PYC without BigWorld.Screener; 'port' runs it
# over the production binding.
import imp
import marshal
import sys
import types
import zipfile

import _screener_host as host

PACKAGE, MODE = sys.argv[1], sys.argv[2]
CALLS = []


def module(name, **attributes):
    result = sys.modules.get(name) or imp.new_module(name)
    result.__dict__.update(attributes)
    sys.modules[name] = result
    if '.' in name:
        parent, child = name.rsplit('.', 1)
        setattr(module(parent), child, result)
    return result


class Interface(object):
    pass


module('BigWorld')
module('debug_utils', LOG_DEBUG=lambda *args: None)
module('helpers.i18n', makeString=lambda key: 'text:' + key)
module('helpers.dependency', descriptor=lambda interface: None)
module('gui.shared', EVENT_BUS_SCOPE=type('Scope', (), {'LOBBY': 'lobby'}))
module('gui.shared.events', LobbySimpleEvent=lambda name: ('event', name))
module('gui.shared.utils', getPlayerDatabaseID=lambda: CALLS.append('getPlayerDatabaseID') or DBID[0])
module('gui.battle_control.arena_info.interfaces', IArenaVehiclesController=Interface)
module('skeletons.gui.battle_session', IBattleSessionProvider=Interface)
module('skeletons.gui.game_control', IScreenCastController=type('IScreenCastController', (object,), {}))
LobbySimpleEvent = sys.modules['gui.shared.events'].LobbySimpleEvent
LobbySimpleEvent.WAITING_SHOWN = 'waitingShown'
DBID = [0]


def load(member, name):
    archive = zipfile.ZipFile(PACKAGE)
    code = marshal.loads(archive.read(member)[8:])
    result = module(name)
    exec code in result.__dict__
    return result


class View(object):
    def showS(self, text):
        CALLS.append(('view.show', text))

    def setCallback(self, callback):
        CALLS.append('view.setCallback')

    def close(self):
        CALLS.append('view.close')

    def fireEvent(self, event, scope):
        CALLS.append(('view.event', event, scope))


def expect_error(kind, text, function, *args):
    try:
        function(*args)
    except kind as error:
        assert str(error) == text, (str(error), text)
        return
    raise AssertionError('%s%r did not raise %s' % (function, args, kind.__name__))


BigWorld = sys.modules['BigWorld']
if MODE == 'port':
    assert host.initScripts() == 1
    assert type(BigWorld.Screener).__name__ == 'PyWatermarkersWrapper'
    assert host.state()['basicsize'] > 0
Waiting = load('scripts/client/gui/Scaleform/Waiting.pyc', 'gui.Scaleform.Waiting').Waiting
screencast = load('scripts/client/gui/game_control/screencast_controller.pyc',
                  'gui.game_control.screencast_controller').ScreenCastController
Waiting.setWainingViewGetter(View)
stack = lambda: [entity.message for entity in Waiting._Waiting__waitingStack]

if MODE == 'absent':
    message = "'module' object has no attribute 'Screener'"
    expect_error(AttributeError, message, Waiting.show, 'loadHangarSpace')
    assert stack() == [] and CALLS == []
    expect_error(AttributeError, message, Waiting.close)
    controller = screencast()
    expect_error(AttributeError, message, controller.onDisconnected)
    DBID[0] = 7
    expect_error(AttributeError, message, controller.onLobbyInited, None)
    assert not controller._isSet
    print('absent: show/close/onDisconnected/onLobbyInited raise AttributeError before any stack change')
    sys.exit(0)

# Account -> Hangar: hangar space, lobby sub-views and a vehicle update nest and unwind.
Waiting.show('loadHangarSpace')
Waiting.show('loadPage')
Waiting.show('loadPage', isSingle=True)
Waiting.show('updateVehicle')
assert stack() == ['loadHangarSpace', 'loadPage', 'updateVehicle'], stack()
assert Waiting.isVisible()
Waiting.hide('loadPage')
Waiting.hide('loadHangarSpace')
assert stack() == ['updateVehicle'] and Waiting.isVisible()
Waiting.hide('updateVehicle')
assert stack() == [] and not Waiting.isVisible()
assert CALLS.count('view.close') == 1, CALLS
# Repeated and redundant transitions settle; close on an empty stack is harmless.
for cycle in range(3):
    Waiting.show('updateCurrentVehicle')
    Waiting.hide('updateCurrentVehicle')
Waiting.hide('neverShown')
Waiting.close()
Waiting.close()
Waiting.show('sinhronize')
Waiting.suspend()
assert Waiting.isSuspended() and stack() == []
# resume re-shows the suspended entities without restoring the stack; rollback closes again.
Waiting.resume()
assert stack() == [] and Waiting.isVisible() and not Waiting.isSuspended()
Waiting.rollback()
assert stack() == [] and not Waiting.isVisible()

# screencast_controller: onLobbyInited -> __checkDbID -> __update -> setUserId(dbID), then onDisconnected.
controller = screencast()
DBID[0] = 0
controller.onLobbyInited(None)
assert not controller._isSet
DBID[0] = 1234567890123L
controller.onLobbyInited(None)
assert controller._isSet and controller._dbID == 1234567890123L
controller.onLobbyInited(None)
controller.onDisconnected()
assert not controller._isSet and controller._dbID == 0
DBID[0] = 42
controller.onLobbyInited(None)
assert controller._isSet

# Contract: arity, conversions and errors; every valid call returns None.
screener = BigWorld.Screener
modules = set(sys.modules)
for value in (False, True, 0, 2, -1, 'true', 'FALSE', 'False'):
    assert screener.setEnabled(value) is None
for value in (0, 1234567890123L, -(2 ** 63), 2 ** 63 - 1, 3.9, -2.5, 7L):
    assert screener.setUserId(value) is None
for args in (('', 0.0), ('mode', 1), (u'\u5b57', 2.5), ('x', 3L)):
    assert screener.setSettings(*args) is None
arity = [('setEnabled', 1, 1, ('bool',)), ('setUserId', 1, 1, ('long long',)),
         ('setSettings', 2, 2, ('BW::string', 'float'))]
for name, optional, total, kinds in arity:
    expected = None
    try:
        host.argCountError(name, optional, total, *kinds)
    except TypeError as error:
        expected = str(error)
    for count in (0, total + 1):
        expect_error(TypeError, expected, getattr(screener, name), *([0] * count))
    # METH_KEYWORDS reaches the auto parser, which reads only positional arguments.
    expect_error(TypeError, expected, lambda: getattr(screener, name)(value=1))
expect_error(TypeError, '() argument 1 must be set to a bool', screener.setEnabled, 1.0)
expect_error(TypeError, '() argument 1 must be set to a bool', screener.setEnabled, 'yes')
expect_error(TypeError, '() argument 1 must be set to a bool', screener.setEnabled, None)
expect_error(TypeError, '() argument 1 must be set to a long', screener.setUserId, '1')
expect_error(TypeError, '() argument 1 must be set to a long', screener.setUserId, None)
expect_error(TypeError, '() argument 1 must be set to a long', screener.setUserId, 2 ** 63)
expect_error(TypeError, '() argument 1 must be set to a string.', screener.setSettings, 1, 1.0)
expect_error(TypeError, '() argument 2 must be set to a float', screener.setSettings, 'a', '1')
assert expected == 'setSettings() expects 2 arguments of types BW::string, float', expected
# setUserId and setSettings start nothing: no module is imported by any call.
assert set(sys.modules) == modules, set(sys.modules) ^ modules
assert sorted(name for name in dir(screener) if not name.startswith('__')) == \
    ['setEnabled', 'setSettings', 'setUserId']

# Teardown: dropping the module attribute frees the wrapper; a second script init registers a new one.
before = host.state()['deallocations']
first = BigWorld.Screener
del BigWorld.Screener, screener, first
assert host.state()['deallocations'] == before + 1
assert host.initScripts() == 1
Waiting.show('loadHangarSpace')
Waiting.close()
assert stack() == []
print('port: Waiting and screencast_controller PYC run through the binding; %d view calls' % len(CALLS))
