from __future__ import print_function
import marshal
import os
import types
import zipfile
import resource_owner as p

assert p.enableStreaming([]) is False
assert p.disableStreaming() is None
assert p.wg_precacheScaleformResource('missing') is False
p._start()
assert p.enableStreaming([]) is False
assert p.enableStreaming([None]) is False
assert p.enableStreaming(['gui/video/login.usm', None]) is True
assert p.disableStreaming() is None
for call in (lambda:p.enableStreaming(), lambda:p.enableStreaming('video'),
             lambda:p.enableStreaming([1]), lambda:p.disableStreaming(1),
             lambda:p.wg_precacheScaleformResource(),lambda:p.wg_eraseScaleformResFromCache(1)):
 try:call()
 except TypeError:pass
 else:raise AssertionError('invalid call was accepted')
assert p.wg_eraseScaleformResFromCache() is None
assert p.wg_eraseScaleformResFromCache('missing') is None
assert p.resetScaleformResources() is None and p._reloads()==1
p._exercise()
p._start()
client=os.environ.get('WOT_0922_CLIENT','/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD')
z=zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))
def codes(code):
 yield code
 for value in code.co_consts:
  if isinstance(value,types.CodeType):
   for child in codes(value):yield child
# Execute original IntroPage load/dispose consumers, preserving their list and
# argument behavior. GUI/super hooks are deliberately inert dependency seams.
class Base(object):
 def _populate(self):pass
 def _dispose(self):pass
class Page(Base):
 def _IntroPage__showNextMovie(self):pass
 onUpdateStage=None
scope={'super':super,'IntroPage':Page,'ScaleformFileLoader':p,'getPathForFlash':lambda x:'gui/flash/'+x,'gui':type('Gui',(),{'g_guiResetters':set()})}
for code in codes(marshal.loads(z.read('scripts/client/gui/Scaleform/daapi/view/login/IntroPage.pyc')[8:])):
 if code.co_name in ('_populate','_dispose'):setattr(Page,code.co_name,types.FunctionType(code,scope))
page=Page();page._IntroPage__movieFiles=['intro.usm'];page._populate();page._dispose();assert not scope['gui'].g_guiResetters
# Run the exact LoginView bytecode call blocks, with their original names and
# constants. These straight-line blocks have no view lifecycle dependencies.
login=marshal.loads(z.read('scripts/client/gui/Scaleform/daapi/view/login/LoginView.pyc')[8:])
for code in codes(login):
 if code.co_name=='_populate' and code.co_firstlineno==366:
  block=code.co_code[248:270]+'d\x00\x00S'
 elif code.co_name=='_dispose' and code.co_firstlineno==401:
  block=code.co_code[:10]+'d\x00\x00S'
 else:continue
 probe=types.CodeType(0,0,code.co_stacksize,0,block,code.co_consts,code.co_names,(),code.co_filename,code.co_name,code.co_firstlineno,'')
 types.FunctionType(probe,{'ScaleformFileLoader':p,'getPathForFlash':lambda x:'gui/flash/'+x,'_LOGIN_VIDEO_FILE':'login.usm'})()
# Execute the actual enabled precache module: its returned admission is the
# native result, and clear reaches the same cache owner.
import sys
sys.modules['BigWorld']=p
debug=types.ModuleType('debug_utils');debug.LOG_DEBUG=debug.LOG_ERROR=lambda *args:None;sys.modules['debug_utils']=debug
scope={};exec(marshal.loads(z.read('scripts/client/gui/shared/fo_precache.pyc')[8:]),scope)
assert scope['add']('missing',False) is False
assert scope['clear']() is None
p._stop();assert p.disableStreaming() is None
print('Production owner and original Python consumers passed')
