from __future__ import print_function
import gc
import marshal
import os
import sys
import types
import weakref
import zipfile
import preferences_owner as p

assert p.wg_preferencesExistedAtStartup() is False
for value in (-1,0,1):
 p._load(value);assert p.wg_preferencesExistedAtStartup() is (value>0)
events=[];roots=[]
def first(root):
 assert p._watch() is False
 assert root['graphicsPreferences'].readInt('nativeGraphics')==9
 assert root['devicePreferences'].readInt('windowedWidth')==800
 assert root['scriptsPreferences'].readInt('userValue')==71
 assert root['consoleHistory'].readInt('line')==1
 roots.append(root);events.append('first');root['devicePreferences'].writeInt('fromFirst',17)
def second(root):
 assert root['devicePreferences'].readInt('fromFirst')==17
 assert root is not roots[0]
 events.append('second')
assert p.wg_subscribeToSavePreferences(first) is None
p.wg_subscribeToSavePreferences(None);p.wg_subscribeToSavePreferences(37)
p.wg_subscribeToSavePreferences(second)
assert p.savePreferences() is True and events==['first','second']
assert p._watch() is True
assert p._root()['devicePreferences'].readInt('fromFirst')==17
assert p.wg_preferencesExistedAtStartup() is True
# Callback errors are reported, cleared, and do not turn an IO success into a failure.
p.wg_unsubAllSavePreferences();events=[]
def broken(root):raise RuntimeError('expected preference callback failure')
p.wg_subscribeToSavePreferences(broken);p.wg_subscribeToSavePreferences(lambda r:events.append('after-error'))
assert p.savePreferences() is True and events==['after-error']
p._fail(1);assert p.savePreferences() is False and p._watch() is True;p._fail(0)
p._emptyPath(1);before=list(events);assert p.savePreferences() is False and events==before;p._emptyPath(0)
# Dispatch retains the originally admitted list. Changes affect the next event.
p.wg_unsubAllSavePreferences();events=[]
def replacement(root):events.append('replacement')
def mutate(root):
 events.append('mutate');p.wg_unsubAllSavePreferences();p.wg_subscribeToSavePreferences(replacement)
p.wg_subscribeToSavePreferences(mutate);p.wg_subscribeToSavePreferences(lambda r:events.append('admitted'))
assert p.savePreferences() is True and events==['mutate','admitted']
assert p.savePreferences() is True and events==['mutate','admitted','replacement']
# Nested saves own independent roots and preserve the outer root's mutation.
p.wg_unsubAllSavePreferences();events=[];nested=[False]
def reenter(root):
 events.append('nested' if nested[0] else 'outer')
 root['devicePreferences'].writeInt('marker',2 if nested[0] else 1)
 if not nested[0]:
  nested[0]=True;assert p.savePreferences();nested[0]=False
p.wg_subscribeToSavePreferences(reenter);assert p.savePreferences() and events==['outer','nested']
assert p._root()['devicePreferences'].readInt('marker')==1
# Strong references, duplicates, and clearing both event registries.
p.wg_unsubAllSavePreferences();events=[]
class Receiver(object):
 def onRead(self,*args):events.append(args)
receiver=Receiver();reference=weakref.ref(receiver)
p.wg_subscribeToReadPreferences(receiver.onRead);p.wg_subscribeToReadPreferences(receiver.onRead)
del receiver;gc.collect();assert reference() is not None
p._read(2);assert events==[('windowMode',2),('windowMode',2)]
p.wg_unsubAllSavePreferences();gc.collect();assert reference() is None
# Reentrant finalizers can admit a new receiver after an ordinary unregister.
class ReentrantFinalizer(object):
 def __call__(self,*args):pass
 def __del__(self):p.wg_subscribeToReadPreferences(lambda *args:events.append(('new',)+args))
obj=ReentrantFinalizer();p.wg_subscribeToSavePreferences(obj);del obj
p.wg_unsubAllSavePreferences();events=[];p._read(1);assert events==[('new','windowMode',1)]
p.wg_unsubAllSavePreferences()
# Original PreferencesSetting constructor and VideoModeSettings reader/writer.
client=os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD')
z=zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))
def walk(c,path=''):
 path=path+'.'+c.co_name
 yield path,c
 for value in c.co_consts:
  if isinstance(value,types.CodeType):
   for child in walk(value,path):yield child
codes=dict(walk(marshal.loads(z.read('scripts/client/account_helpers/settings_core/options.pyc')[8:])))
class Base(object):
 def __init__(self,isPreview=False):self.isPreview=isPreview
class Preferences(Base):pass
scope={'super':super,'PreferencesSetting':Preferences,'BigWorld':p}
for name in ('__init__','_savePrefsCallback','_readPrefsCallback'):
 code=codes['.<module>.PreferencesSetting.'+name]
 setattr(Preferences,name,types.FunctionType(code,scope))
class VideoMode(Preferences):pass
for name in ('_savePrefsCallback','_readPrefsCallback'):
 setattr(VideoMode,name,types.FunctionType(codes['.<module>.VideoModeSettings.'+name],scope))
v=VideoMode(False);p._read(2);assert v._VideoModeSettings__videoMode==2
assert p.savePreferences() and p._root()['devicePreferences'].readInt('windowMode')==2
p.wg_unsubAllSavePreferences()
# Original Settings.save delegates the actual save producer.
sys.modules['BigWorld']=p
settings={};exec(marshal.loads(z.read('scripts/client/Settings.pyc')[8:]),settings)
assert settings['Settings'](None,None,None).save() is None
# Shutdown closes admission before releasing receivers and stops an active
# dispatch safely. It remains harmless twice and leaves no interpreter refs.
events=[];shutdownRejected=[]
class ShutdownFinalizer(object):
 def __call__(self,*args):events.append('unexpected shutdown callback')
 def __del__(self):
  try:p.wg_subscribeToReadPreferences(None)
  except RuntimeError:shutdownRejected.append(True)
shutdown=ShutdownFinalizer();shutdownRef=weakref.ref(shutdown)
p.wg_subscribeToSavePreferences(lambda r:p._fini());p.wg_subscribeToSavePreferences(lambda r:events.append('late'))
p.wg_subscribeToSavePreferences(shutdown);del shutdown
assert p.savePreferences() is True and events==[]
assert shutdownRejected==[True] and shutdownRef() is None
try:p.wg_subscribeToReadPreferences(None)
except RuntimeError:pass
else:raise AssertionError('shutdown admitted a receiver')
p._fini();p._fini();roots[:]=[];p._load(0);assert p.wg_preferencesExistedAtStartup() is False
for call in (lambda:p.wg_subscribeToSavePreferences(),lambda:p.wg_subscribeToReadPreferences(None,None),lambda:p.wg_unsubAllSavePreferences(1),lambda:p.wg_preferencesExistedAtStartup(1)):
 try:call()
 except TypeError:pass
 else:raise AssertionError('invalid arity accepted')
p._fini()
print('Production preference owner, real save body and original consumers passed')
