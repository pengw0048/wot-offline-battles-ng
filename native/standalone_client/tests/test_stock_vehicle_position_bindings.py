"""Actual CPython bindings/PYC callers and exact x86 position/node contracts.

Recorded matrices/camera/node storage do not establish Windows rendering.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / 'tests'
sys.path.insert(0, str(TESTS))


class PositionBindingsTest(unittest.TestCase):
    def test_cpython_original_callers_and_live_lod(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        self.assertTrue(config.exists(), 'Pinned CPython 2.7 development files required')
        client = os.environ.get('WOT_0922_CLIENT')
        self.assertTrue(client, 'Exact #1513 client required')
        source = (ROOT / 'bigworld/stock_vehicle_visuals.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='vehicle-position-binding-') as directory:
            path = Path(directory)
            for name in ('pch.hpp', 'pyscript/script.hpp'):
                (path / name).parent.mkdir(parents=True, exist_ok=True)
                (path / name).write_text('#pragma once\n')
            header = (TESTS / 'stock_battle_vehicle_animation_host.hpp').read_text()
            header = header.replace('};\nstruct Matrix {', '''
    Vector3 operator-(const Vector3& b) const {return Vector3(x-b.x,y-b.y,z-b.z);}
    float length() const {return std::sqrt(x*x+y*y+z*z);}
};
struct Matrix {''', 1)
            header = header.replace('static const Matrix identity;', '''static const Matrix identity;
    bool invert(const Matrix&) {return true;}
    Vector3 applyToOrigin() const {return Vector3();}
    void applyPoint(Vector4& out,Vector3 p) const {out={p.x,p.y,p.z,1.f};}''')
            header = header.replace('struct Matrix {', 'struct Vector4 {float x,y,z,w;};\nstruct Matrix {', 1)
            start = header.index('inline int setData(PyObject * o, Vector3 & value')
            end = header.index('inline int setData(PyObject * o, MatrixProviderPtr', start)
            header = header[:start] + '''inline int setData(PyObject* o,Vector3& value,const char* name="") {
    if(PyArg_ParseTuple(o,"fff",&value.x,&value.y,&value.z))return 0;
    PyErr_Format(PyExc_TypeError,"%s must be set to a Vector3 or a tuple of 3 floats",name);return -1;
}
''' + header[end:]
            (path / 'stock_battle_vehicle_animation_host.hpp').write_text(header)
            begin = source.index('namespace\n{\nbool sampleStockLodPositionLink')
            end = source.index('\nclass Suspension;', begin)
            (path / 'stock_vehicle_lod_owner.inc').write_text(source[begin:end])
            cls = source[source.index('class Suspension :'):source.index('\nPyObject * SuspensionState::wrap')]
            cls = re.sub(r'    PY_METHOD_DECLARE\(.*?\)\n', '', cls)
            cls = cls.replace('    PY_FACTORY_DECLARE()\n', '')
            table = source[source.index('PY_TYPEOBJECT( Suspension )'):source.index('PY_FACTORY( Suspension, Vehicular )')]
            table = re.sub(r'    PY_METHOD\(.*?\)\n', '', table)
            (path / 'stock_suspension_property_owner.inc').write_text(cls + table)
            (path / 'host.cpp').write_text((TESTS / 'stock_vehicle_position_bindings_host.cpp').read_text())
            flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True)) if f != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++17', '-Wno-register', '-Wno-deprecated-register', '-Wno-writable-strings',
                            '-UNDEBUG', '-I', str(path), '-I', str(ROOT / 'bigworld'),
                            str(path / 'host.cpp'), *flags, *libs, '-o', str(path / 'host')], check=True)
            environment = dict(os.environ)
            with zipfile.ZipFile(Path(client) / 'res/packages/scripts.pkg') as archive:
                for module in ('vehicle_assembler', 'CompoundAppearance'):
                    target = path / (module + '.pyc')
                    target.write_bytes(archive.read('scripts/client/vehicle_systems/' + module + '.pyc'))
                    environment['STOCK_' + module.upper()] = str(target)
            subprocess.run([str(path / 'host'), str(TESTS / 'stock_vehicle_position_bindings_consumer.py')],
                           env=environment, check=True)

    def test_exact_x86_owner_dispatch_and_ground_node_vector(self):
        from stock_vehicle_position_bindings_oracle import check
        client = os.environ.get('WOT_0922_CLIENT')
        self.assertTrue(client, 'Exact #1513 client required')
        check(Path(client) / 'WorldOfTanks.exe')


if __name__ == '__main__':
    unittest.main()
