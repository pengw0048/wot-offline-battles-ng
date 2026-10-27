# Python 2.7, run inside stock_hangar_presentation_host: argv = [this, scripts.pkg]
# Drives the original #1513 PYC call sites over the production bindings and
# checks the native owner each one reaches.
import marshal
import sys
import types
import zipfile

import BigWorld
import GUI
import _presentation as host

ARCHIVE = zipfile.ZipFile(sys.argv[1])
CALLS = []


def method(member, cls, name, namespace):
    namespace.setdefault('__builtins__', __builtins__)
    code = marshal.loads(ARCHIVE.read(member)[8:])
    for part in (cls, name):
        code = [c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == part]
        assert len(code) == 1, (member, part)
        code = code[0]
    return types.FunctionType(code, namespace, name)


def expect_type_error(function, *args):
    try:
        function(*args)
    except TypeError as error:
        return str(error)
    raise AssertionError('%r%r did not raise TypeError' % (function, args))


def recorder(name, result=None):
    def call(*args):
        CALLS.append((name,) + args)
        return result
    return call


class Bag(object):
    def __init__(self, **values):
        self.__dict__.update(values)


def check(actual, expected):
    assert actual == expected, '\n%r\n!=\n%r' % (actual, expected)


# ---- argument contracts of the PY_AUTO wrappers ----
for function, args in ((BigWorld.SetDrawInflux, ()), (BigWorld.SetDrawInflux, (True, True)),
                       (BigWorld.wg_enableGUIBackground, (True,)),
                       (BigWorld.wg_setGUIBackground, ()), (BigWorld.wg_setPyModelTexture, (None, 'a')),
                       (GUI.syncMousePosition, (1.0,)), (GUI.syncMousePosition, (1.0, 'x'))):
    message = expect_type_error(function, *args)
    print 'contract:', message
check(host.calls(), [])

# ---- SetDrawInflux: listener polling and Hangar teardown ----
listener = method('scripts/client/gui/battle_control/arena_info/listeners.pyc', 'ArenaSpaceLoadListener',
                  '__loadSpaceCallback', {'BigWorld': BigWorld, '_MAX_PROGRESS_VALUE': 1.0,
                                          '_SPACE_INVALIDATION_PERIOD': 0.5})
sys.modules['BattleReplay'] = Bag(g_replayCtrl=Bag(isTimeWarpInProgress=False))
BigWorld.callback = recorder('callback', 7)
for status in (0.25, 1.0):
    BigWorld.spaceLoadStatus = lambda: status
    owner = Bag(_ArenaSpaceLoadListener__progress=0.0,
                _ArenaSpaceLoadListener__clearSpaceCallback=recorder('clear'),
                _ArenaSpaceLoadListener__onSpaceLoadUpdated=recorder('updated'),
                _ArenaSpaceLoadListener__onSpaceLoadCompleted=recorder('completed'),
                _ArenaSpaceLoadListener__loadInfluxCallback=recorder('influx'),
                _ArenaSpaceLoadListener__loadSpaceCallback='again')
    listener(owner)
    native = host.calls()
    print 'listener', status, CALLS, native
    check(native, ['tPS.drawEnabled:%d' % (status == 1.0)])
    del CALLS[:]

hangar = {'BigWorld': BigWorld, 'LOG_DEBUG': recorder('LOG_DEBUG'),
          'MusicControllerWWISE': Bag(unloadCustomSounds=recorder('unloadCustomSounds')),
          'g_keyEventHandlers': Bag(remove=recorder('keys.remove')),
          'g_mouseEventHandlers': Bag(remove=recorder('mouse.remove')),
          'MapActivities': Bag(g_mapActivities=Bag(stop=recorder('mapActivities.stop'))),
          'g_postProcessing': Bag(disable=recorder('postProcessing.disable')),
          'FovExtended': Bag(instance=lambda: Bag(resetFov=recorder('resetFov')))}
destroy = method('scripts/client/gui/ClientHangarSpace.pyc', 'ClientHangarSpace', '__destroy', hangar)
for name in ('camera', 'worldDrawEnabled', 'cancelCallback', 'delSpaceGeometryMapping', 'clearSpace',
             'releaseSpace', 'wg_disableSpecialFPSMode'):
    setattr(BigWorld, name, recorder(name))
