"""CPython 2.7 harness: exact #1513 optional-service consumers under the offline policy.

Usage: python2.7 optional_services_harness.py SCRIPTS_PKG ENTRY_ROOT CASE [DATA]
ENTRY_ROOT holds OfflineOptionalServices.py. Every module under test is the
exact original code object imported from SCRIPTS_PKG; only engine leaves are
fixtures, and crypto, cache and device leaves record and raise when reached.
"""
from __future__ import print_function
import json
import os
import shutil
import sys
import tempfile
import threading
import types

PACKAGE, ENTRY_ROOT, CASE = sys.argv[1:4]
DATA = json.loads(open(sys.argv[4]).read()) if len(sys.argv) > 4 else None
CLIENT = os.path.join(PACKAGE, 'scripts', 'client')
sys.path[0:0] = [ENTRY_ROOT, CLIENT, os.path.join(PACKAGE, 'scripts', 'common')]
LOG = []


class Reached(Exception):
    """A native crypto, cache or device operation the policy must not reach."""


class Namespace(object):
    def __init__(self, **values):
        self.__dict__.update(values)


def module(name, package=False, **values):
    result = types.ModuleType(name)
    if package:
        result.__path__ = [os.path.join(CLIENT, *name.split('.'))]
    result.__dict__.update(values)
    sys.modules[name] = result
    parent, _, leaf = name.rpartition('.')
    if parent in sys.modules:
        setattr(sys.modules[parent], leaf, result)
    return result


def gate(name):
    def reached(*args):
        LOG.append((name,) + args)
        raise Reached(name)
    return reached


def recorder(name, result=None):
    def record(*args, **kwargs):
        LOG.append((name,) + args)
        return result
    return record


def logged(*names):
    return [entry for entry in LOG if entry[0] in names]


class Logs(types.ModuleType):
    def __getattr__(self, name):
        if name == 'LOG_CURRENT_EXCEPTION':
            return lambda *args: LOG.append((name, repr(sys.exc_info()[1])))
        if name.startswith('LOG_') or name == 'CRITICAL_ERROR':
            return recorder(name)
        if name == 'deprecated':
            return lambda function: function
        raise AttributeError(name)


class Section(object):
    """ResMgr DataSection over nested dicts; slash paths address children."""

    def __init__(self, tree=None, name=''):
        self.tree = {} if tree is None else tree
        self.name = name

    def _node(self, path, create=False):
        node = self.tree
        for part in path.split('/'):
            child = node.get(part)
            if not isinstance(child, dict):
                if not create:
                    return None
                child = node[part] = {}
            node = child
        return node

    def has_key(self, key):
        return key in self.tree

    def __getitem__(self, path):
        node = self._node(path)
        return None if node is None else Section(node, path.rpartition('/')[2])

    def write(self, key, value):
        self.tree[key] = {'': value} if value != '' else {}

    def deleteSection(self, key):
        self.tree.pop(key, None)

    def _read(self, path, default):
        node = self._node(path)
        return default if node is None else node.get('', default)

    def readString(self, path, default=''):
        return self._read(path, default)

    def readBool(self, path, default=False):
        return bool(self._read(path, default))

    def readFloat(self, path, default=0.0):
        return float(self._read(path, default))

    def readInt(self, path, default=0):
        return int(self._read(path, default))

    def _write(self, path, value):
        self._node(path, create=True)[''] = value

    writeString = writeBool = writeFloat = _write

    @property
    def asString(self):
        return self.tree.get('', '')

    def items(self):
        return [(key, Section(value, key)) for key, value in sorted(self.tree.items())
                if key]

    def values(self):
        return [Section(value, key.partition('#')[0]) for key, value in
                sorted(self.tree.items()) if key]


def handlers(event):
    return len(event._Event__delegates)


def snapshot(value):
    return json.dumps(value, sort_keys=True)


# Leaves shared by every case.
bigworld = module('BigWorld', wg_cpdata=gate('wg_cpdata'), wg_ucpdata=gate('wg_ucpdata'),
                  wg_cpsalt=gate('wg_cpsalt'), callback=gate('callback'),
                  cancelCallback=gate('cancelCallback'))
logs = Logs('debug_utils')
sys.modules['debug_utils'] = logs
import Event  # noqa: E402  exact scripts/common/Event.pyc
module('constants', IS_DEVELOPMENT=False)
injected = {}


