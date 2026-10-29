"""Input/aim laws against the exact sender and #1513 packed-angle producer."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''from __future__ import print_function
import ast,json,math,random,sys,types
import offline_client_controls_test as native
path=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
tree=ast.parse(open(path,'rb').read());names=set(('_number','_xyz','_movement_throttle','_LANInputSender','_CRUISE_MODE_THROTTLE','_MOVEMENT_FORWARD','_MOVEMENT_BACKWARD','_MOVEMENT_CRUISE_CONTROL50','_MOVEMENT_CRUISE_CONTROL25'))
selected=[n for n in tree.body if isinstance(n,(ast.ClassDef,ast.FunctionDef)) and n.name in names or isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id in names for t in n.targets)]
env={'math':math};exec(compile(ast.Module(selected),path,'exec'),env)
class Owner(object):
 def local_pose(self):return self.position,self.yaw
 def local_stabilised_position(self):return self.stabilised
owner=Owner();owner.position=(1.,2.,3.);owner.stabilised=(1.1,2.2,3.3);owner.yaw=0.;owner._local_pitch=0.;owner._local_siege_aim_pitch=0.
sender=env['_LANInputSender'](owner)
for flags in range(1024):assert native.movement(flags)==env['_movement_throttle'](flags),flags
for mode in range(-10,11):assert native.cruise(mode)==env['_CRUISE_MODE_THROTTLE'].get(mode,0.),mode
random.seed(1513)
for index in range(600):
 owner.position=tuple(random.uniform(-1000.,1000.) for x in range(3));owner.stabilised=tuple(x+random.uniform(-2.,2.) for x in owner.position);owner.yaw=random.uniform(-math.pi,math.pi);owner._local_pitch=random.uniform(-2.,2.);owner._local_siege_aim_pitch=random.uniform(-.5,.5)
 target=tuple(random.uniform(-1000.,1000.) for x in range(3));relative=bool(index%2)
 if index%37==0:target=(0.,0.,0.)
 sender._track(target,relative);expected=(sender.aim_yaw,sender.gun_pitch,sender.aim_pitch,sender.aim_point)
 actual=native.track(target,relative,owner.position,owner.stabilised);assert expected==actual,(expected,actual)
 turret=random.uniform(-math.pi,math.pi);gun=random.uniform(-1.,1.);sender.align_aim(turret,gun)
 assert native.align(owner.yaw,owner._local_pitch,owner._local_siege_aim_pitch,turret,gun)==(sender.aim_yaw,sender.gun_pitch,sender.aim_pitch,sender.aim_point)
# These arithmetic expressions are the audited #1513 helper bytecode. Running
# the oracle on CPython 2.7 keeps its round semantics at quantization edges.
def packed(yaw,pitch,limits):
 relative=(yaw+math.pi)%(2.*math.pi)-math.pi
 yawcode=int(round(1024*(relative+math.pi)/(math.pi*2.)))&1023
 low,high=limits;t=0. if high==low else (pitch-low)/(high-low)
 return yawcode<<6 | int(round(63*max(0.,min(t,1.))))&63
for i in range(4096):
 yaw=(i-1024)*math.pi/1024.;pitch=random.uniform(-1.,1.);limits=(-.3,.7) if i%13 else (.2,.2)
 assert native.packed(yaw,pitch,limits)==packed(yaw,pitch,limits),(yaw,pitch,limits)
print(json.dumps(dict(movement_flags=1024,aim_samples=600,packed_angles=4096)))
'''
class NativeClientInputControlsTests(unittest.TestCase):
 def test_input_and_aim_match_pinned_owner(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('Fixed v0.10.1 source input is required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
  prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='native-client-controls-') as folder:
   binary=Path(folder)/'offline_client_controls_test.so'
   result=subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_client_input_controls.cpp'),str(ROOT/'tests/stock_native_client_input_controls_host.cpp'),'-o',str(binary)],capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stderr)
   script=Path(folder)/'fixture.py';script.write_text(FIXTURE)
   result=subprocess.run([py27,'-B',str(script),source],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
   self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
if __name__=='__main__':unittest.main()
