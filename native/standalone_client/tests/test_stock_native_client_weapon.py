"""Exact player ammunition, reload, mode and scatter transitions in C++."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
from __future__ import print_function
import copy,gc,json,math,random,struct,sys,types
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
    module=types.ModuleType(name);module.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=module
from gui.mods.offline_lan_0922 import gun_mechanics as original
import offline_client_weapon_native as native
class Box(object):
    def __init__(self,**values):self.__dict__.update(values)
fields=('base_dispersion','after_shot','after_shot_in_burst','burst_count','burst_interval','aim_time','reload','clip_size','clip_reload','shot_index','clip','reload_time','reload_duration','dispersion','load_started','pending_index','_burst_remaining','_burst_total','ammo')
def same(a,b):
    for field in fields:
        x,y=getattr(a,field),getattr(b,field)
        if isinstance(x,float):assert abs(x-y)<=1.e-12*max(1.,abs(x)),(field,x,y)
        else:assert x==y,(field,x,y)
def descriptor(clip,burst):
    return Box(maxAmmo=45,activeGunShotIndex=1,gun=Box(shots=[dict(shell=dict(compactDescr=n)) for n in (2,3,4)],shotDispersionAngle=.12,shotDispersionFactors=dict(afterShot=1.6,afterShotInBurst=.7,turretRotation=.15),burst=burst,aimingTime=2.1,reloadTime=3.2,clip=(clip,.3)),chassis=Box(shotDispersionFactors=(.13,.11)),turret=Box())
loadout=dict(crew_multiplier=.95,dispersion_factor=.8,aim_time_factor=.9,reload_factor=.85,bloom_move_factor=.96,bloom_rotation_factor=.83,bloom_turret_factor=.925)
rng=random.Random(1513);steps=0
for clip in (1,3,10):
 for burst in ((1,0.),(3,.1)):
  for layout in (None,{}, {2:12,3:5,4:9},{2:0,3:0,4:6}):
   for order in (None,[4,2,3]):
    d=descriptor(clip,burst);a=original.GunState(d,loadout,layout,order);b=native.create(d,loadout,layout,order);same(a,b)
    for step in range(180):
     op=rng.randrange(8)
     if op==0:
      args=(rng.choice((0.,.01,.1,.9,4.5)),bool(rng.randrange(2)),rng.uniform(-25.,25.),rng.uniform(-1.,1.),rng.uniform(-1.,1.),d)
      kw=dict(dispersion_factor=rng.choice((1.,2.,.8)),aim_time_factor=rng.choice((.5,1.,2.)))
      assert a.tick(*args,**kw)==b.tick(*args,**kw)
     elif op==1:
      i=rng.randrange(-2,5);instant=bool(rng.randrange(2));assert a.sync_shell_index(i,instant=instant)==b.sync_shell_index(i,instant=instant)
     elif op==2:
      i=rng.randrange(-2,5);assert a.request_shell_index(i)==b.request_shell_index(i)
     elif op==3:assert a.reload_partial_clip()==b.reload_partial_clip()
     elif op==4:
      n=rng.randrange(0,8);assert a.begin_burst(n)==b.begin_burst(n)
     elif op==5:
      final=bool(rng.randrange(2));factor=rng.choice((.5,1.,2.));assert a.commit_burst_round(final,factor)==b.commit_burst_round(final,factor)
     elif op==6:assert a.cancel_burst()==b.cancel_burst()
     else:
      factor=rng.choice((.5,1.,2.));assert a.commit_fire(factor)==b.commit_fire(factor)
     same(a,b);assert a.can_fire()==b.can_fire();steps+=1
    changed=copy.deepcopy(d);changed.gun.reloadTime*=2;changed.gun.shotDispersionAngle*=.8;changed.gun.aimingTime*=.7
    assert a.adopt_descriptor(changed)==b.adopt_descriptor(changed);same(a,b)
    b._offline_replay_owned=True;assert b._offline_replay_owned
    b.ammo=[9,8,7];assert b.ammo==[9,8,7]
    b.pending_index=1;assert b.pending_index==1
    b.pending_index=None;assert b.pending_index is None
# All invalid mode fields are checked before replacing the persistent law.
d=descriptor(3,(1,0.));b=native.create(d,loadout,None,None);before=tuple(getattr(b,n) for n in fields)
bad=copy.deepcopy(d);bad.gun.burst=(4,-1.)
try:b.adopt_descriptor(bad)
except (RuntimeError,ValueError):pass
else:raise AssertionError('invalid mode was adopted')
assert before==tuple(getattr(b,n) for n in fields)
class Vector(object):
    def __init__(self,x,y,z):self.x=float(x);self.y=float(y);self.z=float(z)
    def normalise(self):
        length=math.sqrt(self.x*self.x+self.y*self.y+self.z*self.z)
        if length:self.x/=length;self.y/=length;self.z/=length
a=original.GunState(d,loadout,None,None)
for seed in range(80):
 for xyz in ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.),(1.,2.,3.),(0.,0.,0.)):
  x=Vector(*xyz);y=Vector(*xyz);random.seed(seed);a.scatter(x,dispersion_angle=.3);random.seed(seed);b.scatter(y,dispersion_angle=.3)
  assert all(abs(getattr(x,n)-getattr(y,n))<1.e-14 for n in ('x','y','z')),(seed,xyz,vars(x),vars(y))
def bomb(*args,**kw):raise AssertionError('Python player gun business callback')
for name in ('tick','begin_burst','commit_burst_round','can_fire','scatter','adopt_descriptor'):setattr(original.GunState,name,bomb)
b.tick(.1,True,0.,0.,0.,d);b.begin_burst(1);b.commit_burst_round(True);b.scatter(Vector(1.,2.,3.))
gc.collect()
print(json.dumps({'player_edges':steps,'scatter_cases':400,'python_business_callbacks':0}))
'''


class NativeClientWeaponTests(unittest.TestCase):
    def test_player_weapon_matches_fixed_gun_state(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Fixed v0.10.1 source input is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-client-weapon-') as directory:
            binary = Path(directory) / 'offline_client_weapon_native.so'
            files = ['stock_native_client_weapon.cpp', 'stock_native_client_weapon_python.cpp', 'stock_native_projectile_random.cpp']
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-DWOT_CLIENT_WEAPON_HOST_BUILD', '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld')] + [str(ROOT / 'bigworld' / name) for name in files] + [str(ROOT / 'tests/stock_native_client_weapon_host.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            script = Path(directory) / 'fixture.py'
            script.write_text(FIXTURE)
            run = subprocess.run([py27, '-B', str(script), source], capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('"python_business_callbacks": 0', run.stdout)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
