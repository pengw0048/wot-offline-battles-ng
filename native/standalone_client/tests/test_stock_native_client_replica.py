"""Differential render-clock and native-owner tests against the pinned fork."""
import os
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import copy,math,random,sys,types,struct
source=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922'
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 module=types.ModuleType(name);module.__path__=[source];sys.modules[name]=module
from gui.mods.offline_lan_0922.snapshot_sync import SnapshotSync
import client_replica_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
def state(identifier,x=0.,alive=True):
 return dict(id=identifier,team=2,vehicle='ussr:T-34',x=x,y=0.,z=0.,yaw=0.,pitch=0.,roll=0.,aim_yaw=0.,gun_pitch=0.,health=100 if alive else 0,alive=alive,speed=2.)
def close(a,b,path='root'):
 if isinstance(a,(float,int,long)) and not isinstance(a,bool) and isinstance(b,(float,int,long)):
  assert abs(a-b)<1.e-7*max(1.,abs(a),abs(b)),(path,a,b)
 elif isinstance(a,dict):
  assert set(a)==set(b),(path,set(a),set(b))
  for key in a:close(a[key],b[key],path+'.'+str(key))
 elif isinstance(a,(list,tuple)):
  assert len(a)==len(b),(path,a,b)
  for i,(x,y) in enumerate(zip(a,b)):close(x,y,path+'['+str(i)+']')
 else:assert a==b,(path,a,b)
now=[0.]
a=SnapshotSync(1,clock=lambda:now[0]);b=SnapshotSync(1,clock=lambda:now[0]);owner=native.create();battle=O(_records={})
def ingress(message):
 a.snapshot(copy.deepcopy(message));b.snapshot(copy.deepcopy(message))
 for key,record in b._entities.items():
  if key not in battle._records:battle._records[key]=dict(kind=record['kind'],engine_id=10,ready=False,state={})
 native.ingest(owner,b,battle)
def frame(time):
 expected=a.advance(time);actual,errors=native.advance(owner,b,time)
 assert not errors,errors
 close(sorted(expected,key=lambda v:v['entity']),sorted(actual,key=lambda v:v['entity']))
 for key in a._entities:
  for field in ('current','presentation_time_us','presentation_delay_us','consumer_resume_pending','consumer_resume_sample_us','wreck_settled','presentation_bracket','motion_probe'):
   close(a._entities[key].get(field),b._entities[key].get(field),key+'.'+field)
random.seed(87)
# Authority cadence jitter, late producer gaps, pi crossing, pauses and stalls.
tick=0;sample_us=0
for frame_index in range(1200):
 now[0]+=random.choice((.008,.016,.025,.033,.05,.2)) if frame_index%113==0 else random.choice((.008,.016,.025))
 if frame_index%3==0:
  tick+=1;sample_us+=random.choice((33000,50000,66000,200000))
  angle=3.1+frame_index*.003
  bot=state(7,frame_index*.025);bot.update(yaw=(angle+math.pi)%(2*math.pi)-math.pi,aim_yaw=-angle,pitch=math.sin(angle)*.2,roll=math.cos(angle)*.1,gun_pitch=math.sin(angle)*.3)
  ingress(dict(round_id=1,server_tick=tick,bot_state_revision=tick,motion_time_us=sample_us,bot_state_time_us=sample_us,bots=[bot],players=[state(2,frame_index*.02)]))
 frame(now[0])
# A fast teleport resets every confirmed-history cursor without a return path.
now[0]+=.01;tick+=1;sample_us+=10000
ingress(dict(round_id=1,server_tick=tick,bot_state_revision=tick,motion_time_us=sample_us,bot_state_time_us=sample_us,bots=[state(7,200.)]))
for i in range(30):now[0]+=.016;frame(now[0])
# A dead hull converges and then produces no repeated update events.
now[0]+=.05;tick+=1;sample_us+=50000
ingress(dict(round_id=1,server_tick=tick,bot_state_revision=tick,motion_time_us=sample_us,bot_state_time_us=sample_us,bots=[state(7,201.,False)]))
for i in range(80):now[0]+=.016;frame(now[0])
# Round reset and plain remote-human prediction retain the original law.
a.manifest(dict(round_id=2));b.manifest(dict(round_id=2));battle._records={}
for i in range(120):
 now[0]+=.016
 if i%4==0:ingress(dict(round_id=2,server_tick=i,players=[state(2,i*.03)]))
 frame(now[0])
