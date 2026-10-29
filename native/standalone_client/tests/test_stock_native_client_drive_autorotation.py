"""The native input consumer samples the actual #1513 mode-owned flag."""
import os
from pathlib import Path
import subprocess
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE

class AutorotationProducer(ContactDifferential):
 def test_handler_private_producer(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_autorotation_host.cpp'),str(ROOT/'bigworld/stock_native_client_input_controls.cpp'),'-o',str(folder/'client_autorotation_test.so')]
  compiled=subprocess.run(command,capture_output=True,text=True);self.assertEqual(compiled.returncode,0,compiled.stderr)
  fixture=r'''
import client_autorotation_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Handler(object):
 def getAutorotation(self):raise AssertionError('Python autorotation business callback')
b=O(_avatar=O(inputHandler=None));assert native.read(b)is False
for i in range(1000):
 handler=Handler();handler._AvatarInputHandler__isAutorotation=bool(i%3)
 b._avatar.inputHandler=handler
 assert native.read(b)is bool(i%3)
 # A control-mode or settings edge changes the same stock-owned field.
 handler._AvatarInputHandler__isAutorotation=not bool(i%3)
 assert native.read(b)is(not bool(i%3))
b._avatar.inputHandler=O();assert native.read(b)is False
b._avatar.inputHandler=Handler()
try:native.read(b)
except RuntimeError:pass
else:raise AssertionError('Missing private ABI state was defaulted')
'''
  result=subprocess.run([py27,'-B','-c',fixture],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
