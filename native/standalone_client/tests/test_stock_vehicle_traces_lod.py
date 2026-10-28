"""Trace LOD binding regression with the exact #1513 PYC producer.

The host compiles the real trace wrapper, shared parser, SDK scalar conversion
and trace activation/LOD gate. It does not execute Windows projection/rendering.
"""
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

OLD_WRAPPER = '''PyObject* PyVehicleTraces::py_setLodSettings(PyObject* args)
{
    float distance;unsigned priority;
    if (!PyArg_ParseTuple(args,"(fI):setLodSettings",&distance,&priority)) return NULL;
    auto value=state();if (!value) return NULL;
    value->maxDistance=distance;value->maxPriority=priority;Py_RETURN_NONE;
}'''


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value:
        test.skipTest('Exact #1513 client required')
    path = Path(value)
    for name in ('WorldOfTanks.exe', 'res/packages/scripts.pkg'):
        test.assertTrue((path / name).is_file(), 'Configured #1513 file missing: ' + name)
    return path


def host(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
            test.fail('Configured public SDK archive missing: ' + str(archive))
        test.skipTest('Pinned public SDK archive required')
    with tarfile.open(archive) as tar:
        member = next((m for m in tar if m.name.endswith('/programming/bigworld/lib/pyscript/script.cpp')), None)
        test.assertIsNotNone(member, 'Pinned SDK Script source missing')
        script = tar.extractfile(member).read().decode('utf-8-sig')
    scalar = '\n'.join(block(script, 'int Script::setData( PyObject * pObject, ' + kind)
                       for kind in ('uint &', 'double &', 'float &'))
    source = (ROOT / 'bigworld/stock_vehicle_traces.cpp').read_text()
    parser = block((ROOT / 'bigworld/stock_vehicle_wheels.cpp').read_text(), 'bool readStockLodSettings(')
    begin = source.index('struct TraceState : StockComponentState\n')
    end = source.index('    virtual PyObject* wrap(bool owning);', begin)
    update = source.index('    void update()\n', end)
    gate_end = source.index('        PyObjectPtr model(', update)
    state = source[begin:end] + source[end:update] + source[update:gate_end] + '        ++submitted;\n    }\n};\n'
    wrapper = block(source, 'PyObject* PyVehicleTraces::py_setLodSettings(')
    template = (HERE / 'stock_vehicle_traces_lod_host.cpp').read_text()
    for token, value in (('@CONVERSIONS@', scalar), ('@LOD_PARSER@', parser),
                         ('@STATE@', state), ('@SETTINGS_WRAPPER@', wrapper)):
        test.assertEqual(template.count(token), 1)
        template = template.replace(token, value)
    return template, wrapper


class TraceLodTest(unittest.TestCase):
    def test_real_trace_binding_and_original_assembler(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            if 'CXX' in os.environ:
                self.fail('Configured C++ compiler is unavailable: ' + compiler)
            self.skipTest('C++ compiler required')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if 'WOT_PYTHON27' in os.environ and (not python.is_file() or not config.is_file()):
            self.fail('Configured CPython 2.7 development files are missing')
        original = client(self)
        if not python.is_file() or not config.is_file():
            self.skipTest('CPython 2.7 development files required')
        self.assertIn('Python 2.7.18', subprocess.check_output([str(python), '--version'],
                                                              stderr=subprocess.STDOUT, text=True))
        template, wrapper = host(self)
        flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
        flags = [f for f in flags if not f.startswith('-O') and f != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='trace-lod-') as directory:
            path = Path(directory)
            for negative in (False, True):
                cpp = path / 'host.cpp'
                cpp.write_text(template.replace(wrapper, OLD_WRAPPER) if negative else template)
                binary = path / ('negative' if negative else 'host')
                subprocess.run([compiler, '-std=c++14', '-O0', '-Wall', '-Wextra', '-Werror',
                                '-Wno-missing-field-initializers', '-Wno-deprecated-register',
                                '-Wno-writable-strings', *flags, str(cpp), *libs, '-o', str(binary)], check=True)
                env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
                if negative:
                    env['WOT_TRACE_LOD_NEGATIVE'] = '1'
                result = subprocess.run([str(binary), str(HERE / 'stock_vehicle_traces_lod_consumer.py'),
                                         str(original)], env=env, text=True, capture_output=True)
                if negative:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('model_assembler.py", line 280', result.stderr)
                    self.assertIn('TypeError: an integer is required', result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_pe_uses_shared_parser_and_commits_after_success(self):
        original = client(self)
        try:
            import pefile
            import capstone
        except ImportError as error:
            self.skipTest('pefile/capstone required: ' + str(error))
        pe = pefile.PE(str(original / 'WorldOfTanks.exe'))
        self.assertEqual(pe.FILE_HEADER.Machine, 0x14c)
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        def instructions(address, size):
            return [(i.address, i.mnemonic, i.op_str) for i in decoder.disasm(
                pe.get_data(address - pe.OPTIONAL_HEADER.ImageBase, size), address)]
        wrapper = instructions(0x77ebc0, 0xaf)
        self.assertIn((0x77ec18, 'call', '0x77efe0'), wrapper)
        self.assertIn((0x77ec22, 'je', '0x77ec2c'), wrapper)
        self.assertIn((0x77ec3d, 'mov', 'dword ptr [eax], esi'), wrapper)
        self.assertIn((0x77ec3f, 'mov', 'dword ptr [eax + 4], edi'), wrapper)
        parser = instructions(0x77efe0, 0x167)
        self.assertIn((0x77f0c5, 'cmp', 'esi, 0x1a5c798'), parser)  # None priority.
        self.assertIn((0x77f0cb, 'je', '0x77f0f3'), parser)         # Keep UINT_MAX.
        self.assertIn((0x77f043, 'jne', '0x77f060'), parser)
        self.assertIn((0x77f060, 'mov', 'esi, dword ptr [edi + 0xc]'), parser)
        # Empty settings still read item zero after the default write. The
        # adapter rejects that invalid tuple rather than emulating an OOB read.
        self.assertEqual(pe.get_data(0x1a12e38 - pe.OPTIONAL_HEADER.ImageBase, 8), b'\xff\xff\xff\xff\xff\xff\x7f\x7f')


if __name__ == '__main__':
    unittest.main()
