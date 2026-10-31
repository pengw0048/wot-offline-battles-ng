"""Exercise the actual CPython 2.7 projectile wire boundary."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''
import copy,math,json,sys,types
import projectile_wire_test as native
row=dict(projectile_id='shell',shooter_kind='bot',shooter_id=4,source_vehicle='ussr:T-34',shot_seq=8,burst_group_seq=8,burst_index=0,burst_count=1,shell_index=0,origin=[0.,0.,0.],velocity=[0.,0.,100.],range_origin=[0.,0.,0.],segment_origin=[0.,0.,0.],segment_velocity=[0.,0.,100.],gravity=10.,max_distance=500.,max_time_ms=20000,is_he=False,splash_radius=0.,penetration_factor=1.,launch_server_time_ms=0,segment_start_time_ms=0,ricochet_count=0,base_penetration_multiplier=1.)
row['source_shot']=dict(speed=100.,gravity=10.,maxDistance=500.,piercingPower=[100.,80.],deadeye=False,shell=dict(kind='ARMOR_PIERCING',caliber=100.,damage=[200.,80.],explosionRadius=0.))
value=native.parse(row);assert value['shooter']==4 and value['damage']==200 and value['device_damage']==80 and value['segment']==0
same=copy.deepcopy(row);same['source_shot']['speed']=100;assert native.parse(same)['signature']==value['signature']
for kind,he in [('ARMOR_PIERCING',False),('ARMOR_PIERCING_HE',False),('ARMOR_PIERCING_CR',False),('HOLLOW_CHARGE',False),('HIGH_EXPLOSIVE',True)]:
    test=copy.deepcopy(row);test['source_shot']['shell']['kind']=kind;test['is_he']=he;test['splash_radius']=10000./5555. if he else 0.
    assert native.parse(test)
test=copy.deepcopy(row);test['source_shot']['shell']['kind']='HIGH_EXPLOSIVE';test['is_he']=True
try:native.parse(test)
except ValueError:pass
else:raise AssertionError('HE zero raw radius lost its effective fallback')
for field,bad in [('burst_index',1),('burst_count',65),('shot_seq',9),('gravity',float('nan')),('penetration_factor',-1.),('max_time_ms',20001),('origin',[0.,0.]),('fire_intent_seq',5),('bot_presentation_time_us',-1),('is_he',1)]:
    test=copy.deepcopy(row);test[field]=bad
    try:native.parse(test)
    except (ValueError,TypeError):pass
    else:raise AssertionError((field,bad))
class Number(object):
    def __float__(self):raise AssertionError('Python conversion callback ran')
test=copy.deepcopy(row);test['gravity']=Number()
try:native.parse(test)
except ValueError:pass
else:raise AssertionError('nonplain numeric admitted')
player=copy.deepcopy(row);player['shooter_kind']='player';assert native.parse(player) is None
bounce=copy.deepcopy(row);bounce.update(ricochet_count=1,segment_start_time_ms=200,checked_through_ms=200,segment_velocity=[0.,0.,-100.],base_penetration_multiplier=.75);assert native.parse(bounce)['segment']==1
rows=native.packed();assert len(rows)==1 and rows[0][:2]==(13,'resolve')
w=rows[0][2];assert w['authority_epoch']==7 and w['impact']==[1.,2.,3.] and w['hit_vehicle'] and 'wreck_hit' not in w
e=w['direct'];assert e['critical_target_base_revision']==15 and e['critical_target_ack_seq']==9 and e['hull_damage']==11
assert e['critical_delta']==dict(devices=[dict(name='engineHealth',hp_loss=12.346)],crew_ko=['loader1'],ignite=True)
assert e['critical']['crew_roster']==['commander','driver','loader1','gunner1']
source=sys.argv[2]+'/src/res/scripts/client';sys.path.insert(0,source)
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[source+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import lan_client
from gui.mods.offline_lan_0922.authority_worker import AuthorityWorkerLANClient
samples=[]
for human in (False,True):
 cls=lan_client.LANClient if human else AuthorityWorkerLANClient
 client=object.__new__(cls);client.ready=True;client.phase='battle';client.round_id=1;client.authority_epoch=7
 client.player_id=1 if human else lan_client.WORKER_AUTHORITY_ID;client.bot_authority_id=lan_client.WORKER_AUTHORITY_ID
 client.server_capabilities=(lan_client.PROJECTILE_HIT_VEHICLE_CAPABILITY,lan_client.PROJECTILE_WRECK_HIT_CAPABILITY)
 captured=[];client._send=lambda message:captured.append(copy.deepcopy(message))or True
 for serial,kind,packed in native.terminal_kinds():
  raw=copy.deepcopy(packed);name=(raw['cursors'][0]if kind=='progress'else raw)['projectile_id']
  projectile='1:%s:1:%d'%('p'if human else'b',serial)
  cursor=raw['cursors'][0]if kind=='progress'else raw;cursor['projectile_id']=projectile
  frozen=copy.deepcopy(row);frozen.update(projectile_id=projectile,shooter_kind='player'if human else'bot',shooter_id=1,shot_seq=serial,burst_group_seq=serial,fire_intent_seq=serial,fire_input_seq=serial)
  proof=dict((key,frozen[key])for key in lan_client.PROJECTILE_LAUNCH_PROOF_FIELDS)
  if human:cursor['launch_proof']=proof
  else:frozen.pop('fire_intent_seq');frozen.pop('fire_input_seq')
  raw.update(type='projectile_'+kind,round_id=1)
  arguments=copy.deepcopy(raw);arguments.pop('type');arguments.pop('round_id')
  method=getattr(client,'send_projectile_'+kind)
  assert method(**arguments),(name,human,raw)
  normalized=captured.pop();assert json.loads(json.dumps(normalized))==json.loads(json.dumps(raw)),(name,human,normalized,raw)
  if kind=='resolve':
   if name.startswith('wreck_'):assert raw['wreck_hit']==dict(target_kind='player'if name=='wreck_player'else'bot',target_id=4)
   else:assert 'wreck_hit'not in raw
   assert(raw['impact']is None)==(raw['outcome']!='impact')
  samples.append(dict(name=name,human=human,kind=kind,raw=raw,normalized=normalized,record=frozen))
 with open(sys.argv[1],'w')as output:json.dump(samples,output)
print('CPython 2.7 projectile ingress and publication passed')
'''
SERVER_CONTRACT=r'''
import copy,json,sys
sys.path.insert(0,sys.argv[1])
import lan_battle_server as server
class Socket:
 def sendall(self,*args):pass
def state_for(sample):
 s=server.BattleState(map_name='04_himmelsdorf');s.client_build=server.CLIENT_BUILD_0922;s.phase='battle';s.round_id=1;s.tick=int(round(server.PREBATTLE_SECONDS*server.TICK_HZ));s.authority_epoch=7
 for identity in (1,4):s.players[identity]=server.Player(identity,Socket(),('127.0.0.1',identity),team=1 if identity==1 else 2)
 s.simulation_worker=server.SimulationWorker(Socket(),('127.0.0.1',28782));s.bot_authority_id=server.SIMULATION_WORKER_AUTHORITY_ID
 s.bot_states={1:dict(id=1,alive=True,health=1000,team=1),4:dict(id=4,alive=sample['name']!='wreck_bot',health=1000,team=2)}
 s.players[4].alive=sample['name']!='wreck_player'
 record=copy.deepcopy(sample['record']);record.update(team=1,checked_through_ms=0,checked_distance=0.,piercing_loss=0.,launch_fingerprint='frozen')
 s.projectiles={record['projectile_id']:record}
 # A terminal must not finish this synthetic two-target test room. All wire,
 # ownership, frozen proof, effect and tombstone readers remain production.
 s._maybe_finish_battle=lambda:None
 return s
accepted=0;rejected=0
for sample in json.load(open(sys.argv[2])):
 sender=1 if sample['human']else server.SIMULATION_WORKER_AUTHORITY_ID
 for name in ('raw','normalized'):
  wire=copy.deepcopy(sample[name]);s=state_for(sample)
  method=getattr(s,'progress_projectiles'if sample['kind']=='progress'else sample['kind']+'_projectile')
  assert method(sender,wire),(sample['name'],sample['human'],name,getattr(s,'last_projectile_'+sample['kind']+'_reject',''))
  accepted+=1;projectile=sample['record']['projectile_id']
  if sample['kind']=='resolve':
   event=next(e for e in s.pending_events if e.get('kind')=='projectile_impact')
   assert event['outcome']==wire['outcome']and event['resolved_time_ms']==98,(sample,event)
   assert projectile not in s.projectiles and s.projectile_tombstones[projectile]['outcome']==wire['outcome']
   if wire.get('direct'):assert (s.players[4].health if wire['direct']['target_kind']=='player'else s.bot_states[4]['health'])==990
   if wire.get('splash'):assert s.bot_states[4]['health']==997
  else:
   assert s.projectiles[projectile]['checked_through_ms']==98
   if sample['kind']=='ricochet':assert s.projectiles[projectile]['ricochet_count']==1
  assert method(sender,wire),'frozen retry was not idempotent'
 if sample['kind']=='resolve'and 'wreck_hit'not in sample['raw']:
  invalid=copy.deepcopy(sample['raw']);invalid['wreck_hit']=None;s=state_for(sample)
  assert not s.resolve_projectile(sender,invalid)
  assert s.last_projectile_resolve_reject=='invalid wreck impact shape',s.last_projectile_resolve_reject
  assert s.pending_events[-1]['outcome']=='expired'and s.pending_events[-1]['resolved_time_ms']==0
  rejected+=1
print('Actual server reader: %d dispatch accepts, %d null-wreck rejections; real effects/frontiers/tombstones/retries passed'%(accepted,rejected))
'''
@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'fixed gameplay source is required')
class NativeProjectileWire(unittest.TestCase):
    def test_wire(self):
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        names=['stock_native_projectile_wire','stock_native_projectile_owner','stock_native_projectile_cursor','stock_native_projectile_history','stock_native_projectile_chord','stock_native_projectile_damage','stock_native_bot_armor_law','stock_native_bot_combat','stock_native_client_state']
        with tempfile.TemporaryDirectory(prefix='native-projectile-wire-') as folder:
            folder=Path(folder);binary=folder/'projectile_wire_test.so';script=folder/'fixture.py';script.write_text(SCRIPT)
            command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-DWOT_CLIENT_REPLICA_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native'),str(ROOT/'tests/stock_native_projectile_wire_host.cpp')]+[str(ROOT/'bigworld'/(name+'.cpp')) for name in names]+['-o',str(binary)]
            subprocess.run(command,check=True);samples=folder/'wire_samples.json';subprocess.run([py27,'-B',str(script),str(samples),str(fork)],check=True)
            fixed_server=os.environ.get('WOT_FORK_0101_SERVER')
            if fixed_server:
                server=Path(fixed_server).parent if Path(fixed_server).is_file()else Path(fixed_server)
                subprocess.run([sys.executable,'-B','-c',SERVER_CONTRACT,str(server),str(samples)],check=True)
            else:
                print('Pinned server acceptance requires WOT_FORK_0101_SERVER; native wire and original sender checks passed')
if __name__=='__main__':unittest.main()
