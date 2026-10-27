# Python 2.7: drives the original gun_marker_ctrl, gm_components, damage_panel,
# indicators and Flash PYC over the production battle-GUI bindings.
import imp
import marshal
import math
import struct
import sys
import types
import zipfile

import _battle_gui_host as host

PACKAGE = zipfile.ZipFile(sys.argv[1])
REAL = {
    'AvatarInputHandler.aih_constants', 'AvatarInputHandler.gun_marker_ctrl',
    'gui.Scaleform.daapi.view.battle.shared.crosshair.settings',
    'gui.Scaleform.daapi.view.battle.shared.crosshair.gm_components',
    'gui.Scaleform.daapi.view.battle.shared.damage_panel',
    'gui.Scaleform.daapi.view.battle.shared.indicators',
    'gui.Scaleform.Flash', 'gui.Scaleform.genConsts.ROOT_SWF_CONSTANTS',
    'gui.Scaleform.genConsts.APP_CONTAINERS_NAMES',
}
AUTO_ROOTS = {'BigWorld', 'Math', 'BattleReplay', 'constants', 'AvatarInputHandler', 'ProjectileMover',
              'debug_utils', 'helpers', 'skeletons', 'gui', 'account_helpers', 'shared_utils',
              'CommandMapping', 'SoundGroups', 'ReplayEvents', '_Scaleform'}
STUBS = {}


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def check(condition, message):
    if not condition:
        raise AssertionError(message)


class _AutoMeta(type):
    """Opaque stand-ins for modules and names outside the exercised contract."""
    def __getattr__(cls, name):
        if name.startswith('__'):
            raise AttributeError(name)
        value = _AutoMeta(name, (object,), {'_auto': True})
        setattr(cls, name, value)
        return value

    def __call__(cls, *args, **kwargs):
        if cls.__dict__.get('_auto'):
            return _AutoMeta(cls.__name__, (object,), {'_auto': True})
        return type.__call__(cls, *args, **kwargs)

    def __iter__(cls):
        return iter(())

    def __iadd__(cls, other):
        return cls

    def __isub__(cls, other):
        return cls


class AutoModule(types.ModuleType):
    def __getattr__(self, name):
        if name.startswith('__'):
            raise AttributeError(name)
        full = self.__name__ + '.' + name
        if full in REAL or full in STUBS:
            __import__(full)
            return sys.modules[full]
        value = _AutoMeta(name, (object,), {'_auto': True})
        setattr(self, name, value)
        return value


def pyc_path(name):
    base = 'scripts/client/' + name.replace('.', '/')
    if base + '.pyc' in PACKAGE.namelist():
        return base + '.pyc', False
    return base + '/__init__.pyc', True


PACKAGED = set()
for _name in PACKAGE.namelist():
    if _name.startswith('scripts/client/') and _name.endswith('.pyc'):
        _parts = _name[len('scripts/client/'):-4].split('/')
        if _parts[-1] == '__init__':
            _parts.pop()
        for _i in range(1, len(_parts) + 1):
            PACKAGED.add('.'.join(_parts[:_i]))


class Finder(object):
    def find_module(self, fullname, path=None):
        root = fullname.split('.')[0]
        if fullname in REAL or fullname in STUBS:
            return self
        if root not in AUTO_ROOTS:
            return None
        # Python 2 tries implicit relative imports first; only names that the
        # original package really contains are stand-ins.
        if '.' in fullname and fullname not in PACKAGED:
            return None
        return self

    def load_module(self, fullname):
        if fullname in sys.modules:
            return sys.modules[fullname]
        if fullname in STUBS:
            module = STUBS[fullname]
            sys.modules[fullname] = module
            return module
        if fullname in REAL:
            path, package = pyc_path(fullname)
            module = imp.new_module(fullname)
            module.__file__ = path
            if package:
                module.__path__ = []
            sys.modules[fullname] = module
            exec marshal.loads(PACKAGE.read(path)[8:]) in module.__dict__
            return module
        module = AutoModule(fullname)
        module.__path__ = []
        sys.modules[fullname] = module
        return module


def stub(name, **values):
    module = AutoModule(name)
    module.__path__ = []
    module.__dict__.update(values)
    STUBS[name] = module
    return module


# --- the GUI and Math the bindings and PYC share --------------------------------------------
NAMES = host.initScripts()
GUI = sys.modules['GUI']
GUI.screenResolution = host.screenResolution
GUI.addRoot = host.addRoot
GUI.delRoot = host.delRoot


