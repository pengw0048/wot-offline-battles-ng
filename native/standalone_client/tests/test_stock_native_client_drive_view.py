"""Actual CPython 2.7 getsets read the unique native player, without install."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_0101_SOURCE')
SCRIPT=r'''
import ast,sys
import client_drive_view_test as native
source=open(sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read()
tree=ast.parse(source);runtime=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='BattleRuntime')
runtime.body=[n for n in runtime.body if isinstance(n,ast.FunctionDef)and n.name in ('local_pose','_record_position','_presentation_origin','_turret_detachment_pose')]
ns={};exec compile(ast.Module(body=[runtime]),'actual-player-consumers','exec')in ns
Battle=ns['BattleRuntime'];native.install(Battle);native.install(Battle)
b=Battle();b._worker_mode=b._replay_mode=False;b._local_position=(378.,5.481,-38.);b._local_yaw=-1.37;b._local_speed=0.;b._spectated_engine_id=None
assert b.local_pose()==((378.,5.481,-38.),-1.37)
assert '_native_client_drive_owner'not in b.__dict__
try:b._local_pitch
except AttributeError:pass
else:raise AssertionError('Missing cold state was fabricated')
native.bind(b)
for i in range(1200):
 native.move(b,float(i));position=(float(i),2.*i,-float(i))
 assert b.local_pose()==(position,i*.01)
 assert b._record_position({'local':True})==position
 assert b._presentation_origin()==position
 assert (b._local_pitch,b._local_roll,b._local_speed,b._local_turn_speed,b._local_vertical_speed)==(i*.02,i*.03,i*.04,i*.05,i*.06)
 assert b._local_airborne is bool(i%2)
 assert b._local_air_lateral==(i*.10,i*.11)
 b._local_position=(-999.,0.,0.);b._local_speed=-999.
 assert b._local_position==position and b._local_speed==i*.04
 assert b.__dict__['_local_position']==(-999.,0.,0.)
 for flag in ('_worker_mode','_replay_mode'):
  setattr(b,flag,True);assert b._local_position==(-999.,0.,0.)and b._local_speed==-999.;setattr(b,flag,False)
native.local_death(b);assert b._local_speed==0.
b._local_speed=42.;assert b._local_speed==0.,'Cold setter became a second simulation writer'
native.retire(b);assert b._local_speed==42.and b._local_position==(-999.,0.,0.)
del b._local_speed
try:b._local_speed
except AttributeError:pass
else:raise AssertionError('Deleted cold state remained visible')
class Child(Battle):pass
child=Child();child._local_position=(1,2,3);assert child._local_position==(1,2,3)
print('1200 native pose frames, original event/AOI consumers, cold/replay/worker storage and explicit death edge passed')
'''
@unittest.skipUnless(SOURCE,'Pinned gameplay source is required')
class NativePlayerStateView(unittest.TestCase):
 def test_actual_native_field_descriptors(self):
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  production=(ROOT/'bigworld/stock_native_client_drive_python.cpp').read_text();start=production.index('PyObject*localDeath(');end=production.index('\n',start)
  with tempfile.TemporaryDirectory(prefix='native-player-view-')as tmp:
   folder=Path(tmp);host=(ROOT/'tests/stock_native_client_drive_view_host.cpp').read_text().replace('@LOCAL_DEATH@',production[start:end]);(folder/'host.cpp').write_text(host)
   law=(Path(SOURCE)/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
   (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
   command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-Wall','-Wextra','-Werror','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(folder/'host.cpp'),'-o',str(folder/'client_drive_view_test.so')]
   result=subprocess.run(command,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
   (folder/'fixture.py').write_text(SCRIPT);result=subprocess.run([py27,'-B',str(folder/'fixture.py'),SOURCE],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_cold_class_install_and_death_command_seams(self):
  import sys
  sys.path.insert(0,str(ROOT/'bigworld'));import stock_native_client_drive_bindings as bindings
  source=(Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read_text();staged=bindings.battle(source)
  self.assertEqual(staged.count('native_client_drive.install_state_views(BattleRuntime)'),1)
  tree=ast.parse(staged);health=next(n for n in ast.walk(tree)if isinstance(n,ast.FunctionDef)and n.name=='_apply_health')
  self.assertEqual(sum(1 for n in ast.walk(health)if isinstance(n,ast.Attribute)and n.attr=='local_death'),1)
  production=(ROOT/'bigworld/stock_native_client_drive_python.cpp').read_text();start=production.index('DriverAccess existingPlayer(');end=production.index('\nPyObject*installStateViews',start)
  self.assertNotIn('installPlayer(',production[start:end]);self.assertNotIn('driverAccess(',production[start:end])
