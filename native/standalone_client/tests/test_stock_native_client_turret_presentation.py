"""Exact accepted turret-buffer and frozen arc laws on CPython 2.7."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''
import ast,copy,imp,json,math,os,random,sys,types
import turret_presentation_test as native
base=os.path.join(sys.argv[1],'src/res/scripts/client/gui/mods/offline_lan_0922')
shot=imp.load_source('turret_shot_geometry',os.path.join(base,'shot_geometry.py'))
class Box(object):
 def __init__(self,**kw):self.__dict__.update(kw)
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 m=types.ModuleType(name);m.__path__=[];sys.modules[name]=m
sync=types.ModuleType('gui.mods.offline_lan_0922.snapshot_sync');sync.MIN_TIMED_DELAY_US=60000.;sync.INITIAL_TIMED_DELAY_US=90000.;sync.TIMED_DELAY_DECAY_RATIO=.005;sys.modules[sync.__name__]=sync
pkg=sys.modules['gui.mods.offline_lan_0922'];pkg.snapshot_sync=sync
rowenv=dict(math=math,copy=copy,shot_geometry=shot,turret_detachment=Box(FLIGHT_STEP_SECONDS=.04),EPSILON=1.e-6,ZERO=(0.,0.,0.))
tree=ast.parse(open(os.path.join(base,'rigid_turret.py')).read())
names=('add','sub','scale','dot','cross','length','matrix','mul','rotate','angles','render_pose','interpolate_rotation')
exec compile(ast.Module([n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names or isinstance(n,ast.ClassDef) and n.name=='PresentationBuffer']),'source-turret-buffer','exec') in rowenv
rigid=types.ModuleType('gui.mods.offline_lan_0922.rigid_turret');rigid.__dict__.update(rowenv);sys.modules[rigid.__name__]=rigid;pkg.rigid_turret=rigid
flightenv=dict(math=math,GRAVITY=9.81,_HALF_TURN=math.pi)
tree=ast.parse(open(os.path.join(base,'turret_detachment.py')).read());names=('_finite','_vector3','flight_position','_snap_half_turn','rest_attitude','pose_at')
exec compile(ast.Module([n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names]),'source-turret-arc','exec') in flightenv
rng=random.Random(1513);steps=0

def row(seq,stamp,sleep=False,body=True):
 frame=dict(position=[rng.uniform(-100.,100.) for _ in range(3)],attitude=[rng.uniform(-3.,3.) for _ in range(3)],centre=[rng.uniform(-2.,2.) for _ in range(3)],velocity=[rng.uniform(-12.,12.) for _ in range(3)],angular_velocity=[rng.uniform(-4.,4.) for _ in range(3)],grounded=seq%3==0,sleeping=sleep,impact_serial=seq,impact=None,acks=[])
 flight=dict(origin=[0.,3.,0.],velocity=[1.,4.,2.],rest=[1.,0.,2.],duration=1.,energy=0.,landed=False,contact=None,impact_velocity=[0.,0.,0.],segments=[dict(origin=[0.,3.,0.],velocity=[1.,4.,2.],start=0.,duration=1.)])
 if body:flight['body']=frame
 return dict(actor_kind='player',actor_id=4,created_time_ms=stamp,motion_seq=seq,motion_time_ms=stamp,support_key=None,attitude=[.2,.3,.4],spin=[1.,2.,3.],flight=flight)
def equal(a,b):
 if isinstance(b,(list,tuple)):
  assert len(a)==len(b),(a,b)
  for x,y in zip(a,b):equal(x,y)
 elif isinstance(b,float):assert abs(a-b)<=3.e-11*max(1.,abs(a),abs(b)),(a,b)
 else:assert a==b,(a,b)
for run in range(12):
 a=rowenv['PresentationBuffer']();b=native.create();now=1.;stamp=0
 for seq in range(1,161):
  now+=rng.choice((0.,.01,.04,.12,.3));stamp+=rng.choice((0,40,80,200));r=row(seq,stamp,sleep=seq%23==0)
  age=rng.choice((0.,.01,.1,.25));a.push(r,now,age);assert native.push(b,r,now,age)
  for offset in (0.,.004,.02,.1,.9):
   elapsed=now+offset;position,attitude,event=a.pose(elapsed);v=native.pose(b,elapsed)
   equal(v['position'],position);equal(v['attitude'],attitude);equal(v['cursor'],a.cursor);equal(v['delay'],a.delay);assert v['impact_serial']==event['impact_serial'];assert v['sleeping']==event['sleeping'];assert v['samples']==len(a.samples);steps+=1
 # A stale source is ignored independently of a later receipt timestamp.
 old=copy.deepcopy(r);old['motion_time_ms']=0;a.push(old,now+2.,.4);assert not native.push(b,old,now+2.,.4)
# Frozen visual arcs, explicit resting angles, half-turn snaps, and body fallback.
for i in range(320):
 r=row(i+1,i*40,body=i%2==0)
 if i%4==1:r['flight']['rest_attitude']=[.2,math.pi,0.]
 r['attitude']=[rng.uniform(-5.,5.) for _ in range(3)];r['spin']=[rng.uniform(-8.,8.) for _ in range(3)]
 for elapsed in (-1.,0.,.001,.04,.4,.999,1.,2.,10.):
  p,a=flightenv['pose_at'](r['flight'],r['attitude'],r['spin'],elapsed);v=native.arc(r,elapsed);equal(v['position'],p);equal(v['attitude'],a);steps+=1
# Gimbal singularities and half-turn shortest arcs retain the exact source
# branch at both signs; compare full COM rendering and interpolation.
for pitch in (-math.pi/2.,math.pi/2.,-math.pi/2.+1.e-9,math.pi/2.-1.e-9):
 a=rowenv['PresentationBuffer']();b=native.create()
 for seq,yaw in enumerate((0.,math.pi,-math.pi,math.pi*.999999),1):
  r=row(seq,seq*40);r['flight']['body']['attitude']=[yaw,pitch,math.pi]
  a.push(r,1.+seq*.04,.01);native.push(b,r,1.+seq*.04,.01)
  for offset in (0.,.01,.03,.08):
   p,angles,event=a.pose(1.+seq*.04+offset);v=native.pose(b,1.+seq*.04+offset);equal(v['position'],p);equal(v['attitude'],angles);steps+=1
print(json.dumps(dict(turret_presentation_steps=steps)))
'''
@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'),'Pinned gameplay source is required')
class NativeTurretPresentation(unittest.TestCase):
    def test_buffer_and_arc(self):
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        names=['stock_native_client_turret_presentation','stock_native_detached_turret_snapshot','stock_native_detached_turret_runtime','stock_native_bot_turret_obstacles']
        with tempfile.TemporaryDirectory(prefix='native-turret-presentation-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(SCRIPT)
            command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wno-register','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native'),str(ROOT/'tests/stock_native_client_turret_presentation_host.cpp')]+[str(ROOT/'bigworld'/(name+'.cpp')) for name in names]+[str(fork/'native/offline_destructible_geometry.cpp'),'-o',str(folder/'turret_presentation_test.so')]
            subprocess.run(command,check=True);subprocess.run([py27,'-B',str(script),str(fork)],check=True)
if __name__=='__main__':unittest.main()
