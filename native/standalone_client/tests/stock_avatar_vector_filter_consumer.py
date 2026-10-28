"""Drive the production AvatarFilter vector methods from the original PYC.

Runs in the CPython 2.7 host. argv: scripts.pkg path. The original
AvatarObserver.onEnterWorld resolves all four methods before its observer
check; set_remoteCamera and __switchToObservedControlMode consume them.
"""
import marshal
import sys
import types
import zipfile

import _avatar_vector_host as host

PACKAGE = sys.argv[1]


HOOK = []


class Vector3(object):

    def __init__(self, *args):
        if len(args) == 1:
            args = tuple(args[0]) if not isinstance(args[0], (int, float)) else (args[0],) * 3
        self.x, self.y, self.z = [float(v) for v in args] if args else (0.0, 0.0, 0.0)

    def tuple(self):
        if HOOK:
            HOOK.pop()()
        return (self.x, self.y, self.z)

    def __iter__(self):
        return iter((self.x, self.y, self.z))

    def __eq__(self, other):
        return tuple(self) == tuple(other)

    def __ne__(self, other):
        return not self == other


host.setVectorType(Vector3)


def code(member, names):
    value = marshal.loads(zipfile.ZipFile(PACKAGE).read(member)[8:])
    for name in names:
        value = [c for c in value.co_consts if hasattr(c, 'co_name') and c.co_name == name][0]
    return value


def expect(text, function, *args):
    try:
        function(*args)
    except TypeError as error:
        assert str(error) == text, (str(error), text)
        return
    raise AssertionError('no TypeError: ' + text)


def count_error(*contract):
    """The engine argCountError text for an original #1513 call contract."""
    try:
        host.argCountError(*contract)
    except TypeError as error:
        return str(error)
    raise AssertionError('argCountError raised nothing')


def bits3(vector):
    import struct
    return tuple(struct.unpack('<I', struct.pack('<f', v))[0] for v in vector)


errors = []
debug = []


def namespace():
    big_world = types.ModuleType('BigWorld')
    big_world.serverTime = lambda: 10.3
    math_module = types.ModuleType('Math')
    math_module.Vector3 = Vector3

    class AVATAR_SUBFILTERS(object):
        CAMERA_SHOT_POINT = 0

    class FILTER_INTERPOLATION_TYPE(object):
        LINEAR = 0

    class CTRL_MODE_NAME(object):
        ARCADE, SNIPER, STRATEGIC, ARTY = 'arcade', 'sniper', 'strategic', 'arty'
    return {'__builtins__': __builtins__, 'BigWorld': big_world, 'Math': math_module,
            'AVATAR_SUBFILTERS': AVATAR_SUBFILTERS, 'FILTER_INTERPOLATION_TYPE': FILTER_INTERPOLATION_TYPE,
            'CTRL_MODE_NAME': CTRL_MODE_NAME, 'CTRL_MODES': {1: 'arcade'},
            '_STRATEGIC_VIEW': ('strategic', 'arty'),
            'LOG_ERROR': lambda *a: errors.append(a), 'LOG_DEBUG_DEV': lambda *a: debug.append(a)}


def method(names):
    member = 'scripts/client/AvatarObserver.pyc'
    return types.FunctionType(code(member, ['AvatarObserver'] + names), namespace())


class Avatar(object):
    def __init__(self, observer, filter):
        self.observer = observer
        self.filter = filter
        self.inWorld = True
        self.observerFPVControlMode = 1
        self.switched = []
        self.callbacks = []
        self.inputHandler = types.ModuleType('inputHandler')
        self.inputHandler.onVehicleControlModeChanged = self.switched.append
        # The original reschedules its own bound method.
        self._AvatarObserver__switchToObservedControlMode = 'switch'

    def isObserver(self):
        return self.observer

    def delayCallback(self, delay, callback):
        self.callbacks.append(delay)


