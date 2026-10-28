"""Real CPython 2.7 invokes original #1513 period/invitation PYC."""
from __future__ import print_function
import marshal
import sys
import types
import zipfile
import _notifier_host as host

host.focus(True)  # Partial startup does not construct a native owner.
assert host.events() == []
assert host.initScripts() == 1
import BigWorld
notifier = BigWorld.WGWindowsNotifier
assert type(notifier).__name__ == 'WGWinNotifier'
assert not callable(notifier)


def events():
    result=host.events()
    host.clearEvents()
    return result


host.configure(123,123,False,True,True,True,0,True)
for name in ('onBattleBeginning','onInvitation'):
    assert getattr(notifier,name)() is None
assert events() == []
host.configure(123,999,False,True,True,True,0,True)
notifier.onBattleBeginning()
assert events() == ['load','create','progress:123:1:1','state:123:4','flash:123:6:0:0']
notifier.onInvitation(ignoredKeyword=True)
assert events() == ['release','create','progress:123:1:1','state:123:8','flash:123:6:0:0']
host.focus(False)
assert events() == []
host.focus(True)
assert events() == ['release','create','progress:123:0:0','state:123:0','flash:123:0:0:0']
for name in ('onBattleBeginning','onInvitation','resetTaskbar'):
    try:
        getattr(notifier,name)(1)
        raise AssertionError('Wrong arity accepted')
    except TypeError as error:
        assert str(error)==name+'() takes no arguments.',str(error)
assert events()==[]

with zipfile.ZipFile(sys.argv[1]) as package:
    def method(member,cls,name):
        code=marshal.loads(package.read(member)[8:])
        owner=next(value for value in code.co_consts if isinstance(value,types.CodeType) and value.co_name==cls) if cls else code
        function=next(value for value in owner.co_consts if isinstance(value,types.CodeType) and value.co_name==name)
        return types.FunctionType(function,dict(BigWorld=BigWorld,__builtins__=__builtins__))
    notify=method('scripts/client/gui/battle_control/controllers/period_ctrl.pyc','ArenaPeriodController','_doNotify')
    class Controller(object):_isNotified=False
    controller=Controller()
    del BigWorld.WGWindowsNotifier
    try:
        notify(controller)
        raise AssertionError('Missing notifier failed to interrupt original period notification')
    except AttributeError:
        assert controller._isNotified is False
    BigWorld.WGWindowsNotifier=notifier
    notify(controller)
    assert controller._isNotified is True
    first=events()
    assert first[-3:]==['progress:123:1:1','state:123:4','flash:123:6:0:0']
    notify(controller)
    assert events()==[]
    invite=method('scripts/client/gui/shared/utils/__init__.pyc',None,'showInvitationInWindowsBar')
    invite()
    assert events()[-3:]==['progress:123:1:1','state:123:8','flash:123:6:0:0']
    auto_search=method('scripts/client/gui/Scaleform/daapi/view/lobby/cyberSport/CyberSportMainWindow.pyc',
                       'CyberSportMainWindow','onUnitAutoSearchSuccess')
    auto_search.func_globals['CYBER_SPORT_ALIASES']=type('Aliases',(),{'AUTO_SEARCH_CONFIRMATION_STATE':'confirm'})
    class Search(object):
        def _CyberSportMainWindow__initState(self,**values):
            assert values=={'acceptDelta':12}
    auto_search(Search(),12)
    assert events()[-3:]==['progress:123:1:1','state:123:8','flash:123:6:0:0']

# IsAppThemed unavailable/false, COM failure, and a NULL interface preserve
# the original fallback flashing rather than raising into the arena period.
for library,procedure,themed,result,pointer in [(False,True,True,0,True),
        (True,False,True,0,True),(True,True,False,0,True),
        (True,True,True,-1,False),(True,True,True,0,False)]:
    host.close()
    events()
    host.initScripts()
    notifier=BigWorld.WGWindowsNotifier
    host.configure(123,999,False,library,procedure,themed,result,pointer)
    notifier.onBattleBeginning()
    assert events()[-1]=='flash:123:6:0:0'
    notifier.resetTaskbar()
    assert events()[-1]=='flash:123:0:0:0'

# Native resources have no Python finalization dependency. The hidden worker
# and an unavailable window never touch the shared desktop's taskbar state.
host.close();events();host.initScripts();notifier=BigWorld.WGWindowsNotifier
for window,worker in [(123,True),(0,False)]:
    host.configure(window,999,worker,True,True,True,0,True)
    notifier.onBattleBeginning();notifier.onInvitation();notifier.resetTaskbar()
    host.focus(True)
    assert events()==[]
host.configure(123,999,False,True,True,True,0,True)
notifier.onInvitation();events()
host.close()
assert events()==['release','free']
host.close();notifier.onBattleBeginning();host.focus(True)
assert events()==[]
print('Original #1513 notifier PYC and native Win32/COM boundaries passed')
