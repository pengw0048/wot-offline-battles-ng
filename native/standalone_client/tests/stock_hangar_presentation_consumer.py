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
             'createRenderTarget:stock/Edge detect:1280x720:21:10']
# Every write must reach the material's private binding, never the shared definition.
BG = 'shaders/post_processing/background.fx'
BG_BINDING, MAP = BG + '#binding', 'gui/maps/icons/map/screen/05_prohorovka.dds'
assert host.initialise() and host.initialise()
check(host.calls(), ['createEffect:shaders/post_processing/background.fx'] + EDGE_INIT)
view = battle_loading('gui/maps/icons/map/screen/05_prohorovka.dds')
view._populate()
check(host.calls(), ['TextureManager.get:gui/maps/icons/map/screen/05_prohorovka.dds:111:Art/File Textures/'])
check(host.drawBackground(), True)
check(host.calls(), ['effect.SetTexture:%s:inputTexture:%s' % (BG_BINDING, MAP),
                     'fsQuad.drawChecked:%s:1280x720:inputTexture=%s' % (BG, MAP),
                     'effect.SetTexture:%s:inputTexture:NULL' % BG_BINDING])
# A failed binding selection writes nothing and still leaves the binding clear.
host.setSelectFails(1)
check(host.drawBackground(), False)
check(host.calls(), ['effect.SetTexture:%s:inputTexture:NULL' % BG_BINDING])
host.setSelectFails(0)
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
check(host.calls(), ['effect.SetTexture:%s:inputTexture:NULL' % BG_BINDING])  # binding cleared once
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

# ClientHangarSpace.__onFakeShadowLoaded: only one TankHangarShadowProxy reaches the texture.
Proxy = type('TankHangarShadowProxy', (object,), {})
shadow_namespace = {'BigWorld': BigWorld, '_CFG': {'shadow_model_name': 'shadow.model'},
                    'TankHangarShadowProxy': Bag(TankHangarShadowProxy=Proxy),
                    'LOG_ERROR': recorder('LOG_ERROR'), 'LOG_DEBUG': recorder('LOG_DEBUG')}
loaded = method('scripts/client/gui/ClientHangarSpace.pyc', 'ClientHangarSpace', '__onFakeShadowLoaded',
                shadow_namespace)
get_model = method('scripts/client/gui/ClientHangarSpace.pyc', 'ClientHangarSpace', '__getFakeShadowModel',
                   shadow_namespace)
ENTITIES = {}


def create_entity(kind, space, vehicle, position, orientation, properties):
    CALLS.append(('createEntity', kind, space, position, orientation))
    ENTITIES[77] = Bag()
    return 77


BigWorld.createEntity = create_entity
BigWorld.entity = lambda entity: ENTITIES.get(entity)


def proxy(x):
    result = Proxy()
    result.position, result.roll, result.pitch, result.yaw = (x, 0.0, 0.0), 0.0, 0.0, 1.5
    return result


class Refs(dict):
    def __init__(self, items, failed):
        dict.__init__(self, items)
        self.failedIDs = failed


for name, refs, objects, created in (
        ('failed', Refs({'shadow.model': 1}, ['shadow.model']), [proxy(1.0)], False),
        ('missing', Refs({}, []), [proxy(1.0)], False),
        ('no proxy', None, [Bag()], False),
        ('two proxies', None, [proxy(1.0), proxy(2.0)], False),
        ('one proxy', None, [Bag(), proxy(3.0)], True)):
    shadow = host.makeModel(1)
    ENTITIES.clear()
    BigWorld.userDataObjects = dict(enumerate(objects))
    space = Bag(_ClientHangarSpace__spaceId=5, _ClientHangarSpace__fakeShadowId=None,
                _ClientHangarSpace__fakeShadowScale=1.0, _ClientHangarSpace__fakeShadowAsset='shadow.dds',
                modifyFakeShadowScale=recorder('modifyFakeShadowScale'))
    space._ClientHangarSpace__getFakeShadowModel = lambda space=space: get_model(space)
    space.modifyFakeShadowAsset = lambda asset, space=space: modify(space, asset)
    loaded(space, refs if refs is not None else Refs({'shadow.model': shadow}, []))
    assert (space._ClientHangarSpace__fakeShadowId == 77) == created, name
    native = host.calls()
    check(native, ['Model.gatherMaterials:model1:*', 'property.be:model1/shadow.diffuseMap:shadow.dds']
          if created else [])
    if created:
        assert ENTITIES[77].model is shadow and ENTITIES[77].model.yaw == 1.5
    del CALLS[:]