class Injected(object):
    def __init__(self, interface):
        self.interface = interface

    def __get__(self, instance, owner):
        return injected[self.interface.__name__]


module('helpers', True, dependency=Namespace(descriptor=Injected))
module('skeletons', True)
for name, interface in (('skeletons.connection_mgr', 'IConnectionManager'),
                        ('skeletons.gui', None),
                        ('skeletons.gui.login_manager', 'ILoginManager'),
                        ('skeletons.gui.lobby_context', 'ILobbyContext')):
    if interface is None:
        module(name, True)
    else:
        module(name, **{interface: type(interface, (object,), {})})


def connection_manager(periphery=0):
    return Namespace(onLoggedOn=Event.Event(), onDisconnected=Event.Event(),
                     peripheryID=periphery)


class Codes(object):
    def __getattr__(self, name):
        return name


LOBBY_PACKAGES = ('gui.Scaleform.daapi.view.lobby', 'messenger.gui.Scaleform.view.lobby',
                  'gui.Scaleform.daapi.view.lobby.ny')
ENTRIES = []


def gui_entry(name):
    from messenger.gui.interfaces import IGUIEntry

    class Entry(IGUIEntry):
        """Records the GUIDecorator lifecycle of one stock scope entry."""
        channelsCtrl = name + '.channelsCtrl'

        def __init__(self):
            ENTRIES.append(self)
            LOG.append((name + '.__init__',))

        def __getattribute__(self, attr):
            if attr in ('init', 'clear', 'show', 'close', 'handleKey', 'isFocused',
                        'addClientMessage'):
                LOG.append((name + '.' + attr,))
            return object.__getattribute__(self, attr)
    Entry.__name__ = name
    return Entry


def scaleform_leaf():
    """Exact daapi config and messenger GUI registration; entries record calls."""
    if 'messenger.gui.entry_decorator' in sys.modules:
        return sys.modules['messenger.gui.entry_decorator']
    if 'gui' not in sys.modules:
        gui_leaf()
    constants = sys.modules['constants']
    constants.HAS_DEV_RESOURCES, constants.ARENA_GUI_TYPE = False, Codes()
    module('helpers.i18n', makeString=lambda key, *a, **k: key)
    module('gui.Scaleform', True)
    module('gui.Scaleform.daapi', True)
    module('gui.Scaleform.daapi.settings', True)
    from gui.Scaleform.daapi.settings import config  # exact
    assert config.LOBBY_PACKAGES.count('messenger.gui.Scaleform.view.lobby') == 1
    if 'messenger' not in sys.modules:
        module('messenger', True)
    import messenger.gui.interfaces  # noqa: F401  exact
    module('messenger.gui.Scaleform.battle_entry', BattleEntry=gui_entry('BattleEntry'))
    module('messenger.gui.Scaleform.lobby_entry', LobbyEntry=gui_entry('LobbyEntry'))
    from messenger.gui import entry_decorator  # exact, with exact setGUIEntries
    return entry_decorator


def messenger_leaf():
    """Only the install-order invariants are read outside the messenger case."""
    decorator = scaleform_leaf().GUIDecorator()
    storage = Namespace(_StorageDecorator__storageCache=None)
    module('messenger.MessengerEntry', g_instance=Namespace(storage=storage, gui=decorator))
    return storage


def gui_leaf(**settings):
    values = dict(cryptLoginInfo=True, rememberPassVisible=True,
                  socialNetworkLogin={'enabled': False}, csisRequestRate=0)
    values.update(settings)
    return module('gui', True, GUI_SETTINGS=Namespace(**values),
                  SystemMessages=Namespace(), makeHtmlString=recorder('makeHtmlString'))


