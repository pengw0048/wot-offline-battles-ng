"""Entry modes are explicit, process-local, and inherited by native worker children."""
import os
import sys
import types
import unittest
from unittest import mock
import wot_launcher as launcher


class DiagnosticEntryTests(unittest.TestCase):
    def test_normal_and_diagnostic_entrances_do_not_persist_a_previous_choice(self):
        with mock.patch.dict(os.environ, {'WOT_OFFLINE_DIAGNOSTICS':'1',
                'WOT_OFFLINE_SPIKE_PROFILE':'1',
                'WOT_OFFLINE_ROOF_CRUSH_TEST':'1',
                'WOT_OFFLINE_ROOF_SHOT_DROP':'1',
                'WOT_OFFLINE_REPLAY_FILE':''}), mock.patch.object(
                launcher, 'LauncherWindow') as window, mock.patch.dict(sys.modules,
                {'tkinter':types.SimpleNamespace(filedialog=None, ttk=None)}):
            self.assertEqual(0, launcher.main([]))
            self.assertEqual('0', os.environ['WOT_OFFLINE_DIAGNOSTICS'])
            self.assertEqual(0, launcher.main(['--diagnostics']))
            self.assertEqual('1', os.environ['WOT_OFFLINE_DIAGNOSTICS'])
            self.assertEqual(0, launcher.main([]))
            self.assertEqual('0', os.environ['WOT_OFFLINE_DIAGNOSTICS'])
            self.assertEqual(3, window.return_value.run.call_count)


    def test_removed_server_entry_cannot_start_a_python_server(self):
        with mock.patch.object(launcher, 'LauncherWindow') as window:
            self.assertEqual(2, launcher.main(['--serve', '0.9.22']))
            window.assert_not_called()


if __name__ == '__main__':
    unittest.main()
