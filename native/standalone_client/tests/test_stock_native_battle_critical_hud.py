"""Critical snapshot/event ingress keeps HUD delivery separate from simulation state."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_native_battle_critical_order import method

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')

FIXTURE = r'''
import battle_critical_hud_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
names=['leftTrackHealth','rightTrackHealth','engineHealth','ammoBayHealth','gunHealth','driverHealth']
extras=[O(name='unused')]+[O(name=name) for name in names]
td=O(extras=extras,extrasDict=dict((extra.name,extra) for extra in extras),
 _offline_critical_device_pools=dict((name,(130.,100.)) for name in names if name!='driverHealth'),
 type=O(crewRoles=[('commander',),('driver',)]))
codes=['DEVICE_REPAIRED','DEVICE_REPAIRED_TO_CRITICAL','TANKMAN_RESTORED','TANKMAN_HIT_AT_SHOT','DEVICE_STARTED_FIRE_AT_SHOT','FIRE_STOPPED']
for base in ('DEVICE_DESTROYED','DEVICE_CRITICAL'):
 for suffix in ('_AT_SHOT','_AT_FIRE','_AT_RAMMING','_AT_WORLD_COLLISION','_AT_DROWNING'):codes.append(base+suffix)
constants=O(DAMAGE_INFO_CODES=codes,DAMAGE_INFO_INDICES=dict((code,i) for i,code in enumerate(codes)))
def payload(name,state):
 devices=[];ko=[];fire=False
 if name=='fire':fire=state
 elif name=='driver':ko=['driver'] if state=='destroyed' else []
 else:devices=[dict(name=name,hp={'destroyed':0.,'critical':40.,'normal':130.}[state],max_hp=130.,state=state)]
 return dict(devices=devices,destroyed=[row['name'] for row in devices if row['state']=='destroyed'],crew_ko=ko,fire=fire,ammo_rack_death=False,events=[])
def command(kind='flush',**kw):return dict(kind=kind,**kw)
def apply(name,state,revision=1,**kw):return command('apply',payload=payload(name,state),critical_revision=revision,**kw)
def run(commands,replay=False):
 delivered=[];attempts=[];warnings=[];panel={};holder={}
 entity=O(id=91,typeDescriptor=td,appearance=None)
 holder.update(entity=entity,attached=entity,fail=0,fire_fail=False)
 provider=O(shared=O(messages=object(),vehicleState=object()))
 avatar=O(vehicleTypeDescriptor=td,guiSessionProvider=provider,getVehicleAttached=lambda:holder['attached'])
 def show(identity,index,extra,attacker,equipment):
  attempts.append((identity,codes[index],extra,attacker))
  if holder['fail']:
   holder['fail']-=1
   raise RuntimeError('stock HUD callback unavailable')
  name=avatar.vehicleTypeDescriptor.extras[extra].name if extra else 'fire'
  code=codes[index]
  delivered.append((code,name,attacker))
  state='destroyed' if 'DESTROYED' in code or 'TANKMAN_HIT' in code else 'critical' if 'CRITICAL' in code else 'normal'
  panel[name[:-6] if name.endswith('Health') else name]=state
  if holder.get('reenter'):
   newer=holder.pop('reenter')
   provider.shared.messages=None
   native.reenter(newer,2)
 avatar.showVehicleDamageInfo=show
 def before(action):
  ready=action.get('ready',True)
  provider.shared.messages=object() if ready else None
  if 'fail' in action:holder['fail']=action['fail']
  if 'fire_fail' in action:holder['fire_fail']=action['fire_fail']
  if 'reenter' in action:holder['reenter']=action['reenter']
  if action.get('replace'):holder['entity']=O(id=91,typeDescriptor=td,appearance=None)
  if action.get('attachment')=='other':holder['attached']=O(id=92)
  if action.get('attachment')=='none':holder['attached']=None
  if action['kind']=='descriptor':
   shifted=O(extras=[extras[0],extras[-1]]+extras[1:-1],extrasDict=td.extrasDict,_offline_critical_device_pools=td._offline_critical_device_pools,type=td.type)
   holder['entity'].typeDescriptor=shifted;avatar.vehicleTypeDescriptor=shifted
 def fire_effect():
  if holder['fire_fail']:raise RuntimeError('optional fire effect unavailable')
 modules=O(constants=constants,before=before,warning=lambda *args:warnings.append(args),fire_effect=fire_effect)
 result=native.exercise(avatar,modules,O(entity=lambda identity:holder['entity']),commands,int(replay))
 assert not result['disabled'],result
 assert result['entity_held']==[bool(count) for count in result['pending']],result
 return result,delivered,attempts,warnings,panel
for replay in (False,True):
 for name in ('leftTrackHealth','rightTrackHealth','engineHealth','ammoBayHealth','gunHealth','driver','fire'):
  state=True if name=='fire' else 'destroyed'
  # The repeated snapshot takes both original early returns: its revision and
  # canonical state are already installed. Delivery must still retry later.
  result,rows,attempts,warnings,panel=run([apply(name,state,ready=False),apply(name,state,ready=False),command()],replay)
  assert not result['errors'] and result['pending']==[1,1,0] and len(rows)==1,(name,replay,result,rows)
  # The actual showDamageInfo boundary returns false when its callback throws.
  # Preserve the edge for another frame, without disabling later notifications.
  result,rows,attempts,warnings,panel=run([apply(name,state,fail=2),command()],replay)
  assert not result['errors'] and result['pending']==[1,0] and len(attempts)==3 and len(rows)==1 and warnings,(name,result,rows,attempts)
  normal=False if name=='fire' else 'normal'
  # A repair updates the native owner before the presented cache. The pending
  # destruction must be discarded even if that old cache still says broken.
  result,rows,attempts,warnings,panel=run([apply(name,state,ready=False),command('silent',payload=payload(name,normal))],replay)
  assert not result['errors'] and result['pending']==[1,0] and not rows,(name,result,rows)
  # A newer edge on the same key supersedes the failed red edge.
  result,rows,attempts,warnings,panel=run([apply(name,state,ready=False),apply(name,normal,2,ready=False),command()],replay)
  assert not result['errors'] and result['pending']==[1,1,0] and len(rows)==1,(name,result,rows)
  assert 'DESTROYED' not in rows[0][0] and 'TANKMAN_HIT' not in rows[0][0],rows
  # Source callers that notify before writing Presentation::critical must use
  # the actual damage owner, rather than reject a newly accepted transition.
  result,rows,attempts,warnings,panel=run([command('notify_before_cache',payload=payload(name,state))],replay)
  assert not result['errors'] and result['pending']==[0] and len(rows)==1,(name,result,rows)
for boundary in ('retire','restart','dead','engine','epoch','missing','stopped','replacement','attachment'):
 stop=command(boundary if boundary not in ('replacement','attachment') else 'flush')
 if boundary=='replacement':stop['replace']=True
 if boundary=='attachment':stop['attachment']='other'
 result,rows,attempts,warnings,panel=run([apply('rightTrackHealth','destroyed',ready=False),stop,command()])
 assert not result['errors'] and result['pending']==[1,0,0] and not rows,(boundary,result,rows)
# A valid death-blow notification may be shown synchronously before the death
# boundary. Once dead, only an undelivered old edge is retired.
death=apply('engineHealth','destroyed');death['kind']='death_blow'
result,rows,attempts,warnings,panel=run([death,command()])
assert not result['errors'] and len(rows)==1 and result['pending']==[0,0],(result,rows)
# All domains retain their own undelivered edge. Success on one device does
# not discard another device's failed edge or retire later crew/fire updates.
mixed=payload('rightTrackHealth','destroyed')
mixed['devices'].extend(payload('engineHealth','critical')['devices'])
mixed['crew_ko']=['driver'];mixed['fire']=True
result,rows,attempts,warnings,panel=run([command('apply',payload=mixed,critical_revision=1,ready=False),command(fail=1),command()])
assert not result['errors'] and result['pending']==[4,1,0] and len(rows)==4 and len(attempts)==5,(result,rows,attempts)
assert set(name for code,name,attacker in rows)==set(['rightTrackHealth','engineHealth','driverHealth','fire']),rows
# A synchronous stock callback installs a newer edge on the same key while
# the previous call succeeds. Its success cannot erase that newer pending edge.
result,rows,attempts,warnings,panel=run([apply('engineHealth','destroyed',reenter=payload('engineHealth','normal')),command()])
assert not result['errors'] and result['pending']==[1,0] and [row[0] for row in rows]==['DEVICE_DESTROYED_AT_SHOT','DEVICE_REPAIRED'],(result,rows)
# Re-resolve the exact extra against the current descriptor before retrying.
result,rows,attempts,warnings,panel=run([apply('rightTrackHealth','destroyed',ready=False),command('descriptor')])
assert not result['errors'] and rows==[('DEVICE_DESTROYED_AT_SHOT','rightTrackHealth',0)],(result,rows)
# Initial snapshot damage may be installed while startup still materializes
# ready entities. Preserve it across that phase and repeated canonical input.
result,rows,attempts,warnings,panel=run([command('loading'),apply('rightTrackHealth','destroyed'),apply('rightTrackHealth','destroyed'),command('running')])
assert not result['errors'] and result['pending']==[0,1,1,0] and len(rows)==1,(result,rows)
result,rows,attempts,warnings,panel=run([apply('engineHealth','destroyed',ready=False),command('finished')])
assert not result['errors'] and result['pending']==[1,0] and len(rows)==1,(result,rows)
# Missing attachment is deferred, not redirected into another vehicle's HUD.
result,rows,attempts,warnings,panel=run([apply('engineHealth','destroyed',attachment='none'),command(attachment='other')])
assert not result['errors'] and result['pending']==[1,0] and not rows,(result,rows)
# Optional native effects cannot consume otherwise valid HUD state.
result,rows,attempts,warnings,panel=run([apply('engineHealth','destroyed',fire_fail=True)])
assert not result['errors'] and len(rows)==1 and warnings,(result,rows,warnings)
# Invalid descriptor/ABI data remains an ingress error, never a queued success.
result,rows,attempts,warnings,panel=run([apply('unknownHealth','destroyed')])
assert result['errors'] and not rows and result['pending']==[0],(result,rows)
print('Critical HUD snapshots/events retry, supersession, native owner and lifecycle matrix passed')
'''


@unittest.skipUnless(SOURCE, 'fixed gameplay source is required')
class CriticalHUDTests(unittest.TestCase):
    def test_delivery_is_not_the_simulation_cache(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='critical-hud-') as directory:
            folder = Path(directory)
            source = (ROOT / 'bigworld/stock_native_battle_vehicles_lifecycle.cpp').read_text()
            lifecycle = '\n'.join(method(source, signature) for signature in ('void retire(Battle& b, Actor& v)', 'void quiesce(Battle& b)'))
            cpp = folder / 'host.cpp'
            cpp.write_text((ROOT / 'tests/stock_native_battle_critical_hud_host.cpp').read_text().replace('@LIFECYCLE@', lifecycle))
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wl,-dead_strip',
                       '-Wl,-exported_symbol,_initbattle_critical_hud_test', '-isystem', str(Path(prefix) / 'include/python2.7'),
                       '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(SOURCE) / 'native')]
            command += [str(cpp)] + [str(ROOT / 'bigworld' / name) for name in (
                'stock_native_battle_vehicles_critical.cpp', 'stock_native_battle_critical.cpp',
                'stock_native_bot_descriptor.cpp', 'stock_native_client_state.cpp')]
            command += ['-o', str(folder / 'battle_critical_hud_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
