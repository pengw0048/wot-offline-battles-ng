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
assert host.initialise() and host.initialise()
check(host.calls(), ['createEffect:shaders/post_processing/background.fx'])
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
host.setEffectLoads(0)
assert host.initialise()
check(host.calls(), ['createEffect:shaders/post_processing/background.fx',
                     'ERROR:WGBackgroundImage: background effect could not be loaded\n'])
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
