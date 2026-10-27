# -*- coding: utf-8 -*-
# Python 2.7 driver for the production battle canvas/minimap bindings.
# argv[1] is the probe directory, argv[2] the exact #1513 scripts.pkg.
import marshal
import StringIO
import sys
import types
import weakref
import zipfile

sys.path.insert(0, sys.argv[1])
import battle_gui_probe as probe  # noqa: E402
import GUI  # noqa: E402

PACKAGE = zipfile.ZipFile(sys.argv[2])
IDENTITY = (1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0)


def translated(x, y, z):
    return probe.Matrix(IDENTITY[:12] + (x, y, z, 1.0))


def raises(kind, text, function, *args):
    try:
        function(*args)
    except kind as error:
        assert text is None or str(error) == text, (str(error), text)
        return
    raise AssertionError('%s was not raised by %r' % (kind.__name__, function))


def printed(function, *args):
    saved, sys.stderr = sys.stderr, StringIO.StringIO()
    try:
        result = function(*args)
        assert not sys.exc_info()[0]
        return result, sys.stderr.getvalue()
    finally:
        sys.stderr = saved


def stub(name, **values):
    parts = name.split('.')
    for i in range(1, len(parts) + 1):
        path = '.'.join(parts[:i])
        if path not in sys.modules:
            module = types.ModuleType(path)
            module.__path__ = []
            sys.modules[path] = module
            if i > 1:
                setattr(sys.modules['.'.join(parts[:i - 1])], parts[i - 1], module)
    sys.modules[name].__dict__.update(values)
    return sys.modules[name]


def load(name, path):
    module = stub(name)
    module.__file__ = 'scripts/client/' + path[:-1]
    exec marshal.loads(PACKAGE.read('scripts/client/' + path)[8:]) in module.__dict__
    return module


class Anything(object):
    def __getattr__(self, name):
        return Anything()

    def __call__(self, *args, **kwargs):
        return Anything()


class Position(object):
    z = 0


class Component(object):
    """The owner GUI.Flash of the external marker movie or of the battle app."""

    def __init__(self):
        self.children = {}
        self.position = Position()

    def addChild(self, child, name):
        self.children[name] = child

    def delChild(self, child):
        for name, value in self.children.items():
            if value is child:
                del self.children[name]


