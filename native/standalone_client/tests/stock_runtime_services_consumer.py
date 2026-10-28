# Python 2.7, run inside stock_runtime_services_host: argv = [this, scripts.pkg, mode]
# mode 'absent' runs the original PYC without the bindings; 'port' runs them over
# the production bindings and the patched App frame-pacing owner.
import dis
import imp
import marshal
import struct
import sys
import types
import zipfile

import _runtime_host as host

PACKAGE, MODE = sys.argv[1], sys.argv[2]
ARCHIVE = zipfile.ZipFile(PACKAGE)
CALLS = []
NOTES = []


def module(name, **attributes):
    result = sys.modules.get(name) or imp.new_module(name)
    result.__dict__.update(attributes)
    sys.modules[name] = result
    if '.' in name:
        parent, child = name.rsplit('.', 1)
        setattr(module(parent), child, result)
    return result


def code_of(member):
    return marshal.loads(ARCHIVE.read(member)[8:])


def load(member, name):
    result = module(name)
    exec code_of(member) in result.__dict__
    return result


def nested(code, path):
    for name in path.split('.'):
        code = [const for const in code.co_consts
                if isinstance(const, types.CodeType) and const.co_name == name][0]
    return code


def function(member, path, namespace):
    namespace['__builtins__'] = __builtins__
    return types.FunctionType(nested(code_of(member), path), namespace)


def call_shape(member, path, attribute):
    """(line, argument loads, CALL_FUNCTION oparg) of BigWorld.<attribute>(...) in the exact bytecode."""
    code = nested(code_of(member), path)
    raw, index, line, shapes = code.co_code, 0, code.co_firstlineno, []
    starts = dict(dis.findlinestarts(code))
    ops = []
    while index < len(raw):
        line = starts.get(index, line)
        op = ord(raw[index])
        arg = ord(raw[index + 1]) | ord(raw[index + 2]) << 8 if op >= dis.HAVE_ARGUMENT else None
        ops.append((line, dis.opname[op], arg))
        index += 3 if op >= dis.HAVE_ARGUMENT else 1
    for position, (line, name, arg) in enumerate(ops):
        if name == 'LOAD_ATTR' and code.co_names[arg] == attribute:
            assert ops[position - 1][1] == 'LOAD_GLOBAL' and code.co_names[ops[position - 1][2]] == 'BigWorld'
            end = position + 1
            while not ops[end][1].startswith('CALL_FUNCTION'):
                end += 1
            loads = [code.co_names[arg] if op == 'LOAD_GLOBAL' else (op, arg)
                     for unused, op, arg in ops[position + 1:end]]
            shapes.append((line, loads, ops[end][1], ops[end][2]))
    return shapes


class Event(object):
    def __init__(self):
        self.handlers = []

    def __iadd__(self, handler):
        self.handlers.append(handler)
        return self

    def __isub__(self, handler):
        self.handlers.remove(handler)
        return self

    def __call__(self, *args):
        for handler in list(self.handlers):
            handler(*args)


class Recorder(object):
    """Records attribute calls on a service the exercised statements do not own."""
    def __init__(self, path, returns=None):
        self.__dict__['_path'], self.__dict__['_returns'] = path, returns or {}

    def __getattr__(self, name):
        path = self._path + '.' + name
        child = Recorder(path, self._returns)
        self.__dict__[name] = child
        return child

    def __call__(self, *args, **kwargs):
        CALLS.append((self._path, args, tuple(sorted(kwargs.items()))))
        return self._returns.get(self._path)


def expect_error(kind, text, call, *args, **kwargs):
    try:
        call(*args, **kwargs)
    except kind as error:
        assert str(error) == text, (str(error), text)
        return
    raise AssertionError('%r%r did not raise %s' % (call, args, kind.__name__))


def float32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


