"""Parking geometry and ready-time relocation against pinned CPython 2.7 laws."""
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
    graph=dict(game_version=spg_positions.CATALOG['game_version'],width=21,height=21,cell_size=10.,origin=(-100.,-100.),bounds=(-100.,-100.,100.,100.),heights_mm=[0]*441,links=[255]*441,hazards=[0]*441,directions=((0,1),(1,1),(1,0),(1,-1),(0,-1),(-1,-1),(-1,0),(-1,1)))
    grid=spg_positions._Graph(graph,graph['bounds'])
    initial=dict(catalog='test',map='test',zone='a',cell='A1',source='fixture',radius=2.,clearance=6.,point=dict(x=0.,y=0.,z=0.))
    candidates=[]
    for i in range(4):
        p=dict(initial,point=dict(x=20.*i,y=0.,z=0.),zone='b' if i==3 else 'a',cell='A%d'%(i+1))
        candidates.append(p)
    state=dict(id=1,team=1,profile=dict(class_tag='SPG'),_spg_initial=initial)
    runtime=Runtime();runtime.states={1:state};runtime._decision_cache={};runtime._bot_tactics={};runtime.baked_graph=graph
    runtime._ordered_states=lambda:[state]
    runtime._physical_parking_occupancy=lambda state:runtime.occupied
    runtime._observer_target_key=lambda target:(target['kind'],target['network_id'])
    def displaced(state,plan):
        p=_position(state);index=grid.closest(p)
        return not state['airborne'] and not state['_overturned'] and all(-88.<=p[i]<=88. for i in (0,2)) and index is not None and grid.parking_clear(index,plan['clearance']) and abs(p[1]-grid.point(index)[1])<=1.5
    runtime._spg_displaced_hold=displaced
    cancelled=[0]
    def cancel(actor):cancelled[0]+=1
    runtime._cancel_artillery_intent=cancel
    def select(name,graph,states,**kw):
        reachable=grid._reference_distances(grid.closest(_position(state))) if grid.closest(_position(state)) is not None else {}
        choices=[]
        for i,p in enumerate(candidates):
            point=tuple(p['point'][axis] for axis in ('x','y','z'));index=220+2*i
            if index not in reachable or not spg_positions.parking_point_available(point,6.,1,kw['excluded'],kw['occupied']):continue
            score=(0 if kw.get('preferred_zone') is None or p['zone']==kw['preferred_zone'] else 1,1 if i==3 else 0,reachable[index],p['zone'],p['cell'],index)
            choices.append((score,p))
        return ({1:min(choices)[1]} if choices else {}),{}
    spg_positions.assign_initial_positions=select
    bot_tactics.map_settings=lambda *args:{}
    result=[]
    for row in cases:
        if row[0]=='grid':
            _,index,hazard,height,x,y,z,radius=row
            if index>=0:graph['hazards'][index]=hazard;graph['heights_mm'][index]=height*1000.
            grid._clearance={};closest=grid.closest((x,y,z));dist=grid._reference_distances(closest) if closest is not None else {}
            result.append([closest,closest is not None and grid.parking_clear(closest,radius),[dist.get(i) for i in range(441)]])
            continue
        (_,now,x,y,z,sequence,ready,allow,has_target,airborne,overturned,mode,planning_failed,planning_clear,reason,has_completed,completed,launch_clear,launch_failed,launch_reason,has_launch_completed,launch_completed,launch_target,local,occupied_x)=row
        state.update(x=x,y=y,z=z,yaw=0.,speed=0.,fire_seq=sequence,airborne=bool(airborne),_overturned=bool(overturned))
        target=dict(id=100,network_id=100,kind='bot',alive=True,health=100.,position=(0.,0.,180.)) if has_target else None
        command=dict(combat_mode=mode,fire_allowed=bool(allow));planning=dict(state='failed' if planning_failed else 'clear' if planning_clear else 'pending',reason=reason,local_blockage=bool(local))
        if has_completed:planning['completed']=completed
        exact=dict(launch_failed=bool(launch_failed),reason=launch_reason)
        if has_launch_completed:exact['completed']=launch_completed
        state['_spg_launch_failure']=exact;state['_spg_launch_failure_target']=('bot',launch_target) if launch_target else None
        gun=type('Gun',(),{'ready':lambda self,factor:bool(ready)})();ammo=type('Ammo',(),{'can_fire':lambda self:bool(ready)})()
        runtime._observe_spg_fire_position(state,command,target,planning,gun,ammo,1.,now,dict(state='clear') if launch_clear else {})
        stamp=(_position(state),0.,('bot',100) if target else None);blocked=state.get('_spg_obstruction')
        if blocked is not None and (blocked['stamp'][2]!=stamp[2] or _distance(blocked['stamp'][0],stamp[0])>.25):state.pop('_spg_obstruction',None);blocked=None
        if planning_failed and reason=='world_blocked' and local and target is not None:state['_spg_obstruction']=dict(stamp=blocked['stamp'] if blocked else stamp,since=blocked['since'] if blocked else now,last=now)
        elif target is None or planning_clear:state.pop('_spg_obstruction',None)
        runtime.occupied=[(None,(occupied_x,0.,0.),4.)] if occupied_x>-999. else []
        order=dict(combat_mode=mode,fire_allowed=bool(allow),move_position=(0.,0.,0.),aim_position=(0.,0.,180.),face_position=(0.,0.,180.))
        if target:order['target_id']=100
        out=runtime._artillery_position_order(state,order,{100:target} if target else {},now)
        episode=state.get('_spg_fire_position_failure')
        result.append([out['combat_mode'],out['move_position'],out['fire_allowed'],out.get('target_id'),[state['_spg_initial']['point'][a] for a in ('x','y','z')],state.get('_spg_parking_retries',0),len(state.get('_spg_failed_parking',())),len(state.get('_spg_fire_failed_parking',())),bool(state.get('_spg_fire_relocating')),state.get('_spg_position_event',''),cancelled[0],[episode[k] for k in ('elapsed','last','last_failure','counting')]+[len(episode['proofs'])] if episode else None,bool(state.get('_spg_obstruction'))])
    return result


class NativeParkingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:raise unittest.SkipTest('Fixed v0.10.1 source input is required')
        cls.source=Path(source);cls.tmp=tempfile.TemporaryDirectory(prefix='native-parking-');cls.binary=Path(cls.tmp.name)/'host'
        command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),'-I'+str(cls.source/'native')]+[str(ROOT/'bigworld'/('stock_native_bot_'+name+'.cpp')) for name in ('decision','parking','planner','strategy')]+[str(ROOT/'tests/stock_native_bot_parking_host.cpp'),'-o',str(cls.binary)]
        built=subprocess.run(command,capture_output=True,text=True)
        if built.returncode:raise RuntimeError(built.stderr)
        cls.py27=os.environ.get('WOT_PYTHON27') or subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        text=(cls.source/'src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py').read_text();tree=ast.parse(text)
        names=['_number','_point','_position','_distance','_angle_delta'];functions='\n\n'.join(ast.get_source_segment(text,n) for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names)
        names=['_observe_spg_fire_position','_retry_spg_fire_position','_retry_spg_deployment','_artillery_position_order']
        body=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='BotRuntime')
        methods='\n\n'.join(textwrap.indent(textwrap.dedent(ast.get_source_segment(text,n)),'    ') for n in body.body if isinstance(n,ast.FunctionDef) and n.name in names)
        cls.oracle=Path(cls.tmp.name)/'oracle.py';cls.oracle.write_text('''import sys,json,math,types,os
sys.path.insert(0,sys.argv[1])
for name in ('gui','gui.mods'):
    m=types.ModuleType(name);m.__path__=[os.path.join(sys.argv[1],*name.split('.'))];sys.modules[name]=m
from gui.mods.offline_lan_0922 import spg_positions,bot_tactics,bot_tactics_runtime
TACTICAL_REFRESH_SECONDS=1.
'''+functions+'\nclass Runtime(object):\n'+methods+'\n'+inspect.getsource(reference)+'\nprint(json.dumps(reference(json.load(sys.stdin))))\n')
    @classmethod
    def tearDownClass(cls):
        if hasattr(cls,'tmp'):cls.tmp.cleanup()
    def row(self,**kw):
        keys='op now x y z sequence ready allow target airborne overturned mode failed clear reason has_completed completed launch_clear launch_failed launch_reason has_launch_completed launch_completed launch_target local occupied'.split()
        values=['step',0.,0.,0.,0.,0,1,1,1,0,0,'artillery_hold',1,0,'world_blocked',1,0.,0,0,'world_blocked',0,0.,100,0,-1000.]
        data=dict(zip(keys,values));data.update(kw);return [data[k] for k in keys]
    def compare(self,cases):
        run=subprocess.run([str(self.binary)],input='\n'.join(' '.join(map(str,row)) for row in cases)+'\n',capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stderr)
        expected=subprocess.run([self.py27,'-B',str(self.oracle),str(self.source/'src/res/scripts/client')],input=json.dumps(cases),capture_output=True,text=True);self.assertEqual(expected.returncode,0,expected.stderr)
        rows=[json.loads(l) for l in run.stdout.splitlines()];wanted=json.loads(expected.stdout.splitlines()[-1]);self.assertEqual(len(rows),len(wanted))
        def equal(a,b,path):
            if isinstance(b,list):
                self.assertEqual(len(a),len(b),path)
                for i,(x,y) in enumerate(zip(a,b)):equal(x,y,path+'[%d]'%i)
            elif isinstance(b,(float,int,bool)):self.assertAlmostEqual(a,b,places=8,msg=path)
            else:self.assertEqual(a,b,path)
        for i,(a,b) in enumerate(zip(rows,wanted)):equal(a,b,str(i))
    def test_parking_grid_dry_bounds_slope_and_python27_ties(self):
        rng=random.Random(1513);cases=[]
        for i in range(80):
            cases.append(['grid',rng.randrange(441),rng.choice([0,0,1,2,4,8,16]),rng.choice([0.,0.,.1,4.,12.]),rng.choice([0.,5.,-5.,100.,-100.,rng.uniform(-110.,110.)]),0.,rng.choice([0.,5.,-5.,rng.uniform(-110.,110.)]),rng.choice([0.,2.,6.,12.])])
        self.compare(cases)
    def test_duplicate_receipts_ready_clock_and_fire_retry(self):
        cases=[self.row(now=i,completed=0. if i<12 else 12.,ready=int(i not in (4,5,6)),failed=int(i%3!=0),reason='world_blocked') for i in range(45)]
        cases += [self.row(now=46.,x=20.,failed=0),self.row(now=47.,x=20.,sequence=1),self.row(now=49.,x=20.,launch_clear=1)]
        self.compare(cases)
    def test_exact_launch_identity_age_priority_and_clear_planning(self):
        cases=[self.row(now=0.,failed=0,launch_failed=1,has_launch_completed=1,launch_completed=0.,launch_target=99),self.row(now=1.,failed=0,launch_failed=1,has_launch_completed=1,launch_completed=0.),self.row(now=2.,failed=1,completed=2.,launch_failed=1,has_launch_completed=1,launch_completed=1.),self.row(now=3.,failed=0,clear=1,reason='none',local=0),self.row(now=4.,failed=0,launch_failed=1,has_launch_completed=1,launch_completed=1.),self.row(now=5.,failed=1,reason='no_candidate',completed=5.),self.row(now=6.,launch_clear=1)]
        self.compare(cases)
    def test_deployment_stall_limit_occupancy_and_displaced_arrival(self):
        self.compare([self.row(now=t,x=-20.,mode='artillery_deploy',failed=0) for t in (0.,19.9,20.,20.1,40.,40.1,60.1,60.2,80.2,100.2)])
        self.compare([self.row(now=0.,x=-20.,occupied=0.,failed=0),self.row(now=1.,x=20.,occupied=0.,failed=0),self.row(now=2.,x=26.,occupied=20.,failed=0),self.row(now=3.,x=26.,airborne=1,failed=0),self.row(now=4.,x=26.,failed=0)])

if __name__=='__main__':unittest.main()
