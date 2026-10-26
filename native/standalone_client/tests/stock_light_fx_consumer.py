"""Drive BigWorld.WGLightFx and, when provided, the exact #1513 LightManager."""
import marshal
import os
import sys
import types

import BigWorld
import Host


def fresh(present, missing=(), results=None, count=0, description=''):
    Host.configure(int(present), list(missing), dict(results or {}), count, description)
    Host.calls()


def raises(call, message):
    try:
        call()
    except TypeError as error:
        assert str(error) == message, str(error)
    else:
        raise AssertionError(message)


LOADED = [('LoadLibraryW', (), 'LightFX.dll')]

# An absent LightFX.dll resolves nothing and every operation reports failure.
fresh(False)
fx = BigWorld.WGLightFx()
assert Host.calls() == LOADED
assert fx.initialize() is False
assert fx.reset() == 1 and fx.update() == 1
assert fx.getNumberOfDevices() == 0 and fx.getNumberOfLights(0) == 0
assert fx.getLightDescription(0, 0) == ''
assert fx.setLightColor(0, 0, (1, 2, 3, 4)) == 1 and fx.setLightPulse(0, 0, (1, 2, 3, 4)) == 1
assert fx.actionColorEx(1, 3, (1, 2, 3, 4), (0, 0, 0, 0)) == 1
del fx
assert Host.calls() == []

# A present library still reports failure until LFX_Initialize succeeds.
fresh(True, results={'LFX_Initialize': 3}, count=2, description='Keyboard')
fx = BigWorld.WGLightFx(7)
assert Host.calls() == LOADED
assert fx.reset() == 1 and fx.getNumberOfDevices() == 0 and fx.getLightDescription(0, 0) == ''
assert fx.setLightColor(0, 0, (1, 2, 3, 4)) == 1 and Host.calls() == []
assert fx.initialize() is False and Host.calls() == [('LFX_Initialize', ())]
assert fx.update() == 1 and Host.calls() == []
del fx
assert Host.calls() == [('FreeLibrary', (0x1000,))]

fresh(True, results={'LFX_Initialize': 0, 'LFX_Reset': 0, 'LFX_Update': 5,
                     'LFX_SetLightActionColor': 0, 'LFX_ActionColorEx': 6},
      count=2, description='Keyboard')
fx = BigWorld.WGLightFx()
Host.calls()
assert fx.initialize() is True
assert fx.reset() == 0 and fx.update() == 5
devices = fx.getNumberOfDevices()
assert devices == 2 and type(devices) is int
assert fx.getNumberOfLights(1) == 2
assert fx.getLightDescription(1, 2) == 'Keyboard'
assert Host.calls() == [('LFX_Initialize', ()), ('LFX_Reset', ()), ('LFX_Update', ()),
                        ('LFX_GetNumDevices', ()), ('LFX_GetNumLights', (1,)),
                        ('LFX_GetLightDescription', (1, 2, 255))]
# Channels clamp to [0, 255], truncate, and NaN takes the upper bound.
assert fx.setLightColor(1, 2, (255.9, -1.0, 300.0, 128.7)) == 0
assert fx.setLightPulse(3, 4, (float('nan'), 0.5, 1.5, 254.99)) == 0
assert fx.actionColorEx(4294967295, 3, (10, 20, 30, 40), (1, 2, 3, 4)) == 6
assert Host.calls() == [
    ('LFX_SetLightActionColor', (1, 2, 3, 255 | 255 << 16 | 128 << 24)),
    ('LFX_SetLightActionColor', (3, 4, 2, 255 | 1 << 16 | 254 << 24)),
    ('LFX_ActionColorEx', (0xffffffff, 3, 40 << 24 | 10 << 16 | 20 << 8 | 30,
                           4 << 24 | 1 << 16 | 2 << 8 | 3))]
# A failed SDK query reports no devices, no lights and no description.
Host.configure(1, [], {'LFX_GetNumDevices': 1, 'LFX_GetNumLights': 1,
                       'LFX_GetLightDescription': 1}, 9, 'stale')
assert fx.getNumberOfDevices() == 0 and fx.getNumberOfLights(0) == 0
assert fx.getLightDescription(0, 0) == ''
Host.calls()

raises(lambda: fx.initialize(1), 'initialize() takes no arguments.')
raises(lambda: fx.getNumberOfLights(), 'getNumberOfLights() expects 1 argument of type UINT')
raises(lambda: fx.getNumberOfLights(-1), '() argument 1 must be set to an uint')
raises(lambda: fx.getLightDescription(1), 'getLightDescription() expects 2 arguments of types UINT, UINT')
raises(lambda: fx.setLightColor(0, 0, (1, 2, 3)),
       '() argument 3 must be set to a Vector4 or tuple of 4 floats')