# Retained modules imported by the exercised PYC; their services are fixtures.
ARENA_PERIOD = type('ARENA_PERIOD', (), {'IDLE': 0, 'WAITING': 1, 'PREBATTLE': 2, 'BATTLE': 3, 'AFTERBATTLE': 4})
Interface = type('Interface', (object,), {})
SERVICES = {}
module('BigWorld')
module('ResMgr')
module('Settings')
module('constants', ARENA_PERIOD=ARENA_PERIOD,
       INVALID_CLIENT_STATS=type('INVALID_CLIENT_STATS', (), {'CLIENT_STRAIGHT_INTO_BATTLE': 1,
                                                              'CLIENT_DRR_SCALE_CHANGED': 2}))
module('account_helpers.settings_core.settings_constants', GRAPHICS=type('GRAPHICS', (), {}))
module('gui.shared.utils', graphics=None)
module('debug_utils', LOG_DEBUG=lambda *args: None, LOG_NOTE=NOTES.append)
module('helpers.dependency', descriptor=lambda interface: SERVICES.setdefault(interface, Recorder(interface.__name__)))
for name, interface in (('skeletons.account_helpers.settings_core', 'ISettingsCore'),
                        ('skeletons.connection_mgr', 'IConnectionManager'),
                        ('skeletons.gui.battle_session', 'IBattleSessionProvider'),
                        ('skeletons.helpers.statistics', 'IStatisticsCollector'),
                        ('skeletons.gui.game_control', 'IGameSessionController')):
    module(name, **{interface: type(interface, (Interface,), {})})
settings_core = sys.modules['skeletons.account_helpers.settings_core'].ISettingsCore
battle_session = sys.modules['skeletons.gui.battle_session'].IBattleSessionProvider
SERVICES[settings_core] = type('SettingsCore', (), {'onSettingsChanged': Event()})()
SERVICES[battle_session] = type('Session', (), {'shared': type('Shared', (), {'drrScale': None})()})()


class ViewComponentsController(object):
    def __init__(self):
        self._viewComponents = []

    def setViewComponents(self, *components):
        self._viewComponents = list(components)


module('gui.app_loader', g_appLoader=Recorder('g_appLoader'))
module('gui.battle_control.arena_info.interfaces', IArenaVehiclesController=Interface)
module('gui.battle_control.battle_constants', BATTLE_CTRL_ID=type('BATTLE_CTRL_ID', (), {'ARENA_LOAD_PROGRESS': 9}))
module('gui.battle_control.view_components', ViewComponentsController=ViewComponentsController)
module('messenger.MessengerEntry', g_instance=Recorder('MessengerEntry.g_instance'))

BigWorld = sys.modules['BigWorld']
PLAYER = type('Player', (), {})()
PLAYER.arena = type('Arena', (), {'period': ARENA_PERIOD.IDLE})()
BigWorld.player = lambda: PLAYER
BigWorld.PyGroundEffectManager = lambda: Recorder('PyGroundEffectManager()')

STATISTICS = 'scripts/client/helpers/statistics.pyc'
ARENA_LOAD = 'scripts/client/gui/battle_control/controllers/arena_load_ctrl.pyc'
AVATAR = 'scripts/client/Avatar.pyc'
PERSONALITY = 'scripts/client/gui/shared/personality.pyc'
HANGAR = 'scripts/client/gui/ClientHangarSpace.pyc'

statistics = load(STATISTICS, 'helpers.statistics')
arena_load = load(ARENA_LOAD, 'gui.battle_control.controllers.arena_load_ctrl')
HLS = statistics.HANGAR_LOADING_STATE
STATE = statistics._STATISTICS_STATE
collector = statistics.StatisticsCollector()
host.configure({'renderer/maxFrameRate': 120, 'renderer/reducedFrameRate': 60}, 1000.0)


def missing(name):
    return "'module' object has no attribute '%s'" % name


def disconnect_namespace(services):
    return {'ServicesLocator': services, 'HANGAR_LOADING_STATE': HLS, 'BigWorld': BigWorld,
            'guiModsSendEvent': Recorder('guiModsSendEvent'), 'g_appLoader': Recorder('g_appLoader'),
            'g_prbLoader': Recorder('g_prbLoader'), 'g_clanCache': Recorder('g_clanCache'),
            'g_wgncProvider': Recorder('g_wgncProvider'), 'UsersInfoHelper': Recorder('UsersInfoHelper'),
            'Waiting': Recorder('Waiting')}


