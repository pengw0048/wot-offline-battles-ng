"""Descriptor projection keeps skill carriers, booster limits and crew damage."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import human_projection_test as native
class Box(object):
 def __init__(self, **kw): self.__dict__.update(kw)
profile=dict(vision_factor=1.,binocular_factor=1.,binocular_delay=3.,camouflage_net_delay=3.,has_binoculars=False,has_camouflage_net=False)
state=dict(vision=1.,signal=1.,base_moving=.1,base_still=.2,invisibility_moving=(0.,1.),invisibility_still=(0.,1.))
crew=['radioman','gunner']
members=[dict(instance='radioman',skills=[dict(name='radioman_lasteffort',level=100.,active=True,enabled=True),dict(name='radioman_retransmitter',level=50.,active=True,enabled=True)]),dict(instance='gunner',skills=[dict(name='gunner_rancorous',level=100.,active=True,enabled=True)])]
snapshot=dict(crew=dict(members=members,dynamic_spotting=dict(crew=crew,states={'%d:%d'%(m,f):state for m in range(4) for f in range(2)})),spotting=profile,camouflage=dict(shot_factor=.5),loadout=dict(radio_factor=1.),battle_booster=dict(skill_overrides=dict(designated_target_duration=4.,last_effort_duration=5.,designated_target_sector=.02)))
desc=Box(turret=Box(circularVisionRadius=330.),miscAttrs=Box(circularVisionRadiusFactor=1.),radio=Box(distance=400.))
rows=native.project(dict(id=1,team=1,slot=1),snapshot,desc)
assert len(rows)==24
assert rows[0]==(400.,.05,14.,5.,.02,1,1), rows[0]
assert rows[6]==(400.,0.,14.,5.,.02,0,1), rows[6]
assert rows[12]==(400.,.05,14.,5.,.02,1,0), rows[12]
assert rows[18]==(400.,0.,14.,5.,.02,0,0), rows[18]
raw=dict(id=1,team=1,critical=dict(crew_ko=['radioman'],devices=[dict(name='radioHealth',hp=30.,state='critical')]))
assert native.project(raw,snapshot,desc)==(200.,0.,0,1)
raw['critical']=dict(crew_ko=['gunner'],destroyed=['radioHealth'])
assert native.project(raw,snapshot,desc)==(100.,.05,1,0)
raw['critical']=dict(crew_ko=[])
assert native.project(raw,snapshot,desc)==(400.,.05,1,1)
snapshot.pop('battle_booster')
row=native.project(dict(id=1,team=1),snapshot,desc)[0]
assert row[:4]==(400.,.05,12.,2.) and abs(row[4] - 3.141592653589793/36.) < 1e-10 and row[5:]==(1,1)
print('human projection passed')
'''

class HumanProjectionV111(unittest.TestCase):
    def test_skills_booster_and_crew_masks(self):
        source = os.environ.get('WOT_FORK_SOURCE')
        if not source: self.skipTest('WOT_FORK_SOURCE is required')
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        spec=importlib.util.spec_from_file_location('radio_patch',ROOT/'bigworld/patch_offline_radio.py');patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory() as directory:
            out=Path(directory); native=Path(source)/'native'
            (out/'offline_simulation_control.h').write_text(patch.patch_header((native/'offline_simulation_control.h').read_text()))
            law=(native/'offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (out/'offline_release_drive_law.hpp').write_text('#pragma once\n#include <cmath>\n#include <algorithm>\n'+law)
            (out/'fixture.py').write_text(FIXTURE)
            binary=out/'human_projection_test.so'
            command=['c++','-std=c++17','-O1','-DWOT_CLIENT_REPLICA_HOST_BUILD','-ffunction-sections','-fvisibility=hidden','-Wl,-dead_strip','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+directory,'-I'+str(native),'-I'+str(ROOT/'bigworld'),str(ROOT/'bigworld/stock_native_bot_human_snapshot.cpp'),str(ROOT/'bigworld/stock_native_client_state.cpp'),str(ROOT/'tests/stock_native_human_projection_host.cpp'),'-o',str(binary)]
            built=subprocess.run(command,text=True,capture_output=True);self.assertEqual(0,built.returncode,built.stderr)
            run=subprocess.run([py27,'-B',str(out/'fixture.py')],text=True,capture_output=True);self.assertEqual(0,run.returncode,run.stderr);self.assertIn('human projection passed',run.stdout)
