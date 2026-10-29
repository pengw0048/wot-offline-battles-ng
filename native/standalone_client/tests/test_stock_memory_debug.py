"""Run the patched SDK's memory diagnostic policy without replacing its decisions."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'memory_debug_patch', ROOT / 'bigworld/patch_stock_memory_debug.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def policy(source):
    start = source.index('#if ENABLE_MEMORY_DEBUG', source.index('void Allocator::init()'))
    end = source.index('#endif // ENABLE_MEMORY_DEBUG', start) + len('#endif // ENABLE_MEMORY_DEBUG')
    return source[start:end]


class MemoryDebugTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        engine = os.environ.get('WOT_BW_ENGINE_SOURCE', os.environ.get('WOT_BIGWORLD_ROOT',
            '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        source = Path(engine) / patcher.PATH
        if not source.is_file():
            raise unittest.SkipTest('Requires the pinned BigWorld engine source')
        cls.original = source.read_text()
        cls.patched = patcher.patch_text(cls.original)

    def test_exact_scope_idempotence_and_source_drift(self):
        restored = self.patched
        for before, after in patcher.SEAMS:
            restored = restored.replace(after, before, 1)
        self.assertEqual(restored, self.original)
        self.assertEqual(patcher.patch_text(self.patched), self.patched)
        mutations = [self.original.replace(*patcher.SEAMS[0], 1),
                     self.original.replace(*patcher.SEAMS[1], 1),
                     self.original.replace('void Allocator::init()', 'void changed()', 1),
                     self.original.replace('disabled = false;', 'disabled = unknown;', 1),
                     self.original + '\n' + patcher.DEFAULT,
                     self.original.replace(patcher.MODE, '', 1) + '\n' + patcher.MODE,
                     self.patched + '\n' + patcher.MARK]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / patcher.PATH
            path.parent.mkdir(parents=True)
            for text in mutations:
                with self.subTest(text=text[-80:]):
                    path.write_text(text)
                    before = path.read_bytes()
                    with self.assertRaises(ValueError):
                        patcher.patch(directory)
                    self.assertEqual(path.read_bytes(), before)

    def test_file_encoding_and_failed_replace_preserve_input(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / patcher.PATH
            path.parent.mkdir(parents=True)
            original = b'\xef\xbb\xbf' + self.original.replace('\n', '\r\n').encode()
            path.write_bytes(original)
            path.chmod(0o640)
            with mock.patch.object(patcher.os, 'replace', side_effect=OSError('fixture failure')):
                with self.assertRaises(OSError):
                    patcher.patch(directory)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(list(path.parent.iterdir()), [path])
            self.assertTrue(patcher.patch(directory))
            expected = b'\xef\xbb\xbf' + self.patched.replace('\n', '\r\n').encode()
            self.assertEqual(path.read_bytes(), expected)
            if os.name != 'nt':
                self.assertEqual(path.stat().st_mode & 0o777, 0o640)
            self.assertFalse(patcher.patch(directory))
            self.assertEqual(path.read_bytes(), expected)
            mixed = self.original.replace('\n', '\r\n', 1).encode()
            path.write_bytes(mixed)
            with self.assertRaises(ValueError):
                patcher.patch(directory)
            self.assertEqual(path.read_bytes(), mixed)

    def test_actual_sdk_policy_preserves_all_explicit_diagnostic_options(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            self.skipTest('Requires a C++ compiler')
        prefix = r'''
#include <cstring>
#include <iostream>
#include <string>
#define ENABLE_MEMORY_DEBUG 1
#define _WIN32 1
using LPSTR = char*;
std::string command;
LPSTR GetCommandLineA() { return &command[0]; }
bool dumpStats = false;
void setReportStatsToFile(bool value) { dumpStats = value; }
struct MemoryDebug {
    enum CallstackMode { CM_BW_GUARD, CM_CALLSTACKS_FAST, CM_CALLSTACKS_COMPLETE };
};
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    command = argv[1];
'''
        suffix = r'''
    std::cout << disabled << ' ' << callstackMode << ' ' << reportHotspots << ' '
              << reportIgnoredLeaks << ' ' << reportCacheGrind << ' ' << dumpStats
              << ' ' << debugGuard << ' ' << debugGuardString << '\n';
}
'''
        explicit = [('-memdebug=cs_guard', '0 0 0 0 0 0 0'),
                    ('-memdebug=cs_fast', '0 1 0 0 0 0 0'),
                    ('-memdebug=cs_complete', '0 2 0 0 0 0 0'),
                    ('-memhotspots', '0 0 1 0 0 0 0'),
                    ('-memreportignoredleaks', '0 0 0 1 0 0 0'),
                    ('-memreportcachegrind', '0 0 0 0 1 0 0'),
                    ('-memdumpstats', '0 0 0 0 0 1 0'),
                    ('-memdebugguard=RenderTest', '0 0 0 0 0 0 1 RenderTest')]
        common = explicit + [('-memdebug=disable', '1 0 0 0 0 0 0'),
            ('-memdebug=cs_complete -memdebug=cs_fast -memhotspots -memdumpstats '
             '-memreportignoredleaks -memreportcachegrind -memdebugguard=RenderTest',
             '0 1 1 1 1 1 1 RenderTest')]
        for argument, expected in explicit:
            disabled = '1' + expected[1:]
            common.extend([(argument + ' -memdebug=disable', disabled),
                           ('-memdebug=disable ' + argument, disabled)])
        for label, text, default in [('baseline', self.original, '0'), ('patched', self.patched, '1')]:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                cpp, binary = Path(directory) / 'host.cpp', Path(directory) / 'host'
                cpp.write_text(prefix + policy(text) + suffix)
                result = subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                         str(cpp), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                cases = common + [(argument, default + ' 0 0 0 0 0 0') for argument in
                                  ('client.exe', 'client.exe -noConversion -unattended',
                                   'client.exe -memdebug=unrecognized')]
                for argument, expected in cases:
                    with self.subTest(label=label, argument=argument):
                        run = subprocess.run([str(binary), argument], check=True,
                                             capture_output=True, text=True)
                        self.assertEqual(run.stdout.strip(), expected)


if __name__ == '__main__':
    unittest.main()