def check_direct_canvas():
    probe.reset()
    probe.node('markersRoot')
    probe.node('vehicleMarkersCanvas', 'markersRoot')
    probe.path('_root.vehicleMarkersCanvas', 'vehicleMarkersCanvas')
    probe.symbol('VehicleMarker', ['marker', 'bgShadow'])
    movie = probe.movie()
    message = 'GUI.WGVehicleMarkersCanvasFlash: Argument parsing error: Expected a PyMovieView object'
    raises(TypeError, message, GUI.WGVehicleMarkersCanvasFlashAS3)
    raises(TypeError, message, GUI.WGVehicleMarkersCanvasFlashAS3, 1)
    raises(TypeError, message, GUI.WGVehicleMarkersCanvasFlashAS3, movie, movie)
    canvas = GUI.WGVehicleMarkersCanvasFlashAS3(movie)
    assert type(canvas).__name__ == 'WGVehicleMarkersCanvasGUIComponentAS3'
    assert type(canvas).__base__.__name__ == 'FlashGUIComponent'
    root = probe.display('vehicleMarkersCanvas')
    assert (root['flags'], root['x'], root['y']) == (3, 0.0, 0.0), root
    assert [round(v, 5) for v in canvas.scaleProperties] == [50.0, 100.0, 100.0, -0.3]
    canvas.wg_inputKeyMode = 2
    assert canvas.wg_inputKeyMode == 2

    # Placeholder IDs start at 0; the native canvas accepts active+None.
    assert canvas.addMarker(None, 'VehicleMarker') == 0
    assert canvas.addMarker(None, 'VehicleMarker', True) == 1
    assert type(canvas.addMarker(translated(0, 0, 50), 'VehicleMarker', True)) is int
    raises(TypeError, None, canvas.addMarker, 'matrix', 'VehicleMarker')
    raises(TypeError, None, canvas.addMarker, None)
    raises(RuntimeError, "GUI.WGVehicleMarkersCanvasFlash.addMarker - cannot attach symbol 'Nope'",
           canvas.addMarker, None, 'Nope')
    assert canvas.addMarker(None, 'VehicleMarker') == 3
    assert probe.children('vehicleMarkersCanvas') == ['VehicleMarker0', 'VehicleMarker1',
                                                      'VehicleMarker2', 'VehicleMarker3']
    probe.log()

    # Stale IDs: delMarker/markerInvoke raise, markerSetActive reports, markerSetMatrix is silent.
    raises(KeyError, None, canvas.delMarker, 9)
    raises(KeyError, None, canvas.markerInvoke, 9, ('f',))
    result, text = printed(canvas.markerSetActive, 9, True)
    assert result is None and 'GUI.WGVehicleMarkersCanvasFlash.markerSetActive - invalid marker id 9' in text, text
    assert canvas.markerSetMatrix(9, translated(1, 2, 3)) is None
    assert canvas.delMarker(3) is None
    raises(KeyError, None, canvas.delMarker, 3)
    assert probe.log() == ['call vehicleMarkersCanvas.removeChild(VehicleMarker3)']

    # invokePy2As shapes on marker 2.
    assert canvas.markerInvoke(2, 'updateState') is None
    assert canvas.markerInvoke(2, u'hide') is None
    canvas.markerInvoke(2, ('updateState', 'damaged', True, u'танк', 3, 2L, 1.5, None,
                            [1], (2,), {'a': 1}, Anything()))
    canvas.markerInvoke(2, ('setHealth', [10, 'x']))
    canvas.markerInvoke(2, ('setHealth', (10, 'x')))
    canvas.markerInvoke(2, ('setHealth', [10], 1))
    assert probe.log() == [
        'call VehicleMarker2.updateState()', 'call VehicleMarker2.hide()',
        "call VehicleMarker2.updateState('damaged',true,'\xd1\x82\xd0\xb0\xd0\xbd\xd0\xba',3.000000,"
        "2.000000,1.500000,null,null,null,null,null)",
        "call VehicleMarker2.setHealth(10.000000,'x')",
        'call VehicleMarker2.setHealth(null)',
        'call VehicleMarker2.setHealth(null,1.000000)']
    raises(Exception, 'invokePy2As expected at least 1 argument.', canvas.markerInvoke, 2, ())
    raises(Exception, 'invokePy2As - The first argument must be a String.', canvas.markerInvoke, 2, (1, 2))
    raises(Exception, 'invokePy2As - The first argument must be a String.', canvas.markerInvoke, 2,
           (u'name',))
    raises(Exception, 'invokePy2As - Failed to invoke method. Bad argument count.', canvas.markerInvoke,
           2, ('setHealth', []))
    raises(TypeError, None, canvas.markerInvoke, 2, 5)
    assert probe.log() == []
    for kind, expected in (('number', 2.5), ('string', 'done'), ('bool', True)):
        probe.configure(kind, [])
        assert canvas.markerInvoke(2, 'get') == expected
    for kind in ('int', 'object'):
        probe.configure(kind, [])
        raises(SystemError, 'error return without exception set', canvas.markerInvoke, 2, 'get')
    probe.configure('', ['broken'])
    raises(Exception, 'invokePy2As - Failed to invoke method.', canvas.markerInvoke, 2, ('broken', 1))
    probe.configure('', [])
    probe.log()
    return canvas


