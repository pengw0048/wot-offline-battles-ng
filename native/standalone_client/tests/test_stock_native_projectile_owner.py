"""Native projectile lifecycle and scene boundaries; no Windows claims."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(os.environ.get('WOT_FORK_0101_SOURCE'), 'fixed gameplay source is required')
class NativeProjectileOwner(unittest.TestCase):
    def test_lifecycle(self):
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        names=['stock_native_projectile_owner','stock_native_projectile_cursor','stock_native_projectile_history','stock_native_projectile_chord','stock_native_projectile_damage','stock_native_bot_armor_law','stock_native_bot_combat']
        with tempfile.TemporaryDirectory(prefix='native-projectile-owner-') as folder:
            binary=Path(folder)/'owner'
            command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native'),str(ROOT/'tests/test_stock_native_projectile_owner.cpp')]+[str(ROOT/'bigworld'/(name+'.cpp')) for name in names]+['-o',str(binary)]
            subprocess.run(command,check=True)
            subprocess.run([str(binary)],check=True)
    def test_scene(self):
        fork=Path(os.environ['WOT_FORK_0101_SOURCE'])
        names=['stock_native_projectile_scene','stock_native_projectile_cursor','stock_native_projectile_damage','stock_native_bot_armor_law','stock_native_bot_combat','stock_native_world_pipeline','stock_motion_rotation_batch','stock_offline_query_skin']
        with tempfile.TemporaryDirectory(prefix='native-projectile-scene-') as folder:
            binary=Path(folder)/'scene'
            command=['c++','-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'bigworld'),'-I'+str(fork/'native'),str(ROOT/'tests/test_stock_native_projectile_scene.cpp')]+[str(ROOT/'bigworld'/(name+'.cpp')) for name in names]+[str(fork/'native/offline_destructible_geometry.cpp'),str(fork/'native/world_stage.cpp'),'-o',str(binary)]
            subprocess.run(command,check=True)
            subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
