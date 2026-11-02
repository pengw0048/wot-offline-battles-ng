"""Visible physical-barrel water gating and the existing admitted-shot lifecycle."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'Pinned gameplay source is required')
class NativeClientProjectile(unittest.TestCase):
    def test_physical_barrel_and_admitted_shot_bookkeeping(self):
        fork = Path(os.environ['WOT_FORK_0101_SOURCE'])
        names = ['stock_native_client_projectile', 'stock_native_client_weapon',
                 'stock_native_projectile_owner', 'stock_native_projectile_cursor',
                 'stock_native_projectile_history', 'stock_native_projectile_chord',
                 'stock_native_projectile_damage', 'stock_native_bot_armor_law', 'stock_native_bot_combat']
        with tempfile.TemporaryDirectory(prefix='native-client-projectile-') as temporary:
            binary = Path(temporary) / 'projectile'
            command = ['c++', '-std=c++17', '-O0', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-I' + str(ROOT / 'bigworld'), '-I' + str(fork / 'native'),
                       str(ROOT / 'tests/test_stock_native_client_projectile.cpp')]
            subprocess.run(command + [str(ROOT / 'bigworld' / (name + '.cpp')) for name in names] +
                           ['-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == '__main__':
    unittest.main()
