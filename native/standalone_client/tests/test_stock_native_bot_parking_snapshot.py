"""Compare native catalog candidates with fixed-source deployment laws."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import copy,math,sys,types
import parking_snapshot_test as native
from gui.mods.offline_lan_0922 import bot_tactics as config,spg_positions as sourced
import parking_reference as reference
class O(object): pass
def close(a,b,path='root'):
 if isinstance(a,dict):
  assert set(a)==set(b),(path,set(a),set(b))
  for key in a:close(a[key],b[key],path+'.'+str(key))
 elif isinstance(a,(tuple,list)):
  assert len(a)==len(b),(path,len(a),len(b))
  for i,(x,y) in enumerate(zip(a,b)):close(x,y,path+'.'+str(i))
 elif isinstance(a,float):assert abs(a-b)<=1e-9*max(1.,abs(a),abs(b)),(path,a,b)
 else:assert a==b,(path,a,b)
cases=[]
for name in sorted(sourced.CATALOG['maps'])[:6]:
 entry=sourced.CATALOG['maps'][name];bounds=entry['bounds'];left,bottom,right,top=bounds
 cell=20.;width=int((right-left)/cell)+1;height=int((top-bottom)/cell)+1
 graph=dict(game_version=sourced.CATALOG['game_version'],map=name,width=width,height=height,cell_size=cell,origin=(left,bottom),bounds=bounds,heights_mm=[0]*(width*height),links=[255]*(width*height),hazards=[0]*(width*height),directions=((0,1),(1,1),(1,0),(1,-1),(0,-1),(-1,-1),(-1,0),(-1,1)))
 # The graph provides the real team's side instead of assuming north/south.
 sides=entry['spawn_sides'];positions={'north':((left+right)/2,top-100),'south':((left+right)/2,bottom+100),'east':(right-100,(bottom+top)/2),'west':(left+100,(bottom+top)/2)}
 graph['bases']=[positions[sides['1']],positions[sides['2']]]
 for variant in range(3):
  r=O();r.baked_graph=copy.deepcopy(graph);r._bot_tactics=config.empty();r.states={}
  for team in (1,2):
   r.states[team]=dict(id=team,team=team,vehicle='test:SPG',collision_shape=(1.8,3.2,-.2,2.),profile={'class_tag':'SPG'},_spg_initial=dict(source='community',catalog=sourced.CATALOG['revision']))
  for i in range(6):r.states[10+i]=dict(id=10+i,team=1+i%2,profile={'class_tag':'mediumTank'},route={'waypoints':[(0.,0.,0.),(left+50.+i*100.,0.,0.),(right-50.,0.,0.)]})
  if variant==1:
   for i in range(7,len(r.baked_graph['heights_mm']),31):r.baked_graph['heights_mm'][i]=None
   for i in range(11,len(r.baked_graph['hazards']),29):r.baked_graph['hazards'][i]=1
  if variant==2:
   zones=[dict(id='m1',team=1,point=[(left+right)/2,(bottom+top)/2],radius=65.,heading=37.,priority=2),dict(id='m2',team=2,point=[left+80,bottom+70],radius=45.,heading=-160.,priority=4),dict(id='gone',team=1,point=[0.,0.],radius=40.,heading=0.,priority=1)]
   r._bot_tactics['maps'][name]={'positions':zones,'deleted_positions':['gone']}
   identity=config.digest(r._bot_tactics)
   for team in (1,2):r.states[team]['_spg_initial'].update(source='launcher_manual_v1',catalog=identity)
  expected=reference.freeze_parking(r)['actors'];close(native.freeze(r),expected);cases.append((r,expected))
def forbidden(*args,**kwargs):raise AssertionError('native catalog called a Python business function')
for module_name,module in tuple(sys.modules.items()):
 if module_name.startswith('gui.mods.offline_lan_0922') and module is not None:
  for key,value in tuple(vars(module).items()):
   if isinstance(value,types.FunctionType):setattr(module,key,forbidden)
for runtime,expected in cases:close(native.freeze(runtime),expected)
print('native parking catalog: %d fixed-source cases and callback bombs passed' % len(cases))
'''


class NativeParkingSnapshotTest(unittest.TestCase):
    def test_fixed_catalogs(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('fixed gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-parking-catalog-') as directory:
            folder = Path(directory)
            command = ['c++', '-std=c++17', '-O1', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-isystem',
                       str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                       '-I' + str(Path(source) / 'native'),
                       str(ROOT / 'bigworld/stock_native_bot_parking.cpp'),
                       str(ROOT / 'bigworld/stock_native_bot_parking_snapshot.cpp'),
                       str(ROOT / 'tests/stock_native_bot_parking_snapshot_host.cpp'),
                       '-o', str(folder / 'parking_snapshot_test.so')]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            (folder / 'parking_reference.py').write_text(
                (ROOT / 'tests/stock_native_bot_planning_reference.py').read_text())
            setup = "import sys,types,os\nsys.path.insert(0,sys.argv[1]+'/src/res/scripts/client')\nfor name in ('gui','gui.mods'):\n m=types.ModuleType(name);m.__path__=[os.path.join(sys.path[0],*name.split('.'))];sys.modules[name]=m\n"
            (folder / 'test.py').write_text(setup + FIXTURE)
            run = subprocess.run([py27, '-B', str(folder / 'test.py'), source], cwd=folder,
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
