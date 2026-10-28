"""Exact clone consumer bytecode over the production binding and SDK clone bodies."""
from __future__ import print_function
import gc
import marshal
import random
import sys
import types
import zipfile
import _particle_clone_host as H


source=H.create()
original=H.inspect(source)
assert original['particles']==7 and original['attached'] and original['inWorld']
copies=[source.clone(),source.clone(None,'ignored',123,ignored_keyword=True)]
for copy in copies:
    state=H.inspect(copy)
    assert state['owner']!=original['owner']
    assert state['action']!=original['action'] and state['renderer']!=original['renderer']
    assert state['capacity']==original['capacity'] and state['configuredRate']==original['configuredRate']
    assert state['actionRate']==original['actionRate'] and state['rendererSize']==original['rendererSize']
    assert state['particles']==0 and state['pending']==0 and not state['attached'] and not state['inWorld']
H.mutate(copies[0],81.)
assert H.inspect(source)==original and H.inspect(copies[1])['actionRate']==original['actionRate']
del source
gc.collect()
assert H.inspect(copies[1])['configuredRate']==original['configuredRate']
copies[1].clear()
del copy,copies
gc.collect()
assert H.counts()[:2]==(0,0),H.counts()

for groups in (0,1,2,0x10000,0xffff0000,0xffffffff):
    source=H.create(groups)
    H.mask(0xffff)
    baseline=H.counts()
    copy=source.clone('unused',unused=True)
    if groups&0xffff:
        assert copy is None and H.counts()==baseline
    else:
        assert H.inspect(copy)['groups']==groups
    H.mask(0)
    resumed=source.clone()
    assert H.inspect(resumed)['groups']==groups
    del source,copy,resumed
gc.collect()
assert H.counts()[:2]==(0,0),H.counts()
print('Production particle clone: native deep definitions, independent actions/renderers, empty live state, quality refusal and lifetimes passed')


def codes(code):
    yield code
    for value in code.co_consts:
        if isinstance(value,types.CodeType):
            for child in codes(value):yield child


archive=zipfile.ZipFile(sys.argv[1])
effects=marshal.loads(archive.read('scripts/client/helpers/EffectsList.pyc')[8:])
pixie=marshal.loads(archive.read('scripts/client/helpers/PixieBG.pyc')[8:])
descriptor=next(c for c in codes(effects) if c.co_name=='_PixieEffectDesc')
pending=[]
module=types.ModuleType('Pixie')
module.createBG=lambda filename,callback:pending.append((filename,callback))
sys.modules['Pixie']=module
loading=types.ModuleType('vehicle_systems.stricted_loading')
loading.restrictBySpace=lambda callback:callback
vehicle_systems=types.ModuleType('vehicle_systems');vehicle_systems.stricted_loading=loading
sys.modules['vehicle_systems']=vehicle_systems
sys.modules['vehicle_systems.stricted_loading']=loading
debug=types.ModuleType('debug_utils')
debug.LOG_ERROR=lambda *args:None
debug.LOG_CURRENT_EXCEPTION=lambda *args:None
sys.modules['debug_utils']=debug
pixie_module=types.ModuleType('original_pixie_bg')
exec(pixie,pixie_module.__dict__)


class Node(object):
    def __init__(self):self.attached=[]
    @property
    def actualNode(self):return self
    def attach(self,value):self.attached.append(value)


node=Node()
globals_={'__builtins__':__builtins__,'random':random,'PixieBG':pixie_module.PixieBG,
          '_getSurfaceAlignedTransform':lambda *args:node,'_findTargetNode':lambda *args:node}
consumer=type('OriginalPixieDescriptor',(object,),{})()
for name in ('create','_callbackAfterLoading','_callbackCreate'):
    function=next(c for c in codes(descriptor) if c.co_name==name)
    setattr(consumer,name,types.MethodType(types.FunctionType(function,globals_),consumer))
consumer._nodeName=[]
consumer._surfaceMatKinds=None
consumer._orientByClosestSurfaceNormal=False
consumer._havokFiles=None
consumer._files=('particles/Shells_Eff/trace_3.xml',)
consumer._force=1
consumer._alwaysUpdate=False
consumer._PixieEffectDesc__prototypePixies={}

# First flight loads once; its original callback unconditionally clones the
# native result before attaching the active effect and forcing emission.
entries=[]
consumer.create(object(),entries,{})
assert len(pending)==1 and not node.attached
filename,callback=pending.pop()
assert filename==consumer._files[0]
loaded=H.create()
callback(loaded)
prototype=consumer._PixieEffectDesc__prototypePixies[filename]
assert H.inspect(prototype)['pending']==0
assert H.inspect(loaded)['pending']==10 and node.attached==[loaded]

# The next exact create() calls the prototype's native clone directly. PixieBG
# still submits createBG(name, None); the clone attaches without waiting for it.
# Background-task callback ownership is outside this binding's host boundary.
consumer.create(object(),entries,{})
assert pending==[(filename,None)] and len(node.attached)==2
pending[:]=[]
next_effect=node.attached[1]
assert H.inspect(next_effect)['particles']==0 and H.inspect(next_effect)['pending']==1
assert H.inspect(next_effect)['owner']!=H.inspect(prototype)['owner']
H.mutate(next_effect,27.)
assert H.inspect(prototype)['actionRate']==4.5
for entry in entries:entry['pixie'].destroy()
node.attached[:]=[]
del entry,entries,loaded,prototype,next_effect,callback,consumer
gc.collect()
assert H.counts()[:2]==(0,0),H.counts()
print('Original EffectsList/PixieBG PYC: first-load prototype and repeated-flight native clone passed')
