"""Execute #1513 Vehicle owner assignment with the actual CPython 2.7 weakrefs.

The host compiles production owner accessors. Its PyEntity and other physics
properties are explicit engine boundaries; configuration/core tests remain
separate from this binding/lifetime and exact Vehicle bytecode regression.
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
#include <cstddef>
#include <new>
#include <utility>
#include <cstdint>
using EntityID=std::uint32_t;
class PyObjectPtr {
 PyObject* object_=NULL;
public:
 PyObjectPtr()=default;
 explicit PyObjectPtr(PyObject* value):object_(value){Py_XINCREF(object_);}
 PyObjectPtr(const PyObjectPtr& value):object_(value.object_){Py_XINCREF(object_);}
 PyObjectPtr(PyObjectPtr&&value):object_(value.object_){value.object_=NULL;}
 PyObjectPtr& operator=(PyObjectPtr value){std::swap(object_,value.object_);return *this;}
 ~PyObjectPtr(){Py_XDECREF(object_);}
 PyObject* get()const{return object_;}
};
struct PyEntity:PyObject {
 EntityID identity;bool destroyed;PyObject* weakrefs;
 static PyTypeObject s_type_;
 static bool Check(PyObject* value){return PyObject_TypeCheck(value,&s_type_);}
 EntityID entityID()const{assert(!destroyed);return identity;}
 bool isDestroyed()const{return destroyed;}
};
PyTypeObject PyEntity::s_type_={PyObject_HEAD_INIT(NULL) 0};
class PyWGVehiclePhysics:public PyObject {
public:
 PyObjectPtr owner_;EntityID ownerEntityID_;PyObject* weakrefs;
 const char* typeName()const{return Py_TYPE(this)->tp_name;}
 PyObject* pyGet_owner();int pySet_owner(PyObject*);
};
@PRODUCTION@
PyObject* ownerGet(PyObject* object,void*){return static_cast<PyWGVehiclePhysics*>(object)->pyGet_owner();}
int ownerSet(PyObject* object,PyObject* value,void*){return static_cast<PyWGVehiclePhysics*>(object)->pySet_owner(value);}
PyObject* identity(PyObject* object,void*){return PyInt_FromLong(static_cast<PyEntity*>(object)->identity);}
PyObject* entityNew(PyTypeObject* type,PyObject* args,PyObject*){
 unsigned id=0;if(!PyArg_ParseTuple(args,"|I",&id))return NULL;
 auto object=static_cast<PyEntity*>(type->tp_alloc(type,0));if(!object)return NULL;
 object->identity=id;object->destroyed=false;object->weakrefs=NULL;return object;
}
int entityInit(PyObject*,PyObject*,PyObject*){return 0;}
void entityDelete(PyObject* object){auto entity=static_cast<PyEntity*>(object);if(entity->weakrefs)PyObject_ClearWeakRefs(object);Py_TYPE(object)->tp_free(object);}
PyObject* physicsNew(PyTypeObject* type,PyObject*,PyObject*){
 auto object=static_cast<PyWGVehiclePhysics*>(type->tp_alloc(type,0));if(!object)return NULL;
 new(&object->owner_)PyObjectPtr();object->ownerEntityID_=0;object->weakrefs=NULL;return object;
}
void physicsDelete(PyObject* object){auto physics=static_cast<PyWGVehiclePhysics*>(object);if(physics->weakrefs)PyObject_ClearWeakRefs(object);physics->owner_.~PyObjectPtr();Py_TYPE(object)->tp_free(object);}
PyObject* snapshot(PyObject*,PyObject* object){
 auto physics=static_cast<PyWGVehiclePhysics*>(object);
 return Py_BuildValue("(OI)",physics->owner_.get()?physics->owner_.get():Py_None,physics->ownerEntityID_);
}
PyObject* destroyEntity(PyObject*,PyObject* object){static_cast<PyEntity*>(object)->destroyed=true;Py_RETURN_NONE;}
PyMethodDef methods[]={{"snapshot",snapshot,METH_O,NULL},{"destroyEntity",destroyEntity,METH_O,NULL},{NULL,NULL,0,NULL}};
PyGetSetDef ownerAttributes[]={{const_cast<char*>("owner"),ownerGet,ownerSet,NULL,NULL},{NULL,NULL,NULL,NULL,NULL}};
PyGetSetDef entityAttributes[]={{const_cast<char*>("id"),identity,NULL,NULL,NULL},{NULL,NULL,NULL,NULL,NULL}};
int main(){
 Py_Initialize();auto module=Py_InitModule("_physics_owner",methods);assert(module);
 auto& entity=PyEntity::s_type_;entity.tp_name="BigWorld.Entity";entity.tp_basicsize=sizeof(PyEntity);
 entity.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE;entity.tp_getset=entityAttributes;
 entity.tp_new=entityNew;entity.tp_init=entityInit;entity.tp_dealloc=entityDelete;entity.tp_weaklistoffset=offsetof(PyEntity,weakrefs);
 assert(!PyType_Ready(&entity));Py_INCREF(&entity);assert(!PyModule_AddObject(module,"Entity",reinterpret_cast<PyObject*>(&entity)));
 static PyTypeObject physics={PyObject_HEAD_INIT(NULL) 0};physics.tp_name="BigWorld.WGVehiclePhysics";
 physics.tp_basicsize=sizeof(PyWGVehiclePhysics);physics.tp_flags=Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE;
 physics.tp_new=physicsNew;physics.tp_dealloc=physicsDelete;physics.tp_getset=ownerAttributes;
 physics.tp_weaklistoffset=offsetof(PyWGVehiclePhysics,weakrefs);
 assert(!PyType_Ready(&physics));Py_INCREF(&physics);assert(!PyModule_AddObject(module,"Physics",reinterpret_cast<PyObject*>(&physics)));
 const char* program=R"PY(
import _physics_owner as native,weakref,gc,sys,os,types,marshal,zipfile
Entity=native.Entity
physics=native.Physics()
assert native.snapshot(physics)==(None,0)
try:physics.owner
except TypeError as error:assert 'write-only' in str(error)
else:raise AssertionError('owner is write-only')
first=Entity(12);second=Entity(34);ref=weakref.ref(first);refs=sys.getrefcount(first)
physics.owner=ref
assert native.snapshot(physics)==(ref,12) and sys.getrefcount(first)==refs
physics.owner=weakref.proxy(second)
assert native.snapshot(physics)[1]==34
physics.owner=None;assert native.snapshot(physics)==(None,34)
physics.owner=ref;del physics.owner;assert native.snapshot(physics)==(None,12)
# Retain the weakref object, without retaining the Entity referent.
physics.owner=ref;del first;gc.collect();assert ref() is None
assert native.snapshot(physics)==(ref,12)
class Other(object):pass
other=Other()
for invalid in [Entity(3),object(),1,weakref.ref(other),ref]:
 try:physics.owner=invalid
 except TypeError:pass
 else:raise AssertionError('invalid owner accepted')
 assert native.snapshot(physics)==(ref,12)
destroyed=Entity(56);native.destroyEntity(destroyed)
try:physics.owner=weakref.ref(destroyed)
except ValueError:pass
else:raise AssertionError('destroyed entity accepted')
assert native.snapshot(physics)==(ref,12)
# A weakref subclass finalizer can replace the owner while its previous
# reference is released. The outer setter must not restore a stale ID.
third=Entity(78);fourth=Entity(90);finalized=[]
class Reentrant(weakref.ref):
 def __del__(self):
  finalized.append(True)
  physics.owner=weakref.ref(fourth)
physics.owner=Reentrant(second)
physics.owner=weakref.ref(third)
assert finalized==[True] and native.snapshot(physics)[1]==90
# Clearing also publishes before releasing the previous weakref.
physics.owner=Reentrant(second);physics.owner=None
assert finalized==[True,True] and native.snapshot(physics)[1]==90
# The wrapper destructor releases its retained weakref, with no strong owner
# cycle and no dependence on a successful battle shutdown.
released=[]
class Tracked(weakref.ref):
 def __del__(self):released.append(True)
temporary=native.Physics();temporary.owner=Tracked(second);del temporary;gc.collect()
assert released==[True]
client=os.environ.get('WOT_0922_CLIENT')
if client:
 archive=zipfile.ZipFile(os.path.join(client,'res/packages/scripts.pkg'))
 root=marshal.loads(archive.read('scripts/client/Vehicle.pyc')[8:])
 klass=next(c for c in root.co_consts if isinstance(c,types.CodeType) and c.co_name=='Vehicle')
 code=next(c for c in klass.co_consts if isinstance(c,types.CodeType) and c.co_name=='__startWGPhysics')
 from vehicle_physics_producer import Vector3,Descriptor,load
 # Run the actual spring-layout producer, including mutable native vectors.
 Vector3.__setitem__=lambda self,index,value:setattr(self,('x','y','z')[index],value)
 shared,unused=load(client)
 descriptor=Descriptor(
  physics={'enginePower':300.,'weight':12000.},
  hull=Descriptor(hitTester=Descriptor(bbox=(Vector3(-1,0,-2),Vector3(1,1,2),None)),turretPositions=(Vector3(0,1,0),)),
  chassis=Descriptor(hitTester=Descriptor(bbox=(Vector3(-1,0,-2),Vector3(1,.5,2),None)),hullPosition=Vector3(0,.2,0)),
  turret=Descriptor(hitTester=Descriptor(bbox=(Vector3(-.5,0,-.5),Vector3(.5,.5,.5),None)),gunPosition=Vector3(0,.3,0)),
  gun=Descriptor(pitchLimits={'absolute':(-.2,.4)}),isPitchHullAimingAvailable=False,hasSiegeMode=False)
 constructed=[]
 class Physics(native.Physics):
  def __init__(self):self.springs=[];constructed.append(self)
  def removeAllDamperSprings(self):self.springs=[]
  def addDamperSpring(self,value):self.springs.append(value)
  def setArenaBounds(self,minimum,maximum):self.bounds=(minimum,maximum)
 class Filter(object):
  speedInfo=object()
  def setVehiclePhysics(self,value):self.physics=value
  def syncGunAngles(self,yaw,pitch):self.angles=(yaw,pitch)
 class Vehicle(Entity):pass
 class Speeds(object):
  def set(self,value):self.value=value
 vehicle=Vehicle(123);vehicle.typeDescriptor=descriptor;vehicle.filter=Filter();vehicle.gunAnglesPacked=0;vehicle._Vehicle__speedInfo=Speeds()
 BigWorld=types.ModuleType('BigWorld');BigWorld.WGVehiclePhysics=Physics
 BigWorld.player=lambda:Descriptor(arenaTypeID=(7<<16))
 ArenaType=Descriptor(getVisibilityMask=lambda value:(value+4))
 start=types.FunctionType(code,{'BigWorld':BigWorld,'physics_shared':shared,'weakref':weakref,'ArenaType':ArenaType,'decodeGunAngles':lambda packed,limits:(.1,.2),'__builtins__':__builtins__})
 start(vehicle)
 result=constructed[-1]
 assert native.snapshot(result)[0]() is vehicle and native.snapshot(result)[1]==vehicle.id
 assert result.bounds==((-10000,-10000),(10000,10000)) and result.staticMode is False
 assert result.movementSignals==0 and result.visibilityMask==11
 assert vehicle.filter.physics is result and vehicle.filter.angles==(.1,.2)
 assert vehicle._Vehicle__speedInfo.value is vehicle.filter.speedInfo and len(result.springs)==10
print('WGVehiclePhysics owner lifecycle and exact Vehicle startup consumer passed')
)PY";
 assert(!PyRun_SimpleString(program));Py_Finalize();
}
'''


class StockVehiclePhysicsOwnerTest(unittest.TestCase):
    def test_actual_python_weakrefs_and_original_vehicle_startup(self):
        python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists():
            self.skipTest('Pinned CPython 2.7 development files required')
        source = (ROOT / 'bigworld/vehicle_physics_binding.cpp').read_text()
        methods = '\n'.join(block(source, signature) for signature in (
            'PyObject * PyWGVehiclePhysics::pyGet_owner()',
            'int PyWGVehiclePhysics::pySet_owner('))
        with tempfile.TemporaryDirectory(prefix='stock-physics-owner-') as directory:
            directory = Path(directory)
            cpp,binary = directory / 'host.cpp',directory / 'host'
            cpp.write_text(HOST.replace('@PRODUCTION@',methods))
            flags = [value for value in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True)) if value!='-DNDEBUG']
            libs = shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
            subprocess.run(['c++','-std=c++14','-UNDEBUG','-Wno-deprecated-register','-Wno-invalid-offsetof',
                            *flags,str(cpp),*libs,'-o',str(binary)],check=True)
            environment=dict(os.environ,PYTHONPATH=str(ROOT/'tests'))
            subprocess.run([str(binary)],env=environment,check=True)

    def test_original_owner_set_clear_and_write_only_instructions(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required for original instructions')
        try:
            from stock_tree_lod_oracle import OriginalTreeInstructionOracle
            from unicorn import UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP
        except ImportError:
            self.skipTest('pefile and Unicorn required for original instructions')
        oracle=OriginalTreeInstructionOracle(Path(client)/'WorldOfTanks.exe')
        emulator=oracle.emulator
        owner=oracle.allocate(0x960)
        emulator.mem_write(owner,bytes(0x960))
        emulator.mem_write(owner+4,struct.pack('<I',0x1a5b3a8))
        refs=[]
        for index in range(2):
            entity=oracle.allocate(16);native=oracle.allocate(40);reference=oracle.allocate(32)
            emulator.mem_write(entity,struct.pack('<4I',100,0x1a08ea0,native,0))
            emulator.mem_write(native+0x1c,struct.pack('<I',12+index*22))
            emulator.mem_write(reference,struct.pack('<3I',100,0x1a64528,entity))
            refs.append(reference)
        state={'error':False}

        def boundary(uc,address,size,context):
            stack=uc.reg_read(UC_X86_REG_ESP)
            if address==0xfbf700:
                result=int(oracle.word(stack+4)==oracle.word(stack+8))
            else:
                self.assertEqual(address,0xfc3720)
                self.assertEqual(oracle.word(stack+8),0x174a744)
                state['error']=True;result=0
            uc.reg_write(UC_X86_REG_EAX,result)
            uc.reg_write(UC_X86_REG_EIP,oracle.word(stack))
            uc.reg_write(UC_X86_REG_ESP,stack+4)
        for address in (0xfbf700,0xfc3720):
            emulator.hook_add(UC_HOOK_CODE,boundary,begin=address,end=address)
        for index,reference in enumerate(refs):
            oracle.call(0xf556b0,(owner,reference,0))
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX),0)
            self.assertEqual(oracle.word(owner+0x6e0),reference)
            self.assertEqual(oracle.word(owner+0x898),12+index*22)
            self.assertEqual(oracle.word(reference),101)
        self.assertEqual(oracle.word(refs[0]),100)
        for value in (0x1a5c798,0):
            oracle.call(0xf556b0,(owner,value,0))
            self.assertEqual(emulator.reg_read(UC_X86_REG_EAX),0)
            self.assertEqual(oracle.word(owner+0x6e0),0)
            self.assertEqual(oracle.word(owner+0x898),34)
        self.assertEqual(oracle.word(refs[1]),100)
        oracle.call(0xf7ca60,(owner,0))
        self.assertTrue(state['error'])
        self.assertEqual(emulator.reg_read(UC_X86_REG_EAX),0)


if __name__=='__main__':unittest.main()