BigWorld.isClientSpace = lambda space: True


def hangar_space():
    return Bag(_ClientHangarSpace__cam=Bag(), _ClientHangarSpace__vAppearance=None,
               _ClientHangarSpace__waitCallback=None, _ClientHangarSpace__spaceId=3,
               _ClientHangarSpace__spaceMappingId=11, handleKeyEvent='keys', handleMouseEventGlobal='mouse')


# Without the binding the original teardown stops before releasing the space.
present = BigWorld.SetDrawInflux
del BigWorld.SetDrawInflux
try:
    destroy(hangar_space())
except AttributeError:
    pass
else:
    raise AssertionError('__destroy passed without SetDrawInflux')
assert 'releaseSpace' not in [call[0] for call in CALLS]
del CALLS[:]
BigWorld.SetDrawInflux = present
space = hangar_space()
destroy(space)
order = [call[0] for call in CALLS]
check(order[order.index('mouse.remove'):], ['mouse.remove', 'mapActivities.stop', 'delSpaceGeometryMapping',
                                           'clearSpace', 'releaseSpace', 'wg_disableSpecialFPSMode',
                                           'postProcessing.disable', 'resetFov'])
check(host.calls(), ['tPS.drawEnabled:0'])
assert space._ClientHangarSpace__spaceId is None
del CALLS[:]
print 'influx: listener and Hangar teardown reach the tPS owner'

# ---- wg_enableGUIBackground / wg_setGUIBackground: BattleLoading ----
SessionProvider = Bag(addArenaCtrl=recorder('addArenaCtrl'), removeArenaCtrl=recorder('removeArenaCtrl'))


class LoadingBase(object):
    def _populate(self):
        CALLS.append(('super._populate',))

    def _disposeWithReloading(self):
        CALLS.append(('super._disposeWithReloading',))


replay = Bag(isPlaying=lambda: REPLAY[0])
REPLAY = [False]
loading = {'BigWorld': BigWorld, 'BattleReplay': replay, 'SMALL_MAP_IMAGE_SF_PATH': 'map/%s.png'}
names = ('_populate', '_dispose', 'arenaLoadCompleted', '_addArenaTypeData')
BattleLoading = type('BattleLoading', (LoadingBase,), dict(
    (name, method('scripts/client/gui/Scaleform/daapi/view/battle/shared/battle_loading.pyc',
                  'BattleLoading', name, loading)) for name in names))
loading['BattleLoading'] = BattleLoading
BigWorld.wg_updateColorGrading = recorder('wg_updateColorGrading')


def battle_loading(icon):
    view = BattleLoading()
    view.sessionProvider = SessionProvider
    view._arenaVisitor = Bag(type=Bag(getGeometryName=lambda: '05_prohorovka'))
    view._battleCtx = Bag(getArenaScreenIcon=lambda: icon)
    view.as_setMapIconS = recorder('as_setMapIconS')
    return view


check(host.drawBackground(), True)  # no GUIApp owner yet: a legitimate no-draw
check(host.calls(), [])
EDGE_INIT = ['createEffect:shaders/edgeDetect/transfer.fx', 'OverrideBlock:shaders/edgeDetect/z_pass',
             'effect.SetTexture:g_edgeDetectedMap:NULL', 'createRenderTarget:stock/Edge detect:1280x720:21:10']
assert host.initialise() and host.initialise()
check(host.calls(), ['createEffect:shaders/post_processing/background.fx'] + EDGE_INIT)
view = battle_loading('gui/maps/icons/map/screen/05_prohorovka.dds')
view._populate()
check(host.calls(), ['TextureManager.get:gui/maps/icons/map/screen/05_prohorovka.dds:111:Art/File Textures/'])
check(host.drawBackground(), True)
check(host.calls(), ['effect.SetTexture:inputTexture:gui/maps/icons/map/screen/05_prohorovka.dds',
                     'fsQuad.drawChecked:shaders/post_processing/background.fx:1280x720:'
                     'gui/maps/icons/map/screen/05_prohorovka.dds',
                     'effect.SetTexture:inputTexture:NULL'])