print 'texture: __onFakeShadowLoaded reaches the model only with one shadow proxy'
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


ZPASS = ('shaders/edgeDetect/z_pass.fx#binding', 'shaders/edgeDetect/z_pass_skinned.fx#binding')
TRANSFER = 'shaders/edgeDetect/transfer.fx'
TRANSFER_BINDING = TRANSFER + '#binding'


def member_calls(mask, depth, occluded, name):
    calls = []
    for owner in ZPASS:
        calls += ['effect.SetVector:%s:g_colorMask:%s' % (owner, mask),
                  'effect.SetInt:%s:depthFunc:%d' % (owner, depth),
                  'effect.SetBool:%s:edgeDetectOccluded:%d' % (owner, occluded)]
    # The immediate render op begins the override and must see this member's values.
    return calls + ['embodiment.draw:%s:depthFunc=%d;edgeDetectOccluded=%d;g_colorMask=%s' %
                    (name, depth, occluded, mask)]


def frame(members, colours):
    return (['target.push:stock/Edge detect', 'device.Clear:1:0:1:0', 'context.begin:7',
             'context.pushImmediateMode', 'context.pushOverrideBlock'] + members +
            ['context.popOverrideBlock', 'context.popImmediateMode', 'context.end:7', 'context.flush:7',
             'target.pop:stock/Edge detect',
             'effect.SetTexture:%s:g_edgeDetectedMap:stock/Edge detect' % TRANSFER_BINDING,
             'effect.SetVectorArray:%s:g_edgeColors:%s' % (TRANSFER_BINDING, colours),
             'fsQuad.drawChecked:%s:1280x720:g_edgeColors=%s;g_edgeDetectedMap=stock/Edge detect' %
             (TRANSFER, colours)])


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
check(host.calls()[-2], 'effect.SetVectorArray:%s:g_edgeColors:1,0,1,1|0,0,0,0|0,0,0,0|0,0,0,0' %
      TRANSFER_BINDING)

# A failed binding selection skips the member and reports the pass as failed.
host.setSelectFails(1)
check(host.drawEdges(), False)
check(host.calls(), ['target.push:stock/Edge detect', 'device.Clear:1:0:1:0', 'context.begin:7',
                     'context.pushImmediateMode', 'context.pushOverrideBlock', 'context.popOverrideBlock',
                     'context.popImmediateMode', 'context.end:7', 'context.flush:7',
                     'target.pop:stock/Edge detect'])
host.setSelectFails(0)
# A device reset releases the target from the transfer binding before retiring it.
host.resetDevice()
check(host.calls(), ['effect.SetTexture:%s:g_edgeDetectedMap:NULL' % TRANSFER_BINDING,
                     'effect.SetTexture:%s:g_edgeDetectedMap:NULL' % TRANSFER_BINDING,
                     'createRenderTarget:stock/Edge detect:1280x720:21:10'])
check(host.drawEdges(), True)
check(host.calls()[-1], 'fsQuad.drawChecked:%s:1280x720:g_edgeColors=1,0,1,1|0,0,0,0|0,0,0,0|0,0,0,0;'
      'g_edgeDetectedMap=stock/Edge detect' % TRANSFER)

# Teardown releases both bindings' textures and drops members; a drawer
# without its target ignores additions.
host.finalise()
# Unmanaged then managed release; this background material never selected a binding.
check(host.calls(), ['effect.SetTexture:%s:g_edgeDetectedMap:NULL' % TRANSFER_BINDING] * 2)
host.finalise()
check(host.drawEdges(), True)
check(host.calls(), [])
host.setTargetCreates(0)
assert host.initialise()
host.calls()
BigWorld.wgAddEdgeDetectEntity(own_vehicle, 1)
check(host.drawEdges(), True)
check(host.calls(), [])
host.finalise()
host.setTargetCreates(1)
print 'edge: Account colours, selection and highlight membership reach the z-pass and transfer'

# ---- Gamma and colour filter settings: account_helpers/settings_core/options.pyc ----
import struct


def float32(value):
    return struct.unpack('f', struct.pack('f', value))[0]


