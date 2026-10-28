from __future__ import print_function
import BigWorld as B
import Math
import marshal
import json
import os
import sys
import types
import zipfile


def raises(error,function,*args):
    try:function(*args)
    except error:return
    raise AssertionError('Expected '+error.__name__)


for method in (B.flashBangAnimation,B.removeFlashBangAnimation):
    for args in ((),(None,None)):
        raises(TypeError,method,*args)
    raises(TypeError,method,42)
    assert method(None) is None

# Vector4Provider.coerce accepts a value as a newly allocated constant provider.
# Repeating that value for remove creates another identity, rather than erasing
# the first constant registration.
assert B.flashBangAnimation((.1,.2,.3,.4)) is None
B.removeFlashBangAnimation((.1,.2,.3,.4))
B.fixture('tick');B.fixture('draw')
assert B.fixture('stats')[3]==0x661a334d
B.fixture('close');B.fixture('open')

a=Math.Vector4Animation()
a.keyframes=[(0.,(.2,.4,.6,.8))]
a.duration=1.
refs=sys.getrefcount(a)
assert B.flashBangAnimation(a) is None
assert sys.getrefcount(a)==refs+1
B.flashBangAnimation(a)
assert sys.getrefcount(a)==refs+2
B.removeFlashBangAnimation(a)
assert sys.getrefcount(a)==refs+1
B.fixture('tick')
drawOffset,copyOffset=B.fixture('stats')[:2]
assert B.fixture('draw')
assert B.fixture('stats')==(drawOffset+1,copyOffset+1,'standard2D',0xcc336699)
assert B.fixture('draw')
assert B.fixture('stats')==(drawOffset+2,copyOffset+2,'textured2D',0xcc336699)
# Mutating the original provider changes the actual rendered colour.
a.keyframes=[(0.,(.4,.2,.8,.6))]
B.fixture('tick')
B.fixture('draw')
assert B.fixture('stats')==(drawOffset+3,copyOffset+3,'textured2D',0x996633cc)
B.fixture('deviceLoss')
B.fixture('draw')
assert B.fixture('stats')[2]=='standard2D'
B.fixture('managedLoss')
B.fixture('draw')
assert B.fixture('stats')[2]=='standard2D'
for failure in (1,2,3,4):
    before=B.fixture('stats')
    B.fixture('fail',failure)
    assert B.fixture('draw') is False
    after=B.fixture('stats')
    assert after[0]==before[0]+int(failure==4)
    assert after[1]==before[1]
    B.fixture('fail',0)
    assert B.fixture('draw')
    assert B.fixture('stats')[2]=='standard2D'
for failure in (5,6):
    B.fixture('managedLoss')
    B.fixture('fail',failure)
    before=B.fixture('stats')
    assert B.fixture('draw') is False
    assert B.fixture('stats')==before
    B.fixture('fail',0)
    assert B.fixture('draw')
    assert B.fixture('stats')[2]=='standard2D'
B.removeFlashBangAnimation(a)
B.removeFlashBangAnimation(a)
assert sys.getrefcount(a)==refs
B.fixture('tick')
before=B.fixture('stats')
assert B.fixture('draw') and B.fixture('stats')==before
B.fixture('absent')
B.flashBangAnimation(a)
assert sys.getrefcount(a)==refs
B.fixture('present')
B.flashBangAnimation(a)
B.fixture('close')
assert sys.getrefcount(a)==refs
B.flashBangAnimation(a)
assert sys.getrefcount(a)==refs
B.fixture('open')

client=os.environ.get('WOT_0922_CLIENT')
if client:
    with zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg')) as archive:
        root=marshal.loads(archive.read('scripts/client/helpers/EffectsList.pyc')[8:])
    klass=next(c for c in root.co_consts if isinstance(c,types.CodeType) and c.co_name=='_FlashBangEffectDesc')
    class EffectDesc(object):
        def __init__(self,section):pass
    class Stage(object):
        def __init__(self,colour,duration=.5):self.colour=colour;self.asFloat=duration
        def readVector4(self,key,default):assert key=='color';return self.colour
    class Stages(object):
        def __init__(self,stages):self.stages=stages
        def values(self):return self.stages
    class Section(object):
        def __init__(self,stages=None):self.stages=stages or [Stage((1.,.5,.25,1.)),Stage((0.,0.,0.,0.))]
        def __getitem__(self,key):assert key=='stages';return Stages(self.stages)
    Math.Vector4=lambda *args:args
    callbacks={}
    expectedDelay=.95
    def callback(delay,call):
        assert abs(delay-expectedDelay)<.00001
        index=len(callbacks)+1
        callbacks[index]=call
        return index
    def cancel(index):del callbacks[index]
    B.callback=callback;B.cancelCallback=cancel
    class Player(object):inputHandler=None
    player=Player()
    B.player=lambda:player
    namespace=types.FunctionType(klass,{'__name__':'EffectsList','_EffectDesc':EffectDesc,
        'Math':Math,'BigWorld':B,'__builtins__':__builtins__})()
    Desc=type('_FlashBangEffectDesc',(EffectDesc,),namespace)
    effect=Desc(Section());effects=[]
    assert effect.prerequisites()==[]
    effect.create(None,effects,{'showFlashBang':False})
    B.fixture('tick');before=B.fixture('stats');B.fixture('draw')
    assert B.fixture('stats')==before and len(effects)==1
    effect.create(None,effects,{})
    assert len(callbacks)==1
    B.fixture('tick');B.fixture('draw')
    assert B.fixture('stats')[3]==0xffff8040
    # Re-create removes the old identity and cancels exactly its callback.
    effect.create(None,effects,{})
    assert len(callbacks)==1
    callback_fn=list(callbacks.values())[0];callbacks.clear();callback_fn()
    B.fixture('tick');before=B.fixture('stats');B.fixture('draw')
    assert B.fixture('stats')==before
    class Input(object):isFlashBangAllowed=False
    player.inputHandler=Input()
    effect.create(None,effects,{})
    assert not callbacks
    player.inputHandler.isFlashBangAllowed=True
    effect.create(None,effects,{})
    assert len(callbacks)==1
    assert effect.delete(None,None) is True
    assert not callbacks
    assert effect.delete(None,None) is True
    if os.environ.get('WOT_FLASH_BANG_STAGES'):
        authored=json.load(open(os.environ['WOT_FLASH_BANG_STAGES']))
        for path,rows in authored:
            stages=[Stage(tuple(colour),duration) for duration,colour in rows]
            expectedDelay=sum(duration for duration,colour in rows)-.05
            effect=Desc(Section(stages));effect.create(None,[],{})
            assert len(callbacks)==1,path
            B.fixture('tick',.1)
            before=B.fixture('stats')[0]
            assert B.fixture('draw'),path
            assert B.fixture('stats')[0]==before+1,path
            assert effect.delete(None,None) is True and not callbacks,path
        print('Original authored flash-bang descriptors passed: %d'%len(authored))
    print('Original EffectsList flash-bang producer/lifecycle passed')
B.fixture('close')
print('FlashBang actual provider, shader draw, history/reset and CPython contracts passed')
