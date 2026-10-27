"""Hangar presentation and input: the #1513 Influx draw, GUI background, model
texture, cursor sync, focus and edge-detect family.

Real CPython 2.7 runs the engine's PY_AUTO macros, the public Script bodies and
the production stock_hangar_presentation.cpp, driven by the original BattleLoading,
ClientHangarSpace, listener, Cursor and edge-detect PYC call sites. The Unicorn
oracle runs the exact #1513 wrappers. Neither proves rendering or Windows input.
"""
from pathlib import Path
import importlib.util
import os
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
ENGINE_FILES = ('lib/pyscript/script.hpp', 'lib/pyscript/script.cpp', 'lib/ashes/mouse_cursor.hpp',
                'lib/ashes/mouse_cursor.cpp', 'client/gui_app.cpp', 'client/lens_app.cpp',
                'lib/pyscript/pyobject_plus.hpp')
SET_DATA = ('bool & rBool,', 'float & rFloat,', 'double & rDouble,', 'BW::string & rString,')


def load_patch():
    spec = importlib.util.spec_from_file_location('patch_stock_hangar_presentation',
                                                  ROOT / 'bigworld/patch_stock_hangar_presentation.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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
    bodies = ''.join(between(script_cpp, 'int Script::setData( PyObject * pObject, ' + name, '\n}\n')
                     for name in SET_DATA)
    replacements = {
        '@SCRIPT_BODIES@': bodies + between(script_cpp, 'PyObject * Script::getData( const bool data )', '\n}\n') +
        between(script_cpp, 'PyObject * Script::argCountError(', '\n}\n'),
        '@PYAUTO_MACROS@': between(script_hpp, '// Section: Macros: Module Links', '// Section: Macros: Enums',
                                   False) +
        between(script_hpp, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False),
    }
    text = (ROOT / 'tests/stock_hangar_presentation_host.cpp').read_text()
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    for fake in ('pch.hpp', 'pyscript/script.hpp'):
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


class StockHangarPresentationTest(unittest.TestCase):
    def test_patch_reaches_one_final_state_and_rejects_partial_states(self):
        patch = load_patch()
        sources = {path: text for path, text in engine_sources(self).items() if path in patch.FILES}
        patched = patch.patch_sources(sources)
        self.assertEqual(patch.patch_sources(patched), patched)
        self.assertEqual(patch.state(sources), 'pristine')
        self.assertEqual(patch.state(patched), 'final')
        added = {path: sorted(set(patched[path].splitlines()) - set(sources[path].splitlines()))
                 for path in patch.FILES}
        self.assertEqual(added['lib/ashes/mouse_cursor.hpp'],
                         ['\tPY_RO_ATTRIBUTE_DECLARE( s_inFocus_, inFocus )', '\tstatic bool s_inFocus_;'])
        self.assertEqual(added['lib/ashes/mouse_cursor.cpp'],
                         ['\tPY_ATTRIBUTE( inFocus )', '\ts_inFocus_ = focus;',
                          'bool MouseCursor::s_inFocus_ = false;'])
        self.assertEqual(len(added['client/gui_app.cpp']), 5)
        # The focus byte is written after the original clip release, at the end.
        body = between(patched['lib/ashes/mouse_cursor.cpp'], 'void MouseCursor::staticSetFocus', '\n}\n')
        self.assertTrue(body.endswith('\t}\n\ts_inFocus_ = focus;\n}\n'), body)
        draw = between(patched['client/gui_app.cpp'], 'void GUIApp::draw()', '\n}\n')
        self.assertLess(draw.index('rp().beginGUIDraw();'), draw.index('drawStockGUIBackground()'))
        self.assertLess(draw.index('drawStockGUIBackground()'), draw.index('SimpleGUI::instance().draw();'))
        fini = between(patched['client/gui_app.cpp'], 'void GUIApp::fini()', '\n}\n')
        self.assertLess(fini.index('finaliseStockHangarPresentation'), fini.index('deleteGUI'))

        mutations = {}
        for index, (path, anchor, insertion, before) in enumerate(patch.EDITS):
            joined = patch.joined(anchor, insertion, before)
            mutations['reverted %d' % index] = dict(patched, **{path: patched[path].replace(joined, anchor)})
            mutations['partial %d' % index] = dict(sources, **{path: sources[path].replace(anchor, joined)})
            mutations['duplicated %d' % index] = dict(patched, **{path: patched[path] + insertion})
            mutations['missing anchor %d' % index] = dict(sources, **{path: sources[path].replace(anchor, '')})
            mutations['doubled anchor %d' % index] = dict(sources, **{path: sources[path] + anchor})
        for path, token, unused in patch.TOKENS:
            mutations['stray ' + path + token] = dict(sources, **{path: sources[path] + '// ' + token + '\n'})
        # Other patchers' text around the same anchors does not disturb this seam.
        foreign = dict(sources)
        foreign['client/gui_app.cpp'] = foreign['client/gui_app.cpp'].replace(
            '    rp().beginGUIDraw();\n', '    rp().beginGUIDraw();\n    drawStockBinoculars();\n')
        self.assertEqual(patch.state(patch.patch_sources(foreign)), 'final')
        with tempfile.TemporaryDirectory(prefix='hangar-presentation-patch-') as directory:
            root = Path(directory)
            for name, state in mutations.items():
                self.assertNotEqual(state, patched, name)
                with self.assertRaises(ValueError, msg=name):
                    patch.patch_sources(state)
                for path, text in state.items():
                    (root / path).parent.mkdir(parents=True, exist_ok=True)
                    (root / path).write_text(text, encoding='utf-8')
                with self.assertRaises(ValueError, msg=name):
                    patch.patch(root)
                for path, text in state.items():
                    self.assertEqual((root / path).read_text(encoding='utf-8'), text, name)
            for path, text in sources.items():
                (root / path).write_text(text, encoding='utf-8')
            patch.patch(root)
            patch.patch(root)
            for path, text in patched.items():
                self.assertEqual((root / path).read_text(encoding='utf-8'), text)

    def test_original_pyc_consumers_reach_the_native_owners(self):
        package = scripts_package(self)
        sources = engine_sources(self)
        with tempfile.TemporaryDirectory(prefix='hangar-presentation-cpython-') as directory:
            binary = build_host(self, Path(directory), sources)
            result = subprocess.run([str(binary), str(ROOT / 'tests/stock_hangar_presentation_consumer.py'),
                                     str(package)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_original_wrappers_match_the_production_contract(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_hangar_presentation_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        report = oracle.run(Path(client) / 'WorldOfTanks.exe')
        self.assertEqual(report, oracle.expected())
        print('Original hangar presentation wrappers matched production: %d calls' % len(report))


if __name__ == '__main__':
    unittest.main()