def check_markers_manager(package):
    probe.reset()
    probe.node('markersRoot')
    probe.node('vehicleMarkersCanvas', 'markersRoot')
    probe.path('_root.vehicleMarkersCanvas', 'vehicleMarkersCanvas')
    probe.symbol('VehicleMarker', ['marker', 'bgShadow'])
    probe.camera(IDENTITY, (1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 1.0, 0, 0, -0.5, 0), 0.5, 1500.0, 1.0,
                 960.0, 540.0)

    class ExternalFlashComponent(object):
        def __init__(self, settings):
            self.settings = settings
            self.component = Component()
            self.movie = probe.movie()

        def _populate(self):
            pass

        def _dispose(self):
            pass

    class Meta(object):
        pass

    class Settings(object):
        markerScaleSettings = (40.0, 100.0, 100.0, 0.3)
        markerBgSettings = (30.0, 100.0, 100.0, 0.5)

    stub('gui', DEPTH_OF_VehicleMarker=0.5, GUI_SETTINGS=Settings())
    stub('gui.Scaleform.daapi.view.battle.shared.markers2d.plugins', IMarkersManager=object)
    stub('gui.Scaleform.daapi.view.battle.shared.markers2d.settings', MARKERS_MANAGER_SWF='battleVehicleMarkersApp.swf')
    stub('gui.Scaleform.daapi.view.external_components', ExternalFlashComponent=ExternalFlashComponent,
         ExternalFlashSettings=lambda *args: args)
    stub('gui.Scaleform.daapi.view.meta.VehicleMarkersManagerMeta', VehicleMarkersManagerMeta=Meta)
    load('gui.Scaleform.genConsts.BATTLE_VIEW_ALIASES', 'gui/Scaleform/genConsts/BATTLE_VIEW_ALIASES.pyc')
    load('gui.Scaleform.genConsts.ROOT_SWF_CONSTANTS', 'gui/Scaleform/genConsts/ROOT_SWF_CONSTANTS.pyc')
    stub('gui.shared.utils.plugins', PluginsCollection=object)
    stub('helpers.dependency', instance=lambda cls: None, descriptor=lambda cls: None)
    stub('skeletons.gui.battle_session', IBattleSessionProvider=object)
    manager_module = load('markers2d_manager',
                          'gui/Scaleform/daapi/view/battle/shared/markers2d/manager.pyc')

    manager = manager_module.MarkersManager()
    assert manager.component.wg_inputKeyMode == 2 and manager.component.position.z == 0.5
    manager._MarkersManager__addCanvas(None)
    canvas = manager._MarkersManager__canvas
    assert manager.component.children == {'vehicleMarkersCanvas': canvas}
    assert canvas.wg_inputKeyMode == 2
    assert [round(v, 5) for v in canvas.scaleProperties] == [40.0, 100.0, 100.0, 0.3]
    assert [round(v, 5) for v in canvas.alphaProperties] == [30.0, 100.0, 100.0, 0.5]
    proxy = manager._MarkersManager__canvasProxy
    assert proxy() is canvas

    # VehicleMarkerPlugin.__addMarkerToPool without a proxy, then __onVehicleMarkerAdded.
    placeholder = manager.createMarker('VehicleMarker', matrixProvider=None, active=False)
    assert placeholder == 0
    raises(ValueError, 'Active marker VehicleMarker must has matrixProvider', manager.createMarker,
           'VehicleMarker', None, True)
    near = translated(0.0, 0.0, 100.0)
    enemy = manager.createMarker('VehicleMarker', matrixProvider=translated(5.0, 0.0, 400.0), active=True)
    assert enemy == 1
    manager.setMarkerMatrix(placeholder, near)
    manager.setMarkerActive(placeholder, True)
    probe.tick(canvas)
    # The nearer placeholder moves above the enemy marker.
    assert probe.children('vehicleMarkersCanvas') == ['VehicleMarker1', 'VehicleMarker0']
    shown = probe.display('VehicleMarker0')
    assert shown['visible'] and shown['flags'] == 0x3040, shown
    assert probe.display('marker')['xScale'] < 100.0

    # MarkerPlugin._invokeMarker -> invokeMarker(markerID, *signature).
    probe.log()
    manager.invokeMarker(enemy, 'updateState', 'damaged', False, '')
    manager.invokeMarker(enemy, 'setHealth', [500, 1000])
    assert probe.log() == ["call VehicleMarker1.updateState('damaged',false,'')",
                           'call VehicleMarker1.setHealth(500.000000,1000.000000)']

    # _hideVehicleMarker: deactivate, clear the provider, later re-attach a new vehicle matrix.
    manager.setMarkerActive(enemy, False)
    manager.setMarkerMatrix(enemy, None)
    probe.tick(canvas)
    hidden = probe.display('VehicleMarker1')
    assert not hidden['visible'] and hidden['flags'] == 0x40
    manager.setMarkerActive(enemy, True)
    probe.tick(canvas)
    assert probe.display('VehicleMarker1')['calls'] == hidden['calls']
    manager.setMarkerMatrix(enemy, translated(-5.0, 0.0, 50.0))
    probe.tick(canvas)
    moved = probe.display('VehicleMarker1')
    assert moved['visible'] and moved['projection'][3] < 0 and moved['calls'] == hidden['calls'] + 1
    # Marker 1 (z 50) is again nearer than marker 0 (z 100) and is swapped back above it.
    assert probe.children('vehicleMarkersCanvas') == ['VehicleMarker0', 'VehicleMarker1'], \
        probe.children('vehicleMarkersCanvas')

    manager.destroyMarker(enemy)
    raises(AssertionError, 'Marker is not added by given ID', manager.destroyMarker, enemy)
    assert manager.createMarker('VehicleMarker', matrixProvider=None, active=False) == 1
    manager.stopPlugins()
    assert manager.component.children == {} and manager._MarkersManager__canvas is None
    del canvas
    assert proxy() is None