def case_preferences():
    import Settings
    gui = gui_leaf()
    module('gui.Scaleform', True)
    module('gui.Scaleform.locale', True)
    module('gui.Scaleform.locale.SYSTEM_MESSAGES', SYSTEM_MESSAGES=Namespace())
    module('helpers.i18n', makeString=lambda key, *a, **k: key)
    module('helpers.time_utils', ONE_MINUTE=60)
    module('connection_mgr', CONNECTION_METHOD=Namespace())
    hosts = Namespace(onCsisQueryStart=Event.Event(), onPingPerformed=Event.Event(),
                      onCsisQueryComplete=Event.Event(),
                      readScriptConfig=recorder('readScriptConfig'),
                      startCSISUpdate=recorder('startCSISUpdate'),
                      stopCSISUpdate=recorder('stopCSISUpdate'),
                      requestPing=recorder('requestPing'),
                      shortList=lambda: [('auto', 'Auto', 0, 0), ('cn1', 'CN 1', 0, 1)])
    module('predefined_hosts', g_preDefinedHosts=hosts, AUTO_LOGIN_QUERY_URL='auto',
           AUTO_LOGIN_QUERY_ENABLED=True, HOST_AVAILABILITY=Namespace(),
           REQUEST_RATE=Namespace(ALWAYS=1, ON_REQUEST=2))
    storage = messenger_leaf()
    from gui import login

    def launch(tree):
        """One process lifetime of the exact login services configuration."""
        user_prefs = Section(tree)
        Settings.g_instance = Namespace(userPrefs=user_prefs, scriptConfig=Section())
        injected['IConnectionManager'] = connection = connection_manager(periphery=2)
        services = []
        registry = Namespace(addInstance=lambda interface, instance, finalizer: services.append(
            (interface, instance, finalizer)))
        login.getLoginManagerConfig(registry)
        assert [(i.__name__, f) for i, _, f in services] == [('ILoginManager', 'fini')], services
        return services[0][1], connection

    legacy = {'loginInfo': {'login': {'': 'ENC-login'}, 'rememberPwd': {'': True},
                            'token2': {'': 'ENC-token'}, 'salt': {'': 'ENC-salt'},
                            'user': {'': 'player'}}}
    current = {'loginInfo': {'data': {'': 'ENC-json'}}}
    malformed = {'loginInfo': {'data': {'': '{not-json'}}}

    # Control: the exact store decrypts on every launch after ContactInfo or
    # the first constructor created loginInfo, including an empty section.
    for tree in ({'loginInfo': {}}, legacy, current, malformed):
        del LOG[:]
        try:
            launch(json.loads(snapshot(tree)))
        except Reached:
            pass
        else:
            raise AssertionError('the stock credential store must decrypt')
        assert logged('wg_ucpdata'), LOG
    first = {'accounts': {'profile': {'': 'offline'}}}
    launch(first)[0].fini()
    assert first == {'accounts': {'profile': {'': 'offline'}}, 'loginInfo': {}}, first

    from gui.login.Preferences import Preferences
    original = (login.getLoginManagerConfig, Preferences.__dict__['__init__'],
                Preferences.__dict__['writeLoginInfo'])
    import OfflineOptionalServices as services
    assert services.install() and services.install()
    wrapper = login.getLoginManagerConfig
    assert wrapper is not original[0]

    for remember in (True, False):
        gui.GUI_SETTINGS.rememberPassVisible = remember
        for tree in ({}, {'loginInfo': {}}, legacy, current, malformed):
            tree = json.loads(snapshot(dict(tree, accounts={'profile': {'': 'offline'}},
                                            soundPrefs={'master': {'': 0.5}})))
            before = snapshot(tree)
            for unused_launch in range(2):
                del LOG[:]
                manager, connection = launch(tree)
                prefs = manager._preferences
                # Exact __getitem__ defaults on a dict that read nothing.
                assert manager.getPreference('remember_user') is False
                assert manager.getPreference('server_name') == 'auto'
                assert manager.getPreference('login_type') == 'credentials'
                assert manager.getPreference('password_length') == 0
                assert manager.getPreference('login') == ''
                assert ('token2' in prefs) is (not remember), prefs
                assert manager.servers.selectedServerIdx == 0
                prefs['login'] = 'player'
                manager.writePreferences()
                manager.writePeripheryLifetime()
                assert 'peripheryLifetime' in prefs and prefs['login'] == 'player'
                manager.clearToken2Preference()
                assert prefs['token2'] == ''
                manager.clearPreferences()
                assert dict(prefs) == {} and manager.getPreference('login') == ''
                assert handlers(connection.onLoggedOn) == 1 and len(services._installed) == 5
                manager.fini()
                assert handlers(connection.onLoggedOn) == 0 and manager.servers is None
                assert len(Settings.g_instance.userPrefs.tree) == len(tree)
                assert snapshot(tree) == before, (before, tree)
                assert not logged('wg_cpdata', 'wg_ucpdata', 'wg_cpsalt'), LOG
                assert logged('startCSISUpdate', 'stopCSISUpdate') == [('stopCSISUpdate',)]
                for event in (hosts.onCsisQueryStart, hosts.onPingPerformed,
                              hosts.onCsisQueryComplete):
                    assert handlers(event) == 0
    assert login.getLoginManagerConfig is wrapper
    assert Preferences.__dict__['__init__'] is services._preferences_init
    assert storage.restoreFromCache() is None
    assert bigworld.WGVibration is services.UnavailableVibration

    services.uninstall()
    services.uninstall()
    assert (login.getLoginManagerConfig, Preferences.__dict__['__init__'],
            Preferences.__dict__['writeLoginInfo']) == original
    assert 'restoreFromCache' not in storage.__dict__ and not hasattr(bigworld, 'WGVibration')
    del LOG[:]
    try:
        launch({'loginInfo': {}})
    except Reached:
        pass
    else:
        raise AssertionError('uninstall must restore the stock credential store')
    print('Exact #1513 login Preferences/Manager/Servers: no credential IO, defaults kept PASS')


