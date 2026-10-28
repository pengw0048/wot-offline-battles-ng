"""Profiler metadata must not manufacture errors for legal native callbacks."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from test_stock_vector4_basic import block

spec = importlib.util.spec_from_file_location('callback_metadata_patch', ROOT / 'bigworld/patch_stock_script_callback_metadata.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
FILES = ('lib/pyscript/script.cpp', 'lib/pyscript/script.ipp', 'lib/cstdmf/config.hpp',
         'build/cmake/BWConfiguration_client.cmake')


def sources(test):
    path = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not path.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
            test.fail('Configured public SDK archive missing: ' + str(path))
        test.skipTest('Pinned public SDK archive required')
    result = {}
    with tarfile.open(path) as tar:
        for member in tar:
            for relative in FILES:
                if member.name.endswith('/programming/bigworld/' + relative):
                    result[relative] = tar.extractfile(member).read().decode('utf-8-sig')
    test.assertEqual(set(result), set(FILES))
    return result


def generated(source, script):
    component = (ROOT / 'bigworld/stock_vehicle_components.cpp').read_text()
    animation = (ROOT / 'bigworld/stock_battle_vehicle_animation.cpp').read_text()
    output = (HERE / 'stock_script_callback_metadata_host.cpp').read_text()
    values = {'@RESOLVER@': block(script, 'void resolvePythonModuleAndFunctionNames('),
              '@ASK@': block(script, 'PyObject * Script::ask('),
              '@CALL@': block(source['lib/pyscript/script.ipp'], 'INLINE bool call(').replace('INLINE ', '', 1),
              '@COMPONENT@': block(component, 'class ScriptComponentState :') + ';',
              '@NATIVE_METHODS@': '\n'.join(block(animation, 'PyObject * PySwingingAnimator::py_' + name + '(')
                                          for name in ('activate', 'deactivate'))}
    for token, value in values.items():
        assert output.count(token) == 1, token
        output = output.replace(token, value)
    return output


class CallbackMetadataTest(unittest.TestCase):
    def test_patch_is_atomic_idempotent_and_confined_to_profiler(self):
        original = sources(self)['lib/pyscript/script.cpp']
        patched = patcher.patch_source(original)
        self.assertEqual(patcher.patch_source(patched), patched)
        resolver = block(original, 'void resolvePythonModuleAndFunctionNames(')
        replacement = block(patched, 'void resolvePythonModuleAndFunctionNames(')
        self.assertEqual(original.replace(resolver, replacement, 1), patched)
        self.assertEqual(block(original, 'PyObject * Script::ask('), block(patched, 'PyObject * Script::ask('))
        for text in (original.replace(patcher.EDITS[0][0], '', 1),
                     original + patcher.EDITS[1][0], patched.replace('PyObjectPtr moduleNameOwner;', '', 1)):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / patcher.SOURCE
                path.parent.mkdir(parents=True);path.write_text(text)
                with self.assertRaises(ValueError):patcher.patch(directory)
                self.assertEqual(path.read_text(), text)

    def test_real_cpython_native_callback_and_error_propagation(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            if 'CXX' in os.environ:self.fail('Configured C++ compiler is unavailable: ' + compiler)
            self.skipTest('C++ compiler required')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not python.is_file() or not config.is_file():
            if 'WOT_PYTHON27' in os.environ:self.fail('Configured CPython 2.7 development files are missing')
            self.skipTest('CPython 2.7 development files required')
        self.assertIn('Python 2.7.18', subprocess.check_output([str(python), '--version'], stderr=subprocess.STDOUT, text=True))
        source = sources(self)
        flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
        flags = [flag for flag in flags if not flag.startswith('-O') and flag != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='script-metadata-') as directory:
            path = Path(directory)
            for negative in (False, True):
                script = source['lib/pyscript/script.cpp']
                if not negative:script = patcher.patch_source(script)
                cpp = path / 'host.cpp';cpp.write_text(generated(source, script))
                binary = path / ('negative' if negative else 'host')
                subprocess.run([compiler, '-std=c++14', '-O0', '-Wall', '-Wextra', '-Werror',
                                '-Wno-missing-field-initializers', '-Wno-deprecated-register',
                                '-Wno-writable-strings', *flags, str(cpp), *libs, '-o', str(binary)], check=True)
                env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
                if negative:env['WOT_CALLBACK_METADATA_NEGATIVE'] = '1'
                result = subprocess.run([str(binary), str(HERE / 'stock_script_callback_metadata_consumer.py')],
                                        text=True, capture_output=True, env=env)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                if negative:self.assertEqual(result.stderr.count('TypeError: expected string or Unicode object, NoneType found'), 3)
                else:self.assertEqual(result.stderr, '')

    def test_actual_windows_hybrid_enables_the_affected_profiler(self):
        source = sources(self)
        # The independent runner builds Hybrid. Only Consumer_Release defines
        # CONSUMER_CLIENT; cstdmf/config.hpp therefore enables this code on Win32.
        runner = (ROOT.parent.parent / 'tools/build_bigworld_client.ps1').read_text()
        self.assertIn('"--config", "Hybrid"', runner)
        self.assertIn('COMPILE_DEFINITIONS_CONSUMER_RELEASE\n\tCONSUMER_CLIENT', source[FILES[3]])
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            if 'CXX' in os.environ:self.fail('Configured C++ compiler is unavailable: ' + compiler)
            self.skipTest('C++ compiler required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'probe.cpp'
            path.write_text(source[FILES[2]] + '\n#if ENABLE_PROFILER\nprofiler_enabled\n#else\nprofiler_disabled\n#endif\n')
            for consumer in (False, True):
                result = subprocess.check_output([compiler, '-E', '-P', '-U__APPLE__', '-D_WIN32',
                                                  *(['-DCONSUMER_CLIENT'] if consumer else []), str(path)],
                                                 text=True, stderr=subprocess.DEVNULL)
                self.assertIn('profiler_disabled' if consumer else 'profiler_enabled', result)

    def test_exact_pyc_producer_and_original_native_callback_boundary(self):
        value = os.environ.get('WOT_0922_CLIENT')
        if not value:self.skipTest('Exact #1513 client required')
        client = Path(value)
        for name in ('WorldOfTanks.exe', 'res/packages/scripts.pkg'):
            self.assertTrue((client / name).is_file(), 'Configured #1513 file missing: ' + name)
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        if not python.is_file():
            if 'WOT_PYTHON27' in os.environ:self.fail('Configured CPython 2.7 executable missing')
            self.skipTest('CPython 2.7 required')
        driver = r'''
import marshal,sys,types,zipfile
z=zipfile.ZipFile(sys.argv[1])
def code(member,name):
 def find(c):
  if c.co_name==name:return c
  for item in c.co_consts:
   if isinstance(item,types.CodeType):
    result=find(item)
    if result is not None:return result
 return find(marshal.loads(z.read(member)[8:]))
factory=code('scripts/client/vehicle_systems/model_assembler.pyc','createSwingingAnimator')
assert factory.co_names[:2]==('Vehicular','SwingingAnimator')
assembly=code('scripts/client/vehicle_systems/vehicle_assembler.pyc','_assembleSwinging')
assert 'createSwingingAnimator' in assembly.co_names and 'swingingAnimator' in assembly.co_names
descriptor=code('scripts/client_common/svarog_script/py_component_system.pyc','__set__')
assert 'addComponent' in descriptor.co_names
print('Exact #1513 native SwingingAnimator component producer confirmed')
'''
        subprocess.run([str(python), '-c', driver, str(client / 'res/packages/scripts.pkg')], check=True,
                       env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        source = (ROOT / 'bigworld/stock_battle_vehicle_animation.cpp').read_text()
        self.assertIn('class PySwingingAnimator : public MatrixProvider', source)
        components = (ROOT / 'bigworld/stock_vehicle_components.cpp').read_text()
        self.assertIn('if (StockComponentWrapper::Check( object ))', components)
        self.assertIn('value = std::make_shared<ScriptComponentState>( object );', components)
        try:
            import pefile
            import capstone
        except ImportError as error:self.skipTest('pefile/capstone required: ' + str(error))
        pe = pefile.PE(str(client / 'WorldOfTanks.exe'))
        self.assertEqual(pe.FILE_HEADER.Machine, 0x14c)
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        calls = {i.address: i.op_str for i in decoder.disasm(
            pe.get_data(0x13ed520 - pe.OPTIONAL_HEADER.ImageBase, 0x168), 0x13ed520) if i.mnemonic=='call'}
        # Original Svarog resolves a named callable and calls it directly;
        # optional profiler metadata is absent from that native boundary.
        self.assertEqual(calls[0x13ed559], '0xfb3bf0')  # PyObject_GetAttrString.
        self.assertEqual(calls[0x13ed58b], '0xfb38d0')  # PyCallable_Check.
        self.assertEqual(calls[0x13ed62c], '0xfca730')  # PyObject_Call.


if __name__ == '__main__':
    unittest.main()
