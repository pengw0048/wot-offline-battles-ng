# Python 2.7 driver for stock_flash_input_mode_host.cpp: argv[1] is pristine or patched,
# argv[2] the exact #1513 scripts.pkg.
import dis
import marshal
import sys
import types
import zipfile

import GUI
import _Scaleform
import _flashhost as host

MODE, PACKAGE = sys.argv[1], zipfile.ZipFile(sys.argv[2])
KEY_A = 30


def code_of(path):
    return marshal.loads(PACKAGE.read('scripts/client/' + path)[8:])


def nested(code, *names):
    for name in names:
        code = [c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == name][0]
    return code


def instructions(code):
    raw, i, out = code.co_code, 0, []
    while i < len(raw):
        op = ord(raw[i])
        if op >= dis.HAVE_ARGUMENT:
            arg = ord(raw[i + 1]) | ord(raw[i + 2]) << 8
            value = code.co_consts[arg] if op in dis.hasconst else (
                code.co_names[arg] if op in dis.hasname else arg)
            out.append((dis.opname[op], value))
            i += 3
        else:
            out.append((dis.opname[op], None))
            i += 1
    return out


def module_constant(path, name):
    ops = instructions(code_of(path))
    index = ops.index(('STORE_NAME', name))
    assert ops[index - 1][0] == 'LOAD_CONST', ops[index - 1]
    return ops[index - 1][1]


def stub(name, **values):
    module = types.ModuleType(name)
    module.__dict__.update(values)
    sys.modules[name] = module
    if '.' in name:
        parent, child = name.rsplit('.', 1)
        setattr(sys.modules[parent], child, module)
    return module


def load(name, path):
    module = types.ModuleType(name)
    module.__file__ = 'scripts/client/' + path[:-1]
    sys.modules[name] = module
    if '.' in name:
        parent, child = name.rsplit('.', 1)
        setattr(sys.modules[parent], child, module)
    exec code_of(path) in module.__dict__
    return module


def expect(events):
    actual = host.events()
    assert actual == events, (actual, events)


def raises(kind, text, function, *args):
    try:
        function(*args)
    except kind as error:
        assert str(error) == text, (str(error), text)
        return sys.exc_info()[2]
    raise AssertionError('no %s: %s' % (kind.__name__, text))


# ---- Fixtures for modules outside this boundary; fade_window/fader_manager/Flash are exact PYC ----
calls = []


class CallbackDelayer(object):
    def __init__(self):
        self.pending = []

    def delayCallback(self, delay, callback):
        calls.append(('delayCallback', delay))
        self.pending.append(callback)

    def destroy(self):
        calls.append(('CallbackDelayer.destroy',))
        self.pending = []


class TimeDeltaMeter(object):
    def __init__(self):
        pass

    def measureDeltaTime(self):
        return 0.02


class FaderEvent(object):
    FADE_IN, FADE_OUT = 'fadeIn', 'fadeOut'

    def __init__(self, kind):
        self.kind = kind


class EventBus(object):
    def handleEvent(self, event, scope):
        calls.append(('handleEvent', event.kind, scope))


def log(*args):
    calls.append(('log',) + args)


stub('BigWorld', wg_setRedefineKeysMode=lambda value: calls.append(('wg_setRedefineKeysMode', value)))
stub('helpers')
stub('helpers.CallbackDelayer', CallbackDelayer=CallbackDelayer, TimeDeltaMeter=TimeDeltaMeter)
stub('gui', DEPTH_OF_Battle=module_constant('gui/__init__.pyc', 'DEPTH_OF_Battle'))
stub('gui.shared', g_eventBus=EventBus(), EVENT_BUS_SCOPE=type('Scope', (object,), {'LOBBY': 'lobby'}))
stub('gui.shared.events', FaderEvent=FaderEvent)
stub('gui.Scaleform', SCALEFORM_SWF_PATH=module_constant('gui/Scaleform/__init__.pyc', 'SCALEFORM_SWF_PATH'),
     SCALEFORM_SWF_PATH_V3=module_constant('gui/Scaleform/__init__.pyc', 'SCALEFORM_SWF_PATH_V3'))