class Storage(object):
    """In-memory messenger storage leaf; it never receives cache records."""

    def __init__(self, name):
        self.name = name

    def __getattr__(self, method):
        if method in ('makeRecordInCache', 'restoreFromCache'):
            return gate(self.name + '.' + method)
        return recorder(self.name + '.' + method)


def case_messenger():
    temporary = tempfile.mkdtemp(prefix='offline-messenger-')
    try:
        _messenger(temporary)
    finally:
        for thread in threading.enumerate():
            if thread is not threading.current_thread():
                thread.join(5)
        shutil.rmtree(temporary)


def _messenger(temporary):
    prefs = os.path.join(temporary, 'preferences.xml')
    bigworld.wg_getPreferencesFilePath = recorder('wg_getPreferencesFilePath', prefs)
    gui = gui_leaf()
    module('gui.shared', True)
    module('gui.shared.utils', getPlayerDatabaseID=lambda: 7, getPlayerName=lambda: 'player')
    module('gui.login', getLoginManagerConfig=recorder('getLoginManagerConfig'))
    events = Namespace()
    for name in ('onAccountShowGUI', 'onGuiCacheSyncCompleted', 'onAccountBecomePlayer',
                 'onAvatarBecomePlayer', 'onAccountBecomeNonPlayer',
                 'onAvatarBecomeNonPlayer'):
        setattr(events, name, Event.Event())
    module('PlayerEvents', g_playerEvents=events)
    module('BattleReplay', g_replayCtrl=Namespace(isPlaying=False))
    module('skeletons.account_helpers', True)
    module('skeletons.account_helpers.settings_core', ISettingsCache='ISettingsCache')
    settings = Namespace()
    for name in ('init', 'fini', 'update', 'resetUserPreferences'):
        setattr(settings, name, recorder('settings.' + name))
    module('messenger', True, g_settings=settings, error=type('error', (Exception,), {}))
    service_channel = Namespace(onReceiveSysMessage=recorder('serviceChannel'))
    protos = Namespace(BW=Namespace(serviceChannel=service_channel))
    for name in ('init', 'clear', 'setFilters', 'connect', 'view', 'disconnect',
                 'goToReplay'):
        setattr(protos, name, recorder('protos.' + name))
    module('messenger.proto', ProtoPluginsDecorator=lambda: protos)
    leaves = {'messenger.ext.player_helpers': 'CurrentPlayerHelper',
              'messenger.ext.filters': 'MessageFiltersChain'}
    entry_decorator = scaleform_leaf()
    module('messenger.ext', True)
    for name, cls in leaves.items():
        owner = Storage(cls)
        module(name, **{cls: lambda owner=owner: owner})
    for name in ('ChannelsStorage', 'UsersStorage', 'PlayerCtxStorage'):
        module('messenger.storage.' + name, **{name: lambda name=name: Storage(name)})
    import messenger.storage.local_cache  # noqa: F401  exact
    import messenger.storage as storage_module  # exact StorageDecorator
    assert storage_module.__file__.startswith(CLIENT), storage_module.__file__
    injected['IConnectionManager'] = connection = connection_manager()
    from messenger import MessengerEntry  # exact; builds g_instance at import
    entry = MessengerEntry.g_instance
    storage = entry.storage

    # Control: the exact owner creates the encrypted cache and reads it.
    del LOG[:]
    control = storage_module.StorageDecorator()
    control.restoreFromCache()
    assert control._StorageDecorator__storageCache is not None
    assert logged('wg_getPreferencesFilePath'), LOG
    for thread in threading.enumerate():
        if thread is not threading.current_thread():
            thread.join(5)
    # Existing encrypted cache content stays exactly as the retail client left it.
    for directory, unused, names in os.walk(temporary):
        with open(os.path.join(directory, 'retail-cache.dat'), 'wb') as output:
            output.write(b'\x00encrypted\xff')

    def tree():
        return sorted((os.path.relpath(os.path.join(d, n), temporary),
                       open(os.path.join(d, n), 'rb').read())
                      for d, unused, names in os.walk(temporary) for n in names)
    files = tree()
    # The root and the cache directory the exact owner created both hold one.
    assert len(files) == 2, files

    import OfflineOptionalServices as services
    # A cache opened before the entry is refused without any binding.
    storage._StorageDecorator__storageCache = control._StorageDecorator__storageCache
    try:
        services.install()
    except RuntimeError:
        pass
    else:
        raise AssertionError('an already opened messenger cache must be refused')
    assert services._installed is None and 'restoreFromCache' not in storage.__dict__
    assert not hasattr(bigworld, 'WGVibration')
    assert sys.modules['gui.login'].getLoginManagerConfig.__name__ == 'record'
    storage._StorageDecorator__storageCache = None
    from gui.Scaleform.daapi.settings import config
    from messenger.m_constants import MESSENGER_SCOPE
    LOBBY, BATTLE = MESSENGER_SCOPE.LOBBY, MESSENGER_SCOPE.BATTLE
    lobby_packages, set_entries = config.LOBBY_PACKAGES, entry_decorator.setGUIEntries
    # Control: the stock registration builds and initializes both scope entries.
    del LOG[:]
    entry.gui.init()
    assert logged('LobbyEntry.__init__', 'BattleEntry.__init__') == [
        ('LobbyEntry.__init__',), ('BattleEntry.__init__',)], LOG
    # A GUI initialized before the entry is refused without any binding.
    try:
        services.install()
    except RuntimeError:
        pass
    else:
        raise AssertionError('an initialized messenger GUI must be refused')
    assert services._installed is None and config.LOBBY_PACKAGES is lobby_packages
    entry.gui.clear()
    assert not entry.gui._GUIDecorator__entries
    assert services.install() and services.install()
    assert entry_decorator.setGUIEntries is services._set_battle_gui_entry
    assert 'messenger.gui.Scaleform.view.lobby' not in config.LOBBY_PACKAGES
    assert set(lobby_packages) - set(config.LOBBY_PACKAGES) == set([
        'messenger.gui.Scaleform.view.lobby'])
    assert 'messenger.gui.Scaleform.view.battle' in config.BATTLE_PACKAGES

    del LOG[:]
    # Partial startup: init, disconnect before any Account, then fini.
    entry.init()
    connection.onDisconnected()
    entry.fini()
    for event in (events.onAccountShowGUI, events.onAccountBecomePlayer, connection.onDisconnected):
        assert handlers(event) == 0
    entry.init()
    for unused_round in range(2):
        events.onAccountBecomePlayer()
        events.onAccountShowGUI({})
        events.onAccountShowGUI({})
        events.onAccountBecomeNonPlayer()
        events.onAvatarBecomePlayer()
        entry.onAvatarInitGUI()
        entry.onAvatarShowGUI()
        events.onAvatarBecomeNonPlayer()
    events.onAccountBecomePlayer()
    events.onAccountShowGUI({})
    # Lobby callers reach the stock unknown entry: no channels, keys or focus.
    assert entry.gui.channelsCtrl is None and entry.gui.handleKey(None) is False
    assert entry.gui.isFocused() is False and entry.gui.addClientMessage('text') is None
    events.onAvatarBecomePlayer()
    entry.onAvatarInitGUI()
    # Battle team/common chat keeps the original battle entry.
    assert entry.gui.channelsCtrl == 'BattleEntry.channelsCtrl'
    assert entry.gui.handleKey(None) is False and logged('BattleEntry.handleKey')
    events.onAvatarBecomeNonPlayer()
    events.onAccountBecomePlayer()
    events.onAccountShowGUI({})
    service_channel.onReceiveSysMessage({'data': {}})
    connection.onDisconnected()
    entry.fini()
    assert storage._StorageDecorator__storageCache is None
    assert not logged('wg_cpdata', 'wg_ucpdata', 'wg_getPreferencesFilePath', 'callback'), LOG
    assert not [e for e in LOG if e[0].endswith(('makeRecordInCache', 'restoreFromCache'))]
    for name in ('ChannelsStorage', 'UsersStorage', 'PlayerCtxStorage'):
        # Every in-memory storage is switched and cleared by the original owner.
        assert logged(name + '.switch') == [(name + '.switch', LOBBY), (name + '.switch', BATTLE)] * 3 + [
            (name + '.switch', LOBBY)], LOG
        assert len(logged(name + '.clear')) == 4, LOG
    assert logged('protos.view') == [('protos.view', LOBBY), ('protos.view', LOBBY),
                                     ('protos.view', BATTLE)] * 2 + [
        ('protos.view', LOBBY), ('protos.view', LOBBY)], LOG
    # Two init/fini pairs each built, initialized and cleared one battle entry only.
    assert logged('LobbyEntry.__init__') == [], LOG
    assert [e[0] for e in LOG if e[0] in ('BattleEntry.__init__', 'BattleEntry.init',
                                          'BattleEntry.clear')] == [
        'BattleEntry.__init__', 'BattleEntry.init', 'BattleEntry.clear'] * 2, LOG
    assert logged('serviceChannel') and len(logged('protos.disconnect')) == 2
    assert len(logged('protos.clear')) == 2
    for event in vars(events).values() + [connection.onDisconnected]:
        assert handlers(event) == 0
    assert tree() == files

    services.uninstall()
    services.uninstall()
    assert 'restoreFromCache' not in storage.__dict__
    assert storage.restoreFromCache.__func__ is storage_module.StorageDecorator.__dict__[
        'restoreFromCache']
    assert config.LOBBY_PACKAGES is lobby_packages
    assert entry_decorator.setGUIEntries is set_entries
    print('Exact #1513 MessengerEntry/GUIDecorator/StorageDecorator: battle chat entry only, '
          'no lobby chat or encrypted cache PASS')


