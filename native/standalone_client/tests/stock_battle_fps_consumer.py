# Python 2.7, executed by the real PY_AUTO/CPython debug-query host.
from __future__ import print_function
import collections
import json
import marshal
import sys
import types
import zipfile

import _debug_host as host

assert host.initScripts() == 4
import BigWorld

with open(sys.argv[1], 'rb') as source:
    actions = json.load(source)
for args in ((1,), (1, 2)):
    try:
        BigWorld.getBattleFPS(*args)
    except TypeError as error:
        assert str(error) == 'getBattleFPS() takes no arguments.'
    else:
        raise AssertionError('getBattleFPS accepted positional arguments')
try:
    BigWorld.getBattleFPS(extra=True)
except TypeError as error:
    assert str(error) == 'getBattleFPS() takes no keyword arguments'
else:
    raise AssertionError('getBattleFPS accepted keyword arguments')
output = []
for action in actions:
    if action[0] == 'enable':
        BigWorld.enableBattleStatisticCollector(action[1])
    elif action[0] == 'frame':
        host.tickStatistics(action[1])
    elif action[0] == 'loading':
        host.setLoadingTimer(action[1])
    elif action[0] == 'get':
        output.append(BigWorld.getBattleFPS())
    else:
        raise AssertionError(action)

with zipfile.ZipFile(sys.argv[2]) as archive:
    root = marshal.loads(archive.read('scripts/client/gui/game_control/NotifyController.pyc')[8:])
cls = next(code for code in root.co_consts
           if isinstance(code, types.CodeType) and code.co_name == 'NotifyController')
code = next(code for code in cls.co_consts
            if isinstance(code, types.CodeType) and code.co_name == '__handlePlayingTimeOnArena')
handle = types.FunctionType(code, {'__builtins__': __builtins__, 'BigWorld': BigWorld,
                                  'LOG_DEBUG': lambda *args: None, 'LOG_ERROR': lambda *args: None})
State = collections.namedtuple('State', 'lastBattleAvgFps')
owner = type('Owner', (object,), {})()
owner.MIN_BATTLE_LENGHT = 10
owner._NotifyController__settings = State(-1.)
event = type('Event', (object,), {})()
event.ctx = {'time': 20}
handle(owner, event)
assert owner._NotifyController__settings.lastBattleAvgFps == BigWorld.getBattleFPS()[2]
event.ctx = {'time': 1}
owner._NotifyController__settings = State(123.)
handle(owner, event)
assert owner._NotifyController__settings.lastBattleAvgFps == 123.
event.ctx = {}
handle(owner, event)
saved = BigWorld.getBattleFPS
del BigWorld.getBattleFPS
event.ctx = {'time': 20}
try:
    handle(owner, event)
except AttributeError:
    pass
else:
    raise AssertionError('Original playing-time consumer did not require the native getter')
BigWorld.getBattleFPS = saved
print(json.dumps(output))
