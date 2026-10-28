"""BigWorld Python watcher functions: #1513's consumer-build contract over the Hybrid engine.

The patch is applied to the pinned public pywatcher.cpp. Real CPython 2.7
then runs the patched registration block through the engine's own
PyModuleMethodLink and replays the exact #1513 OfflineMapCreator PYC.
"""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[1]
TESTS = Path(__file__).resolve().parent
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
CLIENT = os.environ.get('WOT_0922_CLIENT')
PY27 = Path(os.environ.get('WOT_PYTHON27', str(Path.home() / '.pyenv/versions/2.7.18/bin/python2.7')))
SOURCES = ('lib/pyscript/pywatcher.cpp', 'lib/pyscript/script.hpp', 'lib/pyscript/script.cpp')

spec = importlib.util.spec_from_file_location('patch_stock_python_watchers',
                                              ROOT / 'bigworld/patch_stock_python_watchers.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def engine_sources(test):
    if not ARCHIVE.is_file():
        test.skipTest('Pinned public BigWorld engine archive required')
    with tempfile.TemporaryDirectory() as directory:
        prefix = subprocess.run(['tar', '-tzf', str(ARCHIVE)], check=True, capture_output=True,
                                text=True).stdout.split('/', 1)[0] + '/programming/bigworld/'
        subprocess.run(['tar', '-xzf', str(ARCHIVE), '-C', directory] + [prefix + name for name in SOURCES],
                       check=True)
        return {name: (Path(directory) / prefix / name).read_bytes() for name in SOURCES}


def function(source, signature):
    start = source.index('{', source.index(signature) + len(signature))
    depth, index = 0, start
    while True:
        depth += {'{': 1, '}': -1}.get(source[index], 0)
        index += 1
        if depth == 0:
            return source[source.index(signature):index]


class PythonWatcherPatchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = None

    def setUp(self):
        if PythonWatcherPatchTest.sources is None:
            PythonWatcherPatchTest.sources = engine_sources(self)
        self.original = self.sources['lib/pyscript/pywatcher.cpp'].decode('utf-8')
        self.patched = patcher.patch_text(self.original)

    def test_no_other_build_patcher_owns_pywatcher(self):
        text = (REPOSITORY / 'tools/build_bigworld_client.ps1').read_text()
        names = re.findall(r'"(patch_[a-z_0-9]+\.py)"', text)
        for name in names:
            if name != 'patch_stock_python_watchers.py':
                self.assertNotIn('pywatcher', (ROOT / 'bigworld' / name).read_text(), name)

    def test_only_the_five_registrations_move_to_the_consumer_function(self):
        enabled, disabled = self.original.split(patcher.SPLIT)
        patched_enabled, patched_disabled = self.patched.split(patcher.SPLIT)
        self.assertEqual(patched_disabled, disabled)
        expected = enabled
        for line in patcher.REGISTRATIONS:
            expected = expected.replace(line, '', 1)
        self.assertEqual(patched_enabled.replace(patcher.BLOCK, '', 1), expected)
        registrations = re.findall(r'PY_MODULE_FUNCTION\( *(\w+), *BigWorld *\)', patched_enabled)
        self.assertEqual(registrations, ['addFunctionWatcher'])
        # The Hybrid implementations and C++ watcher owners are left intact.
        for definition in ('static PyObject * py_setWatcher( PyObject * args )',
                           'static PyObject * py_getWatcher( PyObject * args )',
                           'static PyObject * py_addWatcher( PyObject * args )',
                           'PY_TYPEOBJECT( PyDeferredWatcher )',
                           'Watcher::rootWatcher().setFromString( NULL, path, pCStr )'):
            self.assertEqual(patched_enabled.count(definition), enabled.count(definition), definition)
            self.assertEqual(enabled.count(definition), 1, definition)
        links = re.findall(r'PyModuleMethodLink s_link_py_(\w+)\(\n\t"BigWorld", "(\w+)", '
                           r'py_consumerWatcherFunction, NULL \);', self.patched)
        self.assertEqual(sorted(links), sorted((name, name) for name in patcher.NAMES))

    def test_idempotent_and_fail_closed_without_writing(self):
        self.assertEqual(patcher.patch_text(self.patched), self.patched)
        mutations = [self.original.replace(line, line.replace('BigWorld', 'Engine'), 1)
                     for line in patcher.REGISTRATIONS]
        mutations += [self.original.replace(patcher.REGISTRATIONS[0], '', 1),
                      self.original.replace(patcher.SPLIT, patcher.SPLIT + patcher.SPLIT, 1),
                      self.original.replace(patcher.ANCHOR + patcher.SPLIT, patcher.SPLIT, 1),
                      self.patched + patcher.BLOCK,
                      self.patched.replace(patcher.BLOCK, '', 1)]
        for index, text in enumerate(mutations):
            with self.subTest(index=index), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / patcher.PATH
                target.parent.mkdir(parents=True)
                target.write_text(text)
                before = target.read_bytes()
                with self.assertRaises(ValueError):
                    patcher.patch(directory)
                self.assertEqual(target.read_bytes(), before)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / patcher.PATH
            target.parent.mkdir(parents=True)
            target.write_bytes(b'\xef\xbb\xbf' + self.original.replace('\n', '\r\n').encode())
            patcher.patch(directory)
            expected = b'\xef\xbb\xbf' + self.patched.replace('\n', '\r\n').encode()
            self.assertEqual(target.read_bytes(), expected)
            patcher.patch(directory)
            self.assertEqual(target.read_bytes(), expected)

    def test_embedded_python_contract_and_exact_offline_map_creator(self):
        if not CLIENT:
            self.skipTest('WOT_0922_CLIENT is required for the exact #1513 OfflineMapCreator PYC')
        config = PY27.with_name('python2.7-config')
        package = Path(CLIENT) / 'res/packages/scripts.pkg'
        if not config.exists() or not package.is_file():
            self.skipTest('CPython 2.7 development files and the exact #1513 scripts.pkg are required')
        header = self.sources['lib/pyscript/script.hpp'].decode('utf-8')
        script = self.sources['lib/pyscript/script.cpp'].decode('utf-8')
        link_class = header[header.index('class PyModuleMethodLink : public Script::InitTimeJob'):]
        link_class = link_class[:link_class.index('};') + 2]
        methods = '\n'.join((
            function(script, 'PyModuleMethodLink::PyModuleMethodLink( const char * moduleName,\n'
                             '\t\tconst char * methodName, PyCFunction method,'),
            function(script, 'PyModuleMethodLink::~PyModuleMethodLink()'),
            function(script, 'void PyModuleMethodLink::init()')))
        enabled = self.patched.split(patcher.SPLIT)[0]
        block = enabled[enabled.index(patcher.BLOCK):enabled.index(patcher.BLOCK) + len(patcher.BLOCK)]
        source = (TESTS / 'stock_python_watchers_host.cpp').read_text()
        for key, value in (('@LINK_CLASS@', link_class), ('@LINK_METHODS@', methods), ('@WATCHER_BLOCK@', block)):
            self.assertEqual(source.count(key), 1)
            source = source.replace(key, value)
        flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                 if flag not in ('-DNDEBUG', '-Wstrict-prototypes')]
        libraries = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory() as directory:
            host = Path(directory) / 'host.cpp'
            host.write_text(source)
            binary = Path(directory) / 'host'
            result = subprocess.run(['c++', '-std=c++17', *flags, '-Wall', '-Werror', '-Wno-register',
                                     '-Wno-deprecated-register', '-Wno-writable-strings', str(host),
                                     *libraries, '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), str(TESTS / 'stock_python_watchers_driver.py')],
                                    capture_output=True, text=True,
                                    env=dict(os.environ, WOT_SCRIPTS_PACKAGE=str(package),
                                             PYTHONHOME=str(PY27.parents[1])))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        if os.environ.get('WOT_WATCHER_REPORT'):
            Path(os.environ['WOT_WATCHER_REPORT']).write_text(result.stdout)
        reports = dict(line.split(' ', 1) for line in result.stdout.splitlines())
        for name in patcher.NAMES:
            self.assertEqual(reports[name + '-positional'], 'True', name)
            self.assertEqual(reports[name + '-keyword'], repr('%s() takes no keyword arguments' % name), name)
        self.assertEqual(reports['module-functions'], repr(sorted(patcher.NAMES)))
        # The exact #1513 map creator creates, destroys and cancels without an exception.
        self.assertEqual(reports['native-escaped'], 'None')
        self.assertEqual(reports['native-created'], 'True')
        self.assertEqual(reports['native-logged'], '[]')
        self.assertEqual(reports['native-watcher-calls'], repr([
            (('Visibility/GUI', False), None), (('Visibility/GUI', True), None),
            (('Visibility/GUI', False), None), (('Visibility/GUI', True), None)]))
        # Harness sensitivity: the Hybrid error shape reproduces the room failure.
        self.assertEqual(reports['hybrid-error-shape-logged'],
                         repr(["TypeError('Failed to set Visibility/GUI to False.',)"]))
        self.assertEqual(reports['hybrid-error-shape-escaped'],
                         repr("TypeError('Failed to set Visibility/GUI to True.',)"))


if __name__ == '__main__':
    unittest.main()
