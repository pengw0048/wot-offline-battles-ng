"""Pinned equipment snapshot clocks, semantic admission, and RAM owner traps."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
from __future__ import print_function
import ast,copy,imp,math,random,sys,types
root=sys.argv[1]+'/src/res/scripts/client';sys.path.insert(0,root)
for name in ('gui','gui.mods'):
 m=types.ModuleType(name);m.__path__=[root+'/'+name.replace('.','/')];sys.modules[name]=m
from gui.mods.offline_lan_0922 import equipment_mechanics as equipment
import equipment_snapshot_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
source=open(root+'/gui/mods/offline_lan_0922/battle_runtime.py').read()
binding=imp.load_source('equipment_snapshot_bindings',sys.argv[2]);prepared=binding.battle(source)
def load(text,names,label):
 tree=ast.parse(text);cls=next(x for x in tree.body if isinstance(x,ast.ClassDef) and x.name=='BattleRuntime');cls.body=[x for x in cls.body if isinstance(x,ast.FunctionDef) and x.name in names];module=ast.Module(body=[cls]);ast.fix_missing_locations(module)
 ns=dict(__name__='gui.mods.offline_lan_0922.'+label,equipment_mechanics=equipment,sys=sys);exec(compile(module,label,'exec'),ns);return ns['BattleRuntime']
original=load(source,set(['_restore_local_equipment_snapshot']),'equipment_original')
adapted=load(prepared,set(['_restore_local_equipment_snapshot','_restore_local_equipment_snapshot_cold','_remember_ram_bot_snapshot','_authority_players']),'equipment_adapted')
facade=types.ModuleType('gui.mods.offline_lan_0922.native_client_weapon_runtime')
sys.modules[facade.__name__]=facade
package=sys.modules['gui.mods.offline_lan_0922']
package.native_client_weapon_runtime=facade
cold=[0];gate=native.create()
def ingress(b,states):pass
def restore(b,snapshot,present):
 if b.state!='running' or b._worker_mode or b._replay_mode:return None
 native.set_time(now[0])
 return native.equipment_snapshot(b,snapshot,int(present))
facade.restore_equipment_snapshot=restore;facade.equipment_ingress=native.equipment_ingress
now=[0.]
def battle(cls):
 b=cls();b.state='running';b._worker_mode=False;b._replay_mode=False;b._client_ready_received=True;b.client=O(player_id=7);b._equipment_state=None;b._equipment_revision=-1;b._clock=lambda:now[0];b._runtime=O(bigworld=O(time=native.time));b._equipment_result_logged_seq=0;b._local_damage_report={'old':1};b._local_critical_owned=True;b._avatar=O();b._server=O();b.present=[];b._present_equipments=lambda t:b.present.append(tuple((e.uses_left,e.active,e.ready_at>t) for e in b._equipment_state));return b
a,b=battle(original),battle(adapted)
original_cold=b._restore_local_equipment_snapshot_cold
def counted_cold(*args):cold[0]+=1;return original_cold(*args)
b._restore_local_equipment_snapshot_cold=counted_cold
contract=equipment.project_equipment(dict(name='largeRepairkit',tags=['repairkit'],id=4,compactDescr=44,reuseCount=3,cooldownSeconds=8.,repairAll=True))
auto=equipment.project_equipment(dict(name='autoExtinguishers',tags=[],id=5,compactDescr=55,reuseCount=3,cooldownSeconds=8.,autoactivate=True))
limiter=equipment.project_equipment(dict(name='removedRpmLimiter',tags=[],id=6,compactDescr=66,reuseCount=-1,enginePowerFactor=1.1))
rows=[dict(equipment=x,usesLeft=4 if x['reuseCount']>=0 else -1,cooldownTimeLeft=0.,active=False,autoPendingElapsed=None,aiPendingElapsed=None) for x in (contract,auto,limiter)]
rng=random.Random(1513);revision=0;count=0
for i in range(1200):
 now[0]+=rng.choice((0.,.016,.1,.9,4.1));remaining=max(0.,8.-(i%71)*.15)
 for row in rows[:2]:row['cooldownTimeLeft']=remaining
 rows[0]['aiPendingElapsed']=None if i%3==0 else min(3600.,i*.1)
 rows[1]['autoPendingElapsed']=None if i%5==0 else min(3600.,i*.2)
 if i%71==0:
  revision+=1;rows[2]['active']=not rows[2]['active']
 if i%113==0:
  rows[0]['equipment']['bonusValue']=.1 if i%2 else .25
 if i%149==0:rows[0]['usesLeft']=1 if rows[0]['usesLeft']==4 else 4
 result=dict(intent_seq=i//101,accepted=True)
 snapshot=dict(players=[dict(id=3),dict(id=7,equipment_revision=revision,equipment_states=copy.deepcopy(rows),equipment_intent_result=result)])
 assert a._restore_local_equipment_snapshot(snapshot,True)==b._restore_local_equipment_snapshot(snapshot,True)
 for x,y in zip(a._equipment_state,b._equipment_state):
  assert x.snapshot(now[0])==y.snapshot(now[0]),(i,x.snapshot(now[0]),y.snapshot(now[0]))
 assert a._equipment_revision==b._equipment_revision and a._local_critical_owned==b._local_critical_owned and a._local_damage_report==b._local_damage_report
 assert a.present==b.present
 count+=1
assert cold[0]<80,cold
# An unchanged native clock does not call Python constructors, restore, ready,
# or parser business. The existing slots keep their exact absolute clocks.
def bomb(*args,**kw):raise AssertionError('Python equipment hot business')
saved=equipment.restore_equipment_states;old_restore=equipment.EquipmentState.restore;old_ready=equipment.EquipmentState.ready;equipment.restore_equipment_states=bomb
equipment.EquipmentState.restore=bomb;equipment.EquipmentState.ready=bomb
now[0]+=.03;old_clock=b._clock;b._clock=bomb;b._restore_local_equipment_snapshot(snapshot,True);b._clock=old_clock
equipment.restore_equipment_states=saved;equipment.EquipmentState.restore=old_restore;equipment.EquipmentState.ready=old_ready
# The clock parser accepts the same numeric coercion and rejects malformed
# clocks even when contract/version/quantity remain unchanged.
for field,values in [('cooldownTimeLeft',[-1.,8.000002,True,None,'bad','0x1p2',float('nan'),float('inf'),8.0000005,' 0.5 ']),('autoPendingElapsed',[-1.,3600.0001,True,'bad',float('nan'),0.,3600.,None]),('aiPendingElapsed',[-1.,3600.0001,False,'bad',float('inf'),0.,3600.,None])]:
 for value in values:
  bad=copy.deepcopy(rows);index=1 if field=='autoPendingElapsed' else 0;bad[index][field]=value
  try:expected=equipment.restore_equipment_states(bad,now=now[0]);valid=True
  except ValueError:valid=False
  try:
   testgate=native.create();native.step(testgate,bad,1,None,None,now[0],1);states=equipment.restore_equipment_states(rows,now=now[0]);native.step(testgate,bad,1,None,states,now[0]);actual=True
  except RuntimeError:actual=False
  assert actual==valid,(field,value,valid)
  if valid:assert [e.snapshot(now[0]) for e in states]==[e.snapshot(now[0]) for e in expected]
# Exercise the actual production CAPI's absence/None/regression fences.
assert native.equipment_snapshot(b,{'players':[]},1) is False
assert native.equipment_snapshot(b,{'players':[dict(id=7)]},1) is False
for bad in (dict(id=7,equipment_revision=revision,equipment_states=None),dict(id=7,equipment_revision=-1,equipment_states=rows),dict(id=7,equipment_revision=revision-1,equipment_states=rows)):
 try:native.equipment_snapshot(b,dict(players=[bad]),1)
 except RuntimeError:pass
 else:raise AssertionError('invalid native equipment snapshot was accepted')
# A nested accepted edge wins over the outer cold transaction's stale cache.
outer=copy.deepcopy(snapshot);outer['players'][1]['equipment_revision']=revision+1
inner=copy.deepcopy(outer);inner['players'][1]['equipment_revision']=revision+2;inner['players'][1]['equipment_states'][2]['active']=not rows[2]['active']
def nested_cold(message,present):
 result=original_cold(message,present)
 if message is outer:native.equipment_snapshot(b,inner,int(present))
 return result
b._restore_local_equipment_snapshot_cold=nested_cold
assert native.equipment_snapshot(b,outer,1) is True
assert b._equipment_revision==revision+2
b._restore_local_equipment_snapshot_cold=bomb
assert native.equipment_snapshot(b,inner,1) is True
b._restore_local_equipment_snapshot_cold=counted_cold
# Native visible history is not rebuilt, even if a legacy cache is a trap.
class Trap(dict):
 def get(self,*a):raise AssertionError('duplicate Python RAM history')
b._native_client_state=object();b._bots=O(states=Trap());b._ram_bot_history=Trap()
assert b._remember_ram_bot_snapshot({'bots':Trap(),'bot_state_revision':1,'bot_state_time_us':1}) is False
b._last_snapshot=Trap();b._decorate_ram_contacts=bomb;b._live_local_player_state=bomb
assert b._authority_players()==[]
# Retain the actual legacy path for replay and a worker, rather than claiming
# native live ownership for these modes.
for worker,replay in ((True,False),(False,True)):
 b._worker_mode,b._replay_mode=worker,replay
 assert b._remember_ram_bot_snapshot(None) is False
b._worker_mode=b._replay_mode=False;b.state='loading'
assert facade.restore_equipment_snapshot(b,snapshot,True) is None
print('equipment original AST %d clocks/edges passed; cold parses=%d; hot traps/RAM ownership passed'%(count,cold[0]))
'''


class NativeClientWeaponEquipmentTest(unittest.TestCase):
    def test_original_snapshot_clocks_and_owner_boundaries(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Pinned gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True, env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-equipment-') as directory:
            folder = Path(directory)
            (folder/'fixture.py').write_text(FIXTURE)
            bridge = (ROOT/'bigworld/stock_native_client_weapon_runtime_python.cpp').read_text()
            start = bridge.index('PyObject*equipmentSnapshot(')
            end = bridge.index('PyObject*publish(', start)
            (folder/'host.cpp').write_text((ROOT/'tests/stock_native_client_weapon_equipment_host.cpp').read_text()+'\n'+bridge[start:end])
            command = ['c++', '-std=c++17', '-O1', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(Path(prefix)/'include/python2.7'), '-I'+str(ROOT/'bigworld'), str(folder/'host.cpp'), str(ROOT/'bigworld/stock_native_client_state.cpp'), '-o', str(folder/'equipment_snapshot_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([py27, '-B', str(folder/'fixture.py'), source, str(ROOT/'bigworld/stock_native_client_weapon_runtime_bindings.py')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)


if __name__ == '__main__':
    unittest.main()
