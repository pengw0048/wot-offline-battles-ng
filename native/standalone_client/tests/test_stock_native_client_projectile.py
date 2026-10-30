"""Visible projectile transactions and exact protocol rounding; no VM claims."""
import ast
import math
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]


TRIGGER_FIXTURE = r'''
import copy,sys,types,random
import client_projectile_test as native
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
class Settings:
    CURRENT_SHELLS=1;NEXT_SHELLS=2;RELOAD_PARTIAL_CLIP=3
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
    module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
rules=types.ModuleType('gui.mods.offline_lan_0922.combat_rules');rules._OFFH_HE_TUNING_OVERRIDES={};sys.modules[rules.__name__]=rules
spotting=types.ModuleType('gui.mods.offline_lan_0922.spotting');spotting.SHOT_CAMOUFLAGE_SECONDS=.75;sys.modules[spotting.__name__]=spotting
source=dict(speed=100.,gravity=10.,maxDistance=500.,piercingPower=[100.,80.],deadeye=False,shell=dict(kind='ARMOR_PIERCING',caliber=100.,damage=[200.,80.],explosionRadius=0.))
def battle():
    b=Box(state='running',_battle_live=True,_replay_mode=False,_worker_mode=False,_battle_result=None,_drown_level=0,_overturn_level=0,_generation=1,_local_projectile_sequence=0,_local_player_burst=None,_local_fire_intent=None,_messages=[])
    b._fixture_actor=dict(health=500,alive=True,team=1,vehicle='ussr:T-34',effective_params=dict(gun=dict(shots=[dict(source_shot=source),dict(source_shot=source)])),critical={})
    b._fixture_descriptor=Box(activeTurretPosition=1,hull=Box(turretPositions=(Box(x=0.,y=2.,z=0.),Box(x=0.,y=3.,z=0.))),gun=Box(shots=[dict(shell=dict(effectsIndex=7)),dict(shell=dict(effectsIndex=8))]))
    b._runtime=Box(bigworld=Box(time=native.clock),constants=Box(VEHICLE_SETTING=Settings),vehicles=Box(g_cache=Box(shotEffects={7:dict(targetStickers=dict(armorResisted=11,armorPierced=12)),8:dict(targetStickers=dict(armorResisted=21,armorPierced=22))})))
    b._avatar=Box(_PlayerAvatar__ownVehicleStabMProv=Box(),gunRotator=Box(_VehicleGunRotator__turretYaw=0.,_VehicleGunRotator__gunPitch=0.,_VehicleGunRotator__dispersionAngles=(0.,)))
    b.client=Box(authority_epoch=7,minimum_rtt_ms=100.,rtt_ms=200.,_input_seq=0,ready=True,phase='battle')
    b._server=Box(vehicle_id=104);b._config=dict(perfect_accuracy=True);b._records={'player:4':{}};b._gun_state=Box()
    b._last_snapshot=dict(round_id=11,authority_epoch=7,server_time_ms=0,_client_dispatch_delay=.1)
    native.setup(b);return b
# A failed queue preserves ammunition and reload, while consuming the local
# sequence exactly like the source. A later native trigger can proceed.
b=battle();owner=native.create(b,1.);native.send_flag(False)
assert native.trigger(owner,b,1.)=='launch_send_failed'
assert native.gun()['ammo']==[7,8] and native.gun()['burst']==0 and b._local_fire_intent is None
native.send_flag(True);shot=native.trigger(owner,b,1.)
assert shot['projectile_id']=='11:p:4:2' and shot['source_shot']['deadeye'] is True
assert shot['origin']==[0.,3.5,1.] and shot['velocity']==[0.,0.,100.]
assert shot['launch_server_time_ms']==150 and shot['fire_input_seq']==2
assert native.gun()['ammo']==[6,8] and native.gun()['burst']==2 and native.gun()['ticks']==4
assert b._records['player:4']['shot_penalty_until']==1.75
assert native.observe(owner,b,dict(round_id=11,authority_epoch=7,motion_time_us=5000,_client_dispatch_delay=.2),1.4)
assert b._pose_motion_time_us==5000 and abs(b._pose_motion_local_time-1.15)<1.e-12
assert native.observe(owner,b,dict(round_id=11,authority_epoch=7,motion_time_us=5000,_client_dispatch_delay=.3),1.5)
assert abs(b._pose_motion_local_time-1.15)<1.e-12
assert native.observe(owner,b,dict(round_id=11,authority_epoch=7,motion_time_us=4000),1.6)
assert b._pose_motion_time_us==5000 and abs(b._pose_motion_local_time-1.15)<1.e-12
wire=b._messages[-1]
assert wire['type']=='projectile_launch' and 'projectile_id' not in wire and 'source_vehicle' not in wire
# The input UI's source queue remains discrete. Native final commit replays
# CURRENT/NEXT in order without rebinding any admitted shell or source shot.
b._local_player_burst['deferred_gun_pending_index']=None
b._local_player_burst['deferred_gun_settings']=[(1,1),(2,0)]
assert native.trigger(owner,b,1.1,1)['shell_index']==0
assert native.trigger(owner,b,1.2,1)['shell_index']==0
assert native.gun()['ammo']==[4,8] and native.gun()['shot']==1 and native.gun()['reload']==3.75
assert b._local_player_burst is None
# Canonical rows have no per-row round_id in the actual server schema.
canonical=copy.deepcopy(shot);canonical.pop('round_id');canonical['shooter_kind']='player'
assert isinstance(native.ingress(owner,b,dict(round_id=11,authority_epoch=7,projectiles=[canonical]),1.3),list)
assert not native.event(owner,b,dict(kind='projectile_impact',round_id=10,authority_epoch=7,projectile_id=shot['projectile_id']),1.3)
native.close(owner)
try:native.advance(owner,b,1.4)
except ValueError:pass
else:raise AssertionError('retired owner advanced')
# The native water gate contains only this trigger, and never consumes input.
b=battle();owner=native.create(b,1.);native.water_flag(True)
assert native.trigger(owner,b,1.)=='shot_ray_unavailable' and native.gun()['ticks']==1 and b.client._input_seq==0
native.water_flag(False);assert isinstance(native.trigger(owner,b,1.),dict)
# Re-entry after enqueue cannot change the accepted round's ammunition debit.
b=battle();owner=native.create(b,1.)
def reenter(message):
    assert b._local_fire_intent is not None
    b._local_player_burst['deferred_gun_pending_index']=None
    b._local_player_burst['deferred_gun_settings']=[(1,1)]
    native.close(owner)
b.client._during_enqueue=reenter
assert isinstance(native.trigger(owner,b,1.),dict) and native.gun()['ammo']==[6,8]
# A server launch rejection retires the native cursor and frozen proof as well
# as the Python presentation record. It cannot refund or resurrect the shot.
b=battle();owner=native.create(b,1.);shot=native.trigger(owner,b,1.);committed=native.gun()
assert native.metrics(owner)['retained_flights']==1
assert native.discard(owner,b,shot['projectile_id'])
assert not native.discard(owner,b,shot['projectile_id'])
assert native.metrics(owner)['retained_flights']==0 and native.metrics(owner)['retained_launches']==0
assert native.metrics(owner)['local_failures']==1 and native.gun()==committed
canonical=copy.deepcopy(shot);canonical.pop('round_id');canonical['shooter_kind']='player'
native.ingress(owner,b,dict(round_id=11,authority_epoch=7,projectiles=[canonical]),1.1)
native.advance(owner,b,1.2)
assert native.metrics(owner)['retained_flights']==0
assert not any(message['type']=='projectile_resolve'for message in b._messages)
native.close(owner)
# Execute the actual rejected-launch event through its staged discard seam.
# Presentation cleanup and burst cancellation remain the source's edge law.
import ast
tree=ast.parse(open(sys.argv[1]).read());runtime=next(node for node in tree.body if isinstance(node,ast.ClassDef)and node.name=='BattleRuntime')
runtime.body=[node for node in runtime.body if isinstance(node,ast.FunctionDef)and node.name in ('on_fire_intent_result','_discard_provisional_projectile')]
namespace={'__name__':'gui.mods.offline_lan_0922.battle_runtime'};exec compile(ast.Module(body=[runtime]),'staged-launch-rejection','exec')in namespace
for name in ('on_fire_intent_result','_discard_provisional_projectile'):setattr(Box,name,namespace['BattleRuntime'].__dict__[name])
b=battle();owner=native.create(b,1.);shot=native.trigger(owner,b,1.);ammo=native.gun()['ammo'];events=[]
facade=types.ModuleType('gui.mods.offline_lan_0922.native_client_projectile');facade.enabled=lambda battle:True;facade.discard=lambda battle,id:native.discard(owner,battle,id)
sys.modules[facade.__name__]=facade;sys.modules['gui.mods.offline_lan_0922'].native_client_projectile=facade
b._start_message={'round_id':11};b._projectile_meta={shot['projectile_id']:shot};b._projectile_terminal_data={};b._projectiles=None;b._projectile_visual_terminals=set()
b._owns_projectile=lambda meta:meta['shooter_id']==4
b._report_projectile_terminal_failure=lambda *args:events.append('failure')
b._stop_projectile_visual=lambda *args:events.append('visual_stop')
b._cancel_local_player_burst=lambda apply_settings:native.cancel(owner,b,1.,int(apply_settings))
message=dict(round_id=11,accepted=False,projectile_id=shot['projectile_id'],intent_seq=shot['fire_intent_seq'],reason='server_rejected')
assert b.on_fire_intent_result(message)and events==['failure','visual_stop']
assert not b.on_fire_intent_result(message)
assert native.metrics(owner)['retained_flights']==0 and native.metrics(owner)['retained_launches']==0
assert native.metrics(owner)['local_failures']==1 and native.gun()['ammo']==ammo
assert b._projectile_meta=={}and shot['projectile_id']in b._projectile_visual_terminals
native.close(owner)
print('CPython 2.7 visible native trigger, two advances, queue failure, deferred burst and reentry passed')
'''


