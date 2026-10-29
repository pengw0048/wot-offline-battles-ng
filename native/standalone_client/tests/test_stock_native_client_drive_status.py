"""Accepted player critical and stun state against the pinned data laws."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import sys,random,types
import client_drive_status_test as native
source=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922'
dd=types.ModuleType('device_damage');exec(compile(open(source+'/device_damage.py').read(),source+'/device_damage.py','exec'),dd.__dict__)
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
maxima=dict(engineHealth=240.,ammoBayHealth=180.,gunHealth=200.,turretRotatorHealth=150.,surveyingDeviceHealth=100.,radioHealth=120.)
td=O(_offline_critical_device_pools=dict((key,(value,value*.5))for key,value in maxima.items()))
roles=dict(commander=['commander','radioman','loader'],driver=['driver'],gunner1=['gunner','loader'],loader1=['loader'])
owner=native.create(dict(maxima=maxima,crew_roles=roles));rng=random.Random(144619)
keys=('reload','dispersion','aim_time','turret_speed','mobility','vision','signal','traverse','speed')
for i in range(420):
 ko=[key for key in sorted(roles) if rng.random()<.25]
 impaired=set(role for key in ko for role in roles[key]);devices=[];hp={};dead=set();yellow=set()
 for key,maximum in sorted(maxima.items()):
  value=rng.choice([0.,maximum*.3,maximum*.5,maximum*.8,maximum]);status=rng.choice(['normal','critical','destroyed']);hp[key]=value
  if status=='destroyed':dead.add(key)
  if status=='critical':yellow.add(key)
  devices.append(dict(name=key,hp=value,max_hp=maximum,state=status))
 factors=dict((key,rng.choice([.6,1.,1.2]))for key in keys)
 state=dict(critical=dict(devices=devices,crew_ko=ko,destroyed=sorted(dead)),stun_end_server_time_ms=100000+i,stun_attacker_kind='bot',stun_attacker_id=1,stun_factors=factors)
 result=native.update(owner,state,i,500.)
 for key in keys:
  expected=dd.crew_stat_factor(impaired,key)*dd.module_stat_factor(hp,dead,td,key,yellow)*factors[key]
  assert abs(result[key]-expected)<1e-12,(i,key,result[key],expected,state)
 assert result['casualties']==sorted(ko)
 # Same accepted stun signature does not silently expire at a render tick.
 held=native.update(owner,state,i+10000,200000.)
 assert held==result,(i,held,result)
 # Accepted clear is an explicit edge; module/crew penalties remain.
 state.update(stun_end_server_time_ms=0,stun_attacker_kind='',stun_attacker_id=0,stun_factors={})
 clear=native.update(owner,state,i+20000,None)
 for key in keys:assert abs(clear[key]-result[key]/factors[key])<1e-12,(i,key)
# A changed dictionary with the same revision is not an accepted state edge.
first=native.update(owner,{},50000,None);second=native.update(owner,dict(critical=dict(crew_ko=['commander'])),50000,None)
assert first==second
try:native.update(native.create(dict(maxima=maxima,crew_roles=roles)),dict(stun_end_server_time_ms=12),0,None)
except RuntimeError:pass
else:raise AssertionError('unanchored accepted stun was admitted')
print('native accepted critical/stun exact-law differential passed: 420 states')
'''
class NativeClientDriveStatusTest(unittest.TestCase):
 def test_original_laws(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('fixed source required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
  prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='drive-status-')as directory:
   folder=Path(directory);(folder/'fixture.py').write_text(FIXTURE)
   result=subprocess.run(['c++','-std=c++17','-Wno-register','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native'),str(ROOT/'tests/stock_native_client_drive_status_host.cpp'),'-o',str(folder/'client_drive_status_test.so')],capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stderr)
   result=subprocess.run([py27,'-B',str(folder/'fixture.py'),source],cwd=folder,capture_output=True,text=True)
   self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