class Vector3(tuple):
    def __new__(cls, x=0.0, y=0.0, z=0.0):
        return tuple.__new__(cls, (float(x), float(y), float(z)))

    def __add__(self, other):
        return Vector3(*[a + b for a, b in zip(self, other)])


class Vector2(tuple):
    def __new__(cls, x=0.0, y=0.0):
        return tuple.__new__(cls, (float(x), float(y)))


class Matrix(object):
    """Math.Matrix: row-vector, translation in row 3 (host-sampled when built from a provider)."""
    def __init__(self, provider=None):
        if provider is None:
            self._values = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
        else:
            self._values = list(host.sample(provider))

    def setTranslate(self, v):
        self._values = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, v[0], v[1], v[2], 1.0]

    @property
    def translation(self):
        return Vector3(*self._values[12:15])

    def applyVector(self, v):
        m = self._values
        return Vector3(*[v[0] * m[c] + v[1] * m[4 + c] + v[2] * m[8 + c] for c in range(3)])


stub('Math', MatrixAnimation=host.animation, Matrix=Matrix, Vector3=Vector3, Vector2=Vector2)


class Event(object):
    def __init__(self):
        self.handlers = []

    def __iadd__(self, handler):
        self.handlers.append(handler)
        return self

    def __isub__(self, handler):
        self.handlers.remove(handler)
        return self


class InterfaceScale(object):
    def __init__(self):
        self.onScaleChanged = Event()

    def get(self):
        return 1.5


class SettingsCore(object):
    interfaceScale = InterfaceScale()
    shared = type('Shared', (object,), {'crosshair': None})

    def getSetting(self, name):
        return 0


class Descriptor(object):
    def __get__(self, instance, owner):
        return SETTINGS


SETTINGS = SettingsCore()
stub('helpers.dependency', instance=lambda interface: SETTINGS, descriptor=lambda interface: Descriptor())
stub('helpers', dependency=STUBS['helpers.dependency'])
BINDINGS = {}


def bindRW(key):
    class Binding(object):
        def __get__(self, instance, owner):
            return BINDINGS.get(key)

        def __set__(self, instance, value):
            BINDINGS[key] = value
    return Binding()


stub('AvatarInputHandler.aih_global_binding', bindRW=bindRW)


class Replay(object):
    isPlaying = isRecording = isClientReady = isServerAim = False


stub('BattleReplay', g_replayCtrl=Replay())
stub('constants', SERVER_TICK_LENGTH=0.1, HAS_DEV_RESOURCES=False)
GUN = {'matrix': None}


def getPlayerGunMat(turretYaw, gunPitch):
    return GUN['matrix']


stub('AvatarInputHandler.AimingSystems', getPlayerGunMat=getPlayerGunMat)
stub('AvatarInputHandler', aih_global_binding=STUBS['AvatarInputHandler.aih_global_binding'],
     AimingSystems=STUBS['AvatarInputHandler.AimingSystems'])
stub('debug_utils', LOG_DEBUG=lambda *a: None, LOG_UNEXPECTED=lambda *a: None, LOG_ERROR=lambda *a: None,
     LOG_DEBUG_DEV=lambda *a: None, LOG_CODEPOINT_WARNING=lambda *a: None, LOG_GUI=lambda *a: None)


class CrosshairViewID(object):
    UNDEFINED, ARCADE, SNIPER, STRATEGIC, POSTMORTEM = 0, 1, 2, 3, 4


stub('gui.battle_control.battle_constants', CROSSHAIR_VIEW_ID=CrosshairViewID, HIT_INDICATOR_MAX_ON_SCREEN=5)


class Player(object):
    class gunRotator(object):
        turretYaw, gunPitch, dispersionAngle = 0.0, -0.3, 0.004

    class _Shot(object):
        speed, gravity = 300.0, 50.0

    def getVehicleDescriptor(self):
        return type('Descriptor', (object,), {'shot': self._Shot})

    def getOwnVehicleMatrix(self):
        return OWN_HULL['matrix']


OWN_HULL = {'matrix': None}
PLAYER = Player()
stub('BigWorld', player=lambda: PLAYER)
stub('gui.Scaleform', SCALEFORM_SWF_PATH='gui/flash', SCALEFORM_SWF_PATH_V3='gui/scaleform')
sys.meta_path.insert(0, Finder())

# --- shared helpers ------------------------------------------------------------------------------