def check_minimap(package):
    probe.reset()
    path = '_level0.root.views.main.minimap.entriesContainer'
    probe.node('battle')
    probe.node('entriesContainer', 'battle')
    for name in ('points', 'icons', 'equipments', 'deadVehicles', 'aliveVehicles', 'personal', 'flags'):
        probe.node(name, 'entriesContainer')
    probe.symbol('VehicleEntry', [])
    probe.symbol('ViewPointEntry', [])

    movie = probe.movie()
    raises(TypeError, 'GUI.WGMinimapFlash: Argument parsing error: Expected a PyMovieView object and String',
           GUI.WGMinimapFlashAS3, movie)
    raises(TypeError, 'GUI.WGMinimapFlash: Argument parsing error: Expected a PyMovieView object',
           GUI.WGMinimapFlashAS3, 1, path)
    raises(TypeError, "GUI.WGMinimapFlash: Argument parsing error: PyMovieView object hasn't '%s' movie clip."
           % path, GUI.WGMinimapFlashAS3, movie, path)
    probe.path(path, 'entriesContainer')

    class Meta(object):
        def __init__(self):
            self.backgrounds = []
            self.app = None

        def as_setBackgroundS(self, path):
            self.backgrounds.append(path)

        def _dispose(self):
            self.disposed = True

    class App(object):
        def __init__(self):
            self.movie = movie
            self.component = Component()

    stub('account_helpers.settings_core.options', MinimapVehModelsSetting=Anything())
    load('gui.Scaleform.genConsts.APP_CONTAINERS_NAMES', 'gui/Scaleform/genConsts/APP_CONTAINERS_NAMES.pyc')
    stub('shared_utils', BitmaskHelper=type('BitmaskHelper', (object,), {}))
    settings = load('gui.Scaleform.daapi.view.battle.shared.minimap.settings',
                    'gui/Scaleform/daapi/view/battle/shared/minimap/settings.pyc')
    assert settings.MINIMAP_COMPONENT_PATH == path, settings.MINIMAP_COMPONENT_PATH
    stub('gui.Scaleform.daapi.view.battle.shared.minimap.plugins')
    stub('gui.Scaleform.daapi.view.meta.MinimapMeta', MinimapMeta=Meta)
    stub('gui.battle_control', minimap_utils=types.ModuleType('minimap_utils'), avatar_getter=Anything())
    sys.modules['gui.battle_control'].minimap_utils.MINIMAP_SIZE = (210, 210)
    stub('Math', Vector2=lambda value: tuple(float(v) for v in value))
    stub('SoundGroups', g_instance=Anything())
    stub('debug_utils', LOG_WARNING=lambda *args: None)
    stub('AvatarInputHandler', AvatarInputHandler=Anything())
    stub('constants', IS_DEVELOPMENT=False)
    stub('skeletons.account_helpers.settings_core', ISettingsCore=object)
    component_module = load('minimap_component', 'gui/Scaleform/daapi/view/battle/shared/minimap/component.pyc')

    class Type(object):
        def getBoundingBox(self):
            return (-500.0, -400.0), (500.0, 600.0)

        def getMinimapTexture(self):
            return 'spaces/02_malinovka/mmap.dds'

    class Gui(object):
        def isBootcampBattle(self):
            return False

    class ArenaVisitor(object):
        type = Type()
        gui = Gui()

    minimap = component_module.MinimapComponent()
    minimap.app = App()
    assert minimap._MinimapComponent__createComponent(ArenaVisitor()) is True
    native = minimap._MinimapComponent__component
    assert minimap.app.component.children == {'minimap': native}
    assert native.wg_inputKeyMode == 2 and native.mapSize == (210.0, 210.0)
    assert minimap.backgrounds and 'mmap' in minimap.backgrounds[0]
    result, text = printed(native.setBackground, None)
    assert result is False and 'NotImplementedError: GUI.WGMinimapFlash.setBackground - method not implemented' in text

    vehicle = translated(120.0, 0.0, -45.0)
    first = minimap.addEntry('VehicleEntry', 'aliveVehicles', matrix=vehicle, active=True)
    personal = minimap.addEntry('ViewPointEntry', 'personal', matrix=translated(0, 0, 0), active=True,
                                transformProps=settings.TRANSFORM_FLAG.FULL)
    assert (first, personal) == (1, 2)
    missing, text = printed(minimap.addEntry, 'VehicleEntry', 'nowhere')
    assert missing == 0 and "Container 'nowhere' is not found into the movie." in text
    assert minimap._MinimapComponent__ids == set([1, 2])
    probe.log()
    # MinimapComponent.invoke(entryID, *signature), as EntriesPlugin._invoke forwards it.
    minimap.invoke(first, 'setVehicleInfo', 1, 'ussr', 'R04_T-34', 'mediumTank', 'enemy')
    minimap.invoke(first, 'setDead')
    assert probe.log() == ["call VehicleEntry1.setVehicleInfo(1.000000,'ussr','R04_T-34','mediumTank','enemy')",
                           'call VehicleEntry1.setDead()']
    probe.tick(native)
    entry = probe.display('VehicleEntry1')
    assert entry['flags'] == 0x47 and entry['visible'], entry
    assert abs(entry['x'] - 120 * 0.21) < 1e-4 and abs(entry['y'] - 145 * 0.21) < 1e-4, entry

    # Remembered position: deactivate and swap in the location matrix.
    minimap.setActive(first, False)
    minimap.setMatrix(first, translated(-480.0, 0.0, 480.0))
    probe.tick(native)
    assert probe.display('VehicleEntry1')['flags'] == 0x40 and not probe.display('VehicleEntry1')['visible']
    minimap.setActive(first, True)
    probe.tick(native)
    assert abs(probe.display('VehicleEntry1')['x'] + 480 * 0.21) < 1e-4
    minimap.move(first, 'deadVehicles')
    assert probe.children('deadVehicles') == ['VehicleEntry1']
    result, text = printed(minimap.move, first, 'nowhere')
    assert result is None and "Container 'nowhere' is not found" in text

    # _dispose deletes every recorded ID, then plugin cleanup deletes one again.
    class Plugins(object):
        def stop(self):
            minimap.delEntry(first)

        def fini(self):
            self.finished = True

    minimap._MinimapComponent__plugins = Plugins()
    result, text = printed(minimap._dispose)
    assert 'GUI.WGMinimapFlash.delEntry - invalid entry id 1' in text, text
    assert probe.children('aliveVehicles') == [] and probe.children('personal') == []
    assert minimap.app.component.children == {} and minimap._MinimapComponent__component is None
    for method, args in (('entryInvoke', (1, 'f')), ('entrySetMatrix', (1, None)),
                         ('entrySetActive', (1, True)), ('moveEntry', (1, 'personal'))):
        result, text = printed(getattr(native, method), *args)
        assert result is None and 'GUI.WGMinimapFlash.%s - invalid entry id 1' % method in text, text
    # A fresh entry reuses the most recently freed ID.
    assert native.addEntry('VehicleEntry', 'aliveVehicles') == 2


check_direct_canvas()
check_markers_manager(PACKAGE)
check_minimap(PACKAGE)
print('Production battle canvas/minimap bindings passed the exact PYC consumers')
