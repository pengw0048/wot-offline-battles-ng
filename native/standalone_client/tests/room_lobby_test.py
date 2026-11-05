"""Portable lobby component parity; this does not exercise the room network or engine."""
import concurrent.futures
import copy
import json
import os
import random
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1];ROOM=ROOT/'room'
FORK=os.environ.get('WOT_FORK_SOURCE');RESOURCES=os.environ.get('WOT_ROOM_RESOURCES','/tmp/wot-native-room-resources.json')
ORACLE=r'''
import ast,copy,inspect,json,sys,textwrap,types
sys.path[:0]=[sys.argv[1],sys.argv[2],sys.argv[3]]
import lan_battle_server as s
# Execute the original producer statements instead of maintaining a second
# handwritten welcome schema that could omit the same fields as the port.
handle=ast.parse(textwrap.dedent(inspect.getsource(s.ClientHandler.handle)))
welcome_statements=None
for parent in ast.walk(handle):
 body=getattr(parent,'body',None)
 if not isinstance(body,list):continue
 for index,node in enumerate(body):
  if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='welcome_message' for t in node.targets):
   selected=[node]
   for following in body[index+1:]:
    call=following.value if isinstance(following,ast.Expr) else None
    if not (isinstance(call,ast.Call) and isinstance(call.func,ast.Attribute) and isinstance(call.func.value,ast.Name) and call.func.value.id=='welcome_message' and call.func.attr=='update'):break
    selected.append(following)
   assert welcome_statements is None
   welcome_statements=selected
assert welcome_statements is not None and len(welcome_statements)>=3
welcome_code=compile(ast.fix_missing_locations(ast.Module(body=welcome_statements,type_ignores=[])),'<original ClientHandler.handle welcome>','exec')
request=json.load(sys.stdin);config=request['config']
state=s.BattleState(team1_size=config.get('team1_size',15),team2_size=config.get('team2_size',15),bot_lineup=config.get('bot_lineup'),bot_excluded_vehicles=config.get('bot_excluded_vehicles'),clock=lambda:100.)
state.map_name='04_himmelsdorf';state.receipt_namespace='test';state.receipt_arena_prefix=1
players={};peers={};output=[]
for op in request['operations']:
 deliveries=[];kind=op['type'];peer=op.get('peer',1)
 if kind=='join':
  msg=op['message']
  if msg.get('role')=='simulation_worker':
   p=s.SimulationWorker(None,(op.get('address','127.0.0.1'),0),capabilities=msg['capabilities']);state.simulation_worker=p;state._elect_bot_authority();peers[peer]=p
  else:
   p,error=state.add_player(None,(op.get('address','127.0.0.1'),0),msg)
   if p is None:deliveries.append([peer,dict(type='error',code=error)])
   else:
    peers[peer]=p;players[p.player_id]=p
    namespace=dict(server=types.SimpleNamespace(state=state),player=p)
    exec(welcome_code,s.__dict__,namespace)
    deliveries.append([peer,namespace['welcome_message']])
  for key,p in peers.items():p.offer_reliable=lambda m,key=key:deliveries.append([key,m]) or True
 elif kind=='command':
  p=peers.get(peer);m=op['message'];t=m['type'];error=None
  if t=='descriptor_catalog':state.store_vehicle_catalog(p.player_id,m)
  elif t=='select_vehicle':state.select_vehicle(p.player_id,m)
  elif t=='set_team_class':state.set_team_class(p.player_id,m.get('team'),m.get('class_tag'),m.get('allowed'))
  elif t=='select_team':ok,error=state.select_team(p.player_id,m.get('team'))
  elif t=='set_team_size':ok,error=state.set_team_size(p.player_id,m.get('team'),m.get('size'))
  elif t=='set_bot_skill_mode':ok,error=state.set_bot_skill_mode(p.player_id,m.get('mode'))
  elif t=='set_bot_tier_mode':ok,error=state.set_bot_tier_mode(p.player_id,m.get('mode'))
  if error:deliveries.append([peer,dict(type={'select_team':'team_denied','set_team_size':'team_size_denied','set_bot_skill_mode':'bot_skill_mode_denied','set_bot_tier_mode':'bot_tier_mode_denied'}[t],code=error)])
 elif kind=='start':
  m=op['message'];result,error=state.request_start(peers[peer].player_id,m.get('map'),m.get('round_seconds'),m.get('battle_mode','regular'),m.get('training_bots',False))
  if error:deliveries.append([peer,dict(type='start_denied',code=error,class_error=state.last_bot_class_error)])
  else:
   for key in peers:deliveries.append([key,result])
 elif kind=='reset':state._reset_round()
 elif kind=='patch':
  for key,patch in op.get('players',{}).items():
   for k,v in patch.items():setattr(players[int(key)],k,v)
  if 'phase' in op:state.phase=op['phase']
 snapshot=state.lobby_message();snapshot['bots']=[{k:b[k] for k in ('id','team','slot')} for b in state.bot_roster];snapshot['deliveries']=deliveries
 output.append(snapshot)
print(json.dumps(output))
'''
@unittest.skipUnless(FORK,'Pinned gameplay source required')
class Lobby(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory(prefix='room-lobby-');cls.addClassCleanup(cls.folder.cleanup);folder=Path(cls.folder.name);cls.binary=folder/'lobby'
  cls.resources_path=folder/'resources.json'
  shared_resources=os.environ.get('WOT_ROOM_TEST_RESOURCES')
  if shared_resources:cls.resources_path=Path(shared_resources)
  else:
      subprocess.run([sys.executable,'-B',str(ROOM/'bake_resources.py'),FORK,str(cls.resources_path)],check=True,capture_output=True,text=True)
  sys.path.insert(0,str(ROOT/'wire'))
  try:
   import stage_server
   stage_server.stage(str(Path(FORK)/'server'),str(folder/'server'))
  finally:sys.path.remove(str(ROOT/'wire'))
  prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
  if prebuilt:cls.binary=Path(prebuilt)/'lobby'
  else:
   # The Core command dispatcher belongs to the independent network lane. Any
   # accidental call is a hard test failure, never an accepting stub.
   bridge=folder/'unlinked_dispatch.cpp';bridge.write_text('#include "room_core.hpp"\nnamespace wot::room {void Core::command(Peer,Connection&,const Value&){throw std::logic_error("Network dispatcher is outside portable lobby component scope");}}\n')
   sources=[ROOM/'room_core.cpp',ROOM/'room_core_lobby.cpp',ROOM/'room_lobby_policy.cpp',ROOM/'room_inputs.cpp',ROOM/'room_json.cpp',ROOM/'room_publication_clock.cpp',ROOT/'tests/room_lobby_probe.cpp',bridge]
   sources += [p for p in ROOM.glob('room_rules*.cpp') if not p.name.endswith(('_test_host.cpp','_probe.cpp'))]
   sources += [p for p in ROOM.glob('room_strategy*.cpp') if not p.name.endswith('_probe.cpp')]
   sources += [ROOT/'bigworld/stock_native_bot_combat.cpp',Path(FORK)/'native/offline_contact_roster.cpp']
   def compile_one(item):
    index,source=item;target=folder/('%d.o'%index)
    command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOM),'-I'+str(Path(FORK)/'native'),'-c',str(source),'-o',str(target)]
    result=subprocess.run(command,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stderr)
    return target
   with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_one,enumerate(sources)))
   subprocess.run(['c++',*(str(p) for p in objects),'-o',str(cls.binary)],check=True,capture_output=True,text=True)
  code="import sys,json;sys.path[:0]=sys.argv[1:];from effective_params_fixture import effective_params;from gui.mods.offline_lan_0922 import effective_params as e;print(json.dumps(e.canonical(effective_params())))"
  cls.params=json.loads(subprocess.run([sys.executable,'-I','-B','-c',code,str(Path(FORK)/'tests'),str(Path(FORK)/'src/res/scripts/client')],capture_output=True,text=True,check=True).stdout)
  cls.resources=json.loads(cls.resources_path.read_text());cls.caps=cls.resources['required_capabilities']
 def hello(self,peer=1,**changes):
  m=dict(type='hello',role='player',protocol=5,client_build=self.resources['client_build'],capabilities=self.caps,name='Player%d'%peer,account_key='account%d'%peer,vehicle='ussr:R11_MS-1',max_health=1000,vehicle_compact_descr='AA==',effective_params=self.params,requested_team=1);m.update(changes)
  return dict(type='join',peer=peer,message=m,address='192.168.1.%d'%peer)
 def worker(self):return self.hello(0,role='simulation_worker')
 def command(self,t,peer=1,**changes):return dict(type='command',peer=peer,message=dict(type=t,**changes))
 def start(self,**changes):
  message=dict(type='start_battle',map='04_himmelsdorf');message.update(changes);return dict(type='start',message=message)
 def compare(self,operations,valid_param_variants=(),**options):
  config=dict(schema=1,build_identity='test',semantic_version='test',team1_size=5,team2_size=5);config.update(options)
  missing_critical=copy.deepcopy(self.params);missing_critical.pop('critical')
  request=dict(config=config,valid_params=self.params,valid_param_variants=[missing_critical]+list(valid_param_variants),operations=operations)
  expected=subprocess.run([sys.executable,'-I','-B','-c',ORACLE,str(Path(self.folder.name)/'server'),str(Path(FORK)/'src/res/scripts/client'),str(Path(FORK)/'tests')],input=json.dumps(request),capture_output=True,text=True,check=True)
  left=json.loads(expected.stdout.splitlines()[-1]);result=subprocess.run([str(self.binary),str(self.resources_path)],input=json.dumps(request)+'\n',capture_output=True,text=True,check=True)
  right=json.loads(result.stdout)
  for index,(a,b) in enumerate(zip(left,right)):
   with self.subTest(index=index,operation=operations[index]['type']):
    for field in ('phase','round_id','state_revision','host_player_id','team_sizes','team_size','lineup_locked','team_classes','team_class_notice','bot_tier_mode','bot_skill_mode'):
     self.assertEqual(a.get(field),b.get(field),field)
    # Both sides independently shuffle Chinese callsigns; compare roster slot
    # identities, and test callsign membership/uniqueness separately.
    if a['phase']!='waiting':self.assertEqual(a['bots'],b['bots'],'bot roster')
    self.assertEqual(len(a['players']),len(b['players']))
    for p,q in zip(a['players'],b['players']):
     for field in ('id','name','vehicle','requested_team','team','slot','x','y','z','yaw','aim_yaw','health','max_health','display_health','marks_on_gun','outfits','vehicle_compact_descr','effective_params','participating','alive','siege_state','siege_time_left_ms','critical','equipment_states','equipment_revision','critical_revision','critical_base_revision','critical_ack_seq'):
      self.assertEqual(p.get(field),q.get(field),'player '+field)
     p=copy.deepcopy(p);q=copy.deepcopy(q)
     for row in (p,q):
      receipt=row.get('crew_receipt_id')
      if receipt is not None:row['crew_receipt_id']='namespace:'+':'.join(receipt.split(':')[-2:])
     self.assertEqual(p,q,'complete public player row')
    def messages(v):return [[peer,{k:m.get(k) for k in ('type','code','class_error') if k in m}] for peer,m in v if m['type'].endswith('_denied') or m['type']=='error']
    self.assertEqual(messages(a['deliveries']),messages(b['deliveries']),'terminal replies')
    welcomes_a=[(peer,m) for peer,m in a['deliveries'] if m['type']=='welcome' and 'player_id' in m]
    welcomes_b=[(peer,m) for peer,m in b['deliveries'] if m['type']=='welcome' and 'player_id' in m]
    self.assertEqual(len(welcomes_a),len(welcomes_b),'visible welcome count')
    for (source_peer,x),(native_peer,y) in zip(welcomes_a,welcomes_b):
     self.assertEqual(source_peer,native_peer,'welcome recipient')
     self.assertTrue(set(x).issubset(y),'missing source welcome fields: '+repr(sorted(set(x)-set(y))))
     for field,value in x.items():
      if field=='map':
       # Each implementation independently draws a legal room map before the
       # join. The actor assignment and its spawn still compare exactly.
       self.assertIn(y[field],x['map_pool'],'welcome map membership')
      elif field in ('capabilities','server_capabilities'):
       self.assertEqual(sorted(value),sorted(y[field]),'welcome '+field)
      else:self.assertEqual(value,y[field],'welcome '+field)
    starts_a=[m for peer,m in a['deliveries'] if m['type']=='battle_start'];starts_b=[m for peer,m in b['deliveries'] if m['type']=='battle_start'];self.assertEqual(len(starts_a),len(starts_b))
    if starts_a:
     x,y=starts_a[0],starts_b[0]
     self.assertEqual(set(x),set(y),'complete start field contract')
     names=[row['name'] for row in y['bots']]
     self.assertEqual(len(names),len(set(names)),'unique Bot callsigns')
     self.assertTrue(all(name in self.resources['lobby']['callsigns'] for name in names),'pinned Chinese callsigns')
     for field in ('battle_mode','training_bots','map','requested_by','delay','bot_lineup','bot_tactics','bot_excluded_vehicles','bot_manifest','bot_order_revision','bot_orders','rules','battle_result','destructible_revision','destructibles','destructible_base_revision','detached_turrets','authority_epoch'):
      self.assertEqual(x.get(field),y.get(field),'start '+field)
  return right
 def test_admission_validation_identity_name_and_atomic_vehicle_change(self):
  self.compare([self.hello(1,name='Player'),self.hello(2,name='player',requested_team='auto'),self.hello(3,account_key='account1'),self.hello(4,max_health=True),self.hello(5,name=' ΟΣ '),self.hello(6,name=' Ος '),self.hello(7,requested_team='02'),self.hello(8,vehicle_compact_descr='YR=='),self.hello(9,outfits={'1':'YR=='}),self.hello(10,marks_on_gun=4)])
  self.compare([self.hello(),self.command('select_vehicle',vehicle='usa:A01_T1_Cunningham',max_health=450,vehicle_compact_descr='AQ==',effective_params=self.params,outfits={'2':'YR=='},marks_on_gun=2),self.command('select_vehicle',vehicle='bad',max_health=999999,vehicle_compact_descr='AA==',effective_params=self.params)])
 def test_team_size_class_trim_presets_and_denial_order(self):
  ops=[self.hello(),self.hello(2,requested_team=2),self.command('set_team_size',team=1,size=1),self.command('set_team_class',team=1,class_tag='random',allowed=False),self.command('set_team_size',peer=2,team=True,size=0),self.command('select_team',peer=2,team=1),self.command('select_team',team='auto'),self.command('select_team',peer=2,team=1),self.command('set_bot_skill_mode',mode='hard'),self.command('set_bot_tier_mode',mode='minus1_plus1'),self.command('set_bot_tier_mode',peer=2,mode='bad'),self.command('set_team_size',team=1,size=0)]
  self.compare(ops)
 def test_complete_start_preflight_and_round_reset(self):
  self.compare([self.hello(),self.start(round_seconds=59),self.start(),self.worker(),self.start(round_seconds=60,battle_mode='training',training_bots=False),dict(type='reset'),self.start(battle_mode='training'),dict(type='reset')])
 def test_catalog_classes_tier_windows_and_explicit_filter_lineups(self):
  catalog=[dict(name='ussr:R11_MS-1',level=1,tags=['lightTank']),dict(name='usa:A01_T1_Cunningham',level=1,tags=['lightTank']),dict(name='ussr:R03_BT-7',level=3,tags=['lightTank'])]
  self.compare([self.hello(),self.worker(),self.command('descriptor_catalog',vehicles=catalog),self.start(),dict(type='reset'),self.command('set_team_class',team=1,class_tag='random',allowed=False),self.command('set_team_class',team=1,class_tag='heavyTank',allowed=True),self.start(),self.command('set_team_class',team=1,class_tag='heavyTank',allowed=False),self.start(battle_mode='training',training_bots=False)])
  lineup=[dict(team=1,slot=4,enabled=True),dict(team=1,slot=8,enabled=True,nation='usa',vehicle_class='lightTank'),dict(team=2,slot=2,enabled=True)]
  self.compare([self.hello(),self.worker(),self.command('set_team_size',team=1,size=8),self.start(),self.command('descriptor_catalog',vehicles=catalog),self.start(),dict(type='reset')],bot_lineup=lineup)
 def test_varied_waiting_room_decisions_keep_exact_revision_and_denials(self):
  rng=random.Random(1513);classes=['random','lightTank','mediumTank','heavyTank','AT-SPG','SPG']
  ops=[self.hello(),self.hello(2,requested_team=2),self.hello(3,requested_team=1)]
  for _ in range(180):
   peer=rng.choice([1,2,3]);kind=rng.randrange(5)
   if kind==0:ops.append(self.command('select_team',peer=peer,team=rng.choice([0,1,2,'auto',' 1 ',False,True,1.0,'bad'])))
   elif kind==1:ops.append(self.command('set_team_size',peer=peer,team=rng.choice([1,2,0,True,1.0]),size=rng.choice([0,1,2,3,5,15,16,True,2.0])))
   elif kind==2:ops.append(self.command('set_team_class',peer=peer,team=rng.choice([1,2,True]),class_tag=rng.choice(classes),allowed=rng.choice([True,False,0,None])))
   elif kind==3:ops.append(self.command('set_bot_skill_mode',peer=peer,mode=rng.choice(['easy','relaxed','mixed','hard','brutal','bad',None])))
   else:ops.append(self.command('set_bot_tier_mode',peer=peer,mode=rng.choice(['random','same','minus1_0','0_plus1','minus1_plus1','minus2_0','0_plus2','bad'])))
  self.compare(ops)
 def test_automatic_choices_remain_hidden_through_rejected_start(self):
  self.compare([self.hello(requested_team=0),self.hello(2,requested_team=2),self.start(),self.worker(),self.start(map='invalid_map'),self.start(round_seconds=True),self.start(battle_mode='training',training_bots=False)],team1_size=1,team2_size=1)
 def test_empty_explicit_teams_and_invalid_pin_preflight(self):
  self.compare([self.hello(),self.worker(),self.start()],bot_lineup=[dict(team=1,slot=0,enabled=False),dict(team=2,slot=0,enabled=False)])
  self.compare([self.hello(),self.worker(),self.start()],bot_lineup=[dict(team=2,slot=4,vehicle='usa:A01_T1_Cunningham')])
 def test_failed_equipment_preflight_preserves_earlier_installed_player(self):
  params=copy.deepcopy(self.params);params.pop('critical')
  self.compare([self.hello(),self.hello(2,effective_params=params,requested_team=2),self.worker(),self.start(battle_mode='training',training_bots=False),self.command('select_vehicle',peer=2,vehicle='ussr:R11_MS-1',max_health=900,vehicle_compact_descr='AA==',effective_params=self.params),self.start(battle_mode='training',training_bots=False)])
 def test_visible_welcome_matches_original_handler_for_fixed_pending_unicode_and_params(self):
  heavier=copy.deepcopy(self.params);heavier['physics']['mass']=12000.25
  snapshots=self.compare([self.hello(1,name='  坦克 ΟΣ  ',max_health=450,outfits={'1':'YR=='}),
   self.hello(2,name='未分配 İ',requested_team='auto',vehicle='usa:A01_T1_Cunningham',max_health=1234,vehicle_compact_descr='AQ==',effective_params=heavier),
   self.worker(),self.hello(3,name='Team Two',requested_team=2,max_health=900),
   self.hello(4,name='Second Slot',requested_team=1)],valid_param_variants=[heavier])
  welcomes=[m for snapshot in snapshots for peer,m in snapshot['deliveries'] if m['type']=='welcome' and 'player_id' in m]
  self.assertEqual(4,len(welcomes))
  self.assertEqual((0,-1,0.,0.,0.),(welcomes[1]['team'],welcomes[1]['slot'],welcomes[1]['spawn']['x'],welcomes[1]['spawn']['z'],welcomes[1]['spawn']['yaw']))
  self.assertEqual(12000.25,welcomes[1]['effective_params']['physics']['mass'])
  self.assertEqual({'1':'YQ=='},welcomes[0]['outfits'])
  self.check_real_welcome_consumers(welcomes)
 def test_visible_welcome_preserves_explicit_lineup_slot_spawn(self):
  lineup=[dict(team=1,slot=4,enabled=True),dict(team=1,slot=8,enabled=True),dict(team=2,slot=14,enabled=True)]
  snapshots=self.compare([self.hello(1,name='Slot Four'),self.hello(2,name='Slot Eight'),self.hello(3,name='Slot Fourteen',requested_team=2)],bot_lineup=lineup)
  welcomes=[m for snapshot in snapshots for peer,m in snapshot['deliveries'] if m['type']=='welcome' and 'player_id' in m]
  self.assertEqual([4,8,14],[m['slot'] for m in welcomes])
  self.assertEqual([48.,96.,168.],[m['spawn']['x'] for m in welcomes])
  self.check_real_welcome_consumers(welcomes)
 def check_real_welcome_consumers(self,welcomes):
  # The pinned LANClient executes its own constructor and _handle_message.
  # No accepting fake or replaced stop/validation method is
  # involved; these instances never open a socket or schedule engine callbacks.
  script=r"""
import copy,json,sys
sys.path.insert(0,sys.argv[1])
from gui.mods.offline_lan_0922 import lan_client as original
request=json.load(sys.stdin);out=[]
for implementation in (original,):
 for message in request['welcomes']:
  client=implementation.LANClient('127.0.0.1',28792,'Before welcome','ussr:R11_MS-1',vehicle_compact_descr='AA==',effective_params=request['params'])
  client._handle_message(message)
  out.append(dict(ready=client.ready,error=client.last_error,player_id=client.player_id,team=client.team,slot=client.slot,spawn=client.spawn,effective_params=client.effective_params))
 # A regression back to the old sparse native welcome must fail the real
 # consumer, so an accidental permissive consumer cannot make this test pass.
 for field in ('team','slot','max_health','spawn','effective_params'):
  message=copy.deepcopy(request['welcomes'][0]);message.pop(field)
  client=implementation.LANClient('127.0.0.1',28792,'Before welcome','ussr:R11_MS-1',vehicle_compact_descr='AA==',effective_params=request['params'])
  client._handle_message(message)
  assert not client.ready and client.last_error=='invalid welcome message',(field,client.ready,client.last_error)
print(json.dumps(out))
"""
  result=subprocess.run([sys.executable,'-I','-B','-c',script,str(Path(FORK)/'src/res/scripts/client')],input=json.dumps(dict(welcomes=welcomes,params=self.params)),capture_output=True,text=True)
  self.assertEqual(0,result.returncode,result.stdout+result.stderr)
  outputs=json.loads(result.stdout.splitlines()[-1]);self.assertEqual(len(welcomes),len(outputs))
  for index,actual in enumerate(outputs):
   expected=welcomes[index%len(welcomes)]
   self.assertTrue(actual['ready'],actual['error']);self.assertFalse(actual['error'])
   for field in ('player_id','team','slot','spawn','effective_params'):self.assertEqual(expected[field],actual[field],field)
if __name__=='__main__':unittest.main()
