"""Exercise shipped server serialization with server-only planner diagnostics."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STAGED_TESTS = r'''
import copy
import sys
import unittest

sys.path[:0] = sys.argv[1:3]
import lan_battle_server as server
from battle_wire_codec import WireError
from battle_wire_schema import SCHEMA
from battle_wire_replica import encode as replica


class Endpoint(server._EndpointSendMixin):
    server_time_round_id = None
    last_server_time_ms = 0

    def __init__(self):
        self.failures = []

    def _record_outbound_failure(self, *args, **kwargs):
        self.failures.append((args, kwargs))


def orders():
    return [dict(id=9, combat_mode='route_transfer', route_id='valley',
                 route_index=2, route_join=True, fire_allowed=False,
                 throttle_override=.75, move_position=dict(x=258., y=0., z=254.),
                 target_kind='bot', target_id=20, shell_index=1,
                 route_assignment=dict(sequence=1, selection=dict(
                     contact_inputs=[dict(target_id=20, radio_bot_until={3:1005.349})])),
                 route_attempt=dict(action='physical_progress_renewed', witnesses={4:2}),
                 tactical_evidence=dict(final=dict(fire_allowed=False), inputs={5:3})),
            dict(id=3, combat_mode='engage', target_kind='bot', target_id=19,
                 fire_allowed=True, aim_position=[1.,2.,3.],
                 route_assignment={}, route_attempt=None, tactical_evidence={}),
            dict(id=8, combat_mode='route', route_id='ridge', route_index=4,
                 fire_allowed=False)]


def snapshot(rows):
    return dict(type='snapshot', protocol=5, server_tick=5401, round_id=1,
                state_revision=2, map='114_czech', bot_authority_id=-1,
                players=[], bots=[], bot_state_revision=679, bot_manifest=[],
                bot_order_revision=122, bot_orders=rows, rules=dict(bases={}),
                battle_result=None, destructible_revision=0, destructibles=[],
                timing=dict(phase='battle', start_in_ms=0,
                            remaining_ms=720000, duration_ms=900000),
                authority_epoch=1, server_time_ms=180000, motion_time_us=180000000,
                bot_state_time_us=179950000, projectile_revision=10,
                projectiles=[], detached_turrets=[], worker_status='connected')


class ProjectionTests(unittest.TestCase):
    def test_all_order_envelopes_preserve_commands_and_server_diagnostics(self):
        rows = orders()
        full = snapshot(rows)
        full_replica, baseline = replica(full, None, 1, True)
        delta_replica, unused = replica(dict(full, server_tick=5402), baseline, 1, False)
        loading = dict(full, client_build=server.CLIENT_BUILD_0922)
        loading.pop('timing')
        loading.pop('state_revision')
        envelopes = [full, full_replica, delta_replica, loading]
        for kind in ('battle_start', 'battle_live'):
            envelopes.append(dict(type=kind, round_id=1, authority_epoch=1,
                                  bot_order_revision=122, bot_orders=rows))
        expected = [{key:value for key,value in row.items()
                     if key not in ('route_attempt','route_assignment','tactical_evidence')}
                    for row in rows]
        for message in envelopes:
            with self.subTest(kind=message['type'], full=message.get('full'),
                              loading='client_build' in message):
                before = copy.deepcopy(message)
                endpoint = Endpoint()
                # Exercise the concurrent older-clock copy as well as the
                # normal path; neither may mutate server-owned order receipts.
                for previous_clock in (0, 180100):
                    endpoint.server_time_round_id = 1
                    endpoint.last_server_time_ms = previous_clock
                    payload = endpoint._serialize_message(message)
                    decoded = SCHEMA.decode(payload)
                    self.assertEqual(expected, decoded['bot_orders'])
                    self.assertEqual(122, decoded['bot_order_revision'])
                    self.assertEqual(1, decoded['round_id'])
                    self.assertEqual(1, decoded['authority_epoch'])
                    self.assertEqual(before, message)
                    self.assertEqual({3:1005.349},
                        rows[0]['route_assignment']['selection']['contact_inputs'][0]['radio_bot_until'])
                self.assertEqual([], endpoint.failures)

    def test_projection_is_not_a_general_integer_key_bypass(self):
        message = snapshot([dict(id=9, combat_mode='route',
                                 actual_command={3:'invalid'})])
        with self.assertRaisesRegex(WireError, 'non-string key'):
            Endpoint()._serialize_message(message)

    def test_empty_orders_and_messages_without_orders(self):
        for message in (snapshot([]), dict(type='worker_ping', player_id=1,
                         seq=4, client_time=2., round_id=1, authority_epoch=1)):
            self.assertEqual(message, SCHEMA.decode(Endpoint()._serialize_message(message)))


unittest.main(argv=['staged-bot-order-wire'])
'''


class BotOrderWireProjection(unittest.TestCase):
    def test_shipping_server_projection(self):
        fork = os.environ.get('WOT_FORK_SOURCE')
        if not fork:
            self.skipTest('WOT_FORK_SOURCE is required for the shipping staged server')
        sys.path.insert(0, str(ROOT / 'wire'))
        import stage_server
        with tempfile.TemporaryDirectory(prefix='bot-order-wire-staged-') as directory:
            staged = Path(directory) / 'server'
            client = Path(fork) / 'src/res/scripts/client'
            stage_server.stage(str(Path(fork) / 'server'), str(staged), str(client))
            result = subprocess.run(
                [sys.executable, '-B', '-c', STAGED_TESTS, str(staged), str(client)],
                text=True, capture_output=True, timeout=30)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
