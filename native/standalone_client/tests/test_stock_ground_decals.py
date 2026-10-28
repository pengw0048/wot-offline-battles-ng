"""Ground-decal geometry against the original #1513 contact manifold."""
from pathlib import Path
import os
import random
import struct
import subprocess
import sys
import tempfile
import shlex
import tarfile
import itertools
import re
import shutil
import importlib.util
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))


def compiler_command(test):
    compiler=shutil.which(os.environ.get('CXX','c++'))
    if not compiler:test.skipTest('Optional C++ compiler is unavailable')
    flags=['-std=c++14','-ffp-contract=off','-UNDEBUG','-Wall','-Wextra','-Werror']
    from native_test_compile_flags import native_test_compile_flags
    flags+=native_test_compile_flags(compiler)
    return [compiler]+flags


def exact_client(test):
    client=os.environ.get('WOT_0922_CLIENT')
    if not client:test.skipTest('Exact #1513 executable required')
    for dependency in ('pefile','unicorn'):
        if importlib.util.find_spec(dependency)is None:test.skipTest('Optional original-instruction dependency unavailable: '+dependency)
    return client


class GroundDecalsTests(unittest.TestCase):
    def test_original_prepare_uses_static_collision_qualification(self):
        client=exact_client(self)
        from stock_ground_decals_oracle import prepare_contract
        calls,eligibility=prepare_contract(Path(client)/'WorldOfTanks.exe')
        self.assertEqual([(a,f)for a,f,b in calls],[(0xaba3a0,0x2a),(0xaba800,0x2a)])
        self.assertEqual([b for a,f,b in calls],[struct.pack('<6f',-1.,-2.,-3.,4.,5.,6.)]*2)
        self.assertEqual([bool(value&0x400)for value in eligibility],[bool(flags&0x20)for flags in range(256)])

    def test_original_wrapper_is_strict_eight_arguments_and_ordered_types(self):
        client=exact_client(self)
        from stock_ground_decals_oracle import wrapper_contract
        rows=wrapper_contract(Path(client)/'WorldOfTanks.exe')
        self.assertEqual(len(rows),17)
        kinds=['BW::string','Vector3','Vector3','Vector2','float','int','int','int']
        for argc,fail,calls in rows:
            if fail is None:self.assertEqual(calls[0][:4],['argCountError','wg_addDecal',8,8])
            else:self.assertEqual([call[1]for call in calls],kinds[:fail+1])

    def test_actual_dynamic_submission_preserves_particle_simple_branch(self):
        from test_stock_dynamic_decals_owner import method
        source=(ROOT/'tests/stock_ground_decals_dynamic_host.cpp').read_text().replace('@PRODUCTION@',method(
            (ROOT/'bigworld/stock_dynamic_decals.cpp').read_text(),'bool addStockDynamicDecal('))
        sys.path.insert(0,str(ROOT/'bigworld'))
        import patch_stock_ground_decals as patcher
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():self.skipTest('Pinned SDK required')
        with tarfile.open(archive)as tar:
            prefix=tar.getmembers()[0].name.split('/')[0]+'/programming/bigworld/'
            header=tar.extractfile(prefix+'lib/moo/dynamic_decal.hpp').read().decode('utf-8-sig')
            implementation=tar.extractfile(prefix+'lib/moo/dynamic_decal.cpp').read().decode('utf-8-sig')
        header=header.replace('\tDynamicDecalManager();',patcher.DYNAMIC_MANAGER_GETTER+'\tDynamicDecalManager();')
        header='\n'.join(line for line in header.splitlines()if not line.startswith(('#include','#pragma once')))
        source=source.replace('@DYNAMIC_MANAGER@',header).replace('@DYNAMIC_CONSTRUCTOR@',method(implementation,'DynamicDecalManager::DynamicDecalManager()'))
        with tempfile.TemporaryDirectory(prefix='ground-decal-submission-')as name:
            directory=Path(name);(directory/'host.cpp').write_text(source);binary=directory/'host'
            # The unchanged SDK DecalGroup constructor lists names before lifetime.
            subprocess.run(compiler_command(self)+['-Wno-reorder','-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'include'),str(directory/'host.cpp'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_actual_bsp_method_preserves_own_shared_and_collision_flag_filter(self):
        sys.path.insert(0,str(ROOT/'bigworld'))
        import patch_stock_ground_decals as patcher
        source=(ROOT/'tests/stock_ground_decals_bsp_owner_host.cpp').read_text().replace('@BSP@',patcher.BSP_METHOD)
        with tempfile.TemporaryDirectory(prefix='ground-decal-bsp-owner-')as name:
            directory=Path(name);(directory/'host.cpp').write_text(source);binary=directory/'host'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),str(directory/'host.cpp'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_complete_query_and_actual_terrain_collector(self):
        from test_stock_dynamic_decals_owner import method
        sys.path.insert(0,str(ROOT/'bigworld'))
        import patch_stock_ground_decals as patcher
        source=(ROOT/'tests/stock_ground_decals_owner_host.cpp').read_text()
        source=source.replace('@TERRAIN@',patcher.TERRAIN_METHOD).replace('@QUERY@',method(
            (ROOT/'bigworld/stock_ground_decals.cpp').read_text(),'bool queryStockGroundDecalProjection('))
        with tempfile.TemporaryDirectory(prefix='ground-decal-owner-')as name:
            directory=Path(name);(directory/'host.cpp').write_text(source);binary=directory/'host'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),str(directory/'host.cpp'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_sdk_broadphase_insertion_and_collision_query_uses_actual_bounds(self):
        from test_stock_dynamic_decals_owner import method
        sys.path.insert(0,str(ROOT/'bigworld'))
        import patch_stock_ground_decals as ground
        names=('lib/physics2/bsp.hpp','lib/physics2/bsp.cpp','lib/compiled_space/terrain2_scene_provider.hpp',
               'lib/compiled_space/terrain2_scene_provider.cpp','lib/scene/scene_intersect_context.hpp',
               'lib/scene/scene_intersect_context.cpp','lib/space/dynamic_scene_provider.cpp','lib/moo/dynamic_decal.hpp')
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():self.skipTest('Pinned SDK required')
        with tempfile.TemporaryDirectory(prefix='ground-decal-broadphase-')as name,tarfile.open(archive)as tar:
            directory=Path(name);prefix=tar.getmembers()[0].name.split('/')[0]+'/programming/bigworld/'
            for n in names:
                target=directory/n;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(prefix+n).read())
            ground.patch(directory)
            source=(ROOT/'tests/stock_ground_decals_broadphase_host.cpp').read_text()
            strip=lambda text:'\n'.join(line for line in text.splitlines()if not line.startswith('#include'))
            source=source.replace('@CONTEXT_HEADER@',strip((directory/names[4]).read_text()))
            source=source.replace('@CONTEXT_SOURCE@',strip((directory/names[5]).read_text()))
            dynamic=(directory/names[6]).read_text();terrain=(directory/names[3]).read_text()
            node=method(dynamic,'static void insertObjectNode(').replace('static void insertObjectNode(', 'void DynamicSceneProvider::Detail::insertObjectNode(',1)
            source=source.replace('@INSERT_NODE@',node).replace('@INSERT_OBJECT@','DynamicObjectHandle '+method(dynamic,'DynamicSceneProvider::insertObject_internal('))
            source=source.replace('@ADD_OBJECT@',method(dynamic,'DynamicObjectHandle DynamicSceneProvider::addObject('))
            source=source.replace('@DYNAMIC_INTERSECT@',method(dynamic,'size_t DynamicSceneProvider::intersect('))
            source=source.replace('@TERRAIN_INTERSECT@',method(terrain,'size_t Terrain2SceneProvider::intersect('))
            (directory/'host.cpp').write_text(source);binary=directory/'host'
            # The unchanged SDK constructor lists scene_ before flags_.
            subprocess.run(compiler_command(self)+['-Wno-reorder',str(directory/'host.cpp'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_collector_patches_compose_atomically_and_are_idempotent(self):
        sys.path.insert(0,str(ROOT/'bigworld'))
        import patch_stock_ground_decals as ground
        import patch_stock_space_terrain as terrain
        names={'lib/physics2/bsp.hpp','lib/physics2/bsp.cpp','lib/compiled_space/terrain2_scene_provider.hpp',
               'lib/compiled_space/terrain2_scene_provider.cpp','lib/compiled_space/terrain2_types.hpp',
               'lib/scene/scene_intersect_context.hpp','lib/scene/scene_intersect_context.cpp','lib/space/dynamic_scene_provider.cpp','lib/moo/dynamic_decal.hpp'}
        names.update(re.findall(r"['\"](lib/[^'\"]+\.(?:cpp|hpp))['\"]",(ROOT/'bigworld/patch_stock_space_terrain.py').read_text()))
        archive=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.exists():self.skipTest('Pinned SDK required')
        with tarfile.open(archive)as tar:
            prefix=tar.getmembers()[0].name.split('/')[0]+'/programming/bigworld/'
            available=set(tar.getnames())
            files={n:tar.extractfile(prefix+n).read()for n in names if prefix+n in available}
        for order in itertools.permutations((ground,terrain)):
            with tempfile.TemporaryDirectory(prefix='ground-decal-patch-')as name:
                root=Path(name)
                for n,data in files.items():(root/n).parent.mkdir(parents=True,exist_ok=True);(root/n).write_bytes(data)
                for patch in order:patch.patch(root)
                def snapshot():return {str(p.relative_to(root)):p.read_bytes()for p in root.rglob('*')if p.is_file()}
                before=snapshot()
                for patch in order:patch.patch(root)
                self.assertEqual(before,snapshot())
                header=root/'lib/physics2/bsp.hpp';header.write_text(header.read_text().replace('visitStockDecalBox','unknownBox').replace('void remapFlags( BSPFlagsMap& flagsMap );','void drift();'))
                before=snapshot()
                with self.assertRaises(ValueError):ground.patch(root)
                self.assertEqual(before,snapshot())

    def test_real_python27_module_wrapper_and_error_state(self):
        from test_stock_screener import engine_sources,generated_source,between
        engine=engine_sources(self);source=generated_source(engine)
        cpp=engine['lib/pyscript/script.cpp']
        prelude=r'''
#include <cmath>
namespace BW {
struct Vector2 {float x=0,y=0;float& operator[](unsigned n){return n?y:x;}};
struct Vector3 {float x=0,y=0,z=0;};
template<class T>struct PyVector:PyObject {static bool Check(PyObject*){return false;}T getVector(){return {};}};
namespace Script {
int setData(PyObject*,int&,const char* = "");
int setData(PyObject*,Vector2&,const char* = "");
int setData(PyObject*,Vector3&,const char* = "");
}
@CONVERTERS@
struct IRendererPipeline {enum {TYPE_DEFERRED_SHADING=1};};
struct Renderer {static bool deferred;static Renderer* pInstance(){static Renderer r;return &r;}int pipelineType(){return deferred?1:0;}};
bool Renderer::deferred=true;
struct DynamicDecalManager {static DynamicDecalManager* pInstance(){static DynamicDecalManager m;return &m;}BW::string textureName(int index){return index>=0&&index<3?"texture"+std::to_string(index):"";}};
float stockDynamicDecalGroupLifetime(const BW::string& name){return name=="impact"?12.f:1e9f;}
unsigned emitted=0;
bool addStockDynamicDecal(float lifetime,const Vector3&,const Vector3&,const Vector2&,float,
 const BW::string& diffuse,const BW::string& normal,const BW::string& specular,bool simple,std::string&){
 assert(!simple);assert(lifetime==12.f);assert(diffuse=="texture0"&&normal=="texture1"&&specular.empty());++emitted;return true;}
#define ERROR_MSG(...) ((void)0)
@PRODUCTION@
}
'''
        conversions=''.join(between(cpp,'int Script::setData( PyObject * pObject, '+name,'\n}\n')
            for name in ('int & rInt,','Vector2 & rVector,','Vector3 & rVector,'))
        production=(ROOT/'bigworld/stock_ground_decals.cpp').read_text()
        production=production[production.index('PyObject* wg_addDecal('):production.index('BW_END_NAMESPACE')]
        prelude=prelude.replace('@CONVERTERS@',conversions).replace('@PRODUCTION@',production)
        source=source.replace('#include "stock_screener.cpp"',prelude)
        source=source.replace('int(BW::PyWatermarkersWrapper::s_type_.tp_basicsize)','0')
        source=source.replace('static PyMethodDef hostMethods[] = {','''
static PyObject* deferred(PyObject*,PyObject* args){int value;if(!PyArg_ParseTuple(args,"i",&value))return NULL;BW::Renderer::deferred=value;Py_RETURN_NONE;}
static PyObject* emitted(PyObject*,PyObject*){return PyInt_FromLong(BW::emitted);}
static PyMethodDef hostMethods[] = {
 {"wg_addDecal",BW::_py_wg_addDecal,METH_VARARGS,NULL},{"deferred",deferred,METH_VARARGS,NULL},{"emitted",emitted,METH_NOARGS,NULL},''')
        python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config=python.with_name('python2.7-config')
        if not config.exists():self.skipTest('Real CPython 2.7 headers required')
        flags=[f for f in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True))if f!='-DNDEBUG']
        libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
        consumer=r'''
import _screener_host as native
call=native.wg_addDecal
valid=('impact',(0.,1.,0.),(0.,-1.,0.),(1.,2.),.3,0,1,-1)
assert call(*valid) is None and native.emitted()==1
for count in range(10):
 if count==8:continue
 try:call(*(valid+(0,0))[:count])
 except TypeError:pass
 else:raise AssertionError('Argument count accepted')
for index in range(8):
 invalid=list(valid);invalid[index]=object()
 try:call(*invalid)
 except TypeError:pass
 else:raise AssertionError('Bad argument accepted')
for deferred in (0,1):
 native.deferred(deferred)
 invalid=list(valid);invalid[3]=(0.,0.)
 try:call(*invalid)
 except RuntimeError as error:assert str(error)=='addDecal - decal size is zero.\n'
 else:raise AssertionError('Zero size did not raise RuntimeError')
 assert call(*valid) is None
assert native.emitted()==2
print('Actual SDK RETOWN/8-argument parser, zero-size RuntimeError, cleared error state passed')
'''
        with tempfile.TemporaryDirectory(prefix='ground-decal-python27-')as name:
            directory=Path(name);(directory/'binding.cpp').write_text(source);(directory/'consumer.py').write_text(consumer)
            binary=directory/'binding'
            compiler=shutil.which(os.environ.get('CXX','c++'))
            if not compiler:self.skipTest('Optional C++ compiler is unavailable')
            subprocess.run([compiler,'-std=c++14','-UNDEBUG',*flags,'-Wall','-Werror','-Wno-writable-strings',
                '-Wno-deprecated-register','-Wno-unused-function',str(directory/'binding.cpp'),*libs,'-o',str(binary)],check=True)
            subprocess.run([str(binary),str(directory/'consumer.py')],check=True)
            client=os.environ.get('WOT_0922_CLIENT')
            if client:subprocess.run([str(binary),str(ROOT/'tests/stock_ground_decals_consumer.py'),
                str(Path(client)/'res/packages/scripts.pkg')],check=True)

    def test_aggregate_projection_matches_original_instructions(self):
        client=exact_client(self)
        from stock_ground_decals_oracle import OriginalGroundDecalProjection
        original=OriginalGroundDecalProjection(Path(client)/'WorldOfTanks.exe')
        rng=random.Random(7235);cases=[]
        for unused in range(500):
            contacts=[([rng.uniform(-30,30)for a in range(3)],
                       [rng.uniform(-1,1)for a in range(3)],rng.choice((0,8,16,24)))for n in range(rng.randrange(1,15))]
            start=[rng.uniform(-10,10)for a in range(3)];direction=[rng.uniform(-1,1)for a in range(3)]
            cases.append((contacts,start,direction));cases.append((contacts[::-1],start,direction))
        encoded=bytearray();expected=bytearray()
        for contacts,start,direction in cases:
            encoded.extend(struct.pack('<I6f',len(contacts),*(start+direction)))
            for p,n,f in contacts:encoded.extend(struct.pack('<6fI',*(p+n+[f])))
            expected.extend(original.project(contacts,start,direction))
        with tempfile.TemporaryDirectory(prefix='ground-decal-projection-')as name:
            binary=Path(name)/'probe'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),str(ROOT/'tests/stock_ground_decals_projection_probe.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=encoded)
        if actual!=expected:
            index=next(i for i,(a,b)in enumerate(zip(actual,expected))if a!=b)//24
            self.fail("Projection %d: %r != %r; input=%r"%(index,struct.unpack_from("<5fI",actual,index*24),struct.unpack_from("<5fI",expected,index*24),cases[index]))
        print('Original 932123..932555: %d aggregate projections exact'%len(cases))

    def test_bsp_traversal_preserves_shared_triangles_and_order(self):
        client=exact_client(self)
        from stock_ground_decals_oracle import OriginalGroundDecalBsp
        original=OriginalGroundDecalBsp(Path(client)/'WorldOfTanks.exe')
        randomizer=random.Random(7233);cases=[]
        for unused in range(2000):
            planes=[[randomizer.uniform(-1,1)for axis in range(3)]+[randomizer.uniform(-3,3)]for node in range(15)]
            center=[randomizer.uniform(-5,5)for axis in range(3)]
            axes=[[randomizer.uniform(-3,3)for axis in range(3)]for row in range(3)]
            cases.append((planes,center,axes))
        for normal in ((1.,0.,0.),(0.,1.,0.),(0.,0.,1.),(0.,0.,0.)):
            cases.append(([list(normal)+[0.]]*15,[0.,0.,0.],[[2.,0.,0.],[0.,2.,0.],[0.,0.,2.]]))
        encoded=b''.join(struct.pack('<72f',*(list(c)+sum(axes,[])+sum(planes,[])))for planes,c,axes in cases)
        with tempfile.TemporaryDirectory(prefix='ground-decal-bsp-')as name:
            binary=Path(name)/'probe'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),str(ROOT/'tests/stock_ground_decals_bsp_probe.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=encoded)
        expected=bytearray()
        for planes,center,axes in cases:
            values=original.visit(planes,center,axes)
            expected.extend(struct.pack('<I',len(values)))
            expected.extend(struct.pack('<%dI'%len(values),*values))
        self.assertEqual(actual,expected)
        print('Original 1179620: %d BSP queries, exact visited/shared triangle order'%len(cases))

    def test_original_terrain_bool_is_independent_of_nonempty_contacts(self):
        client=exact_client(self)
        from stock_ground_decals_oracle import OriginalGroundDecalTerrain
        original=OriginalGroundDecalTerrain(Path(client)/'WorldOfTanks.exe')
        triangle=((-1.,0.,-1.),(-1.,0.,1.),(1.,0.,1.))
        static=original.contacts(triangle,(0.,0.,0.),2.)
        result,terrain=original.terrain_records([triangle],(0.,0.,0.),2.)
        self.assertEqual((result,len(terrain)),(0,3))
        self.assertEqual([struct.unpack_from('<H',value,48)[0]for value in terrain],[8]*3)
        self.assertEqual(original.terrain_query,[(0.,0.,0.),(2.,0.,0.),(0.,2.,0.),(0.,0.,2.),(0.,-2.,0.),(0.,2.,0.)])
        for order,expected,stop in ((('terrain',),terrain,0x9325bf),(('bsp',),static,0x932123),
            (('bsp','terrain'),static+terrain,0x932123),(('terrain','bsp'),terrain+static,0x932123)):
            admitted,contacts,destination=original.dispatch(order,static)
            self.assertEqual(admitted,int(stop==0x932123))
            self.assertEqual(contacts,expected);self.assertEqual(destination,stop)

    def test_consumed_manifold_fields_match_original_instructions(self):
        client=exact_client(self)
        from stock_ground_decals_oracle import OriginalGroundDecalContacts
        original=OriginalGroundDecalContacts(Path(client)/'WorldOfTanks.exe')
        randomizer=random.Random(7231)
        cases=[]
        for unused in range(10000):
            triangle=[[randomizer.uniform(-4,4) for axis in range(3)]for vertex in range(3)]
            center=[randomizer.uniform(-2,2)for axis in range(3)]
            cases.append((triangle,center,randomizer.uniform(.25,4)))
        for triangle in (((0.,0.,0.),)*3,((0.,0.,0.),(1.,1.,1.),(2.,2.,2.)),
                         ((-1.,0.,-1.),(-1.,0.,1.),(1.,0.,1.)),
                         ((-100.,0.,-100.),(-100.,0.,100.),(100.,0.,100.))):
            for center in ((0.,0.,0.),(0.,1.,0.),(0.,-1.,0.),(10.,0.,10.)):
                for radius in (.25,1.,2.,100.):cases.append((triangle,center,radius))
        encoded=b''.join(struct.pack('<13f',*(sum((list(v)for v in t),[])+list(c)+[r]))for t,c,r in cases)
        with tempfile.TemporaryDirectory(prefix='ground-decal-contact-')as name:
            binary=Path(name)/'probe'
            subprocess.run(compiler_command(self)+['-I'+str(ROOT/'bigworld'),
                str(ROOT/'tests/stock_ground_decals_probe.cpp'),'-o',str(binary)],check=True)
            actual=subprocess.check_output([str(binary)],input=encoded)
        expected=bytearray();coverage=set();contacts=0
        for triangle,center,radius in cases:
            values=original.contacts(triangle,center,radius)
            if original.selected is not None:coverage.add(0 if original.selected<3 else 1 if original.selected==3 else 2)
            expected.extend(struct.pack('<I',len(values)))
            for value in values:expected.extend(value[12:24]+value[36:48])
            contacts+=len(values)
        self.assertEqual(coverage,{0,1,2})
        self.assertEqual(len(actual),len(expected))
        if actual!=expected:
            offset=next(index for index,(left,right) in enumerate(zip(actual,expected)) if left!=right)
            self.fail('Native contact bytes differ at offset %d: %s != %s'%(offset,
                actual[max(0,offset-12):offset+12].hex(),expected[max(0,offset-12):offset+12].hex()))
        print('Original f69650: %d AABB/triangle cases, %d contact points/normals exact'%(len(cases),contacts))


if __name__=='__main__':unittest.main()