stub('gui.Scaleform.managers')
stub('debug_utils', LOG_DEBUG=log, LOG_CODEPOINT_WARNING=log, LOG_ERROR=log, LOG_GUI=log)
stub('new_year')
fade_window = load('new_year.fade_window', 'new_year/fade_window.pyc')
fader_manager = load('gui.Scaleform.managers.fader_manager', 'gui/Scaleform/managers/fader_manager.pyc')
flash = load('gui.Scaleform.Flash', 'gui/Scaleform/Flash.pyc')

if MODE == 'pristine':
    # Root's f192cf84 failure: FaderManager.setup L25 -> FadeWindow.__init__ L17.
    fader = fader_manager.FaderManager()
    trace = raises(AttributeError, "'GUI.Flash' object has no attribute 'wg_inputKeyMode'", fader.setup)
    frames = []
    while trace:
        frames.append((trace.tb_frame.f_code.co_filename, trace.tb_lineno))
        trace = trace.tb_next
    assert frames[1:] == [('scripts/client/gui/Scaleform/managers/fader_manager.py', 25),
                          ('scripts/client/new_year/fade_window.py', 17)], frames
    expect(['MovieDef:gui/flash/fadeWindow.swf', 'fadeWindow.swf:created', 'fadeWindow.swf:showAsGlobalMovie:0',
            'fadeWindow.swf:visible:1', 'fadeWindow.swf:destroyed'])
    component = GUI.Flash(_Scaleform.MovieDef('gui/flash/fadeWindow.swf').createInstance())
    assert component.inputKeyMode == 0
    print('pristine: FaderManager.setup stops at fade_window.py:17; the public build names it inputKeyMode')
    sys.exit(0)

# ---- Every #1513 PYC site only stores the constant 1 or 2; no script reads the mode ----
sites = []
for name in sorted(PACKAGE.namelist()):
    if not name.endswith('.pyc'):
        continue
    stack = [marshal.loads(PACKAGE.read(name)[8:])]
    while stack:
        code = stack.pop()
        stack.extend(c for c in code.co_consts if isinstance(c, types.CodeType))
        ops = instructions(code)
        for index, (op, value) in enumerate(ops):
            if value == 'wg_inputKeyMode':
                assert op == 'STORE_ATTR', (name, code.co_name, op)
                constant = [v for o, v in ops[:index] if o == 'LOAD_CONST'][-1]
                sites.append((name, code.co_name, constant))
assert len(sites) == 13 and sorted(set(v for unused, unused, v in sites)) == [1, 2], sites
assert [(n.split('/')[-1], c, v) for n, c, v in sites if v == 1] == [
    ('battle_entry.pyc', '_setup', 1), ('Barracks.pyc', '_populate', 1)], sites

# ---- wg_inputKeyMode is the production uint attribute; movie stays read-only ----
probe = GUI.Flash(_Scaleform.MovieDef('gui/flash/probe.swf').createInstance())
assert probe.wg_inputKeyMode == 0 and not hasattr(probe, 'inputKeyMode')
assert sorted(probe.__members__)[-1] == 'wg_inputKeyMode', probe.__members__
for value, stored in ((2, 2), (True, 1), (2.7, 2), (-1.5, 4294967295), (3000000000, 3000000000),
                      (4294967295L, 4294967295), (0L, 0)):
    probe.wg_inputKeyMode = value
    assert probe.wg_inputKeyMode == stored and type(probe.wg_inputKeyMode) is type(stored), (value, stored)
# The setter stores before validating, in #1513 and the public engine alike.
probe.wg_inputKeyMode = 2
raises(TypeError, 'wg_inputKeyMode must be set to an uint', setattr, probe, 'wg_inputKeyMode', -1)
assert probe.wg_inputKeyMode == 4294967295
probe.wg_inputKeyMode = 2
raises(TypeError, 'wg_inputKeyMode must be set to an uint', setattr, probe, 'wg_inputKeyMode', -1L)
assert probe.wg_inputKeyMode == 4294967295
for value in ('2', None, (2,)):
    probe.wg_inputKeyMode = 2
    raises(TypeError, 'wg_inputKeyMode must be set to an uint', setattr, probe, 'wg_inputKeyMode', value)
    assert probe.wg_inputKeyMode == 2
