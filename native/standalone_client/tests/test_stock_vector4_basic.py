"""Check the #1513 factory against SDK providers and the actual Vehicle PYC.

The host uses real CPython 2.7 and the SDK Vector4Basic class/constructor/output.
Its small PyObject base and tuple value accessor replace SDK binding machinery;
Windows still owns validation of actual engine provider consumers and teardown.
"""
from pathlib import Path
import os
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


HOST = r'''
#include <Python.h>
#include <cassert>
#include <cstring>
struct Vector4 {
 float x,y,z,w;
 Vector4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}
};
struct Vector4Provider:PyObject {
 explicit Vector4Provider(bool tick,PyTypeObject* type):autoTick(tick){PyObject_Init(this,type);}
 virtual ~Vector4Provider(){}
 virtual void output(Vector4&)=0;
 bool autoTick;
};
#define Py_Header(...) public: inline static PyTypeObject s_type_={PyObject_HEAD_INIT(NULL) 0};
#define PY_RW_ATTRIBUTE_DECLARE(...)
@CLASS@;
@CONSTRUCTOR@
@FACTORY@
#define MF_ASSERT assert
#define bw_snprintf snprintf
namespace Script {
struct InitTimeJob {explicit InitTimeJob(int){};virtual ~InitTimeJob(){};virtual void init()=0;};
struct FiniTimeJob {virtual ~FiniTimeJob(){};virtual void fini()=0;};
}
@FACTORY_LINK_CLASS@;
@FACTORY_LINK_METHODS@
@LINK@
PyObject* vectorValue(Vector4 value){return Py_BuildValue("(ffff)",value.x,value.y,value.z,value.w);}
PyObject* getValue(PyObject* object,void*){
 return vectorValue(*static_cast<Vector4Basic*>(object)->pValue());
}
int setValue(PyObject* object,PyObject* value,void*){
 if(!value){PyErr_SetString(PyExc_TypeError,"value cannot be deleted");return -1;}
 Vector4 vector;
 if(!PyArg_ParseTuple(value,"ffff",&vector.x,&vector.y,&vector.z,&vector.w))return -1;
 static_cast<Vector4Basic*>(object)->set(vector);return 0;
}
void destroy(PyObject* object){delete static_cast<Vector4Basic*>(object);}
PyObject* sample(PyObject*,PyObject* object){
 if(!PyObject_TypeCheck(object,&Vector4Basic::s_type_)){
  PyErr_SetString(PyExc_TypeError,"Expected Vector4Basic");return NULL;
 }
 Vector4 output;static_cast<Vector4Provider*>(static_cast<Vector4Basic*>(object))->output(output);
 return vectorValue(output);
}
PyMethodDef methods[]={{"sample",sample,METH_O,NULL},{NULL,NULL,0,NULL}};
PyGetSetDef attributes[]={{const_cast<char*>("value"),getValue,setValue,NULL,NULL},{NULL,NULL,NULL,NULL,NULL}};
int main(){
 Py_Initialize();
 auto& type=Vector4Basic::s_type_;type.tp_name="Vector4Basic";type.tp_basicsize=sizeof(Vector4Basic);
 type.tp_flags=Py_TPFLAGS_DEFAULT;type.tp_dealloc=destroy;type.tp_getset=attributes;
 assert(Py_InitModule("Math",methods));StockVector4BasicFactory link;link.init();
 auto args=PyTuple_New(0);auto object=stockVector4BasicNew(&type,args,NULL);Py_DECREF(args);
 auto native=static_cast<Vector4Basic*>(object);Vector4 output;
 assert(Py_REFCNT(object)==1&&!native->autoTick);
 native->output(output);assert(output.x==0&&output.y==0&&output.z==0&&output.w==0);
 // Native consumers must sample a later value, without retaining a snapshot.
 native->set(Vector4(3,-2,9,4));Vector4Provider* provider=native;provider->output(output);
 assert(output.x==3&&output.y==-2&&output.z==9&&output.w==4);
 native->set(Vector4(8,7,6,5));provider->output(output);assert(output.x==8&&output.w==5);
 Py_DECREF(object);
 // The original ignores the requested type as well as args and keywords.
 object=stockVector4BasicNew(&PyBaseObject_Type,NULL,NULL);
 assert(Py_TYPE(object)==&Vector4Basic::s_type_);Py_DECREF(object);
 const char* program=R"PY(
import Math, os, marshal, types, weakref, zipfile
zero=(0.,0.,0.,0.)
a=Math.Vector4Basic()
assert isinstance(Math.Vector4Basic,type) and type(a) is Math.Vector4Basic
assert a.value==zero and Math.sample(a)==zero
for args in [(), (1,), ((1,2,3,4),), (None, 'ignored', object())]:
 b=Math.Vector4Basic(*args)
 assert b is not a and b.value==zero and Math.sample(b)==zero
 b.value=(10.,-20.,30.,40.)
 assert Math.sample(b)==(10.,-20.,30.,40.) and a.value==zero
assert Math.Vector4Basic(value=(1,2,3,4), ignored=None).value==zero
client=os.environ.get('WOT_0922_CLIENT')
if client:
 archive=zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))
 vehicle=marshal.loads(archive.read('scripts/client/Vehicle.pyc')[8:])
 code=next(c for c in vehicle.co_consts if isinstance(c,types.CodeType) and c.co_name=='_VehicleSpeedProvider')
 # Execute the exact class body and all four methods, including private-name ownership.
 namespace=types.FunctionType(code,{'__name__':'Vehicle','Math':Math,'__builtins__':__builtins__})()
 Speed=type('_VehicleSpeedProvider',(object,),namespace)
 speed=Speed()
 assert speed.value==zero
 old=speed._VehicleSpeedProvider__value
 old.value=(12.,13.,14.,15.)
 assert speed.value==Math.sample(old)==(12.,13.,14.,15.)
 replacement=Math.Vector4Basic();replacement.value=(1.,2.,3.,4.)
 speed.set(replacement)
 assert speed._VehicleSpeedProvider__value is replacement and speed.value==(1.,2.,3.,4.)
 speed.reset()
 assert speed.value==zero and speed._VehicleSpeedProvider__value is not old
 assert replacement.value==(1.,2.,3.,4.) and Math.sample(old)==(12.,13.,14.,15.)
 # Exercise Vehicle.__init__ through the original bytecode as well.
 klass=next(c for c in vehicle.co_consts if isinstance(c,types.CodeType) and c.co_name=='Vehicle')
 init=next(c for c in klass.co_consts if isinstance(c,types.CodeType) and c.co_name=='__init__')
 waiting={}
 class Entity(object): pass
 entity=Entity();entity.id=123
 types.FunctionType(init,{'weakref':weakref,'_VehicleSpeedProvider':Speed,'_g_waitingVehicle':waiting,'__builtins__':__builtins__})(entity)
 assert entity._Vehicle__speedInfo.value==zero and waiting[123]() is entity
 assert entity.proxy.id==123 and entity.assembler is None and entity.respawnCompactDescr is None
print('Vector4Basic SDK provider and CPython 2.7 consumer contracts passed')
)PY";
 assert(!PyRun_SimpleString(program));Py_Finalize();link.fini();
}
'''


