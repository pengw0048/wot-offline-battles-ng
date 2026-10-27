"""CPython 2.7 harness: exact #1513 app-loader, mods and connection consumers.

Usage: python2.7 offline_entry_harness.py SCRIPTS_PKG ENTRY_ROOT CASE
ENTRY_ROOT holds OfflineGarageEntry.py with gui/mods/mod_offline_lan_0922.py.
Only leaf services are fixtures; every state, loader, mods and connection
method under test is the exact original code object.
"""
from __future__ import print_function
import imp
import marshal
import os
import sys
import types
import zipfile

PACKAGE, ENTRY_ROOT, CASE = sys.argv[1:4]
ARCHIVE = zipfile.ZipFile(PACKAGE)
LOG = []


def module(name, package=False, **values):
    result = types.ModuleType(name)
    if package:
        result.__path__ = []
    result.__dict__.update(values)
    sys.modules[name] = result
    parent, _, leaf = name.rpartition('.')
    if parent:
        setattr(sys.modules[parent], leaf, result)
    return result


def exact(name, member, package=False):
    result = module(name, package)
    if package:
        result.__path__ = [os.path.join(PACKAGE, os.path.dirname(member))]
    result.__file__ = os.path.join(PACKAGE, member)
    exec(marshal.loads(ARCHIVE.read(member)[8:]), result.__dict__)
    return result


def record(*entry):
    LOG.append(entry)


class Namespace(object):
    def __init__(self, **values):
        self.__dict__.update(values)


def log_function(kind):
    return lambda *args, **kwargs: record(kind, args[0] if args else None)


# Leaf fixtures for engine services, logging and dependency injection.
bigworld = module('BigWorld',
                  clearEntitiesAndSpaces=lambda keep=False: record('clearEntitiesAndSpaces', keep))
module('constants', ARENA_GUI_TYPE=Namespace(UNKNOWN=0), IS_DEVELOPMENT=False,
       CURRENT_REALM='CN', AUTH_REALM='CN', IS_IGR_ENABLED=False,
       CLIENT_INACTIVITY_TIMEOUT=40)
module('debug_utils', LOG_DEBUG=lambda *a: None, LOG_NOTE=lambda *a: None,
       LOG_WARNING=log_function('LOG_WARNING'), LOG_ERROR=log_function('LOG_ERROR'),
       LOG_CURRENT_EXCEPTION=lambda: record('LOG_CURRENT_EXCEPTION', sys.exc_info()[0]))
exact('Event', 'scripts/common/Event.pyc')
class Flags(object):
    def __getattr__(self, name):
        return 0


gui = module('gui', True, GUI_CTRL_MODE_FLAG=Flags())
gui.__path__ = [os.path.join(PACKAGE, 'scripts/client/gui')]
listeners = {}
module('gui.shared', True,
       g_eventBus=Namespace(addListener=lambda kind, fn: listeners.setdefault(kind, []).append(fn),
                            removeListener=lambda kind, fn: listeners[kind].remove(fn)),
       events=Namespace(AppLifeCycleEvent=Namespace(INITIALIZING='init', INITIALIZED='ready',
                                                    DESTROYED='destroyed'),
                        GlobalSpaceEvent=Namespace(GO_NEXT='next')))
module('gui.shared.utils', True)
module('gui.shared.utils.HangarSpace', g_hangarSpace=Namespace(inited=False))
module('gui.shared.utils.decorators',
       ReprInjector=Namespace(simple=lambda *names: (lambda cls: cls)))
module('gui.Scaleform', True)
module('gui.Scaleform.Waiting', Waiting=Namespace(isOpened=lambda name: False,
                                                  hide=lambda name: record('Waiting.hide', name)))
module('BattleReplay', g_replayCtrl=Namespace(autoStartBattleReplay=lambda: False,
                                              isPlaying=False))
startup_video = [True]
manager_holder = []