for playing in (False, True):
    REPLAY[0] = playing
    view.arenaLoadCompleted()
    check(host.drawBackground(), True)
    check(host.calls(), [])
    BigWorld.wg_enableGUIBackground(True, False)
REPLAY[0] = False
view._dispose()
check(host.drawBackground(), True)
check(host.calls(), [])
# A replay keeps the background through _dispose; only a later disable hides it.
BigWorld.wg_enableGUIBackground(True, False)
REPLAY[0] = True
view._dispose()
check(host.drawBackground(), True)
assert len(host.calls()) == 3
REPLAY[0] = False
BigWorld.wg_enableGUIBackground(False, True)
missing = battle_loading('gui/maps/icons/map/screen/missing.dds')
missing._populate()
check(host.calls(), ['TextureManager.get:gui/maps/icons/map/screen/missing.dds:111:Art/File Textures/'])
check(host.drawBackground(), True)
check(host.calls(), [])
missing._dispose()
host.finalise()
host.finalise()
check(host.calls(), ['effect.SetTexture:g_edgeDetectedMap:NULL'])  # the target is unbound once
host.setEffectLoads(0)
assert host.initialise()
check(host.calls(), ['createEffect:shaders/post_processing/background.fx',
                     'ERROR:WGBackgroundImage: background effect could not be loaded\n',
                     'createEffect:shaders/edgeDetect/transfer.fx',
                     'ERROR:EdgeDrawer: transfer effect could not be loaded\n',
                     'OverrideBlock:shaders/edgeDetect/z_pass',
                     'ERROR:EdgeDrawer: rigid z_pass effect could not be loaded\n',
                     'createRenderTarget:stock/Edge detect:1280x720:21:10'])
view._populate()
host.calls()
check(host.drawBackground(), True)
check(host.calls(), [])
host.finalise()
host.setEffectLoads(1)
check([call[0] for call in CALLS][:4], ['super._populate', 'addArenaCtrl', 'wg_updateColorGrading',
                                      'as_setMapIconS'])
del CALLS[:]
print 'background: BattleLoading enable/set/disable reaches the GUIApp draw owner'

# ---- wg_setPyModelTexture: ClientHangarSpace.modifyFakeShadowAsset ----
modify = method('scripts/client/gui/ClientHangarSpace.pyc', 'ClientHangarSpace', 'modifyFakeShadowAsset',
                {'BigWorld': BigWorld})
for levels in (1, 2):
    model = host.makeModel(levels)
    owner = Bag(_ClientHangarSpace__getFakeShadowModel=lambda: model)
    modify(owner, 'content/Hangars/shadow_%d.dds' % levels)
    assert owner._ClientHangarSpace__fakeShadowAsset == 'content/Hangars/shadow_%d.dds' % levels
    expected = []
    for level in range(1, levels + 1):  # the top model, then its parent chain
        expected += ['Model.gatherMaterials:model%d:*' % level,
                     'property.be:model%d/shadow.diffuseMap:content/Hangars/shadow_%d.dds' % (level, levels)]
    check(host.calls(), expected)
owner = Bag(_ClientHangarSpace__getFakeShadowModel=lambda: None)
modify(owner, 'content/Hangars/none.dds')
check(host.calls(), [])
check(BigWorld.wg_setPyModelTexture(host.makeModel(0), 'diffuseMap', 'a.dds'), True)
check(host.calls(), [])
check(BigWorld.wg_setPyModelTexture(host.makeModel(1), 'missingProperty', 'a.dds'), True)
check(host.calls(), ['Model.gatherMaterials:model1:*'])
print 'texture:', expect_type_error(BigWorld.wg_setPyModelTexture, None, 'diffuseMap', 'a.dds')
print 'texture: modifyFakeShadowAsset reaches every material of the model chain'