def services_locator():
    services = Recorder('ServicesLocator', {'ServicesLocator.lobbyContext.getServerSettings':
                                            Recorder('serverSettings', {'serverSettings.isElenEnabled': False})})
    services.__dict__['statsCollector'] = collector
    return services


if MODE == 'absent':
    host.setTicks(1250)
    expect_error(AttributeError, missing('timeExact'), collector.noteHangarLoadingState, HLS.CONNECTED, True)
    assert collector._StatisticsCollector__loadingStates[HLS.CONNECTED] == 0.0
    controller = arena_load.ArenaLoadController()
    controller.startControl(None, Recorder('arenaVisitor'))
    expect_error(AttributeError, missing('wg_setReducedFpsMode'), controller.spaceLoadStarted)
    onDisconnected = function(PERSONALITY, 'onDisconnected', disconnect_namespace(services_locator()))
    expect_error(AttributeError, missing('timeExact'), onDisconnected)
    print('absent: noteHangarLoadingState, spaceLoadStarted and onDisconnected raise AttributeError')
    sys.exit(0)

# Registration: thirteen BigWorld functions, one link each; only purge takes keywords.
assert host.initScripts() == 13
links = host.links()
assert len(set(name for unused, name, unused in links)) == 13, links
assert all(module_name == 'BigWorld' for module_name, unused, unused in links)
assert dict((name, flags) for unused, name, flags in links) == dict(
    [(name, 1) for name in ('timeExact', 'wg_getFrameTimestamp', 'addUPLMessage', 'enableBattleStatisticCollector',
                            'enableLoadingTimer', 'wg_getClientStatistics', 'wg_setSpecialFPSMode',
                            'wg_disableSpecialFPSMode', 'wg_setReducedFpsMode', 'wg_clearTextureReuseList',
                            'PFbeginFrame', 'PFendFrame')] +
    [('purgeUrlRequestCache', 3)])

# The shipped decorators module assigns IS_DEVELOPMENT=True, selecting the
# enabled trackFrame branch even in this client build. Execute its complete
# class body and closure rather than reproducing the call sequence.
decorators = code_of('scripts/client/gui/shared/utils/decorators.pyc')
assignment = (chr(dis.opmap['LOAD_NAME']) + struct.pack('<H', decorators.co_names.index('True')) +
              chr(dis.opmap['STORE_NAME']) + struct.pack('<H', decorators.co_names.index('IS_DEVELOPMENT')))
assert assignment in decorators.co_code
namespace = {'__builtins__': __builtins__, '__name__': 'original_decorators', 'BigWorld': BigWorld}
frame_class = nested(decorators, '_TrackFrameEnabled')
attributes = types.FunctionType(frame_class, namespace)()
TrackFrame = type('_TrackFrameEnabled', (object,), attributes)
namespace['_TrackFrameEnabled'] = TrackFrame
calls = []
wrapped = TrackFrame('battle.frame')(lambda *args, **kwargs: calls.append((args, kwargs)) or 42)
assert wrapped(7, value=3) is None and calls == [((7,), {'value': 3})]
assert BigWorld.PFbeginFrame(u'battle.frame') is None and BigWorld.PFendFrame() is None
expect_error(TypeError, 'PFbeginFrame() expects 1 argument of type BW::string', BigWorld.PFbeginFrame)
expect_error(TypeError, 'PFbeginFrame() expects 1 argument of type BW::string', BigWorld.PFbeginFrame, 'a', 'b')
expect_error(TypeError, '() argument 1 must be set to a string.', BigWorld.PFbeginFrame, None)
expect_error(TypeError, 'PFendFrame() takes no arguments.', BigWorld.PFendFrame, 1)
expect_error(TypeError, 'PFendFrame() takes no keyword arguments', BigWorld.PFendFrame, name='x')
for missing in ('PFbeginFrame', 'PFendFrame'):
    saved = getattr(BigWorld, missing)
    delattr(BigWorld, missing)
    try:
        wrapped()
    except AttributeError:
        pass
    else:
        raise AssertionError('Original trackFrame closure did not require ' + missing)
    setattr(BigWorld, missing, saved)
