"""Exercise the production native Bot pose owner across cache/lifecycle edges."""
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(sys.platform == "darwin" and shutil.which("pyenv"),
                     "The macOS CPython 2.7 extension harness is required")
class NativePresentationTest(unittest.TestCase):
    def test_worker_pose_lifecycle(self):
        try:
            py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,
                stderr=subprocess.PIPE,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
        except (OSError, subprocess.CalledProcessError):
            self.skipTest('CPython 2.7.18 is required for the native extension harness')
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='bot-presentation-') as directory:
            folder=Path(directory)
            implementation=(ROOT/'bigworld/stock_native_bot_presentation.cpp').read_text()
            for name in ('stock_native_battle_remote.hpp','stock_native_battle_bots.hpp','stock_native_bot_runtime.hpp','stock_native_bot_services.hpp'):
                implementation=implementation.replace('#include "'+name+'"','#include "stock_native_bot_presentation_stub.hpp"')
            (folder/'stock_native_bot_presentation_impl.cpp').write_text(implementation)
            command=['c++','-std=c++17','-Wall','-Wextra','-Werror','-bundle','-undefined','dynamic_lookup','-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(folder),'-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'tests'),str(ROOT/'tests/stock_native_bot_presentation_host.cpp'),str(ROOT/'tests/stock_native_client_state_host.cpp'),'-o',str(folder/'presentation_test.so')]
            built=subprocess.run(command,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stderr)
            run=subprocess.run([py27,'-B','-c','import presentation_test;assert presentation_test.verify()'],cwd=folder,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
if __name__=='__main__':unittest.main()