# ---- GUI.syncMousePosition: Cursor.detachCursor / resetMousePosition ----
cursor_namespace = {'GUI': GUI, 'BigWorld': BigWorld, 'LOG_DEBUG': recorder('LOG_DEBUG')}
detach = method('scripts/client/gui/Scaleform/managers/Cursor.pyc', 'Cursor', 'detachCursor', cursor_namespace)
reset = method('scripts/client/gui/Scaleform/managers/Cursor.pyc', 'Cursor', 'resetMousePosition',
               cursor_namespace)
mcursor = Bag(active=True, position=(0.5, 0.5))
GUI.mcursor = lambda: mcursor
BigWorld.setCursor = recorder('setCursor')
host.setGUI(1)
manager = Bag(_Cursor__savedMCursorPos=(0.25, -0.5), hide=recorder('hide'),
              _Cursor__restoreDeviceMousePosition='restore', _Cursor__saveDeviceMousePosition=recorder('save'))
detach(manager)
check(host.calls(), ['SimpleGUI.handleMouseEvent:0,0,0,0.25,-0.5'])
check([call[0] for call in CALLS], ['LOG_DEBUG', 'setCursor', 'hide', 'callback'])
del CALLS[:]
reset(manager)
check(mcursor.position, (0.0, 0.0))
check(host.calls(), ['SimpleGUI.handleMouseEvent:0,0,0,0,0'])
manager._Cursor__savedMCursorPos = None
detach(manager)
check(host.calls(), [])
host.setGUI(0)
check(GUI.syncMousePosition(3, 4), None)
check(host.calls(), [])
print 'cursor: detach/reset dispatch a zero-delta event through SimpleGUI'

# ---- wgSetEdgeDetectColors / wgAdd / wgDel: Account event, Hangar and highlights ----
import StringIO


def warnings_of(function, *args):
    saved, sys.stderr = sys.stderr, StringIO.StringIO()
    try:
        result = function(*args)
    finally:
        captured, sys.stderr = sys.stderr.getvalue(), saved
    return result, captured


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


class PlayerAccount(object):
    pass


EDGE = 'scripts/client/helpers/EdgeDetectColorController.pyc'
player_events = Bag(onAccountShowGUI=Event())
edge_namespace = {'BigWorld': BigWorld, 'g_playerEvents': player_events, 'PlayerAccount': PlayerAccount}
Controller = type('EdgeDetectColorController', (object,), dict(
    (name, method(EDGE, 'EdgeDetectColorController', name, edge_namespace))
    for name in ('create', 'updateColors', '__onAccountShowGUI', '__changeColor')))
for name in ('__onAccountShowGUI', '__changeColor'):
    setattr(Controller, '_EdgeDetectColorController' + name, getattr(Controller, name))
PLAYER = [PlayerAccount()]
BigWorld.player = lambda: PLAYER[0]
palette = {'hangar': (0.1, 0.2, 0.3, 1.0), 'self': (0.4, 0.5, 0.6, 1.0), 'enemy': (1.0, 0.0, 0.0, 1.0),
           'friend': (0.0, 1.0, 0.0, 1.0), 'flag': (1.0, 1.0, 0.0, 1.0)}
controller = Controller()
controller.settingsCore = Bag(onSettingsChanged=Event(), getSetting=lambda name: False)
controller._EdgeDetectColorController__colors = {'common': palette, 'colorBlind': palette}

# Without an EdgeDrawer the colours are still validated and None is returned.
host.finalise()
check(host.calls(), [])
check(warnings_of(BigWorld.wgSetEdgeDetectColors, [(1, 1, 1, 1)] * 5)[1].count('Using the first three'), 1)
check(BigWorld.wgAddEdgeDetectEntity(None, 9, 9), None)
check(BigWorld.wgDelEdgeDetectEntity(None), None)
check(host.drawEdges(), True)
check(host.calls(), [])
assert host.initialise()
check(host.calls(), ['createEffect:shaders/post_processing/background.fx'] + EDGE_INIT)
try:
    BigWorld.wgSetEdgeDetectColors([])
except Warning as error:
    check(str(error), 'wgSetEdgeDetectColors - colors size is zero. ' + '\t' * 9 + 'Using defaults colors.\n')
else:
    raise AssertionError('empty colours did not raise Warning')
