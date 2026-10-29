"""Execute the actual SDK warning guard across nested and unwinding scopes."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    'frame_behind_guard_patch', ROOT / 'bigworld/patch_stock_frame_behind_guard.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def scope(source):
    start = source.index('class ScopedDisableFrameBehindWarning\n{')
    return source[start:source.index('\n};', start) + len('\n};')]


HOST = r'''
#include <cassert>
class PyModelNode {
    friend class ScopedDisableFrameBehindWarning;
    static bool disableFrameBehindWarning() { return disabled; }
    static void disableFrameBehindWarning(bool value) { disabled = value; }
public:
    static bool disabled;
};
bool PyModelNode::disabled = false;
@SCOPE@
int main() {
    const bool restored = @RESTORED@;
    assert(!PyModelNode::disabled);
    { ScopedDisableFrameBehindWarning outer; assert(PyModelNode::disabled); }
    assert(!PyModelNode::disabled);
    {
        ScopedDisableFrameBehindWarning outer;
        assert(PyModelNode::disabled);
        { ScopedDisableFrameBehindWarning inner; assert(PyModelNode::disabled); }
        assert(PyModelNode::disabled == restored);
    }
    assert(!PyModelNode::disabled);
    {
        ScopedDisableFrameBehindWarning outer;
        {
            ScopedDisableFrameBehindWarning middle;
            { ScopedDisableFrameBehindWarning inner; assert(PyModelNode::disabled); }
            assert(PyModelNode::disabled == restored);
        }
        assert(PyModelNode::disabled == restored);
    }
    assert(!PyModelNode::disabled);
    {
        ScopedDisableFrameBehindWarning outer;
        try { ScopedDisableFrameBehindWarning inner; throw 7; }
        catch (int value) { assert(value == 7); }
        assert(PyModelNode::disabled == restored);
    }
    assert(!PyModelNode::disabled);
    try {
        ScopedDisableFrameBehindWarning outer;
        ScopedDisableFrameBehindWarning inner;
        throw 9;
    } catch (int value) { assert(value == 9); }
    assert(!PyModelNode::disabled);
    PyModelNode::disabled = true;
    { ScopedDisableFrameBehindWarning outer; assert(PyModelNode::disabled); }
    assert(PyModelNode::disabled == restored);
}
'''


class FrameBehindGuardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        engine = Path(os.environ.get('WOT_BW_ENGINE_SOURCE',
            '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        path = engine / patcher.PATH
        if not path.is_file():
            if 'WOT_BW_ENGINE_SOURCE' in os.environ:
                raise AssertionError('Configured SDK source file missing')
            raise unittest.SkipTest('Pinned SDK source required')
        cls.source = path.read_text(encoding='utf-8-sig')
        cls.patched = patcher.patch_text(cls.source)
        cls.original = cls.patched.replace(patcher.PATCHED, patcher.ORIGINAL, 1)

    def test_actual_sdk_nested_and_exception_scopes_with_negative_control(self):
        compiler = shlex.split(os.environ.get('CXX', 'c++'))
        if not compiler or not shutil.which(compiler[0]):
            self.skipTest('Requires a C++ compiler')
        for label, text, restored in [('original', self.original, 'false'),
                                      ('patched', self.patched, 'true')]:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as directory:
                cpp, binary = Path(directory) / 'host.cpp', Path(directory) / 'host'
                cpp.write_text(HOST.replace('@SCOPE@', scope(text)).replace('@RESTORED@', restored))
                result = subprocess.run(compiler + ['-std=c++11', '-Wall', '-Wextra', '-Werror',
                    str(cpp), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                subprocess.run([str(binary)], check=True, capture_output=True)

    def test_exact_change_idempotence_and_rejects_drift_without_writing(self):
        self.assertEqual(patcher.patch_text(self.patched), self.patched)
        self.assertEqual(self.patched.replace(patcher.PATCHED, patcher.ORIGINAL, 1), self.original)
        mutations = [self.original.replace('disableFrameBehindWarning( false );',
                                           'disableFrameBehindWarning( true );', 1),
                     self.original + '\n' + patcher.ORIGINAL,
                     self.patched + '\n' + patcher.PATCHED,
                     self.original.replace('class ScopedDisableFrameBehindWarning\n{',
                                           'class ChangedGuard\n{', 1),
                     self.patched.replace('bool previous_;', 'bool changed_;', 1)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / patcher.PATH
            path.parent.mkdir(parents=True)
            for source in mutations:
                with self.subTest(source=source[-80:]):
                    path.write_text(source)
                    before = path.read_bytes()
                    with self.assertRaises(ValueError):
                        patcher.patch(directory)
                    self.assertEqual(path.read_bytes(), before)

    def test_preserves_encoding_permissions_and_failed_replace(self):
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
            self.assertEqual(path.read_bytes(),
                b'\xef\xbb\xbf' + self.patched.replace('\n', '\r\n').encode())
            if os.name != 'nt':
                self.assertEqual(path.stat().st_mode & 0o777, 0o640)
            self.assertFalse(patcher.patch(directory))
            mixed = self.original.replace('\n', '\r\n', 1).encode()
            path.write_bytes(mixed)
            with self.assertRaises(ValueError):
                patcher.patch(directory)
            self.assertEqual(path.read_bytes(), mixed)


if __name__ == '__main__':
    unittest.main()
