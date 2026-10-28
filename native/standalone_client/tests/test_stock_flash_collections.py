"""Real SDK/Python conversion plus original #1513 set dispatch and ordinary PYC producers."""
from pathlib import Path
import importlib.util
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
PATCH_FILE = ROOT/'bigworld/patch_stock_flash_collections.py'
spec = importlib.util.spec_from_file_location('flash_collections_patch', PATCH_FILE)
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def sdk_source(test):
    name = 'WOT_PUBLIC_ENGINE_ARCHIVE'
    archive = Path(os.environ.get(name, '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if name in os.environ:
            test.fail('Configured public engine archive is missing: '+str(archive))
        test.skipTest('Pinned public engine archive required')
    with tarfile.open(archive) as tar:
        member = next((m for m in tar if m.name.endswith('/programming/bigworld/'+patcher.SOURCE)), None)
        test.assertIsNotNone(member, 'Pinned SDK GFx source member missing')
        return tar.extractfile(member).read().decode('utf-8-sig')


def body(source):
    begin = source.index('GFx::Value PyGFxValue::convertValueToGFx(')
    end = source.index('void PyGFxValue::collectChildValues()', begin)
    return source[begin:end]


def build(test, directory, source):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not python.is_file() or not config.is_file():
        if 'WOT_PYTHON27' in os.environ:
            test.fail('Configured CPython 2.7 development files are missing')
        test.skipTest('CPython 2.7 development files required')
    version = subprocess.check_output([str(python), '--version'], stderr=subprocess.STDOUT, text=True)
    test.assertIn('Python 2.7.18', version)
    compiler = os.environ.get('CXX', 'c++')
    if not shutil.which(compiler):
        if 'CXX' in os.environ:
            test.fail('Configured C++ compiler is unavailable: ' + compiler)
        test.skipTest('C++ compiler required')
    call = source[source.index('PyObject *PyGFxValue::pyCall('):source.index('void PyGFxValue::pyAdditionalMembers(')]
    host = (ROOT/'tests/stock_flash_collections_host.cpp').read_text().replace('@CONVERTER@', body(source)).replace('@PYCALL@', call)
    path = directory/'host.cpp';path.write_text(host)
    flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
    flags = [f for f in flags if not f.startswith('-O') and f != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory/'host'
    subprocess.run([compiler, '-std=c++14', '-O0', '-Wall', '-Wextra', '-Werror',
                    '-Wno-missing-field-initializers', '-Wno-deprecated-register', '-Wno-writable-strings',
                    '-Wno-unused-parameter', *flags, str(path), *libs, '-o', str(binary)], check=True)
    return binary


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value:
        test.skipTest('Exact #1513 client required')
    path = Path(value)
    test.assertTrue((path/'WorldOfTanks.exe').is_file(), 'Configured #1513 executable missing')
    test.assertTrue((path/'res/packages/scripts.pkg').is_file(), 'Configured #1513 scripts.pkg missing')
    return path


class FlashCollectionsTest(unittest.TestCase):
    def test_explicit_missing_compiler_fails_instead_of_skipping(self):
        with patch.object(Path, 'is_file', return_value=True), \
             patch.object(subprocess, 'check_output', return_value='Python 2.7.18\n'), \
             patch.object(shutil, 'which', return_value=None):
            with patch.dict(os.environ, {'CXX': 'missing-fixture-compiler'}, clear=True):
                with self.assertRaisesRegex(AssertionError, 'Configured C[+][+] compiler is unavailable'):
                    build(self, Path('/unused-compiler-probe'), '')
            with patch.dict(os.environ, {}, clear=True):
                with self.assertRaisesRegex(unittest.SkipTest, 'C[+][+] compiler required'):
                    build(self, Path('/unused-compiler-probe'), '')

    def test_patch_is_scoped_idempotent_and_transactional(self):
        source = sdk_source(self)
        changed = patcher.patch_source(source)
        restored = changed.replace(patcher.SET_BRANCH, '', 1).replace(patcher.DICT_AFTER, patcher.DICT_BEFORE, 1).replace(patcher.CALL_AFTER, patcher.CALL_BEFORE, 1)
        self.assertEqual(restored, source)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory);path = root/patcher.SOURCE
            path.parent.mkdir(parents=True);path.write_text(source)
            patcher.patch(root);before = path.read_bytes()
            patcher.patch(root);self.assertEqual(path.read_bytes(), before)
            path.write_text(source.replace(patcher.ANCHOR, '\telse if (changed(pyValue))\n'))
            before = path.read_bytes()
            with self.assertRaises(ValueError):patcher.patch(root)
            self.assertEqual(path.read_bytes(), before)

    def test_real_cpython_conversion_and_negative_control(self):
        source = sdk_source(self)
        script = ROOT/'tests/stock_flash_collections_consumer.py'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = root/'old';old.mkdir()
            old_binary = build(self, old, patcher.patch_source(source).replace(patcher.SET_BRANCH, '', 1))
            control = subprocess.run([str(old_binary), str(script)], text=True, capture_output=True)
            self.assertNotEqual(control.returncode, 0, 'Missing-set production negative control passed')
            self.assertIn('AssertionError', control.stderr)
            binary = build(self, root, patcher.patch_source(source))
            result = subprocess.run([str(binary), str(script)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('lifetime contracts passed', result.stdout)

    def test_previous_dictionary_converter_reproduces_crosshair_event_failure(self):
        path = client(self)
        source = sdk_source(self)
        previous = patcher.patch_source(source).replace(patcher.DICT_AFTER, patcher.DICT_BEFORE, 1).replace(patcher.CALL_AFTER, patcher.CALL_BEFORE, 1)
        script = ROOT/'tests/stock_flash_collections_consumer.py'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = root/'old';old.mkdir()
            binary = build(self, old, previous)
            result = subprocess.run([str(binary), str(script), '--crosshair-only', str(path)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('expected string or Unicode object, int found', result.stderr)
            self.assertIn('Event.py', result.stderr)
            binary = build(self, root, patcher.patch_source(source))
            result = subprocess.run([str(binary), str(script), '--crosshair-only', str(path)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('subscribe/deduplicate/notify/unsubscribe passed', result.stdout)

    def test_original_ordinary_battle_pyc_producers(self):
        path = client(self)
        with tempfile.TemporaryDirectory() as directory:
            binary = build(self, Path(directory), patcher.patch_source(sdk_source(self)))
            result = subprocess.run([str(binary), str(ROOT/'tests/stock_flash_collections_consumer.py'), str(path)],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertIn('PYC collection producers passed', result.stdout)

    def test_original_x86_collection_dispatch_and_reference_lifetime(self):
        path = client(self)
        try:
            from stock_flash_collections_oracle import OriginalCollections
        except ImportError:
            self.skipTest('Optional pefile and Unicorn dependencies required')
        original = OriginalCollections(path/'WorldOfTanks.exe')
        for kind in ('set', 'subclass', 'frozenset', 'unsupported'):
            for values in ([], [0], [4, 2, 8], list(range(64))):
                expected = ('undefined',) if kind in ('frozenset', 'unsupported') else (
                    'array', [('number', value) for value in values])
                self.assertEqual(original.convert(kind, values), expected, (kind, values))

    def test_original_x86_dictionary_key_stringification_and_lifetime(self):
        path = client(self)
        try:
            from stock_flash_collections_oracle import OriginalCollections
        except ImportError:
            self.skipTest('Optional pefile and Unicorn dependencies required')
        original = OriginalCollections(path/'WorldOfTanks.exe')
        for keys in ([], [(True, 'exact')], [(False, '1'), (False, '2')],
                     [(True, 'ascii'), (False, 'None'), (False, '(7, 8)'), (False, 'custom-key')]):
            self.assertEqual(original.dictionary(keys), ('object', {
                text: ('number', index + 10) for index, (exact, text) in enumerate(keys)}))
        # Distinct Python keys can have the same AS string name; preserve native overwrite order.
        self.assertEqual(original.dictionary([(True, '1'), (False, '1')]),
                         ('object', {'1': ('number', 11)}))


if __name__ == '__main__':
    unittest.main()
