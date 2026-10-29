"""Exact local camera, tracks, hydraulic pitch and native visual producer."""
import ast
import ctypes
import math
import os
from pathlib import Path
import random
import subprocess
import unittest
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE

class PlayerPresentationDifferential(ContactDifferential):
 @classmethod
 def setUpClass(cls):
  super().setUpClass();folder=Path(cls.folder.name);native=Path(SOURCE)/'native';big=ROOT/'bigworld';binary=folder/'presentation.so'
  subprocess.run(['c++','-std=c++17','-O0','-shared','-fPIC','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(folder),'-I'+str(big),'-I'+str(native),str(big/'stock_native_client_drive_runtime.cpp'),str(big/'stock_native_bot_motion.cpp'),str(big/'stock_native_client_drive_presentation.cpp'),str(ROOT/'tests/stock_native_client_drive_presentation_host.cpp'),'-o',str(binary)],check=True,capture_output=True,text=True)
  lib=ctypes.CDLL(str(binary));cls.camera=lib.player_camera_fixture;ptr=ctypes.POINTER(ctypes.c_double);cls.camera.argtypes=[ctypes.c_double,ptr,ctypes.c_uint,ptr];cls.camera.restype=ctypes.c_int;cls.visual=lib.player_visual_laws;cls.visual.argtypes=[ptr,ptr];cls.visual.restype=ctypes.c_int
  base=Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922';tree=ast.parse((base/'battle_runtime.py').read_text());wanted=set('_local_motion_pose_at _local_camera_motion _local_engine_mode_value _local_acceleration_swing_parameters'.split());methods=[node for outer in tree.body if isinstance(outer,ast.ClassDef) for node in outer.body if isinstance(node,ast.FunctionDef) and node.name in wanted]
  for method in methods:method.decorator_list=[]
  node=ast.ClassDef(name='Original',bases=[],keywords=[],body=methods,decorator_list=[]);module=ast.Module(body=[node],type_ignores=[]);ast.fix_missing_locations(module)
  ns=dict(math=math,_number=lambda v:float(v),ENGINE_MODE_OFF=0,ENGINE_MODE_IDLE=1,ENGINE_MODE_RUNNING=2,_MOVEMENT_FORWARD=1,_MOVEMENT_BACKWARD=2,_MOVEMENT_ROTATE_LEFT=4,_MOVEMENT_ROTATE_RIGHT=8,LOCAL_ACCEL_SWING_MOVE_SECONDS=2.,LOCAL_ACCEL_SWING_ROTATE_SECONDS=1.)
  exec(compile(module,'original-presentation-methods','exec'),ns);cls.Original=ns['Original'];cls.hull={};exec(compile((base/'hull_aiming.py').read_text(),'original-hull-aiming','exec'),cls.hull)
 def test_camera_history(self):
  rng=random.Random(51513);h=self.Original();h._camera_motion_window=lambda:.1;h._local_motion_history=[];h._local_motion_clock=0.;velocity=(0.,0.,0.);inputs=[];expected=[]
  position=[10.,.5,-2.]
  for i in range(1400):
   dt=rng.choice([0.,.005,.016,.033,.08,.17,.4]);reset=2 if i==0 else 1 if i%311==0 else 0
   for axis in range(3):position[axis]+=rng.uniform(-1.,1.)*dt
   if reset:h._local_motion_clock=0.;h._local_motion_history=[(0.,tuple(position))]if reset>1 else [];velocity=(0.,0.,0.)
   if dt>0.:velocity,acceleration=h._local_camera_motion(tuple(position),dt,velocity)
   else:acceleration=(0.,0.,0.)
   inputs.extend([dt]+position+[reset]);expected.extend(velocity+acceleration)
  raw=(ctypes.c_double*len(inputs))(*inputs);out=(ctypes.c_double*len(expected))();self.assertEqual(self.camera(.1,raw,1400,out),1)
  for a,b in zip(expected,out):self.assertAlmostEqual(a,b,places=10)
 def test_visual_laws(self):
  rng=random.Random(1513);h=self.Original();h._sender=type('Sender',(),{})();h._replay_mode=False
  for i in range(640):
   around=rng.choice([0,1]);center=rng.uniform(.7,2.);cap=rng.uniform(5.,30.);speed=rng.uniform(-20.,30.);turn=rng.uniform(-2.,2.);air=rng.choice([0,1]);throttle=rng.choice([-1.,0.,1.]);alive=rng.choice([0,1]);forward=rng.choice([-1.,0.,1.]);effective=rng.choice([-1.,0.,1.]);previous=rng.randrange(16)
   available=rng.choice([0,1]);enabled=rng.choice([0,1]);minimum=-.2;maximum=.18;rate=.08;center_z=rng.uniform(-2.,2.);gun_lo=-.1;gun_hi=.08;current=rng.uniform(-.2,.18);dt=rng.choice([0.,.016,.08,.2]);active=rng.choice([0,1]);has_flat=rng.choice([0,1]);flat=rng.uniform(-.8,.8);world=rng.uniform(-4.,4.);terrain=rng.uniform(-1.,1.)
   values=[around,center,cap,speed,turn,air,throttle,alive,forward,effective,previous,available,enabled,minimum,maximum,rate,center_z,gun_lo,gun_hi,current,dt,active,has_flat,flat,world,terrain];raw=(ctypes.c_double*len(values))(*values);out=(ctypes.c_double*11)();self.assertEqual(self.visual(raw,out),1)
   tracks=self.vehicle_physics.track_scroll(dict(rotationIsAroundCenter=bool(around),trackCenter=center,speedFwd=cap),speed,turn,bool(air),throttle);tracks=tuple(max(-15.,min(30.,v))for v in tracks)
   h._sender.forward=forward;h._local_drive_turn=effective;h._local_speed=speed;mode=h._local_engine_mode_value(alive);swing=self.Original._local_acceleration_swing_parameters(mode[1],previous)
   correction=0.
   if available and not(active and not enabled):
    desired=0.
    if active:desired=self.hull['minimal_correction'](flat,gun_lo,gun_hi,minimum,maximum)[0]if has_flat else self.hull['world_target_correction'](world,terrain,gun_lo,gun_hi,minimum,maximum)[0]
    correction=self.hull['slew'](current,desired,rate,dt)
   translation=self.hull['correction_translation'](correction,center_z);expected=tracks+mode+(bool(swing),swing[0]if swing else 0.,swing[1]if swing else 0.,correction)+translation
   for j,(a,b)in enumerate(zip(expected,out)):self.assertAlmostEqual(a,b,places=12,msg=(i,j,values))
 def test_native_track_producer(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  subprocess.run(['c++','-std=c++17','-Wno-register','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_presentation_python_host.cpp'),'-o',str(folder/'client_drive_visual_test.so')],check=True,capture_output=True,text=True)
  fixture=r'''
import client_drive_visual_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
 def changeEngineMode(self,*args):raise AssertionError('Python business mode callback')
 def updateTracksScroll(self,*args):raise AssertionError('Python business scroll callback')
controller=O(setMode=native.capture_mode,setExternal=native.capture_scroll)
for i in range(100):
 appearance=O(detailedEngineState=O(),_CompoundAppearance__trackScrollCtl=controller if i%3 else None)
 entity=O(appearance=appearance);mode=(i%3,i%16);left=i*.125;right=-i*.25
 native.apply(entity,appearance,mode[0],mode[1],left,right)
 assert entity.engineMode==mode and appearance._CompoundAppearance__engineMode==mode
 assert appearance.detailedEngineState.mode==mode[0]
 assert(appearance._CompoundAppearance__leftTrackScroll,appearance._CompoundAppearance__rightTrackScroll)==(left,right)
 if i%3:assert native.captures[-2:]==[(mode,),(left,right)]
controller.setExternal=lambda *args:None
try:native.apply(entity,appearance,2,1,0.,0.)
except RuntimeError:pass
else:raise AssertionError('Python callback admitted as native controller')
'''
  # Ensure the final rejection exercises a live controller after the last loop.
  fixture=fixture.replace('controller.setExternal=lambda','appearance._CompoundAppearance__trackScrollCtl=controller\ncontroller.setExternal=lambda')
  result=subprocess.run([py27,'-B','-c',fixture],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