check(host.calls(), [])
check(host.drawEdges(), True)  # no members: nothing is drawn
check(host.calls(), [])


def member_calls(mask, depth, occluded, name):
    single = ['effect.SetVector:g_colorMask:' + mask, 'effect.SetInt:depthFunc:%d' % depth,
              'effect.SetBool:edgeDetectOccluded:%d' % occluded]
    return single * 2 + ['embodiment.draw:%s:override' % name]


def frame(members, colours):
    return (['target.push:stock/Edge detect', 'device.Clear:1:0:1:0', 'context.begin:7',
             'context.pushImmediateMode', 'context.pushOverrideBlock'] + members +
            ['context.popOverrideBlock', 'context.popImmediateMode', 'context.end:7', 'context.flush:7',
             'target.pop:stock/Edge detect', 'effect.SetTexture:g_edgeDetectedMap:stock/Edge detect',
             'effect.SetVectorArray:g_edgeColors:' + colours,
             'fsQuad.drawChecked:shaders/edgeDetect/transfer.fx:1280x720:stock/Edge detect'])


# EdgeDetectColorController.create subscribes; onAccountShowGUI sets Hangar colours.
controller.create()
player_events.onAccountShowGUI({})
check(host.calls(), [])
hangar_colours = '0.1,0.2,0.3,1|1,0,0,1|0,1,0,1|1,1,0,1'
SELECTABLE = 'scripts/client/ClientSelectableObject.pyc'
highlight = method(SELECTABLE, 'ClientSelectableObject', 'highlight', {'BigWorld': BigWorld})
selectable = host.makeEntity('hangarTank', 'hangarTankDecal')
selectable._ClientSelectableObject__enabled = True
selectable._ClientSelectableObject__edged = False
selectable._ClientSelectableObject__edgeMode = 2
selectable.onMouseEnter = recorder('onMouseEnter')
selectable.onMouseLeave = recorder('onMouseLeave')
highlight(selectable, True)
assert selectable._ClientSelectableObject__edged
check(host.drawEdges(), True)
check(host.calls(), frame(member_calls('1,0,0,0', 1, 0, 'hangarTank') +
                          member_calls('1,0,0,0', 1, 0, 'hangarTankDecal'), hangar_colours))
highlight(selectable, False)
assert not selectable._ClientSelectableObject__edged
check(host.drawEdges(), True)
check(host.calls(), [])
# A battle player swaps to the self colour.
PLAYER[0] = object()
player_events.onAccountShowGUI({})
battle_colours = '0.4,0.5,0.6,1|1,0,0,1|0,1,0,1|1,1,0,1'

# Highlighter (colour, mode, flag): ally (2,0,False), enemy (1,0,False), own (0,1,True).
HIGHLIGHTER = 'scripts/client/vehicle_systems/components/highlighter.pyc'
highlighter_names = ('highlight', 'deactivate', '__doHighlightOperation')
Highlighter = type('Highlighter', (object,), dict(
    (name, method(HIGHLIGHTER, 'Highlighter', name, {'BigWorld': BigWorld})) for name in highlighter_names))
Highlighter._Highlighter__doHighlightOperation = Highlighter.__dict__['__doHighlightOperation']
Highlighter.HIGHLIGHT_OFF, Highlighter.HIGHLIGHT_ON, Highlighter.HIGHLIGHT_SIMPLE = 0, 1, 2
ALLY = [True]
vehicles = []
for name, ally in (('ally', True), ('enemy', False)):
    vehicle = host.makeEntity(name)
    vehicle.publicInfo = {'team': 1}
    item = Highlighter()
    item._Highlighter__vehicle, item._Highlighter__highlightStatus = vehicle, 0
    item._Highlighter__isPlayersVehicle = False
    item.sessionProvider = Bag(getArenaDP=lambda ally=ally: Bag(isAllyTeam=lambda team: ally))
    item.highlight(True, False)
    vehicles.append((item, vehicle))
