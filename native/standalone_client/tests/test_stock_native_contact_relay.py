"""The compact admitted-contact relay reaches the guarded native reader."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_stock_vehicle_physics_owner import block
from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]

PRODUCER = r'''
import sys
from pathlib import Path
fork=Path(sys.argv[1])
sys.path[:0]=[str(fork/'server'),str(fork/'tests'),str(fork/'src/res/scripts/client')]
from test_port_0922_server_projectiles import _state,_update_player_input,_player_destructible_contact
for case in ('active','not_participating','dead','no_admitted_pose'):
 state=_state(players=1);player=state.players[1];messages=[]
 state.simulation_worker.offer_reliable=lambda message:messages.append(message) or True
 if case=='not_participating':player.participating=False
 if case=='dead':player.alive=False
 if case=='no_admitted_pose':player.client_position=False
 changes={'destructible_contacts':[_player_destructible_contact()]}
 if case=='no_admitted_pose':changes.update(x=None,z=None)
 _update_player_input(state,1,**changes)
 relays=[message for message in messages if message['type']=='player_destructible_contact']
 assert len(relays)==(1 if case=='active' else 0),(case,relays)
 if relays:
  assert player.participating and player.client_position and player.pose_history
  assert set(relays[0]['player'])==set(('id','vehicle','vehicle_compact_descr','destructible_contacts'))
print('Pinned producer relays only admitted active world-pose contacts')
'''

NATIVE = r'''
import copy
import contact_relay_test as native
contact=dict(seq=1, token=[[7,9,None]], x=0., y=0., z=0., yaw=0.,
             end_x=0., end_y=0., end_z=.1, end_yaw=0., speed=2., dt=.05)
player=dict(id=1, vehicle='ussr:R11_MS-1', vehicle_compact_descr='test',
            effective_params={}, destructible_contacts=[contact])
message=dict(type='player_destructible_contact',protocol=5,round_id=7,
             authority_epoch=3,player=player)
before=copy.deepcopy(message)
result=native.relay(message)
assert result['calls']==1 and result['requests']==1,result
assert result['players'][0]['participating'] is True
assert result['players'][0]['world_pose'] is True
assert result['players'][0]['destructible_contacts']==[contact]
assert message==before,'The shared incoming relay was mutated'
assert native.relay(message,0)['calls']==0
assert native.relay(message,1,0)['calls']==0
for changes in (dict(round_id=6),dict(authority_epoch=2),dict(extra=True),
                dict(player=dict(player,id=0)),
                dict(player=dict(player,participating=False)),
                dict(player=dict(player,world_pose=False)),
                dict(player=dict(player,destructible_contacts=[]))):
 try:native.relay(dict(message,**changes))
 except ValueError:pass
 else:raise AssertionError('Malformed/stale relay accepted: %r'%changes)
print('Compact admitted-contact relay reaches guarded native reader')
'''


class ContactRelayTests(unittest.TestCase):
    def test_pinned_producer_requires_active_admitted_pose(self):
        fork = os.environ.get('WOT_FORK_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        result = subprocess.run([sys.executable, '-B', '-c', PRODUCER, fork],
                                text=True, capture_output=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_actual_relay_boundary_preserves_guards_and_reaches_reader(self):
        fork = os.environ.get('WOT_FORK_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        py27 = python27_audit_runtime()
        prefix = Path(subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip())
        source = (ROOT / 'bigworld/stock_native_battle_world.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='contact-relay-native-') as temporary:
            folder = Path(temporary)
            functions = [block(source, signature) for signature in (
                'std::optional<std::int64_t> integer(', 'Value boolean(',
                'Value owned(', 'bool exactKeys(', 'void destructibleContact(')]
            (folder / 'contact_relay_boundary.inc').write_text('\n'.join(functions))
            law = (Path(fork) / 'native/offline_release_drive_python.inc').read_text().split('PyObject *WOT_CDECL release_drive(')[0]
            (folder / 'offline_release_drive_law.hpp').write_text('#pragma once\n#include <algorithm>\n#include <cmath>\n' + law)
            command = ['c++', '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
                '-bundle', '-undefined', 'dynamic_lookup', '-isystem', str(prefix / 'include/python2.7'),
                '-I' + str(folder), '-I' + str(ROOT / 'bigworld'), '-I' + str(Path(fork) / 'native')]
            files = [ROOT / 'bigworld/stock_native_player_contacts_python.cpp',
                ROOT / 'tests/stock_native_contact_relay_host.cpp',
                ROOT / 'tests/stock_native_client_state_host.cpp']
            build = subprocess.run(command + list(map(str, files)) + ['-o', str(folder / 'contact_relay_test.so')],
                                   text=True, capture_output=True)
            self.assertEqual(0, build.returncode, build.stdout + build.stderr)
            result = subprocess.run([py27, '-B', '-c', NATIVE], cwd=folder, text=True, capture_output=True)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
