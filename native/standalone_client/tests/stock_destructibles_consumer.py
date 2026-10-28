from __future__ import print_function
import marshal
import sys
import types
import zipfile
import _destructibles_host as host
assert host.initScripts()==17
import BigWorld
class Value(object):
    def __init__(self,**values):self.__dict__.update(values)
class Manager(object):
    def __init__(self):self.space=0;self.events=[];self.retire=False
    def getSpaceID(self):return self.space
    def startSpace(self,space):self.space=space;self.events.append(('start',space))
    def onChunkLoad(self,chunk,count):
        self.events.append(('load',chunk,count))
        # Synchronous stock callbacks must observe fully published identity.
        assert BigWorld.wg_getChunkDestrFilenames(self.space,chunk)==['','','']
        assert BigWorld.wg_getDestructibleFilename(self.space,chunk,0)==''
        assert BigWorld.wg_getDestructibleFilename(self.space,chunk,2)==''
        before=list(self.events);host.child();assert self.events==before # Original reverse map is not ready in this callback.
        if self.retire:host.unload()
    def onChunkLoose(self,chunk):
        self.events.append(('loose',chunk))
        assert BigWorld.wg_getDestructibleMatrix(self.space,chunk,0) is None
        host.unload()
    def onPlayModuleDestructionAnimation(self,*args):self.events.append(('child',)+args)
manager=Manager()
package=zipfile.ZipFile(sys.argv[1]);code=marshal.loads(package.read('scripts/client/game.pyc')[8:])
game=types.ModuleType('game');sys.modules['game']=game
scope={'AreaDestructibles':Value(g_destructiblesManager=manager)}
for c in code.co_consts:
    if isinstance(c,types.CodeType)and c.co_name in ('wg_onChunkLoad','wg_onChunkLoose','wg_playModuleDestructionAnimation'):
        setattr(game,c.co_name,types.FunctionType(c,scope))
chunk=0x8081
host.prepare();assert BigWorld.wg_getDestructibleMatrix(41,chunk,0)is None
assert host.bind();assert manager.events==[('start',41),('load',chunk,4)]
assert BigWorld.wg_getDestructibleMatrix(41,chunk,0)[12:15]==(20.,3.,40.)
assert BigWorld.wg_getNMHardPointMatrix(41,chunk,1,1)[12:15]==(121.,3.,240.)
assert BigWorld.wg_getDestructibleEffectCategory(41,chunk,0,0)==2
assert BigWorld.wg_getDestructibleEffectName(41,chunk,0,0,'hitEffect')=='hit'
assert BigWorld.wg_getDestructibleEffectName(41,chunk,0,0,'unknown')=='none'
assert BigWorld.wg_getDestructibleEffectScale(41,chunk,0,0)==1.25
assert BigWorld.wg_getDestructibleHeight(41,chunk,0)==7.
assert BigWorld.wg_getFallingParams(41,chunk,0)==(0.,)*16
assert BigWorld.wg_getDestructibleLifetimeEffectChance(-1,-1,-1,-1)==1.
assert BigWorld.wg_getChunkModelFashion(41,chunk,0)is None
assert not BigWorld.wg_setChunkModelFashion(41,chunk,0,None,False)
assert BigWorld.wg_setChunkModelFashion(41,chunk,0,host.fashion(),False)
assert not BigWorld.wg_setChunkModelFashion(41,chunk,-1,host.fashion(),False)
BigWorld.wg_setDestructibleMatrix(41,chunk,0,host.provider(5.,9.),2.)
BigWorld.wg_destroyFragile(41,chunk,0,True,False,True)
BigWorld.wg_destroyModule(41,chunk,1,1,False,True,False)
BigWorld.wg_restoreDestructibles(41)
assert host.calls()==[(0,0,105,209,2),(1,0,1,0,1),(1,1,0,1,0),(2,0,0,0,0),(2,0,0,0,0)]
host.child();assert manager.events[-1]==('child',chunk,1,1,True,False)
assert host.bind();assert len(manager.events)==3
# Execute the exact stock delayed-destruction consumers over the production ABI.
area=marshal.loads(package.read('scripts/client/AreaDestructibles.pyc')[8:])
def find(code,name):
    if code.co_name==name:return code
    for value in code.co_consts:
        if isinstance(value,types.CodeType):
            result=find(value,name)
            if result:return result
self=Value(_DestructiblesManager__spaceID=41)
fragile=types.FunctionType(find(area,'__setFragileDestroyed'),{'BigWorld':BigWorld})
module=types.FunctionType(find(area,'__setModuleDestroyed'),{'BigWorld':BigWorld})
before=len(host.calls());fragile(self,99,chunk,0,True,False,False,None,False);assert len(host.calls())==before
fragile(self,41,chunk,0,True,False,False,None,False)
module(self,41,chunk,1,1,False,True,False,False)
assert host.calls()[-2:]==[(1,0,1,0,0),(1,1,0,1,0)]
for name,valid in (
 ('wg_getChunkMatrix',(41,chunk)),('wg_getChunkDestrFilenames',(41,chunk)),
 ('wg_getDestructibleMatrix',(41,chunk,0)),('wg_getDestructibleFilename',(41,chunk,0)),
 ('wg_getDestructibleHeight',(41,chunk,0)),('wg_getFallingParams',(41,chunk,0)),
 ('wg_getChunkModelFashion',(41,chunk,0)),('wg_getDestructibleEffectCategory',(41,chunk,0,0)),
 ('wg_getDestructibleEffectScale',(41,chunk,0,0)),('wg_getNMHardPointMatrix',(41,chunk,0,0)),
 ('wg_getDestructibleLifetimeEffectChance',(41,chunk,0,0)),
 ('wg_getDestructibleEffectName',(41,chunk,0,0,'hitEffect')),
 ('wg_setChunkModelFashion',(41,chunk,0,host.fashion(),False)),
 ('wg_setDestructibleMatrix',(41,chunk,0,host.provider(0.,0.),0.)),
 ('wg_destroyFragile',(41,chunk,0,False,False,False)),
 ('wg_destroyModule',(41,chunk,0,0,False,False,False)),('wg_restoreDestructibles',(41,))):
    function=getattr(BigWorld,name)
    for args in (valid[:-1],valid+(0,)):
        try:function(*args)
        except TypeError:pass
        else:raise AssertionError((name,'argument count accepted',args))
    try:function(*('bad',)+valid[1:])
    except TypeError:pass
    else:raise AssertionError((name,'integer conversion accepted'))
try:BigWorld.wg_setDestructibleMatrix(41,chunk,0,None,0.)
except TypeError:pass
else:raise AssertionError('Invalid MatrixProvider accepted')
assert BigWorld.wg_getDestructibleFilename(41,chunk,3)is None
assert BigWorld.wg_getDestructibleFilename(41,chunk,8)is None
host.unload();host.unload();assert manager.events[-1]==('loose',chunk)
assert BigWorld.wg_getDestructibleMatrix(41,chunk,0)is None
manager.retire=True;host.prepare();assert not host.bind()
assert manager.events[-2:]==[('load',chunk,4),('loose',chunk)]
# Leave a bound owner for the C++ harness to retire after Py_Finalize.
manager.retire=False;host.prepare();assert host.bind()
print('Production WGDE owner: original game callbacks, parser, forward/reverse identity, re-entry and retirement passed')
