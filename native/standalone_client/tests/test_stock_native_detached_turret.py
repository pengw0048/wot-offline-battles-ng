"""Compound-body physics against the actual pinned CPython 2.7 laws."""
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
import ast,copy,imp,json,math,os,sys,time
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
class Object(object):
    def __init__(self,**kw):self.__dict__.update(kw)
shot=imp.load_source('shot_geometry',os.path.join(base,'shot_geometry.py'))
geometry={ 'math':math }
tree=ast.parse(open(os.path.join(base,'entities/turret_obstacles.py')).read())
exec compile(ast.Module([n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('_finite','_corners')]),'geometry','exec') in geometry
env=dict(copy=copy,math=math,time=time,shot_geometry=shot,geometry=Object(**geometry),turret_detachment=Object(FLIGHT_STEP_SECONDS=.04,impact_energy=lambda v:.5*sum(x*x for x in v)))
tree=ast.parse(open(os.path.join(base,'rigid_turret.py')).read())
exec compile(ast.Module([n for n in tree.body if isinstance(n,(ast.FunctionDef,ast.ClassDef,ast.Assign))]),'rigid_turret','exec') in env
result=[]
for case in json.load(sys.stdin):
    parts=[]
    for part in case['parts']:
        parts.append(('component',Object(weight=part[0]),tuple(part[3]),(tuple(part[1]),tuple(part[2]))))
    body=env['Body'](parts,case['frame'])
    queries=[0];applications=[0];clock_time=[0.]
    def collide(a,b):
        queries[0]+=1
        hits=[]
        for x,y,normal in ((a[1]-case['floor']-case['slope']*a[0],b[1]-case['floor']-case['slope']*b[0],(-case['slope'],1.,0.)),(case['wall']-a[0],case['wall']-b[0],(-1.,0.,0.))):
            if x>=0. and y<=0. and x!=y:
                ratio=x/(x-y)
                hits.append((ratio,tuple(a[i]+(b[i]-a[i])*ratio for i in range(3)),env['unit'](normal)))
        return min(hits,key=lambda h:h[0])[1:] if hits else None
    def apply(vehicle,hit,step):
        applications[0]+=1
        vehicle['boxes']=tuple((env['add'](centre,hit['vehicle_correction']),axes) for centre,axes in vehicle['boxes'])
        vehicle['velocity']=env['add'](vehicle['velocity'],hit['delta'])
    def clock():
        value=clock_time[0];clock_time[0]+=case['tick'];return value
    vehicles=case['vehicles']
    for vehicle in vehicles:
        vehicle['boxes']=tuple((tuple(c),tuple(tuple(a) for a in axes)) for c,axes in vehicle['boxes'])
        vehicle['velocity']=tuple(vehicle['velocity'])
    consumed=env['advance'](body,case['dt'],vehicles,collide,apply,budget=.004 if case['tick'] else None,clock=clock)
    frame=body.frame();impact=frame['impact']
    result.append([body.props['mass'],body.props['centre'],body.props['inertia'],body.props['inverse_inertia'],frame['position'],frame['attitude'],frame['velocity'],frame['angular_velocity'],frame['grounded'],frame['sleeping'],body.kinetic_energy(),body.points(),body.boxes(),queries[0],consumed,applications[0],frame['impact_serial'],[impact['point'],impact['normal'],impact['velocity'],impact['energy']] if impact else None,[[v['boxes'],v['velocity']] for v in vehicles]])
json.dump(result,sys.stdout)
'''

@unittest.skipUnless(SOURCE,'set WOT_FORK_0101_SOURCE to the pinned gameplay checkout')
class DetachedTurret(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='native-detached-turret-')
        cls.binary=Path(cls.tmp.name)/'host'
        cls.oracle=Path(cls.tmp.name)/'oracle.py';cls.oracle.write_text(ORACLE)
        native=Path(SOURCE)/'native'
        cls.py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(native),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_detached_turret_runtime.cpp'),str(ROOT/'bigworld/stock_native_bot_turret_obstacles.cpp'),str(native/'offline_destructible_geometry.cpp'),str(ROOT/'tests/stock_native_detached_turret_host.cpp'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_transactions_budget_and_impulse_acknowledgements(self):
        subprocess.run([str(self.binary),'scenarios'],check=True,capture_output=True,text=True)
    def test_actual_python27_physics(self):
        rng=random.Random(1513);cases=[]
        for index in range(180):
            parts=[]
            for n in range(2):
                low=[rng.uniform(-1.5,-.2) for _ in range(3)];high=[rng.uniform(.2,1.5) for _ in range(3)]
                parts.append([rng.uniform(100.,3000.),low,high,[rng.uniform(-.5,.5) for _ in range(3)]])
            frame=dict(position=[rng.uniform(-1.,1.),rng.uniform(1.,4.),rng.uniform(-1.,1.)],attitude=[rng.uniform(-2.,2.) for _ in range(3)],velocity=[rng.uniform(-4.,4.),rng.uniform(-8.,1.),rng.uniform(-4.,4.)],angular_velocity=[rng.uniform(-2.,2.) for _ in range(3)],grounded=index%5==0,sleeping=index%17==0)
            # Alternate free flight, slopes, walls and reciprocal vehicle
            # constraints; human side contacts consume no worker impulse.
            vehicles=[]
            if index%3:
                vehicles=[dict(mass=rng.uniform(5000.,40000.),human=index%2==0,alive=index%7!=0,velocity=[rng.uniform(-3.,3.),0.,rng.uniform(-3.,3.)],boxes=[[[rng.uniform(-1.,1.),1.,rng.uniform(-1.,1.)],[[2.,0.,0.],[0.,1.,0.],[0.,0.,2.]]]])]
            cases.append(dict(parts=parts,frame=frame,dt=rng.uniform(.001,.15),floor=0. if index%4 else -100.,slope=rng.uniform(-.3,.3),wall=2. if index%6==0 else 100.,tick=.003 if index%11==0 else 0.,vehicles=vehicles))
        expected=json.loads(subprocess.check_output([self.py27,str(self.oracle),SOURCE],input=json.dumps(cases),text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')))
        lines=[]
        for case in cases:
            values=[len(case['parts'])]
            for mass,low,high,offset in case['parts']:values += [mass,*low,*high,*offset]
            frame=case['frame']
            values += [*frame['position'],*frame['attitude'],*frame['velocity'],*frame['angular_velocity'],int(frame['grounded']),int(frame['sleeping']),case['dt'],case['floor'],case['slope'],case['wall'],case['tick'],len(case['vehicles'])]
            for v in case['vehicles']:
                centre,axes=v['boxes'][0];values += [v['mass'],int(v['human']),int(v['alive']),*v['velocity'],*centre,*axes[0],*axes[1],*axes[2]]
            lines.append(' '.join(map(str,values)))
        actual=[json.loads(line) for line in subprocess.check_output([str(self.binary)],input='\n'.join(lines)+'\n',text=True).splitlines()]
        def compare(a,b,path):
            if isinstance(b,(list,tuple)):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):compare(x,y,path+(i,))
            elif b is None or isinstance(b,bool):self.assertEqual(a,b,path)
            else:self.assertTrue(math.isclose(a,b,rel_tol=3.e-9,abs_tol=3.e-9),(path,a,b))
        for i,(a,b) in enumerate(zip(actual,expected)):compare(a,b,(i,))

if __name__=='__main__':unittest.main()
