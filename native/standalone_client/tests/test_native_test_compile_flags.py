"""Keep optional sanitizer detection separate from actual test compilation."""
from contextlib import redirect_stderr
import io
from pathlib import Path
import subprocess
import unittest
from unittest import mock

import native_test_compile_flags as flags


class NativeTestCompileFlagsTests(unittest.TestCase):
    def tearDown(self):
        flags._windows_runtime_flags.cache_clear()
        flags._sanitizers_available.cache_clear()

    def test_posix_keeps_instrumentation_without_a_probe(self):
        with mock.patch.object(flags.sys, 'platform', 'linux'), mock.patch.object(flags.subprocess, 'run') as run:
            self.assertEqual(flags.native_test_compile_flags(), ['-fsanitize=address,undefined'])
            self.assertEqual(flags.native_test_compile_flags(sanitise=False), [])
            run.assert_not_called()

    def test_missing_mingw_sanitizer_keeps_actual_test_build_and_diagnostics(self):
        commands = []
        def run(command, **kwargs):
            commands.append(command)
            if '-dumpmachine' in command:
                return subprocess.CompletedProcess(command, 0, 'x86_64-w64-mingw32\n', '')
            if 'native-test-sanitizer-' in str(command):
                source = Path(command[command.index('-o')-1]).read_text()
                self.assertEqual(source, 'int main() { return 0; }\n')
                return subprocess.CompletedProcess(command, 1, '', 'cannot find -lasan')
            raise subprocess.CalledProcessError(7, command, stderr='actual source error')
        diagnostic = io.StringIO()
        with mock.patch.object(flags.sys, 'platform', 'win32'), mock.patch.object(flags.subprocess, 'run', side_effect=run), redirect_stderr(diagnostic):
            selected = flags.native_test_compile_flags()
            self.assertEqual(selected, ['-static-libgcc', '-static-libstdc++'])
            self.assertEqual(flags.native_test_compile_flags(), selected)
            with self.assertRaises(subprocess.CalledProcessError):
                subprocess.run(['c++', *selected, 'actual_test.cpp'], check=True)
        self.assertIn('compile exit 1', diagnostic.getvalue())
        self.assertIn('cannot find -lasan', diagnostic.getvalue())
        self.assertEqual(len(commands), 3)  # target, cached capability probe, actual failure; no retry

    def test_missing_runtime_dll_is_reported_and_supported_runtime_is_retained(self):
        for exit_code, supported in [(3221225781, False), (0, True)]:
            with self.subTest(exit_code=exit_code):
                flags._sanitizers_available.cache_clear()
                results = [subprocess.CompletedProcess([], 0, '', ''),
                           subprocess.CompletedProcess([], exit_code, '', 'runtime detail')]
                diagnostic = io.StringIO()
                with mock.patch.object(flags.subprocess, 'run', side_effect=results) as run, redirect_stderr(diagnostic):
                    self.assertEqual(flags._sanitizers_available('clang++', flags.SANITIZERS), supported)
                    self.assertEqual(flags._sanitizers_available('clang++', flags.SANITIZERS), supported)
                    self.assertEqual(run.call_count, 2)
                if not supported:
                    self.assertIn('execute exit 3221225781', diagnostic.getvalue())
                    self.assertIn('runtime detail', diagnostic.getvalue())

    def test_windows_available_clang_instruments_without_mingw_runtime_flags(self):
        with mock.patch.object(flags.sys, 'platform', 'win32'), mock.patch.object(flags.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, 'x86_64-pc-windows-msvc\n', '')), mock.patch.object(flags, '_sanitizers_available', return_value=True):
            self.assertEqual(flags.native_test_compile_flags('clang++'), list(flags.SANITIZERS))
