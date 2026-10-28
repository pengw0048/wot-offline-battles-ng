"""Original compiled model state laws, distinct from stock Python effects."""
from pathlib import Path
import random
import os
import shlex
import shutil
from native_test_compile_flags import native_test_compile_flags
from test_stock_screener import engine_sources,generated_source,between
import struct
import subprocess
import tempfile
import unittest
from test_stock_ground_decals import ROOT,compiler_command,exact_client


def build_binding(test,directory):
    if not shutil.which('c++'):test.skipTest('C++ compiler unavailable: c++')
    engine=engine_sources(test);source=generated_source(engine)
    source=source.replace('PyObjectPtr() : object_(NULL) {}','PyObjectPtr() : object_(NULL) {}\n PyObjectPtr(const PyObjectPtr& o):object_(o.object_){Py_XINCREF(object_);}')
    source=source.replace('struct PyModuleMethodLink\n{', 'struct HostMethodLink {const char* module;const char* name;PyCFunction function;};\nstd::vector<HostMethodLink>& methodLinks(){static std::vector<HostMethodLink> value;return value;}\nstruct PyModuleMethodLink\n{')
    source=source.replace('PyModuleMethodLink(const char *, const char *, PyCFunction, const char *) {}', 'PyModuleMethodLink(const char* m,const char* n,PyCFunction f,const char*){methodLinks().push_back({m,n,f});}')
    boundary=(ROOT/'tests/stock_destructibles_binding_host.hpp').read_text()
    sdk=Path(os.environ.get('WOT_BIGWORLD_ROOT','/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
    manager=sdk/'lib/space/space_manager.hpp'
    if not manager.is_file():test.skipTest('Pinned SDK SpaceManager declaration required')
    header=manager.read_text();header=header[header.index('class SPACE_API SpaceManager'):header.index('BW_END_NAMESPACE')].replace('SPACE_API ','')
    boundary=boundary.replace('@SPACE_MANAGER@',header)
    boundary=boundary.replace('@INT_SET_DATA@','namespace BW {\n'+between(engine['lib/pyscript/script.cpp'],'int Script::setData( PyObject * pObject, int & rInt,','\n}\n')+'}\n')
    for tag,filename in (('@DESTRUCTIBLES_HEADER@','stock_destructibles.hpp'),('@DESTRUCTIBLES_SOURCE@','stock_destructibles.cpp')):
        text=(ROOT/'bigworld'/filename).read_text()
        text='\n'.join(line for line in text.splitlines()if not line.startswith('#include')and line!='#pragma once')
        boundary=boundary.replace(tag,text)
    source=source.replace('#include "stock_screener.cpp"',boundary)
    begin=source.index('static PyObject * hostState(');end=source.index('// Runs every recorded',begin)
    source=source[:begin]+'static PyObject* hostState(PyObject*,PyObject*){Py_RETURN_NONE;}\n'+source[end:]
    source=source.replace('return PyInt_FromSize_t(BW::hostResultLinks().size());','''
 for(const auto& link:BW::methodLinks()){
  PyObject* module=PyImport_AddModule(link.module);PyMethodDef definition={link.name,link.function,METH_VARARGS,NULL};
  // CPython retains the method-definition address for the life of the function.
  static std::vector<std::unique_ptr<PyMethodDef>> definitions;definitions.emplace_back(new PyMethodDef(definition));
  PyObject* fn=PyCFunction_New(definitions.back().get(),NULL);
  if(!fn||!module||PyObject_SetAttrString(module,link.name,fn)<0){Py_XDECREF(fn);return NULL;}Py_DECREF(fn);
 }
 return PyInt_FromSize_t(BW::methodLinks().size());
''')
    source=source.replace('static PyMethodDef hostMethods[] = {','''static PyMethodDef hostMethods[] = {
 {"prepare",prepare,METH_VARARGS,NULL},{"bind",bind,METH_NOARGS,NULL},{"unload",unload,METH_NOARGS,NULL},
 {"calls",calls,METH_NOARGS,NULL},{"provider",provider,METH_VARARGS,NULL},{"fashion",fashion,METH_NOARGS,NULL},{"child",child,METH_NOARGS,NULL},''')
    source=source.replace('"_screener_host"','"_destructibles_host"').replace('Py_Finalize();','Py_Finalize();BW::stop();')
    config=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7')).with_name('python2.7-config')
    if not config.is_file():test.skipTest('CPython 2.7 development files required')
    flags=[v for v in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))if v!='-DNDEBUG']
    libraries=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
    (directory/'binding.cpp').write_text(source);binary=directory/'binding'
    subprocess.run(['c++',*native_test_compile_flags('c++',sanitise=False),'-std=c++14','-UNDEBUG',*flags,
        '-Wall','-Werror','-Wno-deprecated-declarations','-Wno-deprecated-register','-Wno-writable-strings','-Wno-unused-function',
        '-I'+str(ROOT/'bigworld'),str(directory/'binding.cpp'),*libraries,'-o',str(binary)],check=True)
    return binary


