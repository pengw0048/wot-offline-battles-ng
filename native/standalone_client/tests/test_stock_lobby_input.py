"""BigWorld keyboard mode, lock state and input language: the #1513 lobby input family.

Real CPython 2.7 runs the engine's PY_AUTO macros, the pristine or patched
lib/scaleform/util.cpp translator and the production wg_getLangCode binding,
driven by the original lobby, battle, settings and login PYC call sites. The
Unicorn oracle runs the exact #1513 instructions. Neither proves Windows key
state, keyboard layouts or the garage UI; the Win32 probe covers the OS sources.
"""
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

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
ENGINE_FILES = ('lib/scaleform/util.cpp', 'lib/pyscript/script.hpp', 'lib/pyscript/script.cpp',
                'lib/input/key_code.hpp')
SDK_REVISION = '7d92be560bf1b2e4ecae9ac69e340d4cae9a557e'
SDK_FILES = ('Src/Kernel/SF_KeyCodes.h', 'Src/GFx/GFx_Event.h')


def load_patch():
    spec = importlib.util.spec_from_file_location('patch_stock_lobby_input',
                                                  ROOT / 'bigworld/patch_stock_lobby_input.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def between(text, begin, end, include_end=True):
    start = text.rindex('\n', 0, text.index(begin)) + 1
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


def pinned_sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    checkout = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT', '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
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
    try:
        for name in SDK_FILES:
            sources[name] = subprocess.run(['git', '-C', str(checkout), 'show', SDK_REVISION + ':' + name],
                                           check=True, capture_output=True).stdout.decode('utf-8').replace('\r\n', '\n')
    except (OSError, subprocess.CalledProcessError):
        test.skipTest('Pinned Scaleform SDK checkout unavailable')
    test.assertEqual(set(sources), set(ENGINE_FILES + SDK_FILES))
    return sources


def generated_source(template, sources, util):
    script_hpp, script_cpp = sources['lib/pyscript/script.hpp'], sources['lib/pyscript/script.cpp']
    keys, events = sources['Src/Kernel/SF_KeyCodes.h'], sources['Src/GFx/GFx_Event.h']
    replacements = {
        '@SCRIPT_BODIES@': between(script_cpp, 'int Script::setData( PyObject * pObject, bool & rBool,', '\n}\n') +
        between(script_cpp, 'PyObject * Script::getData( const bool data )', '\n}\n') +
        between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
        '@KEY_CODES@': between(sources['lib/input/key_code.hpp'], 'const uint32 MODIFIER_SHIFT',
                               '\t\tNUM_KEYS\n\t};\n') + '}\n',
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
        '@SF_KEYS@': between(keys, 'class Key\n', '\nenum PadKeyCode', False),
        '@GFX_EVENTS@': between(events, 'class Event : public NewOverrideBase<Stat_Default_Mem>',
                                '\nclass CharEvent', False),
        '@UTIL@': between(util, '// if true, translateKeyCode will allow all keys', '}\t//namespace ScaleformBW',
                          False),
    }
    text = (ROOT / 'tests' / template).read_text()
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources, util):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for fake in ('pch.hpp', 'pyscript/script.hpp', 'windows.h'):
        (directory / 'include' / fake).parent.mkdir(parents=True, exist_ok=True)
        (directory / 'include' / fake).write_text('#pragma once\n')
    source = directory / 'host.cpp'
    source.write_text(generated_source('stock_lobby_input_host.cpp', sources, util))
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wall', '-Werror', '-Wno-deprecated-register',
                    '-Wno-writable-strings', '-Wno-unused-function', '-I', str(directory / 'include'),
                    '-I', str(ROOT / 'bigworld'), str(source), *flags, *libs, '-o', str(binary)], check=True)
    return binary


def scripts_package(test):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client:
        test.skipTest('WOT_0922_CLIENT is required for the original PYC call sites')
    return Path(client) / 'res/packages/scripts.pkg'


