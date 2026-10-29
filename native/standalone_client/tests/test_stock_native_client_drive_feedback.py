"""Copied velocity views and stock collision effects match the fixed release."""
import os
from pathlib import Path
import subprocess
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE

class PlayerFeedbackDifferential(ContactDifferential):
 def test_stock_feedback(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_feedback_host.cpp'),str(ROOT/'bigworld/stock_native_client_drive_roster.cpp'),'-o',str(folder/'client_feedback_test.so')]
  subprocess.run(command,check=True,capture_output=True,text=True)
  fixture=r'''
import imp,math,random,sys,types
import client_feedback_test as native
base=SOURCE+'/src/res/scripts/client/gui/mods/offline_lan_0922/'
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 sys.modules[name]=types.ModuleType(name)
sys.modules['gui.mods.offline_lan_0922'].native_math=object()
collision=imp.load_source('fixed_collision',base+'collision_feedback.py')
tank=imp.load_source('fixed_tank',base+'tank_collision.py')
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
avatar=O();local=O(id=7,isStarted=True,filter=O(velocity='untouched',extra=19),label='local');remote=O(id=13,isStarted=True,filter=O(velocity='untouched',extra=23),label='remote')
actual=[];expected=[];retained=[]
def capture(target):
 def callback(a,first,second,point,now):
  assert a is avatar and first.label=='local' and second.label=='remote'
  assert first.filter.extra==19 and second.filter.extra==23
  target.append((tuple(first.filter.velocity),tuple(second.filter.velocity),tuple(point),now));retained.append(first)
 return callback
native.create(O(_avatar=avatar,_native_ram_contact_hook=(None,None,capture(actual))))
original=collision.CollisionFeedback();rng=random.Random(1513);now=0.
for i in range(600):
 now+=rng.choice([0.,.01,.05,.2,.35]);local.isStarted=i%17!=0;remote.isStarted=i%23!=0
 a=dict(x=rng.uniform(-2.,2.),y=rng.uniform(-1.,1.),z=rng.uniform(-2.,2.),yaw=rng.uniform(-3.,3.),shape=(1.4,2.8,-.5,1.6),vx=rng.uniform(-5.,5.),vy=rng.uniform(-1.,1.),vz=rng.uniform(-5.,5.))
 b=dict(a,x=a['x']+rng.uniform(-5.,5.),z=a['z']+rng.uniform(-5.,5.),yaw=rng.uniform(-3.,3.))
 if i%7:b.update(vx=rng.uniform(-5.,5.),vz=rng.uniform(-5.,5.))
 native.present(local,remote,a,b,now);point=tank.obb_overlap_point(a,b)
 if point is not None:
  low=max(a['y']+a['shape'][2],b['y']+b['shape'][2]);high=min(a['y']+a['shape'][3],b['y']+b['shape'][3])
  original.present(avatar,capture(expected),local,remote,tuple(a[n]for n in ('vx','vy','vz')),tuple(b[n]for n in ('vx','vy','vz')),(point[0],(low+high)*.5,point[1]),now,tuple)
 assert len(actual)==len(expected),(i,len(actual),len(expected))
 if actual:
  for first,second in zip(actual[-1][:3],expected[-1][:3]):
   assert all(abs(x-y)<1e-9 for x,y in zip(first,second)),(i,first,second)
 assert local.filter.velocity=='untouched' and remote.filter.velocity=='untouched'
assert len(actual)>100
assert retained[0].filter.extra==19 and retained[0].label=='local'
a.update(x=0.,z=0.,vx=3.,vy=0.,vz=0.);b.update(x=0.,z=0.,vx=0.,vy=0.,vz=0.);local.isStarted=remote.isStarted=True
count=len(actual);native.observed(7,13,(3.,0.,0.),(0.,0.,0.),now+1.)
native.present(local,remote,a,b,now+1.1);assert len(actual)==count
native.present(local,remote,a,b,now+1.21);assert len(actual)==count+1
count=len(actual);native.retire();native.present(local,remote,a,b,now+1.);assert len(actual)==count
native.close()
print('600 collision-feedback frames, forwarding views, cooldown and retirement passed')
'''
  fixture=fixture.replace('SOURCE',repr(str(SOURCE)))
  result=subprocess.run([py27,'-B','-c',fixture],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
