"""Persistent native navigator traces against the current release's Python owner."""
import inspect
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import test_stock_native_bot_navigation as grid_test
ROOT=grid_test.ROOT


def reference(payload):
    from gui.mods.offline_lan_0922.ai.navigation import TerrainNavigator
    graph,cases=payload;policy=[0]
    def ground(x,z,hint):return None if policy[0]==3 else .01*x+.02*z
    def obstacle(a,b,width):return 'deferred' if policy[0]==2 else policy[0]==1
    class Oracle(object):
        def run(self,*args,**kwargs):return (-1 if policy[0]==2 else 0 if policy[0]==1 else 1,)
    class Job(object):
        def __init__(self,a,b,now,revision):
            self.done=False;self.result=None;self.hull_revision=revision;self.steps=0;self.progress=dict(start=a,goal=b)
        def proved_prefix(self,grid):return ()
    class Queue(object):
        def __init__(self):self.jobs=[];self.mode=0
        def submit(self,a,b,**kwargs):
            job=Job(a,b,kwargs['now'],nav.grid.static_hull_revision);self.jobs.append(job);return job
        def advance(self,now,budget):
            if self.mode:
                for job in self.jobs:
                    if not job.done:
                        job.done=True;job.result=(job.progress['start'],job.progress['goal']) if self.mode==1 else ()
        def cancel(self,jobs):
            for job in jobs:job.done=True;job.result=()
        def close(self):self.cancel(self.jobs)
    nav=TerrainNavigator(ground,obstacle,baked_graph=graph);nav.grid.native_query_oracle=Oracle();queue=Queue();nav._native_navigation=queue
    out=[]
    for c in cases:
        op,actor,current,goal,now,kind,identity,movement,yaw,policy[0],queue.mode=c[:11]
        key=(kind,actor,identity) if kind=='local' else (kind,identity)
        result=None;verdict=False
        if op=='next':result=nav.next_target(actor,current,goal,key,now,movement_intent=bool(movement))
        elif op=='direct':result=nav.observe_direct_target(actor,current,goal,key,now,bool(movement))
        elif op=='retained':result=nav.retained_local_target(actor,current,goal,key,now,bool(movement),yaw)
        elif op=='hard':verdict=nav.report_hard_contact(actor,current,goal,yaw,now)
        elif op=='blocked':verdict=nav.report_blocked_step(actor,current,goal,now)
        elif op=='replan':verdict=nav.request_replan(actor,current,now)
        elif op=='clear':verdict=nav.clear_blocked_contact(actor)
        elif op=='review':verdict=nav.report_blocked_plan(current,goal)
        elif op=='remember':nav.remember_runtime_target(actor,current,goal,c[11],now)
        s=nav.bot_states.get(actor)
        state=None if s is None else [s.get('navigation_status'),bool(s.get('target_is_terminal')),s.get('index',0),s.get('replan_generation',0),
            s.get('macro_progress_replans',0),s.get('blocked_step_replans',0),bool(s.get('replan_active')),bool(s.get('temporary_stalled')),
            s.get('local_fallback_target'),s.get('last_target'),s.get('controlled_shallow_target'),
            nav.driver_progress_target(actor,current,s.get('last_target') or current,now)]
        out.append([result,verdict,state])
    return out


def event(op='next',actor=1,current=(-20.,0.,0.),goal=(20.,0.,0.),now=0.,kind='local',identity='test',movement=1,yaw=0.,policy=0,queue=0):
    return [op,actor,current,goal,now,kind,identity,movement,yaw,policy,queue]


