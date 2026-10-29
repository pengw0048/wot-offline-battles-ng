"""Exact Python 2.7 outer decision-policy transitions and native cadence."""
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
    runtime=Runtime();runtime.states={};runtime._friendly_repositions={};runtime._decision_cache={};runtime.adapter=type('Adapter',(),{})();runtime.adapter.director=type('Director',(),{'map_data':{}})()
    cancelled=[0]
    def cancel(actor):cancelled[0]+=1
    runtime._cancel_artillery_intent=cancel
    for actor in range(1,4):runtime.states[actor]=dict(id=actor,team=1,profile=dict(class_tag='SPG' if actor==2 else 'mediumTank'),route=dict(id='r',waypoints=[(0.,0.),(0.,20.),(0.,60.),(0.,200.)]),half_length=3.5)
    result=[]
    for row in cases:
        if row[0]=='mark':
            (_,actor,now,x,y,z,yaw,sequence,shell,fire_range,kind,blocker,team,bx,by,bz,has_radius,radius,has_target)=row
            state=runtime.states[actor];state.update(x=x,y=y,z=z,team=1,half_width=1.7,half_length=3.5,collision_shape=(1.7,3.5,0.,2.),shell_index=shell)
            command=dict(fire_range=fire_range)
            if has_target:command['target_id']=100
            verdict=dict(blocker_kind=kind,blocker_id=blocker,blocker_team=team,blocker_position=(bx,by,bz))
            if has_radius:verdict['blocker_radius']=radius
            accepted=runtime._mark_friendly_reposition(state,command,dict(kind='bot'),dict(fire_seq=sequence,shot_yaw=yaw),verdict,now)
            marker=runtime._friendly_repositions.get(actor)
            result.append([accepted,[marker[k] for k in ('destination','target_id','shell_index','fire_range','deadline')] if marker else None])
            continue
        if row[0]=='rear':
            _,actor,now=row;runtime.states[actor]['_rear_aim_episode']=dict(target_id=100,fire_seq=0,identity=('bot',100),start=now)
            runtime.states[actor]['_rear_motion_lease']=dict(origin=(0.,0.,0.),start=now);continue
        if row[0]=='obstruction':
            _,actor,since,last=row;runtime.states[actor]['_spg_obstruction']=dict(stamp=((0.,0.,0.),0.,('bot',100)),since=since,last=last);continue
        (_,actor,now,x,z,yaw,speed,reload,sequence,mode,parking,alive,tx,tz,mx,mz,fire,refused,clear,refresh,fixed,tier,server,token)=row
        state=runtime.states[actor];state.update(x=x,y=0.,z=z,yaw=yaw,speed=speed,reload_time=reload,fire_seq=sequence)
        state['_gun_angle_refusal']=('bot',100) if refused else None
        target=dict(id=100,network_id=100,kind='bot',team=2,position=(tx,0.,tz),alive=bool(alive),visible=True,health=1000.)
        order=dict(target_id=100,aim_position=target['position'],face_position=target['position'],move_position=(mx,0.,mz),combat_mode=mode,parking_phase='' if parking=='none' else parking,fire_allowed=bool(fire),fire_range=450.,route_id='r',route_index=2)
        out=_overlay_live_target_pose(order,target,(x,0.,z));out=runtime._rear_laying_order(state,out,{100:target},now);out=runtime._artillery_position_order(state,out,{100:target},now);out=runtime._gun_angle_order(state,out,{100:target},now,lambda *args:bool(clear))
        result.append([out['combat_mode'],out.get('move_position'),out.get('aim_position'),out.get('face_position'),out.get('fire_allowed',False),out.get('throttle_override'),out.get('target_id'),bool(state.get('_rear_aim_episode')),bool(state.get('_rear_motion_blocked')),bool(state.get('_gun_angle_adjustment')),len(state.get('_gun_angle_rejected',{})),bool(state.get('_spg_position')),cancelled[0]])
    return result

class NativeDecisionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-decision-');cls.binary=Path(cls.tmp.name)/'host'
        files=['decision','parking','planner','strategy']
        command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),'-I'+str(cls.source/'native')]+[str(ROOT/'bigworld'/('stock_native_bot_'+name+'.cpp')) for name in files]+[str(ROOT/'tests/stock_native_bot_decision_host.cpp'),'-o',str(cls.binary)]
        built=subprocess.run(command,capture_output=True,text=True)
        if built.returncode:raise RuntimeError(built.stderr)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        text=(cls.source/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py').read_text();tree=ast.parse(text)
        names=['_number','_point','_position','_distance','_angle_delta','_overlay_live_target_pose']
        functions='\n\n'.join(ast.get_source_segment(text,n) for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names)
        names=['_mark_friendly_reposition','_rear_laying_order','_artillery_position_order','_gun_angle_rejected','_gun_angle_order','_gun_angle_route_order','_artillery_target_identity','_route_waypoint_xz']
        body=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='BotRuntime');methods=[]
        for node in body.body:
            if isinstance(node,ast.FunctionDef) and node.name in names:
                prefix='    @staticmethod\n' if any(isinstance(d,ast.Name) and d.id=='staticmethod' for d in node.decorator_list) else ''
                methods.append(prefix+textwrap.indent(textwrap.dedent(ast.get_source_segment(text,node)),'    '))
        cls.oracle=Path(cls.tmp.name)/'oracle.py';cls.oracle.write_text('''import sys,json,math,types,os
sys.path.insert(0,sys.argv[1])
for name in ('gui','gui.mods'):
    m=types.ModuleType(name);m.__path__=[os.path.join(sys.argv[1],*name.split('.'))];sys.modules[name]=m
from gui.mods.offline_lan_0922.ai import driver as ai_driver
from gui.mods.offline_lan_0922 import tank_collision
FRIENDLY_REPOSITION_SECONDS=4.
'''+functions+'\nclass Runtime(object):\n'+'\n\n'.join(methods)+'\n'+inspect.getsource(reference)+'\nprint(json.dumps(reference(json.load(sys.stdin))))\n')
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()
    def row(self,**kw):
        keys='op actor now x z yaw speed reload sequence mode parking alive tx tz mx mz fire refused clear refresh fixed tier server token'.split()
        vals=['policy',1,0.,0.,0.,0.,0.,3.,0,'engage','none',1,0.,180.,0.,0.,1,0,1,1,1,0,1,1]
        value=dict(zip(keys,vals));value.update(kw);return [value[k] for k in keys]
    def run_host(self,cases):
        run=subprocess.run([str(self.binary)],input='\n'.join(' '.join(map(str,row)) for row in cases)+'\n',capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr);return [json.loads(l) for l in run.stdout.splitlines()]
    def compare(self,cases):
        rows=self.run_host(cases);expected=subprocess.run([self.py27,'-B',str(self.oracle),str(self.source/'src/res/scripts/client')],input=json.dumps(cases),capture_output=True,text=True);self.assertEqual(expected.returncode,0,expected.stderr)
        wanted=json.loads(expected.stdout.splitlines()[-1]);self.assertEqual(len(rows),len(wanted))
        def equal(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):equal(x,y,path+'[%d]'%i)
            elif isinstance(b,(float,int,bool)):self.assertAlmostEqual(a,b,places=8,msg=path)
            else:self.assertEqual(a,b,path)
        for i,(a,b) in enumerate(zip(rows,wanted)):equal(a,b,str(i))
    def test_gun_angle_bounded_recovery_and_shared_candidate_tick(self):
        cases=[]
        for now in [0.,1.,2.,2.,3.,5.9,6.,7.,13.9,14.,15.,16.]:
            for actor in [1,3]:cases.append(self.row(actor=actor,now=now,refused=1,clear=int(actor==1)))
        cases += [self.row(now=17.,parking='waiting',refused=1),self.row(now=18.,mode='route',mx=60.,mz=60.,refused=1)]
        self.compare(cases)
    def test_rear_episode_firing_expiry_movement_and_target_loss(self):
        self.compare([['rear',1,0.],self.row(now=1.),self.row(now=7.9),self.row(now=8.),self.row(now=9.),self.row(now=16.),['rear',1,20.],self.row(now=21.,sequence=1),['rear',1,30.],self.row(now=31.,alive=0)])
    def test_spg_local_obstruction_rear_route_relocation(self):
        self.compare([['obstruction',2,0.,3.],self.row(actor=2,now=3.,mode='artillery_hold'),self.row(actor=2,now=4.,mode='artillery_hold',z=17.),self.row(actor=2,now=5.,mode='route',mz=100.)])
    def test_friendly_blocker_reposition_frozen_destination_and_validation(self):
        cases=[]
        for i in range(90):
            cases.append(['mark',1,0.1*i,2.*i,1.,-3.*i,.07*i,i,i%3,450. if i%2 else -1.,['bot','player','world'][i%3],1 if i%7==0 else 23,2 if i%11==0 else 1,20.,0.,30.,i%2,[-1.,0.,4.][i%3],int(i%13!=0)])
        self.compare(cases)
    def test_decision_deadline_cache_order_change_and_catchup_hold(self):
        cases=[self.row(op='frame',now=0.,mode='route',mz=100.),self.row(op='frame',now=.01,mode='route',mz=100.,refresh=0),self.row(op='frame',now=.05,mode='route',mz=100.),self.row(op='frame',now=.06,mode='route',mz=100.,token=2,refresh=0),self.row(op='frame',now=.2,mode='route',mz=100.,token=2),self.row(op='frame',now=.21,server=0,refresh=0),self.row(op='frame',now=.3,server=0)]
        rows=self.run_host(cases)
        self.assertEqual([r[:3] for r in rows],[[1,0,0],[0,1,0],[0,1,0],[0,0,1],[1,0,0],[0,0,1],[1,0,0]])
        self.assertAlmostEqual(rows[0][-1],.15+((17+33)%29)/29.*.15)
        self.assertEqual(rows[3][3:9],[0,0,'physical_hold','physical_hold',0,0])
        self.assertAlmostEqual(rows[4][-1],.35)

if __name__=='__main__':unittest.main()