check(host.drawEdges(), True)
ALLY_CALLS, ENEMY_CALLS = member_calls('0,0,1,0', 0, 0, 'ally'), member_calls('0,1,0,0', 0, 0, 'enemy')
check(host.calls(), frame(ALLY_CALLS + ENEMY_CALLS, battle_colours))
# Re-highlighting removes the old key first: the ally appears once, last.
vehicles[0][0].highlight(True, False)
check(host.drawEdges(), True)
check(host.calls(), frame(ENEMY_CALLS + ALLY_CALLS, battle_colours))
own = Highlighter()
own_vehicle = host.makeEntity('player')
own._Highlighter__vehicle, own._Highlighter__highlightStatus, own._Highlighter__isPlayersVehicle = own_vehicle, 0, True
own.highlight(True, False)
check(host.drawEdges(), True)
# Mode 1 is the second list, drawn after every mode-0 member.
check(host.calls(), frame(ENEMY_CALLS + ALLY_CALLS + member_calls('1,0,0,0', 3, 1, 'player'), battle_colours))
own.deactivate()
assert own._Highlighter__vehicle is None

# Invalid values warn and are still added; an index past 3 has no mask bit.
result, text = warnings_of(BigWorld.wgAddEdgeDetectEntity, own_vehicle, 7, 5, True)
check((result, text.count('Incorrect draw mode'), text.count('Incorrect color index')), (None, 1, 0))
result, text = warnings_of(BigWorld.wgAddEdgeDetectEntity, own_vehicle, 7)
check((result, text.count('Incorrect color index')), (None, 1))
check(host.drawEdges(), True)
check(host.calls(), frame(ENEMY_CALLS + ALLY_CALLS + member_calls('0,0,0,0', 0, 0, 'player'), battle_colours))
result, text = warnings_of(BigWorld.wgAddEdgeDetectEntity, own_vehicle, 3, 5)
check(text.count('Incorrect draw mode'), 1)
check(host.drawEdges(), True)
check(host.calls(), frame(ENEMY_CALLS + ALLY_CALLS + member_calls('0,0,0,1', 1, 0, 'player'), battle_colours))
print 'edge: invalid', expect_type_error(BigWorld.wgAddEdgeDetectEntity, None, 0)
print 'edge: invalid', expect_type_error(BigWorld.wgDelEdgeDetectEntity, None)
print 'edge: invalid', expect_type_error(BigWorld.wgAddEdgeDetectEntity, own_vehicle, -1)
BigWorld.wgDelEdgeDetectEntity(own_vehicle)
BigWorld.wgDelEdgeDetectEntity(own_vehicle)

# Leaving the world or replacing the model drops the stale key; no draw follows.
host.setEntity(vehicles[1][1], 0, 0)
host.setEntity(vehicles[0][1], 1, 0, 'allyReloaded')
check(host.drawEdges(), True)
check(host.calls(), [])
vehicles[0][0].deactivate()
vehicles[1][0].deactivate()

# More than four colours warn and are kept; the transfer receives four.
result, text = warnings_of(BigWorld.wgSetEdgeDetectColors, [(0.5, 0.5, 0.5, 1)] * 5)
check((result, text.count('Using the first three items.')), (None, 1))
BigWorld.wgAddEdgeDetectEntity(own_vehicle, 2)
check(host.drawEdges(), True)
check(host.calls(), frame(member_calls('0,0,1,0', 0, 0, 'player'), '|'.join(['0.5,0.5,0.5,1'] * 4)))
# Fewer than four: the missing transfer colours are zero.
BigWorld.wgSetEdgeDetectColors([(1, 0, 1, 1)])
check(host.drawEdges(), True)
check(host.calls()[-2], 'effect.SetVectorArray:g_edgeColors:1,0,1,1|0,0,0,0|0,0,0,0|0,0,0,0')

# Teardown drops members; a drawer without its target ignores additions.
host.finalise()
host.finalise()
check(host.drawEdges(), True)
host.setTargetCreates(0)
assert host.initialise()
host.calls()
BigWorld.wgAddEdgeDetectEntity(own_vehicle, 1)
check(host.drawEdges(), True)
check(host.calls(), [])
host.finalise()
host.setTargetCreates(1)
print 'edge: Account colours, selection and highlight membership reach the z-pass and transfer'