class StockDestructiblesTests(unittest.TestCase):
    def test_complete_binding_and_wgde_callback_lifecycle(self):
        client=exact_client(self)
        with tempfile.TemporaryDirectory(prefix='stock-destructibles-binding-')as name:
            binary=build_binding(self,Path(name))
            subprocess.run([str(binary),str(ROOT/'tests/stock_destructibles_consumer.py'),str(Path(client)/'res/packages/scripts.pkg')],check=True)

    def test_production_tree_metadata_reads_live_owner_and_masked_slots(self):
        source=(ROOT/'bigworld/stock_tree_scene.cpp').read_text()
        begin=source.index('bool StockTreeSceneHandler::destructibleSnapshot(')
        end=source.index('void StockTreeSceneHandler::worldCollisionBounds(',begin)
        header=(ROOT/'bigworld/stock_destructibles.hpp').read_text()
        snapshot=header[header.index('struct StockDestructibleSnapshot'):header.index('class StockDestructibleObjects')]
        prelude=r"""
#include <array>
#include <cassert>
#include <memory>
#include <string>
#include <vector>
namespace BW {using string=std::string;struct Matrix {float x=0;static const Matrix identity;void setZero(){x=-1;}};const Matrix Matrix::identity{};
@SNAPSHOT@
struct Geometry {float minimumBounds[3]={-1,-3,-1},maximumBounds[3]={1,7,1};};
struct Type {Geometry geometry;};struct Instance {struct Definition {Matrix world;}definition;BW::string filename;std::shared_ptr<Type> type;};
class StockTreeSceneHandler {public:struct Implementation {bool loaded=true;std::vector<Instance> instances;};
 std::unique_ptr<Implementation> implementation_{new Implementation};bool destructibleSnapshot(size_t,StockDestructibleSnapshot&)const;};
@METHOD@
}
int main(){BW::StockTreeSceneHandler handler;handler.implementation_->instances.resize(2);
 auto& live=handler.implementation_->instances[0];live.type=std::make_shared<BW::Type>();live.filename="oak.spt";live.definition.world.x=23;
 auto& masked=handler.implementation_->instances[1];masked.filename="masked.spt";masked.definition.world.x=71;
 BW::StockDestructibleSnapshot out;assert(handler.destructibleSnapshot(0,out));assert(out.filename=="oak.spt"&&out.world.x==23&&out.hardpointWorld.x==0);
 assert(out.height==10&&out.category==0&&out.effectScale==1&&out.fallingParameters.x==0);for(const auto& e:out.effects)assert(e.empty());
 assert(handler.destructibleSnapshot(1,out)&&out.filename=="masked.spt"&&out.world.x==71&&out.height==0);
 live.type->geometry.minimumBounds[1]=9;assert(handler.destructibleSnapshot(0,out)&&out.height==0);
 assert(!handler.destructibleSnapshot(2,out));handler.implementation_->loaded=false;assert(!handler.destructibleSnapshot(0,out));
}
"""
        with tempfile.TemporaryDirectory(prefix='stock-destructibles-tree-metadata-')as name:
            path=Path(name)/'probe.cpp';binary=Path(name)/'probe'
            path.write_text(prelude.replace('@SNAPSHOT@',snapshot).replace('@METHOD@',source[begin:end]))
            subprocess.run(compiler_command(self)+[str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_metadata_selectors_and_packed_falling_matrix_match_original(self):
        client=exact_client(self)
        from stock_destructibles_oracle import OriginalStaticDestructibles
        original=OriginalStaticDestructibles(Path(client)/'WorldOfTanks.exe')
        rng=random.Random(1513);encoded=bytearray();expected=bytearray();count=0
        for kind in (0,1,2):
            for entry in (0,1,0x100,0x101):
                for effect in ('lifetimeEffect','fractureEffect','touchdownEffect','decayEffect','effect','ramEffect','hitEffect','none','HitEffect'):
                    for absent in (False,True):
                        words=[rng.randrange(1,16)for unused in range(11)];words[3]=0 if absent else words[3]
                        if kind==2:words[8]=entry
                        raw=effect.encode();encoded.extend(struct.pack('<13I',kind,entry,*words));encoded.extend(struct.pack('<I',len(raw))+raw)
                        category,key,scale,physics=original.metadata(kind,entry,words,effect)
                        expected.extend(struct.pack('<3I',category,key,scale)+physics);count+=1
        with tempfile.TemporaryDirectory(prefix='stock-destructibles-metadata-')as name:
            binary=Path(name)/'probe'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'include'),str(ROOT/'tests/stock_destructibles_metadata_probe.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=encoded)
        self.assertEqual(actual,expected)
        print('Original compiled metadata/category/effect/scale/packed Matrix matched %d cases'%count)

    def test_compiled_fragile_and_module_geometry_transitions_match_original(self):
        client=exact_client(self)
        from stock_destructibles_oracle import OriginalStaticDestructibles
        original=OriginalStaticDestructibles(Path(client)/'WorldOfTanks.exe')
        rng=random.Random(3113);encoded=bytearray();expected=bytearray();cases=[]
        for unused in range(1024):
            count=rng.randrange(4,15);index=rng.randrange(1,count)
            activity=[rng.randrange(2)for i in range(count)]
            models=[rng.randrange(2)for i in range(count)];models[index]=rng.randrange(2)
            info=[(2,0),(0,0)];fragile=[[0]*7+[rng.choice((1,0xffffffff)),rng.randrange(2)]]
            auxiliary=[rng.randrange(count)for i in range(rng.randrange(count+1))]
            triples=[(0xffffffff,0xffffffff,0xffffffff)for i in range(count)]
            if rng.randrange(2):
                triples[0]=(1,count-1,0xffffffff)
                triples[index]=(0,len(auxiliary)-1 if auxiliary else 0xffffffff,0)
                if not auxiliary:triples[index]=(0xffffffff,0xffffffff,0)
            else:
                triples[index]=(0,len(auxiliary)-1,0xffffffff)if auxiliary else triples[index]
            cases.append((index,activity,models,info,fragile,triples,auxiliary))
            encoded.extend(struct.pack('<5I',count,index,len(info),len(fragile),len(auxiliary)))
            encoded.extend(struct.pack('<%dI'%count,*activity));encoded.extend(struct.pack('<%dI'%count,*models))
            for value in info:encoded.extend(struct.pack('<2I',*value))
            for value in fragile:encoded.extend(struct.pack('<9I',*value))
            for value in triples:encoded.extend(struct.pack('<3I',*value))
            encoded.extend(struct.pack('<%dI'%len(auxiliary),*auxiliary))
            result=original.destroy_core(*cases[-1])
            expected.extend(struct.pack('<%dI'%count,*result['activity']));expected.extend(struct.pack('<%dI'%count,*result['models']))
            for values in (result['added'],result['removed'],[event[1][0]for event in result['events']if event[0]=='bounds']):
                expected.extend(struct.pack('<I',len(values)));expected.extend(struct.pack('<%dI'%len(values),*values))
        with tempfile.TemporaryDirectory(prefix='stock-destructible-transitions-')as name:
            directory=Path(name);binary=directory/'probe'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'include'),
                str(ROOT/'tests/stock_destructibles_probe.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=encoded)
        self.assertEqual(actual,expected)
        print('Original 6c1300 selection/flag core: %d merged/replacement/debris cases exact'%len(cases))


if __name__=='__main__':unittest.main()
