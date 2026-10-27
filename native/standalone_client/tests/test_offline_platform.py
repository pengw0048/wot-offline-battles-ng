"""Fixed-release protocol audit and opt-in actual Win32 platform probe."""
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('install_exception_trail', 'hide_process_windows', 'show_process_windows')
# The independent client's own never-shown worker window has no fork counterpart.
LOCAL_NAMES = ('confirm_hidden_worker_window',)


class OfflinePlatformTests(unittest.TestCase):
    def test_fixed_release_protocol_and_platform_only_boundary(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('The fixed v0.10.1 source input is required')
        original = (Path(source) / 'native/offline_instance_guard_native.c').read_text()
        adapted = (ROOT / 'bigworld/stock_offline_platform.cpp').read_text()
        # Preserve real argument rejection and integer return boundaries. The
        # remaining four guard hooks belong to central native owners.
        for name in NAMES:
            entry = r'"' + name + r'"\s*,\s*' + name + r'\s*,\s*METH_NOARGS'
            self.assertRegex(original, entry)
            self.assertRegex(adapted, entry)
        table = adapted.split('static PyMethodDef methods[] = {', 1)[1].split('};', 1)[0]
        self.assertEqual(tuple(re.findall(r'\{\s*"([a-z_]+)"', table)), NAMES + LOCAL_NAMES)
        self.assertIn('#include "stock_offline_platform.hpp"', adapted)
        self.assertIn('#include <Python.h>', (ROOT / 'bigworld/stock_offline_platform.hpp').read_text())
        self.assertNotRegex(adapted, r'RVA_|EXPECTED_PE_|validate_host|VirtualProtect|FlushInstructionCache')
        self.assertNotIn('__attribute__', adapted)
        self.assertNotIn('initoffline_instance_guard_native', adapted)
        self.assertNotIn('WorldOfTanks.exe', adapted)

    def test_actual_embedded_win32_probe(self):
        executable = os.environ.get('WOT_OFFLINE_PLATFORM_PROBE')
        python_home = os.environ.get('WOT_EMBEDDED_PYTHON_HOME')
        if not executable or not python_home or sys.platform != 'win32':
            self.skipTest('An isolated Win32 probe and actual embedded Python home are required')
        result = subprocess.run([executable, python_home], capture_output=True,
                                text=True, timeout=60,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Actual embedded CPython 2.7.7 platform probe: 0 failures', result.stdout)
        self.assertIn('active-handler shutdown PASS', result.stdout)
        self.assertIn('generation / destroyed identity / rollback PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
