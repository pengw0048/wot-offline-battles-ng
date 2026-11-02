"""The raw receive enqueue is finished before detach can publish a replay footer."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
import imp,os,sys,types
sys.path.insert(0,sys.argv[1])
import offline_client_wire_native as wire
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
metadata=types.ModuleType('gui.mods.offline_lan_0922.offline_replay')
metadata.MAGIC='WOT_OFFLINE_REPLAY';metadata.SCHEMA=1;metadata.CLIENT='0.9.22.0.1-cn-1513'
metadata.EXTENSION='.wotlanreplay';metadata.MAX_DURATION=14400.
metadata.MESSAGE_TYPES=frozenset(('bot_observation',))
sys.modules[metadata.__name__]=metadata;sys.modules['gui.mods.offline_lan_0922'].offline_replay=metadata
replay=imp.load_source('native_replay',sys.argv[2])
header=dict(magic=metadata.MAGIC,schema=metadata.SCHEMA,client=metadata.CLIENT,
            welcome={},start={'type':'battle_start'},snapshot={},config={})
message=dict(type='bot_observation',round_id=1,contacts=[],radio_links=[])
for iteration in range(8):
 path=os.path.join(sys.argv[1],'race%d.wotlanreplay'%iteration)
 writer=wire.replay_writer(path);wire.replay_write(writer,0,0.,header)
 def footer(value):
  count=wire.replay_count(value)
  wire.replay_write(value,3,100.,{'records':count,'reason':'fixture'})
  return count
 trace=wire.detach_race(writer,wire.encode(message),footer)
 assert wire.replay_finish(writer)[0]
 reader=replay.NativeReader(path);rows=[];error=None
 try:
  while True:
   row=reader.next()
   if row is None:break
   rows.append(row)
 except ValueError as exception:error=str(exception)
 assert error is None,(trace,error)
 assert not trace['lease_escaped'] and trace['published_before_enqueue']==0,trace
 assert trace['footer_count']==wire.replay_count(writer)==1,trace
 assert [row['type'] for row in rows]==['wire','end'],rows
 assert rows[0]['data']==message and rows[1]['records']==1,rows
 reader.close()
print('Eight controlled native detach/footer races preserve frame order, exact count and clean EOF')
'''


class RecordingDetachTests(unittest.TestCase):
    def test_native_receiver_enqueue_precedes_detach_and_footer(self):
        if sys.platform == 'win32':
            self.skipTest('The controlled socket host uses the POSIX loopback API')
        py27 = python27_audit_runtime()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='wire-detach-') as directory:
            folder = Path(directory)
            link = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC', '-pthread']
            command = ['c++', '-std=c++17', '-O1', '-DWOT_WIRE_HOST_BUILD'] + link + [
                       '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld')]
            command += [str(ROOT / name) for name in ('tests/stock_wire_recording_detach_host.cpp',
                        'bigworld/stock_wire_codec.cpp', 'tests/stock_native_client_state_host.cpp',
                        'bigworld/stock_wire_replay.cpp', 'bigworld/stock_wire_replica_store.cpp')]
            command += ['-lz', '-o', str(folder / 'offline_client_wire_native.so')]
            completed = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            (folder / 'fixture.py').write_text(FIXTURE)
            completed = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder),
                                       str(ROOT / 'bigworld/stock_native_replay.py')], text=True, capture_output=True, timeout=30)
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            print(completed.stdout)


if __name__ == '__main__':
    unittest.main()
