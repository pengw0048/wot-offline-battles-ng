"""Exact #1513 draw constants and its empty Havok attachment factory.

Host provider conversion is an explicit binding boundary. The original factory
instructions and original Vehicle/CompoundAppearance consumers run separately.
"""
from pathlib import Path
import os
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONSTANTS = {'ColorPassBit': (0x658600, 1), 'ReflectionPassBit': (0x658640, 2),
             'ShadowPassBit': (0x658650, 4), 'DepthPassBit': (0x658620, 8),
             'DrawAll': (0x658630, 15)}

HOST = r'''
#include <Python.h>
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <string>
#include <memory>
struct MatrixProviderPtr {PyObject* object=nullptr;~MatrixProviderPtr(){Py_XDECREF(object);}};
unsigned conversions=0;
struct Vector3 {float x,y,z;};
namespace BW {using string=std::string;}
namespace Script {
int setData(PyObject* object,Vector3& output,const char*){
 if(!PyTuple_Check(object)){PyErr_SetString(PyExc_TypeError,"Expected Vector3");return -1;}
 return PyArg_ParseTuple(object,"fff",&output.x,&output.y,&output.z)?0:-1;
}
int setData(PyObject* object,float& output,const char*){
 output=float(PyFloat_AsDouble(object));return PyErr_Occurred()?-1:0;
}
int setData(PyObject* object,MatrixProviderPtr& output,const char*){
 ++conversions;
 if(object==Py_None)return 0;
 if(!PyCapsule_IsValid(object,"MatrixProvider")){
  PyErr_SetString(PyExc_TypeError,"Expected MatrixProvider");return -1;
 }
 Py_INCREF(object);output.object=object;return 0;
}
PyObject* argCountError(const char*,int,int,...);
}
@ARG_COUNT_ERROR@
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define PY_MODULE_FUNCTION(...)
#define PY_MODULE_ATTRIBUTE(module,name,expression) PyObject* attribute##name(){return expression;}
#include "@PRODUCTION@"
PyObject* factory(PyObject*,PyObject* args){return py_wg_createHKAttachment(args);}
PyObject* explosion(PyObject*,PyObject* args){return py_wg_havokExplosion(args);}
int nativeProvider=0;
PyObject* provider(PyObject*,PyObject*){return PyCapsule_New(&nativeProvider,"MatrixProvider",NULL);}
PyMethodDef methods[]={{"wg_createHKAttachment",factory,METH_VARARGS,NULL},
 {"wg_havokExplosion",explosion,METH_VARARGS,NULL},
 {"provider",provider,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
int main(){
 Py_Initialize();auto module=Py_InitModule("BigWorld",methods);assert(module);
 @CONSTANTS@
 const char* program=R"PY(
import BigWorld,sys,os,marshal,types,zipfile
assert [BigWorld.ColorPassBit,BigWorld.ReflectionPassBit,BigWorld.ShadowPassBit,BigWorld.DepthPassBit,BigWorld.DrawAll]==[1,2,4,8,15]
assert all(type(getattr(BigWorld,name)) is int for name in ('ColorPassBit','ReflectionPassBit','ShadowPassBit','DepthPassBit','DrawAll'))
node=BigWorld.provider();refs=sys.getrefcount(node)
for body in [None, object(), 12, 'not inspected']:
 assert BigWorld.wg_createHKAttachment(node,body) is None
 assert BigWorld.wg_createHKAttachment(None,body) is None
assert sys.getrefcount(node)==refs
for args in [(), (node,), (node,None,0), (1,None), (object(),None), ('node',None)]:
 try: BigWorld.wg_createHKAttachment(*args)
 except TypeError: pass
 else: raise AssertionError(args)
for args in [((1,2,3),4,5), ((0.,0.,0.),-1,-2), ((1.,2.,3.),float('nan'),float('inf'))]:
 assert BigWorld.wg_havokExplosion(*args) is None
for args in [(), ((1,2,3),), ((1,2,3),4,5,6), ((1,2),4,5), ((1,2,3),object(),5), ((1,2,3),4,object())]:
 try: BigWorld.wg_havokExplosion(*args)
 except TypeError: pass
 else: raise AssertionError(args)
client=os.environ.get('WOT_0922_CLIENT')
if client:
 archive=zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))
 def method(member,klass,name):
  code=marshal.loads(archive.read(member)[8:])
  root=next(c for c in code.co_consts if isinstance(c,types.CodeType) and c.co_name==klass)
  return next(c for c in root.co_consts if isinstance(c,types.CodeType) and c.co_name==name)
 class State(object):pass
 appearance=State();appearance.compoundModel=State();appearance._CompoundAppearance__crashedTracksCtrl=None
 stickers=[];appearance.showStickers=stickers.append
 change=method('scripts/client/vehicle_systems/CompoundAppearance.pyc','CompoundAppearance','changeDrawPassVisibility')
 appearance.changeDrawPassVisibility=types.MethodType(types.FunctionType(change,{'BigWorld':BigWorld,'__builtins__':__builtins__}),appearance)
 vehicle=State();vehicle.isStarted=False;vehicle.appearance=appearance
 show=types.FunctionType(method('scripts/client/Vehicle.pyc','Vehicle','show'),{'BigWorld':BigWorld,'__builtins__':__builtins__})
 # Both constants are evaluated even before a vehicle starts its visuals.
 show(vehicle,True);show(vehicle,False);assert not stickers
 vehicle.isStarted=True;show(vehicle,True)
 assert appearance.compoundModel.visible==15 and not appearance.compoundModel.skipColorPass
 show(vehicle,False)
 assert appearance.compoundModel.visible==4 and appearance.compoundModel.skipColorPass
 assert stickers==[True,True,False,False]
 # The native mask is also passed to crashed-track visibility.
 class Tracks(object):
  def setVisible(self,mask):self.mask=mask
 tracks=Tracks();appearance._CompoundAppearance__crashedTracksCtrl=tracks
 for mask in [0,1,2,4,8,15]:
  appearance.changeDrawPassVisibility(mask)
  assert appearance.compoundModel.visible==mask and tracks.mask==mask
  assert appearance.compoundModel.skipColorPass==(not bool(mask&1))
 # Every normal CompoundAppearance.__setupHavok part accepts the original
 # factory's None result and leaves that part's node unattached.
 class Parts(object):HULL='hull';TURRET='turret';CHASSIS='chassis';GUN='gun'
 setup=types.FunctionType(method('scripts/client/vehicle_systems/CompoundAppearance.pyc','CompoundAppearance','__setupHavok'),{'BigWorld':BigWorld,'TankPartNames':Parts,'__builtins__':__builtins__})
 instance=State();desc=State();compound=State();instance._CompoundAppearance__typeDesc=desc;instance.compoundModel=compound
 calls=[];nodes={name:BigWorld.provider() for name in ('hull','turret','chassis','gun')}
 compound.node=lambda name:(calls.append(name),nodes[name])[1]
 for name in nodes:
  part=State();part.hitTester=State();part.hitTester.getBspModel=lambda:object();setattr(desc,name,part)
 setup(instance);assert calls==['hull','turret','chassis','gun']
 # Avatar uses endPoint, splashStrength, splashRadius in this order; the
 # empty native factory must not stop presentation or delayed bookkeeping.
 avatar=State();avatar.userSeesWorld=lambda:True
 mover=State();exploded=[];mover.explode=lambda *args:exploded.append(args)
 avatar._PlayerAvatar__projectileMover=mover
 vehicles=State();vehicles.g_cache=State()
 params={'shellVelocity':800.,'shellMass':6.,'splashRadius':9.,'splashStrength':3.}
 effects={'physicsParams':params};vehicles.g_cache.shotEffects={7:effects}
 explode=types.FunctionType(method('scripts/client/Avatar.pyc','PlayerAvatar','explodeProjectile'),{'BigWorld':BigWorld,'vehicles':vehicles,'EFFECT_MATERIALS':{2:'ground'},'__builtins__':__builtins__})
 assert explode(avatar,123,7,2,(1.,2.,3.),None,[]) is None
 assert len(exploded)==1 and exploded[0][:3]==(123,effects,'ground')
 delayed=types.FunctionType(method('scripts/client/AreaDestructibles.pyc','DestructiblesManager','__delayedHavokExplosion'),{'BigWorld':BigWorld,'__builtins__':__builtins__})
 destructibles=State();destructibles._DestructiblesManager__spaceID=1
 info=[((1.,2.,3.),None,None,None,9.,3.),True]
 delayed(destructibles,2,info);assert info[1] is True
 delayed(destructibles,1,info);assert info[1] is False
print('Vehicle draw masks and original empty HK attachment consumer passed')
)PY";
 assert(!PyRun_SimpleString(program));assert(conversions>0);Py_Finalize();
}
'''


