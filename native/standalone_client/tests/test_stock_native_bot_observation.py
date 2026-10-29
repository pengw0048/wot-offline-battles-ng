"""Native supplemental lane scheduling against the fixed Python 2.7 owner."""
import ast
import inspect
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest
ROOT=Path(__file__).resolve().parents[1]


def reference(cases):
    runtime=Runtime();runtime._combat_diagnostics=None;runtime._shot_los_deadlines={};runtime._shot_los_cache={};runtime._incoming_lanes={};runtime._shot_lane_completed_pairs=0;runtime._shot_lane_budget_deferred_attempts=0;runtime._probe_totals=[0,0,0];runtime._probe_started=lambda:None;runtime._probe_finished=lambda *args:None
    runtime._reset_shot_lane_work();runtime._ordered_states=lambda:[runtime.states[i] for i in sorted(runtime.states)]
    runtime._bot_observation_target=lambda i,raw,*args:dict(raw,kind='bot',network_id=i,position=_position(raw))
    runtime._human_observation_target=lambda raw,tick,i:dict(raw,kind='human',network_id=i,position=_position(raw))
    runtime.incoming_lane_probe=lambda source,target:None
    calls=[]
    def probe(source,target):
        calls.append([source['id'],target['kind'],target['network_id']])
        return (source['id']+target['network_id']+int(math.floor(runtime.now*10)))%3!=0
    runtime.firing_lane_probe=probe
    next_lane=next_observation=next_cover=0.;cover_queue=[];cover_cursor=0;result=[]
    for now,count,fresh,dead,refresh,publish,selected,collect_cover,distance,inject in cases:
        runtime.now=now;runtime.states=dict((i,dict(id=i,team=1,alive=i!=dead,x=float(i),y=0.,z=0.,profile=dict(class_tag='mediumTank' if i%9 else 'SPG'))) for i in range(1,count+1))
        runtime.states[201]=dict(id=201,team=2,alive=True,x=0.,y=0.,z=distance)
        players=[dict(id=202,team=2,alive=True,x=0.,y=0.,z=distance)]
        if inject:runtime._shot_los_cache[(inject,'bot',201)]=(now,True)
        observation=bool(publish and refresh and now>=next_observation)
        # These fixtures deliberately have no remembered team pose: queued
        # cover work retires as stale and cannot reserve the lane window.
        cover_reserved=False
        lanes=bool(refresh and (now>=next_lane or (next_lane>0. and now+1.+1e-9>=next_lane)))
        cover_jobs=bool(refresh and publish and not cover_queue and now>=next_cover)
        team_visibility={(1,'bot',201):bool(fresh),(1,'human',202):bool(fresh)}
        priorities={}
        for i in range(1,count+1):
            if i==dead:continue
            if lanes and fresh and math.hypot(float(i),distance)<=50.:priorities[(i,'bot',201)]=-1
            if selected:priorities[(i,'human' if selected==2 else 'bot',202 if selected==2 else 201)]=0
        cycle=next_lane;runtime._incoming_lane_budget=1 if refresh else 0
        if lanes:
            pending=runtime._service_shot_lane_work(now,cycle,team_visibility,players,{},set(),priorities,[16])
            if now>=cycle and pending<=0:next_lane=now+1.
        if observation:next_observation=now+.4
        if cover_jobs:
            next_cover=now+1.
            jobs=[i for i in range(1,count+1) if i!=dead] if collect_cover else []
            if jobs:
                start=cover_cursor%len(jobs);ordered=jobs[start:]+jobs[:start];n=min(3,len(jobs));cover_cursor=(start+n)%len(jobs)
                for index,i in enumerate(ordered[:n]):cover_queue.append((now+.5*float(index)/n,i))
        if refresh and cover_queue and now+1e-9>=cover_queue[0][0]:cover_queue.pop(0)
        result.append([observation,lanes,cover_jobs,cover_reserved,cycle,len(runtime._shot_lane_work_set),len(cover_queue),list(calls),[list(k)+list(v) for k,v in sorted(runtime._shot_los_cache.items())]])
    return result


class NativeObservationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-observation-');cls.binary=Path(cls.tmp.name)/'host'
        owned=['observation_runtime','decision','parking','planner','strategy','navigation','navigator','perception','step_state','weapon','artillery']
        release=['offline_navigation','offline_async_worker','offline_destructible_geometry','world_stage','offline_simulation_control','offline_visibility','native_visibility_core','offline_simulation_weapons']
        command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),'-I'+str(cls.source/'native')]
        command += [str(ROOT/'bigworld'/('stock_native_bot_'+name+'.cpp')) for name in owned]
        command += [str(ROOT/'bigworld'/(name+'.cpp')) for name in ('stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin')]
        command += [str(cls.source/'native'/(name+'.cpp')) for name in release]
        command += [str(ROOT/'tests/stock_native_bot_observation_host.cpp'),'-o',str(cls.binary)]
        built=subprocess.run(command,capture_output=True,text=True)
        if built.returncode:raise RuntimeError(built.stderr)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        text=(cls.source/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py').read_text();tree=ast.parse(text)
        functions='\n\n'.join(ast.get_source_segment(text,n) for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('_number','_point','_position','_distance','_copy_runtime_state'))
        names=['_reset_shot_lane_work','_shot_los_key','_shot_los_phase','_queue_shot_lane_identity','_prepare_shot_lane_work','_shot_lane_live_records','_service_shot_lane_work','_shot_lane_query_distance','_shot_clear','_refresh_shot_clear']
        body=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='BotRuntime');methods=[]
        for n in body.body:
            if isinstance(n,ast.FunctionDef) and n.name in names:
                prefix='    @staticmethod\n' if any(isinstance(d,ast.Name) and d.id=='staticmethod' for d in n.decorator_list) else ''
                methods.append(prefix+textwrap.indent(textwrap.dedent(ast.get_source_segment(text,n)),'    '))
        cls.oracle=Path(cls.tmp.name)/'oracle.py';cls.oracle.write_text('''import sys,json,math
_CRITICAL_PARTS_TICK_CACHE="_critical_parts_tick_cache"
_GUN_PITCH_LIMIT_CACHE="_gun_pitch_limit_cache"
SHOT_LANE_PHASES=29
SHOT_LANE_REFRESH_SECONDS=1.
SHOT_LANE_SECONDS=.2
SPG_SHOT_LANE_QUERY_DISTANCE=2500.
SHOT_LANE_QUERY_DISTANCE=585.
'''+functions+'\nclass Runtime(object):\n'+'\n\n'.join(methods)+'\n'+inspect.getsource(reference)+'\nprint(json.dumps(reference(json.load(sys.stdin))))\n')
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()
    def compare(self,cases):
        run=subprocess.run([str(self.binary)],input='\n'.join(' '.join(map(str,row)) for row in cases)+'\n',capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr)
        expected=subprocess.run([self.py27,'-B',str(self.oracle)],input=json.dumps(cases),capture_output=True,text=True);self.assertEqual(expected.returncode,0,expected.stderr)
        rows=[json.loads(l) for l in run.stdout.splitlines()];wanted=json.loads(expected.stdout.splitlines()[-1]);self.assertEqual(len(rows),len(wanted))
        def equal(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):equal(x,y,path+'[%d]'%i)
            elif isinstance(b,(float,int,bool)):self.assertAlmostEqual(a,b,places=8,msg=path)
            else:self.assertEqual(a,b,path)
        for i,(a,b) in enumerate(zip(rows,wanted)):equal(a,b,str(i))
    def test_cover_native_budgeted_geometry_and_deferred_publication(self):
        run=subprocess.run([str(self.binary),'cover'],capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr)
        row=json.loads(run.stdout)
        self.assertEqual(row[:3],['1:0:0:1',[0.,0.,0.],[-6.,0.,0.]])
        self.assertEqual(row[3:6],[0.,.5,1.])
        self.assertAlmostEqual(row[6],1./3.)
        self.assertEqual(row[7:],[0.,-1.,0.])
    def test_persistent_roster_fairness_hidden_debt_and_range(self):
        cases=[]
        for i in range(50):cases.append([i*.1,40 if i<25 else 37,int(i%8!=0),7 if i>10 else 0,int(i%7!=0),1,1 if i%3 else 2,0,700. if i%11==0 else 120.,i%12+1 if i%4==0 else 0])
        self.compare(cases)
    def test_close_urgency_and_stale_cover_jobs_independent_clocks(self):
        self.compare([[i*.05,12,1,0,1,1,1 if i%3 else 0,1,20.,0] for i in range(50)])

if __name__=='__main__':unittest.main()