class Vehicle(object):
    """Healthy player-vehicle leaf for the stock vibration controllers."""

    def __init__(self):
        self.speed = 0.0
        self.alive = True
        self.typeDescriptor = Namespace(physics={'speedLimits': (15.0, 7.0)})

    def isAlive(self):
        return self.alive

    def getSpeed(self):
        return self.speed


def case_vibration():
    import Settings
    effects = {'buttons': dict((name, {'': value}) for name, value in DATA['buttons'].items()),
               'effects': dict(('effect#%02d' % index, {
                   'name': {'': effect['name']}, 'priority': {'': effect['priority']},
                   'group': {'': effect['group']}}) for index, effect in enumerate(DATA['effects']))}
    effects.update((key, {'': value}) for key, value in DATA['values'].items())
    module('ResMgr', openSection=lambda path: (
        LOG.append(('openSection', path)) or Section(json.loads(snapshot(effects)))),
        purge=recorder('purge'))
    module('Math', Vector3=lambda *a: a)
    module('constants', IS_DEVELOPMENT=False, DESTRUCTIBLE_MATKIND=Namespace(MIN=0, MAX=0),
           VEHICLE_HIT_EFFECT=Namespace(ARMOR_PIERCED=0, CRITICAL_HIT=1, RICOCHET=2,
                                        ARMOR_NOT_PIERCED=3, ARMOR_PIERCED_NO_DAMAGE=4,
                                        MAX_CODE=4))
    events = Namespace(onAccountShowGUI=Event.Event())
    module('PlayerEvents', g_playerEvents=events)
    gui = gui_leaf()
    space = Namespace(LOGIN=1, LOBBY=2, BATTLE=3)
    loader = Namespace(onGUISpaceEntered=Event.Event())
    module('gui.app_loader', True, g_appLoader=loader)
    module('gui.app_loader.settings', GUI_GLOBAL_SPACE_ID=space)
    module('gui.login', getLoginManagerConfig=recorder('getLoginManagerConfig'))
    messenger_leaf()
    module('svarog_script', True)
    module('svarog_script.py_component', Component=object)
    module('LightFx', True)
    module('LightFx.LightManager', g_instance=None)
    module('LightFx.LightControllersManager', LightControllersManager=gate('LightFx'))
    module('AuxiliaryFx', g_instance=None)
    prefs = {'accounts': {'profile': {'': 'offline'}}}
    Settings.g_instance = Namespace(userPrefs=Section(prefs))
    from Vibroeffects import VibroManager  # exact
    from vehicle_systems.components.peripherals_controller import PeripheralsController

    # Control: the missing backend leaves the original None singleton, and an
    # ordinary player-vehicle attach dereferences it.
    try:
        VibroManager.g_instance = VibroManager.VibroManager()
    except AttributeError as error:
        assert 'WGVibration' in str(error), error
    else:
        raise AssertionError('the stock manager needs BigWorld.WGVibration')
    assert VibroManager.g_instance is None and handlers(events.onAccountShowGUI) == 0
    try:
        PeripheralsController().attachToVehicle(Vehicle())
    except AttributeError as error:
        assert 'getEffect' in str(error), error
    else:
        raise AssertionError('attach must fail without the vibration owner')

    import OfflineOptionalServices as services
    assert services.install() and services.install()
    for launch in range(2):
        del LOG[:]
        # game.init:124-125, game.start:290.
        VibroManager.g_instance = manager = VibroManager.VibroManager()
        assert manager.connect() is False and not manager.canWork()
        manager.start()
        assert handlers(events.onAccountShowGUI) == 1 and handlers(loader.onGUISpaceEntered) == 1
        device = manager._VibroManager__vibrationObject
        assert type(device) is services.UnavailableVibration
        expected_gain = 0.25 if launch else 1.0
        assert manager.getGain() == expected_gain, manager.getGain()
        events.onAccountShowGUI({})
        manager.playButtonClickEffect('redButton')
        manager.setGain(0.25)
        manager.setEnabledByUser(False)
        manager.setEnabledByUser(True)
        groups = manager.getGroupsSettings()
        manager.setGroupsSettings(groups)
        assert manager.getStatus() is not None

        for unused_vehicle in range(2):
            vehicle = Vehicle()
            peripherals = PeripheralsController()
            peripherals.attachToVehicle(vehicle)
            controllers = peripherals._PeripheralsController__vibrationsCtrl
            assert controllers is not None
            for speed in (0.0, 5.0, 12.0, 3.0):
                vehicle.speed = speed
                peripherals.update(vehicle, Namespace(isLeftTrackBroken=lambda: False,
                                                      isRightTrackBroken=lambda: False))
            controllers.executeShootingVibrations(100)
            controllers.executeHitVibrations(0)
            controllers.executeCriticalHitVibrations(vehicle, 'engine')
            controllers.executeRammingVibrations(5.0, 0)
            vehicle.alive = False
            peripherals.update(vehicle, Namespace(isLeftTrackBroken=lambda: True,
                                                  isRightTrackBroken=lambda: False))
            peripherals.deactivate()
            peripherals.destroy()
            assert peripherals._PeripheralsController__vibrationsCtrl is None
        # Every effect is the original VibroEffect with the unavailable handle.
        effects_ = manager._VibroManager__effects.values() + [
            effect for pool in manager._VibroManager__quickEffects.values() for effect in pool]
        assert effects_ and all(effect.handle is None for effect in effects_)
        assert not manager._VibroManager__runningEffects and not manager.canWork()
        loader.onGUISpaceEntered(space.LOGIN)
        # game.fini:378-380 destroys the singleton once and clears it.
        manager.destroy()
        VibroManager.g_instance = None
        assert handlers(events.onAccountShowGUI) == 0 and handlers(loader.onGUISpaceEntered) == 0
        assert manager._VibroManager__vibrationObject is None
        assert prefs['vibration']['gain'][''] == 0.25, prefs
        assert prefs['accounts'] == {'profile': {'': 'offline'}}
        assert not logged('callback', 'cancelCallback', 'LightFx'), LOG

    services.uninstall()
    assert not hasattr(bigworld, 'WGVibration')
    # A native backend, when present, is never replaced.
    native = type('NativeVibration', (object,), {})
    bigworld.WGVibration = native
    services.install()
    assert bigworld.WGVibration is native
    services.uninstall()
    assert bigworld.WGVibration is native
    print('Exact #1513 VibroManager/controllers/peripherals: unavailable device lifecycle PASS')