class Injected(object):
    def __get__(self, instance, owner):
        return manager_holder[0]


module('helpers', True, isShowStartupVideo=lambda: startup_video[0],
       dependency=Namespace(descriptor=lambda interface: Injected()),
       getClientLanguage=lambda: 'zh_cn')
module('skeletons', True)
module('skeletons.connection_mgr', IConnectionManager=object)
module('gui.app_loader', True)
settings = exact('gui.app_loader.settings', 'scripts/client/gui/app_loader/settings.pyc')
exact('gui.app_loader.interfaces', 'scripts/client/gui/app_loader/interfaces.pyc')
states = exact('gui.app_loader.states', 'scripts/client/gui/app_loader/states.pyc')
loader = exact('gui.app_loader.loader', 'scripts/client/gui/app_loader/loader.pyc')
SPACE, LOBBY_NS = settings.GUI_GLOBAL_SPACE_ID, settings.APP_NAME_SPACE.SF_LOBBY

sys.path.insert(0, ENTRY_ROOT)
import OfflineGarageEntry as entry


class Lobby(object):
    initialized = False


class Factory(object):
    def __init__(self):
        self.lobby = Lobby()

    def __getattr__(self, name):
        return lambda *args, **kwargs: record(name, args[0] if args else None)

    def hasApp(self, ns):
        return ns == LOBBY_NS

    def getDefLobbyApp(self):
        return self.lobby


class Manager(object):
    connected = False

    def isConnected(self):
        return self.connected

    def disconnect(self):
        record('disconnect', None)
        self.connected = False


def app_loader():
    del LOG[:]
    listeners.clear()
    manager_holder[:] = [Manager()]
    result = loader._AppLoader()
    factory = Factory()
    result.init(factory)
    return result, factory


def app_event(kind):
    for listener in list(listeners[kind]):
        listener(Namespace(ns=LOBBY_NS))


def calls(*names):
    return [entry_ for entry_ in LOG if entry_[0] in names]


VIEWS = ('goToLogin', 'goToIntroVideo', 'goToLobby')


def case_routing():
    # Control: the stock StartState enters the intro and then LoginView.
    loader_, factory = app_loader()
    loader_.startLobby()
    assert type(loader_._AppLoader__state) is states.IntroVideoState
    app_event('init')
    assert calls(*VIEWS) == [('goToIntroVideo', LOBBY_NS)], LOG
    startup_video[0] = False
    loader_, factory = app_loader()
    loader_.startLobby()
    factory.lobby.initialized = True
    app_event('ready')
    assert type(loader_._AppLoader__state) is states.LoginState
    assert calls(*VIEWS) == [('goToLogin', LOBBY_NS)], LOG
    assert not entry.login_space_ready(loader_)

    # Product: no intro and no LoginView, even when the intro is requested.
    startup_video[0] = True
    assert entry.install_route() and entry.install_route()
    loader_, factory = app_loader()
    loader_.startLobby()
    state = loader_._AppLoader__state
    assert isinstance(state, states.LoginState) and type(state) is not states.LoginState
    assert loader_.getSpaceID() == SPACE.LOGIN
    assert calls('createLobby', 'clearEntitiesAndSpaces') == [
        ('createLobby', None), ('clearEntitiesAndSpaces', False)], LOG
    app_event('init')
    assert not entry.login_space_ready(loader_)
    factory.lobby.initialized = True
    app_event('ready')
    assert calls(*VIEWS) == [] and calls('attachCursor') == [('attachCursor', LOBBY_NS)], LOG
    assert calls('showDisconnectDialog') == []
    assert entry.login_space_ready(loader_)

    # LOGIN keeps LoginState.goNext: LOBBY needs the connection first.
    del LOG[:]
    loader_.showLobby()
    assert loader_.getSpaceID() == SPACE.LOGIN and calls('LOG_WARNING'), LOG
    manager_holder[0].connected = True
    del LOG[:]
    loader_.showLobby()
    assert type(loader_._AppLoader__state) is states.LobbyState
    assert calls(*VIEWS) == [('goToLobby', LOBBY_NS)], LOG

    # A client error returns to the view-less state with its notice only.
    del LOG[:]
    loader_.goToLoginByError('lan failure')
    state = loader_._AppLoader__state
    assert type(state) is not states.LoginState and loader_.getSpaceID() == SPACE.LOGIN
    assert calls(*VIEWS) == [], LOG
    assert calls('disconnect', 'clearEntitiesAndSpaces', 'showDisconnectDialog') == [
        ('disconnect', None), ('clearEntitiesAndSpaces', False),
        ('showDisconnectDialog', LOBBY_NS)], LOG

    # StartState never accepts a raw LOBBY jump; the route keeps that rule.
    loader_, factory = app_loader()
    manager_holder[0].connected = True
    loader_.changeSpace(SPACE.LOBBY)
    assert type(loader_._AppLoader__state) is states.StartState
    assert calls('LOG_ERROR') == [('LOG_ERROR', 'State can not be switched')], LOG

    entry.uninstall_route()
    entry.uninstall_route()
    loader_, factory = app_loader()
    loader_.startLobby()
    assert type(loader_._AppLoader__state) is states.IntroVideoState
    # A stock LoginView state never satisfies the offline startup gate.
    startup_video[0] = False
    loader_, factory = app_loader()
    loader_.startLobby()
    factory.lobby.initialized = True
    app_event('ready')
    assert type(loader_._AppLoader__state) is states.LoginState
    assert calls('goToLogin') and not entry.login_space_ready(loader_)
    print('Exact #1513 app loader: no intro/LoginView, LOGIN bridge, LOBBY and error routes PASS')