CANONICAL_META_FIXTURE = r'''
import ast,copy,math,sys,types
root,glue,runtime=sys.argv[1:4]
for name,path in (('gui',root+'/gui'),('gui.mods',root+'/gui/mods'),('gui.mods.offline_lan_0922',root+'/gui/mods/offline_lan_0922')):
    module=types.ModuleType(name);module.__path__=[path];sys.modules[name]=module
from gui.mods.offline_lan_0922 import lan_client,combat_rules
tree=ast.parse(open(runtime).read())
fields=next(node for node in tree.body if isinstance(node,ast.Assign) and node.targets[0].id=='_PROJECTILE_IMMUTABLE_FIELDS')
cls=next(node for node in tree.body if isinstance(node,ast.ClassDef) and node.name=='BattleRuntime')
cls.body=[node for node in cls.body if isinstance(node,ast.FunctionDef) and node.name in ('_projectile_wire_meta','_projectile_launch_mismatches')]
namespace=dict(lan_protocol=lan_client,combat_rules=combat_rules,math=math,_INTEGER_TYPES=(int,long),PROJECTILE_MAX_TIME_MS=20000)
exec compile(ast.Module(body=[fields,cls]),'staged-projectile-meta','exec') in namespace
Runtime=namespace['BattleRuntime']
rows=[]
native=types.ModuleType('offline_client_projectile_native')
native.now=lambda battle:1.
native.ingress=lambda owner,battle,message,now:[copy.deepcopy(row) for row in rows]
sys.modules[native.__name__]=native
for name in ('offline_client_weapon_runtime_native','offline_client_transport_native'):sys.modules[name]=types.ModuleType(name)
glue_module=types.ModuleType('gui.mods.offline_lan_0922.native_client_projectile');glue_module.__package__='gui.mods.offline_lan_0922'
exec compile(open(glue,'rb').read(),glue,'exec') in glue_module.__dict__
class Battle(Runtime):
    def __init__(self):
        self._native_client_projectile=object();self._projectile_epoch=7;self._projectile_meta={};self.visuals=[];self.warnings=[]
    def _ensure_projectile_visual(self,meta,now):self.visuals.append(meta)
    def _warn_optional_failure(self,label,error,disable=True):self.warnings.append(str(error))
source=dict(speed=100.,gravity=10.,maxDistance=500.,piercingPower=[100.,80.],deadeye=False,shell=dict(kind='ARMOR_PIERCING',caliber=100.,damage=[200.,80.],explosionRadius=0.))
row=dict(projectile_id='1:b:11:1',shooter_kind='bot',shooter_id=11,source_vehicle='ussr:T-34',source_shot=source,shot_seq=1,
         burst_group_seq=1,burst_index=0,burst_count=1,shell_index=0,origin=[1.,2.,3.],velocity=[0.,0.,100.],range_origin=[1.,2.,3.],
         gravity=10.,maxDistance=500.,max_time_ms=6000,is_he=False,splash_radius=0.,penetration_factor=1.,launch_server_time_ms=1160,
         segment_origin=[1.,2.,3.],segment_velocity=[0.,0.,100.],segment_start_time_ms=0,ricochet_count=0,
         base_penetration_multiplier=1.,bot_presentation_time_us=90431779)
rows.append(row)
# The ordered shot echo normalizes the same launch; it must not see a change.
battle=Battle();assert glue_module.ingress(battle,dict(projectiles=[row]))
meta=battle._projectile_meta[row['projectile_id']];echo=Runtime._projectile_wire_meta(row)
assert echo is not None and Runtime._projectile_launch_mismatches(meta,echo)==(),Runtime._projectile_launch_mismatches(meta,echo)
assert battle.visuals==[meta] and meta['max_distance']==500. and meta['bot_presentation_time_us']==90431779
assert meta['manager_key']==row['projectile_id'] and meta['destructibles_pending']==[] and not battle.warnings
# A row the runtime cannot normalize is reported and never installed.
battle=Battle();del rows[0]['source_shot'];assert glue_module.ingress(battle,dict(projectiles=[]))
assert battle._projectile_meta=={} and battle.visuals==[] and len(battle.warnings)==1
print('native projectile metadata matches the runtime canonical launch')
'''

