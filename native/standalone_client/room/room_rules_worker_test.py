"""Worker ingress against the staged server; this is portable source evidence."""
import copy
import contextlib
import importlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOM=Path(__file__).resolve().parent
PIN=os.environ.get('WOT_FORK_SOURCE')
@unittest.skipUnless(PIN,'WOT_FORK_SOURCE supplies the pinned native laws')
class WorkerIngress(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory(prefix='room-worker-');cls.addClassCleanup(cls.folder.cleanup)
  cls.resources=Path(cls.folder.name)/'resources.json';cls.binary=Path(cls.folder.name)/'worker'
  shared_resources=os.environ.get('WOT_ROOM_TEST_RESOURCES')
  if shared_resources:cls.resources=Path(shared_resources)
  else:
      subprocess.run([sys.executable,'-B',str(ROOM/'bake_resources.py'),PIN,str(cls.resources)],check=True,capture_output=True)
  sources=[]
  for pattern in ('room_core*.cpp','room_rules*.cpp','room_strategy*.cpp'):
   sources.extend(p for p in ROOM.glob(pattern) if not p.name.endswith(('_probe.cpp','_test_host.cpp')))
  sources.extend(ROOM/name for name in ('room_inputs.cpp','room_json.cpp','room_lobby_policy.cpp','room_bot_rows.cpp','room_publication_clock.cpp'))
  prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
  if prebuilt:
      cls.binary=Path(prebuilt)/'worker'
  else:
      subprocess.run(['c++','-std=c++17','-O0','-ffunction-sections','-fdata-sections',('-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections'),'-ffp-contract=off','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(Path(PIN)/'native'),str(ROOM/'room_rules_worker_test_host.cpp'),*(str(p) for p in sources),str(ROOM.parent/'bigworld/stock_native_bot_combat.cpp'),str(Path(PIN)/'native/offline_contact_roster.cpp'),'-o',str(cls.binary)],check=True)
  stage=Path(cls.folder.name)/'server'
  subprocess.run([sys.executable,'-B',str(ROOM.parent/'wire/stage_server.py'),str(Path(PIN)/'server'),str(stage)],check=True)
  sys.path[:0]=[str(stage),str(Path(PIN)/'tests'),str(Path(PIN)/'src/res/scripts/client')]
  cls.server=importlib.import_module('lan_battle_server');cls.fixture=importlib.import_module('test_port_0922_server_projectiles');cls.codec=importlib.import_module('bot_state_rows')
 def run_native(self,roster,messages,**kwargs):
  p=subprocess.run([str(self.binary),str(self.resources)],input=json.dumps(dict(roster=roster,messages=messages,**kwargs))+'\n',capture_output=True,text=True,check=True)
  out=json.loads(p.stdout);self.assertNotIn('error',out)
  sys.path.insert(0,str(ROOM.parent/'wire'))
  from schema import SCHEMA
  snapshot=copy.deepcopy(out['snapshot']);snapshot.pop('_room_sections',None);SCHEMA.encode(snapshot)
  events=copy.deepcopy(out['events'])
  self.assertIn('bot_manifest',{event['kind'] for event in events})
  for index,event in enumerate(events):event['event_id']='1:300:%d'%index
  if events:SCHEMA.encode(dict(type='events',round_id=1,server_tick=300,authority_epoch=1,
      server_time_ms=10000,worker_status='connected',worker_failure_reason='',events=events))
  return out
 def fixture_state(self):
  state=self.fixture._state(players=0)
  roster=[dict(id=1,team=1,slot=0,name='One'),dict(id=16,team=2,slot=0,name='Two')]
  state.bot_roster=roster
  bots=[dict(row,vehicle='ussr:R11_MS-1',health=1000,max_health=1000,x=10.,y=0.,z=0.,yaw=0.,world_pose=True,profile={},reload_time=0.,reload_duration=1.5,route=dict(id='test',waypoints=[])) for row in roster]
  message=dict(type='bot_manifest',round_id=1,bots=bots,player_collision_profiles=[])
  return state,roster,message
 def test_manifest_defaults_and_exact_duplicate_match_original(self):
  state,roster,message=self.fixture_state()
  with contextlib.redirect_stdout(io.StringIO()):self.assertTrue(state.update_bot_manifest(-1,message))
  actual=self.run_native(roster,[message,message])
  self.assertEqual([True,True],[r['accepted'] for r in actual['results']],actual)
  expected=copy.deepcopy(state.bot_manifest)
  for row in actual['manifest']:row.pop('vehicle_class',None)
  self.assertEqual(expected,actual['manifest'])
  self.assertEqual(list(state.bot_states.values()),actual['bots'])
 def test_manifest_full_fittings_equipment_and_optional_data_match_original(self):
  fittings=importlib.import_module('gui.mods.offline_lan_0922.bot_fittings')
  for case in range(8):
   state,roster,message=self.fixture_state()
   raw=message['bots'][0]
   raw.update(x=3000.123456,y=-2000.,z=-3000.,yaw=15.123456,pitch=7.25,roll=-7.25,
    aim_yaw=-10.,gun_pitch=2.,speed=100.,movement_dir=-0.02,rotation_dir=0.01,
    profile=dict(class_tag='lightTank',desired_range=2500,roles={'screen':1.5},
      ammo_limit=100.,ammo_ratios=[0,3,6],burst_size=64,clip_size=255,
      shells=[dict(index=0,name='_75mm_Sprg.18',kind='HIGH_EXPLOSIVE',damage=200.,penetration=30.,speed=600.)]),
    route=dict(id='authored-route',capacity=99,risk=2.,role_weights={'screen':1.5},
      waypoints=[dict(x=2500.,y=2000.,z=-2500.,hold=True)]))
   if case%2:
    raw['equipment_states']=[dict(equipment=contract,usesLeft=1,cooldownTimeLeft=0.,
       active=False,autoPendingElapsed=None,aiPendingElapsed=None) for contract in self.codec.PLACEHOLDER_CONTRACTS]
   if case>=2:
    tier=('rookie','regular','veteran','elite')[case%4]
    plan=fittings.plan(raw['vehicle'],tier)
    raw['skill']=tier
    raw['fitting']=dict(schema=1,vehicle=raw['vehicle'],tier=tier,crew_level=plan['crew'],
      descriptor='AQI=',devices=plan['devices'],directive=plan['directive'],style=plan['style'])
   with contextlib.redirect_stdout(io.StringIO()):self.assertTrue(state.update_bot_manifest(-1,message),case)
   actual=self.run_native(roster,[message])
   self.assertTrue(actual['results'][0]['accepted'],(case,actual))
   self.assertEqual(state.bot_manifest,actual['manifest'],case)
   self.assertEqual(list(state.bot_states.values()),actual['bots'],case)
 def test_bad_manifest_is_local_and_cannot_freeze_a_later_valid_manifest(self):
  changes=[dict(reload_duration=0.),dict(team=2),dict(route=dict(waypoints=[{}]*17)),
           dict(ammo_remaining=[],next_shell_index=0,ammo_reload_pending=False),
           dict(clip=2,clip_size=1),dict(siege_state=2,siege_time_left_ms=0,siege_transition_total_ms=0)]
  for change in changes:
   state,roster,message=self.fixture_state();bad=copy.deepcopy(message);bad['bots'][0].update(change)
   with contextlib.redirect_stdout(io.StringIO()):
    try:accepted=state.update_bot_manifest(-1,bad)
    except ValueError:accepted=False
   self.assertFalse(accepted,change)
   actual=self.run_native(roster,[bad,message])
   self.assertEqual([False,True],[r['accepted'] for r in actual['results']],(change,actual))

 def test_bad_batch_does_not_commit_clock_and_unavailable_keeps_ledgers(self):
  state,roster,message=self.fixture_state()
  with contextlib.redirect_stdout(io.StringIO()):self.assertTrue(state.update_bot_manifest(-1,message))
  raw=copy.deepcopy(list(state.bot_states.values()))
  for row in raw:
   row.update(combat_seq=1,combat_base_revision=0)
   if not row.get('ammo_remaining'):
    for key in ('ammo_remaining','next_shell_index','ammo_reload_pending'):row.pop(key,None)
  rows=[self.codec.row(r) for r in raw]
  bad=dict(type='bot_state',round_id=1,authority_epoch=1,sample_time_us=100000,source_batch_horizon_us=100000,rows=[rows[0],rows[0]])
  valid=dict(bad,rows=[[1],rows[1]])
  actual=self.run_native(roster,[message,bad,valid])
  self.assertEqual([True,False,True],[r['accepted'] for r in actual['results']],actual)
  self.assertEqual(1,actual['bot_state_revision'])
  self.assertEqual(0,actual['bots'][0]['combat_ack_seq'])
  self.assertEqual(1,actual['bots'][1]['combat_ack_seq'])
if __name__=='__main__':unittest.main()
