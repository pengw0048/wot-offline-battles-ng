"""Native descriptor dispatch regression for the exact #1513 garage Tween producer.

Compile the SDK proxy get/set, GFx conversion, matrix accessor, Script Matrix
conversion, and real attribute macros into CPython 2.7. GFx storage and Math's
Python shell are boundary fixtures; this does not prove Windows SWF rendering.
"""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from test_stock_vector4_basic import block
from test_stock_flash_input_mode import between

FILES = ('lib/scaleform/py_gfx_value.cpp', 'lib/scaleform/py_gfx_display_object.cpp',
         'lib/scaleform/py_gfx_display_object.hpp', 'lib/pyscript/pyobject_plus.hpp',
         'lib/pyscript/pyobject_plus.cpp', 'lib/pyscript/script.cpp')


def load_patch():
    spec = importlib.util.spec_from_file_location('flash_display_patch', ROOT / 'bigworld/patch_stock_flash_display_object.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sources(test):
    path = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not path.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
            test.fail('Configured public SDK archive does not exist: ' + str(path))
        test.skipTest('Pinned public SDK archive unavailable')
    result = {}
    with tarfile.open(path) as archive:
        for member in archive:
            for relative in FILES:
                if member.name.endswith('/programming/bigworld/' + relative):
                    result[relative] = archive.extractfile(member).read().decode('utf-8')
    test.assertEqual(set(result), set(FILES))
    return result


def generated(source):
    value = source[FILES[0]]
    display = source[FILES[1]]
    py = source[FILES[3]]
    script = source[FILES[5]]
    replacements = {
        '@OBJECT_DISPATCH@': '\n'.join(block(source[FILES[4]], signature) for signature in (
            'ScriptObject PyObjectPlus::pyGetAttribute(', 'bool PyObjectPlus::pySetAttribute(')),
        '@MATRIX_CONVERSION@': '\n'.join(block(script, signature) for signature in (
            'int Script::setData( PyObject * pObject, Matrix & rMatrix,', 'PyObject * Script::getData( const Matrix & data )')),
        '@ATTRIBUTE_MACROS@': between(py, '/// This macro declares a python attribute that is implemented elsewhere',
                                      '/**\n *\tPyObjectPlus-derived class static data instantiation macros', False),
        '@ATTRIBUTE_DEFINITIONS@': between(py, '#define PY_ATTRIBUTE_CREATE_WRAPPER( NAME, ATTRIB_NAME )',
                                           '/// This macro defines the factory method for the given class', False),
        '@DISPLAY_MATRIX_DECLARATION@': next(line.strip() for line in source[FILES[2]].splitlines()
                                             if 'PY_RW_ACCESSOR_ATTRIBUTE_DECLARE(Matrix, displayMatrix, displayMatrix)' in line),
        '@VALUE_CONVERSIONS@': '\n'.join(block(value, signature) for signature in (
            'PyObject *PyGFxValue::convertValueToPython(GFx::Value &gfxValue, PyMovieView *pyMovieView, PyGFxValue *pyOwner)',
            'GFx::Value PyGFxValue::convertValueToGFx(PyObject *pyValue, PyMovieView *pyMovieView)')),
        '@VALUE_DISPATCH@': '\n'.join(block(value, signature) for signature in (
            'ScriptObject PyGFxValue::pyGetAttribute(', 'bool PyGFxValue::pySetAttribute(',
            'void PyGFxValue::resync()', 'void PyGFxValue::collectChildValues()')),
        '@DISPLAY_MATRIX_OWNER@': '\n'.join(block(display, signature) for signature in (
            'inline void convertFromMatrix2x4(', 'inline SF::Render::Matrix2x4<float> convertToMatrix2x4(',
            'Matrix PyGFxDisplayObject::displayMatrix()', 'void PyGFxDisplayObject::displayMatrix(',
            'BW::string PyGFxDisplayObject::text()', 'void PyGFxDisplayObject::text(')),
    }
    result = (HERE / 'stock_flash_display_object_host.cpp').read_text()
    for token, text in replacements.items():
        assert result.count(token) == 1, token
        result = result.replace(token, text)
    return result


class FlashDisplayObjectTest(unittest.TestCase):
    def test_patch_is_atomic_idempotent_and_preserves_other_owners(self):
        patch = load_patch()
        original = sources(self)
        patched = patch.patch_sources(original)
        self.assertEqual(patch.patch_sources(patched), patched)
        self.assertEqual(patched[FILES[0]], original[FILES[0]].replace(patch.BEFORE, patch.AFTER, 1))
        for name in FILES[1:]: self.assertEqual(original[name], patched[name])
        for text in (original[FILES[0]].replace(patch.BEFORE, '', 1),
                     original[FILES[0]] + patch.BEFORE,
                     patched[FILES[0]] + patch.AFTER,
                     original[FILES[0]] + '\n' + patch.MARKER,
                     patched[FILES[0]].replace('PyErr_Clear();', '/* missing error reset */', 1)):
            changed = dict(original); changed[FILES[0]] = text
            with self.assertRaises(ValueError): patch.patch_sources(changed)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / patch.SOURCE
                path.parent.mkdir(parents=True)
                path.write_text(text)
                with self.assertRaises(ValueError): patch.patch(directory)
                self.assertEqual(path.read_text(), text)

    def test_collection_patch_composes_in_both_orders(self):
        original = sources(self)
        spec = importlib.util.spec_from_file_location('flash_collection_patch', ROOT / 'bigworld/patch_stock_flash_collections.py')
        collections = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(collections)
        dispatch = load_patch()
        first = dispatch.patch_sources(original)
        first[FILES[0]] = collections.patch_source(first[FILES[0]])
        second = dict(original)
        second[FILES[0]] = collections.patch_source(second[FILES[0]])
        second = dispatch.patch_sources(second)
        self.assertEqual(first, second)
        self.assertEqual(dispatch.patch_sources(first), first)
        self.assertEqual(collections.patch_source(first[FILES[0]]), first[FILES[0]])

    def test_original_x86_dispatch_and_matrix_descriptor_contract(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('Exact #1513 executable unavailable')
        try:
            from stock_flash_display_object_oracle import OriginalDisplayObject
        except ImportError as error: self.skipTest('Unicorn/pefile unavailable: ' + str(error))
        original = OriginalDisplayObject(Path(client) / 'WorldOfTanks.exe')
        for name in ('displayMatrix', 'text', 'dynamicMember', 'arbitraryProperty'):
            self.assertEqual(original.route(name, True), ['native'])
            self.assertEqual(original.route(name, False), ['native', 'clear', 'convert'])
        self.assertEqual(original.route('script', False), ['script'])
        import random
        rng = random.Random(1513)
        identity = tuple(1.0 if i // 4 == i % 4 else 0.0 for i in range(16))
        for unused in range(40):
            values = tuple(float(rng.randint(-1000, 1000)) / 8 for i in range(8))
            expected = list(identity)
            for i, j in ((0, 0), (1, 1), (4, 4), (5, 5), (12, 3), (13, 7)):
                expected[i] = values[j]
            self.assertEqual(original.get(values), tuple(expected))
            self.assertEqual(original.get(values, False), identity)
            matrix = tuple(float(rng.randint(-1000, 1000)) / 8 for i in range(16))
            self.assertEqual(original.set(matrix), (matrix[0], matrix[1], 0, matrix[12],
                                                     matrix[4], matrix[5], 0, matrix[13]))
            self.assertIsNone(original.set(matrix, False))

    def test_real_sdk_dispatch_and_original_tween_negative_control(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.is_file():
            if 'WOT_PYTHON27' in os.environ:
                self.fail('Configured CPython 2.7 development files do not exist: ' + str(config))
            self.skipTest('CPython 2.7 development files unavailable')
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('Exact #1513 PYC resources unavailable')
        package = Path(client) / 'res/packages/scripts.pkg'
        self.assertTrue(package.is_file(), 'Configured #1513 script package does not exist: ' + str(package))
        original = sources(self)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            flags = [v for v in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True)) if v != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            patched = load_patch().patch_sources(original)
            spec = importlib.util.spec_from_file_location('flash_collection_host_patch', ROOT / 'bigworld/patch_stock_flash_collections.py')
            collections = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(collections)
            combined = dict(patched)
            combined[FILES[0]] = collections.patch_source(combined[FILES[0]])
            for mode, source in (('pristine', original), ('patched', patched), ('combined', combined)):
                cpp, binary = directory / (mode + '.cpp'), directory / mode
                cpp.write_text(generated(source))
                subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wall', '-Werror', '-Wno-deprecated-register',
                                '-Wno-writable-strings', '-Wno-unused-function', '-Wno-missing-field-initializers',
                                str(cpp), *flags, *libs, '-o', str(binary)], check=True)
                result = subprocess.run([str(binary), str(HERE / 'stock_flash_display_object_consumer.py'), mode,
                                         str(package)], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('Negative control:' if mode == 'pristine' else 'repeated updates', result.stdout)


if __name__ == '__main__': unittest.main()
