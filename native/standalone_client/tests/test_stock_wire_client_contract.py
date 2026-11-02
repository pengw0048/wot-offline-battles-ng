"""Inventory the shipped producers, rather than the retired Python battle path."""
import os
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'wire'))
from schema import SCHEMA  # noqa: E402
from codec import WireError  # noqa: E402
import gen_cpp  # noqa: E402


class ClientWireContract(unittest.TestCase):
    def test_generated_python_producer_types_are_registered(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('Pinned gameplay source is required')
        sys.path.insert(0, str(ROOT / 'bigworld'))
        import prepare_offline_fork
        path = prepare_offline_fork.NATIVE_MATH_PATH.with_name('lan_client.py')
        source = prepare_offline_fork.ADAPTERS[path]((Path(fork) / path).read_text())
        producers = gen_cpp.check_client(source)
        self.assertIn('select_vehicle', producers)
        self.assertNotIn('fire_intent', producers, 'Retired battle producers must stay retired')
        missing = {kind: sorted(methods) for kind, methods in producers.items()
                   if kind not in SCHEMA.messages}
        self.assertEqual({}, missing, 'Generated client producers missing from the binary schema')

    def test_native_control_and_worker_producer_types_are_registered(self):
        # These sites construct wire messages. Other native `type` tags also
        # describe local presentation commands and must not enter the wire enum.
        sources = {
            'stock_native_battle_link_send.cpp': r'(?:message\(|object\["type"\]\s*=\s*textV\()"([a-z_]+)"',
            'stock_native_battle_link.cpp': r'object\["type"\]\s*=\s*text\("([a-z_]+)"\)',
            'stock_native_client_input.cpp': r'p::set\([^;]*"type",\s*p::text\("([a-z_]+)"',
            'stock_native_client_input_runtime.cpp': r'p::set\([^;]*"type",\s*p::text\("([a-z_]+)"',
            'stock_native_client_landing.cpp': r'p::set\([^;]*"type",\s*p::text\("([a-z_]+)"',
            'stock_native_client_lan_poll.cpp': r'put\([^;]*"type",\s*p::text\("([a-z_]+)"',
            'stock_native_battle_projectiles_player.cpp': r'object\["type"\]\s*=\s*text\("([a-z_]+)"\)',
        }
        producers = {}
        for filename, pattern in sources.items():
            kinds = set(re.findall(pattern, (ROOT / 'bigworld' / filename).read_text()))
            self.assertTrue(kinds, 'Producer inventory no longer matches ' + filename)
            for kind in kinds:
                producers.setdefault(kind, []).append(filename)
        self.assertEqual({}, {kind: files for kind, files in producers.items()
                              if kind not in SCHEMA.messages})

    def test_detached_turret_tail_has_no_bot_motion_timestamp(self):
        # bots::detachedAdvance keeps turret proposals moving after battle_result.
        # The edgeless link sender then has no bot motion sample to publish.
        message = {'type': 'bot_state', 'round_id': 7, 'authority_epoch': 2,
                   'rows': [], 'detached_turrets': []}
        self.assertEqual(message, SCHEMA.decode(SCHEMA.encode(message)))

    def test_bot_motion_timestamp_is_optional_but_never_nullable_or_untyped(self):
        message = {'type': 'bot_state', 'round_id': 7, 'rows': [],
                   'sample_time_us': 1000, 'source_batch_horizon_us': 2000}
        self.assertEqual(message, SCHEMA.decode(SCHEMA.encode(message)))
        for invalid in (None, True, 1.5, '1000', 2 ** 63):
            with self.subTest(invalid=invalid), self.assertRaises(WireError):
                SCHEMA.encode(dict(message, sample_time_us=invalid))
        with self.assertRaises(WireError):
            SCHEMA.encode({'type': 'unregistered_control_message'})


if __name__ == '__main__':
    unittest.main()
