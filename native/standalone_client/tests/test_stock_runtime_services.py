"""BigWorld runtime services: clock, frame identity, telemetry, frame pacing, URL cache.

Real CPython 2.7 builds the engine's PY_AUTO macros, the public Script
converters and argCountError, the production bindings and StockFramePacing,
and the App methods, configuration and frame-sleep lines produced by
patch_stock_runtime_services.py from the pinned app.cpp. The original
statistics, arena_load_ctrl, Avatar.leaveArena, personality.onDisconnected and
ClientHangarSpace PYC drive it. The Unicorn oracle runs the exact #1513
wrappers and App frame-pacing methods.
"""
from pathlib import Path
import json
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / 'bigworld'))
import patch_stock_runtime_services  # noqa: E402

ENGINE_FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'client/app.hpp', 'client/app.cpp')
SET_DATA = ('bool & rBool,', 'int64 & rInt,', 'float & rFloat,', 'double & rDouble,', 'BW::string & rString,')


def between(text, begin, end, include_end=True):
    start = text.rindex('\n', 0, text.index(begin)) + 1
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


def engine_sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        test.skipTest('Pinned public engine source archive required')
    sources = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for relative in ENGINE_FILES:
                if member.name.endswith('/programming/bigworld/' + relative):
                    sources[relative] = tar.extractfile(member).read().decode('utf-8')
            if len(sources) == len(ENGINE_FILES):
                break
    test.assertEqual(set(sources), set(ENGINE_FILES))
    return sources


def patched_app(sources, directory):
    for name in ('client/app.hpp', 'client/app.cpp'):
        (directory / name).parent.mkdir(parents=True, exist_ok=True)
        (directory / name).write_text(sources[name])
    patch_stock_runtime_services.patch(directory)
    return {name: (directory / name).read_text() for name in ('client/app.hpp', 'client/app.cpp')}