class StockVector4BasicTest(unittest.TestCase):
    def test_sdk_provider_factory_and_exact_vehicle_bytecode(self):
        engine = Path(os.environ.get('WOT_BIGWORLD_ROOT',
            '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        python = Path(os.environ.get('WOT_PYTHON27',
            '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        header = engine / 'lib/pyscript/script_math.hpp'
        source = engine / 'lib/pyscript/script_math.cpp'
        if not header.exists() or not config.exists():
            self.skipTest('Pinned SDK and CPython 2.7 development files required')
        production = (ROOT / 'bigworld/stock_math.cpp').read_text()
        host = HOST.replace('@CLASS@', block(header.read_text(), 'class Vector4Basic :'))
        host = host.replace('@CONSTRUCTOR@', block(source.read_text(), 'Vector4Basic::Vector4Basic('))
        host = host.replace('@FACTORY@', block(production, 'static PyObject * stockVector4BasicNew('))
        link_header = (engine / 'lib/pyscript/py_factory_method_link.hpp').read_text()
        link_source = (engine / 'lib/pyscript/py_factory_method_link.cpp').read_text()
        host = host.replace('@FACTORY_LINK_CLASS@', block(link_header, 'class PyFactoryMethodLink :'))
        host = host.replace('@FACTORY_LINK_METHODS@', '\n'.join(block(link_source, signature) for signature in (
            'PyFactoryMethodLink::PyFactoryMethodLink(', 'PyFactoryMethodLink::~PyFactoryMethodLink()',
            'void PyFactoryMethodLink::init()', 'void PyFactoryMethodLink::fini()')))
        host = host.replace('@LINK@', block(production, 'class StockVector4BasicFactory :') + ';')
        with tempfile.TemporaryDirectory(prefix='stock-vector4-basic-') as directory:
            directory = Path(directory)
            cpp, binary = directory / 'host.cpp', directory / 'host'
            cpp.write_text(host)
            flags = [value for value in shlex.split(subprocess.check_output(
                [str(config), '--cflags'], text=True)) if value != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-Wno-register',
                            *flags, str(cpp), *libs, '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_original_factory_and_output_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required for original instructions')
        try:
            from stock_tree_lod_oracle import OriginalTreeInstructionOracle
            from unicorn import UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
        except ImportError:
            self.skipTest('pefile and Unicorn required for original instructions')
        oracle = OriginalTreeInstructionOracle(Path(client) / 'WorldOfTanks.exe')
        emulator = oracle.emulator
        records = []

        def boundary(uc, address, size, context):
            stack = uc.reg_read(UC_X86_REG_ESP)
            if address == 0x13d7fe9:
                self.assertEqual(oracle.word(stack + 4), 40)
                result = oracle.allocate(40)
                uc.mem_write(result, b'\xcd' * 40)
                records.append(result)
                cleanup = 0
            else:
                self.assertEqual(address, 0x6a8520)
                self.assertEqual(oracle.word(stack + 4), 0x1a0abf0)
                self.assertEqual(oracle.word(stack + 8), 0)
                result, cleanup = uc.reg_read(UC_X86_REG_ECX), 8
            uc.reg_write(UC_X86_REG_EAX, result)
            uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4 + cleanup)

        for address in (0x13d7fe9, 0x6a8520):
            emulator.hook_add(UC_HOOK_CODE, boundary, begin=address, end=address)
        # Deliberately unmapped args prove the original does not inspect them.
        for args in ((), (0xdead0000,), (0, 0xdead0000, 0xbeef0000)):
            oracle.call(0x694950, args)
            owner = records[-1]
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), owner + 4)
            self.assertEqual(oracle.word(owner), 0x14b96ac)
            self.assertEqual(bytes(emulator.mem_read(owner + 24, 16)), bytes(16))
            self.assertEqual(bytes(emulator.mem_read(owner + 20, 1)), b'\0')
            for words in ((1, 2, 3, 4), (0x80000000, 0x7f800000, 0xff800000, 0x7fc01234)):
                payload = struct.pack('<4I', *words)
                emulator.mem_write(owner + 24, payload)
                output = oracle.allocate(16)
                oracle.call(0x69c460, (output,), ecx=owner)
                self.assertEqual(bytes(emulator.mem_read(output, 16)), payload)
        self.assertEqual(len(records), len(set(records)))


if __name__ == '__main__':
    unittest.main()