def case_mods():
    resource_keys = []
    module('ResMgr', openSection=lambda path: (
        resource_keys.append(path) or Namespace(keys=lambda: ['__init__.pyc'])))
    module('shared_utils', forEach=lambda fn, items: [fn(item) for item in items])
    mods = exact('gui.mods', 'scripts/client/gui/mods/__init__.pyc', package=True)
    personality = module('gui.shared.personality', guiModsInit=mods.init,
                         guiModsFini=mods.fini, guiModsSendEvent=mods.sendEvent)
    # The service policies have their own exact-consumer harness.
    services = module('OfflineOptionalServices', install=lambda: record('services.install', None),
                      uninstall=lambda: record('services.uninstall', None))
    entry_dir = entry.MODS_DIRECTORY
    del LOG[:]
    assert entry.install() and entry.install()
    assert calls('services.install') == [('services.install', None)], LOG
    assert mods.__path__[-1] == entry_dir and mods.__path__.count(entry_dir) == 1
    assert personality.guiModsInit is not mods.init
    del LOG[:]
    personality.guiModsInit()
    mod = sys.modules['gui.mods.mod_offline_lan_0922']
    assert resource_keys == [mods.__path__[0]], resource_keys
    assert mods._mods == {'gui.mods.mod_offline_lan_0922': mod}
    assert mod.calls == ['init'], mod.calls
    personality.guiModsSendEvent('onAccountShowGUI', 1)
    personality.guiModsFini()
    assert mod.calls == ['init', ('onAccountShowGUI', 1), 'fini'], mod.calls
    assert mods._mods == {}
    # Like every discovered mod, a later guiModsInit registers it again.
    personality.guiModsInit()
    assert mods._mods == {'gui.mods.mod_offline_lan_0922': mod}
    assert mod.calls[-1] == 'init' and mod.calls.count('init') == 2, mod.calls
    personality.guiModsFini()
    del LOG[:]
    entry.uninstall()
    entry.uninstall()
    assert personality.guiModsInit is mods.init and entry_dir not in mods.__path__
    assert calls('services.install', 'services.uninstall') == [('services.uninstall', None)], LOG
    # The original binding is the only accepted seam.
    personality.guiModsInit = lambda: None
    del LOG[:]
    try:
        entry.install()
    except RuntimeError:
        pass
    else:
        raise AssertionError('a replaced guiModsInit binding must be refused')
    assert calls('services.install') == [], LOG
    # A refused service policy leaves no partial entry binding.
    personality.guiModsInit = mods.init
    services.install = lambda: 1 / 0
    try:
        entry.install()
    except ZeroDivisionError:
        pass
    else:
        raise AssertionError('a service policy failure must fail the entry')
    assert personality.guiModsInit is mods.init and entry_dir not in mods.__path__
    assert entry._installed is None and entry._route is None
    # Route imports can fail after optional policies have been bound. Roll
    # them back before propagating the error; uninstall must remain harmless.
    services.install = lambda: record('services.install', None)
    original_route = entry.install_route
    entry.install_route = lambda: 1 / 0
    del LOG[:]
    try:
        entry.install()
    except ZeroDivisionError:
        pass
    else:
        raise AssertionError('a route failure must fail the entry')
    finally:
        entry.install_route = original_route
    entry.uninstall()
    entry.uninstall()
    assert calls('services.install', 'services.uninstall') == [
        ('services.install', None), ('services.uninstall', None)], LOG
    assert personality.guiModsInit is mods.init and entry_dir not in mods.__path__
    assert entry._installed is None and entry._route is None
    personality.guiModsInit = mods.init
    print('Exact #1513 gui.mods: explicit offline mod init/sendEvent/fini at guiModsInit PASS')


