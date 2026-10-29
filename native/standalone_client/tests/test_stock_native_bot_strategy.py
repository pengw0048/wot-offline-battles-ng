"""No-server-order strategic state against the fixed Python 2.7 director."""
import inspect
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]


def reference(cases):
    from gui.mods.offline_lan_0922.ai.planner import BattleDirector
    director=BattleDirector.__new__(BattleDirector);director.agents={};director.contacts={1:{},2:{}};director.bases={}
    out=[]
    for row in cases:
        if row[0]=='base':director.bases[row[1]]=tuple(row[2:4])
        elif row[0]=='agent':
            (_,actor,team,seed,kind,role,desired,fire,armor,aggression,caution,initiative,teamwork,has_route,route_id,count)=row[:16]
            cursor=16;points=[]
            for i in range(count):points.append(tuple(row[cursor:cursor+2]));cursor+=2
            roles={};count=row[cursor];cursor+=1
            for i in range(count):roles[row[cursor]]=row[cursor+1];cursor+=2
            shells=[];count=row[cursor];cursor+=1
            for i in range(count):shells.append(dict(zip(('index','kind','penetration','damage'),row[cursor:cursor+4])));cursor+=4
            profile=dict(class_tag=kind,dominant_role=role,desired_range=desired,fire_range=fire,armor=armor,roles=roles,shells=shells)
            director.agents[actor]=dict(id=actor,team=team,seed=seed,profile=profile,personality=dict(aggression=aggression,caution=caution,initiative=initiative,teamwork=teamwork),route=dict(id=route_id,waypoints=points) if has_route else None,waypoint_index=0,target_id=None,position=None,health_fraction=1.)
        elif row[0]=='contact':
            (_,team,actor,target_team,x,y,z,health,max_health,armor,speed,now,kind,visible,count)=row[:15]
            director.update_contact(team,actor,target_team,(x,y,z),health,max_health,kind,bool(visible),now,armor,speed,None if count<0 else row[15:])
        elif row[0]=='order':
            (_,actor,x,y,z,yaw,speed,health,max_health,now)=row
            d=director.order_for(actor,(x,y,z),yaw,speed,health,max_health,now)
            out.append([d.get(k) for k in ('target_id','aim_position','move_position','face_position','fire_allowed','combat_mode','shell_index','desired_range','fire_range','route_id','route_index','route_anchor','route_join','force_balance','hull_angle_degrees')])
    return out


class NativeStrategyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-strategy-');cls.binary=Path(cls.tmp.name)/'strategy'
        built=subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),
            str(ROOT/'bigworld/stock_native_bot_strategy.cpp'),str(ROOT/'tests/stock_native_bot_strategy_host.cpp'),'-o',str(cls.binary)],capture_output=True,text=True)
        if built.returncode:raise RuntimeError(built.stderr)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        cls.oracle=Path(cls.tmp.name)/'oracle.py';cls.oracle.write_text('''import sys,os,types,json
sys.path.insert(0,sys.argv[1])
for name in ('gui','gui.mods'):
    p=types.ModuleType(name);p.__path__=[os.path.join(sys.argv[1],*name.split('.'))];sys.modules[name]=p
'''+inspect.getsource(reference)+'\nprint(json.dumps(reference(json.load(sys.stdin))))\n')
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()
    def agent(self,actor=1,team=1,seed=17,kind='mediumTank',role='support',desired=200.,fire=500.,armor=100.,aggression=.5,caution=.5,initiative=.6,teamwork=.7,has_route=1,points=((0.,0.),(0.,20.),(0.,100.),(100.,200.)),roles=None):
        roles=roles or [('support',.8),('flanker',.3),('brawler',.6),('scout',.2)]
        result=['agent',actor,team,seed,kind,role,desired,fire,armor,aggression,caution,initiative,teamwork,has_route,'r',len(points)]
        for p in points:result+=list(p)
        result.append(len(roles))
        for r in roles:result+=list(r)
        return result+[3,0,'ARMOR_PIERCING',160.,200.,1,'ARMOR_PIERCING_CR',220.,200.,2,'HIGH_EXPLOSIVE',40.,300.]
    def contact(self,actor=100,team=1,target_team=2,x=0.,y=0.,z=180.,health=1000.,max_health=1000.,armor=120.,speed=0.,now=0.,kind='mediumTank',visible=1,shootable=None):
        return ['contact',team,actor,target_team,x,y,z,health,max_health,armor,speed,now,kind,visible,-1 if shootable is None else len(shootable)]+([] if shootable is None else shootable)
    def order(self,actor=1,x=0.,y=0.,z=0.,yaw=0.,speed=0.,health=1000.,max_health=1000.,now=0.):return ['order',actor,x,y,z,yaw,speed,health,max_health,now]
    def compare(self,cases):
        actual=subprocess.run([str(self.binary)],input='\n'.join(' '.join(str(int(v)) if isinstance(v,bool) else str(v) for v in r) for r in cases)+'\n',capture_output=True,text=True);self.assertEqual(actual.returncode,0,actual.stderr)
        expected=subprocess.run([self.py27,'-B',str(self.oracle),str(self.source/'src/res/scripts/client')],input=json.dumps(cases),capture_output=True,text=True);self.assertEqual(expected.returncode,0,expected.stderr)
        rows=[json.loads(l) for l in actual.stdout.splitlines()];wanted=json.loads(expected.stdout.splitlines()[-1]);self.assertEqual(len(rows),len(wanted))
        def equal(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):equal(x,y,path+'[%d]'%i)
            elif isinstance(b,(float,int,bool)):self.assertAlmostEqual(a,b,places=8,msg=path)
            else:self.assertEqual(a,b,path)
        for i,(a,b) in enumerate(zip(rows,wanted)):equal(a,b,str(i))
    def test_route_join_forward_reverse_and_direct_without_server_order(self):
        cases=[['base',2,500.,500.],self.agent(),self.agent(actor=2,has_route=0),self.agent(actor=3)]
        cases += [self.order(),self.order(z=90.,speed=3.,now=1.),self.order(x=80.,z=180.,now=2.),self.order(actor=2),self.order(actor=3,z=60.,yaw=3.14,speed=-2.)]
        self.compare(cases)
    def test_engage_withdraw_flank_contact_memory_focus_and_shells(self):
        cases=[self.agent(actor=i,seed=i,kind='heavyTank' if i==1 else 'mediumTank',role='brawler' if i==1 else 'support',roles=[('flanker',.8),('brawler',.7)],caution=.3 if i==1 else .8) for i in range(1,6)]
        for step in range(12):
            now=step*.8
            if step<8:
                cases += [self.contact(actor=100,now=now,z=40.+step*18.,armor=20. if step%3==0 else 190.,health=200. if step%3==0 else 1800.,visible=step!=7),
                          self.contact(actor=101,now=now,x=30.,z=220.,kind='SPG',shootable=[2,3]),self.contact(actor=102,now=now,x=-40.,z=150.,kind='heavyTank')]
            for actor in range(1,6):cases.append(self.order(actor=actor,x=actor*3.,z=step*5.,now=now,health=120. if step==6 else 1000.))
        cases += [self.order(actor=1,now=30.),self.order(actor=2,now=30.)]
        self.compare(cases)
    def test_random_profiles_positions_visibility_and_persistent_targets(self):
        rng=random.Random(1513);cases=[]
        for actor in range(1,11):cases.append(self.agent(actor=actor,team=1 if actor<6 else 2,seed=actor*11,kind=rng.choice(['mediumTank','heavyTank','AT-SPG','SPG','lightTank']),role=rng.choice(['support','brawler','scout']),desired=rng.uniform(60.,350.),armor=rng.uniform(30.,180.),aggression=rng.random(),caution=rng.random(),initiative=rng.random(),teamwork=rng.random()))
        for step in range(50):
            for team in (1,2):
                for contact in range(100,104):cases.append(self.contact(actor=contact,team=team,target_team=3-team,x=rng.uniform(-200.,200.),z=rng.uniform(-200.,400.),health=rng.uniform(1.,2500.),max_health=2500.,armor=rng.uniform(0.,300.),now=step*.2,visible=int(rng.random()>.2),kind=rng.choice(['SPG','heavyTank','mediumTank'])))
            for actor in range(1,11):cases.append(self.order(actor=actor,x=rng.uniform(-50.,50.),z=step*2.,yaw=rng.uniform(-3.14,3.14),speed=rng.uniform(-3.,8.),health=rng.uniform(100.,1000.),now=step*.2))
        self.compare(cases)

if __name__=='__main__':unittest.main()
