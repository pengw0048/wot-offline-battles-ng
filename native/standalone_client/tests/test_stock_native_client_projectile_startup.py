"""Cold native projectile dependencies are installed before battle_start."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = r'''import sys,types
package=types.ModuleType('fixture');package.__path__=[];sys.modules['fixture']=package
calls=[]
class Battle(object):
 state='loading_entities';_worker_mode=False;_replay_mode=False
 _native_client_state=object()
class Native(types.ModuleType):
 def now(self,battle):return 100.
 def create(self,battle,now):
  assert battle.scene and battle.query and battle.state=='loading_entities'
  calls.append('projectile');return object()
native=Native('offline_client_projectile_native');sys.modules[native.__name__]=native
for name in ('offline_client_weapon_runtime_native','offline_client_transport_native'):
 sys.modules[name]=types.ModuleType(name)
drive=types.ModuleType('fixture.native_client_drive')
def prepare(battle):calls.append('scene');battle.scene=True
query=types.ModuleType('fixture.native_client_query')
def bind(battle):
 assert battle.scene
 calls.append('query');battle.query=not getattr(battle,'fail_query',False)
 return battle.query
drive.prepare=prepare;query.bind=bind
for module in (drive,query):
 sys.modules[module.__name__]=module;setattr(package,module.__name__.rsplit('.',1)[1],module)
module=types.ModuleType('fixture.projectile');module.__package__='fixture';sys.modules[module.__name__]=module
exec(compile(open(sys.argv[1],'rb').read(),sys.argv[1],'exec'),module.__dict__)
battle=Battle();assert module.enabled(battle)
owner=module.bind(battle)
assert calls==['scene','query','projectile'] and module.bind(battle) is owner
assert calls==['scene','query','projectile']
# A missing prerequisite never publishes a partially created projectile owner.
battle=Battle();battle.fail_query=True;calls[:]=[]
try:module.bind(battle)
except RuntimeError:pass
else:raise AssertionError('missing query owner was admitted')
assert calls==['scene','query'] and not hasattr(battle,'_native_client_projectile')
battle.fail_query=False;assert module.bind(battle)
assert calls==['scene','query','scene','query','projectile']
print('loading_entities dependency order and failed-install retry passed')
'''

class ProjectileStartup(unittest.TestCase):
 def check_dependency_order(self, interpreter):
  result=subprocess.run([interpreter,'-B','-c',SCRIPT,str(ROOT/'bigworld/stock_native_client_projectile.py')],text=True,capture_output=True)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)

 def test_cold_dependency_order(self):
  self.check_dependency_order(sys.executable)

 def test_exact_python27_cold_dependency_order(self):
  pyenv=shutil.which('pyenv')
  if pyenv is None:self.skipTest('The CPython 2.7 audit runtime is unavailable')
  result=subprocess.run([pyenv,'which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True,capture_output=True)
  if result.returncode:self.skipTest('The CPython 2.7.18 audit runtime is unavailable')
  self.check_dependency_order(result.stdout.strip())

if __name__=='__main__':unittest.main()
