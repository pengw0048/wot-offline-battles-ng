"""Player-only track auto repair, CAS lineage and stock repair HUD edges."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''from __future__ import print_function
import ast,copy,json,math,random,sys,types
import offline_client_critical_test as native
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import critical_damage as cd,equipment_mechanics as em
path=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py';tree=ast.parse(open(path).read())
names=set(('_tick_local_track_repair','_local_repair_factor','_tick_critical_states','_present_repair_progress','_queue_local_track_repair','_reconcile_critical_authority','acknowledge_local_damage_report','_critical_state'))
methods=[n for cls in tree.body if isinstance(cls,ast.ClassDef) and cls.name=='BattleRuntime' for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in names]
env=dict(critical_damage=cd,equipment_mechanics=em,_number=lambda value,default=0.:default if value is None else float(value),CRITICAL_REPAIR_NETWORK_SECONDS=1.)
cls=ast.ClassDef(name='Original',bases=[ast.Name(id='object',ctx=ast.Load())],body=methods,decorator_list=[]);exec(compile(ast.fix_missing_locations(ast.Module([cls])),path,'exec'),env)
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
maxima=dict(leftTrackHealth=101.,rightTrackHealth=133.,engineHealth=240.,gunHealth=200.,fuelTankHealth=150.)
regeneration=dict(leftTrackHealth=50.0005,rightTrackHealth=78.0005,engineHealth=120.,gunHealth=100.,fuelTankHealth=75.)
extras=dict((name,index+1) for index,name in enumerate(sorted(maxima)))
metadata=dict(maxima=maxima,regeneration=regeneration,extras=extras,repair_speed_factor=1.25)
def full(entity):
 raw=cd._state(entity);return cd._payload(raw,raw,entity.typeDescriptor,force=True)
def entity(raw):
 d=O(_offline_critical_device_pools=dict((key,(maxima[key],regeneration[key])) for key in maxima),miscAttrs=dict(repairSpeedFactor=metadata['repair_speed_factor']),maxHealth=500)
 e=O(id=101,typeDescriptor=d,health=500,maxHealth=500,devices_hp={},_destroyed_devices=set(),_critical_devices=set(),_crew_ko=set(),is_on_fire=False,_ammo_rack_death=False,appearance=O(addCrashedTrack=lambda *args:None,delCrashedTrack=lambda *args:None))
 install(e,raw);return e
def install(e,raw):
 e.devices_hp=dict((row['name'],row['hp']) for row in raw.get('devices',[]));e._destroyed_devices=set(raw.get('destroyed',[]))|set(row['name'] for row in raw.get('devices',[]) if row.get('state')=='destroyed');e._critical_devices=set(row['name'] for row in raw.get('devices',[]) if row.get('state')=='critical');e._crew_ko=set(raw.get('crew_ko',[]));e.is_on_fire=bool(raw.get('fire'));e._ammo_rack_death=bool(raw.get('ammo_rack_death'))
def initial():return dict(devices=[dict(name=key,hp=0. if 'Track' in key else 10.,max_hp=maxima[key],state='destroyed') for key in sorted(maxima)],destroyed=sorted(maxima),crew_ko=['loader1'],fire=True,ammo_rack_death=False,events=[])
def oracle(raw):
 o=env['Original']();e=entity(raw);record=dict(local=True,engine_id=101,critical_state=copy.deepcopy(raw),state=dict(health=500,critical=copy.deepcopy(raw)),critical_revision=1,critical_base_revision=1,critical_ack_seq=0,kind='player')
 o.client=O(player_id=1);o._records={'player:1':record};o._server_entity=lambda unused:e;o._record_alive=lambda record,entity:entity.health>0;o._local_loadout=lambda descriptor:dict(repair_factor=.57);o._equipment_state=[];o._local_damage_report=None;o._local_critical_base_revision=1;o._local_critical_next_seq=0;o._local_critical_owned=False;o._local_critical_server_revision=1;o._next_critical_report_time=0.;o._runtime=O(constants=O(VEHICLE_MISC_STATUS=O(DESTROYED_DEVICE_IS_REPAIRING=5)));o.edges=[];o._avatar=O(updateVehicleMiscStatus=lambda id,status,code,period:o.edges.append((code,float(period[0]))));o._critical_extra_index=lambda descriptor,name:extras[name];o._tick_equipment_cooldowns=lambda now:None;o.now=0.;o._clock=lambda:o.now;o.payload=None;o.new_report=None
 o._present_critical=lambda record,events,attacker:setattr(o,'payload',dict(record['critical_state'],events=copy.deepcopy(events)))
 queue=o._queue_local_track_repair
 def report(payload):o.new_report=queue(payload);return o.new_report
 o._queue_local_track_repair=report
 return o,e,record
def inspect(o,e):return dict(base=o._local_critical_base_revision,seq=o._local_critical_next_seq,server=o._local_critical_server_revision,owned=o._local_critical_owned,report=o._local_damage_report,next=o._next_critical_report_time,hp=e.devices_hp,destroyed=sorted(e._destroyed_devices),critical=sorted(e._critical_devices),progress=getattr(e,'_offline_lan_repair_progress',{}))
def same(a,b):
 if isinstance(a,(int,long,float)) and isinstance(b,(int,long,float)):assert abs(a-b)<=max(1.e-9,abs(float(a))*1.e-13),(a,b)
 elif isinstance(a,dict):
  assert set(a)==set(b),(a,b)
  for k in a:same(a[k],b[k])
 elif isinstance(a,(tuple,list)):
  assert len(a)==len(b),(a,b)
  for x,y in zip(a,b):same(x,y)
 else:assert a==b,(a,b)
raw=initial();o,e,record=oracle(raw);h=native.create(metadata,raw);steps=0
for index in range(1500):
 dt=[0.,.001,.016,.05,.2,1.1,3.7][index%7];o.now+=dt;o.edges=[];o.payload=None;o.new_report=None
 if index%37==0:
  # A new irreversible damage lineage discards only the preceding owner CAS.
  raw=initial();raw['devices'][0]['hp']=regeneration[raw['devices'][0]['name']] if index%74==0 else 0.;base=1+index//37;revision=2+index;ack=0
  canonical=copy.deepcopy(raw);applied=o._reconcile_critical_authority(record,dict(critical_revision=revision,critical_base_revision=base,critical_ack_seq=ack),canonical)
  if applied:install(e,canonical);record['critical_state']=copy.deepcopy(canonical)
  actual=native.accept(h,raw,revision,base,ack);assert actual.pop('applied')==applied;same(inspect(o,e),actual)
 elif index%5==0:
  # Same-base canonical rows cannot rewind already-owned repair, and ACK
  # retirement must not mask a concurrent engine/fire/physical-crew change.
  raw=initial();raw['fire']=bool(index%2);raw['devices'][0]['hp']=17.;ack=o._local_critical_next_seq if index%10==0 else 0;revision=2+index;base=o._local_critical_base_revision
  if o._local_damage_report and index%20==0:
   for row in raw['devices']:
    pending=next((r for r in o._local_damage_report['tracks'] if r['name']==row['name']),None)
    if pending:row.update(pending)
   raw['destroyed']=[r['name'] for r in raw['devices'] if r['state']=='destroyed']
  canonical=copy.deepcopy(raw);applied=o._reconcile_critical_authority(record,dict(critical_revision=revision,critical_base_revision=base,critical_ack_seq=ack),canonical)
  if applied:install(e,canonical);record['critical_state']=copy.deepcopy(canonical)
  actual=native.accept(h,raw,revision,base,ack);assert actual.pop('applied')==applied;same(inspect(o,e),actual)
 factor=[0.,.57,1.,1.5][index%4];bonus=.1 if index%3 else 0.;o._local_loadout=lambda descriptor:dict(repair_factor=factor);o._equipment_state=[dict(equipment=dict(kind='repairkit',bonusValue=bonus),usesLeft=1 if index%11 else 0)]
 repair=o._local_repair_factor(e.typeDescriptor);eligible=index%43!=0;e.health=500 if eligible else 0
 o._tick_critical_states(dt);actual=native.advance(h,dt,o.now,repair,eligible)
 same(o.payload,actual.pop('payload'));same(o.new_report,actual.pop('new_report'))
 edges=actual.pop('progress_edges');same(sorted(o.edges),sorted((code,seconds) for name,code,seconds in edges));same(inspect(o,e),actual);steps+=1
# Canonical red-at-rounded-cap must close its model/UI edge even with no HP gain.
raw=initial()
for row in raw['devices']:
 if 'Track' in row['name']:row['hp']=regeneration[row['name']]
o,e,record=oracle(raw);h=native.create(metadata,raw);o.now=1.;o._tick_critical_states(.1);actual=native.advance(h,.1,1.,.57,True);same(o.payload,actual.pop('payload'));same(o.new_report,actual.pop('new_report'));actual.pop('progress_edges');same(inspect(o,e),actual)
print(json.dumps(dict(track_ticks=steps,cap_completion=True,cas_same_base=True,new_lineage=True,other_modules_never_locally_advanced=True)))
'''
BRIDGE_FIXTURE=r'''from __future__ import print_function
import ast,copy,imp,sys,time,types
import offline_client_critical_native as native
sys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')
for name in ('gui','gui.mods'):
 module=types.ModuleType(name);module.__path__=[sys.path[0]+'/'+name.replace('.','/')];sys.modules[name]=module
import gui.mods.offline_lan_0922
facade=imp.load_source('gui.mods.offline_lan_0922.native_client_critical',sys.argv[2]+'/stock_native_client_critical.py')
setattr(sys.modules['gui.mods.offline_lan_0922'],'native_client_critical',facade)
bindings=imp.load_source('critical_bindings',sys.argv[2]+'/stock_native_client_critical_bindings.py')
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
# Exact scripts/common/constants.pyc uses an old-style class on Python 2.
class VehicleMiscStatus:
 DESTROYED_DEVICE_IS_REPAIRING=7
class NewStyle(object):
 value=13
 def __getattribute__(self,name):raise AssertionError('Native new-style input invoked a Python hook')
for value in (VehicleMiscStatus,VehicleMiscStatus()):
 assert native.attribute(value,'DESTROYED_DEVICE_IS_REPAIRING')==7
assert native.attribute(NewStyle,'value')==13
assert native.attribute(NewStyle(),'value')==13
for value in (VehicleMiscStatus,VehicleMiscStatus(),NewStyle,NewStyle()):
 try:native.attribute(value,'missing')
 except AttributeError:pass
 else:raise AssertionError('Missing attributes must retain AttributeError')
def bomb(*args,**kw):raise AssertionError('Python critical/equipment business callback')
raw=dict(devices=[dict(name='leftTrackHealth',hp=0.,max_hp=20.,state='destroyed'),dict(name='rightTrackHealth',hp=0.,max_hp=20.,state='destroyed'),dict(name='engineHealth',hp=40.,max_hp=40.,state='normal')],destroyed=['leftTrackHealth','rightTrackHealth'],crew_ko=[],fire=False,ammo_rack_death=False,events=[])
def battle():
 calls=[];pools=dict(leftTrackHealth=(20.,10.),rightTrackHealth=(20.,10.),engineHealth=(40.,20.));extra=[None]+[O(name=name) for name in sorted(pools)];descriptor=O(_offline_critical_device_pools=pools,miscAttrs=dict(repairSpeedFactor=1.),extras=extra,extrasDict=dict((e.name,e) for e in extra if e is not None),maxHealth=500)
 entity=O(id=101,typeDescriptor=descriptor,health=500,maxHealth=500,devices_hp=dict((r['name'],r['hp']) for r in raw['devices']),_destroyed_devices=set(raw['destroyed']),_critical_devices=set(),appearance=O(delCrashedTrack=lambda left:calls.append(('crash',left))))
 b=O(state='running',_worker_mode=False,_replay_mode=False,_native_client_critical=None,_test_bonus=0.,_test_effective=dict(loadout=dict(repair_factor=.57)),_local_critical_base_revision=1,_local_critical_next_seq=0,_local_critical_server_revision=1,_local_critical_owned=False,_local_damage_report=None,_next_critical_report_time=0.,_runtime=O(bigworld=O(time=time.time),constants=O(VEHICLE_MISC_STATUS=VehicleMiscStatus,DAMAGE_INFO_INDICES=dict(DEVICE_REPAIRED_TO_CRITICAL=11))),_avatar=O(updateVehicleMiscStatus=lambda *args:calls.append(('progress',args))),_server=O(vehicle_id=101))
 b._records={'player:1':dict(local=True,engine_id=101,critical_revision=1,critical_state=copy.deepcopy(raw),state=dict(alive=True,health=500,critical=copy.deepcopy(raw)))};b._warn_optional_failure=lambda *args,**kw:calls.append(('warning',args));b._damage_info_is_serviceable=lambda:True;b._show_damage_info=lambda *args:calls.append(('repair',args))
 for name in ('_tick_local_track_repair','_local_repair_factor','_queue_local_track_repair','acknowledge_local_damage_report','_present_repair_progress','_tick_equipment_cooldowns','_present_critical'):setattr(b,name,bomb)
 native.setup(b,entity,descriptor,raw)
 return b,entity,calls
b,e,calls=battle();source=open(sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read();tree=ast.parse(bindings.battle(source));method=next(n for cls in tree.body if isinstance(cls,ast.ClassDef) and cls.name=='BattleRuntime' for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='_tick_critical_states');namespace={'__name__':'gui.mods.offline_lan_0922.battle_runtime','__package__':'gui.mods.offline_lan_0922'};exec(compile(ast.Module([method]),'native-critical-prefix','exec'),namespace)
namespace['_tick_critical_states'](b,.5);initial=native.inspect();assert initial['state_revision']==0 and initial['hp']['leftTrackHealth']>0. and initial['equipment']==1 and initial['mobility']==1. and e.is_tracked
assert b._local_damage_report and b._local_critical_owned
before=initial['hp']['leftTrackHealth'];canonical=copy.deepcopy(raw);canonical['devices'][2].update(hp=3.,state='critical');source=dict(critical_revision=2,critical_base_revision=1,critical_ack_seq=0);native.ingress(canonical,1,2,0)
assert native.reconcile(b,b._records['player:1'],source,canonical)
assert native.inspect()['hp']['leftTrackHealth']==before and native.inspect()['hp']['engineHealth']==3.
assert next(r for r in canonical['devices'] if r['name']=='leftTrackHealth')['hp']==before
# The retained stock accepted-state adapter mirrors non-track presentation.
e.devices_hp['engineHealth']=3.;e._critical_devices.add('engineHealth')
namespace['_tick_critical_states'](b,20.);result=native.inspect();assert result['state_revision']==1 and result['equipment']==2 and result['hp']['leftTrackHealth']==10. and result['mobility']==.5
assert e.is_tracked is False and not set(('leftTrackHealth','rightTrackHealth'))&e._destroyed_devices
assert [row[0] for row in calls].count('crash')==2 and [row[0] for row in calls].count('repair')==2
assert all(row['state']=='critical' for row in b._local_damage_report['tracks'])
assert native.reconcile(b,{},source,canonical) is False
facade.retire(b);facade.retire(b);assert b._native_client_critical is None
# A failed visual slot does not roll back repair or prevent equipment clocks;
# its sibling rows can still publish. The failed current HUD edge retries.
b,e,calls=battle();counter=[0]
def fails_once(*args):
 counter[0]+=1
 if counter[0]==1:raise RuntimeError('Flash slot unavailable')
 calls.append(('progress',args))
b._avatar.updateVehicleMiscStatus=fails_once
namespace['_tick_critical_states'](b,.1);assert native.inspect()['equipment']==1 and any(c[0]=='warning' for c in calls)
namespace['_tick_critical_states'](b,.1);assert native.inspect()['equipment']==2 and counter[0]>=3
facade.retire(b)
# Synchronous stock teardown stops all remaining sibling presentation writes.
b,e,calls=battle()
def tears_down(*args):calls.append(('teardown',args));facade.retire(b);b.state='stopped'
b._avatar.updateVehicleMiscStatus=tears_down
namespace['_tick_critical_states'](b,.1);assert [row[0] for row in calls]==['teardown'] and native.inspect()['equipment']==0
assert b._native_client_critical is None
print('Native critical production bridge traps, CAS overlay and retirement passed')
'''

class NativeClientCriticalTests(unittest.TestCase):
 def test_original_track_repair_cas_and_hud(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('Fixed v0.10.1 source input is required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='native-client-critical-') as directory:
   folder=Path(directory);cmd=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DWOT_CLIENT_REPLICA_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_client_state.cpp'),str(ROOT/'bigworld/stock_native_client_critical.cpp'),str(ROOT/'tests/stock_native_client_critical_host.cpp'),'-o',str(folder/'offline_client_critical_test.so')]
   result=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr);fixture=folder/'fixture.py';fixture.write_text(FIXTURE);result=subprocess.run([py27,'-B',str(fixture),source],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'));self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
 def test_production_bridge_hot_traps_and_retirement(self):
  source=os.environ.get('WOT_FORK_0101_SOURCE')
  if not source:self.skipTest('Fixed v0.10.1 source input is required')
  py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip();prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
  with tempfile.TemporaryDirectory(prefix='native-client-critical-bridge-') as directory:
   folder=Path(directory);sources=['stock_native_client_state.cpp','stock_native_client_critical.cpp','stock_native_client_drive_status.cpp','stock_native_bot_combat.cpp']
   cmd=['c++','-std=c++17','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DWOT_CLIENT_REPLICA_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native')]+[str(ROOT/'bigworld'/name) for name in sources]+[str(ROOT/'tests/stock_native_client_critical_bridge_host.cpp'),'-o',str(folder/'offline_client_critical_native.so')]
   result=subprocess.run(cmd,capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stderr);script=folder/'bridge.py';script.write_text(BRIDGE_FIXTURE);result=subprocess.run([py27,'-B',str(script),source,str(ROOT/'bigworld')],capture_output=True,text=True,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'));self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
if __name__=='__main__':unittest.main()