class StockLobbyInputTest(unittest.TestCase):
    def test_patch_changes_only_the_names_and_right_hand_entries(self):
        patch = load_patch()
        pristine = pinned_sources(self)['lib/scaleform/util.cpp']
        patched = patch.patch_source(pristine)
        self.assertEqual(patch.patch_source(patched), patched)
        removed = set(pristine.splitlines()) - set(patched.splitlines())
        added = set(patched.splitlines()) - set(pristine.splitlines())
        self.assertEqual(len(removed), 11)
        self.assertEqual(sorted(line.strip() for line in added), sorted([
            'PY_AUTO_MODULE_FUNCTION(RETDATA, wg_isCapsLockOn, END, BigWorld)',
            'PY_AUTO_MODULE_FUNCTION(RETDATA, wg_isNumLockOn, END, BigWorld)',
            'PY_AUTO_MODULE_FUNCTION(RETDATA, wg_isScrollLockOn, END, BigWorld)',
            'PY_AUTO_MODULE_FUNCTION(RETVOID, wg_setRedefineKeysMode, ARG(bool, END), BigWorld)',
            'bool wg_isCapsLockOn()', 'bool wg_isNumLockOn()', 'bool wg_isScrollLockOn()',
            'void wg_setRedefineKeysMode(bool b)',
            '{ KeyCode::KEY_RALT,\t\t\tSF::Key::RightAlt },',
            '{ KeyCode::KEY_RCONTROL,\t\tSF::Key::RightControl },',
            '{ KeyCode::KEY_RSHIFT,\t\t\tSF::Key::RightShift },']))
        self.assertEqual(patched.count('SF::Key::RightShift'), 2)
        self.assertEqual(patched.count('SF::Key::RightControl'), 2)
        with self.assertRaises(ValueError):
            patch.patch_source(pristine.replace('{ KeyCode::KEY_RALT,\t\t\tSF::Key::Alt },', ''))
        with self.assertRaises(ValueError):
            patch.patch_source(pristine.replace('\tbool isNumLockOn()\n', '\tbool isNumLockOn( )\n'))

    def test_patch_rejects_every_partial_duplicate_or_mixed_state_without_writing(self):
        patch = load_patch()
        pristine = pinned_sources(self)['lib/scaleform/util.cpp']
        patched = patch.patch_source(pristine)
        names = [after for unused, after in patch.NAMES]
        tables = [after for unused, after, unused in patch.TABLES]
        mutations = {
            'reverted RightShift': patched.replace('SF::Key::RightShift', 'SF::Key::Shift'),
            'one reverted RightShift': patched.replace('SF::Key::RightShift', 'SF::Key::Shift', 1),
            'reverted RightControl': patched.replace('SF::Key::RightControl', 'SF::Key::Control', 1),
            'reverted RightAlt': patched.replace('SF::Key::RightAlt', 'SF::Key::Alt'),
            'wrong right-hand value': patched.replace('SF::Key::RightAlt', 'SF::Key::RightControl'),
            'old registration added': patched + 'PY_AUTO_MODULE_FUNCTION(RETDATA, isCapsLockOn, END, BigWorld)\n',
            'old table entry added': patched + patch.TABLES[2][0],
            'one name reverted': patched.replace(names[3], patch.NAMES[3][0]),
            'pristine with one new name': pristine.replace(patch.NAMES[0][0], names[0]),
            'pristine with one new entry': pristine.replace(patch.TABLES[2][0], tables[2]),
            'duplicate pristine entry': pristine + patch.TABLES[0][0],
        }
        for index, line in enumerate(names + tables):
            mutations['deleted final %d' % index] = patched.replace(line, '', 1)
            mutations['duplicated final %d' % index] = patched + line
        for token, unused in patch.TOKENS:
            mutations['extra ' + token] = patched + '// ' + token + '\n'
        with tempfile.TemporaryDirectory(prefix='lobby-input-patch-') as directory:
            path = Path(directory) / 'lib/scaleform/util.cpp'
            path.parent.mkdir(parents=True)
            for name, source in mutations.items():
                self.assertNotEqual(source, patched, name)
                with self.assertRaises(ValueError, msg=name):
                    patch.patch_source(source)
                path.write_text(source, encoding='utf-8')
                with self.assertRaises(ValueError, msg=name):
                    patch.patch(directory)
                self.assertEqual(path.read_text(encoding='utf-8'), source, name)
            path.write_text(pristine, encoding='utf-8')
            patch.patch(directory)
            self.assertEqual(path.read_text(encoding='utf-8'), patched)
            patch.patch(directory)
            self.assertEqual(path.read_text(encoding='utf-8'), patched)

    def test_original_call_sites_reach_the_translator_through_the_real_binding(self):
        package = scripts_package(self)
        sources = pinned_sources(self)
        patched = load_patch().patch_source(sources['lib/scaleform/util.cpp'])
        for mode, util, expected in (('pristine', sources['lib/scaleform/util.cpp'], 'pristine:'),
                                     ('patched', patched, 'patched:')):
            with tempfile.TemporaryDirectory(prefix='lobby-input-' + mode + '-') as directory:
                binary = build_host(self, Path(directory), sources, util)
                result = subprocess.run([str(binary), str(ROOT / 'tests/stock_lobby_input_consumer.py'), mode,
                                         str(package)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(expected, result.stdout)
                print(result.stdout.strip())

    def test_win32_probe_builds_for_i686(self):
        compiler = shutil.which(os.environ.get('WIN32_CXX', 'i686-w64-mingw32-g++'))
        if not compiler:
            self.skipTest('No i686 Windows cross compiler; build the probe on Windows instead')
        sources = pinned_sources(self)
        patched = load_patch().patch_source(sources['lib/scaleform/util.cpp'])
        with tempfile.TemporaryDirectory(prefix='lobby-input-probe-') as directory:
            source = Path(directory) / 'stock_lobby_input_probe.cpp'
            source.write_text(generated_source('stock_lobby_input_probe.cpp', sources, patched))
            target = Path(directory) / 'stock_lobby_input_probe.exe'
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-static', '-I', str(ROOT / 'bigworld'), str(source), '-o', str(target)], check=True)
            self.assertEqual(target.read_bytes()[:2], b'MZ')

    def test_original_instructions_match_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_lobby_input_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        sources = pinned_sources(self)
        patched = load_patch().patch_source(sources['lib/scaleform/util.cpp'])
        with tempfile.TemporaryDirectory(prefix='lobby-input-oracle-') as directory:
            path = Path(directory)
            binary = build_host(self, path, sources, patched)
            report = oracle.compare(Path(client) / 'WorldOfTanks.exe', binary,
                                    ROOT / 'tests/stock_lobby_input_trace.py', path)
        print('Original lobby input instructions matched production:', report)


if __name__ == '__main__':
    unittest.main()
