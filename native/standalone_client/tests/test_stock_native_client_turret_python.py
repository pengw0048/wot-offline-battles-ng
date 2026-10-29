"""Native presenter runs the actual staged manager with one Matrix writer."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''
import ast,copy,imp,math,os,sys,types
import turret_python_test as native
base=os.path.join(sys.argv[1],'src/res/scripts/client')
sys.path.insert(0,base)
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[base+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import rigid_turret,turret_detachment
from gui.mods.offline_lan_0922.entities import detached_turret as original
sys.modules['offline_client_turret_presentation_native']=native
facade=imp.load_source('gui.mods.offline_lan_0922.native_client_turret_presentation',sys.argv[2]+'/stock_native_client_turret_presentation.py')
sys.modules['gui.mods.offline_lan_0922'].native_client_turret_presentation=facade
module=types.ModuleType('native_presenter');exec compile(open(sys.argv[3]).read(),'native-presenter','exec') in module.__dict__
class Box(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class BigWorld(object):
 def __init__(self):self.rows={};self.next=100
 def createEntity(self,*args):self.next+=1;return self.next
 def entity(self,id):return self.rows.get(id)
 destroyEntity=staticmethod(native.destroyEntity)
def manager(native_owner):
 bw=BigWorld();native.entities(bw.rows)
 cls=module.DetachedTurretPresentation if native_owner else original.DetachedTurretPresentation
 return cls(bw,native,Box(spaceID=1),lambda a,b:None),bw

def row(seq=1,stamp=0,body=False,landed=False):
 f=dict(origin=[0.,3.,0.],velocity=[1.,4.,2.],rest=[1.,0.,2.],duration=1.,energy=42.,landed=landed,contact=[1.,0.,2.] if landed else None,impact_velocity=[1.,-3.,2.],segments=[dict(origin=[0.,3.,0.],velocity=[1.,4.,2.],start=0.,duration=1.)])
 if body:f['body']=dict(position=[seq*.3,1.,0.],attitude=[seq*.2,.4,-.2],centre=[.4,.2,-.3],velocity=[1.,0.,2.],angular_velocity=[1.,2.,-.5],grounded=landed,sleeping=False,impact_serial=seq,impact=dict(point=[1.,0.,2.],normal=[0.,1.,0.],velocity=[1.,-3.,2.],energy=42.) if landed else None,acks=[])
 return dict(actor_kind='player',actor_id=4,created_time_ms=stamp,motion_seq=seq,motion_time_ms=stamp,support_key=None,attitude=[.2,.3,.4],spin=[1.,2.,3.],flight=f)
plan=dict(entity_id=4,compact_descr='exact',space_id=1,attitude=(.2,.3,.4),launch=(0.,3.,0.))
def launch(m,bw,r,now,age=0.):native.entities(bw.rows);return m.launch_canonical(plan,r,now,age)
def entity(id=4,model=True):
 e=Box(vehicleID=id,model=Box() if model else None,impacts=[])
 def impact(energy,point,normal):e.impacts.append((energy,tuple(point),tuple(normal)))
 e.onStaticCollision=impact;return e

def close(a,b):
 if isinstance(b,(tuple,list)):
  assert len(a)==len(b)
  for x,y in zip(a,b):close(x,y)
 elif isinstance(b,float):assert abs(a-b)<=5.e-11*max(1.,abs(a),abs(b)),(a,b)
 else:assert a==b,(a,b)
def compare(a,b):
 assert len(a._turrets)==len(b._turrets)
 for x,y in zip(a._turrets,b._turrets):
  for key in ('id','vehicle_id','settled','impacted'):assert x[key]==y[key],(key,x,y)
  if x['matrix'] is not None:
   close(x['matrix'].angles,y['matrix'].angles);close(x['matrix'].translation,y['matrix'].translation)
  if x['entity'] is not None:close(x['entity'].impacts,y['entity'].impacts)

a,ab=manager(False);b,bb=manager(True)
# Accepted id is pending until native prerequisites publish it. No pose callback
# from the legacy Python manager may run in a native frame.
def forbidden(*args,**kw):raise AssertionError('Python turret hot callback')
b._entity=b._entity_matches=forbidden
assert launch(a,ab,row(landed=True),1.)==launch(b,bb,row(landed=True),1.)==True
for now in (1.,1.1,1.4):
 native.entities(ab.rows);x=a.advance(now);native.entities(bb.rows);y=b.advance(now);assert x==y==0;compare(a,b)
ab.rows[101]=entity(model=False);bb.rows[101]=entity(model=False)
for now in (1.5,1.7):
 native.entities(ab.rows);x=a.advance(now);native.entities(bb.rows);y=b.advance(now);assert x==y==0
ab.rows[101].model=Box();bb.rows[101].model=Box()
for now in (1.71,1.8,2.,2.1):
 native.entities(ab.rows);x=a.advance(now);native.entities(bb.rows);y=b.advance(now);assert x==y;compare(a,b)
assert len(bb.rows[101].impacts)==1
# A new accepted body sample wakes the same Matrix and keeps one producer.
first_matrix=b._turrets[0]['matrix']
for seq in range(2,18):
 now=2.2+seq*.08;r=row(seq,seq*80,body=True,landed=seq%3==0)
 assert launch(a,ab,r,now,.04)==launch(b,bb,r,now,.04)==True
 assert 'pose_buffer' not in b._turrets[0]
 for delta in (0.,.01,.04,.12):
  native.entities(ab.rows);x=a.advance(now+delta);native.entities(bb.rows);y=b.advance(now+delta);assert x==y;compare(a,b)
assert b._turrets[0]['matrix'] is first_matrix
# One Matrix failure affects only that write and does not recreate the owner.
a._turrets[0]['matrix'].fail=b._turrets[0]['matrix'].fail=True
native.entities(ab.rows);x=a.advance(4.);native.entities(bb.rows);y=b.advance(4.);assert x==y==0
native.entities(ab.rows);x=a.advance(4.1);native.entities(bb.rows);y=b.advance(4.1);assert x==y;compare(a,b)
# An id reused by another source is retired from our table without destruction.
ab.rows[101]=entity(99);bb.rows[101]=entity(99)
native.entities(ab.rows);a.advance(4.2);native.entities(bb.rows);b.advance(4.2)
assert not a._turrets and not b._turrets and 101 in bb.rows
# A timed-out canonical load retains its id for safe eventual retirement.
a,ab=manager(False);b,bb=manager(True);launch(a,ab,row(),10.);launch(b,bb,row(),10.)
native.entities(ab.rows);assert a.advance(40.)==0;native.entities(bb.rows);assert b.advance(40.)==0
assert len(a._retiring)==len(b._retiring)==1 and not b._turrets
ab.rows[101]=entity();bb.rows[101]=entity();native.entities(ab.rows);a.advance(40.1);native.entities(bb.rows);b.advance(40.1)
assert not a._retiring and not b._retiring and 101 not in bb.rows
# Closed managers can poll partial startup twice, without a second destroy.
a,ab=manager(False);b,bb=manager(True);launch(a,ab,row(),50.);launch(b,bb,row(),50.)
native.entities(ab.rows);assert a.destroy_all()==0;native.entities(bb.rows);assert b.destroy_all()==0
ab.rows[101]=entity();bb.rows[101]=entity();native.entities(ab.rows);assert a.destroy_all()==1;native.entities(bb.rows);assert b.destroy_all()==1
native.entities(bb.rows);assert b.destroy_all()==0 and b.advance(51.)==0

# Compare the original complete visible scheduler. Hot legacy producers are
# traps on the native battle; accepted rows enter only at ingress.
source=open(os.path.join(base,'gui/mods/offline_lan_0922/battle_runtime.py')).read()
tree=ast.parse(source);runtime=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='BattleRuntime')
names=('_advance_detached_turrets','_turret_server_time_ms')
env={};exec compile(ast.Module([n for n in runtime.body if isinstance(n,ast.FunctionDef) and n.name in names]),'source-turret-scheduler','exec') in env
from gui.mods.offline_lan_0922 import spotting
class Modes:
 STRATEGIC='strategic';ARTY='arty'
class Obstacles(object):
 def __init__(self):self.rows=[]
 def add(self,*args):self.rows.append(args);return True
def battle(m,bw,is_native,spg=False):
 descriptor=Box(type=Box(tags=set(['SPG']) if spg else set()),turret=Box(models=Box(exploded='turret')),gun=Box(models=Box(exploded='gun')),makeCompactDescr=lambda:'exact')
 bw.rows[4]=Box(id=4,isStarted=True,isTurretDetached=True,typeDescriptor=descriptor)
 x=Box(_worker_mode=False,_battle_result=None,_detached_turrets=m,_detached_turret_rows={},_detached_turret_geometry=set(),_detached_turret_obstacles=Obstacles(),_detached_turret_retry={},_turret_visual_revisions={},_projectile_server_time_ms=0,_projectile_server_local_time=1.,_spectated_engine_id=None,_native_client_state=object(),_actors=[dict(key='player:4',id=4,local=True,position=[0.,0.,0.])],_local_descriptor=descriptor,_avatar=Box(inputHandler=Box(_AvatarInputHandler__ctrlModeName='arcade')),_runtime=Box(avatar_input_handler=Box(_CTRL_MODE=Modes)),_records={'player:4':dict(engine_id=4,ready=True)},_origin=(0.,0.,0.),_spg=False)
 x._server_entity=bw.entity;x._turret_server_time_ms=types.MethodType(env['_turret_server_time_ms'],x)
 x._turret_obstacle_in_view=lambda r:math.hypot(x._origin[0]-r['flight']['rest'][0],x._origin[2]-r['flight']['rest'][2])<=spotting.VEHICLE_AOI_RADIUS or x._spg
 x._run_optional_feature=lambda name,func,args,**kw:func(*args)
 if is_native:
  native.setup_battle(x)
  x._server_entity=x._turret_server_time_ms=x._turret_obstacle_in_view=x._run_optional_feature=forbidden
 return x
def tick(x,bw,is_native,now):
 native.entities(bw.rows)
 return facade.frame(x,now) if is_native else env['_advance_detached_turrets'](x,now)
def accepted(x,r,is_native):
 x._detached_turret_rows={'player:4':copy.deepcopy(r)};x._detached_turret_geometry.clear()
 if is_native:facade.ingress(x)
def same_schedule(x,y):
 compare(x._detached_turrets,y._detached_turrets)
 assert x._detached_turret_geometry==y._detached_turret_geometry
 assert x._detached_turret_retry==y._detached_turret_retry
 assert x._turret_visual_revisions==y._turret_visual_revisions
 assert len(x._detached_turret_obstacles.rows)==len(y._detached_turret_obstacles.rows)
a,ab=manager(False);b,bb=manager(True);x=battle(a,ab,False);y=battle(b,bb,True)
accepted(x,row(body=True),False);accepted(y,row(body=True),True)
for now in (1.,1.1,1.4):
 assert tick(x,ab,False,now)==tick(y,bb,True,now);same_schedule(x,y)
ab.rows[101]=entity();bb.rows[101]=entity()
x._projectile_server_time_ms=y._projectile_server_time_ms=None
assert tick(x,ab,False,1.45)==tick(y,bb,True,1.45)==0;same_schedule(x,y)
assert b._turrets[0]['matrix'] is None
x._projectile_server_time_ms=y._projectile_server_time_ms=0
for now in (1.5,1.6,1.8):
 assert tick(x,ab,False,now)==tick(y,bb,True,now);same_schedule(x,y)
for seq in (2,3,4):
 r=row(seq,seq*100,body=True,landed=seq==4);accepted(x,r,False);accepted(y,r,True)
 for now in (2.+seq*.1,2.05+seq*.1):
  assert tick(x,ab,False,now)==tick(y,bb,True,now);same_schedule(x,y)
assert bb.next==101 and len(y._detached_turret_obstacles.rows)==4
# AOI postpones creation without consuming an attempt. Moving back creates
# once; a timed-out pending id must retire before it can be attempted again.
a,ab=manager(False);b,bb=manager(True);x=battle(a,ab,False);y=battle(b,bb,True)
x._origin=(2000.,0.,0.);native.move(2000.,0.,0.)
accepted(x,row(),False);accepted(y,row(),True)
assert tick(x,ab,False,1.)==tick(y,bb,True,1.)==0;same_schedule(x,y);assert bb.next==100
x._origin=(0.,0.,0.);native.move(0.,0.,0.)
for now in (1.1,31.1,31.2):
 assert tick(x,ab,False,now)==tick(y,bb,True,now);same_schedule(x,y)
assert len(b._retiring)==1 and bb.next==101
ab.rows[101]=entity();bb.rows[101]=entity()
for now in (31.3,32.3,32.4):
 assert tick(x,ab,False,now)==tick(y,bb,True,now);same_schedule(x,y)
assert bb.next==102
# A new battle epoch retires the typed owner: the existing provider is not
# written and no creation occurs after State replacement/cleanup.
ab.rows[102]=entity();bb.rows[102]=entity();native.retire_state()
assert tick(y,bb,True,32.5)==0 and b._turrets[0]['matrix'] is None
# Exact SPG aiming modes override AOI; arcade restores the normal gate.
a,ab=manager(False);b,bb=manager(True);x=battle(a,ab,False,True);y=battle(b,bb,True,True)
x._origin=(2000.,0.,0.);native.move(2000.,0.,0.)
accepted(x,row(),False);accepted(y,row(),True)
assert tick(x,ab,False,1.)==tick(y,bb,True,1.)==0;same_schedule(x,y)
x._spg=True;y._avatar.inputHandler._AvatarInputHandler__ctrlModeName='arty'
assert tick(x,ab,False,1.1)==tick(y,bb,True,1.1)==0;same_schedule(x,y);assert bb.next==101
print('Native turret manager and accepted scheduler/AOI/retry/geometry lifecycle passed')

'''
@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'),'Pinned gameplay source is required')
class NativeTurretPython(unittest.TestCase):
 def test_production_manager(self):
  import importlib.util
  fork=Path(os.environ['WOT_FORK_0101_SOURCE']);directory=ROOT/'bigworld'
  spec=importlib.util.spec_from_file_location('turret_bindings',directory/'stock_native_client_turret_presentation_bindings.py');bindings=importlib.util.module_from_spec(spec);spec.loader.exec_module(bindings)
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  names=['stock_native_client_turret_presentation','stock_native_client_turret_presentation_python','stock_native_detached_turret_snapshot','stock_native_detached_turret_runtime','stock_native_bot_turret_obstacles']
  with tempfile.TemporaryDirectory(prefix='native-turret-manager-') as folder:
   folder=Path(folder);(folder/'fixture.py').write_text(SCRIPT);original=fork/'src/res/scripts/client/gui/mods/offline_lan_0922/entities/detached_turret.py';(folder/'presenter.py').write_text(bindings.presenter(original.read_text()))
   battle_source=(fork/'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read_text();(folder/'battle.py').write_text(bindings.battle(battle_source))
   subprocess.run([py27,'-B','-c','import sys; [compile(open(p, "rb").read(), p, "exec") for p in sys.argv[1:]]',str(folder/'battle.py'),str(folder/'presenter.py'),str(directory/'stock_native_client_turret_presentation.py')],check=True)
   command=['c++','-std=c++17','-O1','-ffp-contract=off','-Wno-register','-Wall','-Wextra','-Werror','-DWOT_CLIENT_TURRET_PRESENTATION_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(directory),'-I'+str(fork/'native'),str(ROOT/'tests/stock_native_client_turret_python_host.cpp')]+[str(directory/(name+'.cpp')) for name in names]+[str(fork/'native/offline_destructible_geometry.cpp'),'-o',str(folder/'turret_python_test.so')]
   subprocess.run(command,check=True);subprocess.run([py27,'-B',str(folder/'fixture.py'),str(fork),str(directory),str(folder/'presenter.py')],check=True)
if __name__=='__main__':unittest.main()
