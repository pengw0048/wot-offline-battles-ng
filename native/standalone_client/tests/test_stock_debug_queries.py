"""BigWorld.statPing and BigWorld.getFPS over their original owners.

Real CPython 2.7 builds the engine's PY_AUTO macros, the public Script
converters and argCountError, the production bindings and statistics task,
and DebugApp's frame-rate sampling and accessor produced by
patch_stock_debug_queries.py from the pinned debug_app.cpp/hpp. The original
debug_ctrl.pyc drives it. The Unicorn oracle runs the exact #1513 wrappers,
the statistics tick 006da4c0 and DebugApp::tick 006323a0 on the same inputs.
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
import patch_stock_debug_queries  # noqa: E402

ENGINE_FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'client/debug_app.hpp', 'client/debug_app.cpp')


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


def patched_debug_app(sources, directory):
    for name in ('client/debug_app.hpp', 'client/debug_app.cpp'):
        (directory / name).parent.mkdir(parents=True, exist_ok=True)
        (directory / name).write_text(sources[name])
    patch_stock_debug_queries.patch(directory)
    return {name: (directory / name).read_text() for name in ('client/debug_app.hpp', 'client/debug_app.cpp')}


def generated_source(sources, debug_app):
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    replacements = {
        '@RET_DATA@': between(script_hpp, 'template <class T>\n\tclass IsValidRetData',
                              'return Script::IsValidRetData<T>::getData( data );\n\t}'),
        '@SCRIPT_BODIES@': between(script_cpp, 'PyObject * Script::getData( const float data )', '\n}\n') +
        between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n') +
        between(script_cpp, 'int Script::setData( PyObject * pObject, bool & rBool,', '\n}\n'),
        '@ENABLE_BATTLE_BINDING@': between((ROOT/'bigworld/stock_runtime_services.cpp').read_text(), 'static void enableBattleStatisticCollector(', 'ARG( bool, END ), BigWorld )'),
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
        '@DEBUG_ACCESSOR@': between(debug_app['client/debug_app.hpp'], '\tvoid fpsStatistics(',
                                    'maximum = maxFps_;\n\t}'),
        '@DEBUG_SAMPLING@': between(debug_app['client/debug_app.cpp'], '    dRenderTime_ = dRenderTime;',
                                    'fpsAverage_ = float( samples ) / totalTimeInCache;\n    }'),
    }
    text = (ROOT / 'tests/stock_debug_queries_host.cpp').read_text()
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    debug_app = patched_debug_app(sources, directory / 'engine')
    for fake in ('pch.hpp', 'connection_control.hpp', 'debug_app.hpp', 'connection/server_connection.hpp',
                 'connection_model/bw_server_connection.hpp', 'cstdmf/main_loop_task.hpp', 'math/vector4.hpp', 'math/vector3.hpp',
                 'pyscript/script.hpp'):
        (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
        (directory / 'include' / fake).write_text('#pragma once\n')
    source = directory / 'host.cpp'
    source.write_text(generated_source(sources, debug_app))
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    # The original computes in SSE2 single precision; keep clang from fusing.
    subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-ffp-contract=off', *flags, '-Wall', '-Werror',
                    '-Wno-deprecated-register', '-Wno-writable-strings', '-Wno-unused-function',
                    '-I', str(directory / 'include'), '-I', str(ROOT / 'bigworld'), str(source), *libs,
                    '-o', str(binary)], check=True)
    return binary


class StockDebugQueriesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='debug-queries-cpython-')
        cls.host = None

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def built_host(self):
        if type(self).host is None:
            type(self).host = build_host(self, Path(self.directory.name), engine_sources(self))
        return type(self).host

    def test_debug_app_patch_is_anchored_and_idempotent(self):
        sources = engine_sources(self)
        with tempfile.TemporaryDirectory(prefix='debug-queries-app-') as directory:
            directory = Path(directory)
            first = patched_debug_app(sources, directory)
            patch_stock_debug_queries.patch(directory)
            self.assertEqual(first, {name: (directory / name).read_text() for name in first})
            self.assertEqual(first['client/debug_app.hpp'].count('void fpsStatistics('), 1)
            self.assertIn('\tuint32\t\t\t\tfpsIndex_;', first['client/debug_app.hpp'])
            self.assertNotIn('50.f / totalTimeInCache', first['client/debug_app.cpp'])
            shutil.rmtree(directory / 'client')
            stale = dict(sources)
            stale['client/debug_app.cpp'] = stale['client/debug_app.cpp'].replace('fpsAverage_ = 50.f',
                                                                                  'fpsAverage_ = 49.f')
            with self.assertRaisesRegex(RuntimeError, 'frame-rate sampling'):
                patched_debug_app(stale, directory)

    def test_original_debug_controller_over_the_bindings(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original debug_ctrl.pyc')
        binary = self.built_host()
        for mode in ('absent', 'port'):
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_debug_queries_consumer.py'),
                                     str(Path(client) / 'res/packages/scripts.pkg'), mode],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_original_x86_matches_the_bindings_and_owners(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_debug_queries_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        executable = Path(client) / 'WorldOfTanks.exe'
        self.assertEqual(oracle.run_wrappers(executable), oracle.expected_wrappers())
        original = {'frames': oracle.run_debug_app(executable), 'ping': oracle.run_ping(executable)}
        result = subprocess.run([str(self.built_host()), str(ROOT / 'tests/stock_debug_queries_compare.py'),
                                 json.dumps({'frames': oracle.FRAMES, 'ping': oracle.PING})],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        production = json.loads(result.stdout)
        self.assertEqual(production['ping'], original['ping'])
        self.assertEqual(len(production['frames']), len(original['frames']))
        for index, (mine, theirs) in enumerate(zip(production['frames'], original['frames'])):
            self.assertEqual(mine, theirs, 'frame %d' % index)
        print('Original statPing/getFPS wrappers matched; %d DebugApp frames and %d ping ticks bit-identical'
              % (len(original['frames']), len(original['ping'])))


if __name__ == '__main__':
    unittest.main()
