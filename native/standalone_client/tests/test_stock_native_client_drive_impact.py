"""Player landing observations retain the original skid/damage/cooldown law."""
import os
from pathlib import Path
import subprocess
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE
class PlayerImpactDifferential(ContactDifferential):
 def test_landing_and_realized_air_wall_observations(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip();big=ROOT/'bigworld'
  command=['c++','-std=c++17','-bundle','-undefined','dynamic_lookup','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-register','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(big),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_impact_host.cpp')]+[str(big/name)for name in ('stock_native_client_drive_publication.cpp','stock_native_bot_environment.cpp','stock_native_bot_internal_geometry.cpp','stock_native_bot_step_state.cpp','stock_native_bot_combat.cpp')]+['-o',str(folder/'client_impact_test.so')]
  compiled=subprocess.run(command,capture_output=True,text=True);self.assertEqual(compiled.returncode,0,compiled.stderr)
  script=r'''
import ast,math,random,types
import client_impact_test as native
base=SOURCE+'/src/res/scripts/client/gui/mods/offline_lan_0922/';tree=ast.parse(open(base+'battle_runtime.py').read());outer=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='BattleRuntime');outer.body=[n for n in outer.body if isinstance(n,ast.FunctionDef)and n.name in ('_apply_fall_damage','_apply_landing_impact','_apply_world_contact_impact')];module=ast.Module(body=[outer]);ast.fix_missing_locations(module)
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
physics=ast.parse(open(base+'vehicle_physics.py').read());selected=[n for n in physics.body if isinstance(n,ast.FunctionDef)and n.name in ('fall_damage','world_impact_speed')];env=dict(math=math,FALL_SAFE_SPEED=10.,FALL_DMG_PER_MS=.03);exec(compile(ast.Module(body=selected),base+'vehicle_physics.py','exec'),env);vp=O(**env);ns=dict(math=math,vehicle_physics=vp,lan_protocol=O(PLAYER_LANDING_MAX_IMPACT_SPEED=200.),MAX_PENDING_LANDING_IMPACTS=32,sys=O(stdout=O(write=lambda value:None)));exec(compile(module,base+'battle_runtime.py','exec'),ns);entity=O(typeDescriptor=O(maxHealth=1000),matrix=None,health=1000);rng=random.Random(1513)
def owner():
 h=ns['BattleRuntime']();h.client=None;h._landing_contact_witnesses=();h._landing_contact_normal=(0.,1.,0.);h._landing_contact_pose=None;h._pending_landing_impacts=[];return h
for i in range(640):
 h=owner();x=rng.uniform(-30.,30.);z=rng.uniform(-30.,30.);speed=rng.choice([0.,10.,10.033333333333333,200.,251.,rng.uniform(-20.,50.)]);normal=bool(i%2);slide=rng.uniform(0.,15.);h._local_air_lateral=(x,z);h._local_slide_speed=slide
 damage=h._apply_landing_impact(entity,speed,normal_impact=normal);actual=native.landing(speed,x,z,slide,normal)
 assert actual[0]==damage and actual[1]==h._local_slide_speed and actual[2:4]==h._local_air_lateral and actual[4]==h._pending_landing_impacts,(i,actual,damage,h.__dict__)
h=owner();rows=[];expected=[];now=0.
for i in range(260):
 now+=rng.choice([0.,.05,.1,.25]);h._local_airborne=bool(i%5);hit=bool(i%7);normal=rng.choice([(0.,0.,0.),(-1.,0.,0.),(0.,-1.,0.),(.1,.2,-.5),(1e-9,0.,0.)]);speed=35. if i%23==1 else rng.uniform(-8.,8.);yaw=rng.uniform(-math.pi,math.pi);h._clock=lambda:now;trace=dict(normal=normal)
 if hit:trace['hit']=(0.,0.,0.)
 expected.append(h._apply_world_contact_impact(entity,trace,speed,yaw));rows.append((int(h._local_airborne),int(hit))+normal+(speed,yaw,now))
actual,queued=native.world(rows)
assert all(abs(a-b)<1e-12 for a,b in zip(actual,expected)),(actual,expected)
assert queued==h._pending_landing_impacts,(queued,h._pending_landing_impacts)
print('640 landing/skid cases and 260 realized wall/cooldown frames passed')
'''.replace('SOURCE',repr(str(SOURCE)))
  result=subprocess.run([py27,'-B','-c',script],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