raises(TypeError, 'Sorry, the attribute movie in GUI.Flash is read-only', setattr, probe, 'movie',
       _Scaleform.MovieDef('gui/flash/other.swf').createInstance())
del probe
expect(['MovieDef:gui/flash/probe.swf', 'probe.swf:created', 'probe.swf:showAsGlobalMovie:0', 'probe.swf:visible:1',
        'MovieDef:gui/flash/other.swf', 'other.swf:created', 'other.swf:destroyed', 'probe.swf:destroyed'])

# ---- The lobby application: exact Flash.__init__/active ----
lobby = flash.Flash('lobby.swf')
assert lobby.component.wg_inputKeyMode == 0 and lobby.component.focus and abs(lobby.component.position.z - 0.5) < 1e-6
lobby.active(True)
assert GUI.roots() == [lobby.component]
expect(['MovieDef:gui/scaleform/lobby.swf', 'lobby.swf:created', 'lobby.swf:showAsGlobalMovie:0',
        'lobby.swf:visible:1', 'lobby.swf:setFSCommandCallback', 'lobby.swf:setExternalInterfaceCallback'])
assert host.key(KEY_A) is True
expect(['lobby.swf:key:30:1'])

# ---- Exact FaderManager.setup -> FadeWindow.__init__ L8-17 ----
fader = fader_manager.FaderManager()
fader.setup()
window = fader._FaderManager__fadeWindow
fade = window._FadeWindow__component
assert fade.wg_inputKeyMode == 2 and fade.focus and fade.moveFocus and abs(fade.position.z - 0.1) < 1e-6
assert GUI.roots() == [lobby.component]
expect(['MovieDef:gui/flash/fadeWindow.swf', 'fadeWindow.swf:created', 'fadeWindow.swf:showAsGlobalMovie:0',
        'fadeWindow.swf:visible:1'])

# startFade -> activate: addRoot + reSort puts the fader (z 0.1) in front of the lobby (z 0.5).
done = []
fader.startFade([lambda: done.append(True)])
assert GUI.roots() == [lobby.component, fade]
expect(['fadeWindow.swf:set:backgroundAlpha=0.0'])
assert host.key(KEY_A) is True
expect(['lobby.swf:key:30:1'])
assert host.key(KEY_A, 0) is True
expect(['lobby.swf:key:30:0'])

# The same front root with the other legal modes: 0 consumes, 1 forwards and propagates.
fade.wg_inputKeyMode = 0
assert host.key(KEY_A) is True
expect(['fadeWindow.swf:key:30:1'])
fade.wg_inputKeyMode = 1
assert host.key(KEY_A) is True
expect(['fadeWindow.swf:key:30:1', 'lobby.swf:key:30:1'])
fade.wg_inputKeyMode = 3
assert host.key(KEY_A) is True
expect(['fadeWindow.swf:key:30:1'])

# Neither focus nor visibility gates the Flash forward; only mode 2 or the root list does.
fade.focus = False
fade.visible = False
assert host.key(KEY_A) is True
expect(['fadeWindow.swf:key:30:1'])
fade.wg_inputKeyMode = 2
assert host.key(KEY_A) is True
expect(['lobby.swf:key:30:1'])
fade.focus = True
fade.visible = True

# A script on the root takes over: the movie is never fed, whatever the mode.
class Script(object):
    def handleKeyEvent(self, event):
        calls.append(('script', event))
        return False
fade.wg_inputKeyMode = 0
fade.script = Script()
assert host.key(KEY_A) is True and calls[-1] == ('script', (KEY_A, 1))
expect(['lobby.swf:key:30:1'])
fade.script = None
fade.wg_inputKeyMode = 2

# The exact fade update runs to its end and deactivate removes the root.
del calls[:]
while fader.pending:
    callback = fader.pending.pop(0)
    if callback() is not None:
        fader.pending.append(callback)
