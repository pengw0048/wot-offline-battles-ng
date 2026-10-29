"""Exact replica contact parameters and cold installation/resource fences."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''from __future__ import print_function
import ast,copy,json,math,random,sys,types
import offline_peer_installer_test as native
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import vehicle_physics as vp,bot_tactics,bot_gunnery
class O(object):
 trap=False
 def __init__(self,**kw):self.__dict__.update(kw)
 def __getattribute__(self,key):
  if key in ('_base','_factors','physics','chassis','type','_bots') and object.__getattribute__(self,'trap'):raise AssertionError('Hot Python contact metadata producer')
  return object.__getattribute__(self,key)
source=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/bot_runtime.py'
tree=ast.parse(open(source).read());wanted=set(('replica_contact_params','_resolve_bot_rating'))
methods=[n for cls in tree.body if isinstance(cls,ast.ClassDef) and cls.name=='BotRuntime' for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in wanted]
env=dict(bot_tactics=bot_tactics,bot_gunnery=bot_gunnery,_bot_physics_params=lambda descriptor,level:vp.derive_params(descriptor,descriptor._factors[level]))
cls=ast.ClassDef(name='Original',bases=[ast.Name(id='object',ctx=ast.Load())],body=methods,decorator_list=[])
exec(compile(ast.fix_missing_locations(ast.Module([cls])),source,'exec'),env)
def context(mode='mixed',rules=None,pins=None,round=77):
 bots=O(_bot_tactics=dict(behavior=rules or []),_bot_skill_mode=mode,_bot_skill_pins=pins or {},round_id=round)
 return O(_bots=bots)
def oracle(battle):
 o=env['Original']()
 for key,value in battle._bots.__dict__.items():setattr(o,key,value)
 return o
def descriptor(key,index):
 factors=dict((level,{'chassis/terrainResistance':(.8+level*.001,.9+level*.001,1.+level*.001),'vehicle/rotationSpeed':.5+level*.005,'engine/power':.6+level*.004}) for level in (75,90,100))
 d=O(_key=key,_shape=(1.5,3.,-.5,2.),physics=dict(weight=10000.+index*1000.,enginePower=150000.+index*10000.,speedLimits=(12.,5.),terrainResistance=(1.,1.3,2.),specificFriction=.7,brakeForce=0.,trackCenterOffset=1.4,nativePowerRatio=1.1,rollingFrictionFactors=(.95,.96,.97)),chassis=O(rotationSpeed=.7,rotationIsAroundCenter=bool(index%2)),type=O(tags=('heavyTank',)),_factors=factors)
 # A non-central track pivot uses its exact mounted gauge, as in the source.
 d._base=vp.derive_params(d);return d
keys=('mass','powerW','nativePowerRatio','speedFwd','speedBwd','specificFriction','brakeDecel','rotSpd','trackCenter','terrainResist','rotationIsAroundCenter')
def comparable(values):return dict((key,values[key]) for key in keys)
def same(a,b):
 if isinstance(a,(int,long,float)) and isinstance(b,(int,long,float)):assert abs(a-b)<=max(1.e-9,abs(a)*1.e-13),(a,b)
 elif isinstance(a,dict):
  assert set(a)==set(b),(a,b)
  for k in a:same(a[k],b[k])
 elif isinstance(a,(tuple,list)):
  assert len(a)==len(b),(a,b)
  for x,y in zip(a,b):same(x,y)
 else:assert a==b,(a,b)
def row(id,kind='bot',**kw):
 value=dict(key=kind+':'+str(id),kind=kind,id=id,descriptor=id,team=2,slot=id%15,skill_rating=.4)
 value.update(kw);return value
rules=[dict(team=0,class_tag='all',slot=-1,values=dict(crew_level=75)),dict(team=2,class_tag='heavyTank',slot=-1,values=dict(crew_level=90)),dict(team=2,class_tag='heavyTank',slot=4,values=dict(crew_level=100))]
rng=random.Random(1513);ratings=0
for mode in ('easy','relaxed','mixed','hard','brutal','invalid',None):
 for roundid in (None,0,1,77,2147483647):
  for team in (1,2):
   for slot in range(15):
    b=context(mode,round=roundid);d=descriptor('bot:1',1);raw=dict(team=team,slot=slot)
    expected=bot_gunnery.rating_crew_level(oracle(b)._resolve_bot_rating(raw));assert native.crew_level(b,raw,d)==expected,(mode,roundid,team,slot,expected,native.crew_level(b,raw,d));ratings+=1
for raw in [dict(skill_rating=v,skill='elite',team=1,slot=0) for v in (None,-1.,0.,.17,.34,.505,.67,1.,2.,'bad',float('nan'),True)]+[dict(skill=skill,team=1,slot=0) for skill in ('rookie','regular','veteran','elite','bad',None)]:
 b=context(pins={(1,0):'elite'});d=descriptor('bot:1',1);assert native.crew_level(b,raw,d)==bot_gunnery.rating_crew_level(oracle(b)._resolve_bot_rating(raw));ratings+=1
for index in range(240):
 b=context(rules=rules);original=oracle(b);d=descriptor('bot:1',index);raw=row(1,skill_rating=rng.uniform(-.1,1.1),team=1+index%2,slot=index%15,profile=dict(class_tag='heavyTank' if index%3 else 'mediumTank'))
 expected=original.replica_contact_params(raw,d);owner=native.create({1:d});actual=native.synchronize(owner,b,[raw]);same(comparable(actual['params']['bot:1']),comparable(expected));assert actual['producers']==1 and actual['parses']==1
# Humans donate canonical physics directly. Motion and critical-only updates
# must not generate a default crew, derive params or revisit Python descriptors.
b=context();d1=descriptor('bot:1',1);d2=descriptor('player:2',2);human=vp.derive_params(d2,{'engine/power':1.7,'vehicle/rotationSpeed':1.3,'chassis/terrainResistance':(.9,.8,.7)})
rows=[row(1),row(2,'player',effective_params=dict(physics=human))];h=native.create({1:d1,2:d2});result=native.synchronize(h,b,rows);same(result['params']['player:2'],human)
assert result['producers']==1 and result['parses']==2
b.trap=d1.trap=d2.trap=True
for index in range(300):
 for raw in rows:raw.update(x=index*.1,yaw=index*.01,critical=dict(fire=bool(index%2)))
 result=native.synchronize(h,b,rows);assert result['producers']==1 and result['parses']==2
b.trap=d1.trap=d2.trap=False
rows[1]['effective_params']['physics']['powerW']*=1.1
result=native.synchronize(h,b,rows);assert result['producers']==1 and result['parses']==3;same(result['params']['player:2'],human)
rows[0]['skill_rating']=1.;result=native.synchronize(h,b,rows);assert result['producers']==2 and result['parses']==4
rows[0]['descriptor_revision']=1;result=native.synchronize(h,b,rows);assert result['producers']==3 and result['parses']==5
# A malformed accepted human row retains its prior current resource metadata;
# a failing new Bot still contributes its real mounted mass/shape without grip.
d1._fail=True;rows[0]['descriptor_revision']=2;rows[1]['effective_params']={}
result=native.synchronize(h,b,rows);assert result['dynamics']=={'bot:1':False,'player:2':True}
d3=descriptor('bot:3',3);d3._fail=True;bad=row(3);cold=native.create({3:d3});fallback=native.synchronize(cold,context(),[bad]);assert fallback['dynamics']=={'bot:3':False};same(fallback['params']['bot:3'],dict(mass=d3.physics['weight'],shape=d3._shape))
# Resource absence removes only its own previous installation, and a ready
# descriptor transition reinstalls it. Epoch changes reread round-only tactics.
rows[0]['no_geometry']=True;result=native.synchronize(h,b,rows);assert 'bot:1' not in result['dynamics'] and 'player:2' in result['dynamics']
rows[0]['no_geometry']=False;rows[0]['descriptor_revision']=3;d1._fail=False;rows[1]['effective_params']=dict(physics=human);result=native.synchronize(h,b,rows);assert result['dynamics']['bot:1']
b._bots._bot_tactics=dict(behavior=[dict(team=0,class_tag='all',slot=-1,values=dict(crew_level=75))]);result=native.synchronize(h,b,rows,True);same(comparable(result['params']['bot:1']),comparable(oracle(b).replica_contact_params(rows[0],d1)))
print(json.dumps(dict(rating_cases=ratings,descriptor_parameter_cases=240,motion_only_updates=300,human_donation=True,geometry_failure_preserved=True)))
'''
class NativePeerInstallerTests(unittest.TestCase):
 def test_original_replica_parameters_and_cold_owner(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('Fixed v0.10.1 source input is required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='native-peer-installer-') as directory:
   folder=Path(directory);native=Path(source)/'native';law=(native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0];(folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
   cmd=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DWOT_CLIENT_REPLICA_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+directory,'-I'+str(ROOT/'bigworld'),'-I'+str(native),str(ROOT/'bigworld/stock_native_client_state.cpp'),str(ROOT/'tests/stock_native_client_drive_peers_python_host.cpp'),'-o',str(folder/'offline_peer_installer_test.so')]
   result=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr)
   fixture=folder/'fixture.py';fixture.write_text(FIXTURE);result=subprocess.run([py27,'-B',str(fixture),source],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'));self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
if __name__=='__main__':unittest.main()
