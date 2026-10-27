# Python 2.7 driver for stock_scaleform_focus_host.cpp: argv[1] is pristine or patched.
import sys
import _Scaleform

MODE = sys.argv[1]


def raises(kind, text, function, *args):
    try:
        function(*args)
    except kind as error:
        assert str(error) == text, (str(error), text)
    else:
        raise AssertionError('no %s: %s' % (kind.__name__, text))


def expect(events, focussed='unchanged'):
    actual = _Scaleform.events()
    assert actual == events, (actual, events)
    if focussed != 'unchanged':
        assert _Scaleform.focussed() == focussed, (_Scaleform.focussed(), focussed)


_Scaleform.configure(0, 1)
lobby = _Scaleform.create('lobby')
if MODE == 'pristine':
    # The 37b1e207 failure at lobby_entry.py:143 and the #1056 root write.
    raises(TypeError, 'setFocussed() takes no arguments.', lobby.setFocussed, 'gui/flash')
    lobby.setFocussed()
    lobby.backgroundAlpha = 0.0
    expect(['lobby:SetVariable:backgroundAlpha'])
    del lobby
    expect(['removeMovieView'])
    print('pristine: setFocussed takes no argument and backgroundAlpha reaches the AS3 root')
    sys.exit(0)

# 00aa5ab0: exactly one BW::string, through the engine's PY_AUTO converters.
raises(TypeError, 'setFocussed() expects 1 argument of type BW::string', lobby.setFocussed)
raises(TypeError, 'setFocussed() expects 1 argument of type BW::string', lobby.setFocussed, 'a', 'b')
raises(TypeError, '() argument 1 must be set to a string.', lobby.setFocussed, 5)
raises(TypeError, '() argument 1 must be set to a string.', lobby.setFocussed, None)
expect([], None)

# lobby_entry/battle_entry pass SCALEFORM_SWF_PATH_V3; no IME manager is installed.
assert lobby.setFocussed('gui/flash') is None
expect(['lobby:SetFocus'], 'lobby')
lobby.setFocussed('gui/flash')
expect([], 'lobby')

battle = _Scaleform.create('battle')
_Scaleform.configure(1, 1)
battle.setFocussed(u'gui/flash')
expect(['lobby:KillFocus', 'resolve:gui/flash/IME.swf', 'IMEPath:C:/res/gui/flash/IME.swf',
        'battle:SetFocus'], 'battle')
lobby.setFocussed('gui/flash/')
expect(['battle:KillFocus', 'resolve:gui/flash/IME.swf', 'IMEPath:C:/res/gui/flash/IME.swf',
        'lobby:SetFocus'], 'lobby')
battle.setFocussed('gui\\flash\\')
expect(['lobby:KillFocus', 'resolve:gui\\flash\\IME.swf', 'IMEPath:C:/res/gui\\flash\\IME.swf',
        'battle:SetFocus'], 'battle')
lobby.setFocussed('')
expect(['battle:KillFocus', 'resolve:IME.swf', 'IMEPath:C:/res/IME.swf', 'lobby:SetFocus'], 'lobby')
_Scaleform.configure(0, 1)

# 00aa4750: only the focussed view sends KillFocus and clears the owner.
del battle
expect(['removeMovieView'], 'lobby')
del lobby
expect(['removeMovieView', 'lobby:KillFocus'], None)
for name in ('first', 'second', 'third'):
    view = _Scaleform.create(name)
    view.setFocussed('gui/flash')
    expect([name + ':SetFocus'], name)
    del view
    expect(['removeMovieView', name + ':KillFocus'], None)

# 00aa5d80: a native attribute stays native; any other name is an AS3 root variable.
root = _Scaleform.create('root')
root.backgroundAlpha = 0.0
assert root.backgroundAlpha == 0.0
expect([])
root.someVariable = 1
expect(['root:SetVariable:someVariable'])
_Scaleform.configure(0, 0)
raises(AttributeError, "'PyMovieView' object has no attribute 'missing'", setattr, root, 'missing', 1)
expect(['root:SetVariable:missing'])
del root
expect(['removeMovieView'], None)
print('patched: one BW::string, focus owner transitions and native-first setattr')
