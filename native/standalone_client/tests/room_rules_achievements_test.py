"""Pure settlement parity against pinned predicates, rewards and medal bonds."""
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
ORACLE=r'''
import json,sys
sys.path[:0]=[sys.argv[1]+'/server',sys.argv[1]+'/src/res/scripts/client']
from gui.mods.offline_lan_0922 import battle_achievements as achievements,battle_bonds as bonds
import offline_rewards
for request in json.load(sys.stdin):
    response={}
    try:
        operation=request['operation']
        if operation=='award':
            result=achievements.award_battle_achievements(request['battle'])
            value=dict(('%s:%d'%key,names) for key,names in result.items())
        elif operation=='rewards':value=offline_rewards.compute_offline_rewards(request['stats'],request['won'],request['participated'],request['tier'],request.get('spotted_spgs',0),request.get('capture_participants',0),request.get('killed_durability',0))
        elif operation=='medals':value=bonds.medal_rewards(request['achievements'],request['tier'])
        else:value=bonds.battle_reward(request['xp'],request['eligible'])
        response=dict(ok=True,value=value)
    except Exception:response=dict(ok=False)
    print(json.dumps(response))
'''
FIXTURES=r'''
import ast,io,json,pathlib,sys,unittest
sys.path[:0]=[sys.argv[1]+'/server',sys.argv[1]+'/src/res/scripts/client']
from gui.mods.offline_lan_0922 import battle_achievements
from gui.mods.offline_lan_0922.battle_achievements import RECEIPT_STAT_NAMES,ACHIEVEMENT_CONDITIONS,AWARDABLE_ACHIEVEMENTS
requests=[]
def award_battle_achievements(battle):
    requests.append(dict(operation='award',battle=json.loads(json.dumps(battle))))
    return battle_achievements.award_battle_achievements(battle)
source=pathlib.Path(sys.argv[1]+'/tests/test_port_0922_battle_achievements.py').read_text()
tree=ast.parse(source)
selected=[]
for node in tree.body:
    if isinstance(node,ast.FunctionDef) and node.name in ('_actor','_kill','_awards'):selected.append(node)
    if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='EMPTY_STATS' for t in node.targets):selected.append(node)
    if isinstance(node,ast.ClassDef) and node.name in ('BattleHeroTests','SourcedMedalTests','EpicMedalTests'):selected.append(node)
exec(compile(ast.Module(body=selected,type_ignores=[]),'<pinned award fixtures>','exec'))
suite=unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromTestCase(globals()[name]) for name in ('BattleHeroTests','SourcedMedalTests','EpicMedalTests'))
result=unittest.TextTestRunner(stream=io.StringIO()).run(suite)
assert result.wasSuccessful(),(result.errors,result.failures)
print(json.dumps(requests))
'''
@unittest.skipUnless(FORK,'Pinned gameplay source is required')
class RoomSettlement(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.folder=tempfile.TemporaryDirectory(prefix='room-settlement-')
        cls.addClassCleanup(cls.folder.cleanup)
        cls.executable=Path(cls.folder.name)/'settlement'
        prebuilt=os.environ.get('WOT_ROOM_TEST_BIN')
        if prebuilt:
            cls.executable=Path(prebuilt)/'rewards'
        else:
            subprocess.run(['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-I'+str(ROOT/'room'),
                str(ROOT/'tests/room_rules_achievements_test_cli.cpp'),str(ROOT/'room/room_rules_achievements.cpp'),
                str(ROOT/'room/room_json.cpp'),'-o',str(cls.executable)],check=True,capture_output=True,text=True)
    def compare(self,requests):
        reference=subprocess.run([sys.executable,'-I','-B','-c',ORACLE,FORK],input=json.dumps(requests),capture_output=True,text=True,check=True)
        expected=[json.loads(line) for line in reference.stdout.splitlines()]
        native=subprocess.run([str(self.executable)],input=''.join(json.dumps(r)+'\n' for r in requests),capture_output=True,text=True,check=True)
        actual=[json.loads(line) for line in native.stdout.splitlines()]
        self.assertEqual(len(expected),len(actual))
        for index,(left,right) in enumerate(zip(expected,actual)):
            with self.subTest(index=index,request=requests[index]):self.assertEqual(left,right)
        return expected
    @staticmethod
    def actor(actor_id=1,team=1,**overrides):
        result=dict(actor_kind='player',actor_id=actor_id,team=team,tier=5,vehicle_class='mediumTank',
            health=1000,max_health=1000,survived=True,won=True,xp=500,stats={},kills=[])
        result.update(overrides);return result
    def test_exact_pinned_positive_and_negative_medal_fixtures(self):
        output=subprocess.check_output([sys.executable,'-I','-B','-c',FIXTURES,FORK],text=True)
        requests=json.loads(output)
        self.assertGreater(len(requests),70)
        self.compare(requests)
    def test_commendations_all_tie_breaks_and_threshold_neighbours(self):
        rich=self.actor(stats=dict(kills=4,damage=1500,damage_blocked=1500,assist_radio=1000),
            critical_hits=5,duelist_sources=[['bot',1],['bot',2]],damage_sources=[['bot',i] for i in range(1,5)],
            kills=[dict(victim_kind='bot',victim_id=1,ammo_rack=True,death_reason=1),dict(victim_kind='bot',victim_id=2)])
        enemy=self.actor(1,2,actor_kind='bot',kills=[dict(victim_kind='player',victim_id=1)])
        requests=[dict(operation='award',battle=dict(actors=[rich,enemy]))]
        for name,value in [('critical_hits',4),('won',False),('survived',False),('max_health',0),
                ('duelist_sources',[]),('damage_sources',[]),('kills',[])]:
            actor=copy.deepcopy(rich);actor[name]=value
            requests.append(dict(operation='award',battle=dict(actors=[actor,enemy])))
        for name,value in [('kills',3),('kills',5),('kills',6),('damage',1000),('damage_blocked',1000),('assist_radio',999)]:
            actor=copy.deepcopy(rich);actor['stats'][name]=value
            requests.append(dict(operation='award',battle=dict(actors=[actor,enemy])))
        for kinds in [('bot','player'),('player','player')]:
            for xp in [(100,101),(100,100),(101,100)]:
                actors=[self.actor(i+1,actor_kind=kind,xp=points,stats=dict(kills=6)) for i,(kind,points) in enumerate(zip(kinds,xp))]
                requests.append(dict(operation='award',battle=dict(actors=actors)))
                requests.append(dict(operation='award',battle=dict(actors=actors[::-1])))
        self.compare(requests)
    def test_random_class_tier_kill_and_fire_predicates(self):
        r=random.Random(1513);requests=[]
        classes=['lightTank','mediumTank','heavyTank','AT-SPG','SPG']
        for index in range(200):
            actors=[]
            for actor_id in range(1,9):
                kills=[dict(victim_kind='bot',victim_id=r.randrange(1,9),victim_tier=r.randrange(0,11),
                    victim_class=r.choice(classes),death_reason=r.randrange(3),distance=r.choice([-1.,0.,99.,100.,101.,300.]),
                    defended_base=bool(r.randrange(2)),actor_speed=r.choice([-1.,0.,2.,3.0555555555555554,4.]),
                    victim_speed=r.randrange(6),ammo_rack=bool(r.randrange(2))) for _ in range(r.randrange(0,6))]
                actors.append(self.actor(actor_id,1 if actor_id%2 else 2,actor_kind='bot' if actor_id%3 else 'player',
                    tier=r.randrange(0,11),vehicle_class=r.choice(classes),health=r.randrange(0,300),max_health=r.choice([0,1000]),
                    survived=bool(r.randrange(2)),won=bool(r.randrange(2)),xp=r.randrange(1000),kills=kills,
                    stats=dict(kills=r.randrange(0,16),damage=r.randrange(10000),damage_blocked=r.randrange(2000),
                        spotted=r.randrange(12),capture_points=r.randrange(110),dropped_capture_points=r.randrange(100),
                        shots=10,direct_hits=r.randrange(11),assist_radio=r.randrange(1200)),
                    ally_hits=r.randrange(2),team_kills=r.randrange(2),crits_received=r.randrange(7),support_targets=r.randrange(8),
                    exclusive_spot_assists=r.randrange(8),hits_received=r.randrange(20),potential_damage_received=r.randrange(3000),
                    sniper_damage=r.randrange(3000),hits_with_damage=r.randrange(11),lone_stand_enemies=r.randrange(7),
                    best_multi_kill_shot=r.randrange(4),best_deflection_streak=r.randrange(12),enemy_damage_received=r.randrange(1000),
                    damaging_hits_received=r.randrange(4),deflected_hits_at_low_health=r.randrange(2),captured_base=bool(r.randrange(2)),
                    solo_capture=bool(r.randrange(2)),ever_spotted=bool(r.randrange(2)),lucky_devil=bool(r.randrange(2)),last_shell_finisher=bool(r.randrange(2))))
            requests.append(dict(operation='award',battle=dict(actors=actors,base_captured_team=r.randrange(3))))
        self.compare(requests)
    def test_tier_delta_bands_stop_at_the_pinned_maximum(self):
        requests=[]
        for vehicle_class in ('lightTank','mediumTank','AT-SPG'):
            for count in (0,1,2,3,4,254,255,256):
                actor=self.actor(tier=5,vehicle_class=vehicle_class,stats=dict(kills=count),
                    kills=[dict(victim_kind='bot',victim_id=i+1,victim_tier=7,victim_class='heavyTank') for i in range(count)])
                requests.append(dict(operation='award',battle=dict(actors=[actor])))
        self.compare(requests)
    def test_full_medal_bond_table_and_xp_conversion_boundaries(self):
        script="import sys,json;sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client');from gui.mods.offline_lan_0922.battle_bonds import MEDAL_BONDS;print(json.dumps(list(MEDAL_BONDS)))"
        names=json.loads(subprocess.check_output([sys.executable,'-I','-B','-c',script,FORK],text=True))
        requests=[dict(operation='medals',achievements=names+names+['unknown'],tier=tier) for tier in range(-1,13)]
        for eligible in (False,True):
            for xp in [-1,0,399,400,401,500,1000,3000,10000,1000000,9223372036854775807]:
                requests.append(dict(operation='bonds',xp=xp,eligible=eligible))
            requests.extend(dict(operation='bonds',xp=xp,eligible=eligible) for xp in range(12000,12600))
        self.compare(requests)
    def test_offline_rewards_tier_win_penalty_and_bad_stat_inputs(self):
        r=random.Random(922);requests=[]
        names=['damage_dealt','damage_assisted_track','damage_assisted_radio','damage_assisted_stun','spotted',
            'capture_points','dropped_capture_points','team_damage_penalized','team_killed_durability']
        for tier in range(-1,13):
            for won in (False,True):
                for participated in (False,True):
                    requests.append(dict(operation='rewards',stats={name:r.randrange(10000) for name in names},
                        won=won,participated=participated,tier=tier,spotted_spgs=1000,capture_participants=3,killed_durability=2500))
        for invalid in (None,True,False,'123','2.5','bad',[],{},-100,1.9):
            requests.append(dict(operation='rewards',stats={name:invalid for name in names},won=True,participated=False,
                tier=5,spotted_spgs=-1,capture_participants=-1,killed_durability=-1))
        self.compare(requests)
if __name__=='__main__':unittest.main()