def case_connection():
    module('ResMgr')
    events = Namespace()
    module('PlayerEvents', g_playerEvents=events)
    event_module = sys.modules['Event']
    for name in ('onKickWhileLoginReceived', 'onLoginQueueNumberReceived', 'onDisconnected'):
        setattr(events, name, event_module.Event())
    module('shared_utils', nextTick=lambda fn: fn)
    module('predefined_hosts', g_preDefinedHosts=Namespace(_makeHostItem=lambda *a: a),
           AUTO_LOGIN_QUERY_URL='auto')
    module('account_shared', isValidClientVersion=lambda *a: True)
    module('account_helpers', True)
    module('account_helpers.pwd_token', generate=lambda password: password)
    connection = exact('connection_mgr', 'scripts/client/connection_mgr.pyc')
    manager = connection.ConnectionManager()
    seen = []
    manager.onLoggedOn += lambda data: seen.append('onLoggedOn')
    manager.onConnected += lambda: seen.append(('onConnected', manager.isConnected()))
    # Control: the exact retail handler requires the absent WGC owner.
    try:
        manager._ConnectionManager__serverResponseHandler(
            1, connection.LOGIN_STATUS.LOGGED_ON, '{}')
    except AttributeError as error:
        assert 'WGC_onServerResponse' in str(error), error
    else:
        raise AssertionError('the retail progress handler must need WGC')
    assert seen == []
    manager._ConnectionManager__connectionStatus = connection.LOGIN_STATUS.NOT_SET
    entry.publish_logged_on(manager, connection.LOGIN_STATUS)
    assert manager.isConnected() and seen == [('onConnected', True)], seen
    # A failing consumer reaches the compat transaction, which rolls back.
    manager._ConnectionManager__connectionStatus = connection.LOGIN_STATUS.NOT_SET
    manager.onConnected += lambda: 1 / 0
    try:
        entry.publish_logged_on(manager, connection.LOGIN_STATUS)
    except ZeroDivisionError:
        pass
    else:
        raise AssertionError('a failing onConnected consumer must propagate')
    print('Exact #1513 ConnectionManager: offline LOGGED_ON without WGC/login listeners PASS')


{'routing': case_routing, 'mods': case_mods, 'connection': case_connection}[CASE]()