raises(lambda: fx.setLightPulse(0, 0), 'setLightPulse() expects 3 arguments of types UINT, UINT, Vector4')
raises(lambda: fx.actionColorEx(0, 0, (0, 0, 0, 0)),
       'actionColorEx() expects 4 arguments of types UINT, UINT, Vector4, Vector4')
assert Host.calls() == []
assert sorted(fx.__methods__) == sorted(['initialize', 'reset', 'update', 'getNumberOfDevices',
                                          'getNumberOfLights', 'getLightDescription', 'setLightColor',
                                          'setLightPulse', 'actionColorEx'])
del fx
assert Host.calls() == [('LFX_Release', ()), ('FreeLibrary', (0x1000,))]

# Unresolved exports are reported as unavailable, independently per export.
fresh(True, missing=['LFX_Initialize'])
fx = BigWorld.WGLightFx()
assert fx.initialize() is False
del fx
assert Host.calls() == LOADED + [('FreeLibrary', (0x1000,))]
fresh(True, missing=['LFX_Release', 'LFX_Update'], results={'LFX_Initialize': 0})
fx = BigWorld.WGLightFx()
assert fx.initialize() is True and fx.update() == 1
del fx
assert Host.calls() == LOADED + [('LFX_Initialize', ()), ('FreeLibrary', (0x1000,))]

pyc = os.environ.get('LIGHT_MANAGER_PYC')
if pyc:
    class Anything(object):
        """Event and service stand-in for imports outside the LightFx owner."""
        def __getattr__(self, name):
            return Anything()

        def __call__(self, *args, **kwargs):
            return Anything()

        def __iadd__(self, other):
            return self

        def __isub__(self, other):
            return self

    class Vector4(tuple):
        def __new__(cls, *values):
            return tuple.__new__(cls, values or (0.0, 0.0, 0.0, 0.0))

    logged = []
    callbacks = []
    stubs = {
        'LightEffect': {'ACTION_COLOR': 3, 'ACTION_PULSE': 2},
        'LightEffectsCache': {'LightEffectsCache': type('Cache', (), {
            'load': staticmethod(lambda: None), 'allEffects': staticmethod(lambda: {})})},
        'Math': {'Vector4': Vector4},
        'gui': {}, 'gui.SystemMessages': {},
        'debug_utils': {'LOG_DEBUG': lambda *args: logged.append(args)},
        'helpers': {'dependency': Anything()},
        'messenger': {}, 'messenger.m_constants': {'PROTO_TYPE': Anything()},
        'messenger.ext': {}, 'messenger.ext.player_helpers': {'isCurrentPlayer': Anything()},
        'messenger.proto': {}, 'messenger.proto.events': {'g_messengerEvents': Anything()},
        'skeletons': {}, 'skeletons.connection_mgr': {'IConnectionManager': object},
        'PlayerEvents': {'g_playerEvents': Anything()},
        'gui.prb_control': {}, 'gui.prb_control.dispatcher': {'g_prbLoader': Anything()},
    }
    for name, values in stubs.items():
        module = types.ModuleType(name)
        module.__dict__.update(values)
        sys.modules[name] = module
    BigWorld.callback = lambda delay, function: callbacks.append(delay) or len(callbacks)
    BigWorld.cancelCallback = lambda identifier: callbacks.append(-identifier)
    manager = types.ModuleType('LightManager')
    code = marshal.loads(open(pyc, 'rb').read()[8:])
    exec(code, manager.__dict__)

    # The VM has no LightFX runtime: the original disables the manager and continues.
    fresh(False)
    light = manager.LightManager()
    assert light.isEnabled() is False and logged == [('LightFx is not enabled',)]
    light.start()
    light.destroy()
    assert callbacks == [] and Host.calls() == LOADED

    fresh(True, results={'LFX_Initialize': 0, 'LFX_ActionColorEx': 0}, count=1, description='Logo')
    light = manager.LightManager()
    assert light.isEnabled() is True and light.getLights() == {'Logo': (0, 0)}
    calls = Host.calls()
    assert calls[:6] == LOADED + [('LFX_Initialize', ()), ('LFX_Reset', ()), ('LFX_GetNumDevices', ()),
                                  ('LFX_GetNumLights', (0,)), ('LFX_GetLightDescription', (0, 0, 255))]
    # The periodic update turns every named zone off, then flushes the device.
    colors = sorted(call[1] for call in calls if call[0] == 'LFX_ActionColorEx')
    assert colors == sorted((mask, 3, 0, 0) for mask in (1, 4, 16, 524288)), colors
    assert calls[-1] == ('LFX_Update', ()) and callbacks == [0.5]
    light.destroy()
    assert callbacks == [0.5, -1]
    del light
    assert Host.calls() == [('LFX_Release', ()), ('FreeLibrary', (0x1000,))]
    print('Exact #1513 LightManager consumed WGLightFx in both availability states')
