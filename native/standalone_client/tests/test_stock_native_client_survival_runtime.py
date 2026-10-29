"""Exercise production survival bridges and their explicit native scene seam."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')
FIXTURE = r'''
import math,random,sys,types
import client_survival_runtime_test as native
source=sys.argv[1]+'/src/res/scripts/client/gui/mods/offline_lan_0922'
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 m=types.ModuleType(name);m.__path__=[source];sys.modules[name]=m
from gui.mods.offline_lan_0922 import water_geometry
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
def battle():
 events=[];descriptor=O(hull=O(hitTester=O(bbox=((-1.,.1,-2.),(1.,1.5,2.)))),chassis=O(hullPosition=(0.,.6,0.)))
 b=O(authoritative=dict(health=100,alive=True),entity=O(health=100,typeDescriptor=descriptor),_server=O(vehicle_id=10),_avatar=O(updateVehicleMiscStatus=lambda *a:events.append(a)),_runtime=O(bigworld=O(time=native.time,serverTime=native.time),constants=O(VEHICLE_MISC_STATUS=O(VEHICLE_DROWN_WARNING=7,VEHICLE_IS_OVERTURNED=8),DROWN_WARNING_LEVEL=O(SAFE=0,CAUTION=1,DANGER=2),OVERTURN_WARNING_LEVEL=O(SAFE=0,CAUTION=1,DANGER=2),OVERTURN_CONDITION=O(IGNOR_DELAY=.1,WARNING_COSINE=math.cos(math.radians(70)),ONBOARD_COSINE=math.cos(math.radians(80))))),water=-1.,body=None)
 for name in ('drown','overturn'):
  for field in ('check','time','level'):setattr(b,'_'+name+'_'+field,0)
  setattr(b,'_'+name+'_started',None)
 native.install(b);return b,events
random.seed(4)
b,events=battle()
for i in range(400):
 pose=dict(position=(random.uniform(-30,30),random.uniform(-3,3),random.uniform(-40,40)),yaw=random.uniform(-math.pi,math.pi),pitch=random.uniform(-1.5,1.5),roll=random.uniform(-math.pi,math.pi),up=None)
 native.pose(b,pose);b.body=O(**pose) if i%2 else None
 bottom,height=water_geometry.hull_sample(b.entity.typeDescriptor,pose['position'],pose['yaw'],pose['pitch'],pose['roll'])
 b.water=height*[.2,.75,1.25][i%3]
 native.drowning(b,.3,30.+i)
 expected=water_geometry.warning_level(b.water,height)
 assert native.levels(b)[0]==expected
 assert all(abs(x-y)<1.e-10 for x,y in zip(b.sample,bottom)),(b.sample,bottom)
 assert b._avatar._offh_drowning==(expected==2)
 assert b.entity._offh_drowning==(expected==2)
 up=math.cos(pose['pitch'])*math.cos(pose['roll'])
 native.overturn(b,.15,30.+i)
 level=2 if up<=math.cos(math.radians(80)) else 1 if up<=math.cos(math.radians(70)) else 0
 assert native.levels(b)[1]==level
assert all(a[3] in ((0.,0.),(100.,10.),(100.,30.)) for a in events)
# Replacement descriptors are read without inventing a universal hull height.
old=b.entity.typeDescriptor;b.entity.typeDescriptor=O(hull=None)
before=b._drown_level;native.drowning(b,.3,999.);assert b._drown_level==before
b.entity.typeDescriptor=old;b.water=99.;native.drowning(b,.3,1000.);assert b._drown_level==2
# A native HUD consumer may synchronously retire the complete battle.
b,events=battle();native.pose(b,dict(position=(0.,0.,0.),yaw=0.,pitch=0.,roll=0.,up=-1.))
def retire(*args):native.close(b)
b._avatar.updateVehicleMiscStatus=retire
native.overturn(b,.2,1.)
assert b._native_client_survival is None and not hasattr(b.entity,'_offh_overturned')
native.close(b);native.reset()
print('400 production survival geometry/flag boundaries and HUD retirement passed')
'''

class NativeSurvivalRuntimeTest(unittest.TestCase):
    def test_production_bridge(self):
        if not SOURCE:
            self.skipTest('fixed gameplay source is required')
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
            env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='client-survival-runtime-') as folder:
            path = Path(folder)
            law = (Path(SOURCE) / 'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (path / 'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n'+law)
            command = ['c++', '-std=c++17', '-O0', '-ffp-contract=off', '-DWOT_CLIENT_SURVIVAL_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-isystem', str(Path(prefix) / 'include/python2.7'), '-I'+folder, '-I'+str(ROOT / 'bigworld'), '-I'+str(Path(SOURCE) / 'native')]
            command += [str(ROOT / 'bigworld' / name) for name in ('stock_native_client_state.cpp', 'stock_native_client_survival.cpp', 'stock_native_client_drive_runtime.cpp', 'stock_native_bot_motion.cpp', 'stock_native_player_environment.cpp', 'stock_native_bot_combat.cpp')]
            command += [str(ROOT / 'tests/stock_native_client_survival_runtime_host.cpp'), '-o', str(path / 'client_survival_runtime_test.so')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            result = subprocess.run([py27, '-B', '-c', FIXTURE, SOURCE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

if __name__ == '__main__':
    unittest.main()