# A render frame must use its typed state without rescanning Python entities.
class NoScan(dict):
 def __iter__(self):raise AssertionError('render rescanned sync records')
 def items(self):raise AssertionError('render rescanned sync records')
 def iteritems(self):raise AssertionError('render rescanned sync records')
b._entities=NoScan(b._entities)
pending,errors=native.advance(owner,b,now[0]+.016);assert not errors,errors
native.close(owner);native.close(owner)
try:native.advance(owner,b,now[0])
except RuntimeError:pass
else:raise AssertionError('closed replica advanced')
# Actual ready entity path has no Python gameplay pose/aim/track callbacks.
class Trap(O):
 def __getattribute__(self,key):
  if key in ('set_pose','set_aim','settle_motion','update_tracks'):raise AssertionError('Python gameplay callback '+key)
  return object.__getattribute__(self,key)
class Filter(O):
 def setTracksSpeed(self,left,lc,right,rc):self.written=(left,lc,right,rc)
class Appearance(O):
 def changeEngineMode(self,mode,forced):self.engine_mode=mode
 def updateTracksScroll(self,left,right):self.scroll=(left,right)
entity=Trap(owned=True,_offlineNativeDrawVisible=True,typeDescriptor=O(gun=O(staticTurretYaw=None,staticPitch=None)))
f=Filter(movementInfo=object());appearance=Appearance(filter=f,flyingInfoProvider=O(isLeftSideFlying=False,isRightSideFlying=False));entity.filter=f;entity.appearance=appearance
motion=Trap(animation=None,position=(0.,0.,0.),velocity=(0.,0.,0.),acceleration=(0.,0.,0.),yaw=0.,pitch=0.,roll=0.,_last_pose_time=None,_authority_geometry=False,matrix={},provider={},_matrix_pose=None,aim=O(turretMatrix={},gunMatrix={}),_aim_relative_yaw=None,_aim_gun_pitch=None,_track_feed=None,_track_feed_hidden=False,track_mode=None,presentation_capabilities={})
# The real Python #1513 state is the float32 motion/aim differential oracle.
from gui.mods.offline_lan_0922.entities.native_remote_vehicle import _NativeRemoteState
def f32(v):return struct.unpack('f',struct.pack('f',v))[0]
class V(object):
 def __init__(self,*args):
  value=args[0] if len(args)==1 else args
  if isinstance(value,V):value=(value.x,value.y,value.z)
  self.x,self.y,self.z=map(f32,value)
 def __sub__(self,other):return V(f32(self.x-other.x),f32(self.y-other.y),f32(self.z-other.z))
 def scale(self,v):return V(f32(self.x*f32(v)),f32(self.y*f32(v)),f32(self.z*f32(v)))
class M(object):
 def __init__(self,*args):pass
 def setIdentity(self):pass
 def setRotateYPR(self,v):self.rotation=v