def identity():
    return [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def yaw_matrix(yaw, translation=(0.0, 0.0, 0.0)):
    c, s = math.cos(yaw), math.sin(yaw)
    return [c, 0, -s, 0, 0, 1.0, 0, 0, s, 0, c, 0, translation[0], translation[1], translation[2], 1.0]


def multiply(a, b):
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


def camera(eye, yaw, pitch=0.0, fov=1.0, aspect=16.0 / 9.0, near=0.5, far=1500.0):
    """A row-vector camera: invView rows are the camera axes, view = inverse."""
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    right = (cy, 0.0, -sy)
    forward = (sy * cp, -sp, cy * cp)
    up = (forward[1] * right[2] - forward[2] * right[1], forward[2] * right[0] - forward[0] * right[2],
          forward[0] * right[1] - forward[1] * right[0])
    inv_view = list(right) + [0] + list(up) + [0] + list(forward) + [0] + list(eye) + [1.0]
    view = [right[0], up[0], forward[0], 0, right[1], up[1], forward[1], 0, right[2], up[2], forward[2], 0,
            -sum(right[i] * eye[i] for i in range(3)), -sum(up[i] * eye[i] for i in range(3)),
            -sum(forward[i] * eye[i] for i in range(3)), 1.0]
    h = 1.0 / math.tan(fov / 2)
    proj = [h / aspect, 0, 0, 0, 0, h, 0, 0, 0, 0, far / (far - near), 1, 0, 0, -near * far / (far - near), 0]
    vp = multiply(view, proj)
    host.setCamera(view, inv_view, vp, 960.0, 540.0, near, far)
    return [f32(v) for v in vp], [f32(v) for v in view], [f32(v) for v in inv_view]


def clip(position, vp):
    x, y, z = [f32(v) for v in position]
    return [f32(x * vp[c] + y * vp[4 + c] + z * vp[8 + c] + vp[12 + c]) for c in range(4)]


def expect_raises(kind, fragment, function, *args):
    try:
        function(*args)
    except kind as error:
        check(fragment in str(error), (fragment, str(error)))
        return str(error)
    raise AssertionError('%s was not raised by %r' % (kind.__name__, function))


report = []

# --- 1. factories, provider construction and errors ---------------------------------------------
check(sorted(NAMES) == sorted(['GUI.WGAbstractGunMarkerDataProvider', 'GUI.WGGunMarkerDataProvider',
                               'GUI.WGSPGGunMarkerDataProvider', 'GUI.WGCrosshairFlash',
                               'GUI.WGSPGCrosshairFlash', 'GUI.WGHitIndicatorFlash',
                               'GUI.WGTankIndicatorFlash']), NAMES)
gmc = __import__('AvatarInputHandler.gun_marker_ctrl', fromlist=['x'])
aih = sys.modules['AvatarInputHandler.aih_constants']
expect_raises(NotImplementedError, "Can't instantiate abstract class", GUI.WGAbstractGunMarkerDataProvider)
expect_raises(TypeError, 'Number expected', GUI.WGSPGGunMarkerDataProvider, 'x')
expect_raises(TypeError, 'must be greater than zero', GUI.WGSPGGunMarkerDataProvider, 0)
expect_raises(TypeError, 'must be greater than zero', GUI.WGSPGGunMarkerDataProvider, -3)

# AvatarInputHandler constructs StrategicControlMode for every vehicle: both
# marker kinds are created before any SPG is selected.
default_marker = gmc.createGunMarker(False)
strategic_marker = gmc.createGunMarker(True)
factory = gmc._GunMarkersDPFactory()
client = factory.getClientProvider()
server = factory.getServerProvider()
client_spg = factory.getClientSPGProvider()
server_spg = factory.getServerSPGProvider()
check(type(client).__name__ == 'WGGunMarkerDataProvider' and client is not server, client)
check(type(client_spg).__name__ == 'WGSPGGunMarkerDataProvider' and client_spg is not server_spg, client_spg)
check(type(client.positionMatrixProvider).__name__ == 'MatrixAnimation', client.positionMatrixProvider)
check(client.sizeConstraint == (f32(aih.GUN_MARKER_MIN_SIZE * 1.5), 1080.0), client.sizeConstraint)
check(client_spg.sizeConstraint == (aih.SPG_GUN_MARKER_MIN_SIZE, aih.SPG_GUN_MARKER_MAX_SIZE), client_spg.sizeConstraint)
check((client_spg.maxTime, f32(client_spg.serverTickLength), client_spg.sizeScaleRate) == (5.0, f32(0.1), 10.0),
      (client_spg.maxTime, client_spg.serverTickLength))
# relaxTime reads the stored inverse time (8e8e20 returns +0x24) and is read-only.
check(abs(client_spg.relaxTime - 10.0) < 1e-5 and client_spg.enableSmoothFiltering is False, client_spg.relaxTime)
expect_raises(TypeError, 'Sorry, the attribute relaxTime in WGSPGGunMarkerDataProvider is read-only',
              setattr, client_spg, 'relaxTime', 1.0)
expect_raises(TypeError, 'updateSize', client.updateSize, 1.0)
expect_raises(TypeError, 'update', client_spg.update, (0, 0, 0))
expect_raises(TypeError, 'sizeConstraint must be set to a Vector2', setattr, client, 'sizeConstraint', 3)
expect_raises(TypeError, 'positionMatrixProvider must be set to a MatrixProvider', setattr, client,
              'positionMatrixProvider', 3)
client_spg.enableSmoothFiltering = True
client_spg.enableSmoothFiltering = False
# The SPG provider's setStartSize/updateSize slots are empty (60b6b0, 6fe3a0).
client_spg.setStartSize(9.0)
client_spg.updateSize(9.0, 0.5)
# The interface-scale hook re-applies the original size limits.
ctrl = gmc._DefaultGunMarkerController(aih.GUN_MARKER_TYPE.CLIENT, client)
ctrl.create()
SETTINGS.interfaceScale.onScaleChanged.handlers[-1](2.0)
check(client.sizeConstraint == (f32(aih.GUN_MARKER_MIN_SIZE * 2.0), 1080.0), client.sizeConstraint)
SETTINGS.interfaceScale.onScaleChanged.handlers[-1](1.5)
report.append('providers: default and SPG factories, both control-mode markers, 12 error forms')

# --- 2. arcade/sniper crosshair through gm_components ---------------------------------------------
settings = __import__('gui.Scaleform.daapi.view.battle.shared.crosshair.settings', fromlist=['x'])
gmcomp = __import__('gui.Scaleform.daapi.view.battle.shared.crosshair.gm_components', fromlist=['x'])


class Container(object):
    def __init__(self, movie):
        self.movie = movie
        self.children = {}

    def addChild(self, view, name):
        check(name not in self.children, name)
        self.children[name] = view

    def delChild(self, view):
        for name, child in self.children.items():
            if child is view:
                del self.children[name]
                return
        raise AssertionError('unknown child')


battle = host.createMovie()
paths = {}
for name in ('arcadeGunMarker', 'sniperGunMarker'):
    path = settings.CROSSHAIR_ITEM_PATH_FORMAT.format(name)
    paths[name] = path
    host.addClip(battle, path + '.' + settings.CROSSHAIR_RADIUS_MC_NAME + '.invalidateCrosshair', 'function')
    host.addClip(battle, path + '.' + settings.CROSSHAIR_RADIUS_MC_NAME + '.currMixingMC', 'display')
    host.addClip(battle, path + '.radiusMC.currMixingMC.width', 300.0)
    host.addClip(battle, path + '.radiusMC.currMixingMC.height', 256.0)
container = Container(battle)
arcade = gmcomp.DefaultGunMarkerComponent(aih.GUN_MARKER_TYPE.CLIENT, CrosshairViewID.ARCADE,
                                          'arcadeGunMarker', 'linkage', client, isActive=True)
sniper = gmcomp.DefaultGunMarkerComponent(aih.GUN_MARKER_TYPE.CLIENT, CrosshairViewID.SNIPER,
                                          'sniperGunMarker', 'linkage', client)
components = gmcomp.GunMarkersComponents([arcade, sniper])
check(components.addView(container, 'arcadeGunMarker') and components.addView(container, 'sniperGunMarker'), 'views')
arcade_view = container.children['arcadeGunMarker']
sniper_view = container.children['sniperGunMarker']
check(type(arcade_view).__name__ == 'WGCrosshairGUIComponent', arcade_view)
check(arcade_view.wg_inputKeyMode == 2 and arcade_view.heightMode == 'PIXEL', (arcade_view.wg_inputKeyMode, arcade_view.heightMode))
check(host.provider(client)['acquired'] and host.info(battle, paths['arcadeGunMarker'])['visible'], 'arcade attached')
check(not host.info(battle, paths['sniperGunMarker'])['visible'], 'sniper hidden')
check(host.member(battle, paths['arcadeGunMarker'] + '.radiusMC.invalidateCrosshair') == 'function', 'slot')
state = host.component(arcade_view)
check(not state['global'] and state['movieVisible'], state)

# One frame: the live MatrixAnimation position and the blended size.
vp, view, inv_view = camera((0.0, 3.0, -10.0), 0.2, 0.05)
target = Matrix()
target.setTranslate((12.0, 1.0, 140.0))
ctrl._updateMatrixProvider(target, 0.0)
client.updateSize(40.0, 0.25)
host.update(arcade_view, 0.05)
expected = clip((12.0, 1.0, 140.0), vp)
x = f32(f32(expected[0] * f32(1.0 / expected[3])) + 1.0) * 960.0
y = f32(1.0 - f32(expected[1] * f32(1.0 / expected[3]))) * 540.0
info = host.info(battle, paths['arcadeGunMarker'])
check(abs(info['x'] - f32(x)) < 1e-3 and abs(info['y'] - f32(y)) < 1e-3 and info['visible'], (info, x, y))
# setStartSize(48) -> blend 0.2 toward 40, clamp [48, 1080], percent of the 512 default clip.
scale = host.info(battle, paths['arcadeGunMarker'] + '.radiusMC')['xscale']
start = f32(aih.GUN_MARKER_MIN_SIZE * 1.5)
size = f32(f32(f32(1.0 - 0.2) * start) + f32(40.0 * 0.2))
check(abs(scale - max(f32(size * 1.72333), start) / 512.0 * 100.0) < 1e-3, (scale, size))
# invalidateCrosshair: the radius clip is now min(width, height) = 256.
host.callFunction(battle, paths['arcadeGunMarker'] + '.radiusMC.invalidateCrosshair')
host.update(arcade_view, 0.05)
scale_after = host.info(battle, paths['arcadeGunMarker'] + '.radiusMC')['xscale']
check(scale_after > scale * 1.9, (scale, scale_after))
# An inactive mixing clip is ignored.
host.setActive(battle, paths['arcadeGunMarker'] + '.radiusMC.currMixingMC', 0)
host.addClip(battle, paths['arcadeGunMarker'] + '.radiusMC.currMixingMC.height', 64.0)
host.callFunction(battle, paths['arcadeGunMarker'] + '.radiusMC.invalidateCrosshair')
# Behind the camera the marker hides; the shared movie is neither advanced nor drawn.
behind = Matrix()
behind.setTranslate((0.0, 3.0, -60.0))
ctrl._updateMatrixProvider(behind, 0.0)
host.update(arcade_view, 0.05)
check(not host.info(battle, paths['arcadeGunMarker'])['visible'], 'behind camera')
host.draw(arcade_view)
state = host.component(arcade_view)
check(state['advances'] == 0 and state['draws'] == 0 and state['updates'] == 3, state)
# Switching views deactivates before activating, so one provider moves between views.
components.switch(CrosshairViewID.SNIPER)
check(host.info(battle, paths['sniperGunMarker'])['visible'] and
      not host.info(battle, paths['arcadeGunMarker'])['visible'] and host.provider(client)['acquired'], 'switch')
components.switch(CrosshairViewID.ARCADE)
check(host.info(battle, paths['arcadeGunMarker'])['visible'], 'switch back')
# Rebinding to the server provider (setDataProvider on an active component).
arcade.setDataProvider(aih.GUN_MARKER_TYPE.SERVER, server)
check(host.provider(server)['acquired'], 'server provider')
expect_raises(TypeError, 'Expected a PyMovieView object and two String Objects', GUI.WGCrosshairFlash, battle, 'x')
expect_raises(TypeError, 'Expected a PyMovieView object', GUI.WGCrosshairFlash, 3, 'x', 'y')
expect_raises(TypeError, "gunMarkerPath='missing'and radiusName='radiusMC'", GUI.WGCrosshairFlash, battle,
              'missing', 'radiusMC')
expect_raises(TypeError, 'clearDataProvider', arcade_view.clearDataProvider, 1)
deallocations = host.state()['deallocations']
components.removeView(container, 'arcadeGunMarker')
components.removeView(container, 'sniperGunMarker')
# 13df670 replaces the holder without releasing the old provider; it stays acquired.
check(not host.provider(server)['acquired'] and host.provider(client)['acquired'], 'released')
components.clear()
del arcade_view, sniper_view
check(host.state()['deallocations'] >= deallocations + 2, host.state())
# The AS3 slot outlives the view; the detached handler ignores late calls.
host.callFunction(battle, paths['sniperGunMarker'] + '.radiusMC.invalidateCrosshair')
report.append('crosshair: projection, size blend, invalidateCrosshair, hide, switch, rebind, teardown')

# --- 3. strategic crosshair and the SPG provider -----------------------------------------------------
spg_path = settings.CROSSHAIR_ITEM_PATH_FORMAT.format('spgGunMarker')
count = settings.SPG_GUN_MARKER_ELEMENTS_COUNT
for i in range(1, count + 1):
    host.addClip(battle, '%s.elem_%d' % (spg_path, i), 'display')
spg = gmcomp.SPGGunMarkerComponent(aih.GUN_MARKER_TYPE.CLIENT, CrosshairViewID.STRATEGIC, 'spgGunMarker',
                                   'linkage', client_spg, isActive=True)
spg.addView(container)
spg_view = container.children['spgGunMarker']
check(type(spg_view).__name__ == 'WGSPGCrosshairGUIComponent', spg_view)
check(host.info(battle, spg_path)['visible'] and host.provider(client_spg)['acquired'], 'spg attached')
check(host.info(battle, spg_path + '.elem_1')['vars'] & 3 == 3, 'elements placed at the origin')
spg.setScale(1.25)
vp, view, inv_view = camera((0.0, 150.0, -120.0), 0.0, 0.9)
GUN['matrix'] = Matrix()
GUN['matrix']._values = [1.0, 0, 0, 0, 0, math.cos(-0.3), math.sin(-0.3), 0, 0, -math.sin(-0.3),
                         math.cos(-0.3), 0, 0.0, 3.0, 0.0, 1.0]
host.setGround(0.0)
strategic = gmc._SPGGunMarkerController(aih.GUN_MARKER_TYPE.CLIENT, client_spg)
strategic.enable()
queries = host.state()['segmentQueries']
strategic.update(aih.GUN_MARKER_TYPE.CLIENT, Vector3(0, 0, 160), Vector3(0, 0, 1), (60.0, 60.0), 0.1, None)
provider = host.provider(client_spg)
check(host.state()['segmentQueries'] > queries, 'scene queries')
check(all(p[1] == 0.0 or abs(p[1]) < 1e-3 for p in provider['current']), provider['current'][:3])
check(abs(provider['angle'] - 0.004) < 1e-7 and provider['sizes'][0] == 60.0, provider)
host.update(spg_view, 0.05)
element = host.info(battle, spg_path + '.elem_1')
check(element['matrixWrites'] == 1 and element['vars'] & 0x3000 == 0x3000, element)
check(abs(element['matrix3D'][0] - f32(1.25 * 100.0 * 0.01)) < 1e-5, element['matrix3D'])
check(host.component(spg_view)['advances'] == 0, 'shared movie not advanced')
# A ground below the cone's reach leaves the missing-point projection.
host.setGround(-10000.0)
strategic.reset()
host.update(spg_view, 0.05)
check(all(p == (-100000.0, -100000.0, -100000.0) for p in host.provider(client_spg)['current']), 'misses')
missing = host.info(battle, spg_path + '.elem_2')['projection']
check(abs(missing[15] + 100000.0) < 1.0, missing)
expect_raises(TypeError, "'crosshairMC' movie clip", GUI.WGSPGCrosshairFlash, battle, 'missing', 5)
expect_raises(TypeError, 'String and Number expected', GUI.WGSPGCrosshairFlash, battle, 5)
try:
    GUI.WGSPGCrosshairFlash(battle, spg_path, 0)
except SystemError:
    pass
else:
    raise AssertionError('13e7ab0 returns NULL without an exception for a non-positive count')
spg.removeView(container)
check(not host.provider(client_spg)['acquired'] and not host.info(battle, spg_path)['visible'], 'spg released')
strategic.disable()
report.append('SPG: StrategicControlMode providers, cone traced to the scene, 3D element placement, reset/miss')

# --- 4. tank indicator through damage_panel._TankIndicatorCtrl ------------------------------------------
damage_panel = __import__('gui.Scaleform.daapi.view.battle.shared.damage_panel', fromlist=['x'])
containers = sys.modules['gui.Scaleform.genConsts.APP_CONTAINERS_NAMES'].APP_CONTAINERS_NAMES
tank_path = '_level0.root.{}.main.damagePanel.tankIndicator'.format(containers.VIEWS)
host.addClip(battle, tank_path + '.hull', 'display')
host.addClip(battle, tank_path + '.turret', 'display')


class App(object):
    def __init__(self, movie):
        self.movie = movie
        self.component = Container(movie)


app = App(battle)
tank = damage_panel._TankIndicatorCtrl(app)
tank_view = app.component.children['tankIndicator']
check(type(tank_view).__name__ == 'WGTankIndicatorGUIComponent' and tank_view.wg_inputKeyMode == 2, tank_view)
hull = host.matrix(yaw_matrix(0.4, (10, 0, 20)))
turret = host.matrix(yaw_matrix(0.3))
OWN_HULL['matrix'] = hull
vehicle = type('Vehicle', (object,), {'isPlayerVehicle': True, 'matrix': None,
                                       'appearance': type('Appearance', (object,), {'turretMatrix': turret})})
tank.setup(vehicle, (-0.5, 0.5))
check(tank_view.wg_turretYawConstraints == (-0.5, 0.5) and tank_view.wg_hullMatProv is hull, 'tank setup')
vp, view, inv_view = camera((0.0, 3.0, -10.0), 0.1)


def tank_expected(hull_yaw, turret_yaw, camera_yaw):
    hull_deg = f32(f32(hull_yaw) * 57.2957763671875)
    cam_deg = f32(f32(camera_yaw) * -57.2957763671875)
    return f32(hull_deg + cam_deg), f32(f32(f32(f32(turret_yaw) * 57.2957763671875) + hull_deg) + cam_deg)


host.update(tank_view, 0.05)
hull_rot, turret_rot = tank_expected(0.4, 0.3, 0.1)
check(abs(host.info(battle, tank_path + '.hull')['rotation'] - hull_rot) < 1e-3, host.info(battle, tank_path + '.hull'))
check(abs(host.info(battle, tank_path + '.turret')['rotation'] - turret_rot) < 1e-3, 'turret')
check(host.info(battle, tank_path)['invokes'] == [], 'inside limits')
# Live matrices: the indicator follows the hull, not the spawn pose.
host.setMatrix(hull, yaw_matrix(-1.0, (50, 0, 50)))
host.setMatrix(turret, yaw_matrix(0.7))
host.update(tank_view, 0.05)
hull_rot, turret_rot = tank_expected(-1.0, 0.7, 0.1)
check(abs(host.info(battle, tank_path + '.hull')['rotation'] - hull_rot) < 1e-3, 'live hull')
check(host.info(battle, tank_path)['invokes'] == [('onArrivedConstraint', [True])], host.info(battle, tank_path))
host.update(tank_view, 0.05)
host.setMatrix(turret, yaw_matrix(0.497))
host.update(tank_view, 0.05)
host.setMatrix(turret, yaw_matrix(0.2))
host.update(tank_view, 0.05)
check(host.info(battle, tank_path)['invokes'] == [('onArrivedConstraint', [True]), ('onArrivedConstraint', [False])],
      host.info(battle, tank_path)['invokes'])
# An observed vehicle uses its own matrix; no yaw limits means an unconstrained turret.
observed = host.matrix(yaw_matrix(2.0))
vehicle.isPlayerVehicle = False
vehicle.matrix = observed
host.setMatrix(turret, yaw_matrix(3.0))
tank.setup(vehicle, None)
check(tank_view.wg_turretYawConstraints == (0.0, 0.0) and tank_view.wg_hullMatProv is observed, 'observed')
host.update(tank_view, 0.05)
check(len(host.info(battle, tank_path)['invokes']) == 2 and host.samples(observed) == 1, 'unconstrained')
check(host.component(tank_view)['advances'] == 0, 'tank indicator does not advance the movie')
expect_raises(TypeError, "has '_level0.missing' movie clip", GUI.WGTankIndicatorFlash, battle, '_level0.missing')
tank.clear()
check('tankIndicator' not in app.component.children, 'tank cleared')
report.append('tank: own/observed hull, live turret, constrained/unconstrained yaw edges, teardown')

# --- 5. hit indicator through indicators._DamageIndicator and Flash ---------------------------------------
HIT_ROOT = {}


class Root(object):
    def __init__(self):
        self.calls = []

    def __getattr__(self, name):
        if name.startswith('as_'):
            return lambda *args: self.calls.append((name, args))
        raise AttributeError(name)


class MovieDef(object):
    def __init__(self, path):
        self.path = path

    def createInstance(self):
        movie = host.createMovie()
        for i in range(5):
            base = '_root.dmgIndicator.hit_%d' % i
            for suffix in ('', '.extendedAnimation', '.extendedAnimation.stateContainer.damage',
                           '.extendedAnimation.stateContainer.tankName',
                           '.extendedAnimation.stateContainer.tankIcon'):
                host.addClip(movie, base + suffix, 'display')
        movie.root = type('MovieRoot', (object,), {})()
        movie.root.dmgIndicator = Root()
        movie.setFSCommandCallback = lambda callback: None
        movie.setExternalInterfaceCallback = lambda callback: None
        HIT_ROOT['movie'] = movie
        return movie


stub('_Scaleform', MovieDef=MovieDef)
indicators = __import__('gui.Scaleform.daapi.view.battle.shared.indicators', fromlist=['x'])
roots = host.rootCount()
damage = indicators.createDamageIndicator()
movie = HIT_ROOT['movie']
hit_view = damage.component
check(type(hit_view).__name__ == 'WGHitIndicatorGUIComponent' and hit_view.wg_inputKeyMode == 2, hit_view)
check(host.rootCount() == roots + 1 and movie.root.dmgIndicator.calls, 'damage indicator activated')
vp, view, inv_view = camera((0.0, 3.0, -10.0), 0.6)
host.setInfo(movie, '_root.dmgIndicator.hit_0.extendedAnimation', 30.0, 1)
host.setInfo(movie, '_root.dmgIndicator.hit_0.extendedAnimation.stateContainer.tankName', 0.0, 1)
host.setInfo(movie, '_root.dmgIndicator.hit_0.extendedAnimation.stateContainer.tankIcon', 0.0, 1)
host.update(hit_view, 0.05)
camera_yaw = f32(math.atan2(view[8], view[10]))
rotation = f32(f32(f32(0.0 + camera_yaw) * 57.2957763671875) + 180.0)
hit = host.info(movie, '_root.dmgIndicator.hit_0')
check(abs(hit['rotation'] - rotation) < 1e-3, (hit['rotation'], rotation))
total = f32(30.0 + rotation)
damage_info = host.info(movie, '_root.dmgIndicator.hit_0.extendedAnimation.stateContainer.damage')
check(abs(damage_info['rotation'] + total) < 1e-3, damage_info)
angle = math.fmod(total, 360.0)
angle = angle + 360.0 if angle < 0 else angle
label = -90.0 if 10.0 <= angle < 170.0 else (90.0 if 190.0 <= angle < 350.0 else 0.0)
check(host.info(movie, '_root.dmgIndicator.hit_0.extendedAnimation.stateContainer.tankName')['rotation'] == label,
      (angle, host.info(movie, '_root.dmgIndicator.hit_0.extendedAnimation.stateContainer.tankName')))
check(host.info(movie, '_root.dmgIndicator.hit_0.extendedAnimation.stateContainer.tankIcon')['rotation'] == label,
      'icon')
# Hidden extended animation leaves damage and labels alone.
writes = host.info(movie, '_root.dmgIndicator.hit_1.extendedAnimation.stateContainer.damage')['writes']
host.setInfo(movie, '_root.dmgIndicator.hit_1.extendedAnimation', 0.0, 0)
host.update(hit_view, 0.05)
check(host.info(movie, '_root.dmgIndicator.hit_1.extendedAnimation.stateContainer.damage')['writes'] == writes, 'hidden')
check(host.component(hit_view)['advances'] == 2, 'the indicator movie advances with its component')
expect_raises(TypeError, 'Tuple of Strings', GUI.WGHitIndicatorFlash, movie)
expect_raises(TypeError, 'Expected a Tuple object as second argument', GUI.WGHitIndicatorFlash, movie, ['x'])
expect_raises(TypeError, 'String object as item of tuple', GUI.WGHitIndicatorFlash, movie, (1,))
expect_raises(TypeError, "has a '_root.missing' movie clip", GUI.WGHitIndicatorFlash, movie, ('_root.missing',))
expect_raises(TypeError, "has a '_root.dmgIndicator.hit_0' movie clip", GUI.WGHitIndicatorFlash, movie,
              ('_root.dmgIndicator.hit_0', '_root.dmgIndicator.hit_0'))
hit_view.offsetRotationElementsInDegree(-1.0, 10.0)
damage.destroy()
check(host.rootCount() == roots, 'damage indicator closed')
report.append('hit: original Flash/_DamageIndicator, camera-relative rotation, readable labels, close')

print('Original PYC over the production bindings: ' + '; '.join(report))
