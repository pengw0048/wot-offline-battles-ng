"""Exact Python 2.7 descriptor snapshot boundary, without offline callbacks."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'fixed gameplay source is required')
class CombatSnapshotTest(unittest.TestCase):
    def test_native_descriptor_snapshot(self):
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        py27=subprocess.check_output(['pyenv','which','python2.7'],env=dict(os.environ,PYENV_VERSION='2.7.18'),text=True).strip()
        prefix=Path(subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip())
        names=['stock_native_bot_combat_snapshot','stock_native_bot_descriptor','stock_native_bot_combat','stock_native_bot_combat_mode','stock_native_projectile_damage_python','stock_native_bot_resource_lifecycle']
        with tempfile.TemporaryDirectory(prefix='native-combat-snapshot-') as folder:
            binary=Path(folder)/'snapshot'
            command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-DWOT_RESOURCE_LIFECYCLE_HOST_BUILD','-isystem',str(prefix/'include/python2.7'),'-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native')]
            command += [str(ROOT/'bigworld'/(name+'.cpp')) for name in names]
            command += [str(fork/'native/offline_simulation_weapons.cpp'),str(ROOT/'tests/test_stock_native_bot_combat_snapshot.cpp'),'-L'+str(prefix/'lib'),'-lpython2.7','-o',str(binary)]
            subprocess.run(command,check=True)
            subprocess.run([str(binary)],env=dict(os.environ,WOT_GAMEPLAY_SOURCE=str(fork)),check=True)
if __name__=='__main__':unittest.main()
