"""Exercise the production configuration Reader with the pinned CPython 2.7 API.

These are descriptor-boundary/ownership tests. They do not create a Tank, expose
configure success, or establish the unimplemented chassis/shape consumer graph.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def probe_source():
    source = (ROOT / 'bigworld/vehicle_physics_binding.cpp').read_text()
    begin = source.index('namespace\n{\n// The original Reader')
    end = source.index('// This wrapper owns precisely one authoritative core.', begin)
    reader = source[begin:end]
    header = (ROOT / 'include/wot/vehicle_physics.hpp').read_text()
    kinds = ['VehicleBodyConfiguration', 'VehicleShapeConfiguration', 'VehicleGroundSurfaceConfiguration',
             'VehicleGroundConfiguration', 'VehicleChassisConfiguration', 'VehicleEngineConfiguration',
             'VehiclePhysicsModeConfiguration', 'VehicleFakeGearConfiguration', 'VehicleGimletConfiguration',
             'VehiclePhysicsConfiguration']
    declarations = '\n'.join('PyObject*pack(const wot::%s&);' % name for name in kinds)
    definitions = []
    for name in kinds:
        body = re.search(r'struct ' + name + r'\s*\{(.*?)\n\};', header, re.S).group(1)
        fields = re.findall(r'^\s*(?:float|bool|std::int32_t|std::string|Vector3|Vehicle\w+|std::array<[^>]+>|std::vector<[^>]+>)\s+(\w+)\s*;', body, re.M)
        assert fields, name
        definitions.append('PyObject*pack(const wot::%s&value){PyObjectPtr result(PyDict_New(),PyObjectPtr::STEAL_REFERENCE);' % name +
                           ''.join('if(!put(result.get(),"%s",pack(value.%s)))return NULL;' % (field, field) for field in fields) +
                           'Py_INCREF(result.get());return result.get();}')
    return r'''
#include <Python.h>
#include "wot/vehicle_physics.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <stdexcept>
#include <new>
#include <cstdio>
class PyObjectPtr {
 PyObject*value_=NULL;
public:
 enum Mode{STEAL_REFERENCE};
 PyObjectPtr()=default;
 explicit PyObjectPtr(PyObject*v):value_(v){Py_XINCREF(value_);}
 PyObjectPtr(PyObject*v,Mode):value_(v){}
 PyObjectPtr(const PyObjectPtr&o):value_(o.value_){Py_XINCREF(value_);}
 PyObjectPtr(PyObjectPtr&&o):value_(o.value_){o.value_=NULL;}
 PyObjectPtr&operator=(PyObjectPtr o){std::swap(value_,o.value_);return *this;}
 ~PyObjectPtr(){Py_XDECREF(value_);}
 PyObject*get()const{return value_;}explicit operator bool()const{return value_!=NULL;}
};
''' + reader + declarations + r'''
PyObject*pack(float value){return PyFloat_FromDouble(value);}
PyObject*pack(bool value){return PyBool_FromLong(value);}
PyObject*pack(std::int32_t value){return PyInt_FromLong(value);}
PyObject*pack(const std::string&value){return PyString_FromStringAndSize(value.data(),value.size());}
PyObject*pack(const wot::Vector3&value){return Py_BuildValue("(fff)",value.x,value.y,value.z);}
template<class T,std::size_t N>PyObject*pack(const std::array<T,N>&value){
 PyObject*result=PyList_New(N);for(std::size_t i=0;i<N;++i)PyList_SET_ITEM(result,i,pack(value[i]));return result;
}
template<class T>PyObject*pack(const std::vector<T>&value){
 PyObject*result=PyList_New(value.size());for(std::size_t i=0;i<value.size();++i)PyList_SET_ITEM(result,i,pack(value[i]));return result;
}
bool put(PyObject*object,const char*key,PyObject*value){
 PyObjectPtr pin(value,PyObjectPtr::STEAL_REFERENCE);return value&&PyDict_SetItemString(object,key,value)==0;
}
''' + '\n'.join(definitions) + r'''
wot::VehiclePhysicsConfiguration stored;
PyObject*parse(PyObject*,PyObject*arguments){
 PyObject*value;if(!PyArg_ParseTuple(arguments,"O",&value))return NULL;
 if(!readStockVehiclePhysicsConfiguration(value,stored))return NULL;
 return pack(stored);
}
PyObject*current(PyObject*,PyObject*){return pack(stored);}
PyMethodDef methods[]={{"parse",parse,METH_VARARGS,NULL},{"current",current,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
int main(int argc,char**argv){
 if(argc!=2)return 3;Py_Initialize();Py_InitModule3("stock_physics_probe",methods,NULL);
 FILE*file=std::fopen(argv[1],"rb");if(!file)return 4;
 const int result=PyRun_SimpleFileEx(file,argv[1],1);Py_Finalize();return result==0?0:5;
}
'''


class VehiclePhysicsConfigurationTests(unittest.TestCase):
    def test_actual_python27_reader_contracts_and_reentrant_source_ownership(self):
        python = os.environ.get('WOT_PYTHON27')
        if not python:
            self.skipTest('Set WOT_PYTHON27 to pinned CPython 2.7.18')
        prefix = Path(subprocess.check_output([python, '-c', 'import sys;print(sys.prefix)'], text=True).strip())
        version = subprocess.check_output([python, '-c', 'import sys;print(sys.version_info[:2])'], text=True).strip()
        self.assertEqual(version, '(2, 7)')
        with tempfile.TemporaryDirectory(prefix='physics-config-reader-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(probe_source())
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-register',
                       '-ffp-contract=off', '-fno-fast-math', '-fsanitize=address,undefined',
                       '-I' + str(prefix / 'include/python2.7'), '-I' + str(ROOT / 'include'),
                       '-L' + str(prefix / 'lib'), '-lpython2.7', '-Wl,-rpath,' + str(prefix / 'lib'),
                       str(root / 'probe.cpp'), '-o', str(root / 'probe')]
            subprocess.run(command, check=True)
            environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0')
            subprocess.run([str(root / 'probe'), str(ROOT / 'tests/vehicle_physics_configuration_fixture.py')],
                           env=environment, check=True)


if __name__ == '__main__':
    unittest.main()
