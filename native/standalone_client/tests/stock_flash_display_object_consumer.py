# Original #1513 Tween code runs over the SDK C++ proxy and descriptor bindings.
import marshal
import sys
import types
import zipfile
import host
import Math

with zipfile.ZipFile(sys.argv[2]) as archive:
    root = marshal.loads(archive.read('scripts/client/gui/Scaleform/managers/TweenSystem.pyc')[8:])
classes = dict((c.co_name, c) for c in root.co_consts if isinstance(c, types.CodeType))
class AbstractTween(object):
    ALPHA, X, Y, ROTATION, SCALE_X, SCALE_Y = 'alpha', 'x', 'y', 'rotation', 'scaleX', 'scaleY'
    def getTarget(self): return self.target
class Tween(AbstractTween): pass
scope = {'__builtins__': __builtins__, '_AbstractTween': AbstractTween, 'Math': Math}
for code in classes['_PythonTween'].co_consts:
    if isinstance(code, types.CodeType):
        setattr(Tween, code.co_name, types.FunctionType(code, scope))

tween = Tween()
tween.target = host.DisplayObject()
if sys.argv[1] == 'pristine':
    try: tween.setPropToTargetDO({'alpha': 80.0}, None)
    except AttributeError as error:
        trace = sys.exc_info()[2]
        while trace.tb_next: trace = trace.tb_next
        assert trace.tb_lineno == 567
        assert trace.tb_frame.f_code.co_filename.endswith('managers/TweenSystem.py')
        assert "NoneType" in str(error) and 'translation' in str(error)
    else: raise AssertionError('Pristine dispatch did not reproduce the real Tween failure')
    assert tween.target.displayMatrix is None
    assert host.stats(tween.target)[:2] == (1, 0)
    print('Negative control: original Tween fails at line 567 after matrix was assigned to AS undefined')
    sys.exit(0)

# Repeated writes must reach GFx::SetDisplayMatrix, with no AS member of that name.
for x, y in ((12.5, 24.0), (-21.5, 9.25), (0.0, 0.0), (3.0, 5.0)):
    tween.setPropToTargetDO({'alpha': 60.0, 'x': x, 'y': y}, None)
    matrix = tween.target.displayMatrix
    assert isinstance(matrix, Math.Matrix)
    assert abs(matrix.translation.x-x) < 1e-5
    assert abs(matrix.translation.y-y) < 1e-5
    info = tween.target.getDisplayInfo()
    assert (info.x, info.y, info.alpha) == (x, y, 60.0)
assert host.stats(tween.target)[:2] == (0, 8)
for unused in range(20):
    tween.setPropToTargetDO({'alpha': 75.0}, None)
assert host.stats(tween.target)[:2] == (0, 28)

# A second native descriptor also bypasses dynamic AS SetMember.
tween.target.text = 'native text'
assert tween.target.text == 'native text'
assert host.stats(tween.target)[0] == 0
# AS members still use the real conversion and the uncached/cached proxy branches.
for value in (7, -1, 2.5, True, 'value', [1, 'two'], (3, 4), {'key': [5]}):
    tween.target.dynamic = value
    expected = list(value) if isinstance(value, tuple) else value
    actual = tween.target.dynamic
    if isinstance(value, dict):
        assert actual.key == [5]
    else: assert actual == expected, (actual, expected)
if sys.argv[1] == 'combined':
    tween.target.tags = set(('ignored', 'friend'))
    assert set(tween.target.tags) == set(('ignored', 'friend'))
# Script is handled before both native descriptors and AS fields.
script = object()
tween.target.script = script
assert tween.target.script is script
# SDK's GetDisplayMatrix failure contract remains a fresh identity Matrix.
host.control(tween.target, 0, 1)
assert isinstance(tween.target.displayMatrix, Math.Matrix)
assert tween.target.displayMatrix.translation.x == 0.0
assert tween.target.displayMatrix.translation.y == 0.0
host.control(tween.target, 1, 0)
# Native writes work even when GFx rejects dynamic SetMember.
tween.target.text = 'still native'
assert tween.target.text == 'still native'
try: tween.target.unknownRejected = 1
except AttributeError: pass
else: raise AssertionError('Failed native and AS writes must retain the SDK AttributeError')
print('Original Tween repeated updates and native/dynamic descriptor dispatch passed')
