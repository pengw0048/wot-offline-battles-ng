"""Effect-light property dispatch, real scene owner and original PYC producer."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'bigworld'))
sys.path.insert(0, str(ROOT / 'tests'))
import patch_stock_effect_lights as patcher
from test_stock_screener import between
from test_stock_vector4_basic import block

FILES = ('lib/romp/py_omni_light.hpp', 'lib/romp/py_omni_light.cpp',
         'lib/compiled_space/omni_light_embodiment.hpp', 'lib/compiled_space/omni_light_embodiment.cpp',
         'lib/romp/chunk_omni_light_embodiment.hpp', 'lib/romp/chunk_omni_light_embodiment.cpp',
         'lib/pyscript/pyobject_plus.hpp', 'lib/pyscript/pyobject_plus.cpp',
         'lib/pyscript/script.hpp', 'lib/pyscript/script.cpp',
         'lib/pyscript/script_math.hpp', 'lib/pyscript/script_math.cpp')


def sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file(): test.skipTest('Pinned public engine archive required')
    values = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for name in FILES:
                if member.name.endswith('/programming/bigworld/' + name):
                    values[name] = tar.extractfile(member).read().decode('utf-8-sig')
            if len(values) == len(FILES): break
    test.assertEqual(set(values), set(FILES))
    return values


def prepare(root, values):
    for name, value in values.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)


def host(root):
    def read(name): return (root / name).read_text()
    def engine(name): return re.sub(r'^#include .*\n', '', read(name), flags=re.MULTILINE)
    plus = read('lib/pyscript/pyobject_plus.hpp')
    plus_cpp = read('lib/pyscript/pyobject_plus.cpp')
    script = read('lib/pyscript/script.hpp')
    body = read('lib/pyscript/script.cpp')
    math_header=read('lib/pyscript/script_math.hpp')
    math_source=read('lib/pyscript/script_math.cpp')
    converters=script[script.index('#define PY_SCRIPT_CONVERTERS( CLASS )'):script.index('// Section: Macros: Module Links')]
    converters=converters.replace('\\\n','\n')
    smart=re.search(r'int Script::setData\(\s*PyObject \* pObject,\s*SmartPointer<CLASS> & rpDerived',converters)
    assert smart is not None
    scalar = ''.join(between(body, 'int Script::setData( PyObject * pObject, ' + kind, '\n}\n')
                     for kind in ('bool & rBool,', 'int & rInt,', 'double & rDouble,', 'float & rFloat,'))
    macros = between(script, '// Section: Macros: Module Links', '// Section: Macros: Enums', False)
    macros += between(script, '// Section: Macros: Recursion utilities', '// Section: PyModuleLink', False)
    replacements = {
        '@SCRIPT_BODIES@': scalar + between(body, 'PyObject * Script::argCountError(', '\n}\n'),
        '@PYAUTO_MACROS@': macros,
        '@PYOBJECT_PLUS_HEADER@': plus[plus.index('/// This must be the first line'):plus.index('// Section: Weak-referenceable macros')],
        '@PYOBJECT_PLUS_SOURCE@': plus_cpp[plus_cpp.index('// Base needs to be 0.'):plus_cpp.index('// Section: PyObjectPlusWithWeakReference')],
        '@PY_OMNI_HEADER@': engine('lib/romp/py_omni_light.hpp'),
        '@PY_OMNI_SOURCE@': engine('lib/romp/py_omni_light.cpp'),
        '@OMNI_HEADER@': engine('lib/compiled_space/omni_light_embodiment.hpp'),
        '@OMNI_SOURCE@': engine('lib/compiled_space/omni_light_embodiment.cpp'),
        '@VECTOR4_CONVERSION@':block(body,'int Script::setData( PyObject * pObject, Vector4 & rVector,'),
        '@VECTOR4_BASIC_CLASS@':block(math_header,'class Vector4Basic :'),
        '@VECTOR4_BASIC_CONSTRUCTOR@':block(math_source,'Vector4Basic::Vector4Basic('),
        '@VECTOR4_COERCE@':block(math_source,'PyObjectPtr Vector4Provider::coerce('),
        '@VECTOR4_POINTER_CONVERTER@':block(converters,smart.group()).replace(
            '#CLASS','"Vector4Provider"').replace('CLASS','Vector4Provider'),
        '@VECTOR4_VALUE_GETTER@':block(math_source,'PyObject * Vector4Provider::pyGet_value()'),
    }
    text = (ROOT / 'tests/stock_effect_lights_host.cpp').read_text()
    for key, value in replacements.items(): text = text.replace(key, value)
    return text


class EffectLightsTest(unittest.TestCase):
    def test_original_native_sampling_and_shadow_owner(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('Exact #1513 client required')
        try:
            from stock_effect_lights_oracle import audit
        except ImportError:
            self.skipTest('Unicorn and pefile required')
        result = audit(Path(client) / 'WorldOfTanks.exe')
        self.assertEqual(result['sampled_fields'], ('bounds', 'shader'))
        self.assertEqual(result['shadow_flags'], [0, 1, 0, 1])

    def test_patch_is_atomic_idempotent_and_keeps_original_tick(self):
        values = sources(self)
        with tempfile.TemporaryDirectory(prefix='effect-light-patch-') as folder:
            root = Path(folder)
            prepare(root, values)
            patcher.patch(root)
            first = {name: (root / name).read_text() for name in FILES}
            patcher.patch(root)
            self.assertEqual(first, {name: (root / name).read_text() for name in FILES})
            for name in ('lib/compiled_space/omni_light_embodiment.cpp', 'lib/romp/chunk_omni_light_embodiment.cpp'):
                class_name = 'ChunkOmniLightEmbodiment' if '/romp/' in name else 'OmniLightEmbodiment'
                start = 'void ' + class_name + '::doTick('
                self.assertEqual(between(values[name], start, '\n}\n'), between(first[name], start, '\n}\n'))
            prepare(root, values)
            (root / FILES[5]).write_text(values[FILES[5]].replace('bool ChunkOmniLightEmbodiment::visible() const', 'DRIFT'))
            before = {name: (root / name).read_bytes() for name in FILES}
            with self.assertRaises(ValueError): patcher.patch(root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in FILES})

    def test_original_pyc_and_production_light_owner(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists(): self.skipTest('CPython 2.7 development files required')
        values = sources(self)
        flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='effect-light-host-') as folder:
            root = Path(folder)
            prepare(root, values)
            for patched in (False, True):
                if patched: patcher.patch(root)
                source = root / 'host.cpp'; source.write_text(host(root))
                binary = root / 'host'
                subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-Wno-writable-strings',
                                '-fsanitize=address,undefined', *flags, '-UNDEBUG', str(source),
                                *libs, '-o', str(binary)], check=True)
                result = subprocess.run([str(binary), str(ROOT / 'tests/stock_effect_lights_consumer.py')],
                                        capture_output=True, text=True)
                if not patched:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('AttributeError:', result.stderr)
                    self.assertIn('colorAnimator', result.stderr)
                    continue
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('Effect light owner and provider lifetime passed', result.stdout)
                if os.environ.get('WOT_0922_CLIENT'):
                    self.assertIn('Original EffectsList light lifecycle passed', result.stdout)


if __name__ == '__main__': unittest.main()