@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'Pinned gameplay source is required')
class NativeClientProjectile(unittest.TestCase):
    def test_lifecycle_and_wire_rounding(self):
        fork = Path(os.environ['WOT_FORK_0101_SOURCE'])
        names = ['stock_native_client_projectile', 'stock_native_client_weapon',
                 'stock_native_projectile_owner', 'stock_native_projectile_cursor',
                 'stock_native_projectile_history', 'stock_native_projectile_chord',
                 'stock_native_projectile_damage', 'stock_native_bot_armor_law',
                 'stock_native_bot_combat']
        with tempfile.TemporaryDirectory(prefix='client-projectile-') as folder:
            binary = Path(folder) / 'lifecycle'
            command = ['c++', '-std=c++17', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-I'+str(ROOT/'bigworld'), '-I'+str(fork/'native'),
                       str(ROOT/'tests/test_stock_native_client_projectile.cpp')]
            command += [str(ROOT/'bigworld'/(name+'.cpp')) for name in names]+['-o', str(binary)]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)
            # The actual pinned producer uses exact as_integer_ratio ties-even,
            # rather than Python 2 round or nearby C++ round(value*1e6).
            source = fork/'src/res/scripts/client/gui/mods/offline_lan_0922/lan_client.py'
            tree = ast.parse(source.read_text())
            node = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == '_projectile_wire_round')
            namespace = {'math': math}
            exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), 'exec'), namespace)
            oracle = namespace['_projectile_wire_round']
            rng = random.Random(1513)
            values = [0., -0., .0000005, -.0000005, 1.2345675, -1.2345675]
            values += [rng.uniform(-1.e9, 1.e9) for _ in range(1500)]
            values += [math.ldexp(rng.uniform(-1., 1.), exponent) for exponent in range(-70, 30) for _ in range(8)]
            result = subprocess.check_output([str(binary), 'round'], input='\n'.join(map(repr, values))+'\n', text=True)
            self.assertEqual([float(v) for v in result.splitlines()], [oracle(v) for v in values])
            import importlib.util
            import types
            path = fork/'src/res/scripts/client/gui/mods/offline_lan_0922/shot_geometry.py'
            spec = importlib.util.spec_from_file_location('barrel_geometry', path)
            geometry = importlib.util.module_from_spec(spec);spec.loader.exec_module(geometry)
            cases=[];expected=[]
            for i in range(240):
                point=tuple(rng.uniform(-100.,100.) for _ in range(3));attitude=tuple(rng.uniform(-3.,3.) for _ in range(3))
                turret=rng.uniform(-3.,3.);pitch=rng.uniform(-1.,1.)
                hull=tuple(rng.uniform(-2.,2.) for _ in range(3));mount=tuple(rng.uniform(-2.,2.) for _ in range(3));gun=tuple(rng.uniform(-2.,2.) for _ in range(3));length=rng.uniform(.1,8.)
                descriptor=types.SimpleNamespace(activeTurretPosition=1,chassis=dict(hullPosition=hull),hull=dict(turretPositions=[(999.,999.,999.),mount]),turret=dict(gunPosition=gun),gun=dict(hitTester=dict(bbox=[(-1.,-1.,-1.),(1.,1.,length)])))
                expected.append(geometry.barrel_world_point(descriptor,point,*attitude,turret,pitch))
                cases.append(' '.join(map(repr,(*point,*attitude,turret,pitch,*hull,*mount,*gun,length))))
            actual=subprocess.check_output([str(binary),'barrel'],input='\n'.join(cases)+'\n',text=True)
            for row,reference in zip(actual.splitlines(),expected):
                self.assertTrue(all(math.isclose(float(v),r,abs_tol=1.e-12,rel_tol=1.e-12) for v,r in zip(row.split(),reference)))


    def test_native_binding_and_world_syntax(self):
        fork = Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-projectile-syntax-') as folder:
            for name in ('stock_native_client_projectile_python', 'stock_native_client_projectile_world', 'stock_native_client_projectile_state'):
                source = (ROOT/'bigworld'/(name+'.cpp')).read_text()
                source = source.replace('#include "pch.hpp"', '#include "stock_native_client_projectile_syntax_stub.hpp"')
                for include in ('stock_native_bot_vehicle_geometry.hpp', 'stock_native_bot_world_adapter.hpp', 'stock_native_projectile_geometry.hpp', 'stock_native_client_drive_runtime.hpp'):
                    source = source.replace('#include "'+include+'"', '')
                path = Path(folder)/(name+'.cpp');path.write_text(source)
                subprocess.run(['c++', '-std=c++17', '-Wno-register', '-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-isystem', str(Path(prefix)/'include/python2.7'), '-I'+str(ROOT/'tests'), '-I'+str(ROOT/'bigworld'), '-I'+str(fork/'native'), str(path)], check=True)

    def test_python27_trigger_transaction(self):
        fork = Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        names = ['stock_native_client_projectile', 'stock_native_client_weapon', 'stock_native_client_state',
                 'stock_native_client_drive_status', 'stock_native_projectile_wire', 'stock_native_projectile_owner',
                 'stock_native_projectile_cursor', 'stock_native_projectile_history', 'stock_native_projectile_chord',
                 'stock_native_projectile_random', 'stock_native_projectile_damage', 'stock_native_projectile_damage_python',
                 'stock_native_bot_descriptor', 'stock_native_bot_resource_lifecycle', 'stock_native_bot_armor_law',
                 'stock_native_bot_combat', 'stock_native_world_pipeline', 'stock_motion_rotation_batch', 'stock_offline_query_skin']
        with tempfile.TemporaryDirectory(prefix='client-projectile-capi-') as folder:
            folder = Path(folder)
            source = (ROOT/'bigworld/stock_native_client_projectile_python.cpp').read_text()
            source = source.replace('#include "pch.hpp"', '#include "stock_native_client_projectile_syntax_stub.hpp"')
            for name in ('stock_native_bot_vehicle_geometry.hpp', 'stock_native_bot_world_adapter.hpp', 'stock_native_client_drive_runtime.hpp'):
                source = source.replace('#include "'+name+'"', '')
            (folder/'facade.cpp').write_text(source)
            command = ['c++', '-std=c++17', '-O0', '-ffp-contract=off', '-Wno-register', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                       '-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-bundle', '-undefined', 'dynamic_lookup', '-isystem',
                       str(Path(prefix)/'include/python2.7'), '-I'+str(ROOT/'tests'), '-I'+str(ROOT/'bigworld'), '-I'+str(fork/'native'),
                       str(folder/'facade.cpp'), str(ROOT/'tests/stock_native_client_projectile_python_host.cpp')]
            command += [str(ROOT/'bigworld'/(name+'.cpp')) for name in names]
            command += [str(fork/'native/offline_destructible_geometry.cpp'), str(fork/'native/world_stage.cpp'), '-o', str(folder/'client_projectile_test.so')]
            subprocess.run(command, check=True)
            script = folder/'fixture.py';script.write_text(TRIGGER_FIXTURE)
            import importlib.util
            spec=importlib.util.spec_from_file_location('projectile_rejection_bindings',ROOT/'bigworld/stock_native_client_projectile_bindings.py')
            binding=importlib.util.module_from_spec(spec);spec.loader.exec_module(binding)
            staged=folder/'battle_runtime.py';staged.write_text(binding.battle((fork/'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py').read_text()))
            subprocess.run([py27, '-B', str(script),str(staged)], check=True)

    def test_native_projectile_meta_matches_canonical_launch(self):
        fork = Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        client = fork/'src/res/scripts/client'
        subprocess.run([py27, '-B', '-c', CANONICAL_META_FIXTURE, str(client), str(ROOT/'bigworld/stock_native_client_projectile.py'),
                        str(client/'gui/mods/offline_lan_0922/battle_runtime.py')], check=True)

    def test_pinned_staged_seams_parse_python27(self):
        import importlib.util
        path = ROOT/'bigworld/stock_native_client_projectile_bindings.py'
        spec = importlib.util.spec_from_file_location('projectile_bindings', path)
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        source = Path(os.environ['WOT_FORK_0101_SOURCE'])/'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
        staged = module.battle(source.read_text())
        self.assertEqual(staged.count('return native_client_projectile.frame('), 1)
        self.assertEqual(staged.count('return native_client_projectile.shoot('), 1)
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], env=dict(os.environ, PYENV_VERSION='2.7.18'), text=True).strip()
        subprocess.run([py27, '-B', '-c', 'import sys;compile(sys.stdin.read(),"staged-projectile","exec")'], input=staged, text=True, check=True)
        subprocess.run([py27, '-B', '-c', 'import sys;compile(sys.stdin.read(),"projectile-facade","exec")'], input=(ROOT/'bigworld/stock_native_client_projectile.py').read_text(), text=True, check=True)


if __name__ == '__main__':
    unittest.main()
