"""Release runtime lane ownership, occupied endpoint and radio target parity."""
import ast
import inspect
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest
import test_stock_native_bot_navigation as grid_test
ROOT=grid_test.ROOT


def reference(payload):
    from gui.mods.offline_lan_0922.ai.navigation import TerrainNavigator
    graph,roster,cases=payload
    def ground(x,z,hint):return .01*x+.02*z
    def obstacle(*args):return False
    nav=TerrainNavigator(ground,obstacle,baked_graph=graph)
    class Job(object):
        done=False;result=None;steps=0
        def proved_prefix(self,grid):return ()
    class Queue(object):
        def submit(self,a,b,**kwargs):
            job=Job();job.hull_revision=nav.grid.static_hull_revision;job.progress=dict(start=a,goal=b);return job
        def advance(self,*args):pass
        def cancel(self,jobs):
            for job in jobs:job.done=True;job.result=()
    nav._native_navigation=Queue()
    runtime=Runtime();runtime.navigator=nav;runtime.baked_graph=graph;runtime.states={};runtime._server_orders={};runtime._contact_players=()
    runtime.adapter=type('Adapter',(),{'driver':type('Driver',(),{'states':{}})()})();runtime._artillery_intents={};runtime._artillery_reproofs={}
    for raw in roster:
        actor,team,slot,alive,x,y,z,yaw,length,width,join=raw
        runtime.states[actor]=dict(id=actor,team=team,slot=slot,alive=bool(alive),x=x,y=y,z=z,yaw=yaw,half_length=length,half_width=width,
            collision_shape=(width,length,-1.,2.),route=dict(id='r',waypoints=[(-20.,0.),(20.,0.)]),route_index=1,route_join=bool(join))
        runtime._server_orders[actor]=dict(combat_mode='route',route_id='r',route_index=1,route_join=bool(join),route_anchor=(-20.,0.,0.))
    out=[]
    for c in cases:
        op,actor,current,goal,now,join=c[:6];own=runtime.states[actor];own.update(x=current[0],y=current[1],z=current[2])
        strategy=dict(runtime._server_orders[actor],route_join=bool(join));runtime._server_orders[actor]=strategy
        if op=='bind':
            result=runtime._route_lane_binding(actor,(own['team'],'r'),goal,strategy)
            out.append(list(result)+[[[i,s.get('_route_lane_desired',0.),s.get('_route_lane_row_desired',0.),int('_route_lane_group' in s)] for i,s in sorted(runtime.states.items())]])
        elif op=='goal':
            result=runtime._route_lane_goal(actor,current,goal,strategy,now,bool(join));out.append([result[0]]+list(result[1]))
        elif op=='lane':out.append(runtime._route_lane_target(actor,current,goal,c[6],strategy,now))
        elif op=='radio':
            strategy.update(move_area_bounds=c[6],team_command_id='cmd');out.append(runtime._radio_ground_goal(actor,current,goal,strategy,nav.grid))
        elif op=='occupied':out.append([runtime._occupied_route_target(actor,current,goal,now,bool(join)),bool(own.get('route_wreck_blocked'))])
        elif op=='nav':
            state=dict(now=now,speed=0.,yaw=own['yaw']);target=runtime._navigation_target(actor,current,goal,strategy,state)
            out.append([target,bool(state.get('navigation_stop_at_target')),state.get('navigation_status','pending'),state.get('navigation_progress_target')])
        elif op=='alive':own['alive']=bool(join);out.append(None)
    return out


