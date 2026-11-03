"""Native empty-ammo, artillery slot/sortie and blocked tracking ownership."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ModesTest(unittest.TestCase):
    def test_modes_and_tracking(self):
        source=os.environ.get('WOT_FORK_SOURCE')
        if not source:self.skipTest('fixed gameplay source required')
        sys.path.insert(0,str(ROOT/'bigworld'))
        from prepare_offline_fork import stage_bot_ammo_data
        with tempfile.TemporaryDirectory(prefix='bot-modes-') as directory:
            folder=Path(directory);stage_bot_ammo_data(Path(source),folder)
            command=['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),'-I'+str(Path(source)/'native'),'-I'+directory]
            command += [str(ROOT/'bigworld'/('stock_native_bot_'+name+'.cpp')) for name in ('decision','parking','planner','strategy')]
            command += [str(ROOT/'tests/test_stock_native_bot_modes.cpp'),'-o',str(folder/'host')]
            built=subprocess.run(command,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stderr)
            ran=subprocess.run([str(folder/'host')],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stderr)
if __name__=='__main__':unittest.main()
