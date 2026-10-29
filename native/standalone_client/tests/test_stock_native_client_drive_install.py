"""Actual bound local pose precedes any accepted presentation sample."""
import os
from pathlib import Path
import subprocess
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE
class PlayerInitialPose(ContactDifferential):
 def test_bound_pose_is_the_cold_producer(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-Wall','-Wextra','-Werror','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_install_host.cpp'),'-o',str(folder/'client_drive_install_test.so')]
  compiled=subprocess.run(command,capture_output=True,text=True);self.assertEqual(compiled.returncode,0,compiled.stderr)
  script=r'''
import client_drive_install_test as native
values=[0,1,63,127,128,255]
for packed in (values,tuple(values),bytearray(values),bytearray()):
 assert native.hazards(packed)==list(packed)
assert native.hazards([4294967295])==[4294967295]
for invalid in ([-1],[4294967296],[1.5],None,{'0':1},'abc'):
 try:native.hazards(invalid)
 except (RuntimeError,TypeError,ValueError):pass
 else:raise AssertionError('Invalid hazard cells admitted: %r'%(invalid,))
class O(object):pass
b=O();b._local_matrix=object();b._local_position=(1.,3.25,2.);b._local_yaw=.3;b._local_pitch=.1;b._local_roll=-.2;b._local_speed=0.;b._local_air_lateral=(0.,0.)
# The first alignment runs after native presentation binding and before the
# snapshot/presentation owner has ever donated actor.current.
for admitted in (0,1):assert native.initial(b,admitted)==(1.,3.25,2.,.3,.1,-.2,0.)
b._local_matrix=None
try:native.initial(b,1)
except RuntimeError:pass
else:raise AssertionError('Unbound local pose was fabricated')
b._local_matrix=object();b._local_position=(float('nan'),0.,0.)
try:native.initial(b,0)
except RuntimeError:pass
else:raise AssertionError('Nonfinite local pose was admitted')
'''
  result=subprocess.run([py27,'-B','-c',script],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
