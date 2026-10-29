"""Compare the entire live snapshot envelope with the pinned Python 2.7 owner."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''# -*- coding: utf-8 -*-
from __future__ import print_function
import copy,imp,json,math,random,sys,types
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
sys.path.append(sys.argv[2]+'/../../tests')
for name in ('gui','gui.mods'):
 module=types.ModuleType(name);module.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=module
from gui.mods.offline_lan_0922 import lan_client as lan
from effective_params_fixture import effective_params
static=imp.load_source('admission_static',sys.argv[2]+'/bigworld/stock_snapshot_static.py')
import client_admission_test as native
native.configure(static._StaticDict,static._StaticList)
native.configure_input(lan)
native.landing_configure(lan)
clock=[100.]
lan._monotonic_time=lambda:clock[0]
native.configure_admission(lan,static,lambda:clock[0])
raw_params=effective_params();raw_params['physics']['rotationIsAroundCenter']=True
params=lan._canonical_effective_params(raw_params);assert params is not None
FIELDS=('last_snapshot','last_error','server_time_ms','bot_authority_id','authority_epoch','_snapshot_accepted_time','_snapshot_drop_streak','_snapshot_drop_reason','_equipment_intent_seq','_input_seq','_input_seq_round','_landing_observation_seq','_landing_observation_round','_landing_observation_pending','_landing_observation_queue','combat_phase','combat_duration','combat_deadline','combat_end_deadline','_combat_timing_round_id','_combat_timing_tick','_published_player_outfits','_published_player_outfit_sources','_published_player_effective_params','_published_player_effective_param_sources')
def client():
 c=lan.LANClient('localhost',28782,'P','ussr:R11_MS-1');c.phase='battle';c.round_id=7;c.player_id=1;c.authority_epoch=2;c.server_time_ms=1000;c._schema_negotiated=True
 c.capabilities=[lan.PROJECTILE_LEDGER_CAPABILITY];c.server_capabilities=[lan.LEAN_SNAPSHOT_MANIFEST_CAPABILITY]
 c._published_player_effective_params[1]=copy.deepcopy(params);c._published_player_effective_param_sources[1]=copy.deepcopy(params);c._published_player_outfits[1]={};c._published_player_outfit_sources[1]={}
 c.drops=[];c.degraded=[];c.stopped=False;c.notified=[];c.landing=[]
 def ignore(kind,reason,message):
  c.drops.append((kind,reason));return c.phase in ('loading','battle')
 c._ignore_runtime_payload=ignore;c.stop=lambda:setattr(c,'stopped',True);c._notify=lambda *args,**kw:c.notified.append(args)
 c._log_degraded_snapshot=lambda *args:c.degraded.append(args)
 return c
def player(x=1.):return dict(id=1,team=1,slot=0,vehicle='ussr:R11_MS-1',x=x,y=0.,z=2.,health=100,alive=True,input_seq=5,input_processed_seq=6,landing_observation_seq=0,equipment_intent_seq=2)
def snapshot(tick=1):return dict(type='snapshot',protocol=lan.PROTOCOL_VERSION,round_id=7,server_tick=tick,server_time_ms=1000+tick,authority_epoch=2,projectile_revision=0,bot_state_revision=tick,motion_time_us=tick*50000,bot_state_time_us=tick*50000,bot_authority_id=-1,projectiles=[],bot_manifest=[],players=[player()],bots=[])
def compare(a,b):
 for field in FIELDS:
  left=getattr(a,field);right=getattr(b,field)
  if isinstance(left,float) and isinstance(right,float):assert abs(left-right)<1.e-9,(field,left,right)
  else:assert left==right,(field,left,right)
 assert a.stopped==b.stopped,(a.stopped,b.stopped)
 assert a.drops==b.drops,(a.drops,b.drops)
 assert a.degraded==b.degraded,(a.degraded,b.degraded)
 assert a.landing==b.landing,(a.landing,b.landing)
def apply(a,b,message):
 a._handle_message(copy.deepcopy(message));actual=native.accept(b,copy.deepcopy(message))
 compare(a,b)
 if actual is not None:
  assert actual==b.last_snapshot
  assert native.held(b,dict(actual)) is actual
  unrelated=dict(actual);unrelated['players']=list(actual['players']);assert native.held(b,unrelated) is None
 return actual
# Ordinary traffic, duplicate authority revisions and phase/timing transitions.
a=client();b=client()
for i in range(1,301):
 clock[0]+=.016;message=snapshot(i);message['players'][0]['x']=math.sin(i)*10.
 if i%3==0:message['timing']=dict(phase='battle',start_in_ms=0,remaining_ms=900000-i*10,duration_ms=900000)
 if i>1:message.pop('bot_manifest')
 apply(a,b,message)
# Every rejected header keeps accepted actor/static frontiers unchanged.
cases=[]
for field,values in (('server_tick',[None,True,-1,'1',1.5]),('bot_state_revision',[None,True,-1,2147483648,0]),('round_id',[None,True,6,8,'7']),('protocol',[None,False,0,1]),('server_time_ms',[None,True,-1,2147483648,900]),('authority_epoch',[None,True,-1,1,2147483648]),('motion_time_us',[None,True,-1,1,10000000000000001]),('bot_state_time_us',[None,True,-1,20000000]),('players',[None,{},[None],tuple()]),('bots',[None,{},[None]]),('bot_manifest',[None,{},[None]]),('bot_authority_id',[None,False,1,'-1'])):
 for value in values:
  message=snapshot(301);message[field]=value;cases.append(message)
for field,value in (('bot_orders',None),('bot_orders',[{}]),('bot_order_revision',-1),('destructibles',None),('destructible_revision',-1),('timing',None),('timing',dict(phase='battle',start_in_ms=-1,remaining_ms=1,duration_ms=2))):
 message=snapshot(301);message[field]=value;cases.append(message)
for message in cases:
 a=client();b=client();apply(a,b,snapshot());apply(a,b,message)
# Compound failures retain the original prefix ordering, and timing remains a
# receive-clock projection across phase changes, RTT caps, stale ticks and bad
# numeric strings rather than becoming a physics clock.
a=client();b=client();message=snapshot();message.update(server_time_ms=-1,bot_authority_id=9);apply(a,b,message)
for rtt in (None,0.,120.,1000.):
 a=client();b=client();a.rtt_ms=b.rtt_ms=rtt
 for tick,phase,start,remaining,received in ((1,'loading',5000,900000,100.),(2,'prebattle',5000,900000,100.03),(3,'prebattle',4970,900000,100.06),(4,'battle',0,899970,105.03),(3,'prebattle',2000,900000,110.),(5,'finished',0,0,1000.)):
  message=snapshot(tick);message['_client_received_time']=received
  message['timing']=dict(phase=phase,start_in_ms=start,remaining_ms=remaining,duration_ms=900000)
  assert a._load_server_timing(message)==native.timing(b,message)
  for field in ('combat_phase','combat_duration','combat_deadline','combat_end_deadline','_combat_timing_round_id','_combat_timing_tick'):
   left=getattr(a,field);right=getattr(b,field)
   assert left==right or isinstance(left,float) and isinstance(right,float) and abs(left-right)<1.e-9,(field,left,right)
# Retain invalid known actors, omit unknown invalid actors, and preserve the
# lean roster even after the worker advances its authority epoch.
a=client();b=client();apply(a,b,snapshot())
message=snapshot(2);message.pop('bot_manifest');message['authority_epoch']=3;message['players']=[dict(player(),x=float('nan')),dict(player(),id=9,x=float('nan'))]
apply(a,b,message);assert b.last_snapshot['players'][0]['x']==1.
# Local reconnect frontiers and discrete landing completion/rebind edges.
for pending,landing,epoch in ((dict(observation_seq=2),2,2),(dict(observation_seq=3,wire=dict(authority_epoch=1),sent=True,input_seq=4,impact_speed=20.,module_contacts=[]),1,2)):
 a=client();b=client()
 for c in (a,b):c._landing_observation_round=7;c._landing_observation_seq=1;c._landing_observation_pending=copy.deepcopy(pending)
 message=snapshot();message['players'][0].update(landing_observation_seq=landing,input_seq=5,input_processed_seq=9)
 apply(a,b,message)
# The complete projectile envelope is validated natively for human and Bot
# paths, including burst identity, AP/HE/HESH, ricochet and malformed vectors.
def projectile(human=True):
 shot=dict(speed=100.,gravity=9.81,maxDistance=500.,piercingPower=[100.,90.],deadeye=False,shell=dict(kind='ARMOR_PIERCING',caliber=76.,damage=[100.,30.],explosionRadius=0.))
 out=dict(projectile_id='player:1:1' if human else 'bot:2:1',shooter_kind='player' if human else 'bot',shooter_id=1 if human else 2,shot_seq=1,burst_group_seq=1,burst_index=0,burst_count=1,source_vehicle='ussr:R11_MS-1',source_shot=shot,shell_index=0,team=1,origin=[0.,1.,0.],velocity=[100.,0.,0.],gravity=9.81,max_distance=500.,max_time_ms=10000,is_he=False,splash_radius=0.,penetration_factor=1.,launch_server_time_ms=1000,checked_through_ms=100,checked_distance=10.,piercing_loss=0.,range_origin=[0.,1.,0.],segment_origin=[0.,1.,0.],segment_velocity=[100.,0.,0.],segment_start_time_ms=0,ricochet_count=0,base_penetration_multiplier=1.,authority_epoch=2)
 if human:out.update(fire_intent_seq=1,fire_input_seq=6)
 return out
projectile_cases=[projectile(),projectile(False)]
for kind,multiplier in (('ARMOR_PIERCING',.75),('ARMOR_PIERCING_CR',.75),('HOLLOW_CHARGE',1.),('HIGH_EXPLOSIVE',1.),('ARMOR_PIERCING_HE',.75)):
 row=projectile();row['source_shot']['shell']['kind']=kind
 if kind=='HIGH_EXPLOSIVE':row.update(is_he=True,splash_radius=2.);row['source_shot']['shell']['explosionRadius']=2.
 projectile_cases.append(row)
 ric=copy.deepcopy(row);ric.update(ricochet_count=1,base_penetration_multiplier=multiplier,segment_start_time_ms=50,segment_origin=[1.,1.,0.],segment_velocity=[90.,0.,0.]);projectile_cases.append(ric)
for field,values in (('origin',[(0.,1.,0.),[0.,4000.,0.]]),('velocity',[[0.,0.,0.],[3001.,0.,0.]]),('fire_intent_seq',[0,None,True]),('burst_index',[1,None]),('base_penetration_multiplier',[0.,None]),('checked_distance',[501.,None]),('ricochet_count',[2,True]),('authority_epoch',[1,None]),('projectile_id',['bad space',u'中文']),('source_shot',[None,{}])):
 for value in values:row=projectile();row[field]=value;projectile_cases.append(row)
for row in projectile_cases:
 a=client();b=client();message=snapshot();message['projectiles']=[row];apply(a,b,message)
for extra in (dict(bot_presentation_time_us=1),dict(bot_presentation_time_us=True)):
 for human in (True,False):
  a=client();b=client();message=snapshot();row=projectile(human);row.update(extra);message['projectiles']=[row];apply(a,b,message)
a=client();b=client();message=snapshot();message['projectiles']=[projectile(),projectile()];apply(a,b,message)
for patch in ({'stunRadius':1.},dict(stunRadius=2.,stunDuration=10.,stunFactor=.5,guaranteedStunDuration=.5,damageDurationCoeff=.3,guaranteedStunEffect=.5,damageEffectCoeff=.2)):
 a=client();b=client();message=snapshot();row=projectile();row.update(is_he=True,splash_radius=2.);row['source_shot']['shell'].update(kind='HIGH_EXPLOSIVE',explosionRadius=2.,stun=patch);message['projectiles']=[row];apply(a,b,message)
# Rejected envelopes can memoize their immutable value version, but they must
# never publish a new loadout into the client's accepted lean-snapshot cache.
a=client();b=client();apply(a,b,snapshot())
changed=copy.deepcopy(params);changed['physics']['mass']+=100.
version=static.freeze(changed);message=snapshot(2);message['server_tick']=-1;message['players'][0].update(outfits=static.freeze({}),effective_params=version)
apply(a,b,message);assert b._published_player_effective_params[1]['physics']['mass']==params['physics']['mass']
message['server_tick']=2;apply(a,b,message)
assert b._published_player_effective_params[1]['physics']['mass']==changed['physics']['mass']
# Cold normalizer calls stop once immutable value versions are canonical.
counts=[0,0]
original_outfits=lan._canonical_wire_outfits;original_params=lan._canonical_effective_params
def outfits(value):counts[0]+=1;return original_outfits(value)
def parameters(value):counts[1]+=1;return original_params(value)
lan._canonical_wire_outfits=outfits;lan._canonical_effective_params=parameters
native.configure_admission(lan,static,lambda:clock[0]);b=client();counts[:]=[0,0]
source_outfits=static.freeze({});source_params=static.freeze(params)
for i in range(1,101):
 message=snapshot(i);message['players'][0].update(outfits=source_outfits,effective_params=source_params)
 assert native.accept(b,message) is not None
assert counts==[1,1],counts
# Forbidden Python hot-path validators and loops are never consulted.
def bomb(*args,**kwargs):raise AssertionError('Python admission business callback')
for name in ('_strict_mapping_list','_valid_active_projectiles','_valid_visible_authority_message','_canonical_runtime_vehicle_row','_exact_int','_projectile_int_range'):setattr(lan,name,bomb)
b._canonical_runtime_rows=bomb;b._prepare_player_static_inputs=bomb;b._commit_player_static_inputs=bomb;b._load_server_timing=bomb;b._adopt_player_input_frontier=bomb
for i in range(101,121):
 message=snapshot(i);message['players'][0].update(outfits=source_outfits,effective_params=source_params)
 assert native.accept(b,message) is not None
assert counts==[1,1],counts
# Execute the actual production adapter, including its module initialization
# and full _handle_message fast path. A syntactically staged file alone cannot
# prove that runtime module imports or this lifecycle seam are valid.
sys.modules['offline_client_transport_native']=native
sys.modules[lan.__package__+'.stock_snapshot_static']=static
setattr(sys.modules[lan.__package__],'stock_snapshot_static',static)
static_bindings=imp.load_source('test_static_bindings',sys.argv[2]+'/bigworld/stock_snapshot_bindings.py')
transport_bindings=imp.load_source('test_transport_bindings',sys.argv[2]+'/bigworld/stock_native_client_transport_bindings.py')
adapted_source=transport_bindings.client(static_bindings.client(open(lan.__file__.replace('.pyc','.py'),'rb').read()))
adapted=types.ModuleType(lan.__name__+'_native_integration');adapted.__package__=lan.__package__;sys.modules[adapted.__name__]=adapted
exec(compile(adapted_source,'adapted_lan_client.py','exec'),adapted.__dict__)
native.configure_admission(adapted,static,lambda:clock[0])
c=adapted.LANClient('localhost',28782,'P','ussr:R11_MS-1');c.__dict__.update(b.__dict__)
notifications=[];c._notify=lambda *args,**kw:notifications.append((args,kw))
message=snapshot(121);message['players'][0].update(outfits=source_outfits,effective_params=source_params)
c._handle_message(message)
assert len(notifications)==1 and notifications[0][0][0]=='snapshot',notifications
assert c.last_snapshot['server_tick']==121
stale=dict(message,round_id=6);c._handle_message(stale);assert len(notifications)==1
assert native.held(c,c.last_snapshot) is not None
print(json.dumps(dict(ordinary_snapshots=300,invalid_envelopes=len(cases),projectile_cases=len(projectile_cases),steady_python_business_callbacks=0)))
'''

class NativeClientAdmissionTests(unittest.TestCase):
    def test_snapshot_envelope_matches_fixed_client(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-client-admission-') as directory:
            binary = Path(directory) / 'client_admission_test.so'
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-register', '-DWOT_CLIENT_TRANSPORT_HOST_BUILD', '-DWOT_CLIENT_ADMISSION_HOST_BUILD', '-DWOT_CLIENT_INPUT_HOST_BUILD', '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'), str(ROOT / 'bigworld/stock_native_client_admission.cpp'), str(ROOT / 'bigworld/stock_native_client_transport.cpp'), str(ROOT / 'bigworld/stock_native_client_input.cpp'), str(ROOT/'bigworld/stock_native_client_landing.cpp'), str(ROOT / 'tests/stock_native_client_admission_host.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            script = Path(directory) / 'fixture.py'
            script.write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(script), source, str(ROOT)], capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('"steady_python_business_callbacks": 0', run.stdout)
            print(run.stdout.strip())

if __name__ == '__main__':
    unittest.main()
