"""Production MPSBackgroundTask under CPython, including pristine PixieBG PYC."""
from __future__ import print_function
import gc
import marshal
import sys
import types
import weakref
import zipfile
import _particle_clone_host as H


def settled():
    assert H.backgroundStatus()[1:4]==(0,0,0),H.backgroundStatus()
    gc.collect()


# None and non-weak-referenceable callbacks still load the resource, create
# and release their native result, and do not enter Script::call.
for callback in (None,42,'not weak-referenceable',len):
    before=H.backgroundStatus()
    H.submit('resource',callback)
    assert H.backgroundStatus()[1:4]==(1,1,0)
    H.background()
    assert H.backgroundStatus()[1:4]==(1,0,1)
    assert H.counts()[:2]==(1,1)
    H.complete()
    settled()
    assert H.backgroundStatus()[0]==before[0]+1 and H.backgroundStatus()[4]==before[4]
    assert H.counts()[:2]==(0,0),H.counts()

calls=[]
class Callback(object):
    def __call__(self,value):calls.append(value)


callback=Callback();observed=weakref.ref(callback)
H.submit('resource',callback)
del callback
gc.collect()
assert observed() is None
H.background();H.complete();settled()
assert not calls and H.counts()[:2]==(0,0)

# A live callable receives one native wrapper, with ownership retained after
# the completed background task is destroyed. Successful and failing loads
# share the same main-thread completion boundary.
callback=Callback()
H.submit('resource',callback)
H.background();H.complete();settled()
assert len(calls)==1 and calls[0] is not None
assert H.counts()[:2]==(1,1)
calls[:]=[]
for name in ('missing','warning'):
    H.submit(name,callback)
    H.background();H.complete();settled()
    assert calls==[None] and H.counts()[:2]==(0,0)
    calls[:]=[]

# Cancelling before I/O or between I/O and completion releases the weak
# registration and every loaded native resource without calling Python.
for start in (False,True):
    H.submit('cancelled',callback)
    if start:H.background()
    H.cancel();settled()
    assert not calls and H.counts()[:2]==(0,0)

# A callback may remove every scheduler registration synchronously and keep
# the delivered result. The selected callback/result snapshots remain valid.
class Reenter(object):
    def __call__(self,value):
        H.cancel();calls.append(value)
        H.submit('next',callback)
reenter=Reenter()
H.submit('reenter',reenter);H.background();H.complete()
assert len(calls)==1 and H.inspect(calls[0])['particles']==0
assert H.backgroundStatus()[1:4]==(1,1,0)
calls[:]=[]
H.background();H.complete();settled();calls[:]=[]

class Raises(object):
    def __call__(self,value):raise RuntimeError('intentional particle callback failure')
raises=Raises();before=H.backgroundStatus()[4]
H.submit('resource',raises);H.background();H.complete();settled()
assert H.backgroundStatus()[4]==before+1 and H.counts()[:2]==(0,0)
class NotCallable(object):pass
not_callable=NotCallable();before=H.backgroundStatus()[4]
H.submit('resource',not_callable);H.background();H.complete();settled()
assert H.backgroundStatus()[4]==before+1 and H.counts()[:2]==(0,0)
print('Production particle background: weak/None/dead callbacks, resource failures, cancellation, re-entry and exception retirement passed')

# The original stopLoading() drops its guarded loader. A native strong
# callback would keep that bound method alive and still deliver the result.
archive=zipfile.ZipFile(sys.argv[1])
module=types.ModuleType('Pixie');module.createBG=H.submit;sys.modules['Pixie']=module
loading=types.ModuleType('vehicle_systems.stricted_loading');loading.restrictBySpace=lambda callback:callback
vehicle_systems=types.ModuleType('vehicle_systems');vehicle_systems.stricted_loading=loading
sys.modules['vehicle_systems']=vehicle_systems;sys.modules['vehicle_systems.stricted_loading']=loading
debug=types.ModuleType('debug_utils');errors=[]
debug.LOG_ERROR=lambda *args:errors.append(args)
debug.LOG_CURRENT_EXCEPTION=lambda *args:None
sys.modules['debug_utils']=debug
original=types.ModuleType('original_pixie_bg')
exec(marshal.loads(archive.read('scripts/client/helpers/PixieBG.pyc')[8:]),original.__dict__)

pixie=original.PixieBG('resource',callback)
pixie.stopLoading()
H.background();H.complete();settled()
assert pixie.pixie is None and not calls
pixie.destroy();del pixie

pixie=original.PixieBG('missing',callback)
H.background();H.complete();settled()
assert pixie.pixie is None and errors and not calls
pixie.destroy();del pixie

pixie=original.PixieBG('resource',callback)
H.background();H.complete();settled()
assert calls==[pixie] and pixie.pixie is not None
prototype=pixie.pixie.clone()
next_pixie=original.PixieBG('resource',None,prototype.clone())
before=H.backgroundStatus()
assert before[1:4]==(1,1,0)
H.background();H.complete();settled()
assert H.backgroundStatus()[4]==before[4] and H.backgroundStatus()[0]==before[0]+1
assert next_pixie.pixie is not None
calls[:]=[]
pixie.destroy();next_pixie.destroy()
del pixie,next_pixie,prototype,callback,reenter,raises,not_callable
gc.collect()
assert H.counts()[:2]==(0,0),H.counts()
print('Original PixieBG PYC: cancelled loader, load failure and repeated clone createBG(name, None) passed')
