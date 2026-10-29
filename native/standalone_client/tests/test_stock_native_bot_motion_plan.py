"""Selected motion proofs and fair receipt scheduling against CPython 2.7."""
import ast
import inspect
import json
import os
from pathlib import Path
import random
import subprocess
import tempfile
import textwrap
import unittest
ROOT=Path(__file__).resolve().parents[1]


def reference(cases):
    class Box(object):pass
    runtime=BotRuntime();runtime._motion_probe_cache={};runtime._world_receipt_waiting=[];runtime._world_receipt_frame=None
    runtime._turn_speeds={};runtime.states={};runtime._probe_totals=[0]*5
    runtime._probe_started=lambda:None;runtime._probe_finished=lambda *args:None
    runtime.adapter=Box();runtime.adapter.driver=Box();runtime.navigator=Box();runtime.navigator.grid=Box()
    runtime.navigator.report_blocked_step=lambda *args:None
    runtime.navigator.report_blocked_corridor=lambda *args:None
    runtime.adapter.driver.remember_failure=lambda *args:None
    runtime._traffic_coordinator=Box();runtime._traffic_coordinator.clearance_distance=lambda *args:runtime.remaining
    directions=[0];receipts=[0]
    def direction(*args):directions[0]+=1;return None if runtime.kind<0 else dict(runtime.sample)
    def receipt(p,yaw,speed,descriptor,maximum):
        receipts[0]+=1
        if runtime.receipt_kind==0:return None
        if runtime.receipt_kind==1:return False
        if runtime.receipt_kind==2:return 'deferred'
        return dict(origin=p,yaw=yaw,distance=min(15.,maximum if maximum is not None else 15.),half_width=1.6,leading=3.5,direction=-1 if speed<0. else 1)
    runtime._probe_direction=direction;runtime.world_receipt_probe=receipt
    def sample(value):
        if value is None:return None
        r=value.get('world_receipt')
        return [value.get(k,False) for k in ('clear','collision','water','slope','slope_valid','probe_failed','deferred','_world_receipt_pending')]+[
            list(r['origin'])+[r[k] for k in ('yaw','distance','half_width','leading','direction')] if r else None]
    result=[]
    for row in cases:
        if row[0]=='begin':runtime.states=dict((i,dict(alive=True)) for i in row[1:]);runtime._begin_world_receipt_frame();continue
        if row[0]=='end':runtime._finish_world_receipt_frame();result.append(runtime._world_receipt_waiting);continue
        (op,actor,x,y,z,yaw,speed,turn_speed,throttle,turn,step,now,half_length,terrain_pitch,grounded,airborne,due,refresh,exact,baked,intent,cell,recovery,has_move,mx,my,mz,has_probe,probe,has_remaining,remaining,kind,clear,collision,water,slope,slope_valid,failed,deferred,receipt_kind)=row
        runtime.kind=kind;runtime.receipt_kind=receipt_kind;runtime.sample=dict(clear=clear,collision=collision,water=water,slope=slope,slope_valid=slope_valid,probe_failed=failed,deferred=deferred)
        runtime._turn_speeds[actor]=turn_speed;runtime.navigator.grid.prebaked=baked;runtime.navigator.grid.cell_size=cell
        runtime.remaining=remaining if has_remaining else None;runtime.baked_graph={};runtime.motion_resolver=(lambda:None) if exact else None
        state=dict(id=actor,x=x,y=y,z=z,yaw=yaw,speed=speed,half_length=half_length,terrain_pitch=terrain_pitch,grounded_once=grounded,airborne=airborne)
        command=dict(recovery_mode=recovery,movement_intent=intent)
        if has_move:command['move_position']=(mx,my,mz)
        if has_probe:command['recovery_probe_distance']=probe
        out=runtime.selected(state,command,throttle,turn,step,now,bool(due),bool(refresh))
        cached=runtime._motion_probe_cache.get(actor)
        out+=[sample(out.pop())]
        out += [[cached['deadline'],cached['probe_distance'],cached['probe_leading'],sample(cached['result'])] if cached else None,directions[0],receipts[0]]
        result.append(out)
    return result


class NativeMotionPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-motion-plan-');cls.binary=Path(cls.tmp.name)/'host'
        result=subprocess.run(['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),'-I'+str(cls.source/'native'),
            str(ROOT/'bigworld/stock_native_bot_motion_plan.cpp'),str(ROOT/'bigworld/stock_native_bot_candidate.cpp'),str(ROOT/'bigworld/stock_native_bot_navigation.cpp'),str(ROOT/'bigworld/stock_native_bot_navigator.cpp'),str(cls.source/'native/offline_navigation.cpp'),str(cls.source/'native/offline_async_worker.cpp'),str(ROOT/'tests/stock_native_bot_motion_plan_host.cpp'),'-o',str(cls.binary)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stderr)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        text=(cls.source/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py').read_text();tree=ast.parse(text)
        names=['_number','_point','_distance','_angle_delta','_motion_probe_deadline','_motion_drive_pitch']
        functions='\n\n'.join(ast.get_source_segment(text,n) for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names)
        methods=['_begin_world_receipt_frame','_finish_world_receipt_frame','_probe_world_receipt','_call_world_receipt_probe','_probe_is_clear',
                 '_world_receipt_contains','_world_receipt_refresh_due','_contained_cached_world_receipt','_motion_probe_reusable','_motion_probe_covers_distance']
        body=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='BotRuntime');chunks=[]
        for node in body.body:
            if isinstance(node,ast.FunctionDef) and node.name in methods:
                prefix='    @staticmethod\n' if any(isinstance(d,ast.Name) and d.id=='staticmethod' for d in node.decorator_list) else ''
                chunks.append(prefix+textwrap.indent(textwrap.dedent(ast.get_source_segment(text,node)),'    '))
        start=text.index('            travel_sign = -1.0 if (',text.index('            cached_motion_probe =')-7000)
        end=text.index('            steer_dir = 0',start)
        selected='    def selected(self,state,command,throttle,turn,step,now,decision_due,refresh_control):\n        position=(state["x"],state["y"],state["z"]);attempted_yaws={};drive_targets={};descriptor=None\n'
        selected+=textwrap.indent(textwrap.dedent(text[start:end]),'        ')
        selected+='''        return [throttle,turn,travel_sign,travel_yaw,_motion_drive_pitch(motion_probe,travel_sign,state),1 if throttle>.01 else -1 if throttle<-.01 else 0,1 if turn>.01 else -1 if turn<-.01 else 0,path_clear,pose_frozen,bool(motion_probe and motion_probe.get('collision')),exact_motion_owns_collision,not path_clear and not probe_deferred,not path_clear and not probe_deferred and not motion_probe.get('collision') and command.get('move_position') is not None,motion_probe_reusable,catchup_motion_hold,catchup_motion_reprobe,maximum_probe_distance,motion_probe]\n'''
        cls.oracle=Path(cls.tmp.name)/'oracle.py'
        cls.oracle.write_text('''import math,json,sys,types,os
sys.path.insert(0,sys.argv[1])
for name in ('gui','gui.mods'):
    m=types.ModuleType(name);m.__path__=[os.path.join(sys.argv[1],*name.split('.'))];sys.modules[name]=m
from gui.mods.offline_lan_0922.ai import driver as ai_driver
combat_count=lambda *args:None
current_combat=lambda:None
MAX_WORLD_RECEIPTS_PER_FRAME=13
MOTION_PROBE_SECONDS=.15
MOTION_PROBE_FORWARD_BUDGET=3.5
MOTION_PROBE_LATERAL_BUDGET=1.
MAX_CONTROL_ELAPSED_SECONDS=.2
BAKED_MOTION_LOOKAHEAD_SECONDS=1.5
'''+functions+'\nclass BotRuntime(object):\n'+'\n\n'.join(chunks)+'\n'+selected+'\n'+inspect.getsource(reference)+'\nprint(json.dumps(reference(json.load(sys.stdin))))\n')
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()
    def row(self,**kw):
        keys='op actor x y z yaw speed turn_speed throttle turn step now half_length terrain_pitch grounded airborne due refresh exact baked intent cell recovery has_move mx my mz has_probe probe has_remaining remaining kind clear collision water slope slope_valid failed deferred receipt_kind'.split()
        values=['step',1,0.,0.,0.,0.,3.,0.,1.,0.,.05,0.,3.5,.1,1,0,1,1,1,1,1,20.,'drive',1,0.,0.,40.,0,0.,0,0.,0,1,0,0,0.,1,0,0,3]
        data=dict(zip(keys,values));data.update(kw);return [data[k] for k in keys]
    def compare(self,cases):
        lines=[]
        for row in cases:lines.append(' '.join(map(str,[row[0],len(row)-1]+row[1:] if row[0]=='begin' else row)))
        actual=subprocess.run([str(self.binary)],input='\n'.join(lines)+'\n',capture_output=True,text=True)
        self.assertEqual(actual.returncode,0,actual.stderr)
        expected=subprocess.run([self.py27,'-B',str(self.oracle),str(self.source/'src/res/scripts/client')],input=json.dumps(cases),capture_output=True,text=True)
        self.assertEqual(expected.returncode,0,expected.stderr)
        rows=[json.loads(line) for line in actual.stdout.splitlines()];wanted=json.loads(expected.stdout.splitlines()[-1]);self.assertEqual(len(rows),len(wanted))
        def equal(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):equal(x,y,path+'[%d]'%i)
            elif isinstance(b,(float,int,bool)):self.assertAlmostEqual(a,b,places=9,msg=path)
            else:self.assertEqual(a,b,path)
        for i,(a,b) in enumerate(zip(rows,wanted)):equal(a,b,str(i))
    def test_fair_queue_29_bots_and_deferred_recasts(self):
        cases=[]
        for frame in range(8):
            cases.append(['begin']+list(range(1,30)))
            for actor in range(1,30):cases.append(self.row(actor=actor,now=frame*.2,z=frame*.1,receipt_kind=2 if actor%7==0 and frame<3 else 3))
            cases.append(['end'])
        self.compare(cases)
    def test_cache_catchup_deferred_collision_and_recovery_horizons(self):
        cases=[];rng=random.Random(1513)
        for i in range(500):
            if i%10==0:cases.append(['begin']+list(range(1,11)))
            actor=1+i%10;cases.append(self.row(actor=actor,now=i*.013,z=(i//10)*.15,yaw=rng.choice([0.,0.,.02,.4]),
                speed=rng.choice([0.,0.,-3.,3.,12.]),throttle=rng.choice([-1.,0.,1.]),turn=rng.choice([0.,0.,.5]),
                turn_speed=rng.choice([0.,0.,.04]),refresh=int(i%3==0),due=int(i%5==0),baked=i%2,grounded=1,
                airborne=int(i%23==0),kind=-1 if i%19==0 else 0,clear=int(i%13!=0),collision=int(i%11==0),water=int(i%29==0),
                slope=rng.choice([0.,0.,.1,.7,99.]),slope_valid=int(i%17!=0),deferred=int(i%7==0),
                recovery=rng.choice(['drive','avoid','short_forward_escape','short_reverse_escape','contact_escape','forward_escape','wreck_push','reverse_turn','reverse_withdraw','friendly_yield']),
                has_probe=int(i%2==0),probe=rng.uniform(.1,8.),has_remaining=int(i%3==0),remaining=rng.uniform(-1.,5.),
                receipt_kind=rng.randrange(4)))
            if i%10==9:cases.append(['end'])
        self.compare(cases)
    def test_exact_receipt_expiry_containment_and_selected_probe(self):
        cases=[['begin',1],self.row(),self.row(now=.02,z=.1,refresh=0),self.row(now=.04,z=7.,refresh=0),['end'],
            ['begin',1],self.row(now=.3,z=8.),self.row(now=.31,z=8.1,refresh=0),['end'],
            ['begin',1],self.row(now=.5,z=8.2,deferred=1),self.row(now=.51,z=8.2,refresh=0),['end'],
            ['begin',1],self.row(now=.7,z=8.2,yaw=.5,deferred=1),self.row(now=.71,z=8.2,yaw=.5,refresh=0),['end']]
        self.compare(cases)

if __name__=='__main__':unittest.main()
