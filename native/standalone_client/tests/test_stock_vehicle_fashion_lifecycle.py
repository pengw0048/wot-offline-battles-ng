"""Fashion component lifecycle, exact #1513 consumers and x86 dispatch.

The production wrappers and component owner run inside real CPython 2.7.
Materials and the other appearance systems remain fixture boundaries; this
does not establish native Windows rendering or model-retirement safety.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from test_stock_vector4_basic import block


HOST = r'''
#include <Python.h>
#include <cstdlib>
#include <memory>
#include <vector>
#include <string>
static std::vector<std::string> receipts;
struct StockComponentState {
    virtual ~StockComponentState() {}
    virtual void activate()=0;
    virtual void deactivate()=0;
    virtual void destroy()=0;
};
struct Receipt : StockComponentState {
    std::string name;
    explicit Receipt(const char* n):name(n) {}
    void activate() { receipts.push_back(name+"+"); }
    void deactivate() { receipts.push_back(name+"-"); }
    void destroy() { receipts.push_back(name+"x"); }
};
@OWNER@
class WGBaseFashion {
public:
    VehicleFashionComponentOwner components_;
    PyObject* py_activate(PyObject*);
    PyObject* py_deactivate(PyObject*);
};
@METHODS@
struct Wrapper { PyObject_HEAD WGBaseFashion* owner; };
static PyTypeObject baseType={PyVarObject_HEAD_INIT(NULL,0)};
static PyTypeObject vehicleType={PyVarObject_HEAD_INIT(NULL,0)};
static PyObject* make(PyTypeObject* type,PyObject* args,PyObject* kwargs) {
    if (kwargs && PyDict_Size(kwargs)) {
        PyErr_SetString(PyExc_TypeError,"Fashion factory takes positional arguments");return NULL;
    }
    int crashed;
    if (type==&vehicleType) {
        if (!PyArg_ParseTuple(args,"i:WGVehicleFashion",&crashed)) return NULL;
    } else if (!PyArg_ParseTuple(args,":WGBaseFashion")) return NULL;
    Wrapper* object=(Wrapper*)type->tp_alloc(type,0);
    if (object) object->owner=new WGBaseFashion();
    return (PyObject*)object;
}
static void dispose(Wrapper* self) {
    delete self->owner;self->owner=NULL;Py_TYPE(self)->tp_free((PyObject*)self);
}
static PyObject* activate(Wrapper* self,PyObject* args) { return self->owner->py_activate(args); }
static PyObject* deactivate(Wrapper* self,PyObject* args) { return self->owner->py_deactivate(args); }
static PyMethodDef methods[]={
    {"activate",(PyCFunction)activate,METH_VARARGS,NULL},
    {"deactivate",(PyCFunction)deactivate,METH_VARARGS,NULL},
    {NULL,NULL,0,NULL}
};
// Only this host can seed native child receipts. Neither original fashion
// factory nor the production replacement exposes a component insertion API.
static PyObject* seed(PyObject*,PyObject* args) {
    PyObject* value;const char* name;
    if (!PyArg_ParseTuple(args,"Os",&value,&name)) return NULL;
    if (!PyObject_TypeCheck(value,&baseType)) return NULL;
    ((Wrapper*)value)->owner->components_.entries.push_back(
        std::make_shared<Receipt>(name));
    Py_RETURN_NONE;
}
static PyObject* trace(PyObject*,PyObject* args) {
    if (!PyArg_ParseTuple(args,"")) return NULL;
    PyObject* result=PyList_New(receipts.size());
    for (size_t i=0;i<receipts.size();++i)
        PyList_SET_ITEM(result,i,PyString_FromString(receipts[i].c_str()));
    receipts.clear();return result;
}
static PyMethodDef probeMethods[]={
    {"seed",seed,METH_VARARGS,NULL},{"trace",trace,METH_VARARGS,NULL},{NULL,NULL,0,NULL}
};
int main(int argc,char** argv) {
    if (argc!=3) return 2;
    Py_Initialize();
    baseType.tp_name=(char*)"BigWorld.WGBaseFashion";
    baseType.tp_basicsize=sizeof(Wrapper);baseType.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE;
    baseType.tp_new=make;baseType.tp_dealloc=(destructor)dispose;
    baseType.tp_methods=std::getenv("WOT_FASHION_OMIT_LIFECYCLE")?NULL:methods;
    vehicleType.tp_name=(char*)"BigWorld.WGVehicleFashion";
    vehicleType.tp_basicsize=sizeof(Wrapper);vehicleType.tp_flags=Py_TPFLAGS_DEFAULT;
    vehicleType.tp_new=make;vehicleType.tp_base=&baseType;
    if (PyType_Ready(&baseType)<0 || PyType_Ready(&vehicleType)<0) return 3;
    PyObject* module=Py_InitModule("BigWorld",NULL);
    Py_INCREF(&baseType);PyModule_AddObject(module,"WGBaseFashion",(PyObject*)&baseType);
    Py_INCREF(&vehicleType);PyModule_AddObject(module,"WGVehicleFashion",(PyObject*)&vehicleType);
    Py_InitModule("fashion_probe",probeMethods);
    PySys_SetArgv(argc-1,argv+1);
    FILE* file=fopen(argv[1],"rb");if (!file) return 4;
    int result=PyRun_SimpleFileEx(file,argv[1],1);
    if (PyErr_Occurred()) { PyErr_Print();result=-1; }
    Py_Finalize();return result?1:0;
}
'''


DRIVER = r'''
import collections,gc,marshal,os,sys,types,zipfile
import BigWorld as bw
from fashion_probe import seed,trace
archive=zipfile.ZipFile(os.path.join(sys.argv[1],'res','packages','scripts.pkg'))
def code(member,name,line=None):
    def visit(c):
        if c.co_name==name and (line is None or c.co_firstlineno==line): return c
        for item in c.co_consts:
            if isinstance(item,types.CodeType):
                found=visit(item)
                if found is not None:return found
    result=visit(marshal.loads(archive.read(member)[8:]));assert result is not None
    return result
Parts=collections.namedtuple('Parts','chassis hull turret gun')
prepare=types.FunctionType(code('scripts/client/vehicle_systems/camouflages.pyc','prepareFashions'),
                          {'BigWorld':bw,'VehiclePartsTuple':Parts,'__builtins__':__builtins__})
for damaged in (False,True):
    fashions=prepare(damaged)
    if damaged:assert fashions==(None,None,None,None)
    else:
        assert isinstance(fashions.chassis,bw.WGVehicleFashion)
        assert all(isinstance(x,bw.WGBaseFashion) for x in fashions)
        for fashion in (() if os.environ.get('WOT_FASHION_OMIT_LIFECYCLE') else fashions):
            assert fashion.activate() is None and fashion.deactivate() is None
            for method in (fashion.activate,fashion.deactivate):
                for args in ((1,), (None,), (1,2)):
                    try:method(*args)
                    except TypeError:pass
                    else:raise AssertionError('Nonempty fashion lifecycle arguments accepted')
assert trace()==[] # Original factories create no native children.

class Box(object):
    def __init__(self,**kw):self.__dict__.update(kw)
def nothing(*args):pass
class Signal(list):
    def __iadd__(self,value):self.append(value);return self
    def __isub__(self,value):self.remove(value);return self
class Filter(object):
    enableStabilisedMatrix=enableLagDetection=reset=syncGunAngles=nothing
class Base(object):
    activate=deactivate=nothing
class CompoundAppearance(Base):pass
member='scripts/client/vehicle_systems/CompoundAppearance.pyc'
env={'__builtins__':__builtins__,'CompoundAppearance':CompoundAppearance,'BigWorld':bw,'_PERIODIC_TIME':.25,
     'Math':Box(Matrix=lambda *a:Box(yaw=.25,pitch=.5)),'TankPartIndexes':Box(HULL=1),
     'VehiclePartsTuple':Parts,'BattleReplay':Box(isFinished=lambda:False),
     'model_assembler':Box(setupTurretRotations=nothing)}
for name in ('activate','deactivate'):
    setattr(CompoundAppearance,name,types.FunctionType(code(member,name),env,name,(True,) if name=='deactivate' else ()))
CompoundAppearance.fashion=property(types.FunctionType(code(member,'<lambda>',75),env))
CompoundAppearance.compoundModel=property(types.FunctionType(code(member,'<lambda>',72),env))
for name in ('__onModelsRefresh','__prepareSystemsForDamagedVehicle','__setFashions'):
    setattr(CompoundAppearance,'_CompoundAppearance'+name,types.FunctionType(code(member,name),env))
CompoundAppearance.setVehicle=lambda self,vehicle:setattr(self,'_CompoundAppearance__vehicle',vehicle)
player=Box(handleVehicleCollidedVehicle=nothing,arena=Box(onPeriodChange=Signal()),
           inputHandler=Box(onCameraChanged=Signal(),removeVehicleFromCameraCollider=nothing))
bw.player=lambda:player
bw.callback=lambda *a:123
bw.cancelCallback=nothing
def appearance(damaged,fashions):
    a=CompoundAppearance()
    a._CompoundAppearance__activated=False
    a._CompoundAppearance__vehicle=Box(isPlayerVehicle=False,isStrafing=False,filter=Filter(),isTurretDetached=False)
    a._CompoundAppearance__typeDesc=Box(type=Box(tags=set()),name='vehicle')
    a._CompoundAppearance__filter=Filter();a.filter=a._CompoundAppearance__filter
    a._CompoundAppearance__compoundModel=Box(matrix=Box())
    a._CompoundAppearance__inSpeedTreeCollision=False
    a._CompoundAppearance__chassisDecal=Box(attach=nothing,detach=nothing)
    a._CompoundAppearance__currentDamageState=Box(isCurrentModelDamaged=damaged,modelState='destroyed')
    a.damageState=a._CompoundAppearance__currentDamageState
    a._CompoundAppearance__periodicTimerID=None
    a._CompoundAppearance__fashions=fashions
    a._CompoundAppearance__vehicleStickers=Box(detach=nothing)
    a._CompoundAppearance__crashedTracksCtrl=None
    a._CompoundAppearance__boundEffects=Box(stop=nothing)
    for name in ('linkCompound','createAndAttachStickers','startSystems','stopSystems',
                 'arenaPeriodChanged','onCameraChanged','onPeriodicTimer','stopEffects',
                 'setupModels','reattachComponents','destroySystems','destroyEngineAudition'):
        setattr(a,'_CompoundAppearance__'+name,nothing)
    a.setupGunMatrixTargets=a.onModelChanged=nothing
    a.lodCalculator=a.swingingAnimator=a.detailedEngineState=None
    a.flyingInfoProvider=None;a.waterSensor=Box();a._CompoundAppearance__weaponEnergy=0
    a.turretMatrix=Box();a.gunMatrix=Box()
    return a

# Run unchanged original appearance activation/deactivation bytecode. The
# test-only children make calls observable, including the original guards.
for roundID in (1,2):
    healthy=prepare(False)
    seed(healthy.chassis,'round%d'%roundID)
    a=appearance(False,healthy)
    a.activate();a.activate() # Already-active guard must not dispatch twice.
    assert trace()==['round%d+'%roundID]
    a.deactivate(False);a.deactivate(False)
    assert trace()==['round%d-'%roundID]
    # Rebind the same fashion after model refresh; its component owner is
    # retained, while the previous vehicle/filter binding is replaced.
    a=appearance(False,healthy);a.activate();a.deactivate()
    assert trace()==['round%d+'%roundID,'round%d-'%roundID]
    # Execute the stock asynchronous damaged-model replacement callback and
    # its stock preparation function. The old fashion is deactivated before
    # the damaged appearance removes all four fashions and reactivates.
    a=appearance(False,healthy);a.activate()
    assert trace()==['round%d+'%roundID]
    a.damageState.isCurrentModelDamaged=True
    replacement=Box(matrix=Box(),root=Box(),setupFashions=nothing)
    a._CompoundAppearance__onModelsRefresh('destroyed',{'vehicle':replacement})
    assert a.fashion is None and a._CompoundAppearance__activated
    assert trace()==['round%d-'%roundID]
    a.deactivate();assert trace()==[]
    del a,healthy;gc.collect()
    assert trace()==['round%d-'%roundID,'round%dx'%roundID]
    damaged=prepare(True);a=appearance(True,damaged)
    a.activate();a.deactivate();del a,damaged;gc.collect()
    assert trace()==[]
assert player.arena.onPeriodChange==[] and player.inputHandler.onCameraChanged==[]

# Native dispatch itself does not deduplicate. Child components own their
# activation guards, and destructor retirement visits insertion order.
f=bw.WGVehicleFashion(1)
seed(f,'a');seed(f,'b');f.activate();f.activate();f.deactivate()
assert trace()==['a+','b+','a+','b+','a-','b-']
del f;gc.collect();assert trace()==['a-','ax','b-','bx']
print('Fashion lifecycle and exact #1513 PYC consumers passed')
'''


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value:
        test.skipTest('Exact #1513 client required')
    path = Path(value)
    for name in ('WorldOfTanks.exe', 'res/packages/scripts.pkg'):
        test.assertTrue((path / name).is_file(), 'Configured #1513 file missing: ' + name)
    return path


class FashionLifecycleTest(unittest.TestCase):
    def test_real_wrappers_owner_and_original_pyc_lifecycle(self):
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
        self.assertIn('Python 2.7.18', subprocess.check_output(
            [str(python), '--version'], stderr=subprocess.STDOUT, text=True))
        source = (ROOT / 'bigworld/stock_vehicle_fashions.cpp').read_text()
        owner = source[source.index('struct VehicleFashionComponentOwner\n'):source.index('\nstruct VehicleFashionState\n')]
        methods = '\n'.join(block(source, 'PyObject * WGBaseFashion::py_' + name + '(')
                            for name in ('activate', 'deactivate'))
        for name in ('activate', 'deactivate'):
            table = source[source.index('PY_BEGIN_METHODS( WGBaseFashion )'):source.index('PY_END_METHODS()', source.index('PY_BEGIN_METHODS( WGBaseFashion )'))]
            self.assertIn('PY_METHOD( ' + name + ' )', table)
        flags = shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
        flags = [f for f in flags if not f.startswith('-O') and f != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        with tempfile.TemporaryDirectory(prefix='fashion-lifecycle-') as directory:
            path = Path(directory)
            (path / 'host.cpp').write_text(HOST.replace('@OWNER@', owner).replace('@METHODS@', methods))
            (path / 'consumer.py').write_text(DRIVER)
            binary = path / 'host'
            subprocess.run([compiler, '-std=c++14', '-O0', '-Wall', '-Wextra', '-Werror',
                            '-Wno-missing-field-initializers', '-Wno-deprecated-register',
                            '-Wno-writable-strings', *flags, str(path / 'host.cpp'), *libs,
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary), str(path / 'consumer.py'), str(original)], check=True,
                           env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            negative = subprocess.run([str(binary), str(path / 'consumer.py'), str(original)],
                                      capture_output=True, text=True,
                                      env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1',
                                               WOT_FASHION_OMIT_LIFECYCLE='1'))
            self.assertNotEqual(negative.returncode, 0)
            self.assertIn('CompoundAppearance.py", line 389', negative.stderr)
            self.assertIn("has no attribute 'activate'", negative.stderr)

    def test_original_x86_factory_and_component_dispatch(self):
        original = client(self)
        try:
            from stock_vehicle_fashion_lifecycle_oracle import OriginalFashionLifecycle
        except ImportError as error:
            self.skipTest('Unicorn/pefile required: ' + str(error))
        oracle = OriginalFashionLifecycle(original / 'WorldOfTanks.exe')
        oracle.check_factories()
        for children in (0, 1, 3):
            for activate in (False, True):
                self.assertEqual(oracle.dispatch(children, activate), list(range(children)))
                self.assertEqual(oracle.dispatch(children, activate, 1), ['arity-error'])


if __name__ == '__main__':
    unittest.main()
