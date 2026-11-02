"""Canonical native fire drives the stock effect, independent of dead Python fields."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from stock_battle_gui_support import client_executable, compiler, python27
from test_stock_native_battle_radio import SOURCE
from test_stock_native_battle_critical_order import method

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import battle_fire_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
class Extra(object):
 def __init__(self):self.running=False;self.calls=[];self.fail=False
 def isRunningFor(self,entity):return self.running
 def startFor(self,entity):
  if self.fail:raise RuntimeError('streamed effect unavailable')
  self.calls.append(('start',entity.id));self.running=True
 def stopFor(self,entity):self.calls.append(('stop',entity.id));self.running=False
extra=Extra()
def entity(id=101):return O(id=id,typeDescriptor=O(extrasDict={'fire':extra}),appearance=O(compoundModel=object()))
e=entity()
# Stock Vehicle has no legacy field. Canonical fire must start and idempotently remain on.
assert native.sync(e,{101:True,202:False},None,2)
assert extra.calls==[('start',101)]
assert not native.sync(e,{101:True},None,2)
# An opposite stale Python value cannot override either canonical edge.
e.is_on_fire=True
assert native.sync(e,{101:False},None,2)
e.is_on_fire=False
assert native.sync(e,{101:True},None,2)
# Hide overrides canonical burning and permits cleanup outside Running.
assert native.sync(e,{101:True},False,6)
assert not native.sync(e,{101:True},False,6)
# Missing canonical state and loading/retired owners cannot start effects from stale Python.
e.is_on_fire=True
for states,phase in [(None,2),({},2),({202:True},2),({101:True},1),({101:True},5)]:
 assert not native.sync(e,states,None,phase)
assert not extra.running
# Resolve by this entity's id, including remote vehicles; no local-player shortcut.
r=entity(202)
assert native.sync(r,{101:False,202:True},None,2)
assert extra.calls[-1]==('start',202)
assert native.sync(r,{202:False},None,2)
# Partial startup is contained. Appearance readiness is retried from canonical state.
del e.id
assert not native.sync(e,{101:True},None,2)
e.id=101
e.typeDescriptor=None
assert not native.sync(e,{101:True},None,2)
e.typeDescriptor=O(extrasDict={})
assert not native.sync(e,{101:True},None,2)
e.typeDescriptor.extrasDict['fire']=extra
e.appearance.compoundModel=None
assert not native.sync(e,{101:True},None,2)
e.appearance.compoundModel=object()
extra.fail=True
try:native.sync(e,{101:True},None,2)
except RuntimeError:pass
else:raise AssertionError('effect failure swallowed')
assert not extra.running
extra.fail=False
assert native.sync(e,{101:True},None,2)
assert native.sync(e,{101:False},None,2)
# Explicit start/stop remains an intentional override independent of state.
assert native.sync(e,None,True,2)
assert native.sync(e,None,False,5)
print('Canonical fire ownership, overrides, identity and partial-startup retries passed')
'''


@unittest.skipUnless(SOURCE, 'set WOT_FORK_0101_SOURCE to the fixed gameplay checkout')
class NativeBattleFireTest(unittest.TestCase):
    def test_production_effect_reads_native_owner_and_retries_readiness(self):
        py27 = python27(self)
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        edges = (ROOT / 'bigworld/stock_native_battle_vehicles_edges.cpp').read_text()
        critical = (ROOT / 'bigworld/stock_native_battle_critical.cpp').read_text()
        body = method(edges, 'bool syncFireEffect(')
        damage = method(critical, 'const VehicleDamage* damage(')
        host = (ROOT / 'tests/stock_native_battle_fire_host.cpp').read_text()
        host = host.replace('@DAMAGE@', damage).replace('@SYNC_FIRE@', body)
        with tempfile.TemporaryDirectory(prefix='battle-fire-') as directory:
            folder = Path(directory)
            path = folder / 'host.cpp'
            path.write_text(host)
            shared = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
            command = [compiler(self), '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD',
                       '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register'] + shared + [
                       '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                       '-I' + str(Path(SOURCE) / 'native'), '-I' + str(ROOT / 'tests'), str(path),
                       str(ROOT / 'bigworld/stock_native_client_state.cpp'), '-o', str(folder / 'battle_fire_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # The previous real consumer loses the very first native fire edge.
            legacy = body[:body.index('\n    if (!burning)')] + '\n' + body[body.index('    auto descriptor'):]
            legacy = legacy.replace('const bool on = *burning;',
                                    'const bool on = burning ? *burning : p::flag(py::optional(entity, "is_on_fire").get());')
            path.write_text(host.replace(body, legacy))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('AssertionError', result.stderr)
        # The real producer writes only the native owner; no Python mirror is restored.
        producer = method(critical, 'Value applyPayload(')
        self.assertIn('d.fire = on_fire;', producer)
        self.assertNotIn('is_on_fire', producer)
        self.assertNotIn('is_on_fire', body.replace('// applyPayload owns fire in C++; stock Vehicle has no is_on_fire property.', ''))

    def test_exact_stock_has_no_fire_attribute_bridge(self):
        exe = client_executable(self)
        self.assertNotIn(b'is_on_fire', exe.data)  # No native registration name.
        client = Path(os.environ['WOT_0922_CLIENT'])
        with zipfile.ZipFile(client / 'res/packages/scripts.pkg') as package:
            self.assertNotIn(b'is_on_fire', package.read('scripts/client/Vehicle.pyc'))


if __name__ == '__main__':
    unittest.main()
