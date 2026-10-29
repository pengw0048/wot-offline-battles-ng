"""Current v0.10.1 navigation geometry and oracle differential contracts."""
import inspect
import json
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


def reference(payload):
    from gui.mods.offline_lan_0922.ai.navigation import TerrainGrid
    graph, cases = payload
    policy = [0]
    def ground(x,z,hint):
        return None if policy[0] == 3 else .01*x+.02*z
    def obstacle(a,b,width):
        return 'deferred' if policy[0] == 2 else policy[0] == 1
    class Oracle(object):
        def run(self,*args,**kwargs):
            return (-1 if policy[0] == 2 else 0 if policy[0] == 1 else 1,)
    grid=TerrainGrid(ground,obstacle,baked_graph=graph)
    grid.native_query_oracle=Oracle()
    result=[]
    for case in cases:
        op=case[0]
        if op=='geometry':
            a,b,yaw,length,width=case[1:]
            c=grid.cell_for(a); corridor=grid._baked_corridor(a,b)
            result.append(list(c)+[grid._baked_cell_height(c),corridor[0],corridor[1],
                grid.segment_has_baked_hazard(a,b,4),grid.segment_has_motion_hazard(a,b,5),
                grid.point_has_baked_hazard(a,3),grid.baked_hazard_near(a,1),
                grid.hull_pose_clear(a,yaw,length,width),grid._baked_link_count(c),
                grid._baked_clearance_penalty(c),grid.local_corridor(a)])
        elif op=='proof':
            a,b,policy[0],review=case[1:]
            grid.invalidate_native_review();grid._native_review_pending=set();grid._proof_deferred=False
            if review:grid.review_native_corridor(a,b)
            result.append([grid._supported_slope_corridor(a,b),grid.segment_clear(a,b),
                           grid.dry_segment_clear(a,b,0.),grid._proof_deferred])
        elif op=='local':
            a,b,side,minimum,policy[0]=case[1:]
            grid.invalidate_native_review();grid._native_review_pending=set();grid._proof_deferred=False
            result.append(grid.safe_local_target(a,b,0.,side_preference=side,minimum_offset=minimum))
        elif op=='hulls':
            hulls,padding,a,b=case[1:]
            changed=grid.set_static_hulls(hulls,padding)
            result.append([changed,grid.static_hull_revision,len(grid._static_hull_edges),grid.segment_penalty(a,b,0.)])
        elif op=='path':
            path=case[1]
            result.append([grid._baked_clearance_exposure(path),grid.shortcut_preserves_climb_approach(path,0,len(path)-1),
                           grid.shortcut_preserves_baked_clearance(path,0,len(path)-1)])
    return result


class NativeNavigationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-navigation-')
        cls.binary=Path(cls.tmp.name)/'navigation'
        subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-pthread',
            '-I'+str(ROOT/'bigworld'),'-I'+str(cls.source/'native'),
            str(ROOT/'bigworld/stock_native_bot_navigation.cpp'),str(cls.source/'native/offline_navigation.cpp'),str(cls.source/'native/offline_async_worker.cpp'),
            str(ROOT/'tests/stock_native_bot_navigation_host.cpp'),'-o',str(cls.binary)],check=True,capture_output=True,text=True)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],
            env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        cls.oracle=Path(cls.tmp.name)/'oracle.py'
        cls.oracle.write_text('import json, os, sys, types\n'
            'sys.path.insert(0,sys.argv[1])\n'
            'for name in ("gui", "gui.mods"):\n'
            '    package=types.ModuleType(name)\n'
            '    package.__path__=[os.path.join(sys.argv[1],*name.split("."))]\n'
            '    sys.modules[name]=package\n'+inspect.getsource(reference)+
            '\nprint(json.dumps(reference(json.load(sys.stdin))))\n')

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()

    def graph(self):
        width=15;height=15;heights=[];links=[];hazards=[]
        missing={(4,4),(4,5),(4,6),(5,6),(11,10)}
        steps=[(-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)]
        for z in range(height):
            for x in range(width):
                heights.append(None if (x,z) in missing else (x*40+z*80-840))
                links.append(sum(1<<n for n,(dx,dz) in enumerate(steps)
                    if 0<=x+dx<width and 0<=z+dz<height and (x+dx,z+dz) not in missing
                    and (x,z) not in missing and not (x==8 and dx==1 and dz!=0)))
                hazards.append(4 if x==9 and 2<=z<=5 else 1 if x==4 and z==5 else 2 if x==11 and z==10 else 0)
        return dict(format='offline-lan-0922-navgraph',version=2,width=width,height=height,
                    heights_mm=heights,links=links,hazards=hazards,origin=(-28.,-28.),cell_size=4.,bounds=(-30.,-30.,30.,30.))

    def compare(self,cases):
        graph=self.graph()
        rows=['15 15 4 -28 -28 -30 -30 30 30']
        for h,l,v in zip(graph['heights_mm'],graph['links'],graph['hazards']):
            rows.append('%d %.17g %d %d'%(h is not None,0 if h is None else h/1000.,l,v))
        for case in cases:
            parts=[case[0]]
            if case[0]=='hulls':
                hulls,padding,a,b=case[1:];parts.extend([len(hulls),padding])
                for hull in hulls:parts.extend(hull)
                parts.extend(a);parts.extend(b)
            elif case[0]=='path':
                parts.append(len(case[1]))
                for point in case[1]:parts.extend(point)
            else:
                for value in case[1:]:parts.extend(value if isinstance(value,(tuple,list)) else [value])
            rows.append(' '.join(map(str,parts)))
        run=subprocess.run([str(self.binary)],input='\n'.join(rows)+'\n',text=True,capture_output=True)
        self.assertEqual(run.returncode,0,run.stderr)
        actual=[json.loads(line) for line in run.stdout.splitlines()]
        oracle=subprocess.run([self.py27,'-B',str(self.oracle),str(self.source/'src/res/scripts/client')],
            input=json.dumps([graph,cases]),text=True,capture_output=True)
        self.assertEqual(oracle.returncode,0,oracle.stderr)
        expected=json.loads(oracle.stdout)
        def equal(a,b,context):
            if isinstance(b,list):
                self.assertIsInstance(a,list,context);self.assertEqual(len(a),len(b),context)
                for i,(av,bv) in enumerate(zip(a,b)):equal(av,bv,context+'[%d]'%i)
            elif isinstance(b,(float,int)) and not isinstance(b,bool):self.assertAlmostEqual(a,b,places=9,msg=context)
            else:self.assertEqual(a,b,context)
        for i,(a,b) in enumerate(zip(actual,expected)):equal(a,b,repr(cases[i]))
        self.assertEqual(len(actual),len(expected))

    def test_grid_corridors_hazards_and_hull_footprints(self):
        rng=random.Random(219)
        self.compare([['geometry',tuple(rng.uniform(-35,35) if i!=1 else 0. for i in range(3)),
            tuple(rng.uniform(-35,35) if i!=1 else 0. for i in range(3)),rng.uniform(-3,3),rng.uniform(.1,6),rng.uniform(.1,3)] for _ in range(350)])

    def test_native_review_and_streaming_receipts(self):
        rng=random.Random(119)
        self.compare([['proof',(rng.uniform(-29,29),0.,rng.uniform(-29,29)),
                       (rng.uniform(-29,29),0.,rng.uniform(-29,29)),n%4,1] for n in range(100)])

    def test_safe_local_candidate_order(self):
        rng=random.Random(1819)
        self.compare([['local',(rng.uniform(-29,29),0.,rng.uniform(-29,29)),
                       (rng.uniform(-29,29),0.,rng.uniform(-29,29)),(-1,1)[n%2],(0.,.5)[n%2],n%4] for n in range(100)])

    def test_static_hulls_and_changed_edge_revision(self):
        hulls=[[1,0.,0.,.4,3.5,1.7],[2,12.,-12.,-.2,4.,1.8]]
        self.compare([['hulls',hulls,1.7,(-20.,0.,0.),(20.,0.,0.)],
            ['hulls',hulls,1.7,(-20.,0.,0.),(20.,0.,0.)],
            ['hulls',[],1.7,(-20.,0.,0.),(20.,0.,0.)]])

    def test_climb_and_clearance_shortcut_guards(self):
        self.compare([['path',[(0.,0.,0.),(0.,0.,4.),(4.,3.,4.)]],
            ['path',[(0.,0.,0.),(0.,2.,4.),(0.,4.,8.)]],
            ['path',[(-20.,0.,-20.),(-20.,0.,0.),(0.,0.,0.)]]])


if __name__=='__main__':unittest.main()
