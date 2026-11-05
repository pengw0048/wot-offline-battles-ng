"""Actual manifest/snapshot admission must be invariant under room object views."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_stock_wire_codec import python27_audit_runtime

ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import copy,sys
sys.path.insert(0,sys.argv[1])
import client_state_shared_test as native
mode=sys.argv[2]
def same(first,second,path='$'):
 if isinstance(first,dict) and isinstance(second,dict):
  assert set(first)==set(second),'%s keys differ'%path
  for key in sorted(first):same(first[key],second[key],path+'.'+str(key))
 elif isinstance(first,list) and isinstance(second,list):
  assert len(first)==len(second),'%s count %d != %d'%(path,len(first),len(second))
  for index,(a,b) in enumerate(zip(first,second)):same(a,b,path+'[%d]'%index)
 else:assert first==second,'%s: %r != %r'%(path,first,second)
def compare(manifest,frames):
 plain=native.replay(manifest,frames,0)
 production=native.replay(manifest,frames,1)
 fully_shared=native.replay(manifest,frames,2)
 same(plain,production,'objectView')
 same(plain,fully_shared,'fullyShared')
 return fully_shared
def actor(identifier,x=0.,health=1000):
 return dict(id=identifier,team=1 if identifier==2 else 2,slot=0,name='Actor',
  vehicle='ussr:R11_MS-1',x=x,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,aim_yaw=0.,
  gun_pitch=0.,speed=1.,movement_dir=1,rotation_dir=0,world_pose=True,
  alive=True,health=health,max_health=1000)
manifest=dict(round_id=9,players=[actor(2)],bots=[actor(16)])
def snapshot(sequence,revision,source,phase='loading',x=0.):
 return dict(round_id=9,server_tick=sequence,bot_state_revision=revision,
  motion_time_us=source+20000,bot_state_time_us=source,server_time_ms=sequence*33,
  players=[actor(2,x)],bots=[actor(16,x)],timing=dict(phase=phase),
  bot_order_revision=sequence,bot_orders=[dict(id=16,move_target=[float(sequence),0.])])
if mode=='manifest':
 shared=compare(manifest,[])
 assert sorted(shared[0]['actors'])==['bot:16','player:2'],shared
 assert len(shared[0]['events'])==2,shared
elif mode=='snapshot':
 frames=[snapshot(1,0,100000),snapshot(2,1,133333,'prebattle',1.),
  snapshot(3,2,166666,'battle',2.),snapshot(4,2,166666,'battle',3.)]
 frames[-1]['bot_orders']=None;frames[-1]['bot_order_revision']=99
 shared=compare(manifest,frames)
 last=shared[-1]
 assert last['sequence']==4 and last['bot_revision']==2,last
 assert last['motion_time']==186666 and last['bot_time']==166666,last
 assert last['server_time_ms']==132 and last['phase']=='battle',last
 assert last['order_revision']==3,last
 assert last['actors']['bot:16']['target'][0]==2.,last
 assert last['actors']['player:2']['target'][0]==3.,last
 assert any(e['type']=='order' for e in shared[1]['events']),shared
elif mode=='cadence':
 frames=[snapshot(1,1,100000),snapshot(2,2,300000,'loading',1.),snapshot(3,3,500000,'loading',2.)]
 for frame in frames[1:]:frame['_client_coalesced_timing']=dict(source_interval_us=50000.,snapshot_interval_us=33333.)
 shared=compare(manifest,frames)
 assert shared[-1]['actors']['bot:16']['observed']<110000.,shared
elif mode=='optional':
 optional=dict(manifest,round_id=None)
 shared=compare([manifest,optional],[])
else:raise AssertionError(mode)
print('shared client state %s passed'%mode)
'''

class SharedClientState(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  if sys.platform=='win32' or not shutil.which('c++'):
   raise unittest.SkipTest('The CPython 2.7 extension requires a Unix C++ compiler')
  cls.py27=os.environ.get('WOT_PYTHON27') or python27_audit_runtime()
  cls.folder=tempfile.TemporaryDirectory(prefix='client-state-shared-');cls.addClassCleanup(cls.folder.cleanup)
  cls.path=Path(cls.folder.name);(cls.path/'fixture.py').write_text(FIXTURE)
  cls.compile()
 @classmethod
 def compile(cls,source=None):
  prefix=subprocess.check_output([cls.py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  link=['-bundle','-undefined','dynamic_lookup'] if sys.platform=='darwin' else ['-shared','-fPIC']
  command=['c++','-std=c++17','-O1','-Wno-register']+(['-DWOT_CLIENT_REPLICA_HOST_BUILD'] if source else [])+link+[
   '-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),
   str(ROOT/'tests/stock_native_client_state_shared_host.cpp'),
   str(source or ROOT/'tests/stock_native_client_state_host.cpp'),
   '-o',str(cls.path/'client_state_shared_test.so')]
  result=subprocess.run(command,capture_output=True,text=True)
  if result.returncode:raise AssertionError(result.stdout+result.stderr)
 def check(self,mode):
  result=subprocess.run([self.py27,'-B',str(self.path/'fixture.py'),str(self.path),mode],capture_output=True,text=True)
  self.assertEqual(0,result.returncode,result.stdout+result.stderr)
 def test_manifest_preserves_shared_actor_rows(self):self.check('manifest')
 def test_snapshot_preserves_revision_timing_actor_and_order_paths(self):self.check('snapshot')
 def test_snapshot_preserves_coalesced_timing(self):self.check('cadence')
 def test_optional_null_manifest_round_is_equivalent(self):self.check('optional')

if __name__=='__main__':unittest.main()