math_module=O(Vector3=V,Matrix=M)
compat=O(set_vehicle_pose_overlay=lambda *args:True)
reference=_NativeRemoteState(O(),math_module,compat,None,(0.,0.,0.),(0.,0.,0.),interpolate_motion=False)
reference.entity=O(typeDescriptor=entity.typeDescriptor)
record=dict(kind='bot',engine_id=10,ready=True,native_remote=True,state=dict(speed=2.,alive=True,health=100),track_params=dict(trackCenter=1.5,speedFwd=10.,rotationIsAroundCenter=True))
battle=O(_records={'bot:7':record},_remote_factory=O(_states={10:motion},_vehicles={10:entity}),_runtime=O(compatibility=O(_vehicle_property_overlays={id(entity):{}})))
sync=SnapshotSync(1,clock=lambda:now[0]);owner=native.create()
assert not native.manages(owner,record)
reference_last=None
for i in range(10):
 now[0]+=.05;moving=state(7,i*.1);moving.update(yaw=3.1+i*.05,aim_yaw=3.2+i*.05,pitch=i*.005,roll=math.sin(i)*.01,gun_pitch=.03);sync.snapshot(dict(round_id=3,server_tick=i,bot_state_revision=i,motion_time_us=i*50000,bot_state_time_us=i*50000,bots=[moving]));native.ingest(owner,sync,battle);pending,errors=native.advance(owner,sync,now[0]);assert not pending and not errors,(pending,errors)
 pose=record['presented_pose']
 if pose!=reference_last:
  reference.set_pose((pose['x'],pose['y'],pose['z']),(pose['roll'],pose['pitch'],pose['yaw']),now=now[0]);reference_last=dict(pose)
 reference.set_aim(pose['yaw'],pose['aim_yaw'],pose['gun_pitch'])
 close(motion.velocity,(reference.velocity.x,reference.velocity.y,reference.velocity.z),'velocity')
 close(motion.acceleration,(reference.acceleration.x,reference.acceleration.y,reference.acceleration.z),'acceleration')
 close(motion.speed,reference.speed,'speed');close(motion.turn_speed,reference.turn_speed,'turn_speed')
 close(motion._render_pose,reference._render_pose,'render_pose')
 close(motion.aim.turretMatrix['rotation'],reference.aim.turretMatrix.rotation,'turret')
 close(motion.aim.gunMatrix['rotation'],reference.aim.gunMatrix.rotation,'gun')
