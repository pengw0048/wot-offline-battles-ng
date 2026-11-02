"""Accepted round barriers precede recording, and replay keeps their phase contract."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_0101_SOURCE')
START_FIXTURE = r'''
import copy,os,sys,types
import replay_boundary_test as native
class O(object):
 def __init__(self,**kw):self.__dict__.update(kw)
for name in ('gui','gui.mods','gui.mods.offline_lan_0922'):
 module=types.ModuleType(name);module.__path__=[];sys.modules[name]=module
replay=types.ModuleType('gui.mods.offline_lan_0922.offline_replay')
replay.CONFIG_KEYS=('map',)
replay.poll_process_status=lambda client:None
replay.capture_message=lambda value:dict(value)
replay.native_recording_mode=lambda:1
replay.log=lambda text:None
replay.os=os
for name,attrs in (
 ('offline_replay',replay.__dict__),
 ('replay_presentation',dict(descriptor_contract=lambda value:{})),
 ('replay_resources',dict(active_signature=lambda root:{}))):
 module=replay if name=='offline_replay' else types.ModuleType('gui.mods.offline_lan_0922.'+name)
 module.__dict__.update(attrs);sys.modules[module.__name__]=module
 setattr(sys.modules['gui.mods.offline_lan_0922'],name,module)
for owner in ('startup','ensure_recording'):
 calls=[]
 def recorder(directory,mode,header,clock):
  calls.append(copy.deepcopy(header))
  return O(path='recorded',attach=lambda client:calls.append('attach'))
 module=types.ModuleType('gui.mods.offline_lan_0922.native_replay');module.NativeRecorder=recorder
 sys.modules[module.__name__]=module;sys.modules['gui.mods.offline_lan_0922'].native_replay=module
 client=O(player_id=4,_offline_replay_welcome={'type':'welcome'},_offline_replay_live=None,
          _offline_replay_attempted=None,_offline_replay_recorder=None,is_offline_replay=False,
          last_snapshot={'type':'snapshot','round_id':7,'players':[],'bots':[],'revision':1})
 runtime=O(client=client,state='running',_worker_mode=False,_client_ready_received=True,_ready_sent=True,
           _start_message={'type':'battle_start','round_id':7},_config={'map':'01_karelia'},_local_descriptor=object(),
           _clock=lambda:1.,_garage_loadout_snapshot=lambda:{'crew_group':0},bigworld=O(time=lambda:1.))
 def invoke():
  if owner=='startup':native.startup(runtime)
  else:native.ensure_recording(runtime,replay.__dict__)
 for live in (None,{'type':'battle_live','round_id':6},{'type':'other','round_id':7}):
  client._offline_replay_live=live;invoke()
  assert client._offline_replay_attempted is None and not calls,(owner,live,calls)
 client._offline_replay_live={'type':'battle_live','round_id':7,'countdown_seconds':10.}
 for snapshot in (None,{'type':'snapshot','round_id':6}):
  client.last_snapshot=snapshot;invoke()
  assert client._offline_replay_attempted is None and not calls,(owner,snapshot,calls)
 client.last_snapshot={'type':'snapshot','round_id':7,'players':[],'bots':[],'revision':9}
 runtime.state='stopped';invoke()
 assert client._offline_replay_attempted is None and not calls,(owner,calls)
 runtime.state='running';invoke();invoke()
 assert client._offline_replay_attempted==7 and len(calls)==2 and calls[1]=='attach',(owner,calls)
 assert calls[0]['live']==client._offline_replay_live and calls[0]['snapshot']['revision']==9,(owner,calls)
print('Both native recording owners wait for current accepted live and latest snapshot exactly once')
'''


TRANSPORT_FIXTURE = r'''
import json,os,socket,sys,threading,time,types
sys.path.insert(0,sys.argv[1])
root=sys.argv[2]
for name in ('gui','gui.mods'):
 module=types.ModuleType(name);module.__path__=[root+'/'+name.replace('.','/')];sys.modules[name]=module
package=types.ModuleType('gui.mods.offline_lan_0922');package.__path__=[sys.argv[3],root+'/gui/mods/offline_lan_0922'];sys.modules[package.__name__]=package
sys.modules['offline_client_lifecycle_native']=types.ModuleType('offline_client_lifecycle_native')
import offline_client_wire_native as wire
from gui.mods.offline_lan_0922 import lan_client as lan,native_replay,replay_resources
replay_resources.verify=lambda signature,root:False
# Use the final staged admission method over the pinned module's unchanged
# helpers. Native snapshot ownership is covered separately; no snapshot is
# bypassed or synthesized in these barrier/observation messages.
environment=lan.__dict__
exec(compile(open(sys.argv[4],'rb').read(),sys.argv[4],'exec'),environment)
lan.LANClient._handle_message=environment['_staged_handle_message']
from gui.mods.offline_lan_0922.replay_transport import ReplayClient
spec=json.load(open(sys.argv[5]))

def recorded(name,header_live,frames):
 path=os.path.join(sys.argv[1],name+'.wotlanreplay')
 writer=wire.replay_writer(path)
 header=dict(spec['header'],live=header_live)
 wire.replay_write(writer,0,0.,header)
 listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1)
 def server():
  conn,unused=listener.accept();pending=''
  while len(pending)<8 or len(pending)<wire.frame_size(pending[:8]):pending+=conn.recv(4096)
  conn.sendall(''.join(frame.decode('hex') for frame in frames))
  conn.recv(4096);conn.close()
 thread=threading.Thread(target=server);thread.start()
 handle=wire.net_open('127.0.0.1',listener.getsockname()[1],3.)
 wire.net_record(handle,writer,0.)
 assert wire.net_send(handle,wire.encode({'type':'hello'}))
 count=0;deadline=time.time()+10.
 while (count<len(frames) or wire.replay_count(writer)<len(frames)) and time.time()<deadline:
  batch,state,error=wire.net_receive(handle,256);count+=len(batch)
  if state=='failed':raise AssertionError(error)
  if count<len(frames) or wire.replay_count(writer)<len(frames):time.sleep(.001)
 assert count==len(frames) and wire.replay_count(writer)==len(frames)
 wire.net_record(handle,None)
 wire.replay_write(writer,3,100.,{'records':len(frames),'reason':'fixture'})
 assert wire.replay_finish(writer)[0]
 wire.net_send(handle,wire.encode({'type':'leave'}));wire.net_close(handle,.5)
 thread.join(10);assert not thread.is_alive();listener.close()
 return path

def consumer(path):
 client=ReplayClient('127.0.0.1',28782,'Replay','ussr:R11_MS-1',replay_path=path)
 client.round_id=1;client.phase='loading';client.ready=client.running=client.connected=True
 client.player_id=1;client.authority_epoch=1
 delivered=[]
 client._notify=lambda kind,message,**kwargs:delivered.append((kind,message))
 if client.reader.header.get('live') is not None:client._feed(client.reader.header['live'])
 return client,delivered

for name,header_live,frames in (
 ('header',spec['live'],spec['observations']),
 ('later',None,[spec['live_frame']]+spec['observations']),
 ('missing',None,spec['observations'])):
 path=recorded(name,header_live,frames)
 client,delivered=consumer(path);observed=[];error=None
 try:
  while True:
   row=client.reader.next()
   if row is None or row['type']=='end':break
   assert row['type']=='wire'
   client._feed(row['data'])
   if row['data']['type']=='bot_observation':observed.append(row['data'])
 except ValueError as exception:error=str(exception)
 if name=='missing':
  assert error and 'missing the battle-live barrier' in error,error
  assert client.phase=='loading' and not observed
 else:
  assert error is None and client.phase=='battle' and len(observed)==len(spec['observations']),(name,error,client.phase)
  assert [row['contacts'] for row in observed]==spec['contacts']
  bad=dict(observed[-1],contacts=[dict(observed[-1]['contacts'][0],visible_by_bot_ids=['invalid'])])
  try:client._feed(bad)
  except ValueError as rejected:assert 'invalid_contacts' in str(rejected) and 'missing' not in str(rejected),rejected
  else:raise AssertionError('invalid live contact accepted')
 client.stop()

# The original reported recording has no live barrier. Validate every actual
# NativeReader observation against the same final admission with the live
# phase controlled independently; this proves contact representations do not
# cause the report's early failure, without repairing or ignoring its records.
if sys.argv[6]:
 reader=native_replay.NativeReader(sys.argv[6]);client,delivered=consumer(sys.argv[6])
 assert reader.header.get('live') is None
 first=None;count=nonempty=0
 while True:
  row=reader.next()
  if row is None or row['type']=='end':break
  if row['type']!='wire' or row['data'].get('type')!='bot_observation':continue
  if first is None:first=row
  client.phase='battle';client._battle_live_round_id=1
  expected=row['data']['contacts'];client._feed(row['data'])
  assert delivered[-1][1]['contacts']==expected
  count+=1;nonempty+=bool(expected)
 assert count and nonempty and first['data']['contacts']==[]
 client.phase='loading';client._battle_live_round_id=None
 try:client._feed(first['data'])
 except ValueError as error:assert 'missing the battle-live barrier' in str(error),error
 else:raise AssertionError('missing barrier accepted')
 reader.close();client.stop()
 print('actual replay native contact parity count=%d nonempty=%d first_t=%.9f'%(count,nonempty,first['t']))
print('Native raw recording, NativeReader and staged replay preserve current/later/missing live boundaries')
'''


@unittest.skipUnless(SOURCE, 'fixed gameplay source is required')
class NativeReplayBoundaryTests(unittest.TestCase):
    def test_recording_waits_for_accepted_live_of_this_round(self):
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='native-replay-boundary-') as directory:
            folder = Path(directory)
            command = ['c++', '-std=c++17', '-O1', '-DWOT_NATIVE_BATTLE_HOST_BUILD', '-DWOT_CLIENT_REPLICA_HOST_BUILD',
                       '-DWOT_CLIENT_LIFECYCLE_HOST_BUILD', '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup',
                       '-Wl,-dead_strip', '-Wl,-exported_symbol,_initreplay_boundary_test', '-isystem',
                       str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'),
                       '-I' + str(Path(SOURCE) / 'native'), str(ROOT / 'tests/stock_native_replay_boundary_host.cpp'),
                       str(ROOT / 'bigworld/stock_native_client_state.cpp'), '-o', str(folder / 'replay_boundary_test.so')]
            completed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            completed = subprocess.run([py27, '-B', '-c', START_FIXTURE], cwd=folder, capture_output=True, text=True)
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)

    def test_native_reader_and_final_transport_live_barriers(self):
        import ast
        import json
        import sys
        import textwrap
        sys.path.insert(0, str(ROOT / 'bigworld'))
        sys.path.insert(0, str(ROOT / 'wire'))
        from prepare_offline_fork import NATIVE_MODULE_ROOT, RUNTIME_ROOT, stage_runtime
        from schema import SCHEMA
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
                                      env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='replay-barrier-wire-') as directory:
            folder = Path(directory)
            stage_runtime(Path(SOURCE), folder / 'prepared')
            package = folder / 'prepared' / RUNTIME_ROOT / NATIVE_MODULE_ROOT
            (folder / 'pkg').mkdir()
            for name in ('native_replay.py', 'replay_transport.py', 'offline_replay.py'):
                (folder / 'pkg' / name).write_bytes((package / name).read_bytes())
            source = (package / 'lan_client.py').read_text()
            parent = next(node for node in ast.parse(source).body if isinstance(node, ast.ClassDef) and node.name == 'LANClient')
            node = next(node for node in parent.body if isinstance(node, ast.FunctionDef) and node.name == '_handle_message')
            method = textwrap.dedent('\n'.join(source.splitlines()[node.lineno - 1:node.end_lineno]))
            (folder / 'admission.py').write_text(method.replace('def _handle_message(', 'def _staged_handle_message(', 1) + '\n')
            header = dict(magic='WOT_OFFLINE_REPLAY', schema=1, client='0.9.22.0.1-cn-1513', config={},
                          welcome={'type': 'welcome'}, start={'type': 'battle_start', 'round_id': 1, 'players': [], 'bots': []},
                          snapshot={'type': 'snapshot', 'round_id': 1, 'players': [], 'bots': []})
            live = dict(type='battle_live', protocol=5, round_id=1, state_revision=4, bot_authority_id=-1,
                        authority_epoch=1, countdown_seconds=10., battle_duration_seconds=900., server_tick=1,
                        timing=dict(phase='prebattle', start_in_ms=10000, remaining_ms=900000, duration_ms=900000))
            # The reported file's first contact is empty; a later remembered Bot
            # uses the native ten-second memory and keeps every radio recipient.
            contact = dict(observing_team=1, target_kind='bot', target_id=30, target_team=2, visible=True,
                           fresh=False, time_left=9.681, visible_by_bot_ids=[], visible_by_player_ids=[],
                           shootable_by_bot_ids=[], threatened_bot_ids=[],
                           radio_recipients=[{'kind': 'human', 'id': 1, 'time_left': 9.681}])
            messages = [dict(type='bot_observation', protocol=5, round_id=1, contacts=rows, radio_links=[])
                        for rows in ([], [contact])]
            spec = dict(header=header, live=live, live_frame=SCHEMA.encode(live).hex(), contacts=[m['contacts'] for m in messages],
                        observations=[SCHEMA.encode(message).hex() for message in messages])
            (folder / 'spec.json').write_text(json.dumps(spec))
            command = ['c++', '-std=c++17', '-O1', '-DWOT_WIRE_HOST_BUILD', '-bundle', '-undefined', 'dynamic_lookup',
                       '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(ROOT / 'bigworld')]
            command += [str(ROOT / path) for path in ('bigworld/stock_wire_codec.cpp', 'tests/stock_native_client_state_host.cpp',
                        'bigworld/stock_wire_replay.cpp', 'bigworld/stock_wire_net.cpp', 'bigworld/stock_wire_replica_store.cpp',
                        'tests/stock_wire_host_client_module.cpp')]
            command += ['-lz', '-o', str(folder / 'offline_client_wire_native.so')]
            completed = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            (folder / 'fixture.py').write_text(TRANSPORT_FIXTURE)
            completed = subprocess.run([py27, '-B', str(folder / 'fixture.py'), str(folder), str(Path(SOURCE) / 'src/res/scripts/client'),
                                        str(folder / 'pkg'), str(folder / 'admission.py'), str(folder / 'spec.json'),
                                        os.environ.get('WOT_TEST_REPLAY', '')], text=True, capture_output=True, timeout=60)
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            print(completed.stdout)


if __name__ == '__main__':
    unittest.main()