# Its exception path deliberately never closes the frame and propagates the
# function's exception; the native no-op must not add state requiring cleanup.
end = BigWorld.PFendFrame
ended = []
BigWorld.PFendFrame = lambda: ended.append(True)
def fail_frame():
    raise ValueError('frame failed')
expect_error(ValueError, 'frame failed', TrackFrame('failed.frame')(fail_frame))
assert not ended
BigWorld.PFendFrame = end

# timeExact samples the App timer on every call; wg_getFrameTimestamp is the frame identity.
host.setTicks(1000)
first, frame = BigWorld.timeExact(), BigWorld.wg_getFrameTimestamp()
host.setTicks(1500)
second = BigWorld.timeExact()
assert (first, second) == (1.0, 1.5) and type(first) is float
assert BigWorld.wg_getFrameTimestamp() == frame and type(frame) is int
host.setTicks(3333)
assert BigWorld.timeExact() == float32(3.333) != 3.333
host.nextFrame()
assert BigWorld.wg_getFrameTimestamp() == frame + 1
host.nextFrame(0x80000000)
# Script::getData(uint) yields long above LONG_MAX: on the 32-bit client, not this LP64 host.
assert BigWorld.wg_getFrameTimestamp() == 0x80000000
assert type(BigWorld.wg_getFrameTimestamp()) is (long if sys.maxint < 0x80000000 else int)
host.nextFrame(7)
expect_error(TypeError, 'timeExact() takes no arguments.', BigWorld.timeExact, 1)
expect_error(TypeError, 'timeExact() takes no keyword arguments', BigWorld.timeExact, now=1)
expect_error(TypeError, 'wg_getFrameTimestamp() takes no arguments.', BigWorld.wg_getFrameTimestamp, None)

# Telemetry dictionary remains unavailable; frame collection has a separate real owner.
assert BigWorld.addUPLMessage('HANGAR LOADING STATE: CONNECTED') is None
expect_error(TypeError, 'addUPLMessage() expects 1 argument of type BW::string', BigWorld.addUPLMessage)
expect_error(TypeError, 'addUPLMessage() expects 1 argument of type BW::string', BigWorld.addUPLMessage, 'a', 'b')
for name in ('enableBattleStatisticCollector', 'enableLoadingTimer', 'wg_setReducedFpsMode'):
    call = getattr(BigWorld, name)
    expect_error(TypeError, '%s() expects 1 argument of type bool' % name, call)
    expect_error(TypeError, '%s() expects 1 argument of type bool' % name, call, True, False)
for name in ('enableBattleStatisticCollector', 'enableLoadingTimer'):
    assert getattr(BigWorld, name)(True) is None and getattr(BigWorld, name)(0) is None
# Only the statistics owner's ping state follows them: True resets, the timer freezes.
assert host.state()['pingResets'] == 1 and not host.state()['loadingTimer']
BigWorld.enableLoadingTimer(1)
assert host.state()['loadingTimer']
BigWorld.enableLoadingTimer(False)
none_references = sys.getrefcount(None)
for unused in range(1000):
    assert BigWorld.wg_getClientStatistics() is None
assert sys.getrefcount(None) == none_references
expect_error(TypeError, 'wg_getClientStatistics() takes no arguments.', BigWorld.wg_getClientStatistics, 1)
print('port: bindings registered once with the original call flags; timeExact float32 %r, frame %d'
      % (BigWorld.timeExact(), BigWorld.wg_getFrameTimestamp()))

# Hangar special mode: ClientHangarSpace.create L314 and __destroy L802 call shapes.
assert call_shape(HANGAR, 'ClientHangarSpace.create', 'wg_setSpecialFPSMode') == [
    (314, [], 'CALL_FUNCTION', 0)], call_shape(HANGAR, 'ClientHangarSpace.create', 'wg_setSpecialFPSMode')
