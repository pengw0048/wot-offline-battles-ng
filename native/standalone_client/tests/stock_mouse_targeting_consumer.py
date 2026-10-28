# Exact original _Targeting and forced GUI lifecycle over the native SDK binding.
from __future__ import print_function
import marshal,zipfile,types,sys,math
import _mouse_targeting_host as native
z=zipfile.ZipFile(sys.argv[1]);root=marshal.loads(z.read('scripts/client/AvatarInputHandler/__init__.pyc')[8:])
codes={}
def walk(code,path=''):
 path=path+'.'+code.co_name;codes[path]=code
 for item in code.co_consts:
  if isinstance(item,types.CodeType):walk(item,path)
walk(root)
def close(a,b):assert abs(a-b)<1e-6,(a,b)
native.cursor(1,.25,-.5);matrix=native.create()
initial=native.sample(matrix);close(initial[0],.25);close(initial[1],-.5)
# The same bool converter and arity guards as the original native wrapper.
for value,state in ((True,True),(False,False),(1,True),(0,False),(-17,True),('TrUe',True),('FALSE',False)):
 assert matrix.detach(value,ignored_keyword=object()) is None
 native.cursor(1,.5,.75);before=native.sample(matrix)
 native.cursor(1,-.75,.125);after=native.sample(matrix)
 if state:assert before[:3]==after[:3]
 else:close(after[0],-.75);close(after[1],.125)
for args in ((),(False,True),(None,),(.5,),(1L,),([],),('yes',)):
 try:matrix.detach(*args)
 except TypeError:pass
 else:raise AssertionError(('invalid detach admitted',args))
# Focus, bounds and detach are independent gates. Constructor always samples,
# even when out-of-window/unfocused. Every matrix read still uses current camera.
native.cursor(0,2.,3.);fresh=native.create();assert native.sample(fresh)[:3]==(2.,3.,1.)
native.cursor(1,.25,-.5);matrix.detach(False);native.sample(matrix)
for focus,x,y in ((0,.8,.9),(1,1.001,0),(1,0,-1.001),(1,float('nan'),0)):
 native.cursor(focus,x,y);out=native.sample(matrix);close(out[0],.25);close(out[1],-.5)
for x,y in ((-1.,-1.),(1.,1.)):
 native.cursor(1,x,y);out=native.sample(matrix);close(out[0],x);close(out[1],y)
matrix.detach(True);native.cursor(1,0.,0.);native.camera(10.,20.,30.)
out=native.sample(matrix);assert out[:3]==(11.,21.,31.)
matrix.detach(False);out=native.sample(matrix);assert out[:3]==(10.,20.,31.)
native.camera(0.,0.,0.)
print('Production targeting owner: arity/bool conversion, cached coordinate, focus/bounds, detach/re-attach and current camera passed')
class Box(object):pass
BigWorld=Box();BigWorld.target=Box();BigWorld.MouseTargettingMatrix=native.create
BigWorld.target.clear=lambda:events.append('clear')
player=Box();player.target='current';player.targetBlur=lambda value:events.append(('blur',value))
BigWorld.player=lambda:player
namespace={'__builtins__':__builtins__,'BigWorld':BigWorld}
class Targeting(object):pass
for name in ('__init__','enable','detach','onRecreateDevice'):
 setattr(Targeting,name,types.FunctionType(codes['.<module>._Targeting.'+name],namespace,name))
events=[];targeting=Targeting();targeting.enable(True)
assert BigWorld.target.isEnabled and BigWorld.target.source is targeting._Targeting__mouseMatProv
provider=BigWorld.target.source;native.cursor(1,.2,.3);native.sample(provider)
class Apps(object):
 def attachCursor(self,*args,**kwargs):events.append(('attachCursor',args,kwargs))
 def detachCursor(self,*args):events.append(('detachCursor',args))
 def syncCursor(self,*args,**kwargs):events.append(('syncCursor',args,kwargs))
class Flags(object):CURSOR_ATTACHED=1;AIMING_ENABLED=2
class Settings(object):
 class APP_NAME_SPACE(object):SF_BATTLE='battle'
class Aiming(object):USER_DISABLED=7
namespace.update({'g_appLoader':Apps(),'GUI_CTRL_MODE_FLAG':Flags,'settings':Settings,'AIMING_MODE':Aiming})
class Handler(object):
 def setAimingMode(self,*args):events.append(('aiming',args))
handler=Handler();handler._AvatarInputHandler__isDetached=False;handler._AvatarInputHandler__targeting=targeting
handler._AvatarInputHandler__curCtrl=Box();handler._AvatarInputHandler__curCtrl.setForcedGuiControlMode=lambda value:events.append(('control',value))
forced=types.FunctionType(codes['.<module>.AvatarInputHandler.setForcedGuiControlMode'],namespace)
assert forced(handler,1) is True
native.cursor(1,.8,.9);out=native.sample(provider);close(out[0],.2);close(out[1],.3)
assert forced(handler,1) is False # Already detached: sync existing GUI owner.
assert forced(handler,0) is True
out=native.sample(provider);close(out[0],.8);close(out[1],.9)
assert ('control',False) in events and ('control',True) in events
assert any(event[0]=='attachCursor' for event in events if isinstance(event,tuple))
assert any(event[0]=='detachCursor' for event in events if isinstance(event,tuple))
targeting.onRecreateDevice();targeting.enable(False);assert not BigWorld.target.isEnabled and BigWorld.target.source is None
targeting.enable(False);assert BigWorld.target.source is None
# New account/battle owner samples a new coordinate rather than stale old state.
native.cursor(1,-.4,.6);second=Targeting();second.enable(True)
assert BigWorld.target.source is not provider
out=native.sample(BigWorld.target.source);close(out[0],-.4);close(out[1],.6)
second.enable(False)
print('Original #1513 PYC: enable/source, forced GUI detach/sync/re-attach, device reset, disable/retire and next owner passed')
