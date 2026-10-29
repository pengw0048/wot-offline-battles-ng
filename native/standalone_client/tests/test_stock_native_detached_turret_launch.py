"""Frozen visual flights, deterministic launch and disabled-body policy."""
import json
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_0101_SOURCE')
ORACLE=r'''
import ast,hashlib,imp,json,math,os,sys
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
law=imp.load_source('turret_detachment',os.path.join(base,'turret_detachment.py'))
shot=imp.load_source('shot_geometry',os.path.join(base,'shot_geometry.py'))
env=dict(shot_geometry=shot,turret_detachment=law,hashlib=hashlib)
tree=ast.parse(open(os.path.join(base,'ai/planner.py')).read())
exec compile(ast.Module([n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='stable_seed']),'planner','exec') in env
tree=ast.parse(open(os.path.join(base,'entities/detached_turret.py')).read())
exec compile(ast.Module([n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('detachment_plan','turret_mount_offset','_xyz','_turret_clearance','freeze_visual_plan')]),'detached','exec') in env
env['turret_components']=lambda d:(('turret',d.turret,(0.,0.,0.),d.turret.hitTester.bbox),('gun',d.gun,(0.,0.,0.),d.gun.hitTester.bbox))
env['DetachedTurretPresentation']=Box(_exploded_model=lambda c:c.models.exploded)
results=[]
for case in json.load(sys.stdin):
    count=[0]
    def collide(a,b):
        count[0]+=1;hits=[]
        for x,y in ((a[1]-case['floor']-case['slope']*a[0],b[1]-case['floor']-case['slope']*b[0]),(case['wall']-a[0],case['wall']-b[0])):
            if x>=0. and y<=0. and x!=y:
                ratio=x/(x-y);hits.append((ratio,tuple(a[i]+(b[i]-a[i])*ratio for i in range(3))))
        return min(hits,key=lambda h:h[0])[1] if hits else None
    bounds=((-1.,-case['clearance'],-1.),(1.,1.,1.));part=Box(models=Box(exploded='stock'),hitTester=Box(bbox=bounds))
    descriptor=Box(chassis=Box(hullPosition=(0.,0.,0.)),hull=Box(turretPositions=[case['mount']]),turret=part,gun=part)
    p=case['position'];a=case['attitude'];pose=dict(x=p[0],y=p[1],z=p[2],yaw=a[0],pitch=a[1],roll=a[2],turret_yaw=case['turret_yaw'])
    seed=env['stable_seed']('turret-obstacle-v1',case['round'],('player:' if case['human'] else 'bot:')+str(case['id']))
    impulse=law.launch_impulse(seed);row=env['freeze_visual_plan'](Box(typeDescriptor=descriptor,id=7),pose,seed,collide);f=row['flight']
    results.append([seed,impulse['velocity'],impulse['spin'],row['attitude'],[f['origin'],f['velocity'],[[s['origin'],s['velocity'],s['start'],s['duration']] for s in f['segments']],f['duration'],f['rest'],f['contact'],f['impact_velocity'],f['energy'],f['landed']],count[0]])
json.dump(results,sys.stdout)
'''
@unittest.skipUnless(SOURCE,'set WOT_FORK_0101_SOURCE')
class Launch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='native-turret-launch-');folder=Path(cls.tmp.name);cls.binary=folder/'launch';cls.oracle=folder/'oracle.py';cls.oracle.write_text(ORACLE)
        cls.py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        native=Path(SOURCE)/'native'
        subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(native),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_detached_turret_runtime.cpp'),str(ROOT/'bigworld/stock_native_detached_turret_launch.cpp'),str(ROOT/'bigworld/stock_native_bot_turret_obstacles.cpp'),str(native/'offline_destructible_geometry.cpp'),str(ROOT/'tests/stock_native_detached_turret_launch_host.cpp'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_retry_and_visual_policy(self):subprocess.run([str(self.binary),'scenarios'],check=True,capture_output=True,text=True)
    def test_exact_python27_frozen_flights(self):
        rng=random.Random(922);cases=[]
        for index in range(160):
            cases.append(dict(round=str(1000000+index),human=index%2,id=1+index,position=[rng.uniform(-5.,5.),rng.uniform(1.,10.),rng.uniform(-5.,5.)],attitude=[rng.uniform(-math.pi,math.pi) for _ in range(3)],mount=[rng.uniform(-2.,2.) for _ in range(3)],turret_yaw=rng.uniform(-math.pi,math.pi),clearance=rng.uniform(0.,1.),floor=-1000. if index%7==0 else 0.,slope=rng.uniform(-.3,.3),wall=6. if index%3==0 else 10000.))
        expected=json.loads(subprocess.check_output([self.py27,'-B',str(self.oracle),SOURCE],input=json.dumps(cases),text=True))
        lines=[]
        for c in cases:lines.append(' '.join(map(str,[c['round'],int(c['human']),c['id'],*c['position'],*c['attitude'],*c['mount'],c['turret_yaw'],c['clearance'],c['floor'],c['slope'],c['wall']])))
        actual=[json.loads(line) for line in subprocess.check_output([str(self.binary)],input='\n'.join(lines)+'\n',text=True).splitlines()]
        def compare(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):compare(x,y,path+(i,))
            elif b is None or isinstance(b,bool):self.assertEqual(a,b,path)
            else:self.assertTrue(math.isclose(a,b,rel_tol=2.e-12,abs_tol=2.e-12),(path,a,b))
        for i,(a,b) in enumerate(zip(actual,expected)):compare(a,b,(i,))
if __name__=='__main__':unittest.main()