assert done == [True] and GUI.roots() == [lobby.component] and fader.isInFade() is False
assert ('handleEvent', 'fadeOut', 'lobby') in calls
assert [e for e in host.events() if not e.startswith('fadeWindow.swf:set:backgroundAlpha=')] == []
assert host.key(KEY_A) is True
expect(['lobby.swf:key:30:1'])

# ---- Barracks._populate L251: exact statement on the lobby application ----
def replay(code, line, *args):
    class Stop(Exception):
        pass
    state = {'line': None}

    def local(frame, event, arg):
        if event == 'line':
            if state['line'] is None:
                state['line'] = frame.f_lineno
                frame.f_lineno = line
            elif frame.f_lineno != line:
                raise Stop()
        return local
    sys.settrace(lambda frame, event, arg: local if frame.f_code is code else None)
    try:
        types.FunctionType(code, {'__builtins__': __builtins__})(*args)
    except Stop:
        pass
    finally:
        sys.settrace(None)

barracks = nested(code_of('gui/Scaleform/daapi/view/lobby/barracks/Barracks.pyc'), 'Barracks', '_populate')
replay(barracks, 251, type('Barracks', (object,), {'app': lobby})())
assert lobby.component.wg_inputKeyMode == 1
assert host.key(KEY_A) is False
expect(['lobby.swf:key:30:1'])
lobby.component.wg_inputKeyMode = 0

# ---- BattleEntry._setup L160-165: the exact method on a battle application ----
battle = flash.Flash('battle.swf', path=sys.modules['gui.Scaleform'].SCALEFORM_SWF_PATH_V3)
battle.active(True)
host.events()
setup = nested(code_of('gui/Scaleform/battle_entry.pyc'), 'BattleEntry', '_setup')
del calls[:]
types.FunctionType(setup, {'DEPTH_OF_Battle': sys.modules['gui'].DEPTH_OF_Battle,
                           'SCALEFORM_SWF_PATH_V3': sys.modules['gui.Scaleform'].SCALEFORM_SWF_PATH_V3,
                           'BigWorld': sys.modules['BigWorld'], 'False': False})(battle)
assert battle.component.wg_inputKeyMode == 1 and calls == [('wg_setRedefineKeysMode', False)]
expect(['battle.swf:set:backgroundAlpha=0.0', 'battle.swf:setFocussed:gui/flash'])
# After a re-sort DEPTH_OF_Battle (0.1) puts the battle root in front: mode 1 feeds it and still
# reaches the lobby.
assert battle.component.position.z < lobby.component.position.z
GUI.reSort()
assert GUI.roots() == [lobby.component, battle.component]
assert host.key(KEY_A) is True
expect(['battle.swf:key:30:1', 'lobby.swf:key:30:1'])
battle.close()
expect(['battle.swf:destroyed'])
assert GUI.roots() == [lobby.component]

# ---- Teardown: exact FaderManager.destroy releases the fader component and its movie ----
fader.destroy()
assert GUI.roots() == [lobby.component]
del window, fade
expect(['fadeWindow.swf:destroyed'])
assert host.key(KEY_A) is True
expect(['lobby.swf:key:30:1'])

# Duplicate and absent roots are contained by SimpleGUI itself.
GUI.addRoot(lobby.component)
assert GUI.roots() == [lobby.component]
assert host.events()[0].startswith('warning:SimpleGUI::addSimpleComponent')

# ---- Movie replacement: a new application gets a new component at the default mode ----
lobby.component.wg_inputKeyMode = 1
lobby.close()
expect(['lobby.swf:destroyed'])
assert GUI.roots() == [] and host.key(KEY_A) is False
expect([])
GUI.delRoot(GUI.Flash(_Scaleform.MovieDef('gui/flash/orphan.swf').createInstance()))
host.events()
lobby = flash.Flash('lobby.swf')
assert lobby.component.wg_inputKeyMode == 0
lobby.active(True)
host.events()
assert host.key(KEY_A) is True
expect(['lobby.swf:key:30:1'])
print('patched: 13 PYC sites store 1/2; FadeWindow mode 2 routes keys past the fader to the lobby; '
      'Barracks/BattleEntry mode 1 forwards and propagates; teardown releases each movie')
