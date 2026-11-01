"""The native outline producer feeds both stock lock modes and team commands."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import battle_target_lock_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
def original(avatar,target):
 avatar.received.append(target)
 avatar._PlayerAvatar__autoAimVehID=target.id if target else 0
def arcade(control,is_down,key,mods):
 if is_down and key==2:control.avatar.autoAim(control.avatar,None)
def sniper(control,is_down,key,mods):
 if is_down and key==2:control.avatar.autoAim(control.avatar,None)
def run(avatar,control,candidate,arcade_hook,sniper_hook,team_target):
 assert team_target is candidate
 for hook in (arcade_hook,sniper_hook):
  avatar.received[:]=[]
  hook(control,True,2,0)
  assert avatar.received==[candidate],avatar.received
  assert avatar._PlayerAvatar__autoAimVehID==candidate.id
  # An explicit unlock outside the lock-key scope must stay an unlock.
  avatar.autoAim(avatar,None)
  assert avatar.received==[candidate,None]
  hook(control,False,2,0)
  assert avatar.received==[candidate,None]
for identity in (1001,1002):
 avatar=O(received=[],_PlayerAvatar__autoAimVehID=0)
 control=O(avatar=avatar)
 candidate=O(id=identity,_offlineNativeRemote=True,isAlive=lambda:True,_spot_visible=True,model=object(),inWorld=True)
 modules=O(legacy=O(_battle_active=False,_target_lock_candidate=None),bigworld=O(player=lambda:avatar),
  control_modes=O(CommandMapping=O(CMD_CM_LOCK_TARGET=7,g_instance=O(isFired=lambda command,key:command==7 and key==2))),
  getattribute=object.__getattribute__,auto_aim=original,arcade=arcade,sniper=sniper,run=run)
 native.exercise(avatar,control,candidate,modules)
 assert modules.legacy._target_lock_candidate is None
print('Native outline owner, arcade/sniper lock, explicit unlock, release and team target passed')
import offline_client_vehicle_hooks_native as hooks
def probe(legacy,vehicle,avatar,appearance,active):
 get,setter,speed,speeds,avatar_get,compound_get=[hooks.create(legacy,i) for i in range(6)]
 if active:
  assert get(vehicle,'position')=='current position'
  assert get(vehicle,'matrix')=='current matrix'
  assert get(vehicle,'speedInfo').value==(9.,.2,9.,'preserved')
  setter(vehicle,'health',75)
  assert get(vehicle,'health')==75 and vehicle.health==100
  assert speed(vehicle)==9. and speeds(avatar)==(9.,.2)
  assert avatar_get(avatar,'cell') is avatar.fakeServer
  assert compound_get(appearance,'_CompoundAppearance__filter')._vehicle_filter is appearance._CompoundAppearance__filter
 else:
  assert get(vehicle,'position')=='birth position'
  assert get(vehicle,'health')==100
  setter(vehicle,'health',99)
  assert vehicle.health==99
  assert speed(vehicle)==-9 and speeds(avatar)==(-9.,-1.)
  assert avatar_get(avatar,'cell')=='stock cell'
  assert compound_get(appearance,'_CompoundAppearance__filter') is appearance._CompoundAppearance__filter
vehicle=O(position='birth position',matrix='birth matrix',health=100,speedInfo=O(value=(0,0,0,'preserved')))
avatar=O(playerVehicleID=7,fakeServer=object(),cell='stock cell')
appearance=O(_CompoundAppearance__filter=object())
legacy=O(_battle_active=False,_vehicle_property_overlays={id(vehicle):dict(position='stale Python pose')},
 _original_vehicle_getattribute=object.__getattribute__,_original_vehicle_setattr=object.__setattr__,
 _original_avatar_getattribute=object.__getattribute__,_original_compound_getattribute=object.__getattribute__,
 _original_vehicle_get_speed=lambda value:-9,_original_avatar_get_speeds=lambda value,instantaneous:(-9.,-1.),
 _runtime=O(bigworld=O(entity=lambda identity:vehicle)))
native.remote_hooks(legacy,vehicle,avatar,appearance,probe)
assert legacy._vehicle_property_overlays[id(vehicle)]['position']=='stale Python pose'
print('Native vehicle/avatar/compound hooks use round state and preserve stock behavior outside it')
'''


@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'fixed gameplay source is required')
class NativeBattleTargetLockTests(unittest.TestCase):
    def test_outline_candidate_reaches_the_native_input_owner(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='battle-target-lock-') as directory:
            folder = Path(directory)
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register',
                       '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                       '-Wl,-exported_symbol,_initbattle_target_lock_test', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(os.environ['WOT_FORK_0101_SOURCE']) / 'native'),
                       str(ROOT / 'tests/stock_native_battle_target_lock_host.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_compat.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_compat_hooks.cpp'),
                       str(ROOT / 'bigworld/stock_native_client_targeting.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_remote_hooks.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_remote_gates.cpp'),
                       str(ROOT / 'bigworld/stock_native_client_state.cpp'), '-o', str(folder / 'battle_target_lock_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
