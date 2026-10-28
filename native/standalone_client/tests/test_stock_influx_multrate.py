"""Real MultRate wrapper/owner and the unchanged #1513 PixieBG producer.

The scene effect records rate updates; resource loading, emitter execution and
Windows rendering remain outside this focused Python conversion regression.
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

HOST = r'''
#include <Python.h>
#include <memory>
namespace Script {
int setData(PyObject*,double&,const char*);
int setData(PyObject*,float&,const char*);
}
@CONVERSIONS@
struct SceneEffect { float rate=-1.f;unsigned calls=0;void multRate(float v){rate=v;++calls;} };
struct ParticleSystemAction { virtual ~ParticleSystemAction() {} };
class InfluxPSA : public ParticleSystemAction {
public:
    float rateMultiplier_=1.f;
    std::weak_ptr<SceneEffect> effect_;
@OWNER@
};
struct ParticleSystemActionPtr {
    std::shared_ptr<ParticleSystemAction> value;
    ParticleSystemAction* getObject() { return value.get(); }
};
class PyInfluxPSA {
public:
    ParticleSystemActionPtr action{std::make_shared<InfluxPSA>()};
    std::shared_ptr<SceneEffect> scene;
    ParticleSystemActionPtr& pAction() { return action; }
    PyObject* py_MultRate(PyObject*);
};
@WRAPPER@
struct Wrapper { PyObject_HEAD PyInfluxPSA* owner; };
static PyTypeObject influxType={PyVarObject_HEAD_INIT(NULL,0)};
static PyObject* create(PyTypeObject* type,PyObject* args,PyObject*) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    Wrapper* object=(Wrapper*)type->tp_alloc(type,0);
    if (object) object->owner=new PyInfluxPSA();
    return (PyObject*)object;
}
static void dispose(Wrapper* self) { delete self->owner;Py_TYPE(self)->tp_free((PyObject*)self); }
static PyObject* multRate(Wrapper* self,PyObject* args) { return self->owner->py_MultRate(args); }
static PyObject* snapshot(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    auto action=dynamic_cast<InfluxPSA*>(self->owner->pAction().getObject());
    if (!action) { PyErr_SetString(PyExc_RuntimeError,"Invalid native Influx action owner");return NULL; }
    return Py_BuildValue("ffi",action->rateMultiplier_,
        self->owner->scene?self->owner->scene->rate:-1.f,
        self->owner->scene?self->owner->scene->calls:0);
}
static PyObject* attach(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    self->owner->scene=std::make_shared<SceneEffect>();
    static_cast<InfluxPSA*>(self->owner->pAction().getObject())->effect_=self->owner->scene;
    Py_RETURN_NONE;
}
static PyObject* retire(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    self->owner->scene.reset();Py_RETURN_NONE;
}
static PyObject* invalid(Wrapper* self,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    self->owner->action.value=std::make_shared<ParticleSystemAction>();Py_RETURN_NONE;
}
static PyMethodDef methods[]={
    {"MultRate",(PyCFunction)multRate,METH_VARARGS,NULL},
    {"snapshot",(PyCFunction)snapshot,METH_VARARGS,NULL},
    {"attach",(PyCFunction)attach,METH_VARARGS,NULL},
    {"retire",(PyCFunction)retire,METH_VARARGS,NULL},
    {"invalid",(PyCFunction)invalid,METH_VARARGS,NULL},{NULL,NULL,0,NULL}
};
int main(int argc,char** argv) {
    if (argc!=3) return 2;
    Py_Initialize();
    influxType.tp_name=(char*)"Pixie.InfluxPSA";influxType.tp_basicsize=sizeof(Wrapper);
    influxType.tp_flags=Py_TPFLAGS_DEFAULT;influxType.tp_new=create;
    influxType.tp_dealloc=(destructor)dispose;influxType.tp_methods=methods;
    if (PyType_Ready(&influxType)<0) return 3;
    PyObject* module=Py_InitModule("influx_probe",NULL);
    Py_INCREF(&influxType);PyModule_AddObject(module,"Influx",(PyObject*)&influxType);
    PySys_SetArgv(argc-1,argv+1);
    FILE* file=fopen(argv[1],"rb");if (!file) return 4;
    int result=PyRun_SimpleFileEx(file,argv[1],1);
    if (PyErr_Occurred()) { PyErr_Print();result=-1; }
    Py_Finalize();return result?1:0;
}
'''

DRIVER = r'''
import marshal,math,os,sys,types,zipfile
from influx_probe import Influx
z=zipfile.ZipFile(os.path.join(sys.argv[1],'res','packages','scripts.pkg'))
def find(c):
 if c.co_name=='enablePixie':return c
 for item in c.co_consts:
  if isinstance(item,types.CodeType):
   found=find(item)
   if found is not None:return found
errors=[]
enable=types.FunctionType(find(marshal.loads(z.read('scripts/client/helpers/PixieBG.pyc')[8:])),
                         {'__builtins__':__builtins__,'LOG_CURRENT_EXCEPTION':lambda:errors.append(sys.exc_info()[1])})
class System(object):
 def __init__(self):self.source=Influx()
 def action(self,index):assert index==16;return self.source
class Pixie(object):
 def __init__(self):self.systems=[System(),System()]
 def nSystems(self):return len(self.systems)
 def system(self,index):return self.systems[index]
pixie=Pixie()
if os.environ.get('WOT_INFLUX_NEGATIVE'):
 enable(pixie,False)
 assert len(errors)==2 and all(isinstance(e,SystemError) for e in errors)
 assert all(str(e)=='error return without exception set' for e in errors)
 assert all(s.source.snapshot()[0]==1. for s in pixie.systems)
 print('Old wrapper reproduces both original PixieBG.py:18 SystemErrors')
 sys.exit(0)
for turn_on in (False,True,False,True):
 enable(pixie,turn_on)
 assert errors==[]
 assert all(s.source.snapshot()[0]==float(turn_on) for s in pixie.systems)
value=Influx()
for number in (0.,1.,.5,-2.,3,True,4L):
 assert value.MultRate(number) is None
 assert value.snapshot()[0]==float(number)
value.attach();value.MultRate(.25)
assert value.snapshot()==(.25,.25,1)
value.MultRate(0.);assert value.snapshot()==(0.,0.,2)
before=value.snapshot()
for args in ((),(1,2),(None,),('bad',),([],),(object(),)):
 try:value.MultRate(*args)
 except TypeError:pass
 else:raise AssertionError('Invalid MultRate arguments accepted')
 assert value.snapshot()==before
for number in (float('inf'),float('nan')):
 assert value.MultRate(number) is None
 assert (math.isnan if math.isnan(number) else math.isinf)(value.snapshot()[0])
value.retire();value.retire();value.MultRate(.75)
assert value.snapshot()==(.75,-1.,0)
value.invalid()
try:value.MultRate(1.)
except RuntimeError as error:assert str(error)=='Invalid native Influx action owner'
else:raise AssertionError('Wrong native action owner accepted')
print('Production MultRate wrapper, owner updates and exact #1513 enablePixie passed')
'''


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value:test.skipTest('Exact #1513 client required')
    path = Path(value)
    for name in ('WorldOfTanks.exe', 'res/packages/scripts.pkg'):
        test.assertTrue((path / name).is_file(), 'Configured #1513 file missing: ' + name)
    return path


class InfluxMultRateTest(unittest.TestCase):
    def test_production_wrapper_and_original_pixiebg(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            if 'CXX' in os.environ:self.fail('Configured C++ compiler is unavailable: ' + compiler)
            self.skipTest('C++ compiler required')
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if 'WOT_PYTHON27' in os.environ and (not python.is_file() or not config.is_file()):
            self.fail('Configured CPython 2.7 development files missing')
        original = client(self)
        if not python.is_file() or not config.is_file():self.skipTest('CPython 2.7 development files required')
        self.assertIn('Python 2.7.18', subprocess.check_output([str(python), '--version'], stderr=subprocess.STDOUT, text=True))
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:self.fail('Configured public SDK archive missing')
            self.skipTest('Pinned public SDK archive required')
        with tarfile.open(archive) as tar:
            member = next((m for m in tar if m.name.endswith('/programming/bigworld/lib/pyscript/script.cpp')), None)
            self.assertIsNotNone(member, 'Pinned SDK Script source missing')
            script = tar.extractfile(member).read().decode('utf-8-sig')
        source = (ROOT / 'bigworld/stock_tps_scene.cpp').read_text()
        wrapper = block(source, 'PyObject * PyInfluxPSA::py_MultRate(')
        influx = source[source.index('class InfluxPSA :'):]
        owner = block(influx, '    void multRate( float value )')
        conversions = '\n'.join(block(script, 'int Script::setData( PyObject * pObject, ' + kind)
                                for kind in ('double &', 'float &'))
        template = HOST.replace('@OWNER@', owner).replace('@CONVERSIONS@', conversions)
        flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
        flags = [flag for flag in flags if not flag.startswith('-O') and flag != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='influx-multrate-') as directory:
            path = Path(directory);(path / 'consumer.py').write_text(DRIVER)
            for negative in (False, True):
                method = wrapper
                if negative:
                    before = 'if (Script::setData( PyTuple_GET_ITEM(args, 0), value, "MultRate argument 1" ) != 0) return NULL;'
                    self.assertEqual(method.count(before), 1)
                    method = method.replace(before, 'if (!Script::setData( PyTuple_GET_ITEM(args, 0), value, "MultRate argument 1" )) return NULL;', 1)
                cpp = path / 'host.cpp';cpp.write_text(template.replace('@WRAPPER@', method))
                binary = path / ('negative' if negative else 'host')
                subprocess.run([compiler, '-std=c++14', '-O0', '-Wall', '-Wextra', '-Werror',
                                '-Wno-missing-field-initializers', '-Wno-deprecated-register',
                                '-Wno-writable-strings', *flags, str(cpp), *libs, '-o', str(binary)], check=True)
                env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
                if negative:env['WOT_INFLUX_NEGATIVE'] = '1'
                subprocess.run([str(binary), str(path / 'consumer.py'), str(original)], env=env, check=True)

    def test_original_pe_converts_before_modifying_the_native_owner(self):
        original = client(self)
        try:
            import pefile
            import capstone
        except ImportError as error:self.skipTest('pefile/capstone required: ' + str(error))
        pe = pefile.PE(str(original / 'WorldOfTanks.exe'));self.assertEqual(pe.FILE_HEADER.Machine, 0x14c)
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        code = {(i.address, i.mnemonic, i.op_str) for i in decoder.disasm(
            pe.get_data(0xfab240 - pe.OPTIONAL_HEADER.ImageBase, 0x93), 0xfab240)}
        self.assertIn((0xfab281, 'call', '0x68fba0'), code)  # Script::setData(float).
        self.assertIn((0xfab28b, 'jne', '0xfab252'), code)   # Nonzero means failure.
        self.assertIn((0xfab298, 'movss', 'dword ptr [eax + 0x58], xmm0'), code)
        self.assertIn((0xfab29f, 'je', '0xfab2ac'), code)    # Effect not created yet.
        self.assertIn((0xfab2a9, 'call', 'dword ptr [eax + 0x28]'), code)


if __name__ == '__main__':
    unittest.main()
