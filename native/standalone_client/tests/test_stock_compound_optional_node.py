"""Compare optional compound-node lookup with #1513 and its sticker consumer."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_vehicle_compound_nodes import block

ROOT = Path(__file__).resolve().parents[1]


def lookup_host(body):
    return r'''
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace BW {using string=std::string;}
struct PyObject {int refs=0;virtual ~PyObject()=default;};
struct Provider {};
using MatrixProviderPtr=std::shared_ptr<Provider>;
struct Node {MatrixProviderPtr source;};
struct CompoundState {bool active=true;std::vector<Node> nodes;
 std::map<BW::string,size_t> names;MatrixProviderPtr matrix;};
using CompoundStatePtr=std::shared_ptr<CompoundState>;
struct PyCompoundNode:PyObject {
 PyCompoundNode(const CompoundStatePtr&,size_t){};
};
struct PyObjectPtr:std::shared_ptr<PyObject> {
 enum ReferenceType {STEAL_REFERENCE};
 PyObjectPtr()=default;
 template<class T> PyObjectPtr(T*value,ReferenceType):std::shared_ptr<PyObject>(value){}
};
PyObject none;int error=0;int PyExc_ValueError=1;
#define Py_RETURN_NONE return &none
#define Py_INCREF(value) (++(value)->refs)
void PyErr_SetString(int value,const char*){error=value;}
void PyErr_Format(int value,const char*,const char*){error=value;}
struct Owner {
 CompoundStatePtr state_=std::make_shared<CompoundState>();
 std::map<size_t,PyObjectPtr> handles_;
 Owner(){state_->nodes.resize(2);state_->names["V"]=1;}
 @BODY@
};
// The bridge calls the actual production method, preserving a strict error
// result instead of making every missing lookup succeed in the Python fake.
extern "C" int lookup(const char*name,int active){
 Owner owner;owner.state_->active=active;error=0;
 PyObject*result=owner.node(name,MatrixProviderPtr());
 return error?-1:result==&none?0:1;
}
#ifndef BUILD_SHARED_BRIDGE
int main(){
 Owner owner;auto provider=std::make_shared<Provider>();
 for(const char*name:{"Gun","GUN","absent","HP_optional"}){
  error=0;assert(owner.node(name,provider)==&none);assert(!error);
  assert(owner.handles_.empty());assert(!owner.state_->matrix);
  for(const Node&node:owner.state_->nodes)assert(!node.source);
 }
 auto root=owner.node("",MatrixProviderPtr());
 assert(root&&root!=&none);assert(owner.node("",provider)==root);
 assert(owner.state_->matrix==provider);
 auto bone=owner.node("V",provider);assert(bone&&bone!=root&&bone!=&none);
 assert(owner.state_->nodes[1].source==provider);
 assert(owner.node("V",MatrixProviderPtr())==bone);
 assert(owner.state_->nodes[1].source==provider&&owner.handles_.size()==2);
 owner.state_->active=false;error=0;
 assert(owner.node("V",provider)==nullptr&&error==PyExc_ValueError);
 error=0;assert(owner.node("absent",provider)==nullptr&&error==PyExc_ValueError);
 assert(owner.handles_.size()==2);
}
#endif
'''.replace('@BODY@', body)


STOCK_STICKERS = r'''
from __future__ import print_function
import ctypes,marshal,os,sys,types,zipfile
library=ctypes.CDLL(sys.argv[1]);library.lookup.argtypes=[ctypes.c_char_p,ctypes.c_int]
archive=zipfile.ZipFile(os.path.join(os.environ['WOT_0922_CLIENT'],'res/packages/scripts.pkg'))
def code(path):return marshal.loads(archive.read('scripts/client/'+path+'.pyc')[8:])
namespace={'__name__':'stock_optional_node_probe','__builtins__':__builtins__}
def load_class(module,name):
 body=next(c for c in module.co_consts if isinstance(c,types.CodeType) and c.co_name==name)
 result=type(name,(object,),types.FunctionType(body,namespace)());namespace[name]=result
 return result
structure=code('vehicle_systems/tankStructure')
for name in ('TankPartNames','DetachedTurretPartNames','DetachedTurretPartIndexes','TankPartIndexes','TankNodeNames'):
 load_class(structure,name)
sticker=load_class(code('VehicleStickers'),'VehicleStickers')
assert ('gun','Gun') in sticker.COMPONENT_NAMES
class Record(object):
 def __init__(self):self.calls=[]
 def attachStickers(self,*args):self.calls.append(args)
class Entry(object):
 def __init__(self):self.stickers=Record();self.damageStickers={}
class Matrix(object):
 def invert(self):pass
 def preMultiply(self,other):pass
Math=types.ModuleType('Math')
Math.Matrix=lambda node:Matrix()
namespace['Math']=Math
MathUtils=types.ModuleType('mathUtils')
MathUtils.createIdentityMatrix=lambda:Matrix()
namespace['mathUtils']=MathUtils
class Compound(object):
 def __init__(self,missing):self.missing=set(missing);self.lookups=[];self.geometry=[]
 def node(self,name):
  self.lookups.append(name)
  # Existing nodes use the same production path; optional authored names use
  # its real missing-name branch. Conversion/retired-owner errors stay errors.
  result=library.lookup(name if name in self.missing else 'V',1)
  if result<0:raise ValueError('CompoundModel has been reset')
  return None if not result else object()
 def getPartGeometryLink(self,index):self.geometry.append(index);return index
def run(missing,damaged,animated,detached):
 instance=object.__new__(sticker)
 instance._VehicleStickers__animateGunInsignia=animated
 instance._VehicleStickers__stickers=dict((name,Entry()) for name,node in sticker.COMPONENT_NAMES)
 instance._VehicleStickers__stickers['gunInsignia']=Entry()
 compound=Compound(missing)
 instance.attach(compound,damaged,False,detached)
 return compound,instance._VehicleStickers__stickers
for detached in (False,True):
 compound,entries=run(('Gun',),True,False,detached)
 assert len(entries['gun'].stickers.calls)==0
 assert len(entries['gunInsignia'].stickers.calls)==1
 compound,entries=run(('G',),False,True,detached)
 assert len(entries['gun'].stickers.calls)==1
 assert len(entries['gunInsignia'].stickers.calls)==0
 compound,entries=run((namespace['TankNodeNames'].GUN_INCLINATION,),False,False,detached)
 assert len(entries['gun'].stickers.calls)==0
 assert len(entries['gunInsignia'].stickers.calls)==0
 compound,entries=run((),False,False,detached)
 assert len(entries['gun'].stickers.calls)==1
 assert len(entries['gunInsignia'].stickers.calls)==1
print('Original VehicleStickers.attach optional-node guards and existing geometry passed')
'''


class OptionalCompoundNodeTests(unittest.TestCase):
    def test_production_optional_nodes_and_retired_owner(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable: c++')
        body = block((ROOT / 'bigworld/vehicle_appearance.cpp').read_text(),
                     'PyObject * node( const BW::string & name, MatrixProviderPtr source )')
        with tempfile.TemporaryDirectory(prefix='compound-optional-node-') as temporary:
            folder = Path(temporary)
            source = folder / 'probe.cpp'
            source.write_text(lookup_host(body))
            command = [compiler, *native_test_compile_flags('c++'), '-std=c++17',
                       '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            subprocess.run(command + [str(source), '-o', str(folder / 'probe')], check=True)
            subprocess.run([str(folder / 'probe')], check=True)

    def test_exact_sticker_consumer_with_production_node(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT to the exact #1513 client')
        self.assertTrue((Path(client) / 'res/packages/scripts.pkg').is_file())
        python = os.environ.get('WOT_PYTHON27')
        if not python:
            self.skipTest('Set WOT_PYTHON27 to CPython 2.7.18')
        self.assertTrue(Path(python).is_file())
        version = subprocess.check_output([python, '-c', 'import sys;print(sys.version_info[:3])'], text=True)
        self.assertEqual(version.strip(), '(2, 7, 18)')
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable: c++')
        body = block((ROOT / 'bigworld/vehicle_appearance.cpp').read_text(),
                     'PyObject * node( const BW::string & name, MatrixProviderPtr source )')
        with tempfile.TemporaryDirectory(prefix='compound-optional-pyc-') as temporary:
            folder = Path(temporary)
            source = folder / 'probe.cpp'
            source.write_text(lookup_host(body))
            # This bridge is dlopened by an uninstrumented pinned interpreter;
            # the separate executable test above carries sanitizer coverage.
            command = [compiler, *native_test_compile_flags('c++', sanitise=False), '-std=c++17',
                       '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            library = folder / ('probe.dylib' if sys.platform == 'darwin' else 'probe.so')
            subprocess.run(command + ['-shared', '-fPIC', '-DBUILD_SHARED_BRIDGE', str(source),
                                      '-o', str(library)], check=True)
            script = folder / 'stock_stickers.py'
            script.write_text(STOCK_STICKERS)
            subprocess.run([python, str(script), str(library)], check=True)

    def test_exact_native_missing_lookup_and_none_conversion(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT to the exact #1513 client')
        executable = Path(client) / 'WorldOfTanks.exe'
        self.assertTrue(executable.is_file())
        for dependency in ('pefile', 'unicorn'):
            if importlib.util.find_spec(dependency) is None:
                self.skipTest('Optional original-x86 oracle dependency unavailable: ' + dependency)
        import struct
        import unicorn
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX
        from stock_flash_input_mode_oracle import OriginalFlashInputMode, OBJECT, VALUE, MARKS
        oracle = OriginalFlashInputMode(executable)
        owner = MARKS + 0x2000
        name = MARKS + 0x3000
        provider = MARKS + 0x3100
        lookups = []
        oracle.hooks[0x8e0570] = lambda: oracle._return(owner)
        def absent():
            lookups.append(oracle._arg(1))
            oracle._return(0xffffffff, 4)
        oracle.hooks[0x8e2560] = absent
        for address in (0x8e0570, 0x8e2560):
            oracle.u.hook_add(unicorn.UC_HOOK_CODE, oracle._hook, begin=address, end=address)
        for text in (b'Gun', b'G', b'HP_optional'):
            oracle.u.mem_write(name, text + b'\0' * (16-len(text)) + struct.pack('<II', len(text), 15))
            oracle.u.mem_write(VALUE, struct.pack('<I', 0xdeadbeef))
            self.assertEqual(oracle._call(0x8dd500, OBJECT, VALUE, name, provider), VALUE)
            self.assertEqual(bytes(oracle.u.mem_read(VALUE, 4)), b'\0' * 4)
            # Execute the original Script::getData smart-pointer converter too.
            before = struct.unpack('<I', oracle.u.mem_read(0x1a5c798, 4))[0]
            self.assertEqual(oracle._call(0x67f120, 0, VALUE), 0x1a5c798)
            after = struct.unpack('<I', oracle.u.mem_read(0x1a5c798, 4))[0]
            self.assertEqual(after, before + 1)
        self.assertEqual(lookups, [name] * 3)


if __name__ == '__main__':
    unittest.main()