class NativeRuntimeNavigationTests(unittest.TestCase):
    graph=grid_test.NativeNavigationTests.graph
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-nav-runtime-');cls.binary=Path(cls.tmp.name)/'runtime'
        compiled=subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-pthread',
            '-I'+str(ROOT/'bigworld'),'-I'+str(cls.source/'native'),str(ROOT/'bigworld/stock_native_bot_navigation.cpp'),
            str(ROOT/'bigworld/stock_native_bot_navigator.cpp'),str(ROOT/'bigworld/stock_native_bot_navigation_runtime.cpp'),
            str(cls.source/'native/offline_navigation.cpp'),str(cls.source/'native/offline_async_worker.cpp'),
            str(ROOT/'tests/stock_native_bot_navigation_runtime_host.cpp'),'-o',str(cls.binary)],capture_output=True,text=True)
        if compiled.returncode:raise RuntimeError(compiled.stderr)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        source_text=(cls.source/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py').read_text();tree=ast.parse(source_text)
        names=['_number','_position','_value','_distance','_point','_route_lane_layout','_route_row_layout','_route_lane_fallbacks','_route_row_fallbacks']
        functions='\n\n'.join(ast.get_source_segment(source_text,n) for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names)
        methods=['_route_lane_cohort','_route_waypoint_xz','_route_lane_forward','_route_lane_binding','_route_lane_leg_forward','_safe_route_lane_goal',
            '_navigation_bot_state','_route_lane_goal','_route_lane_target','_radio_ground_goal','_occupied_route_target','_navigation_target','_baked_boundary_overflow','_physical_parking_occupancy']
        body=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='BotRuntime')
        method_text=[]
        for node in body.body:
            if isinstance(node,ast.FunctionDef) and node.name in methods:
                decorated=('    @staticmethod\n' if node.name=='_route_waypoint_xz' else '')
                method_text.append(decorated+textwrap.indent(textwrap.dedent(ast.get_source_segment(source_text,node)),'    '))
        cls.oracle=Path(cls.tmp.name)/'oracle.py'
        cls.oracle.write_text('''import json,os,sys,types,math
sys.path.insert(0,sys.argv[1])
for name in ('gui','gui.mods'):
    p=types.ModuleType(name);p.__path__=[os.path.join(sys.argv[1],*name.split('.'))];sys.modules[name]=p
from gui.mods.offline_lan_0922.ai import driver as ai_driver
BAKED_FATAL_HAZARDS=3
BAKED_SHALLOW_WATER=4
BAKED_MOTION_LOOKAHEAD_SECONDS=1.5
ROUTE_LANE_ORDER=(-10.,-5.,0.,5.,10.)
ROUTE_JOIN_ROW_LIMIT=4.
ROUTE_JOIN_ARRIVAL_RADIUS=13.
TACTICAL_REFRESH_SECONDS=1.
'''+functions+'\nclass Runtime(object):\n'+'\n\n'.join(method_text)+'\n'+inspect.getsource(reference)+'\nprint(json.dumps(reference(json.load(sys.stdin))))\n')
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()
    def compare(self,roster,cases):
        graph=self.graph();rows=['15 15 4 -28 -28 -30 -30 30 30']
        for h,l,v in zip(graph['heights_mm'],graph['links'],graph['hazards']):rows.append('%d %.17g %d %d'%(h is not None,0 if h is None else h/1000.,l,v))
        rows.append(str(len(roster)));rows.extend(' '.join(map(str,r)) for r in roster)
        for c in cases:
            values=[]
            for value in c:values.extend(value if isinstance(value,(tuple,list)) else [value])
            rows.append(' '.join(map(str,values)))
        actual=subprocess.run([str(self.binary)],input='\n'.join(rows)+'\n',capture_output=True,text=True);self.assertEqual(actual.returncode,0,actual.stderr)
        expected=subprocess.run([self.py27,'-B',str(self.oracle),str(self.source/'src/res/scripts/client')],input=json.dumps([graph,roster,cases]),capture_output=True,text=True)
        self.assertEqual(expected.returncode,0,expected.stderr)
        rows=[json.loads(line) for line in actual.stdout.splitlines()];expected=json.loads(expected.stdout.splitlines()[-1])
        def equal(a,b,context):
            if isinstance(b,list):
                self.assertIsInstance(a,list,context);self.assertEqual(len(a),len(b),context)
                for n,(av,bv) in enumerate(zip(a,b)):equal(av,bv,context+'[%d]'%n)
            elif isinstance(b,(float,int)) and not isinstance(b,bool):self.assertAlmostEqual(a,b,places=8,msg=context)
            else:self.assertEqual(a,b,context)
        self.assertEqual(len(rows),len(expected))
        for n,(a,b) in enumerate(zip(rows,expected)):equal(a,b,str(cases[n]))
    def roster(self,count=10):return [[i+1,1,i,1,-20.+(i%3)*4.,0.,-15.+i*3.,0.,3.5,1.7,1] for i in range(count)]
    def test_lane_cohort_stable_across_progress(self):
        roster=self.roster(15);cases=[]
        for i in range(1,16):cases.append(['bind',i,tuple(roster[i-1][4:7]),(20.,0.,0.),0.,1])
        for i in range(1,16):cases.append(['bind',i,(i-8.,0.,8.),(0.,0.,24.),5.,0])
        self.compare(roster,cases)
    def test_lane_goals_bounds_hazards_and_nonjoin_offsets(self):
        roster=self.roster(8)
        cases=[['goal',i,tuple(roster[i-1][4:7]),(20.,0.,0.),0.,1] for i in range(1,9)]
        cases += [['lane',i,(-12.,0.,0.),(20.,0.,0.),2.,0,(4.,0.,0.)] for i in range(1,9)]
        self.compare(roster,cases)
    def test_radio_ground_goal_dry_preference_and_cache(self):
        self.compare(self.roster(1),[['radio',1,(-20.,0.,0.),(8.,0.,-16.),0.,0,(0.,-24.,16.,-8.)],
            ['radio',1,(-20.,0.,0.),(16.,0.,-8.),1.,0,(0.,-24.,16.,-8.)],
            ['radio',1,(-20.,0.,0.),(100.,0.,100.),2.,0,(80.,80.,120.,120.)]])
    def test_wreck_occupied_point_persistent_bypass(self):
        roster=self.roster(2);roster[1][3]=0;roster[1][4:7]=[8.,0.,0.]
        self.compare(roster,[['occupied',1,(-12.,0.,0.),(8.,0.,0.),0.,0],
            ['occupied',1,(-11.,0.,1.),(8.,0.,0.),.5,0],['occupied',1,(-10.,0.,2.),(8.,0.,0.),2.,0],
            ['occupied',1,(-10.,0.,2.),(8.,0.,0.),3.,1]])
    def test_complete_navigation_target_direct_and_join(self):
        roster=self.roster(3)
        self.compare(roster,[['nav',1,(-20.,0.,0.),(20.,0.,0.),0.,1],
            ['nav',2,(-20.,0.,4.),(20.,0.,0.),.2,1],['nav',1,(-18.,0.,0.),(20.,0.,0.),1.,1],
            ['nav',1,(0.,0.,0.),(0.,0.,12.),2.,0],['nav',2,(0.,0.,4.),(0.,0.,12.),3.,0]])

if __name__=='__main__':unittest.main()
