"""Run the shipped staged server's probe methods through the actual wire codec."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STAGED_TESTS = r'''
import heapq
from pathlib import Path
import sys
import threading
from types import SimpleNamespace
import unittest

server_root, client_root = sys.argv[1:3]
sys.path[:0] = [server_root, client_root]
import lan_battle_server as server
from battle_wire_schema import SCHEMA
from gui.mods.offline_lan_0922.lan_client import LANClient


class Endpoint:
    connected = True
    accepted = True

    def __init__(self, identity=1):
        self.player_id = identity
        self.messages = []

    def offer_reliable(self, message):
        if not self.accepted:
            return False
        self.messages.append(SCHEMA.decode(SCHEMA.encode(message)))
        return True


class RelayTests(unittest.TestCase):
    def setUp(self):
        self.state = server.BattleState.__new__(server.BattleState)
        self.state.lock = threading.RLock()
        self.player, self.worker = Endpoint(), Endpoint(-1)
        self.state.players = {1:self.player}
        self.state.simulation_worker = self.worker
        self.state.round_id, self.state.authority_epoch = 7, 3
        self.assertEqual(Path(server_root)/'lan_battle_server.py',
                         Path(self.state.request_worker_ping.__code__.co_filename))

    def request(self, seq=1, stamp=100.):
        return self.state.request_worker_ping(self.player, dict(seq=seq,client_time=stamp))

    def reply(self, probe, worker=None, **changes):
        message = dict(probe, type='worker_pong', frame_ms=100.)
        message.update(changes)
        return self.state.resolve_worker_ping(worker or self.worker,
            SCHEMA.decode(SCHEMA.encode(message)))

    def test_slow_worker_replies_survive_120_seconds_of_new_probes(self):
        for delay, expected in ((10,119),(900,119),(1010,118),(2200,117)):
            with self.subTest(delay=delay):
                self.setUp()
                client = LANClient('localhost',28792,'Tester','ussr:R11_MS-1')
                client.connected = True
                client.round_id, client.authority_epoch = 7,3
                events = [(tick,0,tick//1000) for tick in range(1000,120000,1000)]
                heapq.heapify(events)
                while events:
                    when, kind, payload = heapq.heappop(events)
                    if when >= 120000:
                        continue
                    now = 100. + when/1000.
                    if kind == 0:
                        client._ping_seq = payload
                        self.assertTrue(self.request(payload,now))
                        probe = self.worker.messages[-1]
                        heapq.heappush(events,(when+delay,1,probe))
                    else:
                        self.assertTrue(self.reply(payload))
                        message = dict(self.player.messages[-1],_client_received_time=now)
                        client._handle_message(message)
                        self.assertAlmostEqual(client.worker_rtt_ms,float(delay),places=6)
                self.assertEqual(expected,len(self.player.messages))
                self.assertEqual(119,len(self.worker.messages))
                self.assertEqual((100,False),client.worker_ping_display(220.))
                self.assertFalse(hasattr(self.state,'worker_ping_pending'))

    def test_frozen_worker_then_recovery_preserves_original_stamps(self):
        client = LANClient('localhost',28792,'Tester','ussr:R11_MS-1')
        client.connected = True
        client.round_id, client.authority_epoch = 7,3
        queued = []
        for seq in range(1,14):
            self.assertTrue(self.request(seq,100.+seq))
            queued.append(self.worker.messages[-1])
        # Twelve seconds pass before its main loop runs; no probe is replaced.
        for probe in queued:
            self.assertTrue(self.reply(probe,frame_ms=12000.))
            client._handle_message(dict(self.player.messages[-1],_client_received_time=113.01))
        self.assertEqual(list(range(1,14)),[m['seq'] for m in self.player.messages])
        self.assertEqual([100.+n for n in range(1,14)],
                         [m['client_time'] for m in self.player.messages])
        self.assertTrue(self.request(14,114.))
        self.assertTrue(self.reply(self.worker.messages[-1]))
        self.assertEqual(100.,self.player.messages[-1]['frame_ms'])
        client._handle_message(dict(self.player.messages[-1],_client_received_time=114.01))
        self.assertEqual((100,False),client.worker_ping_display(114.02))

    def test_dropped_probe_and_rejected_offer_do_not_block_later_probes(self):
        self.worker.accepted = False
        self.assertFalse(self.request())
        self.worker.accepted = True
        self.assertTrue(self.request(2,102.))
        # Its reply is dropped. The next probe still completes independently.
        self.assertTrue(self.request(3,103.))
        self.assertTrue(self.reply(self.worker.messages[-1]))
        self.assertEqual([3],[m['seq'] for m in self.player.messages])
        self.player.accepted = False
        self.assertTrue(self.request(4,104.))
        self.assertFalse(self.reply(self.worker.messages[-1]))
        self.player.accepted = True
        self.assertTrue(self.request(5,105.))
        self.assertTrue(self.reply(self.worker.messages[-1]))
        self.assertEqual([3,5],[m['seq'] for m in self.player.messages])

    def test_worker_round_epoch_and_player_guards_are_preserved(self):
        self.assertTrue(self.request())
        probe = self.worker.messages[-1]
        self.assertFalse(self.reply(probe,worker=Endpoint(-2)))
        self.assertFalse(self.reply(probe,round_id=6))
        self.assertFalse(self.reply(probe,authority_epoch=2))
        for player_id in (True,1.5,'1',2):
            self.assertFalse(self.reply(probe,player_id=player_id))
        self.worker.connected = False
        self.assertFalse(self.reply(probe))
        self.assertFalse(self.request(2,101.))
        self.worker.connected = True
        self.player.connected = False
        self.assertFalse(self.reply(probe))
        self.player.connected = True
        del self.state.players[1]
        self.assertFalse(self.reply(probe))
        self.assertFalse(self.request(2,101.))
        self.assertEqual([],self.player.messages)

    def test_scope_or_worker_replacement_accepts_only_current_replies(self):
        self.assertTrue(self.request())
        old = self.worker.messages[-1]
        self.state.round_id += 1
        self.state.authority_epoch += 1
        previous = self.worker
        self.worker = self.state.simulation_worker = Endpoint(-2)
        self.assertFalse(self.reply(old,worker=previous))
        self.assertFalse(self.reply(old))
        self.assertTrue(self.request(2,102.))
        self.assertTrue(self.reply(self.worker.messages[-1]))
        self.assertEqual(8,self.player.messages[-1]['round_id'])
        self.assertEqual(4,self.player.messages[-1]['authority_epoch'])

    def test_players_with_equal_sequences_keep_their_own_timestamps(self):
        other = Endpoint(2)
        self.state.players[2] = other
        self.assertTrue(self.request(1,100.))
        self.assertTrue(self.state.request_worker_ping(other,dict(seq=1,client_time=200.)))
        for probe in self.worker.messages:
            self.assertTrue(self.reply(probe))
        self.assertEqual(100.,self.player.messages[-1]['client_time'])
        self.assertEqual(200.,other.messages[-1]['client_time'])

    def test_request_validation_and_frame_sanitization_are_unchanged(self):
        for seq, stamp in ((True,100.),(0,100.),(2147483648,100.),
                           (1,False),(1,0.),(1,float('nan')),(1,float('inf')),(1,'100')):
            self.assertFalse(self.request(seq,stamp))
        stale = Endpoint(1)
        self.assertFalse(self.state.request_worker_ping(stale,dict(seq=1,client_time=100.)))
        self.assertEqual([],self.worker.messages)
        for seq, frame in enumerate((None,True,0.,float('nan'),float('inf'),-1.),1):
            self.assertTrue(self.request(seq,100.+seq))
            self.assertTrue(self.reply(self.worker.messages[-1],frame_ms=frame))
            self.assertIsNone(self.player.messages[-1]['frame_ms'])


unittest.main(argv=[sys.argv[0]],verbosity=2)
'''


class StagedWorkerPingTests(unittest.TestCase):
    def test_shipping_relay_does_not_replace_delayed_probes(self):
        fork = os.environ.get('WOT_FORK_SOURCE')
        if not fork:
            self.skipTest('WOT_FORK_SOURCE is required for the shipping staged server')
        sys.path.insert(0,str(ROOT / 'wire'))
        import stage_server
        with tempfile.TemporaryDirectory(prefix='worker-ping-staged-') as directory:
            folder = Path(directory)
            staged = folder / 'server'
            client = Path(fork) / 'src/res/scripts/client'
            stage_server.stage(str(Path(fork)/'server'),str(staged),str(client))
            result = subprocess.run([sys.executable,'-B','-c',STAGED_TESTS,str(staged),str(client)],
                                    text=True,capture_output=True,timeout=30)
            self.assertEqual(0,result.returncode,result.stdout+result.stderr)


if __name__ == '__main__':
    unittest.main()