def case_xmpp():
    gui_leaf()
    module('gui.shared', True)
    module('gui.shared.utils', getPlayerDatabaseID=lambda: 7)
    module('messenger', True)
    module('messenger.proto', True)
    module('messenger.proto.interfaces', IProtoSettings=object)
    module('messenger.proto.xmpp', True)
    module('messenger.proto.xmpp.gloox_constants', CONNECTION_IMPL_TYPE=Namespace())
    module('messenger.proto.xmpp.jid', ContactJID=object)
    from messenger.proto.xmpp.XmppServerSettings import XmppServerSettings  # exact
    settings = XmppServerSettings()
    for payload in (DATA['pinned_server_settings'], {}, {'xmpp_enabled': False}):
        settings.clear()
        settings.update(payload)
        assert settings.isEnabled() is False, payload
    # Control: only an explicit producer with a host would enable the client.
    settings.update({'xmpp_enabled': True, 'xmpp_host': 'chat.example',
                     'xmpp_connections': [('chat.example', 5222)]})
    assert settings.isEnabled() is True
    print('Exact #1513 XmppServerSettings: the pinned offline payload keeps XMPP disabled PASS')


class NativeVOIP(object):
    """BigWorld.VOIP: records every native voice call the stock owners make."""

    def __getattr__(self, name):
        return recorder('VOIP.' + name)