assert call_shape(HANGAR, 'ClientHangarSpace.__destroy', 'wg_disableSpecialFPSMode') == [
    (802, [], 'CALL_FUNCTION', 0)]
state = host.state
assert state()['minFrameTime'] == 8 and not state()['special']
assert BigWorld.wg_disableSpecialFPSMode() is None and state()['minFrameTime'] == 8  # destroy after failed create
assert BigWorld.wg_setSpecialFPSMode() is None and state()['minFrameTime'] == 16 and state()['special']
BigWorld.wg_setSpecialFPSMode()  # repeated create keeps the first saved frame time
BigWorld.wg_disableSpecialFPSMode()
assert state()['minFrameTime'] == 8 and not state()['special']
BigWorld.wg_disableSpecialFPSMode()
assert state()['minFrameTime'] == 8
for fps, expected in ((30.0, 33), (0.0, 16), (-1.0, 16), (float('nan'), 16), (float('inf'), 0), (7, 142)):
    BigWorld.wg_setSpecialFPSMode(fps)
    assert state()['minFrameTime'] == expected, (fps, state())
BigWorld.wg_disableSpecialFPSMode()
assert state()['minFrameTime'] == 8
expect_error(TypeError, 'wg_setSpecialFPSMode() expects between 0 and 1 arguments of types float',
             BigWorld.wg_setSpecialFPSMode, 1.0, 2.0)
expect_error(TypeError, 'wg_disableSpecialFPSMode() takes no arguments.', BigWorld.wg_disableSpecialFPSMode, 1)
expect_error(TypeError, '() argument 1 optionally must be set to a float',
             BigWorld.wg_setSpecialFPSMode, 'fast')
assert state()['minFrameTime'] == 8 and not state()['special']
# An unlimited normal frame time is restored as unlimited.
host.configure({'renderer/reducedFrameRate': 60}, 1000.0)
BigWorld.wg_setSpecialFPSMode()
assert state()['minFrameTime'] == 16
BigWorld.wg_disableSpecialFPSMode()
assert state()['minFrameTime'] == 0
# Without renderer/reducedFrameRate the default special mode is unlimited, as in #1513.
host.configure({'renderer/maxFrameRate': 120}, 1000.0)
BigWorld.wg_setSpecialFPSMode()
assert state()['minFrameTime'] == 0
BigWorld.wg_disableSpecialFPSMode()
assert state()['minFrameTime'] == 8
host.setTicks(0)  # a new App start for the elapsed-time checks below
host.configure({'renderer/maxFrameRate': 120, 'renderer/reducedFrameRate': 60}, 1000.0)
print('port: Hangar special mode 8 -> 16 -> 8 ticks; repeated and early destroy are harmless')

# Account: personality.onConnected -> noteHangarLoadingState continues into addUPLMessage.
host.setTicks(1250)
collector.noteHangarLoadingState(HLS.CONNECTED, True)
assert collector._StatisticsCollector__loadingStates[HLS.CONNECTED] == 1.25
assert NOTES[-1] == 'HANGAR LOADING STATE: CONNECTED - 1.25', NOTES
host.setTicks(1250 + 4321)
collector.noteHangarLoadingState(HLS.HANGAR_READY, showSummaryNow=True)
assert collector._StatisticsCollector__hangarLoadingTime == float32(5.571) - 1.25
assert NOTES[-1].endswith(' TOTAL = ' + str(float32(5.571) - 1.25)), NOTES
print('port: noteHangarLoadingState recorded %s and summarised %r s' % (
    NOTES[-3], collector._StatisticsCollector__hangarLoadingTime))

# Arena: spaceLoadStarted -> reduced mode; arenaLoadCompleted -> normal, loading timer, reuse list.
assert call_shape(AVATAR, 'PlayerAvatar.onBecomePlayer', 'enableLoadingTimer') == [
    (340, ['True'], 'CALL_FUNCTION', 1)]
