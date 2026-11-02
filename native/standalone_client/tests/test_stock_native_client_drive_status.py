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
# Exercise the real local Status + repair Owner chain, including accepted
# server echoes. A destroyed turret must not keep repaired tracks immobile.
import copy
for tracks in (['leftTrackHealth'],['rightTrackHealth'],['leftTrackHealth','rightTrackHealth']):
 pools=dict((name,200.)for name in tracks+['engineHealth','turretRotatorHealth'])
 owner=native.create(dict(maxima=pools,regeneration=dict((name,100.)for name in pools),crew_roles=roles))
 devices=[dict(name=name,hp=0. if name in tracks or name=='turretRotatorHealth' else 200.,max_hp=200.,state='destroyed' if name in tracks or name=='turretRotatorHealth' else 'normal')for name in sorted(pools)]
 state=dict(critical=dict(devices=devices,destroyed=sorted(tracks+['turretRotatorHealth']),crew_ko=['driver']),critical_revision=1,critical_base_revision=1,critical_ack_seq=0)
 broken=native.update(owner,state,1,None)
 assert set(tracks).issubset(broken['destroyed']) and broken['turret_speed']==0. and broken['mobility']>0.,broken
 partial=native.repair(owner,6.,6.);pending=copy.deepcopy(partial['report'])
 assert all(partial['hp'][name]==50. for name in tracks),partial
 # Repeated snapshots retain local progress while its checkpoint is pending.
 repeated=native.update(owner,state,2,None)
 assert repeated['hp']==partial['hp'] and repeated['destroyed']==partial['destroyed'],(partial,repeated)
 repaired=native.repair(owner,6.,12.)
 assert not set(tracks).intersection(repaired['destroyed']) and repaired['turret_speed']==0. and repaired['mobility']>0.,repaired
 completed=copy.deepcopy(repaired['report'])
 # An older partial ACK must not reinstall broken-track mobility flags.
 echoed=copy.deepcopy(state);echoed.update(critical_revision=2,critical_ack_seq=pending['critical_seq'])
 for row in echoed['critical']['devices']:
  if row['name'] in tracks:row['hp']=50.
 accepted=native.update(owner,echoed,3,None)
 assert not set(tracks).intersection(accepted['destroyed']) and all(accepted['hp'][name]==100. for name in tracks),accepted
 # Completion ACK retires the overlay. Non-track repair clears independently.
 echoed.update(critical_revision=3,critical_ack_seq=completed['critical_seq'])
 echoed['critical']['destroyed']=['turretRotatorHealth']
 for row in echoed['critical']['devices']:
  if row['name'] in tracks:row.update(hp=100.,state='critical')
 accepted=native.update(owner,echoed,4,None)
 assert accepted['report'] is None and accepted['destroyed']==['turretRotatorHealth'],accepted
 echoed.update(critical_revision=4)
 echoed['critical']['destroyed']=[]
 for row in echoed['critical']['devices']:
  if row['name']=='turretRotatorHealth':row.update(hp=100.,state='critical')
 accepted=native.update(owner,echoed,5,None)
 assert accepted['turret_speed']>0. and accepted['mobility']>0. and not accepted['destroyed'],accepted
 # A genuinely new hit resets ownership and must stop a track again.
 again=copy.deepcopy(state);again.update(critical_revision=5,critical_base_revision=5)
 accepted=native.update(owner,again,6,None)
 assert set(tracks).issubset(accepted['destroyed']) and all(accepted['hp'][name]==0. for name in tracks),accepted
 # Repairing tracks cannot override an independently destroyed engine.
 for row in again['critical']['devices']:
  if row['name']=='engineHealth':row.update(hp=0.,state='destroyed')
 again['critical']['destroyed']=sorted(tracks+['engineHealth','turretRotatorHealth'])
 again.update(critical_revision=6,critical_base_revision=6)
 native.update(owner,again,7,None)
 accepted=native.repair(owner,12.,24.)
 assert not set(tracks).intersection(accepted['destroyed']) and 'engineHealth' in accepted['destroyed'] and accepted['mobility']==0.,accepted
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
