"""Lobby policies and wire validation compared with actual staged gameplay laws."""
import copy
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FORK=os.environ.get('WOT_FORK_SOURCE')
RESOURCES=os.environ.get('WOT_ROOM_RESOURCES','/tmp/wot-native-room-resources.json')
ORACLE=r'''
import copy,json,sys
sys.path[:0]=[sys.argv[1],sys.argv[2]]
import lan_battle_server as s
from gui.mods.offline_lan_0922 import team_class_policy as policy
out=[]
for op in json.load(sys.stdin):
 try:
  t=op['type'];v=op.get('value')
  if t=='safe':value=(s._safe_vehicle if op.get('vehicle') else s._safe_name)(v,op.get('fallback',''))
  elif t=='lower':value=v.lower()
  elif t=='team':value=s._requested_team(v)
  elif t=='outfits':value=s._validated_outfits(v)
  elif t=='marks':value=s._validated_marks_on_gun(v)
  elif t=='descriptor':value=s._validated_vehicle_compact_descr(v)
  elif t=='normalize':
   value=copy.deepcopy(v);value['bot_lineup']=s.BattleState._normalize_bot_lineup(v.get('bot_lineup'));value['bot_excluded_vehicles']=s.BattleState._normalize_bot_excluded_vehicles(v.get('bot_excluded_vehicles'))
   explicit=any('enabled' in r for r in value['bot_lineup'])
   for team in (1,2):
    key='team%d_size'%team;size=s._exact_int(v.get(key,15),1,15)
    if explicit:size=sum(r.get('enabled',False) and r['team']==team for r in value['bot_lineup'])
    value[key]=size
  elif t=='catalog':
   state=s.BattleState();state.players[1]=s.Player(1,None,('127.0.0.1',0))
   if not state.store_vehicle_catalog(1,{'vehicles':v}):raise ValueError()
   value=state.vehicle_catalogs[1]
  elif t=='tiers':value=s.bot_planner.bot_match_tiers(op['host'],op['mode'],available_tiers=op.get('available',()),required_tiers=op.get('humans',()))
  elif t=='policy':
   row=copy.deepcopy(op['row']);pinned=op['pinned'];removed=policy.trim(row,op['automatic'],pinned,op.get('keep'))
   limit=policy.artillery_limit(op['size'],op['human_spgs'],pinned.count('SPG'),row)
   value=dict(row=row,removed=removed,required=policy.required(row,pinned),limit=limit,reason=policy.validate(row,op['automatic'],pinned,set(op['candidates']),limit))
  elif t=='matching':value=policy.matching_tiers(op['candidates'],op['needed'],op['humans'],op['proposed'],op['random'],op['groups'],op['slots'])
  out.append(dict(value=value))
 except (ValueError,TypeError,OverflowError):out.append(dict(error=True))
print(json.dumps(out))
'''
@unittest.skipUnless(FORK,'Pinned gameplay source is required')
class LobbyPolicy(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.folder=tempfile.TemporaryDirectory(prefix='room-lobby-policy-');cls.addClassCleanup(cls.folder.cleanup)
  cls.binary=Path(cls.folder.name)/'policy'
  cls.resources_path=Path(cls.folder.name)/'resources.json'
  shared_resources=os.environ.get('WOT_ROOM_TEST_RESOURCES')
  if shared_resources:cls.resources_path=Path(shared_resources)
  else:
    subprocess.run([sys.executable,'-B',str(ROOT/'room/bake_resources.py'),FORK,str(cls.resources_path)],check=True,capture_output=True,text=True)
  prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
  if prebuilt:
      cls.binary=Path(prebuilt)/'policy'
  else:
      subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror',str(ROOT/'tests/room_lobby_policy_probe.cpp'),str(ROOT/'room/room_lobby_policy.cpp'),str(ROOT/'room/room_json.cpp'),'-o',str(cls.binary)],check=True,capture_output=True,text=True)
  sys.path.insert(0,str(ROOT/'wire'))
  try:
   import stage_server
   stage_server.stage(str(Path(FORK)/'server'),str(Path(cls.folder.name)/'server'))
  finally:sys.path.remove(str(ROOT/'wire'))
 def compare(self,operations):
  expected=subprocess.run([sys.executable,'-I','-B','-c',ORACLE,str(Path(self.folder.name)/'server'),str(Path(FORK)/'src/res/scripts/client')],input=json.dumps(operations),capture_output=True,text=True,check=True)
  left=json.loads(expected.stdout.splitlines()[-1])
  right=json.loads(subprocess.run([str(self.binary),str(self.resources_path)],input=json.dumps(operations)+'\n',capture_output=True,text=True,check=True).stdout)
  for op,a,b in zip(operations,left,right):
   with self.subTest(op=op):self.assertEqual(a,b)
 def test_unicode_name_vehicle_and_context_sensitive_lower(self):
  values=[None,False,0,1,1.0,'  Player  ','! foo !','克伦威尔\u2003A','ΑΟΣ','ΟΣ','ΟΣΑ','AΣʰ','AʰΣ',' İIẞΣ ']+['x'*25,'😀','ßÉ中文αβγ','\u00a0A\u0085','ussr:A.12-M_1']
  operations=[]
  for v in values:
   for vehicle in (False,True):operations.append(dict(type='safe',value=v,fallback='fallback',vehicle=vehicle))
  for v in ['ΑΟΣ','ΟΣ','ΟΣΑ','AΣʰ','AʰΣ','Σ','ΣΣ','Σ_Σ','İẞABC','AΣ\u0301A','AΣ\u0301']:
   operations.append(dict(type='lower',value=v))
  self.compare(operations)
 def test_base64_outfits_marks_and_team_validator_boundaries(self):
  ops=[]
  for v in [None,'',False,True,0,0.,1,1.,2,'0','auto',' 1 ','01','+1','\t2\n','\u20031\u00a0',3,{},[]]:ops.append(dict(type='team',value=v))
  for v in [None,False,0,1,3,4,1.,'1']:
   ops.append(dict(type='marks',value=v))
  encoded=['','YQ==','YR==','YWJj','YWJj=','YWJj====','YQ===','YWI=','YWI==','YQ','AA==','!!!!','A===','YW=Jj','中']
  for v in encoded:
   ops.append(dict(type='descriptor',value=v));ops.append(dict(type='outfits',value={'1':v}))
  for v in [None,{},[],{'2':'YQ==','4':'YWI='},{'3':'AA=='},{' 1 ':'YQ=='},{'01':'YQ=='},{'١':'YQ=='},{'１':'YQ=='},{'1':'YQ==','2':'YQ==','4':'YQ==','7':'YQ=='}]:ops.append(dict(type='outfits',value=v))
  self.compare(ops)
 def test_configuration_and_catalog_validation(self):
  rows=[[],[dict(team=1,slot=4,vehicle='usa:A01_T1_Cunningham')],[dict(team=2,slot=14,enabled=True,nation='uk',vehicle_class='SPG')],[dict(team=1,slot=0,enabled=False)],
   [dict(team=1,slot=1,enabled=False,vehicle='ussr:R11_MS-1')],[dict(team=True,slot=0,enabled=True)],[dict(team=1,slot=1.0,enabled=True)],[dict(team=1,slot=0)],[dict(team=1,slot=0,skill='bad')],[dict(team=1,slot=0,enabled=True,nation='bad')],
   [dict(team=1,slot=0,enabled=True),dict(team=1,slot=0,enabled=False)],[dict(team=1,slot=0,vehicle='ussr:A.v1',nation='uk',vehicle_class='SPG',ignored=42)]]
  ops=[dict(type='normalize',value=dict(bot_lineup=v,team1_size=4,team2_size=6)) for v in rows]
  for v in [None,[],[dict(name=' usa:A.1 ',level=1,tags=['lightTank'])],[dict(name='usa:A_1',level=True,tags=[])],[dict(name='usa:A1',level=1.9,tags=[True,None,42])],[dict(name='usa:A1',level=' ٣ ',tags=[])],[dict(name='usa:A1',level=11,tags=[])],[dict(name='usa:A1',level=2,tags=[]) for _ in range(2)]]:ops.append(dict(type='catalog',value=v))
  self.compare(ops)
 def test_tier_modes_class_trimming_quotas_and_matching_windows(self):
  rng=random.Random(16922);classes=['lightTank','mediumTank','heavyTank','AT-SPG','SPG'];ops=[]
  for _ in range(600):
   host=rng.randint(1,10);available=rng.sample(range(1,11),rng.randint(0,10));humans=rng.sample(range(1,11),rng.randint(0,3))
   ops.append(dict(type='tiers',host=host,mode=rng.choice(['random','same','minus1_0','0_plus1','minus1_plus1','minus2_0','0_plus2','bad']),available=available,humans=humans))
   order=rng.sample(classes,rng.randint(0,5));row={tag:tag in order for tag in classes};row.update(random=rng.choice([False,True]),order=order)
   ops.append(dict(type='policy',row=row,pinned=rng.choices(classes,k=rng.randint(0,3)),automatic=rng.randint(0,15),keep=rng.choice(classes+['']),size=rng.randint(1,15),human_spgs=rng.randint(0,3),candidates=rng.sample(classes,rng.randint(0,5))))
   ops.append(dict(type='matching',candidates=[[rng.randint(1,10),rng.choice(classes)] for _ in range(rng.randint(0,30))],needed=rng.sample(classes,rng.randint(0,5)),humans=humans,proposed=available,random=rng.choice([True,False]),groups=[rng.sample(classes,rng.randint(0,5)) for _ in range(rng.randint(0,2))],slots=[rng.sample(range(1,11),rng.randint(0,5)) for _ in range(rng.randint(0,2))]))
  self.compare(ops)
if __name__=='__main__':unittest.main()
