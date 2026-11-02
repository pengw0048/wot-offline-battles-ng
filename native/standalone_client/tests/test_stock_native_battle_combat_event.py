"""Optional hit effects cannot drop confirmed combat credit or health transitions."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')
FIXTURE = r'''
import battle_combat_event_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Entity(O):
 def __getattribute__(self,name):
  if name=='isStarted' and object.__getattribute__(self,'broken_sticker'):
   raise RuntimeError('native sticker entity became unavailable')
  return object.__getattribute__(self,name)
class Types(object):
 DAMAGE=1;CRIT=2;KILL=3;RECEIVED_DAMAGE=4;TANKING=5;RECEIVED_CRIT=6
 @staticmethod
 def packDamage(amount,*args):return amount
 @staticmethod
 def packCrits(count,*args):return count
class VehicleMarkerPlugin(object):
 def __init__(self,provider):
  self.sessionProvider=provider;self.__playerVehicleID=91
 def __getVehicleDamageType(self,info):return 7 if info.vehicleID==self.__playerVehicleID else 8
flags='ATTACK_IS_EXTERNAL_EXPLOSION ATTACK_IS_DIRECT_PROJECTILE MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_EXPLOSION MATERIAL_WITH_POSITIVE_DF_NOT_PIERCED_BY_PROJECTILE MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE RICOCHET DEVICE_PIERCED_BY_EXPLOSION DEVICE_PIERCED_BY_PROJECTILE DEVICE_DAMAGED_BY_EXPLOSION DEVICE_DAMAGED_BY_PROJECTILE CHASSIS_DAMAGED_BY_EXPLOSION CHASSIS_DAMAGED_BY_PROJECTILE GUN_DAMAGED_BY_EXPLOSION GUN_DAMAGED_BY_PROJECTILE FIRE_STARTED VEHICLE_KILLED'.split()
for incoming in (0,1):
 for failing in ('sticker','descriptor','callback','shot_result','battle_events',None):
  rows,shots,health,warnings,hits=[],[],[],[],[]
  attempted_rows,attempted_shots=[],[]
  dp=O(getPlayerVehicleID=lambda cached:91,getVehicleInfo=lambda identity:O(vehicleID=identity))
  provider=O(getArenaDP=lambda:dp,setVehicleHealth=lambda *args:None,shared=O(feedback=object()))
  def battle_events(items):
   attempted_rows.append(items)
   if failing=='battle_events' and len(attempted_rows)==1:raise RuntimeError('stock battle events callback failed')
   rows.extend(items)
  def shot_results(items):
   attempted_shots.append(items)
   if failing=='shot_result' and len(attempted_shots)==1:raise RuntimeError('stock shot results callback failed')
   shots.extend(items)
  avatar=O(playerVehicleID=91,guiSessionProvider=provider,onBattleEvents=battle_events,showShotResults=shot_results)
  entities=dict((identity,Entity(health=500,broken_sticker=failing=='sticker',typeDescriptor=O(gun=O(shots=[{'shell':{'kind':'ARMOR_PIERCING'}}])))) for identity in (91,92))
  def hit(event):
   hits.append(event['health'])
   if event.get('fail'):
    if failing=='descriptor':raise ValueError('combat shell effects index is unavailable')
    raise RuntimeError('stock hit direction callback failed')
  modules=O(hit=hit,warning=lambda *args:warnings.append(args),health=lambda *args:health.append(args),
   constants=O(VEHICLE_HIT_FLAGS=O(**dict((name,1<<i) for i,name in enumerate(flags))),SHELL_TYPES_INDICES={'ARMOR_PIERCING':0,'HIGH_EXPLOSIVE':1}),
   battle_feedback_common=O(BATTLE_EVENT_TYPE=Types,NONE_SHELL_TYPE=-1),vehicle_marker_damage_type=O(FROM_PLAYER=7))
  event=dict(source='shot',kind='bot_human_hit' if incoming else 'bot_hit',damage=108,dead=False,shot_result=2,health=392,alive=True,death_reason=0,attack_reason=0,world_pose=True,x=10.,y=1.,z=100.,shell_index=0)
  event.update(dict(attacker_bot=2,target=1) if incoming else dict(attacker=1,target_bot=2))
  second=dict(event,health=284)
  if failing in ('descriptor','callback'):event['fail']=True
  if failing=='sticker':event['damage_sticker']=0
  result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[event,second],incoming)
  assert not result['errors'],result
  assert not result['disabled'],result
  assert health==[(91 if incoming else 92,392),(91 if incoming else 92,284)],health
  delivered=1 if failing=='battle_events' else 2
  assert [row['eventType'] for row in rows]==[4 if incoming else 1]*delivered,rows
  assert [row['details'] for row in rows]==[108]*delivered,rows
  assert len(shots)==(0 if incoming else (1 if failing=='shot_result' else 2)),shots
  assert entities[91 if incoming else 92].health==284
  assert hits==[392,284],hits
  assert len(warnings)==(1 if failing and not (incoming and failing=='shot_result') else 0),warnings
  # A stale native identity remains a real error; the presentation guards
  # must not relabel attacker damage or turn that invariant into success.
  if not incoming:
   avatar.playerVehicleID=0
   result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[second],incoming)
   assert len(result['errors'])==1 and 'identity mismatch' in result['errors'][0],result
   avatar.playerVehicleID=91
  # Neither malformed combat nor critical rejection is caught by the effect seam.
  bad=dict(event,cosmetic_only=True,critical={})
  result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[bad],incoming)
  assert len(result['errors'])==1 and 'cosmetic_only' in result['errors'][0],result
  bad=dict(second,critical={'invalid':True})
  result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[bad],incoming)
  assert len(result['errors'])==1 and 'critical payload rejected' in result['errors'][0],result
# Attacker-free environment health uses the same isolated damage-log callback.
# Both durable edges apply even when its first stock callback is unavailable.
rows[:]=[];health[:]=[];warnings[:]=[];attempted_rows[:]=[]
failing='battle_events'
first=dict(source='environment',kind='health',damage=108,dead=False,health=392,alive=True,death_reason=0,attack_reason=3,target=1)
second=dict(first,health=284)
result=native.exercise(avatar,modules,O(entity=lambda identity:entities[identity]),VehicleMarkerPlugin(provider),[first,second],1)
assert not result['errors'] and health==[(91,392),(91,284)] and len(rows)==1 and len(warnings)==1,(result,health,rows,warnings)
print('Human and Bot hit failures preserve credit, health, later hits and protocol rejection')
'''


@unittest.skipUnless(SOURCE, 'fixed gameplay source is required')
class NativeCombatEventTests(unittest.TestCase):
    def test_optional_hit_failures_preserve_confirmed_combat(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='battle-combat-event-') as directory:
            folder = Path(directory)
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                       '-Wl,-exported_symbol,_initbattle_combat_event_test', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(SOURCE) / 'native')]
            command += [str(ROOT / path) for path in (
                'tests/stock_native_battle_combat_event_host.cpp', 'bigworld/stock_native_battle_events_combat.cpp',
                'bigworld/stock_native_battle_events.cpp', 'bigworld/stock_native_battle_feedback.cpp',
                'bigworld/stock_native_battle_compat.cpp', 'bigworld/stock_native_client_state.cpp')]
            command += ['-o', str(folder / 'battle_combat_event_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