def case_voice():
    module('ResMgr', openSection=lambda path: None)
    import Settings
    Settings.g_instance = Namespace(userPrefs=Section())
    gui_leaf()
    bigworld.VOIP = NativeVOIP()
    module('helpers.i18n', makeString=lambda key, *a, **k: key)
    sys.modules['constants'].SERVER_TICK_LENGTH = 0.1
    player = Namespace(serverSettings=DATA['pinned_server_settings'])
    bigworld.player = lambda: player
    module('BattleReplay', g_replayCtrl=Namespace(isPlaying=False))
    module('CommandMapping', g_instance=Namespace())
    module('SoundGroups', g_instance=Namespace())
    module('account_helpers', True)
    module('account_helpers.settings_core', True)
    module('account_helpers.settings_core.settings_constants', SOUND=Codes())
    module('skeletons.account_helpers', True)
    module('skeletons.account_helpers.settings_core', ISettingsCore=type(
        'ISettingsCore', (object,), {}))
    module('messenger', True)
    module('messenger.proto', True, proto_getter=lambda proto: property)
    users = Namespace(onUsersListReceived=Event.Event(), onUserActionReceived=Event.Event())
    module('messenger.proto.events', g_messengerEvents=Namespace(users=users))
    module('messenger.proto.shared_find_criteria', MutedFindCriteria=object)
    module('messenger.storage', storage_getter=lambda name: property)
    module('messenger.proto.bw_chat2', True)  # the package plugin is not under test
    from messenger.proto.bw_chat2.VOIPChatController import VOIPChatController  # exact
    import VOIP  # exact getVOIPManager and VOIPManager

    # BW_CHAT2.connect starts and disconnect stops the controller; repeat it.
    for unused_connection in range(2):
        del LOG[:]
        controller = VOIPChatController()
        controller.start()
        manager = VOIP.getVOIPManager()
        assert manager.isNotInitialized() and not controller.isReady()
        assert ('LOG_WARNING', 'Initialize. Vivox is not supported') in LOG, LOG
        assert not logged('LOG_CURRENT_EXCEPTION'), LOG
        # Only the stock handler registration reaches the native object: no
        # initialise, login, channel, capture-device or microphone call.
        assert [e for e in LOG if e[0].startswith('VOIP.')] in (
            [('VOIP.setHandler', manager)], []), LOG
        controller.stop()
        for name in ('onInitialized', 'onFailedToConnect', 'onCaptureDevicesUpdated',
                     'onPlayerSpeaking', 'onStateToggled', 'onJoinedChannel'):
            assert handlers(getattr(manager, name)) == 0, name
    # Control: an explicit voice domain would initialise the native service.
    injected['ISettingsCore'] = Namespace(options=Namespace(getSetting=lambda name: Namespace(
        initFromPref=recorder('initFromPref'))))
    player.serverSettings = dict(DATA['pinned_server_settings'], voipDomain='voice.example',
                                 voipUserDomain='users.example')
    del LOG[:]
    controller = VOIPChatController()
    controller.start()
    assert logged('VOIP.initialise'), LOG
    print('Exact #1513 VOIPChatController/VOIPManager: pinned payload never initialises voice PASS')


{'preferences': case_preferences, 'messenger': case_messenger,
 'vibration': case_vibration, 'xmpp': case_xmpp, 'voice': case_voice}[CASE]()
