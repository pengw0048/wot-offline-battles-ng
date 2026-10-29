"""Visible peer timestamp history and ACK-coherent momentum prediction."""
import os
from pathlib import Path
import subprocess
import unittest
from test_stock_native_client_drive_contacts import ContactDifferential,ROOT,SOURCE

FIXTURE=r'''
import sys,math,random,types,ast,bisect
import client_drive_peers_test as native
base=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922'
for name in('gui','gui.mods','gui.mods.offline_lan_0922'):
 module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
module=types.ModuleType('gui.mods.offline_lan_0922.worker_diagnostics');module.observed=lambda *args:lambda fn:fn;sys.modules[module.__name__]=module
vp=types.ModuleType('vehicle_physics');exec(compile(open(base+'/vehicle_physics.py').read(),base+'/vehicle_physics.py','exec'),vp.__dict__);vp._release_drive=lambda *args:None;vp._native_contact_ground=lambda *args:None
ram=types.ModuleType('ram_motion');exec(compile(open(base+'/ram_motion.py').read(),base+'/ram_motion.py','exec'),ram.__dict__)
rng=random.Random(144619)
p=dict(mass=30000.,powerW=400000.,nativePowerRatio=1.,speedFwd=16.,speedBwd=7.,specificFriction=.7,brakeDecel=7.,rotSpd=.8,terrainResist=(1.,1.4,2.5));shape=(1.5,3.,-.4,2.)
for i in range(420):
 state=dict(alive=bool(i%3),airborne=not(i%7),yaw=rng.uniform(-math.pi,math.pi),pitch=rng.uniform(-1.,1.),roll=rng.uniform(-1.,1.),speed=rng.uniform(-8.,18.),push_x=rng.uniform(-3.,3.),push_z=rng.uniform(-3.,3.),push_yaw=rng.uniform(-.5,.5),movement_dir=rng.choice([-1.,0.,1.]),rotation_dir=rng.choice([-1.,0.,1.]),service_brake=not(i%5));stamp=rng.uniform(10.,50.);now=stamp+rng.uniform(0.,.6);impulses=sorted((stamp+rng.uniform(-.1,.8),rng.uniform(-1.,1.),rng.uniform(-1.,1.),rng.uniform(-.1,.1))for _ in range(i%5))
 expected=vp.predict_contact_velocity(p,state,shape,stamp,now,impulses);result=native.predict(p,vp.__dict__,state,shape,stamp,now,impulses)
 for j,(a,b)in enumerate(zip(expected,result)):assert abs(a-b)<1e-10,(i,j,expected,result,state,stamp,now,impulses)
# The complete pinned history projection independently selects the same exact
# samples, neighbouring velocity and non-future ram-motion witness.
tree=ast.parse(open(base+'/battle_runtime.py').read());outer=next(n for n in tree.body if isinstance(n,ast.ClassDef)and n.name=='BattleRuntime');outer.body=[n for n in outer.body if isinstance(n,ast.FunctionDef)and n.name in('_ram_bot_state_at','_ram_bot_revision_at')];module=ast.Module(body=[outer]);ast.fix_missing_locations(module);ns=dict(bisect=bisect,ram_motion=ram,_number=lambda v:float(v),_angle_delta=lambda a,b:(b-a+math.pi)%(2.*math.pi)-math.pi);exec(compile(module,'original-ram-history','exec'),ns);old=ns['BattleRuntime']();old._ram_bot_lookup_cache={};old._ram_bot_history_index={2:[]};old._ram_bot_history={};rows=[]
for i in range(32):
 stamp=1000000+i*100000+(0 if i<16 else 25000);state=dict(time_us=stamp,revision=i+1,x=rng.uniform(-10.,10.),y=rng.uniform(0.,1.),z=rng.uniform(-10.,10.),yaw=rng.uniform(-math.pi,math.pi),pitch=0.,roll=0.,alive=True,ram_motion=[[1,i+1,stamp-50000]+[rng.uniform(-1.,1.)for _ in range(18)]])
 rows.append(state);old._ram_bot_history_index[2].append((stamp,i+1));old._ram_bot_history[i+1]={2:state}
for cursor in [900000,4500000]+[row['time_us']for row in rows]+[row['time_us']+50000 for row in rows]:
 revision=old._ram_bot_revision_at(2,cursor);expected=old._ram_bot_state_at(2,revision,cursor,velocity_only=True)if revision is not None else None;result=native.history(rows,cursor,1)
 if expected is None:assert result is None,(cursor,result)
 else:
  assert result['velocity']==tuple(expected[k]for k in('ram_vx','ram_vy','ram_vz')),(cursor,result,expected)
  witness=ram.for_player(expected['ram_motion'],1);assert result['witness']==(tuple(witness)if witness else None),(cursor,result,expected)
print('native peer ACK velocity/history differential passed: 420 predictions, 66 history cursors')
'''
class PlayerPeersDifferential(ContactDifferential):
 def test_native_prediction_and_history(self):
  folder=Path(self.folder.name);py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  result=subprocess.run(['c++','-std=c++17','-ffp-contract=off','-Wno-register','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(SOURCE)/'native'),str(ROOT/'tests/stock_native_client_drive_peers_host.cpp'),'-o',str(folder/'client_drive_peers_test.so')],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
  (folder/'peers_fixture.py').write_text(FIXTURE);result=subprocess.run([py27,'-B',str(folder/'peers_fixture.py'),SOURCE],cwd=folder,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
