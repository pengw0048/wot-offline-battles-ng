"""Exercise lifecycle/event DTO readers inside actual CPython 2.7."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
FIXTURE=r'''
import physics_snapshot_test as native
class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
travel=Box(physics={'speedLimits':(18.,7.)})
descriptor=Box(chassis=Box(rotationIsAroundCenter=False),physics={'trackCenterOffset':-1.5},
    isPitchHullAimingAvailable=True,defaultVehicleDescr=travel)
battle=Box(_bots=Box(_descriptor_pairs={}))
state=dict(id=7,pose_sample=(12.,15.,.3),_last_world_impact_time=19.,turret_yaw=.6)
expected=(1,1,0,1.5,18.,7.,19.,1,.3,.6)
assert native.inspect(battle,state,descriptor,1)==expected
battle._bots._descriptor_pairs[7]=(Box(physics={'speedLimits':(22.,9.)}),descriptor)
assert native.inspect(battle,state,descriptor,1)[4:6]==(22.,9.)
del battle._bots._descriptor_pairs[7]
descriptor.chassis.rotationIsAroundCenter=None
assert native.inspect(battle,state,descriptor,0)[1]==0
descriptor.chassis.rotationIsAroundCenter=False
descriptor.physics['trackCenterOffset']=float('nan')
assert native.inspect(battle,state,descriptor,0)[1]==0
descriptor.physics['trackCenterOffset']=-1.5
battle._bots._descriptor_pairs[7]=(None,descriptor)
assert native.inspect(battle,state,descriptor,1)==expected
del battle._bots._descriptor_pairs[7]
for bad in (float('nan'),-1.):
    travel.physics['speedLimits']=(bad,7.)
    try:native.inspect(battle,state,descriptor,1)
    except (ValueError,TypeError):pass
    else:raise AssertionError('bad travel limit accepted')
travel.physics['speedLimits']=(18.,7.)
assert native.inspect(battle,state,descriptor,1)==expected
row=dict(actor_kind='bot',actor_id=7,created_time_ms=1234,motion_seq=2,
    attitude=(0.,.1,.2),spin=(1.,2.,3.),support_key='player:9',
    flight=dict(rest=(10.,2.,30.),duration=1.5,landed=True,rest_attitude=(.5,.6,.7)))
assert native.row(row)==(0,7,1234.,2,1.5,2.,1,0,1,9)
row['flight']['body']={}
assert native.row(row)[7]==1
for key,bad in [('actor_id',True),('actor_kind','wreck'),('support_key','player:0'),('created_time_ms',float('inf'))]:
    old=row[key];row[key]=bad
    try:native.row(row)
    except (ValueError,TypeError):pass
    else:raise AssertionError('bad row accepted '+key)
    finally:row[key]=old
print('physics snapshot passed')
'''
class PhysicsSnapshotTests(unittest.TestCase):
    def test_installed_geometry_and_event_identity(self):
        source=os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:self.skipTest('fixed gameplay source is required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='physics-snapshot-') as directory:
            folder=Path(directory);script=folder/'fixture.py';script.write_text(FIXTURE)
            law=(Path(source)/'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
            command=['c++','-std=c++17','-O1','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup',
                '-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(Path(source)/'native'),'-I'+str(ROOT/'bigworld'),
                str(ROOT/'bigworld/stock_native_bot_physics_snapshot.cpp'),str(ROOT/'tests/stock_native_bot_physics_snapshot_host.cpp'),'-o',str(folder/'physics_snapshot_test.so')]
            built=subprocess.run(command,text=True,capture_output=True);self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B',str(script)],text=True,capture_output=True)
            self.assertEqual(run.returncode,0,run.stderr);self.assertIn('physics snapshot passed',run.stdout)
if __name__=='__main__':unittest.main()
