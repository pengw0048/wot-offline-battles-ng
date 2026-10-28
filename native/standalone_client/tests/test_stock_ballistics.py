"""BigWorld.PyBallisticsSimulator: #1513 native tracer presentation.

The core test exercises records, handles, terminal timing and re-entrancy.
Unicorn runs the exact #1513 subdivision, plane, matrix, update and hide
instructions against the production core bit for bit. Real CPython 2.7 runs
the original ProjectileMover.pyc over the production binding.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
from native_test_compile_flags import native_test_compile_flags
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
ENGINE_FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/pyscript/pyobject_plus.hpp',
                'client/world_app.cpp')
SET_DATA = ('bool & rBool,', 'int & rInt,', 'uint & rUint,', 'float & rFloat,', 'double & rDouble,')
CORE = [str(ROOT / 'src/stock_ballistics.cpp')]
STRICT = ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-ffp-contract=off', '-I', str(ROOT / 'include')]


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


SCENE_FAKE_HEADERS = (
    'pch.hpp', 'cstdmf/bw_namespace.hpp', 'cstdmf/bw_string.hpp', 'cstdmf/stdmf.hpp',
    'math/vector3.hpp', 'math/vector4.hpp', 'physics2/worldtri.hpp',
    'physics2/collision_callback.hpp', 'physics2/collision_obstacle.hpp',
    'pyscript/script.hpp', 'pyscript/script_math.hpp', 'romp/water.hpp',
    'space/forward_declarations.hpp', 'space/client_space.hpp',
    'space/deprecated_space_helpers.hpp', 'space/space_manager.hpp')


def scene_owner(test):
    """The battle scene-query owner's sources: this tree once integrated, else BQ's."""
    for candidate in (ROOT / 'bigworld', Path(os.environ.get(
            'WOT_BQ_STANDALONE', '/Users/peng/.codex/worktrees/claude-battle-scene-queries/'
            'wot-offline-battles/native/standalone_client')) / 'bigworld'):
        source = candidate / 'stock_battle_scene_queries.cpp'
        if source.is_file() and 'collideStockSegmentNative' in source.read_text():
            return candidate
    test.skipTest('The battle scene-query owner with collideStockSegmentNative is required')


def python27_flags(test):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    return flags, shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))


def client(test):
    path = os.environ.get('WOT_0922_CLIENT')
    if not path:
        test.skipTest('WOT_0922_CLIENT is required for the original client')
    return Path(path)


class StockBallisticsTest(unittest.TestCase):
    def test_core_lifecycle(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        with tempfile.TemporaryDirectory(prefix='ballistics-core-') as directory:
            binary = Path(directory) / 'core'
            subprocess.run(['c++', *native_test_compile_flags('c++'), *STRICT, '-g', *CORE,
                            str(ROOT / 'tests/test_stock_ballistics.cpp'), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('stock ballistics core passed', result.stdout)

    def test_original_x86_instructions_match_the_core(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        executable = client(self) / 'WorldOfTanks.exe'
        try:
            import stock_ballistics_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        with tempfile.TemporaryDirectory(prefix='ballistics-oracle-') as directory:
            library = Path(directory) / 'libballistics.so'
            subprocess.run(['c++', *native_test_compile_flags('c++', sanitise=False), *STRICT, '-O2', '-shared', '-fPIC', *CORE,
                            str(ROOT / 'tests/stock_ballistics_oracle_shim.cpp'), '-o', str(library)],
                           check=True)
            counts, mismatches = oracle.run(executable, library)
        self.assertEqual(mismatches, [])
        self.assertEqual(counts, {'subdivide': 60, 'intersect': 200, 'matrix': 120, 'update': 300,
                                  'plane': 100})
        print('Original ballistics instructions matched: %r' % counts)

    def test_original_projectile_mover_over_the_binding(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        package = client(self) / 'res/packages/scripts.pkg'
        sources = engine_sources(self)
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files unavailable')
        script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
        plus = sources['lib/pyscript/pyobject_plus.hpp']
        bodies = ''.join(between(script_cpp, 'int Script::setData( PyObject * pObject, ' + name, '\n}\n')
                         for name in SET_DATA)
        replacements = {
            '@SCRIPT_BODIES@': bodies + between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
            '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links',
                                       '// Section: Macros: Enums', False) +
            between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
            '@PY_AUTO_METHOD_DECLARE@': between(plus, '/// This macro declares an auto-parsed standard python method',
                                                '/// This macro declares a python method that uses keywords.', False),
        }
        text = (ROOT / 'tests/stock_ballistics_host.cpp').read_text()
        for key, value in replacements.items():
            text = text.replace(key, value)
        with tempfile.TemporaryDirectory(prefix='ballistics-cpython-') as directory:
            directory = Path(directory)
            for fake in ('pch.hpp', 'app.hpp', 'cstdmf/bw_namespace.hpp', 'duplo/motor.hpp', 'duplo/pymodel.hpp',
                         'moo/render_context.hpp', 'pyscript/pyobject_plus.hpp', 'pyscript/script.hpp',
                         'pyscript/script_math.hpp', 'space/forward_declarations.hpp'):
                (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
                (directory / 'include' / fake).write_text('#pragma once\n')
            (directory / 'host.cpp').write_text(text)
            flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
                     if flag != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            core = directory / 'core.o'
            subprocess.run(['c++', *native_test_compile_flags('c++', sanitise=False), *STRICT, '-c', *CORE, '-o', str(core)], check=True)
            binary = directory / 'host'
            subprocess.run(['c++', *native_test_compile_flags('c++', sanitise=False), '-std=c++14', '-UNDEBUG', *flags, '-Wall', '-Werror', '-Wno-deprecated-register',
                            '-Wno-writable-strings', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-local-typedef',
                            '-I', str(directory / 'include'), '-I', str(ROOT / 'bigworld'), '-I', str(ROOT / 'include'),
                            str(directory / 'host.cpp'), str(core), *libs, '-o', str(binary)], check=True)
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_ballistics_consumer.py'), str(package)],
                                    capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('ProjectileMover.pyc over production binding', result.stdout)
        print(result.stdout.strip())

    def test_real_scene_owner_with_the_core(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        owner = scene_owner(self)
        flags, libs = python27_flags(self)
        effects = (ROOT / 'bigworld/stock_ground_effects.cpp').read_text()
        begin = effects.index('float Water::stockGroundRayDistance(')
        water = 'namespace BW {\n' + effects[begin:effects.index('\nnamespace\n{', begin)] + '\n}\n'
        with tempfile.TemporaryDirectory(prefix='ballistics-scene-') as directory:
            directory = Path(directory)
            for fake in SCENE_FAKE_HEADERS:
                (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
                (directory / 'include' / fake).write_text('#pragma once\n')
            (directory / 'include/stock_ground_water_query.inc').write_text(water)
            core = directory / 'core.o'
            subprocess.run(['c++', *native_test_compile_flags('c++'), *STRICT, '-g', '-c', *CORE, '-o', str(core)],
                           check=True)
            binary = directory / 'cross'
            subprocess.run(['c++', *native_test_compile_flags('c++'), '-std=c++14', '-g', '-UNDEBUG', *flags,
                            '-ffp-contract=off', '-Wall', '-Werror', '-Wno-deprecated-register',
                            '-Wno-unused-function', '-Wno-writable-strings',
                            '-I', str(directory / 'include'), '-I', str(owner), '-I', str(ROOT / 'bigworld'),
                            '-I', str(ROOT / 'include'), '-I', str(ROOT / 'tests'),
                            str(ROOT / 'tests/stock_ballistics_scene_cross.cpp'), str(core), *libs,
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True,
                                    env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('BQ native scene with BP presenter passed', result.stdout)
        print('Scene owner: %s' % owner)

    def test_tick_patch_is_exact_and_idempotent(self):
        import importlib.util
        source = engine_sources(self)['client/world_app.cpp']
        spec = importlib.util.spec_from_file_location('patch_stock_ballistics',
                                                      ROOT / 'bigworld/patch_stock_ballistics.py')
        patcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patcher)
        with tempfile.TemporaryDirectory(prefix='ballistics-patch-') as directory:
            path = Path(directory) / 'client' / 'world_app.cpp'
            path.parent.mkdir()
            path.write_text(source)
            patcher.patch_engine(directory)
            once = path.read_text()
            patcher.patch_engine(directory)
            self.assertEqual(path.read_text(), once)
        self.assertEqual(once.count('tickStockBallistics();'), 1)
        self.assertIn('        GlobalEmbodiments::tick( dGameTime );\n        tickStockBallistics();\n', once)
        self.assertEqual(once.count('#include "stock_ballistics_simulator.hpp"'), 1)


if __name__ == '__main__':
    unittest.main()
