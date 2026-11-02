"""Combat feedback uses the same native identity owner as battle startup."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')

FIXTURE = r'''
import battle_feedback_test as native
# Native install takes over the stock callable directly, including modules
# imported before install, and restores all mutations on repeated teardown.
import sys,types
original=lambda *args:'stock'
replacement=lambda *args:'native'
other=lambda *args:'unrelated'
mover=types.ModuleType('mover_alias_fixture');mover.getCollidableEntities=original
reader=types.ModuleType('reader_alias_fixture');reader.getCollidableEntities=original
unrelated=types.ModuleType('unrelated_alias_fixture');unrelated.getCollidableEntities=other
sys.modules[mover.__name__]=mover;sys.modules[reader.__name__]=reader;sys.modules[unrelated.__name__]=unrelated
native.aliases(mover,original,replacement)
assert mover.getCollidableEntities is replacement and reader.getCollidableEntities is replacement
assert unrelated.getCollidableEntities is other
native.restore_aliases();native.restore_aliases()
assert mover.getCollidableEntities is original and reader.getCollidableEntities is original
assert unrelated.getCollidableEntities is other
for module in (mover,reader,unrelated):del sys.modules[module.__name__]
print('Native stock query alias takeover and idempotent restoration passed')
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Legacy(object):
 # Lobby compatibility intentionally has no battle identity in the native path.
 def assert_vehicle_marker_identity(self,*args):raise AssertionError('legacy identity owner called')
 def assert_vehicle_marker_damage_type(self,*args):raise AssertionError('legacy classifier owner called')
 def set_postmortem_vehicle(self,*args):raise AssertionError('legacy postmortem owner called')
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
rows=[]
shots=[]
dp=O(getPlayerVehicleID=lambda cached:91,getVehicleInfo=lambda identity:O(vehicleID=identity))
provider=O(getArenaDP=lambda:dp,setVehicleHealth=lambda *args:None,shared=O(feedback=object()))
avatar=O(playerVehicleID=91,guiSessionProvider=provider,onBattleEvents=lambda items:rows.extend(items),showShotResults=lambda items:shots.extend(items))
flag_names='ATTACK_IS_EXTERNAL_EXPLOSION ATTACK_IS_DIRECT_PROJECTILE MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_EXPLOSION MATERIAL_WITH_POSITIVE_DF_NOT_PIERCED_BY_PROJECTILE MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE RICOCHET DEVICE_PIERCED_BY_EXPLOSION DEVICE_PIERCED_BY_PROJECTILE DEVICE_DAMAGED_BY_EXPLOSION DEVICE_DAMAGED_BY_PROJECTILE CHASSIS_DAMAGED_BY_EXPLOSION CHASSIS_DAMAGED_BY_PROJECTILE GUN_DAMAGED_BY_EXPLOSION GUN_DAMAGED_BY_PROJECTILE FIRE_STARTED VEHICLE_KILLED'.split()
flags=O(**dict((name,1<<index) for index,name in enumerate(flag_names)))
modules=O(constants=O(VEHICLE_HIT_FLAGS=flags,SHELL_TYPES_INDICES={'ARMOR_PIERCING':0,'HIGH_EXPLOSIVE':1}),battle_feedback_common=O(BATTLE_EVENT_TYPE=Types,NONE_SHELL_TYPE=-1),vehicle_marker_damage_type=O(FROM_PLAYER=7))
plugin=VehicleMarkerPlugin(provider)
entity=O(typeDescriptor=O(gun=O(shots=[{'shell':{'kind':'ARMOR_PIERCING'}}])))
def call(event=None,expected=91,local=1,target_local=0,same_team=0,visible=1,worker=0):
 return native.combat(avatar,modules,O(entity=lambda identity:entity),Legacy(),plugin,event or dict(source='ram',kind='health',damage=234,dead=True),expected,local,target_local,same_team,visible,worker)
assert call() is True
assert rows==[dict(eventType=1,targetID=92,count=1,details=234),dict(eventType=3,targetID=92,count=1,details=0)],rows
rows[:]=[]
assert call(dict(source='shot',kind='bot_hit',damage=108,dead=True,shot_result=2)) is True
assert [(row['eventType'],row['details']) for row in rows]==[(1,108),(3,0)]
assert shots==[((flags.ATTACK_IS_DIRECT_PROJECTILE|flags.MATERIAL_WITH_POSITIVE_DF_PIERCED_BY_PROJECTILE|flags.VEHICLE_KILLED)<<32)|92],shots
# Friendly, remote, blind and worker traffic must not become local damage credit.
for opts in (dict(same_team=1),dict(local=0),dict(visible=0),dict(worker=1)):
 rows[:]=[];assert call(**opts) is False;assert rows==[]
rows[:]=[];assert call(local=0,target_local=1) is True
assert rows==[dict(eventType=4,targetID=91,count=1,details=234)],rows
# The real native guards still reject stale runtime, stock cache, Avatar and ArenaDP identities.
for drift in ('runtime','plugin','avatar','arena'):
 plugin._VehicleMarkerPlugin__playerVehicleID=91
 avatar.playerVehicleID=91;dp.getPlayerVehicleID=lambda cached:91
 if drift=='plugin':plugin._VehicleMarkerPlugin__playerVehicleID=0
 if drift=='avatar':avatar.playerVehicleID=0
 if drift=='arena':dp.getPlayerVehicleID=lambda cached:0
 rows[:]=[]
 try:call(expected=0 if drift=='runtime' else 91)
 except RuntimeError:pass
 else:raise AssertionError('stale '+drift+' identity accepted')
 assert rows==[]
# The current cache may be correct while the stock classifier/provider is stale.
avatar.playerVehicleID=91;dp.getPlayerVehicleID=lambda cached:91
for mutate,restore in (
 (lambda:setattr(plugin,'_VehicleMarkerPlugin__getVehicleDamageType',lambda info:8),lambda:delattr(plugin,'_VehicleMarkerPlugin__getVehicleDamageType')),
 (lambda:setattr(plugin,'sessionProvider',O()),lambda:setattr(plugin,'sessionProvider',provider))):
 plugin._VehicleMarkerPlugin__playerVehicleID=91;mutate();rows[:]=[]
 try:call()
 except RuntimeError:pass
 else:raise AssertionError('stale stock classifier/provider accepted')
 assert rows==[];restore()
print('Native combat damage/kill rows, exclusions and identity guards passed')
# Postmortem callbacks read the native attachment, including a zero/previous rollback.
attached=O(target=object());old_target=attached.target
camera=O(vehicleMProv=object());old_camera=camera.vehicleMProv
mode=O(curPostmortemDelay=None,_PostMortemControlMode__cam=camera)
handler=O(_AvatarInputHandler__ctrlModeName='postmortem',_AvatarInputHandler__curCtrl=mode)
matrices=O(attachedVehicleMatrix=attached,_ConsistentMatrices__setTarget=lambda matrix,unused:setattr(attached,'target',matrix))
modules.avatar_input_handler=O(_CTRL_MODE=O(POSTMORTEM='postmortem'))
modules.math=O(Vector3=lambda *args:args);modules.server=O(vehicle_id=91);modules.client=O(team=1)
target=O(matrix=object(),isAlive=lambda:True)
world=O(entity=lambda identity:target if identity in (77,92) else None)
seen=[]
def switched(identity,position):
 assert native.postmortem_id()==identity==92
 assert attached.target is target.matrix and camera.vehicleMProv is attached
 seen.append(identity)
avatar.inputHandler=handler;avatar.consistentMatrices=matrices;avatar.onSwitchViewpoint=switched
result=native.postmortem(avatar,modules,world,Legacy(),dict(team=1,alive=True,health=100))
assert result==dict(switched=True,error='',vehicle=92,spectated=92),result
assert seen==[92]
attached.target=old_target;camera.vehicleMProv=old_camera
def failed(identity,position):
 switched(identity,position)
 raise RuntimeError('stock callback failed')
avatar.onSwitchViewpoint=failed
result=native.postmortem(avatar,modules,world,Legacy(),dict(team=1,alive=True,health=100))
assert not result['switched'] and result['error'] and result['vehicle']==77 and result['spectated']==0,result
assert attached.target is old_target and camera.vehicleMProv is old_camera
assert target._postmortem_visible is False
result=native.postmortem(avatar,modules,world,Legacy(),dict(team=1,alive=True,health=100),0)
assert not result['switched'] and result['vehicle']==0 and result['spectated']==0,result
for state in (dict(team=2,alive=True,health=100),dict(team=1,alive=False,health=0)):
 result=native.postmortem(avatar,modules,world,Legacy(),state)
 assert result==dict(switched=False,error='',vehicle=77,spectated=0),result
print('Native postmortem attachment, callback ordering, rollback and candidate guards passed')
# The actual result presenter and binding publish AFTERBATTLE before notification,
# once, with the tuple consumed by the stock music/result controllers.
import cPickle,zlib
constants=O(ARENA_UPDATE=O(PERIOD=6),ARENA_PERIOD=O(AFTERBATTLE=4),FINISH_REASON=O(EXTERMINATION=1,BASE=2,TIMEOUT=3,UNKNOWN=4))
order=[]
def period(kind,blob):
 assert kind==6
 order.append(('period',cPickle.loads(zlib.decompress(blob))))
avatar.updateArena=period
avatar.onRoundFinished=lambda winner,reason:order.append(('finished',winner,reason))
for reason,expected in (('team_eliminated',1),('base_captured',2),('battle_timeout',3),('other',4)):
 order[:]=[];event=dict(winner=2,reason=reason)
 result=native.result(avatar,constants,[event,event],0)
 assert order==[('period',(4,0.,0.,(2,expected))),('finished',2,expected)],order
 assert result==dict(notified=True,live=True,result=event),result
order[:]=[]
result=native.result(avatar,constants,[event],1)
assert order==[] and result['notified'] and not result['live'],result
def rejected(*args):raise RuntimeError('arena presentation unavailable')
avatar.updateArena=rejected
native.result(avatar,constants,[event,event],0)
assert order==[('finished',2,4)],order
print('Native AFTERBATTLE/music outcome tuple, ordering, idempotence and failure isolation passed')
'''


@unittest.skipUnless(SOURCE, 'set WOT_FORK_0101_SOURCE to the fixed gameplay checkout')
class NativeBattleFeedbackTests(unittest.TestCase):
    def test_native_identity_owner_delivers_damage_and_kill_rows(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='battle-feedback-') as directory:
            folder = Path(directory)
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                       '-Wl,-exported_symbol,_initbattle_feedback_test', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(SOURCE) / 'native'), str(ROOT / 'tests/stock_native_battle_feedback_host.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_feedback.cpp'), str(ROOT / 'bigworld/stock_native_battle_compat.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_feedback_postmortem.cpp'), str(ROOT / 'bigworld/stock_native_battle_vehicles.cpp'),
                       str(ROOT / 'bigworld/stock_native_battle_startup_binding.cpp'),
                       str(ROOT / 'bigworld/stock_native_client_state.cpp'), '-o', str(folder / 'battle_feedback_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
