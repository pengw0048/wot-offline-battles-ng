"""Execute the startup recorder's actual stock-entity field reads on Python 2."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_stock_native_bot_fitting_bridge import cpp_function

ROOT = Path(__file__).resolve().parents[1]


class LocalRecordingContractTests(unittest.TestCase):
    def test_stock_packed_gun_field_is_recorded_without_reading_the_old_alias(self):
        if sys.platform == 'win32' or not shutil.which('c++'):
            self.skipTest('the Python 2 extension host requires a Unix C++ compiler')
        py27 = os.environ.get('WOT_PYTHON27')
        if not py27 and shutil.which('pyenv'):
            found = subprocess.run(['pyenv', 'which', 'python2.7'], capture_output=True,
                                   text=True, env=dict(os.environ, PYENV_VERSION='2.7.18'))
            if found.returncode == 0:
                py27 = found.stdout.strip()
        if not py27:
            self.skipTest('set WOT_PYTHON27 to the Python 2 audit interpreter')
        bigworld = ROOT / 'bigworld'
        source = (bigworld / 'stock_native_battle_startup_session.cpp').read_text()
        start = source.index('auto read_int =', source.index('void recordLocal('))
        end = source.index(';', source.index('p::set(row.get(), "gun_angles"', start)) + 1
        reads = source[start:end]
        optional = cpp_function((bigworld / 'stock_native_battle.hpp').read_text(),
                                'inline p::Ref optional(')
        integer = cpp_function((bigworld / 'stock_native_battle_startup_internal.hpp').read_text(),
                               'inline std::int64_t asInt(')
        host = '''#include "stock_native_bot_python.hpp"
namespace p=stock_native_bot_python;
namespace py {''' + optional + '}\n' + integer + '''
static PyObject* record(PyObject*,PyObject* argument){try{
auto entity=p::retain(argument);auto row=p::dict();
''' + reads + '''
return row.release();}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
static PyMethodDef methods[]={{"record",record,METH_O,nullptr},{nullptr,nullptr,0,nullptr}};
extern "C" void initlocal_recording_contract(){Py_InitModule("local_recording_contract",methods);}
'''
        fixture = '''import local_recording_contract as native
class Entity(object):
    health=880
    @property
    def gunAngles(self):
        raise AssertionError('the obsolete gunAngles alias was read')
for packed in (0,12,2147483648L,4294967295L):
    entity=Entity();entity.gunAnglesPacked=packed
    row=native.record(entity)
    assert row == dict(health=880,gun_angles=packed), row
print('startup recorder canonical gun field passed')
'''
        prefix = subprocess.check_output([py27, '-c', 'import sys;print(sys.prefix)'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='local-recording-contract-') as directory:
            folder = Path(directory)
            (folder / 'host.cpp').write_text(host)
            (folder / 'fixture.py').write_text(fixture)
            link = ['-bundle', '-undefined', 'dynamic_lookup'] if sys.platform == 'darwin' else ['-shared', '-fPIC']
            command = ['c++', '-std=c++17', '-Wno-register'] + link + [
                '-isystem', str(Path(prefix) / 'include/python2.7'), '-I' + str(bigworld),
                str(folder / 'host.cpp'), '-o', str(folder / 'local_recording_contract.so')]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([py27, '-B', str(folder / 'fixture.py')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