BigWorld.enableLoadingTimer(True)
controller = arena_load.ArenaLoadController()
controller.startControl(None, Recorder('arenaVisitor', {'arenaVisitor.getArenaGuiType': 1}))
controller.spaceLoadStarted()
assert state()['reduced'] and [host.sleep(n) for n in (0, 5, 20, 33)] == [20, 20, 20, 33]
controller.stopControl()  # an aborted load keeps the flag until arenaLoadCompleted, as in #1513
assert state()['reduced']
clears = state()['reuseListClears']
controller.arenaLoadCompleted()
assert not state()['reduced'] and [host.sleep(n) for n in (0, 5)] == [0, 5]
assert state()['reuseListClears'] == clears + 1
assert ('MessengerEntry.g_instance.onAvatarShowGUI', (), ()) in CALLS
controller.arenaLoadCompleted()
assert not state()['reduced'] and state()['reuseListClears'] == clears + 2
print('port: arena reduced mode floors the frame sleep at 20 ms until arenaLoadCompleted')

# Battle statistics are unavailable: getStatistics stops; leaveArena reports None.
collector.start()
PLAYER.arena.period = ARENA_PERIOD.BATTLE
collector.update()
assert collector._StatisticsCollector__state == STATE.IN_PROGRESS
assert len(SERVICES[settings_core].onSettingsChanged.handlers) == 1
assert not hasattr(BigWorld, 'getAutoDetectGraphicsSettingsScore')
base, avatar = Recorder('avatar.base'), type('Avatar', (), {})()
avatar.statsCollector, avatar.base, avatar._PlayerAvatar__projectileMover = collector, base, None
avatar._PlayerAvatar__setIsOnArena = Recorder('avatar.setIsOnArena')
avatar.lobbyContext = type('LobbyContext', (), {'collectUiStats': True})()
player_events = type('PlayerEvents', (), {'isPlayerEntityChanging': False,
                                          'onPlayerEntityChanging': Recorder('onPlayerEntityChanging')})()
leaveArena = function(AVATAR, 'PlayerAvatar.leaveArena', {
    'LOG_DEBUG': lambda *args: None, 'BigWorld': BigWorld, 'g_playerEvents': player_events,
    'BattleReplay': type('BattleReplay', (), {'g_replayCtrl': type('Replay', (), {
        'isPlaying': False, 'isRecording': False})()}),
    'LOG_CURRENT_EXCEPTION': lambda: None})
del CALLS[:]
leaveArena(avatar)
assert ('avatar.base.leaveArena', (None,), ()) in CALLS, CALLS
assert collector._StatisticsCollector__state == STATE.STOPPED
assert SERVICES[settings_core].onSettingsChanged.handlers == []
assert collector.getStatistics() == {} and collector._StatisticsCollector__state == STATE.STOPPED
print('port: wg_getClientStatistics None -> stop -> base.leaveArena(None)')

# Teardown: personality.onDisconnected L435 notes DISCONNECTED; L470 purges the URL cache.
assert call_shape(PERSONALITY, 'onDisconnected', 'purgeUrlRequestCache') == [(470, [], 'CALL_FUNCTION', 0)]
host.setUrlManager(1)
assert BigWorld.purgeUrlRequestCache() is None
assert BigWorld.purgeUrlRequestCache(1, 'ignored', key=2) is None
host.setTicks(9000)
del CALLS[:]
assert function(PERSONALITY, 'onDisconnected', disconnect_namespace(services_locator()))() is None
assert collector._StatisticsCollector__loadingStates[HLS.DISCONNECTED] == 9.0
assert ('Waiting.cancelCallback', (), ()) in CALLS and ('ServicesLocator.clear', (), ()) in CALLS
host.setUrlManager(0)
expect_error(EnvironmentError, 'PyURLRequest has not been initialised', BigWorld.purgeUrlRequestCache)
collector._StatisticsCollector__loadingStates[HLS.DISCONNECTED] = 0.0
del CALLS[:]
expect_error(EnvironmentError, 'PyURLRequest has not been initialised',
             function(PERSONALITY, 'onDisconnected', disconnect_namespace(services_locator())))
assert ('Waiting.cancelCallback', (), ()) in CALLS
print('port: onDisconnected completes through purgeUrlRequestCache; without URL::Manager only L470 raises')
