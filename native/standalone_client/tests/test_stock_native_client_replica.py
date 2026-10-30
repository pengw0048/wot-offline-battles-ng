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
receipt_contract=native.receipt_contract()
assert receipt_contract["appends"]==2049 and receipt_contract["bound_checks"]==10245 and receipt_contract["steady_allocations"]==0,receipt_contract
allocation_contract=native.value_contract()
assert allocation_contract["probe"]==1,allocation_contract
for key in ("primitive","shared","move"):assert allocation_contract[key]==0,allocation_contract
assert allocation_contract["vector_relocation"]==1,allocation_contract
print("Value allocation contract",allocation_contract)
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class LegacyGun:
 staticTurretYaw=None;staticPitch=None
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
 def __init__(self,**kw):
  O.__init__(self,**kw);self.setTracksSpeed=native.controller(self,'_filter_effect')
 def _filter_effect(self,left,lc,right,rc):self.written=(left,lc,right,rc)
class Appearance(O):
 def __init__(self,**kw):
  O.__init__(self,**kw)
  self._CompoundAppearance__trackScrollCtl=O(setMode=native.controller(self,'_engine_effect'),setExternal=native.controller(self,'_scroll_effect'))
 def changeEngineMode(self,*args):raise AssertionError('Python engine mode producer')
 def updateTracksScroll(self,*args):raise AssertionError('Python track scroll producer')
 def _engine_effect(self,mode):self.engine_mode=mode
 def _scroll_effect(self,left,right):self.scroll=(left,right)
entity=Trap(owned=True,_offlineNativeDrawVisible=True,typeDescriptor=O(gun=LegacyGun()))
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
 class ReentrantFilter(Filter):
  def _filter_effect(self,*args):
   calls.append('filter')
   if phase=='filter':native.close(cell[0]);checkpoint[0]=(motion.track_mode,motion.track_written,motion._track_feed)
 class ReentrantAppearance(Appearance):
  def _engine_effect(self,*args):
   calls.append('engine')
   if phase=='engine':native.close(cell[0]);checkpoint[0]=(motion.track_mode,motion.track_written,motion._track_feed)
  def _scroll_effect(self,*args):
   calls.append('scroll')
   if phase=='scroll':native.close(cell[0]);checkpoint[0]=(motion.track_mode,motion.track_written,motion._track_feed)
 f=ReentrantFilter(movementInfo=object())
 appearance=ReentrantAppearance(filter=f,flyingInfoProvider=O(isLeftSideFlying=False,isRightSideFlying=False))
 entity=Trap(owned=True,_offlineNativeDrawVisible=True,typeDescriptor=O(gun=LegacyGun()),filter=f,appearance=appearance)
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

