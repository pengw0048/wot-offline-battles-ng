"""Original control-mode PYC consumes the production native render setters."""
from __future__ import print_function
import marshal
import os
import sys
import types
import zipfile
import BigWorld
import _control_render_host as host


def child(code, name):
    return next(value for value in code.co_consts
                if isinstance(value, types.CodeType) and value.co_name == name)


assert host.lowDetailed() is False
assert host.floraEnabled() is True
for name, read in [('wg_setLowDetailedMode', host.lowDetailed), ('setFloraEnabled', host.floraEnabled)]:
    function = getattr(BigWorld, name)
    for value, expected in [(False, False), (True, True), (0, False), (-2, True),
                            ('true', True), ('FALSE', False)]:
        assert function(value) is None
        assert read() is expected
    for value in (None, 1.0, 1L, u'true', '', 'yes', [], object()):
        before = read()
        try:
            function(value)
            raise AssertionError('Invalid bool accepted')
        except TypeError as error:
            assert str(error) == '() argument 1 must be set to a bool', str(error)
        assert read() is before
    for args in ((), (True, False)):
        try:
            function(*args)
            raise AssertionError('Invalid arity accepted')
        except TypeError as error:
            assert str(error) == name + '() expects 1 argument of type bool', str(error)


class Object(object):
    def __init__(self, **values):
        self.__dict__.update(values)


calls = []


class Base(object):
    def enable(self, **args):
        calls.append('enable')

    def disable(self):
        calls.append('disable')


with zipfile.ZipFile(sys.argv[1]) as package:
    modes = marshal.loads(package.read('scripts/client/AvatarInputHandler/control_modes.pyc')[8:])
for name in ('StrategicControlMode', 'ArtyControlMode'):
    namespace = dict(BigWorld=BigWorld, STRATEGIC_CAMERA=Object(TRAJECTORY=1, AERIAL=0))
    namespace['__builtins__'] = __builtins__
    cls = type(name, (Base,), {})
    namespace[name] = cls
    code = child(modes, name)
    for method in ('enable', 'disable'):
        setattr(cls, method, types.FunctionType(child(code, method), namespace))
    mode = cls()
    mode.enable()
    assert host.floraEnabled() is (name == 'ArtyControlMode')
    mode.disable()
    assert host.floraEnabled() is True
    mode.disable()
    assert host.floraEnabled() is True
assert calls == ['enable', 'disable', 'disable'] * 2

# Reuse the original sniper/input-handler/CallbackDelayer cleanup regression,
# this time also using the real low-detail setter instead of a boundary fake.
BigWorld.wg_setLowDetailedMode(True)
execfile(os.path.join(os.path.dirname(__file__), 'stock_havok_sniper_consumer.py'))
assert host.lowDetailed() is False
assert host.floraEnabled() is True
print('Original #1513 control modes restored native render owners')
