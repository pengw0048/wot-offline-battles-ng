"""Worker probe echoes the visible clock without comparing clock epochs."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_FORK_SOURCE')

@unittest.skipUnless(SOURCE, 'set WOT_FORK_SOURCE to the fixed gameplay checkout')
class WorkerProbeTest(unittest.TestCase):
    def test_worker_echoes_probe_scope_sequence_and_timestamp(self):
        py27=subprocess.check_output(['pyenv','which','python2.7'],text=True,env=dict(os.environ,PYENV_VERSION='2.7.18')).strip()
        prefix=subprocess.check_output([py27,'-c','import sys;print(sys.prefix)'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='worker-probe-') as directory:
            target=Path(directory)/'worker_probe_test.so'
            command=['c++','-std=c++17','-O1','-DWOT_NATIVE_BATTLE_HOST_BUILD','-DWOT_CLIENT_REPLICA_HOST_BUILD','-Wno-register',
                     '-bundle','-undefined','dynamic_lookup','-Wl,-dead_strip','-Wl,-exported_symbol,_initworker_probe_test',
                     '-isystem',str(Path(prefix)/'include/python2.7'),'-I'+str(ROOT/'bigworld'),
                     '-I'+str(ROOT.parent),'-I'+str(Path(SOURCE)/'native'),
                     str(ROOT/'tests/stock_native_worker_probe_host.cpp'),str(ROOT/'bigworld/stock_native_client_state.cpp'),str(ROOT/'bigworld/stock_wire_codec.cpp'),'-o',str(target)]
            subprocess.run(command,check=True,capture_output=True,text=True)
            script='''import worker_probe_test as native
request=dict(type='worker_ping',player_id=7,seq=42,round_id=12,authority_epoch=3,client_time=12345.625)
reply=native.reply(request)
assert reply==dict(type='worker_pong',player_id=7,seq=42,round_id=12,authority_epoch=3,client_time=12345.625,frame_ms=None),reply
'''
            result=subprocess.run([py27,'-B','-c',script],cwd=directory,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__':unittest.main()
