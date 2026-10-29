"""Exercise native presentation against the pinned worker pose scheduler."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import math,sys,types,struct
def f32(value): return struct.unpack('f',struct.pack('f',value))[0]
import presentation_test as native
from gui.mods.offline_lan_0922 import battle_runtime as br
from gui.mods.offline_lan_0922.entities import remote_vehicle as nr
class O(object): pass
class Trap(O):
 def __getattribute__(self,key): raise AssertionError('Python entity callback: '+key)
entity=motion=Trap();entity._offlineLANPresentation=True;entity._aim_yaw=None
motion.position=(0.,0.,0.);motion.velocity=(0.,0.,0.);motion.yaw=motion.pitch=motion.roll=0.;motion._last_pose_time=None
motion.matrix={};motion.provider={};motion._matrix_pose=None;motion._animation=O();motion._render_pose=(0.,0.,0.,0.,0.,0.);motion._render_from=None;motion._render_to=None;motion._key_from={};motion._key_to={};motion._authority_geometry=False;appearance=O();motion.appearance=appearance;appearance.turretMatrix={};appearance.gunMatrix={};remote_filter=O();motion.filter=remote_filter;remote_filter.speed=0.;remote_filter.velocity=(0.,0.,0.);motion._aim_relative_yaw=None;motion._aim_gun_pitch=None
battle=O();battle._generation=1;battle._bots=O();battle._bots._sample_time_us=1;battle._bot_pose_times={};battle._bot_yaw_rates={};battle._authority_pose_skips=battle._authority_pose_writes=battle._authority_aim_skips=battle._authority_aim_writes=0
battle._remote_factory=O();battle._remote_factory._vehicles={10:entity};battle._runtime=O();battle._runtime.compatibility=O();battle._runtime.compatibility._battle_active=True;battle._runtime.compatibility._vehicle_property_overlays={id(entity):{}}
oracle=br.BattleRuntime.__new__(br.BattleRuntime);oracle._bot_pose_times={};oracle._bot_yaw_rates={}
class Vec(object):
 def __init__(self,v): self.x,self.y,self.z=v
reference=nr.RemoteVehicle.__new__(nr.RemoteVehicle);reference._animation=O();reference._render_pose=(0.,0.,0.,0.,0.,0.);reference._render_from=None;reference._render_to=None;reference._render_duration=0.;reference._key_from={};reference._key_to={}
reference._write_pose=lambda matrix,pose:None;reference._rekey=lambda duration:True
record={'kind':'bot','network_id':1,'engine_id':10,'ready':True};battle._records={'bot:1':record}
state={'id':1,'x':0.,'y':0.,'z':0.,'yaw':0.,'pitch':0.,'roll':0.,'aim_yaw':0.,'gun_pitch':0.,'speed':1.,'alive':True,'health':100}
def step(now,static=0):
 if record['ready']:
  yaw,pitch,roll=br._native_ypr((state['yaw'],state['pitch'],state['roll']))
  sample=(tuple(map(f32,(state['x'],state['y'],state['z']))),br._engine_rotation(yaw,pitch,roll))
  relax=br.BattleRuntime._bot_pose_relax(oracle,state,sample,now)
 writes=battle._authority_pose_writes
 errors,applied=native.run(battle,[state],now,static)
 assert not errors,errors
 if record['ready']:
  assert battle._bot_pose_times==oracle._bot_pose_times,(battle._bot_pose_times,oracle._bot_pose_times)
  assert set(battle._bot_yaw_rates)==set(oracle._bot_yaw_rates)
  for key in battle._bot_yaw_rates: assert abs(battle._bot_yaw_rates[key]-oracle._bot_yaw_rates[key])<1.e-12
  if battle._authority_pose_writes>writes:
   reference.position=Vec(sample[0]);reference.yaw,reference.pitch,reference.roll=yaw,pitch,roll;reference._retarget_render_pose(relax,now)
   for name in ('_render_pose','_render_from','_render_to'):
    actual=object.__getattribute__(motion,name);expected=getattr(reference,name)
    assert (actual is None)==(expected is None),(name,actual,expected)
    if actual is not None: assert max(abs(x-y) for x,y in zip(actual,expected))<1.e-12,(name,actual,expected)
 return applied
assert step(1.); assert (battle._authority_pose_writes,battle._authority_aim_writes)==(1,1)
assert step(1.1);assert battle._authority_pose_skips==1 and battle._authority_aim_skips==1
battle._bots._sample_time_us=2;step(1.2);assert battle._authority_pose_writes==2
state.update(x=3.,yaw=3.1,aim_yaw=-3.1,gun_pitch=.2);battle._bots._sample_time_us=3;step(1.3)
assert abs(appearance.turretMatrix['rotation'][0]-(((-3.1-3.1+math.pi)%(2*math.pi))-math.pi))<1.e-12
state.update(x=4.,yaw=-3.1,aim_yaw=-3.1);battle._bots._sample_time_us=4;step(1.4)
assert abs(battle._bot_yaw_rates[1]-((( -3.1-3.1+math.pi)%(2*math.pi)-math.pi)/.1))<1.e-8
state['speed']=0.;battle._bots._sample_time_us=5;step(1.5);assert remote_filter.speed==0. and remote_filter.velocity==(0.,0.,0.)
state['gun_pitch']=.3;step(1.6,1);assert appearance.turretMatrix['rotation'][0]==.25 and appearance.gunMatrix['rotation'][1]==-.1
old=record['projectile_collision_pose'];record['projectile_collision_pose']={};step(1.7,1);assert record['projectile_collision_pose'] is not old and record['projectile_collision_pose']['x']==4.
record['ready']=False;assert not step(1.8);assert '_authority_pose_signature' not in record
record['ready']=True;battle._generation+=1;oracle._bot_pose_times.clear();oracle._bot_yaw_rates.clear();step(1.9,1);assert record['_authority_presentation_lifecycle'][0]==2
# A retained target remains the interpolation source even if render_pose is absent.
old_target=(4.,0.,0.,-3.1,0.,0.)
motion._render_pose=None;motion._render_from=None;motion._render_to=old_target
reference._render_pose=None;reference._render_from=None;reference._render_to=old_target
state.update(x=4.5,speed=1.);battle._bots._sample_time_us=6
assert step(1.95,1)
assert object.__getattribute__(motion,'_render_from')==old_target
# Exercise the real _rekey exception recovery and continue publishing the pose.
class BrokenAnimation(object):
 @property
 def keyframes(self): return None
 @keyframes.setter
 def keyframes(self,value): raise RuntimeError('native animation was retired')
motion.model=O();reference.model=O();reference.matrix=object()
motion._animation=BrokenAnimation();reference._animation=BrokenAnimation()
reference._rekey=nr.RemoteVehicle._rekey.__get__(reference,nr.RemoteVehicle)
state.update(x=5.,speed=1.);battle._bots._sample_time_us=6
assert step(2.0,1)
assert object.__getattribute__(motion,'_animation') is None and reference._animation is None
assert object.__getattribute__(motion,'model').matrix is object.__getattribute__(motion,'matrix') and reference.model.matrix is reference.matrix
assert remote_filter.position==(5.,0.,0.) and remote_filter.speed>0.
class BrokenTime(object):
 @property
 def time(self):return 0.
 @time.setter
 def time(self,value):raise RuntimeError('native animation clock retired')
motion._animation=BrokenTime();reference._animation=BrokenTime()
state.update(x=5.1);battle._bots._sample_time_us=7
assert step(2.01,1)
assert object.__getattribute__(motion,'_animation') is None and reference._animation is None
assert object.__getattribute__(motion,'model').matrix is object.__getattribute__(motion,'matrix')
# Fixed set_pose/filter.update differential with explicit float32 vector math.
import struct
def f32(value): return struct.unpack('f',struct.pack('f',value))[0]
class FVec(object):
 def __init__(self,*args):
  value=args[0] if len(args)==1 else args
  if isinstance(value,FVec):value=(value.x,value.y,value.z)
  self.x,self.y,self.z=map(f32,value)
 def __sub__(self,other):return FVec(f32(self.x-other.x),f32(self.y-other.y),f32(self.z-other.z))
 def scale(self,value):return FVec(f32(self.x*f32(value)),f32(self.y*f32(value)),f32(self.z*f32(value)))
 @property
 def length(self):return f32(math.sqrt(f32(f32(f32(self.x*self.x)+f32(self.y*self.y))+f32(self.z*self.z))))
vmath=O();vmath.Vector3=FVec
velocity_ref=nr.RemoteVehicle.__new__(nr.RemoteVehicle);velocity_ref._math=vmath
velocity_ref.position=FVec((5.1,0.,0.));velocity_ref._last_pose_time=2.01;velocity_ref.yaw=velocity_ref.pitch=velocity_ref.roll=0.
velocity_ref._update_matrix=lambda:None;velocity_ref._retarget_render_pose=lambda *args:None
velocity_ref.filter=nr._RemoteFilter(vmath,velocity_ref.position,None);velocity_ref.appearance=O()
for index,(time,position,active) in enumerate(((2.05,(5.1001,-1.345678,2.312345),True),(2.1,(5.1002,-1.345678,2.312345),True),(2.2,(6.1,-1.345678,2.312345),False))):
 state.update(x=position[0],y=position[1],z=position[2],speed=1. if active else 0.);battle._bots._sample_time_us=7+index
 velocity_ref.set_pose(FVec(position),(state['roll'],state['pitch'],state['yaw']),now=time)
 if not active:velocity_ref.settle_motion(now=time)
 errors,applied=native.run(battle,[state],time,1);assert not errors and applied,errors
 expected=(velocity_ref.filter.velocity.x,velocity_ref.filter.velocity.y,velocity_ref.filter.velocity.z)
 assert remote_filter.velocity==expected,(remote_filter.velocity,expected)
 assert abs(remote_filter.speed-velocity_ref.filter.speed)<1.e-7,(remote_filter.speed,velocity_ref.filter.speed)
 assert appearance.gear==velocity_ref.appearance.gear
# A replacement record fences all prior signatures and pose timing.
old_record=record
record={'kind':'bot','network_id':1,'engine_id':10,'ready':True}
battle._records['bot:1']=record
writes=battle._authority_pose_writes
errors,applied=native.run(battle,[state],3.0,1)
assert not errors and applied and battle._authority_pose_writes==writes+1
assert record['_authority_presentation_lifecycle']!=old_record['_authority_presentation_lifecycle']
assert battle._bot_yaw_rates[1]==0.
# A partial matrix failure invalidates only the pose cache and retries later.
motion.matrix=[];state.update(x=7.,aim_yaw=1.,speed=1.);battle._bots._sample_time_us=20
writes=battle._authority_pose_writes;aim_writes=battle._authority_aim_writes
errors,applied=native.run(battle,[state],3.1,0)
assert errors and applied and '_authority_pose_signature' not in record
assert battle._authority_pose_writes==writes and battle._authority_aim_writes==aim_writes+1
assert record['projectile_collision_pose']['x']==7.
motion.matrix={};errors,applied=native.run(battle,[state],3.2,0)
assert not errors and applied and battle._authority_pose_writes==writes+1
print('native presentation fixture passed')
'''
class NativePresentationTest(unittest.TestCase):
 def test_worker_pose_lifecycle(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('fixed gameplay source is required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
  prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='bot-presentation-') as directory:
   folder=Path(directory)
   impl=(ROOT/'bigworld/stock_native_bot_presentation.cpp').read_text().replace('#include "stock_native_bot_services.hpp"','#include "stock_native_bot_presentation_stub.hpp"')
   (folder/'stock_native_bot_presentation_impl.cpp').write_text(impl)
   (folder/'test.py').write_text("import sys,types\nsys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')\ngui=types.ModuleType('gui');gui.__path__=[sys.path[0]+'/gui'];sys.modules['gui']=gui\nmods=types.ModuleType('gui.mods');mods.__path__=[sys.path[0]+'/gui/mods'];sys.modules['gui.mods']=mods\n"+FIXTURE)
   cmd=['c++','-std=c++17','-Wno-register','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'tests'),str(ROOT/'tests/stock_native_bot_presentation_host.cpp'),str(ROOT/'bigworld/stock_native_bot_resource_lifecycle.cpp'),'-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD','-o',str(folder/'presentation_test.so')]
   result=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
   result=subprocess.run([py27,'-B',str(folder/'test.py'),source],cwd=str(folder),capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
if __name__=='__main__':unittest.main()