assert native.manages(owner,record)
assert record['projectile_collision_pose']['x']==record['presented_pose']['x']
assert appearance.scroll[0]>0. and f.written[1] and f.written[3]
assert motion.matrix['translation'][0]>0.
close(motion.aim.turretMatrix['rotation'],reference.aim.turretMatrix.rotation)
# Visibility is applied by snapshot lifecycle; hidden belts settle exactly once.
entity._offlineNativeDrawVisible=False
now[0]+=.05;pending,errors=native.advance(owner,sync,now[0]);assert not errors,errors
assert appearance.scroll==(0.,0.) and motion._track_feed_hidden
entity._offlineNativeDrawVisible=True
now[0]+=.05;pending,errors=native.advance(owner,sync,now[0]);assert not errors,errors
assert appearance.scroll[0]>0. and not motion._track_feed_hidden
# An engine-released object must never be dereferenced by presentation again.
entity.owned=False
assert native.manages(owner,record)
old=copy.deepcopy(motion.matrix)
now[0]+=.05;pending,errors=native.advance(owner,sync,now[0]);assert not errors,errors
assert motion.matrix==old
# A restored native owner accepts the next snapshot and resumes immediately.
entity.owned=True
now[0]+=.05;sync.snapshot(dict(round_id=3,server_tick=10,bot_state_revision=10,motion_time_us=500000,bot_state_time_us=500000,bots=[state(7,2.)]));native.ingest(owner,sync,battle)
pending,errors=native.advance(owner,sync,now[0]);assert not pending and not errors,(pending,errors)
assert native.manages(owner,record)
# A same-id replacement is not claimed until its distinct lifecycle is ingested.
replacement=dict(record);assert not native.manages(owner,replacement)
battle._records['bot:7']=replacement
native.ingest(owner,sync,battle);assert native.manages(owner,replacement)
# Changing the installed gun invalidates cached component angles.
entity.typeDescriptor.gun=O(staticTurretYaw=.25,staticPitch=-.1)
native.ingest(owner,sync,battle)
now[0]+=.05;pending,errors=native.advance(owner,sync,now[0]);assert not errors,errors
assert motion.aim.turretMatrix['rotation']==(.25,0.,0.)
assert motion.aim.gunMatrix['rotation']==(0.,-.1,0.)
# A failed matrix write is local and retries a repaired provider next frame.
motion.matrix=[]
now[0]+=.05;pending,errors=native.advance(owner,sync,now[0]);assert len(errors)==1,errors
motion.matrix={}
now[0]+=.05;pending,errors=native.advance(owner,sync,now[0]);assert not errors,errors
assert 'translation' in motion.matrix
native.close(owner)
# Every stock/native callback may synchronously retire the battle owner.
# No Entry storage is destroyed in that callback and no later writer runs.
def reentrant(phase):
 calls=[];cell=[None];checkpoint=[None]
 class ReentrantFilter(O):
  def setTracksSpeed(self,*args):
   calls.append('filter')
   if phase=='filter':native.close(cell[0]);checkpoint[0]=(motion.track_mode,motion.track_written,motion._track_feed)
 class ReentrantAppearance(O):
  def changeEngineMode(self,*args):
   calls.append('engine')
   if phase=='engine':native.close(cell[0]);checkpoint[0]=(motion.track_mode,motion.track_written,motion._track_feed)
  def updateTracksScroll(self,*args):
   calls.append('scroll')
   if phase=='scroll':native.close(cell[0]);checkpoint[0]=(motion.track_mode,motion.track_written,motion._track_feed)
 f=ReentrantFilter(movementInfo=object())
 appearance=ReentrantAppearance(filter=f,flyingInfoProvider=O(isLeftSideFlying=False,isRightSideFlying=False))
 entity=Trap(owned=True,_offlineNativeDrawVisible=True,typeDescriptor=O(gun=O(staticTurretYaw=None,staticPitch=None)),filter=f,appearance=appearance)
 motion=Trap(animation=None,position=(0.,0.,0.),velocity=(0.,0.,0.),acceleration=(0.,0.,0.),yaw=0.,pitch=0.,roll=0.,_last_pose_time=None,_authority_geometry=False,matrix={},provider={},_matrix_pose=None,aim=O(turretMatrix={},gunMatrix={}),_aim_relative_yaw=None,_aim_gun_pitch=None,_track_feed=None,_track_feed_hidden=False,track_mode=None,track_written=None,presentation_capabilities={})
 params=dict(trackCenter=1.5,speedFwd=10.,rotationIsAroundCenter=True)
 record=dict(kind='bot',engine_id=10,ready=True,native_remote=True,state=dict(speed=2.,alive=True,health=100),track_params=params)
 battle=O(_records={'bot:7':record},_remote_factory=O(_states={10:motion},_vehicles={10:entity}),_runtime=O(compatibility=O(_vehicle_property_overlays={id(entity):{}})))
 def params_reader(*args):
  calls.append('params');native.close(cell[0]);return params
 if phase=='params':del record['track_params'];battle._bot_track_params=params_reader
 sync=SnapshotSync(1,clock=lambda:now[0]);cell[0]=native.create()
 sync.snapshot(dict(round_id=5,server_tick=1,bot_state_revision=1,motion_time_us=50000,bot_state_time_us=50000,bots=[state(7,1.)]))
 errors=native.ingest(cell[0],sync,battle);assert not errors,errors
 if phase=='params':assert calls==['params'] and motion.matrix=={},calls
 else:
  pending,errors=native.advance(cell[0],sync,now[0]+.05)
  assert not pending and not errors,(pending,errors)
  expected={'filter':['filter'],'engine':['filter','engine'],'scroll':['filter','engine','scroll']}[phase]
  assert calls==expected,(phase,calls)
  assert checkpoint[0]==(motion.track_mode,motion.track_written,motion._track_feed),(phase,checkpoint[0],motion.__dict__)
 native.close(cell[0])
for phase in ('filter','engine','scroll','params'):reentrant(phase)
print('native client replica differential and lifecycle fixture passed')
'''


class NativeClientReplicaTest(unittest.TestCase):
    def test_standalone_binding_loader(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        spec = importlib.util.spec_from_file_location('isolated_replica_bindings',
            ROOT / 'bigworld/stock_native_client_replica_bindings.py')
        bindings = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bindings)
        folder = Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922'
        for name, transform in (('snapshot_sync.py', bindings.snapshot),
                                ('battle_runtime.py', bindings.battle)):
            prepared = transform((folder / name).read_text())
            compile(prepared, name, 'exec')
            self.assertIn('native_client_replica', prepared)

    def test_typed_history_and_presentation(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
            env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-replica-') as directory:
            folder = Path(directory)
            (folder / 'fixture.py').write_text(FIXTURE)
            cmd = ['c++', '-std=c++17', '-Wno-register', '-bundle', '-undefined',
                'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                '-I' + str(ROOT / 'bigworld'),
                str(ROOT / 'tests/stock_native_client_replica_host.cpp'),
                '-o', str(folder / 'client_replica_test.so')]
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([py27, '-B', str(folder / 'fixture.py'), source],
                cwd=str(folder), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
