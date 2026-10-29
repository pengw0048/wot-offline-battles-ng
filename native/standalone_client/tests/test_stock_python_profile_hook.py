"""The #1513 engine does not automatically install its Python profile hook."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'profile_hook_patch', ROOT / 'bigworld/patch_stock_python_profile_hook.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for i in range(opening, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if depth == 0:
            return source[start:i + 1]
    raise ValueError('Unterminated function')


def init_block(source):
    begin = source.index('bool Script::init(')
    end = source.index('// Run any init time jobs', begin)
    start = source.rindex('#if ENABLE_STACK_TRACKER || ENABLE_PROFILER', begin, end)
    return source[start:end]


def fini_block(source):
    begin = source.index('void Script::fini(')
    start = source.index('#if ENABLE_STACK_TRACKER || ENABLE_PROFILER', begin)
    end = source.index('#endif', start) + len('#endif')
    return source[start:end]


class PythonProfileHookTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        engine = os.environ.get('WOT_BW_ENGINE_SOURCE')
        if not engine:
            raise unittest.SkipTest('WOT_BW_ENGINE_SOURCE must point to the pinned engine source')
        cls.original = (Path(engine) / patcher.PATH).read_text()
        cls.patched = patcher.patch_text(cls.original)

    def test_exact_sites_and_fail_closed(self):
        self.assertEqual(self.patched.replace(patcher.MARK, patcher.INSTALL), self.original)
        self.assertEqual(patcher.patch_text(self.patched), self.patched)
        self.assertEqual(self.original.count('PyEval_SetProfile( NULL, NULL );'), 2)
        mutations = [self.original.replace(patcher.INSTALL, patcher.MARK, 1),
                     self.original.replace(patcher.INSTALL, 'changed;', 1),
                     self.original + '\n' + patcher.INSTALL,
                     self.original.replace('void __onThreadStart(', 'void changed(', 1),
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
            data = b'\xef\xbb\xbf' + self.original.replace('\n', '\r\n').encode()
            path.write_bytes(data)
            patcher.patch(directory)
            first = path.read_bytes()
            self.assertEqual(first, b'\xef\xbb\xbf' + self.patched.replace('\n', '\r\n').encode())
            patcher.patch(directory)
            self.assertEqual(path.read_bytes(), first)
            path.write_bytes(self.original.replace('\n', '\r\n', 1).encode())
            with self.assertRaises(ValueError):
                patcher.patch(directory)

    def test_sdk_functions_execute_expected_hook_and_registration_behavior(self):
        # Extract the actual SDK thread functions and exact init/fini guarded
        # statements. The host substitutes only the external engine APIs.
        prefix = r'''
#include <cassert>
#include <string>
#define ENABLE_PROFILER 1
#define ENABLE_STACK_TRACKER 1
namespace BW { typedef std::string string; }
typedef int (*TraceFunc)(void*, void*, int, void*);
int profileFunc(void*, void*, int, void*) { return 0; }
int installs = 0, clears = 0, registrations = 0, removals = 0;
void PyEval_SetProfile(TraceFunc f, void*) { if (f) ++installs; else ++clears; }
int OurThreadID() { return 4; }
struct Profiler {
    void addThisThread(const char* name) { assert(std::string(name) == "worker"); ++registrations; }
    void removeThread(int id) { assert(id == 4); ++removals; }
} g_profiler;
'''
        for label, text, expected in [('baseline', self.original, 2), ('patched', self.patched, 0)]:
            source = prefix + '\n' + function(text, 'void __onThreadStart(') + '\n' + \
                function(text, 'void __onThreadEnd()') + '\nvoid init() {\n' + init_block(text) + \
                '\n}\nvoid fini() {\n' + fini_block(text) + '\n}\n' + '''
int main() {
    init(); __onThreadStart("worker");
    assert(installs == %d && registrations == 1 && clears == 0);
    __onThreadEnd(); fini();
    assert(clears == 2 && removals == 1 && registrations == 1 && installs == %d);
}
''' % (expected, expected)
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                cpp = Path(directory) / 'host.cpp'
                cpp.write_text(source)
                exe = Path(directory) / 'host'
                result = subprocess.run(['c++', '-std=c++11', '-Wall', '-Werror', str(cpp), '-o', str(exe)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
