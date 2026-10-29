"""Native projectile total-owner ingress and transport on real CPython 2.7."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SCRIPT=r'''
import copy,random,sys,types
import projectile_python_test as native
items=types.ModuleType('items');stun=types.ModuleType('items.stun');stun.g_cfg={};sys.modules['items']=items;sys.modules['items.stun']=stun
class Battle(object):
    def _clock(self):raise AssertionError("legacy clock wrapper")
    def _prebattle_seconds(self):raise AssertionError("legacy countdown wrapper")
    def _battle_seconds(self):raise AssertionError("legacy duration wrapper")
battle=Battle();battle._runtime=dict(bigworld=dict(time=native.clock));battle._config=dict(prebattleCountdownSeconds=10.,battleDurationSeconds=900.);battle.client=dict(authority_epoch=7);battle._start_message=dict(round_id=11);battle._bot_fire_seen={};battle._projectile_server_time_ms=0;battle._projectile_server_local_time=1.
battle._remote_factory=dict(_vehicles={1:dict(isStarted=True,health=500,position=[0.,0.,5.],devices_hp={},_destroyed_devices=[],_critical_devices=[],_crew_ko=[],is_on_fire=False,_ammo_rack_death=False)})
battle._records={'player:1':dict(kind='player',network_id=1,engine_id=1,ready=True,state=dict(x=0.,y=0.,z=5.,health=500,alive=True,equipment_states=[],critical_base_revision=4,critical_ack_seq=2))}
assert native.create(battle)
def forbidden(*args):raise AssertionError('Python hot business callback')
battle._clock=forbidden;battle._prebattle_seconds=forbidden;battle._battle_seconds=forbidden
for name in ('_projectile_direct_effect','_projectile_splash_effects','_record_alive','_launch_bot_projectile','_confirm_bot_projectile_launch','_server_entity','_projectile_record_poses'):setattr(battle,name,forbidden)
# A frozen first payload survives a transport failure and a descriptor change.
assert native.launch(dict(type='bot_state'))==[]
rows=native.launch(dict(type='bot_state',launches=[dict(id=8,fire_seq=1)]));assert rows[0][:2]==(8,1);assert native.launch_result(8,1,1);assert native.launch(None)==[]
random.seed(1513);rows=native.launch(dict(type='bot_state',launches=[dict(id=4,fire_seq=1),dict(id=4,fire_seq=2),dict(id=8,fire_seq=1)]));assert len(rows)==1 and rows[0][:2]==(4,1);first=copy.deepcopy(rows[0]);assert native.launch_result(4,1,0)
rows=native.launch(None);assert rows[0][:2]==(8,1);assert native.launch_result(8,1,1);assert native.launch(None)==[]
native.change();rows=native.launch(dict(type='bot_state',launches=[dict(id=4,fire_seq=1),dict(id=4,fire_seq=2),dict(id=8,fire_seq=1)]));assert rows[0]==first;assert native.launch_result(4,1,1);rows=native.launch(None);assert rows[0][:2]==(4,2) and rows[0][2][5][2]==110.;assert native.launch_result(4,2,1);rows=native.launch(None);assert rows[0][:2]==(8,1);assert native.launch_result(8,1,1);assert native.launch(None)==[]
row=dict(kind='bot_shot',round_id=11,authority_epoch=7,projectile_id='shell',shooter_kind='bot',shooter_id=4,source_vehicle='ussr:T-34',shot_seq=1,burst_group_seq=1,burst_index=0,burst_count=1,shell_index=0,origin=[0.,0.,0.],velocity=[0.,0.,100.],range_origin=[0.,0.,0.],segment_origin=[0.,0.,0.],segment_velocity=[0.,0.,100.],gravity=10.,max_distance=500.,max_time_ms=20000,is_he=False,splash_radius=0.,penetration_factor=1.,launch_server_time_ms=0,segment_start_time_ms=0,ricochet_count=0,base_penetration_multiplier=1.)
row['source_shot']=dict(speed=100.,gravity=10.,maxDistance=500.,piercingPower=[100.,80.],deadeye=False,shell=dict(kind='ARMOR_PIERCING',caliber=100.,damage=[200.,80.],explosionRadius=0.))
assert native.advance(battle,1.)==[]
assert native.event(row,1.);assert native.acks()==[[4,1]]
rows=native.advance(battle,1.1);terminal=[r for r in rows if r[1]=='resolve'];assert len(terminal)==1,rows
serial,kind,wire=terminal[0];assert wire['outcome']=='impact' and wire['hit_vehicle'];assert wire['direct']['target_id']==1 and wire['direct']['shot_result']==2 and wire['direct']['damage']>0,wire
work_names=('chords','scans','candidates','terminals');hit_stats=native.stats()
assert all(hit_stats[name]>0 for name in work_names),hit_stats
assert hit_stats['active']==0 and hit_stats['debt']==0.,hit_stats
hit_perf=dict(battle._projectile_perf)
assert set(hit_perf)==set(('active','chords','debt','advance','terminals','scans','candidates')),hit_perf
assert all(hit_perf[name]>0 for name in work_names),hit_perf
assert hit_perf['active']==1 and hit_perf['advance']>0. and hit_perf['debt']==0.,hit_perf
# Failed send does not rerun damage, and frozen terminal survives RNG changes.
assert native.ack(serial,0);random.seed(999);rows=native.advance(battle,1.2);retry=[r for r in rows if r[1]=='resolve'];assert retry[0][2]==wire
idle_stats=native.stats();assert all(idle_stats[name]==hit_stats[name] for name in work_names),idle_stats
idle_perf=battle._projectile_perf
assert all(idle_perf[name]==0 for name in work_names+('active','debt')),idle_perf
assert idle_perf['advance']>=0.,idle_perf
assert native.ack(retry[0][0],1);assert native.advance(battle,1.3)==[]
assert native.ingress(dict(round_id=11,authority_epoch=7,projectiles=[]),1.4)
assert native.event(row,1.4);assert native.advance(battle,1.5)==[]
# Canonical out of order launch confirmations advance only the contiguous prefix.
r3=copy.deepcopy(row);r3.update(projectile_id='third',shot_seq=3,burst_group_seq=3);assert native.event(r3,1.6);assert native.acks()==[[4,1]]
r2=copy.deepcopy(row);r2.update(projectile_id='second',shot_seq=2,burst_group_seq=2);assert native.event(r2,1.6);assert native.acks()==[[4,1],[4,2],[4,3]]
assert native.event(dict(kind='projectile_impact',projectile_id='third',round_id=11,authority_epoch=7),1.6)
assert native.event(dict(kind='projectile_impact',projectile_id='second',round_id=11,authority_epoch=7),1.6)
assert native.advance(battle,1.7)==[]
assert native.event(r3,1.8);assert native.advance(battle,1.9)==[]
assert native.observe(dict(server_time_ms=500,motion_time_us=123,authority_epoch=7),2.);assert battle._projectile_server_time_ms==500 and battle._pose_motion_time_us==123
native.stale()
try:native.advance(battle,3.)
except ValueError:pass
else:raise AssertionError('stale owner accepted')
native.clear()
restored=Battle();restored.__dict__.update(copy.deepcopy(battle.__dict__));restored._clock=lambda:1.;restored._prebattle_seconds=lambda:10.;restored._battle_seconds=lambda:900.;restored._projectile_server_time_ms=0;restored._projectile_server_local_time=1.;restored._bot_fire_seen={};restored._fixture_fire_sequence=1
restored._projectile_meta={'legacy':{}}
try:native.create(restored)
except ValueError:pass
else:raise AssertionError('legacy active owner silently replaced')
restored._projectile_meta={};restored._last_snapshot=dict(round_id=11,authority_epoch=7,server_time_ms=0,projectiles=[row])
assert native.create(restored);assert native.acks()==[]
rows=native.advance(restored,1.1);assert any(r[1]=='resolve' and r[2]['direct']['target_id']==1 for r in rows),rows
native.clear()
print('CPython 2.7 native projectile owner, damage, launch barrier, retry and epoch passed')
'''
@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'fixed gameplay source is required')
class NativeProjectilePython(unittest.TestCase):
    def test_total_owner(self):
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        names=['stock_native_projectile_wire','stock_native_projectile_owner','stock_native_projectile_cursor','stock_native_projectile_history','stock_native_projectile_chord','stock_native_projectile_scene','stock_native_projectile_random','stock_native_projectile_damage','stock_native_projectile_damage_python','stock_native_bot_descriptor','stock_native_bot_resource_lifecycle','stock_native_bot_armor_law','stock_native_bot_combat','stock_native_bot_turret_obstacles','stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin']
        with tempfile.TemporaryDirectory(prefix='native-projectile-python-') as folder:
            folder=Path(folder);binary=folder/'projectile_python_test.so';script=folder/'fixture.py';script.write_text(SCRIPT)
            facade=(ROOT/'bigworld/stock_native_projectile_python.cpp').read_text().replace('#include "stock_native_projectile_geometry.hpp"','#include "stock_native_projectile_python_stub.hpp"').replace('#include "stock_native_bot_services.hpp"','').replace('#include "stock_native_world_events.hpp"','')
            (folder/'facade.cpp').write_text(facade)
            command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'tests'),'-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native'),str(ROOT/'tests/stock_native_projectile_python_host.cpp'),str(folder/'facade.cpp')]+[str(ROOT/'bigworld'/(name+'.cpp')) for name in names]+[str(fork/'native/offline_destructible_geometry.cpp'),str(fork/'native/world_stage.cpp'),'-o',str(binary)]
            subprocess.run(command,check=True);subprocess.run([py27,'-B',str(script)],check=True)
if __name__=='__main__':unittest.main()