PIPELINE_FIXTURE = r'''
# Execute the actual staged SnapshotSync adapter. Its old actor dictionary and
# upsert/target methods are forbidden, including during snapshot admission.
sys.modules['offline_client_replica_native']=native
sys.modules['gui.mods.offline_lan_0922'].__path__.append(sys.argv[2])
from gui.mods.offline_lan_0922 import native_client_replica
namespace=dict(__name__='gui.mods.offline_lan_0922.snapshot_sync_native',
               __package__='gui.mods.offline_lan_0922')
execfile(sys.argv[2]+'/snapshot_sync_native.py',namespace)
PreparedSync=namespace['SnapshotSync']
def forbidden(*args,**kwargs):raise AssertionError('Python snapshot/history hot path executed')
clock=[0.];events=[];materialized=[];journal=[]
battle=O(_worker_mode=False,_config={},_records={},
          _warn_optional_failure=lambda *args,**kwargs:forbidden(*args))
def consume(event):
 events.append(event)
 kind=event['type'];key=event.get('entity')
 if kind=='create':
  battle._records[key]=dict(kind=event['kind'],engine_id=10,ready=False,state={})
 elif kind=='update' and event.get('state'):
  # Model the stock ordered callback boundary. A state event must retain the
  # journal-before-health ordering; pose-only frames do not materialize state.
  while journal:materialized.append(('journal',journal.pop(0)))
  materialized.append(('health',event['state'].get('health')))
  battle._records[key]['state'].update(event['state'])
 elif kind=='destroy' and not event.get('keep_corpse'):
  battle._records.pop(key,None)
a=SnapshotSync(1,clock=lambda:clock[0]);b=PreparedSync(1,on_event=consume,clock=lambda:clock[0])
assert native_client_replica.bind(b,battle)
owner=b._standalone_replica_owner
b._entities=NoScan();b._upsert=forbidden;b._set_remote_target=forbidden
a.manifest(dict(round_id=7));b.manifest(dict(round_id=7))
assert native.clock_state(owner)[:2]==(None,0.)
def compare_frame(t):
 expected=a.advance(t);actual=b.advance(t)
 # Removed/missing entities have no native binding or render work.
 expected=[row for row in expected if row['entity'] in battle._records]
 close(sorted(expected,key=lambda v:v['entity']),sorted(actual,key=lambda v:v['entity']))
 observed=native.inspect(owner)
 for key,record in a._entities.items():
  if key not in battle._records:continue
  got=observed[key]
  for field in ('current','target','presentation_time_us','presentation_delay_us','consumer_resume_sample_us','presentation_bracket'):
   close(record.get(field),got.get(field),key+'.'+field)
  close(bool(record.get('consumer_resume_pending')),got['consumer_resume_pending'],key+'.resume')
  close(bool(record.get('wreck_settled')),got['wreck_settled'],key+'.settled')
random.seed(531);tick=0;stamp=0
for i in range(1430):
 clock[0]+=random.choice((.008,.016,.025,.2)) if i%97==0 else random.choice((.008,.016,.025))
 if i%3==0:
  tick+=1;stamp+=random.choice((33000,50000,66000,200000))
  bot=state(7,i*.025);angle=3.1+i*.003
  bot.update(yaw=(angle+math.pi)%(2*math.pi)-math.pi,aim_yaw=-angle,pitch=math.sin(angle)*.2,roll=math.cos(angle)*.1,gun_pitch=math.sin(angle)*.3,
             reload_time=max(0.,60.-i*.02),combat_fire_elapsed=i*.02,combat_fire_timer=i*.01,
             equipment_states=[dict(equipment=dict(id=1),usesLeft=3,active=False,cooldownTimeLeft=max(0.,60.-i*.02),autoPendingElapsed=i*.01,aiPendingElapsed=i*.02)])
  message=dict(round_id=7,server_tick=tick,bot_state_revision=tick,motion_time_us=stamp,bot_state_time_us=stamp,bots=[bot],players=[state(2,i*.02)])
  a.snapshot(copy.deepcopy(message));before=len(materialized);b.snapshot(message)
  if i>0:assert len(materialized)==before,'pose-only snapshot rematerialized stock entities'
 compare_frame(clock[0])
assert not b._entities,'native snapshot populated Python actor history'
# Header failures do not commit any sequence frontier. The next good frame
# with that sequence must remain admissible.
base=dict(round_id=7,server_time_ms=1234,_client_dispatch_delay=.035,server_tick=tick+1,bot_state_revision=tick+1,motion_time_us=stamp+50000,bot_state_time_us=stamp+50000,bots=[state(7,37.)])
for patch in (dict(bot_state_revision=-1),dict(bot_state_time_us=stamp+100000),dict(motion_time_us=stamp-1),dict(bot_state_revision=True)):
 bad=dict(base);bad.update(patch)
 for sync in (a,b):
  try:sync.snapshot(bad)
  except ValueError:pass
  else:raise AssertionError(('invalid snapshot admitted',patch))
a.snapshot(copy.deepcopy(base));b.snapshot(base)
server,received,epoch=native.clock_state(owner);assert server==1234 and abs(received-(clock[0]-.035))<1.e-12
clock[0]+=.016;compare_frame(clock[0])
# Canonical health transitions are discrete, ordered, and delivered once.
changed=dict(base);changed.update(server_tick=tick+2,bot_state_revision=tick+2,motion_time_us=stamp+100000,bot_state_time_us=stamp+100000)
changed['bots']=[state(7,38.)];changed['bots'][0]['health']=75
journal.append('accepted-hit');before=len(materialized)
a.snapshot(copy.deepcopy(changed));b.snapshot(changed)
assert materialized[before:]==[('journal','accepted-hit'),('health',75)],materialized[before:]
versions=native.storage(owner)['bot:7'];assert versions['current_shared'] and versions['sample_shared'] and versions['receipt_shared']
assert versions['first_receipt']['health']==100 and versions['first_receipt']['equipment_states'][0]['usesLeft']==3
assert native.inspect(owner)['bot:7']['state']['health']==75
before=len(materialized);assert b.snapshot(changed)==[];assert len(materialized)==before
clock[0]+=.016;compare_frame(clock[0])
# A shoved wreck converges, then a round replacement discards its old history.
changed.update(server_tick=tick+3,bot_state_revision=tick+3,motion_time_us=stamp+150000,bot_state_time_us=stamp+150000,bots=[state(7,39.,False)])
a.snapshot(copy.deepcopy(changed));b.snapshot(changed)
for i in range(60):clock[0]+=.016;compare_frame(clock[0])
a.manifest(dict(round_id=8));b.manifest(dict(round_id=8));battle._records={}
assert native.clock_state(owner)==(None,0.,epoch+1)
message=dict(round_id=8,server_tick=1,players=[state(2,5.)])
a.snapshot(copy.deepcopy(message));b.snapshot(message)
clock[0]+=.016;compare_frame(clock[0])
assert set(native.inspect(owner))==set(['player:2'])
# The phase edge may precede the first new authority revision. Its canonical
# hold establishes the same live anchor without a countdown-sized lag.
a.manifest(dict(round_id=9));b.manifest(dict(round_id=9));battle._records={}
for seq,revision,stamp,phase,x in ((1,1,0,'loading',0.),(2,1,5000000,'battle',0.),(3,2,5060000,'battle',.1),(4,3,5120000,'battle',.2)):
 clock[0]+=.05
 message=dict(round_id=9,server_tick=seq,bot_state_revision=revision,motion_time_us=stamp,bot_state_time_us=0 if revision==1 else stamp,timing=dict(phase=phase),bots=[state(7,x)])
 a.snapshot(copy.deepcopy(message));b.snapshot(message);compare_frame(clock[0])
native_client_replica.retire(battle);native.close(owner)
assert battle._native_client_state is None
# A failed stock create remains an admitted operation and retries with the
# newest accepted combat state rather than resurrecting an old health value.
attempt=[0];created=[]
def fail_create(event):
 if event['type']=='create':
  attempt[0]+=1
  if attempt[0]==1:raise RuntimeError('temporary resource startup failure')
  created.append(event['state']['health'])
  battle._records[event['entity']]=dict(kind='bot',engine_id=10,ready=False,state={})
battle._records={};b=PreparedSync(1,on_event=fail_create,clock=lambda:clock[0]);native_client_replica.bind(b,battle)
b.manifest(dict(round_id=10));message=dict(round_id=10,server_tick=1,bots=[state(7)])
try:b.snapshot(message)
except RuntimeError:pass
else:raise AssertionError('failed create was swallowed')
message.update(server_tick=2,bots=[state(7)]);message['bots'][0]['health']=75
b.snapshot(message);assert created==[75],created
assert native.statistics(b._standalone_replica_owner)['pending_creates']==0
native_client_replica.retire(battle)
# Canonical immutable static versions are imported once. Their native values
# stay shared across snapshot state and the bounded RAM receipt archive.
class _StaticDict(dict):pass
outfits=_StaticDict(camouflage='installed');effective=_StaticDict(gun=dict(reloadTime=3.))
b=PreparedSync(1,on_event=consume,clock=lambda:clock[0]);native_client_replica.bind(b,battle);b.manifest(dict(round_id=11))
owner=b._standalone_replica_owner
for seq in range(620):
 bot=state(7,seq*.01);bot.update(outfits=outfits,effective_params=effective)
 b.snapshot(dict(round_id=11,server_tick=seq,bot_state_revision=seq,motion_time_us=seq*50000,bot_state_time_us=seq*50000,bots=[bot]))
stats=native.statistics(owner);assert stats['static_reads']==2,stats
assert stats['static_reuses']==1238,stats
assert native.inspect(owner)['bot:7']['receipt_count']==512
versions=native.storage(owner)['bot:7'];assert versions['current_shared'] and versions['sample_shared'] and versions['receipt_shared']
assert versions['first_receipt']['x']==1.08,versions['first_receipt']
# Neither the caller nor a later canonical version can mutate accepted RAM.
bot['health']=7;assert native.inspect(owner)['bot:7']['state']['health']==100
stats=native.statistics(owner);assert stats['snapshot_calls']==620 and stats['snapshot_parse_calls']==620
assert all(stats[key]>=0. for key in ('snapshot_parse_seconds','snapshot_state_seconds','snapshot_events_seconds','snapshot_release_seconds'))
native_client_replica.retire(battle)
# Exercise production typed ingress with a real ready presentation and an
# asynchronous transition. Gameplay callbacks and Python histories are traps.
errors=[];params=dict(trackCenter=1.5,speedFwd=10.,rotationIsAroundCenter=True)
f=Filter(movementInfo=object());appearance=Appearance(filter=f,flyingInfoProvider=O(isLeftSideFlying=False,isRightSideFlying=False))
entity=Trap(owned=True,_offlineNativeDrawVisible=True,typeDescriptor=O(gun=LegacyGun()),filter=f,appearance=appearance)
motion=Trap(animation=None,position=(0.,0.,0.),velocity=(0.,0.,0.),acceleration=(0.,0.,0.),yaw=0.,pitch=0.,roll=0.,_last_pose_time=None,_authority_geometry=False,matrix={},provider={},_matrix_pose=None,aim=O(turretMatrix={},gunMatrix={}),_aim_relative_yaw=None,_aim_gun_pitch=None,_track_feed=None,_track_feed_hidden=False,track_mode=None,presentation_capabilities={})
battle=O(_worker_mode=False,_config={},_records={},_remote_factory=O(_states={10:motion},_vehicles={10:entity}),_runtime=O(compatibility=O(_vehicle_property_overlays={id(entity):{}})),_warn_optional_failure=lambda *args,**kwargs:errors.append(args))
def ready_consume(event):
 key=event.get('entity')
 if event['type']=='create':battle._records[key]=dict(kind='bot',engine_id=10,ready=False,native_remote=True,state={},track_params=params)
 elif event.get('state'):battle._records[key]['state'].update(event['state'])
b=PreparedSync(1,on_event=ready_consume,clock=lambda:clock[0]);native_client_replica.bind(b,battle)
b._entities=NoScan();b._upsert=forbidden;b._set_remote_target=forbidden
b.manifest(dict(round_id=12));b.snapshot(dict(round_id=12,server_tick=1,bots=[state(7,1.)]))
record=battle._records['bot:7'];assert motion.matrix=={}
record['ready']=True;clock[0]+=.05;b.advance(clock[0]);assert 'translation' in motion.matrix
assert native.manages(b._standalone_replica_owner,record)
entity.owned=False;old=copy.deepcopy(motion.matrix);clock[0]+=.05;b.advance(clock[0]);assert motion.matrix==old
entity.owned=True;clock[0]+=.05;b.advance(clock[0]);assert native.manages(b._standalone_replica_owner,record)
b.snapshot(dict(round_id=12,server_tick=2,bots=[state(7,2.)]))
motion.matrix=[];clock[0]+=.05;b.advance(clock[0]);assert errors
motion.matrix={};clock[0]+=.05;b.advance(clock[0]);assert 'translation' in motion.matrix
# A stock callback may synchronously delete every facade handle during step.
def retire_during_scroll(*args):native_client_replica.retire(battle)
appearance._scroll_effect=retire_during_scroll
appearance._CompoundAppearance__trackScrollCtl.setExternal=native.controller(appearance,'_scroll_effect')
motion._track_feed=None;clock[0]+=.05;b.advance(clock[0])
assert battle._native_client_state is None
# The actual pending-create/removal loop uses the original cold callbacks, but
# never scans a Python .items() list or calls a ready actor on a steady frame.
import ast
battle_source=open(sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py','rb').read()
parsed=ast.parse(battle_source)
method=next(n for cls in parsed.body if isinstance(cls,ast.ClassDef) for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='_flush_pending_entities')
scope={};exec(compile(ast.Module([method]),'<pinned-pending>','exec'),scope)
class Records(dict):
 def items(self):raise AssertionError('Python ready-record scan')
def lifecycle(rows,records_type=dict):
 value=O(_records=records_type(copy.deepcopy(rows)),calls=[])
 def materialize(record):
  value.calls.append(('create',record['engine_id']))
  if record.get('complete'):record['ready']=True;return True
  return False
 value._materialize_record=materialize
 value._flush_tombstone=lambda record:value.calls.append(('remove',record['engine_id']))
 return value
for tick in (0.,1.,10.):
 rows={'ready':dict(engine_id=1,ready=True),'wait':dict(engine_id=2,ready=False,ready_deadline=20.),'done':dict(engine_id=3,complete=True),'dead':dict(engine_id=4,tombstone=True)}
 a=lifecycle(rows);b=lifecycle(rows,Records);h=native.create()
 scope['_flush_pending_entities'](a,tick);assert native.flush_pending(h,b,tick)
 assert a.calls==b.calls and a._records==b._records
 native.close(h)
b=lifecycle(dict((str(i),dict(engine_id=i,ready=True)) for i in range(100)),Records);h=native.create()
b._materialize_record=b._flush_tombstone=forbidden
for unused in range(400):assert native.flush_pending(h,b,100.)
native.close(h)
b=lifecycle({'wait':dict(engine_id=5,ready=False,ready_deadline=20.)},Records);h=native.create()
try:native.flush_pending(h,b,20.)
except RuntimeError as error:assert str(error)=='Vehicle 5 did not enter world before timeout'
else:raise AssertionError('pending timeout disappeared')
native.close(h)
# A cold creation callback may retire the entire round synchronously.
b=lifecycle({'a':dict(engine_id=1),'b':dict(engine_id=2)},Records);h=native.create()
def retire_pending(record):b.calls.append(record['engine_id']);native.close(h);return False
b._materialize_record=retire_pending
assert native.flush_pending(h,b,0.) and len(b.calls)==1

# Thirty canonical actors keep every accepted clock/checkpoint/frontier while
# Python receives only stock semantic edges. No worker authority is involved.
battle=O(_worker_mode=False,_config={},_records={},_warn_optional_failure=lambda *a,**kw:forbidden(*a))
b=PreparedSync(1,on_event=consume,clock=lambda:clock[0]);native_client_replica.bind(b,battle);b.manifest(dict(round_id=13))
owner=b._standalone_replica_owner;seq=[0]
def accepted_population(patch=None):
 seq[0]+=1;players=[];bots=[]
 for kind,rows in (('player',players),('bot',bots)):
  for identifier in range(1,16):
   row=state(identifier,seq[0]*.01)
   row.update(reload_time=120.-seq[0]*.01,siege_state=1,siege_time_left_ms=4000-seq[0],combat_fire_elapsed=seq[0]*.01,combat_fire_timer=seq[0]*.02,gun_checkpoint_seq=seq[0],gun_checkpoint=dict(reload_time=20.-seq[0]*.01),forward=seq[0]%3-1,turn=seq[0]%3-1,up_cosine=.9+seq[0]*.0001,input_processed_seq=seq[0],landing_observation_seq=seq[0],ram_contact_admitted_seq=seq[0],ram_contact_resolved_seq=seq[0],destructible_contact_admitted_seq=seq[0],destructible_contact_resolved_seq=seq[0],destructible_contact_resolved_seqs=[seq[0]],destructible_contact_rejected_seqs=[seq[0]],ram_contact=dict(sequence=seq[0]),ram_contacts=[dict(sequence=seq[0])],destructible_contacts=[dict(sequence=seq[0])],equipment_states=[dict(equipment=dict(id=1,tags=[]),usesLeft=3,active=False,cooldownTimeLeft=20.-seq[0]*.01,autoPendingElapsed=seq[0]*.01,aiPendingElapsed=seq[0]*.02)])
   if kind=='bot' and identifier==7 and patch:patch(row)
   rows.append(row)
 return dict(round_id=13,server_tick=seq[0],bot_state_revision=seq[0],motion_time_us=seq[0]*50000,bot_state_time_us=seq[0]*50000,players=players,bots=bots)
message=accepted_population();b.snapshot(message);before=len(materialized)
for unused in range(120):
 message=accepted_population();b.snapshot(message)
 assert len(materialized)==before,'running clocks rematerialized a stock actor'
observed=native.inspect(owner)
for kind,rows in (('player',message['players']),('bot',message['bots'])):
 for row in rows:assert observed[kind+':'+str(row['id'])]['state']==row
# All reviewed stock edges, nested equipment semantics and unknown fields
# remain conservative. Each changed actor gets one update before health.
patches=[lambda r:r.update(health=75),lambda r:r.update(fire_seq=1),lambda r:r.update(siege_state=2,siege_time_left_ms=0),lambda r:r.update(stun_end_server_time_ms=5000,stun_attacker_kind='player',stun_attacker_id=2,stun_factors=dict(reload=.5)),lambda r:r.update(critical=dict(fire=True)),lambda r:r.update(critical_revision=2,critical_base_revision=1,critical_ack_seq=1),lambda r:r.update(frags=1,team_killer=True),lambda r:r.update(death_attacker_kind='player',death_attacker_id=2,death_reason=1,display_health=75),lambda r:r['equipment_states'][0].update(usesLeft=2),lambda r:r['equipment_states'][0].update(active=True),lambda r:r['equipment_states'][0]['equipment'].update(tags=['changed']),lambda r:r['equipment_states'][0].update(future_edge=1),lambda r:r['equipment_states'].append(dict(equipment=dict(id=2),usesLeft=1,active=False)),lambda r:r.update(equipment_revision=2,equipment_intent_seq=1,equipment_intent_result=dict(accepted=True)),lambda r:r.update(unreviewed_native_field=1)]
for patch in patches:
 # Reset the previous actual edge with its own single update.
 b.snapshot(accepted_population());before=len(materialized);journal.append('accepted-shot-or-damage')
 message=accepted_population(patch);returned=b.snapshot(message)
 updates=[event for event in returned if event['type']=='update']
 assert len(updates)==1 and updates[0]['entity']=='bot:7',(patch,updates)
 assert materialized[before:]==[('journal','accepted-shot-or-damage'),('health',message['bots'][6]['health'])],materialized[before:]
 assert b.snapshot(message)==[],'same accepted sequence replayed a stock edge'
native_client_replica.retire(battle)

# Unused envelopes are owned by admission/world/projectiles, not replica.
# A private resource in those payloads must never be parsed or cloned here.
class UnusedEnvelope(object):
 def __str__(self):raise AssertionError('unused envelope parsed')
 def __unicode__(self):raise AssertionError('unused envelope parsed')
battle=O(_worker_mode=False,_config={},_records={},_warn_optional_failure=lambda *a,**kw:forbidden(*a))
b=PreparedSync(1,on_event=consume,clock=lambda:clock[0]);native_client_replica.bind(b,battle)
opaque=UnusedEnvelope();b.manifest(dict(round_id=15,players=[],bots=[],bot_manifest=opaque,destructibles=opaque,future_envelope=opaque))
message=dict(round_id=15,sequence=1,server_tick=99,bot_state_revision=1,motion_time_us=50000,bot_state_time_us=50000,timing=dict(phase='battle'),server_time_ms=50,_client_dispatch_delay=.01,_client_coalesced_timing=dict(source_interval_us=50000,snapshot_interval_us=50000),bot_orders=[dict(bot_id=7,command='hold')],bot_order_revision=1,bots=[dict(state(7),unknown_actor_field=dict(edge=1))],players=[],bot_manifest=opaque,projectiles=opaque,destructibles=opaque,future_envelope=opaque)
returned=b.snapshot(message);assert any(event['type']=='order' for event in returned)
assert native.inspect(b._standalone_replica_owner)['bot:7']['state']['unknown_actor_field']==dict(edge=1)
# Sequence takes precedence over the server-tick alias, as in the source.
message.update(sequence=1,server_tick=100);assert b.snapshot(message)==[]
message.update(sequence=2,server_tick=100,motion_time_us=100000,bot_state_time_us=100000);assert b.snapshot(message)==[]
native_client_replica.retire(battle)
# A stable/rejected local Siege request has only an input ACK, which is native
# data rather than a stock state edge. It still releases the legacy latch.
battle=O(_worker_mode=False,_config={},_records={},_warn_optional_failure=lambda *a,**kw:forbidden(*a),_local_siege_pending=None)
acks=[];battle._report_local_siege_edge=lambda *args:acks.append(args)
b=PreparedSync(1,on_event=consume,clock=lambda:clock[0]);native_client_replica.bind(b,battle);b.manifest(dict(round_id=14))
def siege_snapshot(sequence,mode=0):
 row=state(1);row.update(input_seq=sequence,siege_state=mode,siege_time_left_ms=500 if mode in (1,3) else 0)
 return b.snapshot(dict(round_id=14,server_tick=sequence,players=[row]))
siege_snapshot(1);before=len(materialized);battle._local_siege_pending=(True,3)
siege_snapshot(2);assert battle._local_siege_pending==(True,3)
siege_snapshot(3);assert battle._local_siege_pending is None and len(materialized)==before
assert acks==[('ack',0,0,3)],acks
battle._local_siege_pending=(True,4);siege_snapshot(4,1);assert battle._local_siege_pending==(True,4)
siege_snapshot(5,2);assert battle._local_siege_pending is None and acks[-1]==('ack',2,0,5)
native_client_replica.retire(battle)
print('native accepted snapshot pipeline: 1430 frames, 30 actor clocks and ordered semantic edges passed')

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
            law = (Path(source) / 'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder / 'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
            spec = importlib.util.spec_from_file_location('pipeline_replica_bindings',
                ROOT / 'bigworld/stock_native_client_replica_bindings.py')
            bindings = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(bindings)
            snapshot_source = (Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922/snapshot_sync.py').read_text()
            (folder / 'snapshot_sync_native.py').write_text(bindings.snapshot(snapshot_source))
            (folder / 'native_client_replica.py').write_text(
                (ROOT / 'bigworld/stock_native_client_replica.py').read_text())
            (folder / 'fixture.py').write_text(FIXTURE + PIPELINE_FIXTURE)
            cmd = ['c++', '-std=c++17', '-Wno-register', '-bundle', '-undefined',
                'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'),
                '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(source) / 'native'), '-I' + directory,
                str(ROOT / 'tests/stock_native_client_replica_host.cpp'),
                '-o', str(folder / 'client_replica_test.so')]
            result = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([py27, '-B', str(folder / 'fixture.py'), source, str(folder)],
                cwd=str(folder), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
