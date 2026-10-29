"""The transport boundary preserves native one-shot ordering and retries."""
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


class ConsumerTests(unittest.TestCase):
    def fixture(self):
        trace = []
        result = dict(serial=7, player_id=2, seq=3, accepted=True,
                      token=[[10, 11, None]])
        native = SimpleNamespace(
            world_events=lambda *args: [(5, {'chunk_id': 10})],
            ack_world=lambda *args: trace.append(('world_ack', args[1])),
            player_contacts=lambda *args: [result],
            ack_player_contact=lambda *args: trace.append(('contact_ack', args[1])),
            close=lambda *args: trace.append(('close',)),
            player_environment=lambda *args: (9, [{'player_id': 2, 'level': 1}]),
            ack_player_environment=lambda *args: trace.append(('environment_ack', args[2], args[3])),
        )
        spec = importlib.util.spec_from_file_location('native_consumer_test',
            ROOT / 'bigworld/stock_native_bot_consumer.py')
        module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, offline_bot_runtime_native=native):
            spec.loader.exec_module(module)
        client = SimpleNamespace(
            send_destructible=lambda *args: trace.append(('world_send',)) or True,
            send_player_destructible_contact_result=lambda *args: trace.append(('contact_send',)) or True,
            is_bot_authority=lambda: True,
            send_player_environment=lambda *args: trace.append(('environment_send',)) or True,
        )
        battle = SimpleNamespace(_battle_live=True, _worker_mode=True, client=client)
        runtime = SimpleNamespace(_standalone_battle=battle, _native_bot_owner=object(),
                                  is_authority=lambda: True, adapter=object(), finished=False)
        return module, native, runtime, trace

    def test_world_transport_precedes_contact_terminal(self):
        module, unused, runtime, trace = self.fixture()
        self.assertEqual(module.player_contacts(runtime, [], 2.), 1)
        self.assertEqual(trace, [('world_send',), ('world_ack', 5),
                                 ('contact_send',), ('contact_ack', 7)])

    def test_failed_world_admission_retains_contact_and_world_receipts(self):
        module, unused, runtime, trace = self.fixture()
        runtime._standalone_battle.client.send_destructible = lambda event: False
        self.assertEqual(module.player_contacts(runtime, [], 2.), 0)
        self.assertEqual(trace, [])
        runtime._standalone_battle.client.send_destructible = lambda event: True
        self.assertEqual(module.player_contacts(runtime, [], 2.1), 1)
        self.assertEqual(trace, [('world_ack', 5), ('contact_send',), ('contact_ack', 7)])

    def test_countdown_does_not_admit_world_or_contact_events(self):
        module, unused, runtime, trace = self.fixture()
        runtime._standalone_battle._battle_live = False
        self.assertEqual(module.player_contacts(runtime, [], 2.), 0)
        self.assertEqual(trace, [])

    def test_failed_contact_admission_is_not_acknowledged(self):
        module, unused, runtime, trace = self.fixture()
        runtime._standalone_battle.client.send_player_destructible_contact_result = lambda *args: False
        self.assertEqual(module.player_contacts(runtime, [], 2.), 0)
        self.assertEqual(trace, [('world_send',), ('world_ack', 5)])

    def test_environment_ack_receives_actual_transport_result(self):
        module, unused, runtime, trace = self.fixture()
        self.assertTrue(module.player_environment(runtime, .3, 2.))
        self.assertEqual(trace, [('environment_send',), ('environment_ack', 9, 1)])
        trace[:] = []
        runtime._standalone_battle.client.send_player_environment = lambda *args: False
        self.assertFalse(module.player_environment(runtime, .3, 2.3))
        self.assertEqual(trace, [('environment_ack', 9, 0)])

    def test_retirement_is_idempotent(self):
        module, unused, runtime, trace = self.fixture()
        module.retire(runtime)
        module.retire(runtime)
        self.assertEqual(trace, [('close',)])
        self.assertIsNone(runtime._native_bot_owner)

    def test_result_preserves_native_owner_for_terminal_turret_tail(self):
        module, native, runtime, trace = self.fixture()
        owner = runtime._native_bot_owner
        native.snapshot = lambda *args: trace.append(('snapshot',))
        native.detached_advance = lambda *args: {'detached_turrets': [{'actor_id': 2}]}
        runtime._standalone_battle.client.send_bot_state = (
            lambda rows, **kwargs: trace.append(('tail', rows, kwargs)))
        self.assertTrue(module.snapshot(runtime, {'battle_result': {'winner': 1}}))
        self.assertTrue(runtime.finished)
        self.assertIs(runtime._native_bot_owner, owner)
        module.detached_advance(runtime, 4.)
        self.assertEqual(trace, [('snapshot',), ('tail', [], {
            'detached_turrets': [{'actor_id': 2}]})])
        module.retire(runtime)
        self.assertEqual(trace[-1], ('close',))

    def test_projectile_dispatch_preserves_order_and_actual_admission(self):
        module, native, runtime, trace = self.fixture()
        rows = [(10, 'resolve', {'projectile_id': 'first'}),
                (11, 'ricochet', {'projectile_id': 'second'}),
                (12, 'progress', {'cursors': []})]
        native.projectile_advance = lambda *args: rows
        native.projectile_ack = lambda owner, serial, sent: trace.append(('ack', serial, sent))
        client = runtime._standalone_battle.client
        client.send_projectile_resolve = lambda **wire: trace.append(('resolve', wire)) or False
        client.send_projectile_ricochet = lambda **wire: trace.append(('ricochet', wire)) or True
        client.send_projectile_progress = lambda **wire: trace.append(('progress', wire)) or True
        self.assertEqual(module.projectile_advance(runtime, 3.), 2)
        self.assertEqual(trace, [('resolve', rows[0][2]), ('ack', 10, 0),
            ('ricochet', rows[1][2]), ('ack', 11, 1),
            ('progress', rows[2][2]), ('ack', 12, 1)])

    def test_preinstall_snapshot_keeps_cold_lifecycle_owner(self):
        module, native, runtime, trace = self.fixture()
        runtime._native_bot_owner = None
        self.assertIsNone(module.detached_ingress(runtime, {}, 3.))
        self.assertIsNone(module.projectile_ingress(runtime, {}, 3.))
        self.assertFalse(module.snapshot(runtime, {}))
        self.assertEqual(trace, [])

    def test_one_transport_exception_preserves_sibling_dispatch(self):
        module, native, runtime, trace = self.fixture()
        native.projectile_advance = lambda *args: [(10, 'resolve', {}), (11, 'progress', {})]
        native.projectile_ack = lambda owner, serial, sent: trace.append(('ack', serial, sent))
        native.transport_error = lambda owner, raw, message: trace.append(('error', message))
        client = runtime._standalone_battle.client
        def reject(**wire):
            raise ValueError('invalid local dispatch')
        client.send_projectile_resolve = reject
        client.send_projectile_ricochet = reject
        client.send_projectile_progress = lambda **wire: trace.append(('progress',)) or True
        self.assertEqual(module.projectile_advance(runtime, 3.), 1)
        self.assertEqual(trace, [('error', 'invalid local dispatch'), ('ack', 10, 0),
            ('progress',), ('ack', 11, 1)])

    def test_native_launch_continuations_keep_ack_before_next_wave(self):
        module, native, runtime, trace = self.fixture()
        message = {'type': 'bot_state', 'launches': [7, 8]}
        waves = iter([[(7, 1, ['bot', 7, 1], {})], [(8, 3, ['bot', 8, 3], {})], []])
        native.projectile_launches = lambda owner, raw, incoming: (
            trace.append(('wave', incoming)) or next(waves))
        native.projectile_launch_result = lambda owner, bot, sequence, accepted: trace.append(('ack', bot, sequence, accepted))
        runtime._standalone_battle.client.send_projectile_launch = (
            lambda kind, bot, sequence, **kwargs: trace.append(('send', bot)) or (0 if bot == 7 else sequence))
        self.assertFalse(module.projectile_launches(runtime, message))
        self.assertEqual(trace, [('wave', message), ('send', 7), ('ack', 7, 1, 0),
            ('wave', None), ('send', 8), ('ack', 8, 3, 1), ('wave', None)])


if __name__ == '__main__':
    unittest.main()
