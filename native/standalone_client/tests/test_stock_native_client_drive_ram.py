"""Player RAM episodes, two-attempt geometry proof and immutable receipts."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''from __future__ import print_function
import ast,copy,cStringIO,json,math,random,struct,sys,types
import offline_player_ram_test as native
assert native.geometry_adapter()
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=m
module=types.ModuleType('gui.mods.offline_lan_0922.native_input_diagnostics');module.write_json_record=lambda *args:None;sys.modules[module.__name__]=module
from gui.mods.offline_lan_0922 import tank_collision as tc,vehicle_physics as vp,effective_params
path=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
tree=ast.parse(open(path,'rb').read());wanted=set(('_validated_ram_contact_normal','_native_ram_vehicle_armor','_ram_plate_pair_from_probe','_ram_plate_pair_from_probe_impl','_ram_corner_probe_direction','_queue_ram_contact_proof','_retry_native_ram_contact_proof','_retry_native_ram_contact_proofs','_ram_contact_xz_samples','_poll_local_ram_contact_episodes'))
methods=[n for cls in tree.body if isinstance(cls,ast.ClassDef) and cls.name=='BattleRuntime' for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in wanted]
def f32(n):return struct.unpack('f',struct.pack('f',n))[0]
class Bag(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Vector(object):
 def __init__(self,v):self.x,self.y,self.z=map(f32,v)
 def __getitem__(self,i):return (self.x,self.y,self.z)[i]
class Matrix(object):
 def __init__(self):self.translation=Vector((0.,0.,0.));self.yaw=self.pitch=self.roll=0.
 def setRotateYPR(self,ypr):self.yaw,self.pitch,self.roll=ypr
rays=[]
def collide(vehicle,matrix,start,end,unused_math,chassis_matrix=None):
 rays.append([vehicle.id,[start.x,start.y,start.z],[end.x,end.y,end.z]])
 spec=vehicle.spec;y=start.y-matrix.translation.y
 if not vehicle.available or spec['mode']==1 or spec['mode'] in (2,3) and not spec['span'][0]<=y<=spec['span'][1]:return []
 shape=vehicle.typeDescriptor.shape;reach=math.sqrt(shape[0]*shape[0]+shape[1]*shape[1])+1.
 first=Bag(dist=reach+.02,matInfo=Bag(armor=5.,vehicleDamageFactor=0.,extra=Bag(name='leftTrackHealth' if spec['mode']==3 else 'screen')))
 structural=Bag(dist=reach+.1,matInfo=Bag(armor=spec['armor'],vehicleDamageFactor=1.,extra=Bag(name='hull')))
 return [first,structural]
env=dict(math=math,sys=Bag(stdout=cStringIO.StringIO()),tank_collision=tc,vehicle_physics=vp,collide_vehicle_at_matrix=collide,_number=lambda v,default=0.:default if v is None else float(v),_xyz=lambda v:tuple(v[i] for i in range(3)))
exec(compile(ast.fix_missing_locations(ast.Module([ast.ClassDef(name='Harness',bases=[ast.Name(id='object',ctx=ast.Load())],body=methods,decorator_list=[])])),path,'exec'),env)
class Oracle(env['Harness']):
 _ram_obb_overlap_polygon=staticmethod(tc.obb_overlap_polygon);_ram_obb_overlap_point=staticmethod(tc.obb_overlap_point)
 def __init__(self,specs):
  self.client=Bag(round_id=7);self._native_ram_contact_proofs={};self._native_ram_event_seq=0;self._local_ram_episode_contacts=frozenset();self._native_ram_contact_failures=set();self._local_ram_seq=0;self._local_ram_receipts={};self._local_ram_receipt=None;self._local_pitch=self._local_roll=0.;self._runtime=Bag(math=Bag(Matrix=Matrix));self.specs=dict((s['id'],s) for s in specs);self.vehicles={};self.records={};self.captures={};self.now=0.;self._vector=Vector
 def vehicle(self,id,shape):
  if id not in self.vehicles:
   spec=self.specs[id];self.vehicles[id]=Bag(id=id,spec=spec,available=True,typeDescriptor=Bag(shape=shape,hull=Bag(hitTester=Bag(bbox=((-1.,spec['span'][0],-1.),(1.,spec['span'][1],1.)))),chassis=Bag(hullPosition=(0.,0.,0.))))
  return self.vehicles[id]
 def _clock(self):return 0.
 def _estimated_motion_time_us(self,now):return self.now
 def _collision_shape(self,descriptor):return descriptor.shape
 def _ram_pose_matrix(self,position,yaw,pitch=0.,roll=0.):
  m=Matrix();m.setRotateYPR((yaw,pitch,roll));m.translation=Vector(position);return m
 def _ram_bot_revision_at(self,id,stamp):return 3 if stamp is not None else None
 def _native_ram_contact_plate_pair(self,proof):
  for vehicle in (proof['local_vehicle'],proof['bot_vehicle']):
   id=vehicle.id;self.captures[id]=self.captures.get(id,0)+1;vehicle.available=self.captures[id]>vehicle.spec['ready_after']
  return self._ram_plate_pair_from_probe(proof,self._native_ram_vehicle_armor)
 def poll(self,own,peers,motion):
  self.now=motion;self._local_pitch=own.get('local_pitch',own['pitch']);self._local_roll=own.get('local_roll',own['roll']);values=[]
  for peer in peers:
   value=copy.deepcopy(peer);id=value['network_id'];record=self.records.setdefault(id,{})
   record.update(network_id=id,presentation_time_us=value.get('presentation_time_us'),presentation_bracket=value.get('presentation_bracket'))
   value['_record']=record;value['_vehicle']=self.vehicle(id,value['shape']);values.append(value)
  entity=self.vehicle(1,own['shape']);return self._poll_local_ram_contact_episodes(entity,own,values)
 def inspect(self):return dict(receipts=[self._local_ram_receipts[k] for k in sorted(self._local_ram_receipts)],active=sorted(self._local_ram_episode_contacts),pending=len(self._native_ram_contact_proofs),rays=copy.deepcopy(rays))
def same(a,b):
 if isinstance(a,(float,int,long)) and isinstance(b,(float,int,long)):
  assert abs(a-b)<=max(1.e-9,abs(float(a))*1.e-12),(a,b)
 elif isinstance(a,dict):
  assert set(a)==set(b),(a,b)
  for key in a:same(a[key],b[key])
 elif isinstance(a,(list,tuple)):
  assert len(a)==len(b),(a,b)
  for x,y in zip(a,b):same(x,y)
 else:assert a==b,(a,b)
shape=(1.5,3.,-.5,2.)
def body(id=2,**kw):
 raw=dict(id=100+id,network_id=id,kind='bot',alive=True,team=2,x=0.,y=0.,z=5.,yaw=0.,pitch=0.,roll=0.,vx=0.,vy=0.,vz=-1.,shape=shape,presentation_time_us=1000000,presentation_bracket=[2,900000,3,1100000],revision=3)
 raw.update(kw);return raw
own=body(1,kind='player',team=1,z=0.,vz=5.,ram_profile=dict(spall_coefficient=1.2,ramming_bonus=.075))
def specs(mode=0,ready=0):return [dict(id=1,armor=90.,mode=mode,span=(.1,.7),ready_after=ready),dict(id=2,armor=120.,mode=mode,span=(.2,.8),ready_after=ready)]
def run(rows,settings=None,ack=False):
 del rays[:];settings=settings or specs();oracle=Oracle(settings);handle=native.create(settings)
 for index,(a,peers,motion,retry) in enumerate(rows):
  before=len(rays)
  if retry:oracle._retry_native_ram_contact_proofs();native.retry(handle)
  expected=oracle.poll(copy.deepcopy(a),peers,motion)
  actual=native.poll(handle,a,peers,a.get('local_pitch',a['pitch']),a.get('local_roll',a['roll']),motion,1.2,.075)
  assert expected==actual,(index,expected,actual,a,peers)
  expected_state=oracle.inspect();actual_state=native.inspect(handle)
  if any(s['ready_after'] for s in settings):expected_state.pop('rays');actual_state.pop('rays')
  try:same(expected_state,actual_state)
  except AssertionError:print('RAM mismatch',index,a,peers);raise
 return oracle,handle
# Sustained pressure, interpolation gap, separation, re-entry and peer filters.
sequence=[]
for index in range(50):
 peer=body(z=5. if index<15 or index>=30 else 9.,vz=-1. if index<20 or index>=30 else 8.)
 sequence.append((own,[peer],1000000+index*20000,True))
run(sequence)
random.seed(1513)
for index in range(360):
 a=copy.deepcopy(own);a.update(x=random.uniform(-.6,.6),y=random.uniform(-.2,.2),yaw=random.uniform(-.3,.3),pitch=random.uniform(-.2,.2),roll=random.uniform(-.15,.15),vz=random.uniform(-3.,8.),vx=random.uniform(-2.,2.),vy=random.uniform(-2.,2.))
 peer=body(x=random.uniform(-2.,2.),y=random.uniform(-.2,.2),z=random.uniform(3.,7.),yaw=random.uniform(-.3,.3),pitch=random.uniform(-.2,.2),roll=random.uniform(-.15,.15),vx=random.uniform(-2.,2.),vz=random.uniform(-3.,3.),vy=random.uniform(-2.,2.))
 if index%13==0:peer['team']=1
 if index%17==0:peer['alive']=False
 if index%19==0:peer['kind']='player'
 run([(a,[peer],1000000,False)],specs(mode=index%4))
# One next-frame retry, failed pair release, structural band sampling, tracks,
# missing history, no motion clock, epoch retirement and durable cap/ACK.
for mode in (0,1,2,3):
 for ready in (0,1,3):run([(own,[body()],1000000,False),(own,[body()],1020000,True),(own,[body()],1040000,True)],specs(mode,ready))
for stamp,bracket in ((None,None),(1000000,None)):
 run([(own,[body(presentation_time_us=stamp,presentation_bracket=bracket)],1000000,False),(own,[body(presentation_time_us=stamp,presentation_bracket=bracket)],1020000,True)])
run([(own,[body()],None,False)])
o,h=run([(own,[body()],1000000,False)],specs(0,1));native.retire(h);native.retry(h);assert native.inspect(h)['pending']==0 and native.inspect(h)['active']==[]
# Exact 21-field worker witness restores the original pair rather than the
# later pushed presentation; preserve the original local attitude override.
witness=[1,7,1000000,0.,0.,-1.,0.,0.,5.,0.,.12,-.1,0.,0.,0.,0.,.2,.1,0.,0.,5.]
witness_peer=body(z=100.,_ram_motion=witness,bot_ram_motion_seq=7)
for mode in (0,1,2,3):run([(own,[witness_peer],1000000,False),(own,[witness_peer],1020000,True),(own,[witness_peer],1040000,True)],specs(mode))
# Receipt pressure never discards a frozen proof or advances its sequence.
settings=specs()+[dict(specs()[1],id=id) for id in range(3,22)]
peers=[body(id) for id in range(2,22)]
o,h=run([(own,peers,1000000,False)],settings)
assert len(native.inspect(h)['receipts'])==16 and native.inspect(h)['pending']==4
native.retry(h);o._retry_native_ram_contact_proofs()
same(o.inspect(),native.inspect(h))
assert native.ack(h,8)
for key in list(o._local_ram_receipts):
 if key<=8:del o._local_ram_receipts[key]
native.retry(h);o._retry_native_ram_contact_proofs();same(o.inspect(),native.inspect(h))
assert native.inspect(h)['pending']==0 and len(native.inspect(h)['receipts'])==12
# A stale descriptor, replacement actor or engine entity invalidates only
# its own queued operation, including the local-entity replacement case.
for id in (1,2):
 for kind in (0,1,2):
  o,h=run([(own,[body()],1000000,False)],specs(0,1));native.invalidate(h,id,kind);native.retry(h)
  assert native.inspect(h)['pending']==0 and native.inspect(h)['active']==[]
# Discrete native-collision observations freeze the same old callback source:
# current local attitude, remote matrix yaw with zero pitch/roll, no worker seq.
for index in range(80):
 del rays[:];settings=specs(index%4);o=Oracle(settings);h=native.create(settings)
 a=copy.deepcopy(own);a.update(pitch=random.uniform(-4.,4.),roll=random.uniform(-4.,4.),yaw=random.uniform(-7.,7.))
 peer=body(pitch=.2,roll=-.2);vehicle=o.vehicle(2,shape);vehicle.position=Vector((peer['x'],peer['y'],peer['z']));vehicle.matrix=Bag(yaw=peer['yaw'])
 o._local_position=(a['x'],a['y'],a['z']);o._local_yaw=a['yaw'];o._local_pitch=a['pitch'];o._local_roll=a['roll'];o._ram_profile=lambda descriptor,local=False:dict(spall_coefficient=1.2,ramming_bonus=.075)
 record=dict(network_id=2,presentation_time_us=1000000,presentation_bracket=[2,900000,3,1100000])
 expected=o._queue_ram_contact_proof(record,o.vehicle(1,shape),vehicle,Vector((0.,.75,2.5)),(a['vx'],a['vy'],a['vz']),(peer['vx'],peer['vy'],peer['vz']),1000000)
 if expected or o._native_ram_contact_proofs:o._local_ram_episode_contacts=frozenset([2])
 actual=native.observe(h,a,peer,(0.,.75,2.5),1000000)
 assert expected==actual
 same(o.inspect(),native.inspect(h))
# The real profile law uses levels below 100 and the surviving physical driver,
# including a commander with a secondary driver affiliation.
for index in range(200):
 members=[dict(instance='driver',roles=['driver'],skills=[dict(name='driver_rammingmaster',level=index/2.,active=True,enabled=True)]),dict(instance='commander',roles=['commander','driver'],skills=[dict(name='driver_rammingmaster',level=75.,active=True,enabled=bool(index%2))])]
 effective=dict(crew=dict(members=members),ramming=dict(spall_coefficient=1.1))
 for ko in ([],['driver'],['commander'],['driver','commander']):
  same(native.profile(effective,ko),(1.1,effective_params.living_skill_level(effective,'driver_rammingmaster',ko)*.0015))
print(json.dumps(dict(random_episodes=360,sustained_frames=50,retry_scenarios=12,crew_profiles=800,native_callbacks=80,durable_cap=16,lifecycle_fences=6,worker_witness_scenarios=4)))
'''
class NativePlayerRamTests(unittest.TestCase):
 def test_original_player_episodes_plate_search_and_receipts(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('Fixed v0.10.1 source input is required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
  prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='native-player-ram-') as directory:
   folder=Path(directory);native=Path(source)/'native'
   law=(native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
   (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
   sources=['stock_native_client_drive_ram_geometry.cpp','stock_native_client_drive_ram.cpp','stock_native_client_drive_publication.cpp','stock_native_client_state.cpp']
   cmd=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DWOT_CLIENT_REPLICA_HOST_BUILD','-DWOT_CLIENT_RAM_GEOMETRY_HOST_BUILD','-include',str(ROOT/'tests/stock_native_client_drive_ram_geometry_stub.hpp'),'-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+directory,'-I'+str(ROOT/'bigworld'),'-I'+str(native)]+[str(ROOT/'bigworld'/s) for s in sources]+[str(ROOT/'tests/stock_native_client_drive_ram_host.cpp'),'-o',str(folder/'offline_player_ram_test.so')]
   result=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
   fixture=folder/'fixture.py';fixture.write_text(FIXTURE)
   result=subprocess.run([py27,'-B',str(fixture),source],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
   self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
if __name__=='__main__':unittest.main()