OPTIONS = 'scripts/client/account_helpers/settings_core/options.pyc'
settings = {'BigWorld': BigWorld}
gamma_get = method(OPTIONS, 'GammaSetting', '_get', settings)
gamma_set = method(OPTIONS, 'GammaSetting', '_set', settings)


def ramp_matches(gamma):
    actual, original = host.gammaRamps(gamma)
    check(actual, original)
    return actual


# 00925250 stores the raw float and marks it dirty; present (or createDevice)
# clamps it to 0.5..6, stores that back and applies the ramp (00929250).
host.detachDevice()
check(gamma_get(None), 1.0)
BigWorld.setGammaCorrection(0.25)  # before a device: stored raw
check(BigWorld.getGammaCorrection(), 0.25)
check(host.calls(), [])
BigWorld.setGammaCorrection(1.25)  # the devicePreferences seed
host.applyDeviceGamma()  # createDevice applies the stored value
check(len(host.calls()), 1)
ramp_matches(1.25)
check(gamma_get(None), 1.25)
host.presentFrame()  # nothing dirty: no ramp
check(host.calls(), [])
for raw, applied in ((0.25, 0.5), (7, 6.0), (1.1, 1.1), (0.5, 0.5), (6.0, 6.0)):
    BigWorld.setGammaCorrection(raw)
    check(BigWorld.getGammaCorrection(), float32(raw))
    check(host.calls(), [])
    host.presentFrame()
    check(len(host.calls()), 1)
    check(BigWorld.getGammaCorrection(), float32(applied))
    ramp_matches(float32(applied))
# GammaSetting clamps the UI value to 0.5..2.0 before the native setter.
for ui, native in ((-3, 0.5), (0.5, 0.5), (1.0, 1.0), (1.37, 1.37), (2.0, 2.0), (4, 2.0)):
    gamma_set(None, ui)
    check(gamma_get(None), float32(native))
    check(host.calls(), [])
    host.presentFrame()
    check(len(host.calls()), 1)
    ramp_matches(float32(native))
gamma_set(None, 0.5)
host.presentFrame()
host.calls()
low = ramp_matches(0.5)
gamma_set(None, 2.0)
host.presentFrame()
host.calls()
high = ramp_matches(2.0)
assert high[64] > low[64]  # a higher gamma brightens the mid-tones
# Apply then cancel before the next frame restores the previous value.
previous = gamma_get(None)
gamma_set(None, 0.8)
gamma_set(None, previous)
check(gamma_get(None), previous)
host.presentFrame()
check(len(host.calls()), 1)
ramp_matches(previous)
# A reset marks the value dirty; the next present reapplies the user value.
host.resetDeviceGamma()
check(host.calls(), [])
host.presentFrame()
check(len(host.calls()), 1)
check(gamma_get(None), previous)
ramp_matches(previous)
host.presentFrame()
check(host.calls(), [])
for function, args in ((BigWorld.getGammaCorrection, (1,)), (BigWorld.setGammaCorrection, ()),
                       (BigWorld.setGammaCorrection, ('bright',)), (BigWorld.setGammaCorrection, (1.0, 2.0)),
                       (BigWorld.getColorGradingStrength, (0,)), (BigWorld.setColorGradingStrength, ()),
                       (BigWorld.setColorGradingStrength, (None,)), (BigWorld.setColorGradingStrength, (1, 2))):
    print 'settings contract:', expect_type_error(function, *args)
check(BigWorld.getGammaCorrection(), previous)
check(host.calls(), [])
print 'gamma: raw dirty store, present/create clamp and ramp, GammaSetting, cancel and reset'

# The absent ColorGrading owner: 0.0 reads back as the UI minimum 25, and a
# converted setter value is not retained.
color = Bag()
color._adjustValue = types.MethodType(method(OPTIONS, 'ColorFilterIntensitySetting', '_adjustValue', settings), color)
color_get = method(OPTIONS, 'ColorFilterIntensitySetting', '_get', settings)
color_set = method(OPTIONS, 'ColorFilterIntensitySetting', '_set', settings)
check(BigWorld.getColorGradingStrength(), 0.0)
check(color_get(color), 25)
for ui in (-2, 25, 37, 100, 120):
    check(color_set(color, ui), None)
    check(BigWorld.getColorGradingStrength(), 0.0)
    check(color_get(color), 25)
check(BigWorld.setColorGradingStrength(0.37), None)
check(host.calls(), [])
print 'colour filter: the absent owner reads 0.0 and stores nothing'
