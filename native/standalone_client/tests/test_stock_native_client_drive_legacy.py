"""Whole hydraulic/excluded-trial player vertical/support path differential."""
import ast
import ctypes
import math
from pathlib import Path
import random
import struct
import subprocess
import types
import unittest
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE

class PlayerLegacyDifferential(ContactDifferential):
 @classmethod
 def setUpClass(cls):
  super().setUpClass();folder=Path(cls.folder.name);native=Path(SOURCE)/'native';big=ROOT/'bigworld';binary=folder/'legacy.so'
  command=['c++','-std=c++17','-O0','-shared','-fPIC','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(folder),'-I'+str(big),'-I'+str(native)]
  command +=[str(big/(name+'.cpp'))for name in('stock_native_client_drive_legacy','stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin')]
  command +=[str(native/(name+'.cpp'))for name in('offline_destructible_geometry','world_stage')]+[str(ROOT/'tests/stock_native_client_drive_legacy_host.cpp'),'-o',str(binary)]
  subprocess.run(command,check=True,capture_output=True,text=True);lib=ctypes.CDLL(str(binary));cls.legacy=lib.player_legacy_fixture;ptr=ctypes.POINTER(ctypes.c_double);cls.legacy.argtypes=[ptr,ptr];cls.legacy.restype=ctypes.c_int
  tree=ast.parse((Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read_text());wanted=set('_support_column _terrain_support _sample_ground_plane _ground_pitch _commit_ground_plane _update_vertical_motion_legacy'.split());methods=[node for outer in tree.body if isinstance(outer,ast.ClassDef)for node in outer.body if isinstance(node,ast.FunctionDef)and node.name in wanted]
  for method in methods:method.decorator_list=[]
  module=ast.Module(body=[ast.ClassDef(name='Original',bases=[],keywords=[],body=methods,decorator_list=[])],type_ignores=[]);ast.fix_missing_locations(module);ns=dict(math=math,vehicle_physics=cls.vehicle_physics,tank_collision=cls.tank_collision,GROUND_PLANE_EPSILON=.35,_field=lambda obj,key,default=None:getattr(obj,key,default),_xyz=lambda v:tuple(v));exec(compile(module,'original-player-legacy','exec'),ns);cls.Original=ns['Original']
 def test_full_original_player_support(self):
  f32=lambda v:struct.unpack('f',struct.pack('f',v))[0]
  class Vector:
   def __init__(self,v):self.x,self.y,self.z=map(f32,v)
   def __iter__(self):return iter((self.x,self.y,self.z))
  rng=random.Random(1513144);shape=(1.5,3.,-.5,2.);previous=self.tank_collision.chassis_shape;self.tank_collision.chassis_shape=lambda descriptor:shape
  try:
   for i in range(320):
    scenario=i%8;position=(rng.uniform(-2.,2.),rng.choice([0.,.3,1.,3.,20.]),rng.uniform(-2.,2.));yaw=rng.uniform(-math.pi,math.pi);pitch=rng.uniform(-.7,.7);roll=rng.uniform(-.3,.3);speed=rng.choice([-5.,0.,10.]);vertical=rng.choice([-15.,0.,5.]);last_pitch=rng.uniform(-1.,1.);air=i%3==0;armed=i%7!=0;dt=rng.choice([0.,.016,.04,.1]);tick=(position[0]-.1,position[1]-.05,position[2]+.1)
    def collide(start,end,filter):
     x,z=start.x,start.z
     if scenario==7 or(scenario==5 and z>0.):return None
     y=.25*x-.12*z if scenario==1 else(-2. if abs(x)<.6 and abs(z)<2. else 0.)if scenario==2 else(.9 if x>0. else 0.)if scenario==3 else(0. if abs(x)>.8 or abs(z)>2. else -4.)if scenario==6 else 0.
     levels=([(6.,.3)]if scenario==4 else[])+[(f32(y),1.)]
     for value,up in levels:
      if end.y<=value<=start.y:return Vector((x,value,z)),Vector((0.,up,0.))
     return None
    h=self.Original();h._vector=Vector;h._ground_filter=lambda *args:None;h._collide_down=collide;h._local_pitch=pitch;h._local_roll=roll;h._local_speed=speed;h._local_vertical_speed=vertical;h._local_last_pitch=last_pitch;h._local_airborne=air;h._local_fall_armed=armed;h._local_support_tick_pose=tick;h._local_surface_up_cosine=None;h._local_downhill=(0.,0.,0.);h._local_slope_tangent=0.;h._local_ground_plane=None;h._local_legacy_support_sample=None;h._local_support_rise_blocked=False;impacts=[]
    h._apply_landing_impact=lambda entity,speed:impacts.append((h._landing_contact_pose,speed));h._ground_y=lambda x,z,hint:None
    descriptor=types.SimpleNamespace(hull=types.SimpleNamespace(hitTester=types.SimpleNamespace(bbox=((-1.5,-.5,-3.),(1.5,2.,3.)))));entity=types.SimpleNamespace(typeDescriptor=descriptor)
    expected=h._update_vertical_motion_legacy(entity,position,yaw,dt)
    if not h._local_airborne and not h._local_support_rise_blocked:h._ground_pitch(expected,yaw,descriptor)
    data=[scenario]+list(position)+[yaw,pitch,roll,speed,vertical,last_pitch,air,armed,dt]+list(tick)+[self.vehicle_physics.GRAVITY];raw=(ctypes.c_double*len(data))(*data);out=(ctypes.c_double*64)();count=self.legacy(raw,out);self.assertGreater(count,0)
    target=list(expected)+[h._local_vertical_speed,h._local_airborne,h._local_fall_armed,h._local_pitch,h._local_roll,h._local_downhill[0],h._local_downhill[2],h._local_slope_tangent,h._local_surface_up_cosine if h._local_surface_up_cosine is not None else-99.,h._local_ground_plane is not None,h._local_support_rise_blocked,len(impacts)]
    for p,v in impacts:target+=list(p)+[v]
    self.assertEqual(count,len(target))
    for j,(a,b)in enumerate(zip(target,out)):self.assertAlmostEqual(a,b,places=10,msg=(i,j,data,target,list(out)[:count]))
  finally:self.tank_collision.chassis_shape=previous
if __name__=='__main__':unittest.main()
