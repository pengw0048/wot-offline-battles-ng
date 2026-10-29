"""Exact release direction-probe call order, water and signed-grade parity."""
import ast
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import textwrap
import unittest

ROOT=Path(__file__).resolve().parents[1]

class NativeCandidateTests(unittest.TestCase):
    def test_direction_probe_against_python27(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('Fixed v0.10.1 source input is required')
        source=Path(source)
        with tempfile.TemporaryDirectory(prefix='native-candidate-') as temporary:
            binary=Path(temporary)/'candidate'
            built=subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-pthread',
                '-I'+str(ROOT/'bigworld'),'-I'+str(source/'native'),str(ROOT/'bigworld/stock_native_bot_candidate.cpp'),
                str(ROOT/'bigworld/stock_native_bot_navigation.cpp'),str(ROOT/'bigworld/stock_native_bot_navigator.cpp'),
                str(source/'native/offline_navigation.cpp'),str(source/'native/offline_async_worker.cpp'),
                str(ROOT/'tests/stock_native_bot_candidate_host.cpp'),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            text=(source/'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read_text()
            method=next(n for n in ast.walk(ast.parse(text)) if isinstance(n,ast.FunctionDef) and n.name=='_direction_probe')
            oracle=Path(temporary)/'oracle.py'
            oracle.write_text('''import json,math,sys
BOT_WATER_AVOID_DEPTH=.90
BOT_WATER_ESCAPE_DEEPEN_EPSILON=.10
NAVIGATION_MAX_GRADE=math.tan(math.radians(25.))
VEHICLE_SKIP_FLAGS=0
class Vector(object):
    def __init__(self,p):self.x,self.y,self.z=p
class Scene(object):
    def _collision_shape(self,descriptor):return (descriptor,)
    def _vector(self,p):return Vector(p)
    def _water_depth(self,p):return self.water+(p[2]*.04 if self.policy==5 else 0.)
    def _record_navigation_hit(self,*args):pass
    def _collide_navigation(self,a,b,trace):
        self.queries+=1
        if a.x==b.x and a.z==b.z:
            if self.policy==2:raise RuntimeError('fixture')
            if self.policy==3:return None
            return (Vector((a.x,self.gx*a.x+self.gz*a.z+self.base,a.z)),Vector((0.,1.,0.)))
        if self.policy==4:raise RuntimeError('fixture')
        return (Vector((b.x,b.y,b.z)),Vector((0.,1.,0.))) if self.policy==1 else None
'''+textwrap.indent(textwrap.dedent(ast.get_source_segment(text,method)),'    ')+'''

def _xyz(p):return p
out=[]
for c in json.load(sys.stdin):
    x,y,z,yaw,speed,width,bounded,maximum,policy,gx,gz,base,water=c
    scene=Scene();scene.queries=0;scene.policy=policy;scene.gx=gx;scene.gz=gz;scene.base=base;scene.water=water
    s=scene._direction_probe((x,y,z),yaw,speed,width,maximum if bounded else None)
    out.append([s.get('clear',True),s.get('collision',False),s.get('water',False),s.get('slope',0.),s.get('probe_failed',False),s.get('slope_valid',True),s.get('reason',''),scene.queries])
print(json.dumps(out))
''')
            rng=random.Random(1571);cases=[]
            for n in range(400):
                x,z=rng.uniform(-50,50),rng.uniform(-50,50);gx,gz=rng.uniform(-.7,.7),rng.uniform(-.7,.7)
                cases.append([x,gx*x+gz*z,z,rng.uniform(-5,5),rng.uniform(-12,12),rng.uniform(.5,3.),n%2,rng.uniform(0,23),n%6,gx,gz,0.,(-1.,.2,1.1)[n%3]])
            py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
            expected=subprocess.run([py27,'-B',str(oracle)],input=json.dumps(cases),capture_output=True,text=True)
            self.assertEqual(expected.returncode,0,expected.stderr)
            actual=subprocess.run([str(binary)],input='\n'.join(' '.join(map(str,c)) for c in cases)+'\n',capture_output=True,text=True)
            self.assertEqual(actual.returncode,0,actual.stderr)
            rows=[json.loads(line) for line in actual.stdout.splitlines()]
            for n,(a,b) in enumerate(zip(rows,json.loads(expected.stdout))):
                self.assertEqual(a[:3],b[:3],(n,cases[n]));self.assertAlmostEqual(a[3],b[3],places=9,msg=str((n,cases[n])))
                self.assertEqual(a[4:],b[4:],(n,cases[n]))
            self.assertEqual(len(rows),len(cases))

if __name__=='__main__':unittest.main()
