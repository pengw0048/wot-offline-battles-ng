"""BigWorld.Screener: the #1513 PyWatermarkersWrapper compatibility binding.

Real CPython 2.7 runs the engine's PY_AUTO macros, the public Script::setData
and argCountError bodies and the production binding, driven by the original
Waiting and screencast_controller PYC. The Unicorn oracle runs the exact
#1513 methods and checks the same argument-count and conversion contract.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
ENGINE_FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/pyscript/pyobject_plus.hpp')
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


def generated_source(sources):
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    plus = sources['lib/pyscript/pyobject_plus.hpp']
    bodies = ''.join(between(script_cpp, 'int Script::setData( PyObject * pObject, ' + name, '\n}\n')
                     for name in SET_DATA)
    replacements = {
        '@SCRIPT_BODIES@': bodies + between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
        '@PY_AUTO_METHOD_DECLARE@': between(plus, '/// This macro declares an auto-parsed standard python method',
                                            '/// This macro declares a python method that uses keywords.', False),
    }
    text = (ROOT / 'tests/stock_screener_host.cpp').read_text()
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for fake in ('pch.hpp', 'pyscript/script.hpp', 'pyscript/pyobject_plus.hpp'):
        (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
        (directory / 'include' / fake).write_text('#pragma once\n')
    source = directory / 'host.cpp'
    source.write_text(generated_source(sources))
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


class StockScreenerTest(unittest.TestCase):
    def test_original_pyc_consumers_over_the_binding(self):
        package = scripts_package(self)
        sources = engine_sources(self)
        with tempfile.TemporaryDirectory(prefix='screener-cpython-') as directory:
            binary = build_host(self, Path(directory), sources)
            for mode in ('absent', 'port'):
                result = subprocess.run([str(binary), str(ROOT / 'tests/stock_screener_consumer.py'),
                                         str(package), mode], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(result.stdout.strip())

    def test_original_x86_methods_match_the_auto_parser_contract(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_screener_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        report = oracle.run(Path(client) / 'WorldOfTanks.exe')
        self.assertEqual(report, oracle.expected())
        print('Original Screener methods matched the PY_AUTO contract: %d calls' % len(report))


if __name__ == '__main__':
    unittest.main()
