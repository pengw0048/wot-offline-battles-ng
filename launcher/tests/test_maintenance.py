"""Maintenance must operate on NG instances without changing the source client."""
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import core
import save_slots


class MaintenanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.path.realpath(tempfile.gettempdir()))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.player = self.root / 'ng' / 'player'
        self.worker = self.root / 'ng' / 'worker'
        self.original = self.root / 'original'
        for path in (self.player, self.worker, self.original):
            path.mkdir(parents=True)
        env = mock.patch.dict(os.environ, {'APPDATA': str(self.root / 'appdata')})
        env.start()
        self.addCleanup(env.stop)
        guard = mock.patch.object(core, '_require_0_9_22_maintenance_target')
        guard.start()
        self.addCleanup(guard.stop)
        worker = mock.patch.object(core, 'worker_root', return_value=str(self.worker))
        worker.start()
        self.addCleanup(worker.stop)

    def put(self, path, text='old'):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_preferences_reset_is_limited_to_ng_instances(self):
        player = self.put(self.player / 'preferences.xml')
        worker = self.put(self.worker / 'preferences.xml')
        original = self.put(self.original / 'preferences.xml')
        saved = self.put(Path(save_slots.saves_root(str(self.player))) / 'custom/save.json')
        core.reset_offline_preferences(str(self.player), is_running=lambda: False)
        self.assertFalse(player.exists())
        self.assertFalse(worker.exists())
        self.assertTrue(original.exists())
        self.assertTrue(saved.exists())

    def test_failed_rebuild_restores_saves_and_config(self):
        saved = self.put(Path(save_slots.saves_root(str(self.player))) / 'custom/save.json')
        config = self.put(self.player / 'mods/configs/offline_lan_0922/config.json', '{"save_slot":"custom"}')
        with mock.patch.object(core, '_fresh_default_save', side_effect=OSError('disk full')):
            with self.assertRaisesRegex(core.LauncherError, 'disk full'):
                core.clear_offline_saves(str(self.player), is_running=lambda: False)
        self.assertEqual('old', saved.read_text())
        self.assertEqual('custom', core._read_json(str(config))['save_slot'])

    def test_successful_clear_preserves_ng_preferences_and_other_profiles(self):
        saved = self.put(Path(save_slots.saves_root(str(self.player))) / 'custom/save.json')
        prefs = self.put(self.player / 'preferences.xml')
        config = self.put(self.player / 'mods/configs/offline_lan_0922/config.json', '{"save_slot":"custom"}')
        core.clear_offline_saves(str(self.player), is_running=lambda: False)
        self.assertFalse(saved.exists())
        self.assertTrue(prefs.exists())
        self.assertEqual('default', core._read_json(str(config))['save_slot'])
        self.assertEqual('unlocked', save_slots.read_slot('default', str(self.player))['mode'])


if __name__ == '__main__':
    unittest.main()
