"""GUI.Flash.wg_inputKeyMode: the #1513 key-routing mode of FlashGUIComponent.

Real CPython 2.7 runs the engine's attribute macros and uint conversions, the public
SimpleGUIComponent key guards and SimpleGUI root loop, and the pristine or patched
FlashGUIComponent, driven by the original fade_window, fader_manager, Flash, Barracks and
BattleEntry PYC. The Unicorn oracle runs the exact #1513 getter, setter and input handlers.
Neither proves GFx key handling, Windows focus or the garage UI.
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
FLASH_FILES = ('lib/scaleform/flash_gui_component.hpp', 'lib/scaleform/flash_gui_component.cpp')
ENGINE_FILES = FLASH_FILES + ('lib/pyscript/pyobject_plus.hpp', 'lib/pyscript/script.cpp',
                              'lib/ashes/simple_gui_component.cpp', 'lib/ashes/simple_gui_component.ipp',
                              'lib/ashes/simple_gui.cpp')


def load_patch():
    spec = importlib.util.spec_from_file_location('patch_stock_flash_input_mode',
                                                  ROOT / 'bigworld/patch_stock_flash_input_mode.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def between(text, begin, end, include_end=True):
    start = text.rindex('\n', 0, text.index(begin)) + 1
    stop = text.index(end, start)
    return text[start:stop + (len(end) if include_end else 0)]


def pinned_sources(test):
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


def flash_sources(sources):
    return {name: sources[name] for name in FLASH_FILES}


def generated_source(sources, flash):
    pyobject, script = sources['lib/pyscript/pyobject_plus.hpp'], sources['lib/pyscript/script.cpp']
    component, gui = sources['lib/ashes/simple_gui_component.cpp'], sources['lib/ashes/simple_gui.cpp']
    header, body = flash['lib/scaleform/flash_gui_component.hpp'], flash['lib/scaleform/flash_gui_component.cpp']
    replacements = {
        '@SCRIPT_UINT@': between(script, 'int Script::setData( PyObject * pObject, uint & rUint,', '\n}\n') +
        between(script, 'PyObject * Script::getData( const uint data )', '\n}\n'),
        '@PY_ATTRIBUTE_DECLARE_MACROS@': between(
            pyobject, '/// This macro declares a python attribute that is implemented elsewhere',
            '/**\n *\tPyObjectPlus-derived class static data instantiation macros', False),
        '@PY_ATTRIBUTE_DEFINE_MACROS@': between(pyobject, '#define PY_ATTRIBUTE_CREATE_WRAPPER( NAME, ATTRIB_NAME )',
                                                '/// This macro defines the factory method for the given class',
                                                False),
        '@HANDLES_INPUT@': between(sources['lib/ashes/simple_gui_component.ipp'],
                                   'INLINE bool SimpleGUIComponent::handlesInput() const', '\n}\n'),
        '@COMPONENT_HANDLE_KEY@': between(component, 'bool SimpleGUIComponent::handleKeyEvent( const KeyEvent & event )',
                                          '\n}\n'),
        '@SIMPLE_GUI@': between(gui, 'int depth_compare_gui_components', '\n}\n') +
        between(gui, 'void\nSimpleGUI::addSimpleComponent', '\n}\n') +
        between(gui, 'void\nSimpleGUI::removeSimpleComponent', '\n}\n') +
        between(gui, 'void\nSimpleGUI::reSort( void )', '\n}\n') +
        between(gui, 'bool SimpleGUI::handleKeyEvent( const KeyEvent & event )', '\n}\n'),
        '@FLASH_CLASS@': between(header, 'class FlashGUIComponent : public SimpleGUIComponent', '\n};\n'),
        '@FLASH_DEFINITIONS@': between(body, 'PY_TYPEOBJECT( FlashGUIComponent )', '\nvoid\nFlashGUIComponent::update',
                                       False),
        '@FLASH_INPUT@': between(body, 'bool FlashGUIComponent::handleMouseEvent( const MouseEvent & event )',
                                 '} //namespace ScaleformBW', False),
    }
    text = (ROOT / 'tests/stock_flash_input_mode_host.cpp').read_text()
    for key, value in replacements.items():
        assert text.count(key) == 1, key
        text = text.replace(key, value)
    return text


def build_host(test, directory, sources, flash):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists():
        test.skipTest('Pinned CPython 2.7 development files unavailable')
    source = directory / 'host.cpp'
    source.write_text(generated_source(sources, flash))
    flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if flag != '-DNDEBUG']
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = directory / 'host'
    subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wall', '-Werror', '-Wno-deprecated-register',
                    '-Wno-writable-strings', '-Wno-unused-function', '-Wno-missing-field-initializers',
                    str(source), *flags, *libs, '-o', str(binary)], check=True)
    return binary


def client_package(test):
    client = os.environ.get('WOT_0922_CLIENT')
    if not client:
        test.skipTest('WOT_0922_CLIENT is required for the original PYC')
    return Path(client) / 'res/packages/scripts.pkg'


class StockFlashInputModeTest(unittest.TestCase):
    def test_patch_renames_only_the_attribute(self):
        patch = load_patch()
        pristine = flash_sources(pinned_sources(self))
        patched = patch.patch_sources(pristine)
        self.assertEqual(patch.patch_sources(patched), patched)
        for name in FLASH_FILES:
            removed = sorted(set(pristine[name].splitlines()) - set(patched[name].splitlines()))
            added = sorted(set(patched[name].splitlines()) - set(pristine[name].splitlines()))
            self.assertEqual([line.replace('wg_inputKeyMode', 'inputKeyMode') for line in added], removed)
        self.assertEqual(sorted(line.strip() for name in FLASH_FILES for line in patched[name].splitlines()
                                if 'wg_inputKeyMode' in line),
                         ['/*~ attribute FlashGUIComponent.wg_inputKeyMode', 'PY_ATTRIBUTE(wg_inputKeyMode)',
                          'PY_RW_ATTRIBUTE_DECLARE( inputKeyMode_, wg_inputKeyMode )'])

    def test_patch_rejects_every_partial_duplicate_or_mixed_state_without_writing(self):
        patch = load_patch()
        pristine = flash_sources(pinned_sources(self))
        patched = patch.patch_sources(pristine)
        header, body = FLASH_FILES

        def changed(state, name, text):
            result = dict(state)
            result[name] = text
            return result
        mutations = {}
        for index, (name, before, after) in enumerate(patch.SEAMS):
            mutations['final seam %d reverted' % index] = changed(patched, name, patched[name].replace(after, before))
            mutations['pristine seam %d applied' % index] = changed(pristine, name,
                                                                    pristine[name].replace(before, after))
            mutations['final seam %d deleted' % index] = changed(patched, name, patched[name].replace(after, ''))
            mutations['pristine seam %d deleted' % index] = changed(pristine, name, pristine[name].replace(before, ''))
            mutations['final seam %d duplicated' % index] = changed(patched, name, patched[name] + after)
            mutations['pristine seam %d duplicated' % index] = changed(pristine, name, pristine[name] + before)
            mutations['final with old seam %d' % index] = changed(patched, name, patched[name] + before)
        for index, (name, text) in enumerate(patch.ANCHORS):
            for label, state in (('final', patched), ('pristine', pristine)):
                mutations['%s anchor %d deleted' % (label, index)] = changed(state, name, state[name].replace(text, ''))
                mutations['%s anchor %d duplicated' % (label, index)] = changed(state, name, state[name] + text)
        mutations['header final, source pristine'] = changed(pristine, header, patched[header])
        mutations['header pristine, source final'] = changed(pristine, body, patched[body])
        mutations['extra token in pristine header'] = changed(pristine, header, pristine[header] + '// wg_inputKeyMode\n')
        mutations['extra token in final source'] = changed(patched, body, patched[body] + '// wg_inputKeyMode\n')
        mutations['alias added'] = changed(patched, body, patched[body].replace(
            '\tPY_ATTRIBUTE(wg_inputKeyMode)\n', '\tPY_ATTRIBUTE(wg_inputKeyMode)\n\tPY_ATTRIBUTE(inputKeyMode)\n'))
        mutations['mode 2 reader changed'] = changed(patched, body, patched[body].replace(
            '2 != inputKeyMode_', 'inputKeyMode_ < 2'))
        with tempfile.TemporaryDirectory(prefix='flash-input-mode-patch-') as directory:
            paths = {name: Path(directory) / name for name in FLASH_FILES}
            paths[header].parent.mkdir(parents=True)
            for label, state in mutations.items():
                self.assertNotEqual(state, patched, label)
                with self.assertRaises(ValueError, msg=label):
                    patch.patch_sources(state)
                for name, path in paths.items():
                    path.write_text(state[name], encoding='utf-8')
                with self.assertRaises(ValueError, msg=label):
                    patch.patch(directory)
                for name, path in paths.items():
                    self.assertEqual(path.read_text(encoding='utf-8'), state[name], label)
            for name, path in paths.items():
                path.write_text(pristine[name], encoding='utf-8')
            patch.patch(directory)
            self.assertEqual({name: path.read_text(encoding='utf-8') for name, path in paths.items()}, patched)
            patch.patch(directory)
            self.assertEqual({name: path.read_text(encoding='utf-8') for name, path in paths.items()}, patched)

    def test_original_pyc_routes_keys_through_the_production_component(self):
        package = client_package(self)
        sources = pinned_sources(self)
        patched = load_patch().patch_sources(flash_sources(sources))
        for mode, flash in (('pristine', flash_sources(sources)), ('patched', patched)):
            with tempfile.TemporaryDirectory(prefix='flash-input-mode-' + mode + '-') as directory:
                binary = build_host(self, Path(directory), sources, flash)
                result = subprocess.run([str(binary), str(ROOT / 'tests/stock_flash_input_mode_consumer.py'), mode,
                                         str(package)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(mode + ':', result.stdout)
                print(result.stdout.strip())

    def test_original_instructions_match_production(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import stock_flash_input_mode_oracle as oracle
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        sources = pinned_sources(self)
        patched = load_patch().patch_sources(flash_sources(sources))
        with tempfile.TemporaryDirectory(prefix='flash-input-mode-oracle-') as directory:
            path = Path(directory)
            binary = build_host(self, path, sources, patched)
            report = oracle.compare(Path(client) / 'WorldOfTanks.exe', binary,
                                    ROOT / 'tests/stock_flash_input_mode_trace.py', path)
        print('Original FlashGUIComponent input-mode instructions matched production:', report)


if __name__ == '__main__':
    unittest.main()