class StockVehicleRenderingTest(unittest.TestCase):
    def test_native_binding_and_original_vehicle_consumers(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files required')
        engine = Path(os.environ.get('WOT_BIGWORLD_ROOT', '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        if not (engine / 'lib/pyscript/script.cpp').exists():
            self.skipTest('Pinned SDK argument-count error owner required')
        from test_stock_vector4_basic import block
        error_owner = block((engine / 'lib/pyscript/script.cpp').read_text(), 'PyObject * Script::argCountError(')
        with tempfile.TemporaryDirectory(prefix='stock-vehicle-rendering-') as directory:
            directory = Path(directory)
            for name in ('pch.hpp', 'pyscript/script.hpp', 'pyscript/script_math.hpp'):
                path = directory / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('')
            source, binary = directory / 'host.cpp', directory / 'host'
            constants = '\n'.join('assert(!PyModule_AddObject(module,"' + name + '",attribute' + name + '()));' for name in CONSTANTS)
            source.write_text(HOST.replace('@PRODUCTION@', str(ROOT / 'bigworld/stock_vehicle_rendering.cpp')).replace('@CONSTANTS@', constants).replace('@ARG_COUNT_ERROR@', error_owner))
            flags = [value for value in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True)) if value != '-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
            subprocess.run(['c++', '-std=c++14', '-UNDEBUG', '-Wno-deprecated-register', *flags,
                            '-I', str(directory), str(source), *libs, '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_original_constant_and_empty_attachment_instructions(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required for original instructions')
        try:
            from stock_tree_lod_oracle import OriginalTreeInstructionOracle
            from unicorn import UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
        except ImportError:
            self.skipTest('pefile and Unicorn required for original instructions')
        oracle = OriginalTreeInstructionOracle(Path(client) / 'WorldOfTanks.exe')
        emulator = oracle.emulator
        result = {'count': 2, 'convert_error': False, 'node': 0}
        second = oracle.allocate(16)
        emulator.mem_write(second, struct.pack('<4I', 100, 0xdeadbeef, 0, 0))
        node = oracle.allocate(16)
        emulator.mem_write(node, struct.pack('<4I', 0, 100, 0, 0))
        conversions = []

        def boundary(uc, address, size, context):
            stack = uc.reg_read(UC_X86_REG_ESP)
            if address == 0xfe1200:
                value = oracle.word(stack + 4)
                result['constant'] = value
            elif address == 0xfb3610:
                value = result['count']
            elif address == 0xfb3390:
                index = oracle.word(stack + 8)
                value = second if index else 0xdead0000
            elif address == 0x69ef50:
                self.assertEqual(oracle.word(stack + 4), 0xdead0000)
                output = oracle.word(stack + 8)
                conversions.append(result['node'])
                emulator.mem_write(output, struct.pack('<I', result['node']))
                if result['node']:
                    emulator.mem_write(node + 4, struct.pack('<I', oracle.word(node + 4) + 1))
                value = int(result['convert_error'])
            else:
                self.assertEqual(address, 0x68e100)
                result['argument_error'] = True
                value = 0
            uc.reg_write(UC_X86_REG_EAX, value)
            uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4)

        for address in (0xfe1200, 0xfb3610, 0xfb3390, 0x69ef50, 0x68e100):
            emulator.hook_add(UC_HOOK_CODE, boundary, begin=address, end=address)
        for name, (producer, value) in CONSTANTS.items():
            oracle.call(producer)
            self.assertEqual(result['constant'], value, name)
        for provider in (0, node):
            result['node'] = provider
            oracle.call(0x653540, (0, 0xdead0000))
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), 0x1a5c798)
            self.assertEqual(oracle.word(second), 100)
            self.assertEqual(oracle.word(node + 4), 100)
        self.assertEqual(conversions, [0, node])
        result['node'], result['convert_error'] = 0, True
        oracle.call(0x653540, (0, 0xdead0000))
        self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), 0)
        for count in (0, 1, 3):
            result['count'], result['argument_error'] = count, False
            oracle.call(0x653540, (0, 0xdead0000))
            self.assertTrue(result['argument_error'])
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), 0)
        self.assertEqual(len(conversions), 3)

    def test_original_explosion_converts_arguments_without_backend_calls(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required for original instructions')
        try:
            from stock_tree_lod_oracle import OriginalTreeInstructionOracle
            from unicorn import UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP
        except ImportError:
            self.skipTest('pefile and Unicorn required for original instructions')
        oracle = OriginalTreeInstructionOracle(Path(client) / 'WorldOfTanks.exe')
        emulator = oracle.emulator
        state = {'count': 3, 'fail': None, 'error': False}
        converted = []

        def boundary(uc, address, size, context):
            stack = uc.reg_read(UC_X86_REG_ESP)
            if address == 0xfb3610:
                result = state['count']
            elif address == 0xfb3390:
                result = 0xdead1000 + oracle.word(stack + 8)
            elif address in (0x690240, 0x68fba0):
                index = oracle.word(stack + 4) - 0xdead1000
                self.assertEqual(address, 0x690240 if index == 0 else 0x68fba0)
                converted.append(index)
                result = int(state['fail'] == index)
                if not result:
                    output = oracle.word(stack + 8)
                    payload = struct.pack('<3f', 1., 2., 3.) if index == 0 else struct.pack('<f', -4. if index == 1 else float('inf'))
                    uc.mem_write(output, payload)
            else:
                self.assertEqual(address, 0x68e100)
                state['error'], result = True, 0
            uc.reg_write(UC_X86_REG_EAX, result)
            uc.reg_write(UC_X86_REG_EIP, oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP, stack + 4)

        for address in (0xfb3610, 0xfb3390, 0x690240, 0x68fba0, 0x68e100):
            emulator.hook_add(UC_HOOK_CODE, boundary, begin=address, end=address)
        for failure in (None, 0, 1, 2):
            state['fail'] = failure
            converted.clear()
            oracle.call(0x653ce0, (0, 0xdead0000))
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), 0x1a5c798 if failure is None else 0)
            self.assertEqual(converted, [0, 1, 2] if failure is None else list(range(failure + 1)))
        for count in (0, 1, 2, 4):
            state['count'], state['error'] = count, False
            converted.clear()
            oracle.call(0x653ce0, (0, 0xdead0000))
            self.assertTrue(state['error'])
            self.assertEqual(converted, [])
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX), 0)


if __name__ == '__main__':
    unittest.main()