def generated_source(sources, app):
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    bodies = ''.join(between(script_cpp, 'int Script::setData( PyObject * pObject, ' + name, '\n}\n')
                     for name in SET_DATA)
    bodies += between(script_cpp, 'PyObject * Script::getData( const uint data )', '\n}\n')
    bodies += between(script_cpp, 'PyObject * Script::getData( const float data )', '\n}\n')
    source = app['client/app.cpp']
    replacements = {
        '@RET_DATA@': between(script_hpp, 'template <class T>\n\tclass IsValidRetData',
                              'return Script::IsValidRetData<T>::getData( data );\n\t}'),
        '@SCRIPT_BODIES@': bodies + between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
        '@APP_CONFIGURATION@': between(source, '\tint frameRate = configSection->readInt(',
                                       'uint64(frameTimerFreq() / reducedFrameRate) : 0 );'),
        '@APP_SLEEP@': between(source, '\t\tsleepTime = max( sleepTime, sleepTime_ );',
                               '\t\tsleepTime = framePacing_.sleepTime( sleepTime );'),
        '@APP_METHODS@': between(source, 'double App::getRenderTimeNow() const', '\n}\n') +
        between(source, 'void App::setSpecialFPSMode( float fps )', 'framePacing_.setReduced( enabled );\n}\n'),
    }
    text = (ROOT / 'tests/stock_runtime_services_host.cpp').read_text()
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    app = patched_app(sources, directory / 'engine')
    for fake in ('pch.hpp', 'app.hpp', 'moo/render_context.hpp', 'pyscript/script.hpp',
                 'urlrequest/manager.hpp', 'cstdmf/stdmf.hpp'):
        (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
        (directory / 'include' / fake).write_text('#pragma once\n')
    source = directory / 'host.cpp'
    source.write_text(generated_source(sources, app))
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    subprocess.run(['c++', '-std=c++14', '-UNDEBUG', *flags, '-Wall', '-Werror', '-Wno-deprecated-register',
                    '-Wno-writable-strings', '-Wno-unused-function', '-I', str(directory / 'include'),
                    '-I', str(ROOT / 'bigworld'), str(source), *libs, '-o', str(binary)], check=True)
    return binary


def scripts_package(test):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client:
        test.skipTest('WOT_0922_CLIENT is required for the original PYC call sites')
    return Path(client) / 'res/packages/scripts.pkg'


class StockRuntimeServicesTest(unittest.TestCase):
    def test_app_patch_is_anchored_and_idempotent(self):
        sources = engine_sources(self)
        with tempfile.TemporaryDirectory(prefix='runtime-services-app-') as directory:
            directory = Path(directory)
            first = patched_app(sources, directory)
            patch_stock_runtime_services.patch(directory)
            second = {name: (directory / name).read_text() for name in first}
            self.assertEqual(first, second)
            header, source = first['client/app.hpp'], first['client/app.cpp']
            self.assertEqual(header.count('wot::StockFramePacing framePacing_;'), 1)
            self.assertEqual(header.count('#include "stock_runtime_services.hpp"'), 1)
            self.assertEqual(source.count('framePacing_.sleepTime( sleepTime );'), 1)
            self.assertEqual(source.count('"renderer/reducedFrameRate"'), 1)
            # The reduced floor follows sleepTime_ and precedes the Sleep decision.
            loop = between(source, '\t\tint sleepTime = 0;', '\t\tlastFrameEndTime_ = frameTimerValue();')
            self.assertLess(loop.index('sleepTime_ );'), loop.index('framePacing_.sleepTime'))
            self.assertLess(loop.index('framePacing_.sleepTime'), loop.index('::Sleep( sleepTime );'))
            # A changed anchor fails instead of silently skipping the owner.
            shutil.rmtree(directory / 'client')
            stale = dict(sources)
            stale['client/app.cpp'] = stale['client/app.cpp'].replace('sleepTime = max( sleepTime, sleepTime_ );',
                                                                      'sleepTime = sleepTime_;')
            with self.assertRaisesRegex(RuntimeError, 'frame sleep'):
                patched_app(stale, directory)

    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='runtime-services-cpython-')
        cls.host = None

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def built_host(self):
        if type(self).host is None:
            type(self).host = build_host(self, Path(self.directory.name), engine_sources(self))
        return type(self).host

    def test_original_pyc_consumers_over_the_bindings(self):
        package = scripts_package(self)
        binary = self.built_host()
        for mode in ('absent', 'port'):
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_runtime_services_consumer.py'),
                                     str(package), mode], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_original_x86_matches_the_bindings_and_frame_pacing(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_runtime_services_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        executable = Path(client) / 'WorldOfTanks.exe'
        report = oracle.run_wrappers(executable)
        self.assertEqual(report, oracle.expected_wrappers())
        original = oracle.run_frame_pacing(executable)
        result = subprocess.run([str(self.built_host()), str(ROOT / 'tests/stock_runtime_services_pacing.py'),
                                 json.dumps(oracle.PACING)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        production = json.loads(result.stdout)
        self.assertEqual(len(production), len(original))
        # #1513 keeps the saved rate as a float and restores only a positive one,
        # so an unlimited normal rate stays at the special cap; production restores it.
        leaked = [index for index, row in enumerate(original) if row != production[index]]
        self.assertEqual([original[index] for index in leaked], [
            ['unlimited normal', ['disable'], [16, 0]], ['unlimited normal nested', ['disable'], [16, 0]]])
        self.assertEqual([production[index] for index in leaked], [
            ['unlimited normal', ['disable'], [0, 0]], ['unlimited normal nested', ['disable'], [0, 0]]])
        print('Original wrappers matched the production declarations: %d calls; frame pacing matched '
              '%d of %d transitions (the rest are the unlimited-rate restore)'
              % (len(report), len(original) - len(leaked), len(original)))

if __name__ == '__main__':
    unittest.main()
