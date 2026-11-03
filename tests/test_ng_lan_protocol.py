"""Exercise the binary NG room against the pinned gameplay server."""
import importlib.util
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WIRE = ROOT / 'native/standalone_client/wire'
sys.path.insert(0, str(WIRE))
from schema import SCHEMA
import stage_server

SERVER = '''
import json, sys
from types import SimpleNamespace
sys.path.insert(0, sys.argv[1])
sys.path.insert(0, sys.argv[2])
import lan_battle_server as server
server._server_log = lambda message: print(message, file=sys.stderr, flush=True)
listener = server.ThreadedTCPServer(('127.0.0.1', 0), server.ClientHandler)
listener.game_server = SimpleNamespace(state=server.BattleState(team_size=2), vehicle_overlay=None)
print(json.dumps(dict(port=listener.server_address[1], protocol=server.PROTOCOL_VERSION,
    client_build=server.CLIENT_BUILD_0922,
    visible=list(server.MODERN_CLIENT_REQUIRED_CAPABILITIES),
    worker=list(server.SIMULATION_WORKER_REQUIRED_CAPABILITIES))), flush=True)
listener.serve_forever()
'''


SNAPSHOTS = r'''
import copy, json, sys
sys.path[:0] = sys.argv[1:4]
import lan_battle_server as server
from effective_params_fixture import effective_params
server._server_log = lambda message: None
class Connection:
    def sendall(self, payload): pass
state = server.BattleState(team_size=1)
worker, error = state.add_simulation_worker(Connection(), ('127.0.0.1', 1), dict(
    role='simulation_worker', client_build=server.CLIENT_BUILD_0922,
    capabilities=list(server.SIMULATION_WORKER_REQUIRED_CAPABILITIES)))
assert error is None, error
players = []
for index in (1, 2):
    player, error = state.add_player(Connection(), ('127.0.0.1', 1000 + index), dict(
        client_build=server.CLIENT_BUILD_0922,
        capabilities=list(server.MODERN_CLIENT_REQUIRED_CAPABILITIES),
        name='Player-%d' % index, account_key='snapshot_%d' % index,
        requested_team=index, vehicle='ussr:R11_MS-1', max_health=90,
        vehicle_compact_descr='dGVzdA==', effective_params=effective_params()))
    assert error is None, error
    players.append(player)
start, error = state.request_start(players[0].player_id, battle_mode='training')
assert error is None, error
state.phase = 'battle'
snapshots = []
def capture(message):
    if message['type'] == 'snapshot':
        snapshots.append(copy.deepcopy(message))
    return True
for endpoint in players + [worker]:
    endpoint.offer_snapshot = capture
    endpoint.offer_reliable = capture
for index in range(3):
    if index == 1:
        players[1].x += 3.5
        players[1].health -= 10
    if index == 2:
        players[1].connected = False
    state.tick_once(1.0 / server.TICK_HZ)
# Tick publication is cadence-limited for visible clients; the worker receives
# one complete canonical snapshot every tick.
by_tick = dict((message['server_tick'], message) for message in snapshots)
assert len(by_tick) == 3, by_tick.keys()
print(json.dumps([by_tick[key] for key in sorted(by_tick)]))
'''


class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(('127.0.0.1', port), timeout=5)
        self.socket.settimeout(5)
        self.buffer = b''

    def close(self):
        self.socket.close()

    def send(self, message):
        self.socket.sendall(SCHEMA.encode(message))

    def receive(self):
        while len(self.buffer) < 8:
            self._read()
        size = 8 + int.from_bytes(self.buffer[:4], 'little')
        while len(self.buffer) < size:
            self._read()
        frame, self.buffer = self.buffer[:size], self.buffer[size:]
        return SCHEMA.decode(frame)

    def _read(self):
        chunk = self.socket.recv(65536)
        if not chunk:
            raise EOFError('The NG room closed the connection')
        self.buffer += chunk

    def until(self, kind):
        for unused in range(30):
            message = self.receive()
            if message['type'] == kind:
                return message
            if message['type'] in ('error', 'start_denied'):
                raise AssertionError(message)
        raise AssertionError('Expected %s in the room stream' % kind)


class NGLanProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='ng-lan-protocol-')
        cls.addClassCleanup(cls.temporary.cleanup)
        base = Path(cls.temporary.name)
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if fork:
            cls.fork = Path(fork)
        else:
            spec = importlib.util.spec_from_file_location('ng_lan_stage', ROOT / 'tools/stage_ng_launcher_source.py')
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            archive = subprocess.run(['git', 'archive', module.pinned_revision(),
                'server', 'src', 'navgraphs', 'tests/effective_params_fixture.py'],
                cwd=ROOT, capture_output=True)
            cls.fork = base / 'fork'
            cls.fork.mkdir()
            if archive.returncode:
                module._copy_fork(None, module.pinned_revision(), cls.fork)
            else:
                with tarfile.open(fileobj=io.BytesIO(archive.stdout)) as source:
                    source.extractall(cls.fork, filter='data')
        cls.staged = base / 'server'
        stage_server.stage(str(cls.fork / 'server'), str(cls.staged),
                           str(cls.fork / 'src/res/scripts/client'))
        fixture = cls.fork / 'tests/effective_params_fixture.py'
        namespace = {}
        exec(compile(fixture.read_bytes(), str(fixture), 'exec'), namespace)
        cls.effective_params = staticmethod(namespace['effective_params'])

    def setUp(self):
        self.log = tempfile.TemporaryFile(mode='w+')
        self.addCleanup(self.log.close)
        environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
            WOT_OFFLINE_SEMANTIC_VERSION='0.1.0', WOT_OFFLINE_BUILD_IDENTITY='ng-test-build')
        self.server = subprocess.Popen([sys.executable, '-B', '-u', '-c', SERVER,
            str(self.staged), str(self.fork / 'src/res/scripts/client')],
            stdout=subprocess.PIPE, stderr=self.log, text=True, env=environment)
        self.addCleanup(self._stop_server)
        self.contract = json.loads(self.server.stdout.readline())

    def _stop_server(self):
        self.server.terminate()
        self.server.wait(timeout=10)
        self.server.stdout.close()

    def peer(self, role='probe', **fields):
        peer = Peer(self.contract['port'])
        self.addCleanup(peer.close)
        peer.send(dict(type='hello', role=role, protocol=self.contract['protocol'],
            client_build=self.contract['client_build'],
            capabilities=self.contract['worker' if role == 'simulation_worker' else 'visible'],
            **fields))
        return peer

    def test_two_human_tick_snapshots_split_for_each_recipient(self):
        import replica_split
        run = subprocess.run([sys.executable, '-B', '-c', SNAPSHOTS, str(self.staged),
            str(self.fork / 'src/res/scripts/client'), str(self.fork / 'tests')],
            text=True, capture_output=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(0, run.returncode, run.stderr)
        messages = json.loads(run.stdout)
        for own_id, remote_id in ((1, 2), (2, 1)):
            with self.subTest(own_id=own_id):
                baseline, actors = None, {}
                for index, message in enumerate(messages):
                    outgoing, baseline = replica_split.encode(message, baseline, own_id, False)
                    decoded = SCHEMA.decode(SCHEMA.encode(outgoing))
                    self.assertEqual(index == 0, decoded['full'])
                    own = next((p for p in message['players'] if p['id'] == own_id), None)
                    self.assertEqual(own, decoded.get('own'))
                    for key in decoded['removed_players']:
                        actors.pop(key)
                    for row in decoded['player_states']:
                        actors.setdefault(row['id'], {}).update(row)
                    for row in decoded['player_motion']:
                        actors.setdefault(row['id'], {}).update(row)
                    remote = next((p for p in message['players'] if p['id'] == remote_id), None)
                    if remote is None:
                        self.assertNotIn(remote_id, actors)
                    else:
                        self.assertEqual(remote['x'], actors[remote_id]['x'])
                        self.assertEqual(remote['health'], actors[remote_id]['health'])
                        self.assertEqual(remote['vehicle_compact_descr'],
                                         actors[remote_id]['vehicle_compact_descr'])
                self.assertEqual(set() if own_id == 1 else {1}, set(actors))
                next_round = dict(messages[0], round_id=messages[0]['round_id'] + 1)
                reset, unused = replica_split.encode(next_round, baseline, own_id, False)
                self.assertTrue(SCHEMA.decode(SCHEMA.encode(reset))['full'])

    def test_probe_advertises_the_packaged_ng_identity(self):
        welcome = self.peer().receive()
        self.assertEqual('welcome', welcome['type'])
        self.assertEqual(dict(schema=1, semanticVersion='0.1.0', buildIdentity='ng-test-build'),
                         welcome['ng_identity'])

    def test_probe_rejects_a_different_protocol(self):
        peer = Peer(self.contract['port'])
        self.addCleanup(peer.close)
        peer.send(dict(type='hello', role='probe', protocol=self.contract['protocol'] + 1,
            client_build=self.contract['client_build'], capabilities=self.contract['visible']))
        self.assertEqual('protocol', peer.receive()['code'])

    def test_two_players_join_one_worker_and_receive_the_same_round(self):
        worker = self.peer('simulation_worker')
        self.assertEqual(-1, worker.receive()['bot_authority_id'])
        players, welcomes = [], []
        for index in (1, 2):
            player = self.peer('player', name='Player-%d' % index,
                account_key='ng_test_%d' % index, requested_team=index,
                vehicle='ussr:R11_MS-1', max_health=90, outfits={}, marks_on_gun=0,
                vehicle_compact_descr='dGVzdA==', effective_params=self.effective_params())
            players.append(player)
            welcomes.append(player.receive())
        self.assertEqual([1, 2], [m['player_id'] for m in welcomes])
        self.assertEqual([1, 2], [m['team'] for m in welcomes])
        self.assertTrue(all(m['host_player_id'] == 1 for m in welcomes))
        roster = worker.until('roster')
        while len(roster['players']) < 2:
            roster = worker.until('roster')
        self.assertEqual([1, 2], [p['id'] for p in roster['players']])
        players[0].send(dict(type='start_battle', round_id=welcomes[0]['round_id'],
                             battle_mode='training', training_bots=False))
        try:
            starts = [peer.until('battle_start') for peer in players + [worker]]
        except (OSError, EOFError):
            self.log.seek(0)
            self.fail(self.log.read()[-6000:])
        self.assertEqual(1, len(set(message['round_id'] for message in starts)))
        for message in starts:
            self.assertEqual([1, 2], [p['id'] for p in message['players']])
            self.assertEqual(-1, message['bot_authority_id'])
            self.assertEqual('loading', message['phase'])


if __name__ == '__main__':
    unittest.main()
