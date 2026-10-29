"""Full original player-clock/HUD differential and no-business-callback traps."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
from __future__ import print_function
import ast,copy,math,random,sys,types
source=sys.argv[1]+'/src/res/scripts/client';sys.path.insert(0,source)
for name in ('gui','gui.mods'):
 module=types.ModuleType(name);module.__path__=[source+'/'+name.replace('.','/')];sys.modules[name]=module
from gui.mods.offline_lan_0922 import gun_mechanics as original,critical_damage,device_damage,crew_battle,effective_params,equipment_mechanics
import weapon_runtime_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
def field(o,key,default=None):return o.get(key,default) if isinstance(o,dict) else getattr(o,key,default)
methods=set(('_advance_local_gun_to','_rescale_current_reload','_crew_stat_factor','_local_stat_factor','_publish_ammo_state','_present_equipments','_equipment_echo','_equipment_stages','_tick_equipment_cooldowns','_publish_reload_event','_publish_targeting_info'))
parsed=ast.parse(open(source+'/gui/mods/offline_lan_0922/battle_runtime.py').read())
cls=next(n for n in parsed.body if isinstance(n,ast.ClassDef) and n.name=='BattleRuntime');cls.body=[n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in methods];module=ast.Module(body=[cls]);ast.fix_missing_locations(module)
ns=dict(__name__='gun_runtime_oracle',critical_damage=critical_damage,crew_battle=crew_battle,equipment_mechanics=O(),gun_mechanics=original,math=math,_field=field,_number=lambda n,default=0.:float(default if n is None else n))
exec(compile(module,'pinned-full-gun-chain','exec'),ns)
def descriptor(clip):
 return O(maxHealth=100,type=O(crewRoles=(('commander','gunner'),('loader',),('loader','gunner'))),maxAmmo=45,activeGunShotIndex=0,miscAttrs={},
  gun=O(maxHealth=200,maxRegenHealth=100,rotationSpeed=.65,shots=[dict(shell=dict(compactDescr=n)) for n in (2,3,4)],shotDispersionAngle=.12,shotDispersionFactors=dict(afterShot=1.6,afterShotInBurst=.7,turretRotation=.15),burst=(1,0.),aimingTime=2.1,reloadTime=3.2,clip=(clip,.3)),
  chassis=O(shotDispersionFactors=(.13,.11),maxHealth=200,maxRegenHealth=100),hull=O(ammoBayHealth=(200,100)),turret=O(rotationSpeed=.7,turretRotatorHealth=(200,100),surveyingDeviceHealth=(200,100)),engine=O(maxHealth=200,maxRegenHealth=100),radio=O(maxHealth=200,maxRegenHealth=100))
loadout=dict(crew_multiplier=.95,crew_factor=1.1,gun_rotation_factor=.85,dispersion_factor=.8,aim_time_factor=.9,reload_factor=.85,bloom_move_factor=.96,bloom_rotation_factor=.83,bloom_turret_factor=.925)
crew=('commander','loader1','loader2')
states={}
for mask in range(8):
 for fire in (0,1):states[str(mask)+':'+str(fire)]=dict(battle_factors=dict(adrenaline_health_fraction=.2,adrenaline_reload_factor=.76+mask*.01,engine_fire_factor=1.,damaged_gun_factor=.55+fire*.1))
effective=dict(crew=dict(dynamic_spotting=dict(crew=list(crew),states=states),members=[dict(instance=name,roles=['loader' if 'loader' in name else 'commander'],skills=[dict(name='gunner_sniper' if name=='commander' else 'loader_intuition',level=100.,active=True,enabled=True)]) for name in crew]))
fields=('reload_time','reload_duration','dispersion','clip','shot_index','load_started','ammo','pending_index')
def close(a,b,path):
 if isinstance(a,(int,long,float)) and not isinstance(a,bool) and isinstance(b,(int,long,float)):
  assert abs(a-b)<=1.e-11*max(1.,abs(a)),(path,a,b)
 elif isinstance(a,(tuple,list)):
  assert len(a)==len(b),(path,a,b)
  for i,(x,y) in enumerate(zip(a,b)):close(x,y,path+'/'+str(i))
 else:assert a==b,(path,a,b)
rng=random.Random(1513);checks=0
for clip in (1,3,10):
 d=descriptor(clip);a=original.GunState(d,loadout,{2:100,3:100,4:100});b=native.create(d,loadout,{2:100,3:100,4:100});runtime=native.runtime(b,d)
 entity=O(typeDescriptor=d,maxHealth=100,health=100,_crew_ko=set(),_crew_impaired=set(),is_on_fire=False,devices_hp={},_destroyed_devices=set(),_critical_devices=set(),_offlineStunFactors={},maxima={key:200. for key in ('gunHealth','ammoBayHealth','turretRotatorHealth','engineHealth','surveyingDeviceHealth')},getAimParams=lambda:(.13,-.17))
 old=ns['BattleRuntime']();old._gun_state=a;old._gun_last_tick=None;old._worker_mode=False;old._replay_mode=False;old._replay_publishing=False;old._battle_live=False;old._client_ready_received=False;old._ammo_signature=None;old._equipment_signature=None;old._reload_event=None;old._targeting_signature=None;old._local_effective_params=effective;old._server=O(vehicle_id=11);old._server_entity=lambda id:entity;old._report_crew_penalty=lambda e:None;old._run_optional_feature=lambda *args:None;old._ensure_live_reload_sound=lambda:None
 old._runtime=O(constants=O(VEHICLE_SETTING=O(CURRENT_SHELLS=3),EQUIPMENT_STAGES=O(READY=7,COOLDOWN=8,EXHAUSTED=9)))
 events=[]
 old._avatar=O(updateVehicleAmmo=lambda vehicle,compact,q,stage,remaining:events.append(('equipment' if compact>=100 else 'ammo',(compact,q,stage,remaining))),updateVehicleSetting=lambda vehicle,setting,shell:events.append(('shell',shell)),updateVehicleGunReloadTime=lambda vehicle,remaining,duration:events.append(('reload',(remaining,duration))),updateTargetingInfo=lambda *args:events.append(('targeting',args[2:])))
 now=0.;equipment=[O(contract=dict(compactDescr=101,kind='repairkit'),uses_left=1,active=False,ready_at=0.),O(contract=dict(compactDescr=102,kind='rpm_limiter'),uses_left=-1,active=False,ready_at=0.)];old._equipment_state=equipment;old._clock=lambda:now
 for i in range(700):
  now+=rng.choice((0.,.001,.016,.033,.1,.9,3.1));old._battle_live=5<=i<650;old._client_ready_received=i>=4
  old._local_speed=rng.uniform(-20.,20.);old._local_turn_speed=rng.uniform(-1.5,1.5)
  mask=(i//9)%8;entity._crew_ko=set(name for index,name in enumerate(crew) if mask&(1<<index));critical_damage._recompute_crew_impaired(entity)
  entity.health=(0.,1.,19.999999,20.,21.,100.)[(i//5)%6];entity.is_on_fire=bool((i//13)%2)
  entity.devices_hp={'gunHealth':(200.,100.,50.,0.)[(i//7)%4],'ammoBayHealth':(200.,80.,0.)[(i//11)%3],'turretRotatorHealth':(200.,100.,0.)[(i//17)%3]}
  entity._destroyed_devices=set(key for key,value in entity.devices_hp.items() if value==0.);entity._critical_devices=set(key for key,value in entity.devices_hp.items() if 0.<value<=100.)
  entity._offlineStunFactors={} if (i//19)%2==0 else dict(reload=1.25,dispersion=1.1,aim_time=1.3,turret_speed=.7,bloom_move=1.2,bloom_rotation=1.4,bloom_turret=1.1)
  if i%17==0:
   factor=old._local_stat_factor(entity,'reload');assert a.commit_fire(factor)==b.commit_fire(factor)
  if i%41==0:assert a.request_shell_index((i//41)%3)==b.request_shell_index((i//41)%3)
  if i%89==0:assert a.reload_partial_clip()==b.reload_partial_clip()
  if i%31==0:equipment[0].ready_at=now+2.3
  equipment[0].uses_left=0 if (i//37)%3==2 else 1;equipment[1].active=bool((i//23)%2)
  equipment[0].contract['bonusValue']=[-.2,0.,.1,.25][i%4];equipment[0].contract['kind']='fuel' if i%3 else 'repairkit';equipment[0].contract['enginePowerFactor']=[-1.,0.,1.,1.12,1.5][i%5];equipment[1].contract['enginePowerFactor']=[0.,1.,1.1,1.35][i%4]
  events[:]=[];old._advance_local_gun_to(entity,now)
  result=native.advance_runtime(runtime,now,int(old._battle_live),old._local_speed,old._local_turn_speed,entity,effective,equipment,int(old._client_ready_received))
  actual=[]
  for e in equipment:
   item=object.__new__(equipment_mechanics.EquipmentState);item.contract=e.contract;item.uses_left=e.uses_left;item.active=e.active;actual.append(item)
  close(result['engine_power'],max(0.,equipment_mechanics.passive_effects(actual)['enginePowerFactor']),'engine_power')
  close(result['repair_bonus'],max(0.,equipment_mechanics.passive_effects(actual)['repairkitBonusValue']),'repair_bonus')
  for name in fields:close(getattr(a,name),getattr(b,name),str(i)+'/'+name)
  expected=[]
  for kind,value in events:
   if kind=='ammo':expected.append((kind,value[:3]))
   else:expected.append((kind,value))
  actual=[('ammo',value) for value in result['ammo']]
  if result['shell'] is not None:actual.append(('shell',result['shell']))
  actual += [('equipment',value) for value in result['equipment']]
  if result['reload'] is not None:actual.append(('reload',result['reload'][:2]))
  if result['targeting'] is not None:actual.append(('targeting',result['targeting']))
  close(expected,actual,'events/'+str(i))
  expected_factors=(old._local_stat_factor(entity,'dispersion'),critical_damage.stat_factor(entity,'aim_time'),old._local_stat_factor(entity,'reload'),critical_damage.stat_factor(entity,'turret_speed'),entity._offlineStunFactors.get('turret_speed',1.),entity._offlineStunFactors.get('bloom_move',1.),entity._offlineStunFactors.get('bloom_rotation',1.),entity._offlineStunFactors.get('bloom_turret',1.))
  close(expected_factors,result['factors'],'factors/'+str(i))
  for skill in ('gunner_sniper','GUNNER_SNIPER','loader_intuition','missing_skill'):
   assert native.skills(effective,skill,entity._crew_ko)==effective_params.living_skill_count(effective,skill,entity._crew_ko)
  checks+=1
 # Once running, all legacy business helpers may be trapped without changing
 # native advancement. The actual stock HUD methods remain presentation-only.
 def bomb(*args,**kw):raise AssertionError('Python player business callback')
 for name in ('tick','can_fire','commit_fire'):setattr(original.GunState,name,bomb)
 critical_damage.stat_factor=bomb
 native.advance_runtime(runtime,now+.1,1,3.,.2,entity,effective,equipment,1)
 # Restore the original oracle before constructing the next independent case.
 reload(original);reload(critical_damage)
for bad in ('', 'gunner sniper', 'gunner-sniper', 'x'*65):
 try:native.skills(effective,bad,set())
 except RuntimeError:pass
 else:raise AssertionError('invalid skill name accepted: '+bad)
try:native.skills(effective,'gunner_sniper',set(['not_a_member']))
except RuntimeError:pass
else:raise AssertionError('unknown physical casualty accepted')
invalid=copy.deepcopy(effective);invalid['crew']['members'][0]['skills'][0]['active']=1
assert native.skills(invalid,'gunner_sniper',set())==effective_params.living_skill_count(invalid,'gunner_sniper',set())==0
# Original audio policy stays at discrete edges; the C++ deadline gate
# dispatches its pump only when work is due, preserving all audible effects.
from gui.mods.offline_lan_0922.replay_sound import ReplayReloadSound
class Sound(object):
 def __init__(self,backend,name):self.backend,self.name=backend,name
 def play(self):self.backend.events.append(('play',self.name))
 def stop(self):self.backend.events.append(('stop',self.name))
class Backend(object):
 def __init__(self):self.events=[]
 def enabled(self):return True
 def event(self,name):self.events.append(('event',name))
 def handle(self,name):return Sound(self,name)
 def caliber(self,name):self.events.append(('caliber',name))
desc=O(duration=1.2,shellDuration=.2,shellDt=.15,shellDtLast=.1,soundEvent='short',startLong='intro',startLoop='loop_start',stopLoop='loop_stop',loopShell='shell',loopShellLast='last_shell',lastShellAlert='last_alert',caliber='medium')
settings=O(clip=O(size=3));left=Backend();right=Backend();a=ReplayReloadSound(O(_desc=desc),settings,backend=left);b=ReplayReloadSound(O(_desc=desc),settings,backend=right)
now=0.
for i in range(600):
 now+=(.4 if i%53==0 else .033)
 if i%13==0:
  remaining=0. if i%39==0 else 1.5 if i%26==0 else .4
  gun=dict(reload_time=remaining,reload_duration=1.5,clip=0 if i%26==0 else 1,shot_index=0,ammo=[30-i//26])
  publication=(remaining,1.5,'complete' if not remaining else 'start' if i%26==0 else 'update')
  a.on_edge(publication,gun,now,now);b.on_edge(publication,gun,now,now)
 a.pump(now)
 if native.audio_due(b.queue[0][0] if b.queue else None,now):b.pump(now)
 assert a.queue==b.queue and left.events==right.events,(i,a.queue,b.queue,left.events,right.events)
assert native.audio_due(1.,1.-1.e-9) and not native.audio_due(None,2.)
print('native full local gun clock/HUD/conditional crew differential passed: %d edges'%checks)
'''


class NativeClientWeaponRuntimeTests(unittest.TestCase):
    def test_pinned_factory_and_hot_seams(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        spec = importlib.util.spec_from_file_location('weapon_runtime_bindings', ROOT / 'bigworld/stock_native_client_weapon_runtime_bindings.py')
        bindings = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bindings)
        path = Path(source) / 'src/res/scripts/client/gui/mods/offline_lan_0922/battle_runtime.py'
        prepared = bindings.battle(path.read_text())
        compile(prepared, str(path), 'exec')
        self.assertNotIn('gun_mechanics.GunState(', prepared)
        self.assertEqual(prepared.count("__import__('offline_client_weapon_native').create("), 2)
        self.assertIn('native_client_weapon_runtime.advance(self, entity, now)', prepared)

    def test_cold_startup_and_live_callback_traps(self):
        native = types.ModuleType('offline_client_weapon_runtime_native')
        calls = []
        native.advance = lambda *args: calls.append(('advance', args)) or 'native-gun'
        native.publish = lambda *args: calls.append(('publish', args)) or True
        with mock.patch.dict(sys.modules, {'offline_client_weapon_runtime_native': native}):
            spec = importlib.util.spec_from_file_location('weapon_runtime_facade', ROOT / 'bigworld/stock_native_client_weapon_runtime.py')
            facade = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(facade)
        battle = types.SimpleNamespace(_worker_mode=False, _replay_mode=False, _replay_publishing=False, state='loading', _clock=lambda: 3.)
        self.assertIsNone(facade.advance(battle, object()))
        self.assertIsNone(facade.publish(battle, 'targeting'))
        self.assertEqual(calls, [])
        battle.state = 'running'
        self.assertEqual(facade.advance(battle, object()), 'native-gun')
        self.assertTrue(facade.publish(battle, 'equipment'))
        self.assertEqual([kind for kind, unused in calls], ['advance', 'publish'])

    def test_presentation_failure_and_teardown_are_local(self):
        native = types.ModuleType('offline_client_weapon_runtime_native')
        with mock.patch.dict(sys.modules, {'offline_client_weapon_runtime_native': native}):
            spec = importlib.util.spec_from_file_location('weapon_runtime_publisher', ROOT / 'bigworld/stock_native_client_weapon_runtime.py')
            facade = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(facade)
        token = object()
        calls = []
        battle = types.SimpleNamespace(_native_client_weapon_runtime=token,
            state='running', _server=types.SimpleNamespace(vehicle_id=11),
            _replay_mode=False, _warn_optional_failure=lambda *a, **kw: calls.append('failure'))
        def ammo(vehicle, compact, *unused):
            calls.append(compact)
            if compact == 2:
                raise RuntimeError('one stock ammo slot failed')
        battle._avatar = types.SimpleNamespace(updateVehicleAmmo=ammo)
        edges = [dict(id='ammo:2', kind='ammo', compact=2, quantity=3, clip=1),
                 dict(id='ammo:3', kind='ammo', compact=3, quantity=4, clip=0)]
        self.assertEqual(facade._publish(battle, None, edges), ['ammo:3'])
        self.assertEqual(calls, [2, 'failure', 3])
        def retire(*unused):
            battle._native_client_weapon_runtime = None
        calls.clear()
        battle._native_client_weapon_runtime = token
        battle._avatar = types.SimpleNamespace(updateVehicleGunReloadTime=retire)
        battle._ensure_live_reload_sound = lambda: self.fail('audio ran after teardown')
        reload = dict(id='reload', kind='reload', remaining=2., duration=3.,
                      reason='start', now=1., live=True, gun={})
        self.assertEqual(facade._publish(battle, None, [reload]), [])
        self.assertFalse(hasattr(battle, '_reload_event'))
        battle._native_client_weapon_runtime = token
        entity = types.SimpleNamespace(getAimParams=lambda: (retire() or .1, .2))
        battle._avatar = types.SimpleNamespace(updateTargetingInfo=lambda *a: self.fail('targeting ran after teardown'))
        self.assertEqual(facade._publish(battle, entity,
            [dict(id='targeting', kind='targeting', signature=(1.,) * 7)]), [])

    def test_full_original_clock_and_hud(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True, env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-weapon-runtime-') as directory:
            folder = Path(directory)
            (folder / 'fixture.py').write_text(FIXTURE)
            files = ('stock_native_client_weapon.cpp', 'stock_native_client_weapon_python.cpp', 'stock_native_client_weapon_runtime.cpp', 'stock_native_client_state.cpp', 'stock_native_bot_combat.cpp', 'stock_native_projectile_random.cpp')
            command = ['c++', '-std=c++17', '-O1', '-DWOT_CLIENT_WEAPON_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld')]
            command += [str(ROOT / 'bigworld' / name) for name in files]
            command += [str(ROOT / 'tests/stock_native_client_weapon_runtime_host.cpp'), '-o', str(folder / 'weapon_runtime_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([py27, '-B', str(folder / 'fixture.py'), source], cwd=str(folder), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
