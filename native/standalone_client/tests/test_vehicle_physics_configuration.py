"""Exercise the production Reader and configure transaction with CPython 2.7.

The native core runs with declared host math stand-ins; Win32 D3DX/UCRT
results and the BigWorld attribute shell are covered by the native probe.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PRODUCER_VEHICLES = ('scripts/item_defs/vehicles/ussr/R11_MS-1.xml',
                     'scripts/item_defs/vehicles/sweden/S11_Strv_103B.xml',
                     'scripts/item_defs/vehicles/germany/G42_Maus.xml')


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
#include <cmath>
#include <memory>
#include <vector>
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
// Declared host stand-ins for the D3DX9_43/UCRT services.
float host_tangent(float value){return std::tan(value);}
wot::Vector3 host_normalize(wot::Vector3 v){
 const float length=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);return {v.x/length,v.y/length,v.z/length};
}
wot::Vector3 host_coordinate(wot::Vector3 v,const wot::VehicleMatrix&m){
 const auto&r=m.rows;
 return {v.x*r[0][0]+v.y*r[1][0]+v.z*r[2][0]+r[3][0],v.x*r[0][1]+v.y*r[1][1]+v.z*r[2][1]+r[3][1],
         v.x*r[0][2]+v.y*r[1][2]+v.z*r[2][2]+r[3][2]};
}
wot::Vector3 host_normal(wot::Vector3 v,const wot::VehicleMatrix&m){
 const auto&r=m.rows;
 return {v.x*r[0][0]+v.y*r[1][0]+v.z*r[2][0],v.x*r[0][1]+v.y*r[1][1]+v.z*r[2][1],v.x*r[0][2]+v.y*r[1][2]+v.z*r[2][2]};
}
void host_inverse(wot::VehicleMatrix&out,const wot::VehicleMatrix&in){
 // Diagonal inputs only in these fixtures; a singular one leaves out unchanged.
 for(int i=0;i!=4;++i)for(int j=0;j!=4;++j)if(i!=j&&in.rows[i][j]!=0)return;
 for(int i=0;i!=4;++i)if(in.rows[i][i]==0)return;
 out=wot::VehicleMatrix{};for(int i=0;i!=4;++i)out.rows[i][i]=1.0f/in.rows[i][i];
}
const wot::VehicleNativeMath math{&host_tangent,&host_normalize,&host_coordinate,&host_normal,&host_inverse};
std::vector<std::unique_ptr<wot::VehiclePhysics>> cores;
wot::VehiclePhysics*core(PyObject*arguments,PyObject**value=NULL){
 int handle;PyObject*ignored;
 if(!PyArg_ParseTuple(arguments,value?"iO":"i",&handle,value?value:&ignored))return NULL;
 if(handle<0||size_t(handle)>=cores.size()||!cores[size_t(handle)]){PyErr_SetString(PyExc_KeyError,"no core");return NULL;}
 return cores[size_t(handle)].get();
}
PyObject*create(PyObject*,PyObject*){cores.emplace_back(new wot::VehiclePhysics());return PyInt_FromLong(long(cores.size()-1));}
PyObject*destroy(PyObject*,PyObject*arguments){
 wot::VehiclePhysics*target=core(arguments);if(!target)return NULL;
 for(auto&item:cores)if(item.get()==target)item.reset();
 Py_RETURN_NONE;
}
PyObject*configure(PyObject*,PyObject*arguments){
 PyObject*value;wot::VehiclePhysics*target=core(arguments,&value);if(!target)return NULL;
 if(!configureStockVehiclePhysics(*target,value,math))return NULL;
 Py_RETURN_TRUE;
}
PyObject*summary(PyObject*,PyObject*arguments){
 wot::VehiclePhysics*target=core(arguments);if(!target)return NULL;
 const wot::VehicleTank*tank=target->tank();
 PyObjectPtr result(PyDict_New(),PyObjectPtr::STEAL_REFERENCE);
 put(result.get(),"epoch",PyInt_FromLong(long(target->configuration_epoch())));
 put(result.get(),"frozen",PyBool_FromLong(target->frozen()));
 if(tank){
  put(result.get(),"slots",PyInt_FromLong(long(tank->settings.size())));
  put(result.get(),"mass",PyFloat_FromDouble(tank->hull.mass));
  put(result.get(),"inverse_mass",PyFloat_FromDouble(target->rigid_body().inverse_mass));
  put(result.get(),"full_mass",PyFloat_FromDouble(tank->current_settings().body.full_mass));
  put(result.get(),"complex",PyBool_FromLong(tank->current_settings().shape.use_complex_form));
  put(result.get(),"wheels",PyInt_FromLong(long(tank->chassis.wheels.size())));
  put(result.get(),"components",PyInt_FromLong(long(tank->components.size())));
  put(result.get(),"boxes",PyInt_FromLong(long(tank->collections[0].boxes.size())));
  put(result.get(),"polyhedra",Py_BuildValue("(ii)",int(tank->collections[0].polyhedra.size()),
      int(tank->collections[1].polyhedra.size())));
  put(result.get(),"vertical_force",PyFloat_FromDouble(tank->chassis.vertical_force));
  put(result.get(),"gears",PyInt_FromLong(long(tank->fake_gearbox.curves.at(0).size())));
  put(result.get(),"grounds",PyInt_FromLong(long(tank->current_chassis().grounds.size())));
  put(result.get(),"hull_com_z",PyFloat_FromDouble(target->hull_com_z()));
 }
 Py_INCREF(result.get());return result.get();
}
PyObject*set_frozen(PyObject*,PyObject*arguments){
 PyObject*value;wot::VehiclePhysics*target=core(arguments,&value);if(!target)return NULL;
 try{target->set_frozen(PyObject_IsTrue(value)==1);}
 catch(const std::logic_error&error){PyErr_SetString(PyExc_ValueError,error.what());return NULL;}
 Py_RETURN_NONE;
}
PyObject*hull_com_z(PyObject*,PyObject*arguments){
 wot::VehiclePhysics*target=core(arguments);if(!target)return NULL;
 if(!target->tank()){PyErr_SetString(PyExc_ValueError,"hullCOMZ requires a configured WGVehiclePhysics");return NULL;}
 return PyFloat_FromDouble(target->hull_com_z());
}
PyMethodDef methods[]={{"parse",parse,METH_VARARGS,NULL},{"current",current,METH_NOARGS,NULL},
 {"create",create,METH_NOARGS,NULL},{"destroy",destroy,METH_VARARGS,NULL},
 {"configure",configure,METH_VARARGS,NULL},{"summary",summary,METH_VARARGS,NULL},
 {"set_frozen",set_frozen,METH_VARARGS,NULL},{"hull_com_z",hull_com_z,METH_VARARGS,NULL},
 {NULL,NULL,0,NULL}};
int main(int argc,char**argv){
 if(argc<2)return 3;Py_Initialize();PySys_SetArgvEx(argc-1,argv+1,0);
 Py_InitModule3("stock_physics_probe",methods,NULL);
 FILE*file=std::fopen(argv[1],"rb");if(!file)return 4;
 const int result=PyRun_SimpleFileEx(file,argv[1],1);Py_Finalize();return result==0?0:5;
}
'''


class VehiclePhysicsConfigurationTests(unittest.TestCase):
    def run_fixture(self, script, *arguments):
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
            command += [str(ROOT / 'src' / name) for name in
                        ['vehicle_physics.cpp', 'vehicle_dynamics.cpp', 'point_velocity.cpp']]
            subprocess.run(command, check=True)
            environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0',
                               PYTHONPATH=str(ROOT / 'tests'))
            subprocess.run([str(root / 'probe'), str(ROOT / 'tests' / script)] + list(arguments),
                           env=environment, check=True)

    def test_actual_python27_reader_contracts_and_reentrant_source_ownership(self):
        self.run_fixture('vehicle_physics_configuration_fixture.py')

    def test_configure_transactions_and_pinned_producer(self):
        client = os.environ.get('WOT_0922_CLIENT')
        with tempfile.TemporaryDirectory(prefix='physics-producer-') as temporary:
            arguments = []
            if client:
                export = Path(temporary) / 'physics.json'
                subprocess.run([sys.executable, str(ROOT / 'tests/vehicle_physics_export.py'), client,
                                str(export)] + list(PRODUCER_VEHICLES), check=True)
                arguments = [client, str(export)]
            self.run_fixture('vehicle_physics_configure_fixture.py', *arguments)


if __name__ == '__main__':
    unittest.main()