def main():
    on_enter = method(['onEnterWorld'])
    remote = method(['set_remoteCamera'])
    switch = method(['__switchToObservedControlMode'])

    # Without the methods, the original fails before its observer check.
    for observer in (False, True):
        try:
            on_enter(Avatar(observer, object()))
        except AssertionError:
            pass
        else:
            raise AssertionError('original onEnterWorld accepted a filter without vector methods')
    assert len(errors) == 2 and 'syncVector3' in errors[0][0], errors
    del errors[:]

    # A non-observer resolves all four and changes nothing.
    filter = host.new()
    host.attach(filter)
    host.clock(1, 10.0, 10.05)
    avatar = Avatar(False, filter)
    on_enter(avatar)
    assert errors == [] and host.samples(filter, 0) == -1
    # An observer sets LINEAR and resets CAMERA_SHOT_POINT: the channel exists, empty.
    avatar.observer = True
    on_enter(avatar)
    assert host.samples(filter, 0) == (0, 0)

    # The remote camera syncs shot points; the switch reads them at serverTime.
    switch(avatar)
    assert avatar.switched == [] and avatar.callbacks == [0.0], (avatar.switched, avatar.callbacks)
    for stamp, point in ((10.0, (1.0, 2.0, 3.0)), (10.2, (3.0, 4.0, 5.0)), (10.4, (5.0, 6.0, 7.0))):
        avatar.remoteCamera = types.ModuleType('remoteCamera')
        avatar.remoteCamera.shotPoint = Vector3(point)
        avatar.remoteCamera.time = stamp
        remote(avatar, None)
    assert host.samples(filter, 0) == (3, 0)
    switch(avatar)
    assert avatar.switched == ['arcade'], avatar.switched
    value = avatar.filter.getVector3(0, 10.3)
    assert isinstance(value, Vector3) and value != Vector3(0, 0, 0)
    # Omitted get time stays zero (before every sample: the first sample).
    assert tuple(avatar.filter.getVector3(0)) == (1.0, 2.0, 3.0)
    # Omitted sync time takes the connection's lastMessageTime.
    host.clock(1, 11.0, 11.5)
    filter.syncVector3(1, Vector3(9.0, 9.0, 9.0))
    assert host.samples(filter, 1) == (1, 0)
    assert tuple(filter.getVector3(1, 99.0)) == (9.0, 9.0, 9.0)

    # resetVector3 keeps the mode; ids are unsigned 0..9; modes are not clamped.
    filter.setInterpolationType(2, 77)
    filter.syncVector3(2, (1.0, 1.0, 1.0), 12.0)
    filter.resetVector3(2)
    assert host.samples(filter, 2) == (0, 77)
    for bad in (10, -1, 1 << 20):
        filter.syncVector3(bad, (1.0, 1.0, 1.0), 12.0)
        filter.resetVector3(bad)
        filter.setInterpolationType(bad, 1)
        assert tuple(filter.getVector3(bad, 12.0)) == (0.0, 0.0, 0.0)
        assert host.samples(filter, bad) == -1

    # Argument counts and conversions keep the original #1513 contract.
    expect(count_error('getVector3', 1, 2, 'int', 'double'), filter.getVector3)
    expect(count_error('getVector3', 1, 2, 'int', 'double'), filter.getVector3, 0, 1.0, 2)
    expect(count_error('syncVector3', 2, 3, 'int', 'BW::Vector3', 'double'), filter.syncVector3, 0)
    expect(count_error('syncVector3', 2, 3, 'int', 'BW::Vector3', 'double'),
           filter.syncVector3, 0, (1.0, 2.0, 3.0), 1.0, 4)
    expect(count_error('resetVector3', 1, 1, 'int'), filter.resetVector3)
    expect(count_error('setInterpolationType', 2, 2, 'int', 'int'), filter.setInterpolationType, 0)
    expect('() argument 1 must be set to an int', filter.getVector3, 'a')
    expect('() argument 2 optionally must be set to a float', filter.getVector3, 0, 'a')
    expect('() argument 2 must be set to a Vector3 or a tuple of 3 floats', filter.syncVector3, 0, 5)
    expect('() argument 3 optionally must be set to a float', filter.syncVector3, 0, (1.0, 1.0, 1.0), 'a')
    expect('() argument 2 must be set to an int', filter.setInterpolationType, 0, 'a')
    assert host.samples(filter, 0) == (3, 0)

    # Conversion runs before the native filter is resolved: a re-entrant
    # replacement during conversion receives the write, never the old filter.
    old = host.generation(filter)
    HOOK.append(lambda: host.attach(filter))
    filter.syncVector3(3, Vector3(4.0, 4.0, 4.0), 13.0)
    assert host.generation(filter) != old and host.samples(filter, 3) == (1, 0)
    # A fresh native filter starts with fresh channels.
    assert host.samples(filter, 0) == -1
    HOOK.append(lambda: host.detach(filter))
    filter.syncVector3(3, Vector3(5.0, 5.0, 5.0), 14.0)
    log = host.log()
    assert any(line.startswith('error MF_ASSERT_DEV FAILED: pFilter') for line in log), log

    # Detached: writes are ignored and read a real zero Vector3; reattach replays nothing.
    for call in (lambda: filter.syncVector3(0, (1.0, 1.0, 1.0), 1.0), lambda: filter.resetVector3(0),
                 lambda: filter.setInterpolationType(0, 1)):
        assert call() is None
    zero = filter.getVector3(0, 1.0)
    assert isinstance(zero, Vector3) and tuple(zero) == (0.0, 0.0, 0.0)
    assert len([l for l in host.log() if l.startswith('error MF_ASSERT_DEV')]) == 4
    host.attach(filter)
    assert host.samples(filter, 0) == -1 and tuple(filter.getVector3(0, 1.0)) == (0.0, 0.0, 0.0)

    print('Original AvatarObserver consumers drove the production vector methods')

main()
