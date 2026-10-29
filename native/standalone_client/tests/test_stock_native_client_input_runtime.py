"""Pinned sender checkpoint and actual native control/queue bridge differential."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''from __future__ import print_function
import ast,copy,json,math,random,sys,time,types
import offline_client_input_runtime_test as native
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import lan_client as lan
native.configure_input(lan)
native.landing_configure(lan)
path=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
source=open(path,'rb').read();tree=ast.parse(source)
names=set(('_LANInputSender','_movement_throttle','_number','_xyz','_CRUISE_MODE_THROTTLE','_MOVEMENT_FORWARD','_MOVEMENT_BACKWARD','_MOVEMENT_CRUISE_CONTROL50','_MOVEMENT_CRUISE_CONTROL25'))
selected=[n for n in tree.body if isinstance(n,(ast.ClassDef,ast.FunctionDef)) and n.name in names or isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id in names for t in n.targets)]
env={'math':math};exec(compile(ast.Module(selected),path,'exec'),env)
Sender=env['_LANInputSender']
class Bag(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Owner(Bag):
 def local_pose(self):return self.position,self.yaw
 def local_stabilised_position(self):return self.stabilised
 def local_ram_contacts(self):return self.ram_contacts
 def local_destructible_contacts(self):return self.destructible_contacts
 def local_damage_report(self):return self._local_damage_report
 def _estimated_motion_time_us(self,now):
  if self._pose_motion_time_us is None or self._pose_motion_local_time is None:return None
  return int(self._pose_motion_time_us+max(0.,float(now)-float(self._pose_motion_local_time))*1000000.)
 def _advance_local_gun_to(self,entity):self.advances+=1;return self._gun_state
 def _ram_contacts_enqueued(self):
  if 'ram_contact_ledger_v2' not in self.client.server_capabilities:raise RuntimeError('missing ram contract')
 def _destructible_contacts_enqueued(self):
  if lan.HUMAN_RAM_TIMELINE_CAPABILITY not in self.client.server_capabilities:raise RuntimeError('missing destructible contract')
 def _echo_local_gun_angles(self,turret=None,pitch=None):
  if turret is None:turret=self._avatar.gunRotator._VehicleGunRotator__turretYaw;pitch=self._avatar.gunRotator._VehicleGunRotator__gunPitch
  # Separate exact packed-angle test covers quantization; observe parity here.
  self.echoes.append((turret,pitch))
def client():
 c=lan.LANClient('localhost',28782,'P','ussr:R11_MS-1');c.ready=True;c.phase='battle';c.round_id=7;c.player_id=1;c.connected=True;c.running=True;c.sock=object();c._stopping=False;c._outbound_accepting=True
 c.capabilities=[lan.HUMAN_RAM_TIMELINE_CAPABILITY,lan.PLAYER_PROJECTILE_OWNER_CAPABILITY,lan.PLAYER_ENVIRONMENT_CAPABILITY,'ram_contact_ledger_v2'];c.authority_epoch=2;c.server_capabilities=list(c.capabilities);return c
def owner(raw,now):
 c=client();g=Bag(**raw['gun']);o=Owner(client=c,position=raw['position'],yaw=raw['yaw'],_local_pitch=raw['pitch'],_local_roll=raw['roll'],_local_speed=raw['speed'],_local_surface_up_cosine=raw['up_cosine'],_local_siege_braking=raw['braking'],_gun_state=g,_gun_last_tick=1.,_pose_motion_time_us=1234567890123,_pose_motion_local_time=now-.037,_local_damage_report=None,_local_contact_pushes={},_local_turret_pushes={},ram_contacts=raw['ram_contacts'],destructible_contacts=raw['destructible_contacts'],_worker_mode=False,state='running',_local_siege_aim_pitch=.1,_runtime=Bag(bigworld=Bag(time=time.time)),_server=Bag(vehicle_id=1),advances=0,echoes=[],stabilised=(1.1,2.2,3.3))
 o._runtime.math=Bag(Matrix=lambda provider:Bag(translation=provider));o._local_stabilised_snapshot=o.stabilised
 o._clock=lambda:now;o._server_entity=lambda unused:object();o._sender=Sender(o)
 for k,v in raw['controls'].items():setattr(o._sender,k,v)
 o._local_contact_pushes=dict(enumerate(raw['tank_pushes']));o._local_turret_pushes=dict(enumerate(raw['turret_pushes']))
 o._avatar=Bag(gunRotator=Bag(_VehicleGunRotator__turretYaw=.25,_VehicleGunRotator__gunPitch=-.11))
 o._test_entity=Bag(typeDescriptor=Bag(gun=Bag(pitchLimits={'absolute':(-.3,.7)})))
 return o
base=dict(position=(10.,20.,30.),yaw=.3,pitch=.2,roll=-.1,speed=8.,up_cosine=None,braking=None,controls=dict(forward=1.,turn=-.3,aim_yaw=.4,gun_pitch=-.2,aim_pitch=-.2,aim_point=None,handbrake=False),gun=dict(shot_index=0,pending_index=None,clip=1,clip_size=1,reload_time=1.,reload_duration=5.,dispersion=.01),ram_contacts=[],tank_pushes=[],turret_pushes=[],destructible_contacts=[])
def outcome(fn,*args):
 try:return ('ok',fn(*args))
 except Exception as error:return ('error',type(error).__name__)
def wire(c):return [(seq,plain,size) for seq,plain,size in c._outbound_queue]
def compare(raw,now,siege=None,attrs=None):
 a=owner(raw,now);b=owner(raw,now)
 for k,v in (attrs or {}).items():
  if k.startswith('client.'):setattr(a.client,k[7:],copy.deepcopy(v));setattr(b.client,k[7:],copy.deepcopy(v))
  else:setattr(a,k,copy.deepcopy(v));setattr(b,k,copy.deepcopy(v))
 native.install(b,raw);ra=outcome(a._sender.send_current,siege);rb=outcome(native.send_at,b,now,siege)
 assert ra==rb,(raw,attrs,ra,rb)
 assert wire(a.client)==wire(b.client),(raw,attrs,wire(a.client),wire(b.client))
 assert a.advances==native.advance_count(),(a.advances,native.advance_count())
 return a,b
random.seed(1513)
for index in range(500):
 raw=copy.deepcopy(base);raw.update(position=tuple(random.uniform(-999.,999.) for unused in range(3)),yaw=random.uniform(-math.pi,math.pi),pitch=random.uniform(-2.,2.),roll=random.uniform(-2.,2.),speed=random.uniform(-20.,20.),up_cosine=None if index%3 else random.uniform(-1.2,1.2),braking=None if index%5 else False)
 raw['controls'].update(forward=random.uniform(-1.,1.),turn=random.uniform(-1.,1.),aim_yaw=random.uniform(-9.,9.),gun_pitch=random.uniform(-1.,1.));raw['gun'].update(shot_index=index%3,pending_index=(index+1)%3 if index%7==0 else None,reload_time=random.uniform(0.,5.))
 if index%11==0:raw['ram_contacts']=[dict(seq=index,target_id=2)]
 if index%13==0:raw['destructible_contacts']=[dict(seq=index,item_index=8)]
 if index%17==0:raw['tank_pushes']=[dict(id=2,x=.3,z=-.5)];raw['turret_pushes']=[dict(id=9,x=.1,z=.2)]
 compare(raw,1000.+index*.037,None if index%4 else bool(index%8))
valid=dict(tracks=[dict(name='leftTrackHealth',hp=12.3455,max_hp=50.0005,state='critical')],critical_base_revision=4,critical_seq=9)
reports=[None,{},valid,dict(valid,tracks=[]),dict(valid,tracks=[dict(valid['tracks'][0],hp=-1.)]),dict(valid,tracks=[dict(valid['tracks'][0],extra=1)]),dict(valid,tracks=valid['tracks']*2),dict(valid,critical_base_revision=-1),dict(valid,critical_seq=0)]
for key,values in [('critical_base_revision',[True,'4',1.2,2**80]),('critical_seq',[False,'9',1.2,2**80])]:
 for value in values:reports.append(dict(valid,**{key:value}))
for value in (True,None,'12.3',float('nan'),float('inf')):reports.append(dict(valid,tracks=[dict(valid['tracks'][0],hp=value)]))
for key in ('name','state'):
 for value in (None,1,False,[],{}):reports.append(dict(valid,tracks=[dict(valid['tracks'][0],**{key:value})]))
for report in reports:compare(base,100.,attrs={'_local_damage_report':report})
for attrs in ({'client.ready':False},{'client._outbound_accepting':False},{'client._outbound_bytes':lan.MAX_OUTBOUND_BYTES},{'_pose_motion_time_us':None},{'_pose_motion_local_time':None},{'_pose_motion_local_time':101.},{'client.server_capabilities':[lan.HUMAN_RAM_TIMELINE_CAPABILITY,lan.PLAYER_PROJECTILE_OWNER_CAPABILITY]}):compare(base,100.,attrs=attrs)
# Native runtime may not dispatch any of the old per-send gameplay callbacks.
b=owner(base,100.);native.install(b,base)
def bomb(*args,**kw):raise AssertionError('Python input business callback')
for key in ('local_pose','local_ram_contacts','local_destructible_contacts','local_damage_report','_advance_local_gun_to','_estimated_motion_time_us','_ram_contacts_enqueued','_destructible_contacts_enqueued'):setattr(b,key,bomb)
b.client.send_input=b.client.send_track_repair=b.client._send=bomb
assert native.send_at(b,100.)
assert native.advance_count()==1
# Contact barriers freeze their exact pose before newer current-pose input.
# A full outbound queue retains the unsent frontier and all later barriers.
b=owner(base,100.);native.install(b,base)
native.barrier((1.,2.,3.),.1,3.,.4);native.barrier((4.,5.,6.),.2,6.,.8)
b.client._outbound_accepting=False
assert native.send_at(b,100.) is False
assert native.frontiers()==(2,0) and wire(b.client)==[]
b.client._outbound_accepting=True
assert native.send_at(b,100.) is True
assert native.frontiers()==(0,0)
assert [row[1]['x'] for row in wire(b.client)]==[1.,4.,10.]
assert [row[1]['input_seq'] for row in wire(b.client)]==[1,2,3]
assert native.advance_count()==4
assert native.gun_motion()==[(3.,.4),(3.,.4),(6.,.8),(8.,0.)]
# Admission can close after the first barrier. It remains sent exactly once,
# while the second barrier and current checkpoint wait for backpressure.
b=owner(base,100.);native.install(b,base)
native.barrier((7.,2.,3.),.1);native.barrier((8.,5.,6.),.2)
class CloseAfterOne(object):
 def set(self):b.client._outbound_accepting=False
b.client._outbound_event=CloseAfterOne()
assert native.send_at(b,100.) is False
assert native.frontiers()==(1,0) and [row[1]['x'] for row in wire(b.client)]==[7.]
b.client._outbound_event=__import__('threading').Event();b.client._outbound_accepting=True
assert native.send_at(b,100.) is True
assert [row[1]['x'] for row in wire(b.client)]==[7.,8.,10.]
# Landing must publish its current input before its one-shot report. A failed
# report preserves both the original impact and its existing retry sequence.
b=owner(base,100.);native.install(b,base)
native.barrier((7.,2.,3.),.1)
native.landing(dict(impact_speed=19.4,module_contacts=[dict(name='engineHealth',weight=.4)]))
b.client._outbound_accepting=False
assert native.flush_landing(b,100.) is False and native.frontiers()==(1,1)
b.client._outbound_accepting=True
assert native.flush_landing(b,100.) is True and native.frontiers()==(0,0)
rows=[row[1] for row in wire(b.client)]
assert [row['type'] for row in rows]==['input','input','landing_observation']
assert rows[-1]['input_seq']==2 and rows[-1]['impact_speed']==19.4
assert native.advance_count()==3
b=owner(base,100.);native.install(b,base)
native.landing(dict(impact_speed=18.25,module_contacts=[]))
b.client._outbound_event=CloseAfterOne()
assert native.flush_landing(b,100.) is False and native.frontiers()==(0,1)
assert b.client._landing_observation_pending['reported'] is False
b.client._outbound_event=__import__('threading').Event();b.client._outbound_accepting=True
b.client.send_landing_observation=b.client._send_pending_landing_observation=bomb
assert native.flush_landing(b,100.) is True and native.frontiers()==(0,0)
rows=[row[1] for row in wire(b.client)]
assert [row['type'] for row in rows]==['input','input','landing_observation']
assert rows[-1]['observation_seq']==1 and rows[-1]['input_seq']==2 and rows[-1]['impact_speed']==18.25
assert native.advance_count()==2
# Exercise the actual production control bridge and its transformed Python
# mirror at mailbox entry, including cold initialization before driver install.
sys.path.insert(0,sys.argv[2]);import stock_native_client_input_bindings as bindings
transformed=ast.parse(bindings.battle(source));cls=next(n for n in transformed.body if isinstance(n,ast.ClassDef) and n.name=='_LANInputSender');scope=dict(env);exec(compile(ast.Module([cls]),path,'exec'),scope)
sys.modules['offline_client_transport_native']=native
cold=owner(base,100.);cold.state='loading';cold._sender=scope['_LANInputSender'](cold)
assert cold._sender.forward==0.
for state in ('loading','stopped'):
 cold.state=state;assert native.control(cold,'move',{'flags':1}) is None
 assert native.align_control(cold,.2,-.1) is None
 assert native.assign_control(cold,'forward',1.) is False
native.install(cold,base);cold.state='running';cold._pose_motion_time_us=None;cold._sender=scope['_LANInputSender'](cold)
for key,value in base['controls'].items():setattr(cold._sender,key,value)
for key,value in [('forward',-.4),('turn',.7),('aim_yaw',2.4),('gun_pitch',-.2),('aim_pitch',.3),('aim_point',(3.,4.,5.)),('handbrake',True),('aim_point',None)]:
 setattr(cold._sender,key,value);assert native.controls()[key]==getattr(cold._sender,key)
for flags in range(128):
 assert cold._sender.send_avatar_input(1,'move',{'flags':flags})
 c=native.controls();assert c['forward']==env['_movement_throttle'](flags) and c['turn']==(1. if flags&8 else -1. if flags&4 else 0.) and c['handbrake']==bool(flags&64)
 for key in c:assert c[key]==getattr(cold._sender,key),(key,c[key],getattr(cold._sender,key))
for kind,payload in [('cruise',{'mode':-2}),('track_world',{'point':(100.,2.,30.)}),('track_relative',{'point':(-2.,3.,4.)}),('stop_tracking',{'turret_yaw':.4,'gun_pitch':-.1})]:
 original=owner(base,100.);original._pose_motion_time_us=None
 expected=original._sender.send_avatar_input(1,kind,payload)
 native.install(cold,base)
 for key,value in base['controls'].items():setattr(cold._sender,key,value)
 assert cold._sender.send_avatar_input(1,kind,payload)==expected
 for key,value in native.controls().items():assert value==getattr(original._sender,key),(kind,key,value,getattr(original._sender,key))
# Unknown mailbox operations must keep their original owner dispatch.
assert native.control(cold,'shoot',{}) is None
cold._test_retired=True
assert outcome(native.send_at,cold,100.)[0]=='error'
assert outcome(native.control,cold,'move',{'flags':1})[0]=='error'
# Exact startup publishes the ground-adjusted local matrix/pose before the
# arcade camera aligns, while native snapshots still have no local pose.
startup=owner(base,100.);startup.state='loading';startup._sender=scope['_LANInputSender'](startup);startup._local_matrix=None;startup._local_air_lateral=(0.,0.);startup._local_position=startup.position=(27.,8.,-13.);startup._local_yaw=startup.yaw=1.234;startup._local_pitch=.17;startup._local_roll=-.22
native.cold(startup);startup.state='running'
assert outcome(native.align_control,startup,0.,0.)[0]=='error'
startup._local_matrix=object();order=[]
startup._runtime.avatar_input_handler=Bag(_CTRL_MODE=Bag(ARCADE='arcade'))
startup._avatar.inputHandler=Bag(_AvatarInputHandler__ctrlModeName='arcade',_AvatarInputHandler__curCtrl=Bag(camera=Bag(setToVehicleDirection=lambda:order.append('camera'))))
startup._avatar.gunRotator.reset=lambda:order.append('rotator')
startup._bind_local_control_sources=lambda *args:order.append('sources')
startup._echo_local_gun_angles=lambda *args:order.append('echo')
method=next(n for cls in tree.body if isinstance(cls,ast.ClassDef) and cls.name=='BattleRuntime' for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='_bind_local_arcade_camera')
camera_scope={};exec(compile(ast.Module([method]),path,'exec'),camera_scope)
assert camera_scope['_bind_local_arcade_camera'](startup)
assert order==['sources','camera','rotator','echo']
assert native.pose()==((27.,8.,-13.),1.234,.17,-.22)
assert startup._sender.aim_yaw==1.234 and startup._sender.gun_pitch==0. and startup._sender.aim_pitch==.27
assert native.advance_count()==0
print(json.dumps(dict(sender_frames=500,repair_cases=len(reports),typed_control_edges=140,hot_python_callbacks=0)))
'''
class NativeClientInputRuntimeTests(unittest.TestCase):
 def test_pinned_sender_and_production_control_bridge(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('Fixed v0.10.1 source input is required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
  prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='native-client-input-runtime-') as directory:
   binary=Path(directory)/'offline_client_input_runtime_test.so'
   law=(Path(source)/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
   (Path(directory)/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
   sources=['stock_native_client_drive_install.cpp','stock_native_client_input.cpp','stock_native_client_input_runtime.cpp','stock_native_client_input_controls.cpp','stock_native_client_input_controls_python.cpp','stock_native_client_state.cpp','stock_native_client_landing.cpp','stock_native_client_drive_publication.cpp']
   cmd=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DWOT_CLIENT_INPUT_HOST_BUILD','-DWOT_CLIENT_INPUT_RUNTIME_HOST_BUILD','-DWOT_CLIENT_INPUT_CONTROLS_HOST_BUILD','-DWOT_CLIENT_REPLICA_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+directory,'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native')]+[str(ROOT/'bigworld'/s) for s in sources]+[str(ROOT/'tests/stock_native_client_input_runtime_host.cpp'),'-o',str(binary)]
   result=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
   fixture=Path(directory)/'fixture.py';fixture.write_text(FIXTURE)
   result=subprocess.run([py27,'-B',str(fixture),source,str(ROOT/'bigworld')],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
   self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
if __name__=='__main__':unittest.main()
