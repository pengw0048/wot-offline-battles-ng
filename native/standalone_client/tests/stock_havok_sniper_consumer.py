"""Real CPython 2.7 invokes the production binding and unchanged #1513 PYC.

Camera, event and component owners are fixtures. SniperControlMode's destroy,
disable, AvatarInputHandler.stop and CallbackDelayer cleanup are original code.
"""
from __future__ import print_function
import marshal
import sys
import types
import zipfile

import BigWorld


def child(code, name):
    return next(value for value in code.co_consts
                if isinstance(value, types.CodeType) and value.co_name == name)


with zipfile.ZipFile(sys.argv[1]) as package:
    def load(name):
        return marshal.loads(package.read('scripts/client/' + name + '.pyc')[8:])
    sniper_code = child(load('AvatarInputHandler/control_modes'), 'SniperControlMode')
    handler_code = child(load('AvatarInputHandler/__init__'), 'AvatarInputHandler')
    delayer_code = child(load('helpers/CallbackDelayer'), 'CallbackDelayer')
    consumers = []

    def scan(code, path):
        if 'wg_havokSetSniperMode' in code.co_names:
            consumers.append((path, code.co_name))
        for value in code.co_consts:
            if isinstance(value, types.CodeType):
                scan(value, path)

    for member in package.namelist():
        if member.endswith('.pyc'):
            data = package.read(member)
            if b'wg_havokSetSniperMode' in data:
                scan(marshal.loads(data[8:]), member)
    assert sorted(consumers) == [
        ('scripts/client/AvatarInputHandler/control_modes.pyc', 'disable'),
        ('scripts/client/AvatarInputHandler/control_modes.pyc', 'enable')]


function = BigWorld.wg_havokSetSniperMode
for value in (False, True, 0, 1, -2, 'true', 'FALSE', 'tRuE', 'false\0ignored'):
    assert function(value) is None
for value in (None, 1.0, 1L, u'true', '', 'yes', [], object()):
    try:
        function(value)
        raise AssertionError('Invalid bool accepted: %r' % (value,))
    except TypeError as error:
        assert str(error) == '() argument 1 must be set to a bool', str(error)
for args in ((), (True, False), (False, True, False)):
    try:
        function(*args)
        raise AssertionError('Invalid arity accepted')
    except TypeError as error:
        assert str(error) == 'wg_havokSetSniperMode() expects 1 argument of type bool', str(error)


class Object(object):
    def __init__(self, **values):
        self.__dict__.update(values)


events = []


def record(name):
    return lambda *args, **kwargs: events.append(name)


class Event(list):
    def clear(self):
        events.append('event.clear')
        del self[:]

    def __isub__(self, callback):
        self.remove(callback)
        events.append('event.unsubscribe')
        return self


class BaseMode(object):
    def disable(self):
        events.append('mode.disable')

    def destroy(self):
        events.append('mode.destroy')


BigWorld.wg_enableTreeHiding = record('tree.disable')
if not hasattr(BigWorld, 'wg_setLowDetailedMode'):
    BigWorld.wg_setLowDetailedMode = record('detail.disable')
BigWorld.cancelCallback = lambda number: events.append(('cancel', number))
namespace = dict(BigWorld=BigWorld, g_postProcessing=Object(disable=record('post.disable')),
                 BattleReplay=Object(g_replayCtrl=Object(isPlaying=False, isRecording=False)),
                 TriggersManager=Object(g_manager=None))
namespace['__builtins__'] = __builtins__
SniperControlMode = type('SniperControlMode', (BaseMode,), {})
namespace['SniperControlMode'] = SniperControlMode
SniperControlMode.disable = types.FunctionType(child(sniper_code, 'disable'), namespace,
                                               'disable', (False,))
SniperControlMode.destroy = types.FunctionType(child(sniper_code, 'destroy'), namespace)
mode = SniperControlMode()
mode._SniperControlMode__binoculars = Object(enabled=True, resetTextures=record('binoculars.reset'))
mode._cam = Object(writeUserPreferences=record('camera.preferences'))
mode._aih = Object(siegeModeControl=None)

CallbackDelayer = type('CallbackDelayer', (object,), dict(
    (name, types.FunctionType(child(delayer_code, name), namespace))
    for name in ('destroy', 'clearCallbacks')))
Handler = type('Handler', (CallbackDelayer,), {})
namespace.update(aih_global_binding=Object(clear=record('binding.clear')),
                 g_guiResetters=[], ComponentSystem=Object(destroy=record('component.destroy')),
                 CallbackDelayer=CallbackDelayer)
Handler.stop = types.FunctionType(child(handler_code, 'stop'), namespace)
sys.modules['SoundGroups'] = Object(g_instance=Object(changePlayMode=record('sound.reset')))


def handler():
    value = Handler()
    value._AvatarInputHandler__ctrls = {'sniper': mode}
    value._AvatarInputHandler__remoteCameraSender = Object(destroy=record('remote.destroy'))
    for name in ('onCameraChanged', 'onPostmortemVehicleChanged',
                 'onPostmortemKillerVisionEnter', 'onPostmortemKillerVisionExit'):
        setattr(value, name, Event([object()]))
    value._AvatarInputHandler__targeting = Object(enable=record('target.disable'))
    value._AvatarInputHandler__onRecreateDevice = object()
    value._AvatarInputHandler__onArenaStarted = object()
    value._AvatarInputHandler__onSettingsChanged = object()
    value._AvatarInputHandler__onVehicleChanged = object()
    value.settingsCore = Object(onSettingsChanged=Event([value._AvatarInputHandler__onSettingsChanged]))
    arena = Object(onPeriodChange=Event([value._AvatarInputHandler__onArenaStarted]))
    matrices = Object(onVehicleMatrixBindingChanged=Event([value._AvatarInputHandler__onVehicleChanged]))
    BigWorld.player = lambda: Object(arena=arena, consistentMatrices=matrices)
    value._CallbackDelayer__callbacks = {'first': 71, 'second': 72, 'already retired': None}
    return value


# Reproduce the actual missing-interface teardown interruption, including the
# callback and subscription owners that occur after the control-mode loop.
del BigWorld.wg_havokSetSniperMode
broken = handler()
try:
    broken.stop()
    raise AssertionError('Missing binding failed to interrupt original teardown')
except AttributeError as error:
    assert 'wg_havokSetSniperMode' in str(error)
assert 'component.destroy' not in events
assert not any(isinstance(value, tuple) and value[0] == 'cancel' for value in events)
assert len(broken._CallbackDelayer__callbacks) == 3
assert len(broken.onCameraChanged) == 1

# The same original owner sequence completes with the restored native ABI.
BigWorld.wg_havokSetSniperMode = function
events[:] = []
fixed = handler()
fixed.stop()
assert fixed.onCameraChanged is None
assert not fixed.settingsCore.onSettingsChanged
assert fixed._CallbackDelayer__callbacks == {}
assert sorted(value[1] for value in events if isinstance(value, tuple)) == [71, 72]
assert events.index('mode.destroy') < events.index('remote.destroy')
assert events.index('remote.destroy') < events.index('component.destroy')
assert events.index('component.destroy') < min(events.index(('cancel', number)) for number in (71, 72))
# Mode cleanup itself tolerates repeated disable; there is no new state owner.
mode.disable()
mode.disable()
print('Original #1513 sniper ABI and callback teardown passed')
