"""Battle FPS uses the real per-frame statistics owner and original consumer."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from test_stock_debug_queries import ROOT, build_host, engine_sources


class StockBattleFpsTests(unittest.TestCase):
    def test_original_sampler_and_notify_controller_match_actual_owner(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Exact #1513 client is required')
        try:
            from stock_battle_fps_oracle import OriginalBattleFps, frame_actions, GET_BATTLE_FPS
        except ImportError:
            self.skipTest('pefile and Unicorn are required')
        actions = frame_actions()
        original = OriginalBattleFps(Path(client) / 'WorldOfTanks.exe')
        for argc in (1, 2):
            self.assertEqual(original.wrapper(GET_BATTLE_FPS, 'getBattleFPS', argc),
                             ['getBattleFPS', argc, 'NULL',
                              [['argCountError', 'getBattleFPS', 0, 0]]])
        expected = original.actions(actions)
        with tempfile.TemporaryDirectory(prefix='battle-fps-owner-') as folder:
            path = Path(folder)
            binary = build_host(self, path, engine_sources(self))
            (path / 'actions.json').write_text(json.dumps(actions))
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_battle_fps_consumer.py'),
                                     str(path / 'actions.json'), str(Path(client) / 'res/packages/scripts.pkg')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            actual = json.loads(result.stdout)
        self.assertEqual(len(actual), len(expected))
        for index, (ours, theirs) in enumerate(zip(actual, expected)):
            self.assertEqual([struct.pack('<f', value) for value in ours],
                             [struct.pack('<f', value) for value in theirs], index)
        self.assertGreater(len(actual), 50)
