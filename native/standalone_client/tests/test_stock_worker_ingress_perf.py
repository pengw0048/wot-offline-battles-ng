"""Exercise worker ingress diagnostics in the actual staged shipping server."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STAGED_TESTS = r'''
import json
from pathlib import Path
import socket
import sys
import threading
from types import SimpleNamespace
import unittest

server_root, client_root = sys.argv[1:3]
sys.path[:0] = [server_root, client_root]
import lan_battle_server as server
from battle_wire_schema import SCHEMA


class Clock:
    wall = 0.0
    cpu = 0.0
    process = 0.0

    def advance(self, wall, cpu=0.0):
        self.wall += wall
        self.cpu += cpu


class IngressTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.logs = []
        self.old_log = server._server_log
        server._server_log = self.logs.append
        self.addCleanup(setattr, server, '_server_log', self.old_log)

    def perf(self):
        return server._WorkerIngressPerf(-1, lambda: self.clock.wall,
                                        lambda: self.clock.cpu,
                                        lambda: self.clock.process)

    def records(self):
        prefix = 'PERF worker_ingress '
        return [json.loads(line[len(prefix):]) for line in self.logs
                if line.startswith(prefix)]

    def test_actual_worker_loop_keeps_decode_dispatch_and_timeout_semantics(self):
        clock = self.clock
        frames = [SCHEMA.encode(dict(type='worker_pong', seq=n,
                    player_id=1, client_time=100.0+n, frame_ms=12.0,
                    round_id=1, authority_epoch=1)) for n in (1, 2)]
        # Invalid wire data exercises the existing decode exception path.
        invalid = bytes([0, 0, 0, 0, 255, 255, 255, 255])
        actions = [socket.timeout(), frames[0][:5],
                   frames[0][5:] + invalid + frames[1], b'']

        class Connection:
            def settimeout(self, value):
                self.timeout = value

            def recv(self, size):
                self.size = size
                clock.advance(5.1, .001)
                value = actions.pop(0)
                if isinstance(value, Exception):
                    raise value
                return value

        conn = Connection()
        worker = SimpleNamespace(worker_id=-1, connected=True, send=lambda m: True)
        state = SimpleNamespace(lock=threading.RLock(), simulation_worker=worker,
            phase='waiting', round_id=1, battle_result=None, players={},
            client_build='test', add_simulation_worker=lambda *args: (worker, None),
            broadcast=lambda message: None, lobby_message=lambda: {},
            remove_simulation_worker=lambda endpoint: (endpoint, False))
        handler = server.ClientHandler.__new__(server.ClientHandler)
        handler.client_address = ('127.0.0.1', 12345)
        handler._worker_welcome = lambda state, endpoint: {}
        dispatched = []

        def dispatch(game, endpoint, message):
            dispatched.append(message['seq'])
            clock.advance(.2, .1)
            if message['seq'] == 1:
                raise ValueError('contained handler error')
            return True

        handler._dispatch_simulation_worker_message = dispatch
        perf = self.perf()
        original = server._WorkerIngressPerf
        server._WorkerIngressPerf = lambda identity: perf
        try:
            handler._handle_simulation_worker(SimpleNamespace(state=state), conn, b'', {})
        finally:
            server._WorkerIngressPerf = original
        self.assertEqual([1, 2], dispatched)
        self.assertEqual([], actions)
        self.assertEqual(.5, conn.timeout)
        self.assertEqual(4096, conn.size)
        self.assertEqual(Path(server_root)/'lan_battle_server.py',
            Path(handler._handle_simulation_worker.__code__.co_filename))
        records = self.records()
        self.assertEqual(1, len(records))
        self.assertEqual(2, records[0]['recv']['calls'])
        self.assertEqual(1, records[0]['recv']['timeouts'])
        self.assertAlmostEqual(10200., records[0]['recv']['wait_wall_ms'])
        self.assertAlmostEqual(2., records[0]['recv']['wait_cpu_ms'])
        receive_lock = records[0]['types']['_receive_loop']
        self.assertEqual(2, receive_lock['lock_calls'])
        self.assertEqual(0., receive_lock['lock_wait_wall_ms'])
        self.assertNotIn('frames', receive_lock)
        # The interval rolls over before the following frames arrive.
        clock.advance(10.)
        perf.report()
        record = self.records()[-1]
        self.assertEqual(2, record['types']['_receive_loop']['lock_calls'])
        self.assertNotIn('frames', record['types']['_receive_loop'])
        row = record['types']['worker_pong']
        self.assertEqual(2, row['frames'])
        self.assertEqual(sum(map(len, frames)), row['bytes'])
        self.assertEqual(400., row['handler_wall_ms'])
        self.assertEqual(200., row['handler_wall_ms_max'])
        self.assertEqual(200., row['handler_cpu_ms'])
        self.assertEqual(100., row['handler_cpu_ms_max'])
        self.assertEqual(1, record['types']['invalid']['frames'])
        self.assertTrue(any('WORKER MESSAGE ignored type=worker_pong error=ValueError'
                            in line for line in self.logs))
        self.assertTrue(any('WORKER MESSAGE ignored type=invalid' in line
                            for line in self.logs))

    def test_metrics_are_bounded_reset_and_separate_decode_cpu(self):
        perf = self.perf()
        original = server._decode_frame

        def decode(frame):
            self.clock.advance(.03, .02)
            return {'type': 'bot_state'}

        server._decode_frame = decode
        try:
            self.assertEqual({'type': 'bot_state'}, perf.decode(b'abc'))
            perf.decode(b'12345')
        finally:
            server._decode_frame = original
        for index in range(100):
            perf._row('kind_%d' % index)
        self.assertLessEqual(len(perf.types), 32)
        perf.report()
        self.assertEqual([], self.records())
        self.clock.advance(10.)
        self.clock.process = 7.0
        perf.report()
        record = self.records()[0]
        self.assertEqual(7000., record['process_cpu_ms'])
        row = record['types']['bot_state']
        self.assertEqual(2, row['frames'])
        self.assertEqual(8, row['bytes'])
        self.assertEqual(60., row['decode_wall_ms'])
        self.assertEqual(30., row['decode_wall_ms_max'])
        self.assertEqual(40., row['decode_cpu_ms'])
        self.assertEqual(20., row['decode_cpu_ms_max'])
        self.assertEqual({}, perf.types)
        self.assertEqual({}, perf.recv_stats)
        perf.report()
        self.assertEqual(1, len(self.records()))
        self.clock.advance(10.)
        self.clock.process += 2.0
        perf.report()
        self.assertEqual(2000., self.records()[-1]['process_cpu_ms'])

    def test_actual_dispatch_measures_existing_lock_contention_without_replacing_lock(self):
        perf = self.perf()
        lock = threading.RLock()
        worker = SimpleNamespace(connected=True, _ng_ingress_perf=perf)
        state = SimpleNamespace(lock=lock, simulation_worker=worker,
            _message_round_matches=lambda message: True,
            resolve_worker_ping=lambda endpoint, message: True)
        handler = server.ClientHandler.__new__(server.ClientHandler)
        message = dict(type='worker_pong')
        entered = threading.Event()
        finished = threading.Event()
        result = []
        original = perf.lock_started

        def started():
            stamp = original()
            entered.set()
            return stamp

        perf.lock_started = started

        def run():
            try:
                result.append(perf.dispatch(handler._dispatch_simulation_worker_message,
                    SimpleNamespace(state=state), worker, message))
            except BaseException as error:
                result.append(error)
            finally:
                finished.set()

        with lock:
            thread = threading.Thread(target=run)
            thread.start()
            self.assertTrue(entered.wait(5.))
            self.assertFalse(finished.is_set())
            self.clock.advance(3.)
        self.assertTrue(finished.wait(5.))
        thread.join(5.)
        self.assertEqual([True], result)
        self.assertIs(lock, state.lock)
        self.clock.advance(10.)
        perf.report()
        row = self.records()[0]['types']['worker_pong']
        self.assertEqual(1, row['lock_calls'])
        self.assertEqual(3000., row['lock_wait_wall_ms'])
        self.assertEqual(3000., row['lock_wait_wall_ms_max'])
        self.assertEqual(0., row['lock_wait_cpu_ms'])
        self.assertEqual(3000., row['handler_wall_ms'])

unittest.main(argv=[sys.argv[0]], verbosity=2)
'''


class StagedWorkerIngressTests(unittest.TestCase):
    def test_shipping_worker_ingress_aggregates_without_changing_dispatch(self):
        fork = os.environ.get('WOT_FORK_SOURCE')
        if not fork:
            self.skipTest('WOT_FORK_SOURCE is required for the shipping staged server')
        sys.path.insert(0, str(ROOT / 'wire'))
        import stage_server
        with tempfile.TemporaryDirectory(prefix='worker-ingress-staged-') as directory:
            staged = Path(directory) / 'server'
            client = Path(fork) / 'src/res/scripts/client'
            stage_server.stage(str(Path(fork) / 'server'), str(staged), str(client))
            result = subprocess.run([sys.executable, '-B', '-c', STAGED_TESTS,
                str(staged), str(client)], text=True, capture_output=True, timeout=30,
                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
