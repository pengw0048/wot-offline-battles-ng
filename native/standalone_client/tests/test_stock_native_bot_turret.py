"""Detached turret zonotope sweeps against the fixed source geometry law."""
import ast
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import types
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_0101_SOURCE')

@unittest.skipUnless(SOURCE,'set WOT_FORK_0101_SOURCE to the pinned gameplay checkout')
class NativeTurret(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='native-turret-');cls.binary=Path(cls.tmp.name)/'turret'
        base=Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922'
        spec=importlib.util.spec_from_file_location('turret_shot_geometry',base/'shot_geometry.py')
        shot=importlib.util.module_from_spec(spec);spec.loader.exec_module(shot)
        cls.env=dict(math=math,shot_geometry=shot,_ROTATION_SLICE=math.pi/36.,_CONTACT_EPSILON=1.e-7)
        sensor={}
        def functions(path,names,env):
            text=path.read_text();nodes=[n for n in ast.parse(text).body if isinstance(n,ast.FunctionDef) and (names is None or n.name in names)]
            exec(compile(ast.Module(body=nodes,type_ignores=[]),str(path),'exec'),env)
        functions(base/'destructibles_sensor.py',{'_vector_dot','_vector_cross'},sensor)
        cls.env['destructibles_sensor']=types.SimpleNamespace(**sensor)
        functions(base/'entities/turret_obstacles.py',None,cls.env)
        code=ast.parse((base/'entities/turret_obstacles.py').read_text())
        exec(compile(ast.Module(body=[n for n in code.body if isinstance(n,ast.ClassDef)],type_ignores=[]),'turret-obstacles','exec'),cls.env)
        native=Path(SOURCE)/'native'
        subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror',
            '-I'+str(native),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_turret_obstacles.cpp'),
            str(native/'offline_destructible_geometry.cpp'),str(ROOT/'tests/stock_native_bot_turret_host.cpp'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_lifecycle(self):self.assertEqual(subprocess.check_output([str(self.binary),'scenarios'],text=True).strip(),'ok')
    def test_fixed_sweeps_and_departures(self):
        rng=random.Random(1513);rows=[];expected=[]
        def point(scale=4.):return tuple(rng.uniform(-scale,scale) for _ in range(3))
        def component():
            low=point(2.);high=tuple(v+rng.uniform(.01,4.) for v in low);return low,high,point(1.)
        for index in range(320):
            low,high,offset=component();start,a,end,b=point(),point(math.pi),point(),point(math.pi)
            pivot=(point(),rng.uniform(-math.pi,math.pi)) if index%3==0 else None
            if index%4==0:b=a
            boxes=self.env['_component_sweeps']((low,high),offset,(start,a),(end,b),pivot)
            operation=index%2;row=[operation,*low,*high,*offset,*start,*a,*end,*b,int(pivot is not None),*(pivot[0] if pivot else (0.,0.,0.)),pivot[1] if pivot else 0.]
            if operation:
                ol,oh,oo=component();op,oa=point(),point(math.pi)
                obstacle=self.env['_world_box']((ol,oh),oo,op,oa)
                value=self.env['_leaves_overlap'](self.env['_world_box']((low,high),offset,start,a),self.env['_world_box']((low,high),offset,end,b),boxes,obstacle)
                row.extend([*ol,*oh,*oo,*op,*oa]);expected.append(int(value))
            else:expected.append(boxes)
            rows.append(' '.join(map(str,row)))
        actual=[json.loads(row) for row in subprocess.check_output([str(self.binary)],input='\n'.join(rows)+'\n',text=True).splitlines()]
        def equal(a,b):
            if isinstance(b,(list,tuple)):
                self.assertEqual(len(a),len(b))
                for x,y in zip(a,b):equal(x,y)
            else:self.assertTrue(math.isclose(a,b,rel_tol=3.e-12,abs_tol=3.e-12),(a,b))
        for a,b in zip(actual,expected):equal(a,b)
    def test_exact_bsp_chord_frames_and_clock(self):
        import struct
        f32=lambda x:struct.unpack('f',struct.pack('f',x))[0]
        rng=random.Random(922);rows=[];expected=[]
        for i in range(80):
            rest=tuple(rng.uniform(-2.,2.) for _ in range(3));attitude=tuple(rng.uniform(-1.,1.) for _ in range(3))
            start=(0.,0.,-5.);end=(0.,0.,5.);now=1900.+i*20.;begin=1800.+i*10.
            shapes=[((-2.,-2.,-2.),(2.,2.,2.),(0.,0.,0.)),((-1.,-1.,-3.),(1.,1.,3.),(.25,.5,1.))]
            queries=[]
            class Tester:
                def __init__(self,gun):self.gun=gun
                def localHitTest(self,a,b):
                    queries.append([a,b]);return [(v,) for v in ([1.,5.] if self.gun else [2.,4.,8.])]
            vector=lambda *p:tuple(f32(x) for x in p)
            store=self.env['DetachedTurretObstacles'](types.SimpleNamespace(Vector3=vector))
            store._turrets={7:dict(rest=rest,attitude=attitude,radius=5.,settles_at_ms=2000.,components=[
                ('gun' if n else 'turret',types.SimpleNamespace(hitTester=Tester(n)),s[2],s[:2]) for n,s in enumerate(shapes)])}
            distance=store.block_distance(start,end,now,begin);expected.append([distance,queries])
            row=[2,*rest,*attitude,now,begin]
            for lo,hi,offset in shapes:row.extend([*lo,*hi,*offset])
            row.extend([*start,*end]);rows.append(' '.join(map(str,row)))
        actual=[json.loads(row) for row in subprocess.check_output([str(self.binary)],input='\n'.join(rows)+'\n',text=True).splitlines()]
        self.assertEqual(actual,json.loads(json.dumps(expected)))

if __name__=='__main__':unittest.main()