class NativeNavigatorTests(unittest.TestCase):
    graph=grid_test.NativeNavigationTests.graph
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-navigator-');cls.binary=Path(cls.tmp.name)/'navigator'
        compiled=subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-pthread',
            '-I'+str(ROOT/'bigworld'),'-I'+str(cls.source/'native'),str(ROOT/'bigworld/stock_native_bot_navigation.cpp'),
            str(ROOT/'bigworld/stock_native_bot_navigator.cpp'),str(cls.source/'native/offline_navigation.cpp'),str(cls.source/'native/offline_async_worker.cpp'),
            str(ROOT/'tests/stock_native_bot_navigator_host.cpp'),'-o',str(cls.binary)],capture_output=True,text=True)
        if compiled.returncode:raise RuntimeError(compiled.stderr)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        cls.oracle=Path(cls.tmp.name)/'oracle.py'
        cls.oracle.write_text('import json, os, sys, types\nsys.path.insert(0,sys.argv[1])\nfor name in ("gui", "gui.mods"):\n'
            '    package=types.ModuleType(name)\n    package.__path__=[os.path.join(sys.argv[1],*name.split("."))]\n    sys.modules[name]=package\n'+
            inspect.getsource(reference)+'\nprint(json.dumps(reference(json.load(sys.stdin))))\n')
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()
    def compare(self,cases):
        graph=self.graph();rows=['15 15 4 -28 -28 -30 -30 30 30']
        for h,l,v in zip(graph['heights_mm'],graph['links'],graph['hazards']):rows.append('%d %.17g %d %d'%(h is not None,0 if h is None else h/1000.,l,v))
        for c in cases:
            values=[]
            for value in c:values.extend(value if isinstance(value,(tuple,list)) else [value])
            rows.append(' '.join(map(str,values)))
        run=subprocess.run([str(self.binary)],input='\n'.join(rows)+'\n',text=True,capture_output=True);self.assertEqual(run.returncode,0,run.stderr)
        actual=[json.loads(line) for line in run.stdout.splitlines()]
        oracle=subprocess.run([self.py27,'-B',str(self.oracle),str(self.source/'src/res/scripts/client')],input=json.dumps([graph,cases]),text=True,capture_output=True)
        self.assertEqual(oracle.returncode,0,oracle.stderr);expected=json.loads(oracle.stdout.splitlines()[-1])
        def equal(a,b,context):
            if isinstance(b,list):
                self.assertIsInstance(a,list,context);self.assertEqual(len(a),len(b),context)
                for i,(av,bv) in enumerate(zip(a,b)):equal(av,bv,context+'[%d]'%i)
            elif isinstance(b,(float,int)) and not isinstance(b,bool):self.assertAlmostEqual(a,b,places=8,msg=context)
            else:self.assertEqual(a,b,context)
        self.assertEqual(len(actual),len(expected))
        for i,(a,b) in enumerate(zip(actual,expected)):equal(a,b,'case %d %s'%(i,repr(cases[i])))
    def test_direct_route_progress_and_tactical_holds(self):
        self.compare([event(current=(0.,0.,float(n)),goal=(0.,0.,24.),now=n*.2,kind='route') for n in range(20)]+
            [event(current=(0.,0.,19.),goal=(0.,0.,24.),now=5.,kind='route',movement=0)])
    def test_pending_short_leg_lease_and_completed_search(self):
        cases=[event(current=(-17.,0.,-16.),goal=(20.,0.,20.),now=n*.2,queue=0) for n in range(12)]
        cases += [event(current=(-17.,0.,-16.),goal=(20.,0.,20.),now=3.+n*.2,queue=2) for n in range(10)]
        self.compare(cases)
    def test_stable_hard_contact_edge_and_reverse_recovery(self):
        cases=[event(current=(0.,0.,0.),goal=(0.,0.,24.))]
        cases += [event('hard',current=(0.,0.,0.),goal=(0.,0.,24.),now=.25*n,yaw=(-.1,.1)[n%2]) for n in range(1,7)]
        cases += [event('next',current=(0.,0.,0.),goal=(0.,0.,24.),now=2.)]
        cases += [event('hard',current=(0.,0.,0.),goal=(0.,0.,24.),now=3.+.25*n,yaw=math.pi+(-.1,.1)[n%2]) for n in range(7)]
        cases += [event('clear',current=(0.,0.,0.),goal=(0.,0.,24.),now=6.)]
        self.compare(cases)
    def test_macro_stall_and_goal_identity_changes(self):
        cases=[event(current=(0.,0.,0.),goal=(0.,0.,24.),now=n*.5) for n in range(30)]
        cases += [event(current=(0.,0.,0.),goal=(20.,0.,0.),now=16.+n*.5,identity='new') for n in range(8)]
        self.compare(cases)
    def test_direct_watchdog_escape_and_displacement(self):
        self.compare([event('direct',current=(0.,0.,0.),goal=(0.,0.,24.),now=n*.5) for n in range(32)]+
                     [event('direct',current=(1.,0.,2.),goal=(0.,0.,24.),now=17.)])
    def test_local_target_heading_lease_and_manual_replan(self):
        cases=[event(current=(0.,0.,0.),goal=(0.,0.,24.)),event('remember',current=(0.,0.,0.),goal=(0.,0.,24.),now=.1)+[(3.,0.,3.)]]
        cases += [event('retained',current=(0.,0.,0.),goal=(0.,0.,24.),now=n*.5,yaw=n*.03) for n in range(1,25)]
        cases += [event('retained',current=(3.5,0.,3.5),goal=(0.,0.,24.),now=13.),event('replan',current=(3.5,0.,3.5),goal=(0.,0.,24.),now=14.)]
        self.compare(cases)

if __name__=='__main__':unittest.main()
