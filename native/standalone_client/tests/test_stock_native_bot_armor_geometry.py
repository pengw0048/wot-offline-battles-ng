"""Native ram plate search versus pinned CPython 2.7 contact consumers."""
import json,math,os,random,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ArmorGeometry(unittest.TestCase):
    def test_exact_search_trace_and_tracks(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        rng=random.Random(1513);cases=[]
        for i in range(420):
            def body(identity,x):return [identity,x,rng.uniform(-.1,.1),rng.uniform(-.8,.8),rng.uniform(-.5,.5),rng.uniform(-.3,.3),rng.uniform(-.3,.3),rng.uniform(.8,2.),rng.uniform(2.,4.),0.,2.]
            cases.append([i%21,body(1,-.8),body(2,.8),[1.,rng.uniform(-.3,.3)]])
        with tempfile.TemporaryDirectory(prefix='ram-geometry-') as directory:
            binary=Path(directory)/'armor'
            subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native'),str(ROOT/'bigworld/stock_native_bot_armor_geometry.cpp'),
                str(ROOT/'tests/stock_native_bot_armor_geometry_host.cpp'),'-o',str(binary)],check=True)
            rows=[' '.join(map(str,[r[0]]+r[1]+r[2]+r[3])) for r in cases]
            actual=[json.loads(v) for v in subprocess.check_output([str(binary)],input='\n'.join(rows)+'\n',text=True).splitlines()]
            expected=[json.loads(v) for v in subprocess.check_output([py27,'-B',str(ROOT/'tests/stock_native_bot_armor_geometry_oracle.py'),source],input='\n'.join(map(json.dumps,cases))+'\n',text=True).splitlines()]
        def compare(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):compare(x,y,path+'/'+str(i))
            elif isinstance(b,(int,float)):self.assertTrue(math.isclose(a,b,rel_tol=3.e-12,abs_tol=3.e-12),(path,a,b))
            else:self.assertEqual(a,b,path)
        for i,(a,b) in enumerate(zip(actual,expected)):compare(a,b,str(i))
if __name__=='__main__':unittest.main()
