"""Actual attachment owner, original PYC, exact PE descriptors and GPU schema."""
from pathlib import Path
import itertools
import os
import shlex
import struct
import shutil
import subprocess
from native_test_compile_flags import native_test_compile_flags
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
sys.path.insert(0,str(ROOT/'bigworld'))
from test_stock_screener import engine_sources,generated_source,between
import patch_stock_space_decals as scene
import patch_stock_dynamic_decals as dynamic
import patch_stock_vehicle_decals as vehicle

INCLUDES=('pch.hpp','cstdmf/stdmf.hpp','duplo/py_attachment.hpp','moo/device_callback.hpp','moo/draw_context.hpp',
          'moo/format_name_map.hpp','moo/render_context.hpp','moo/texture_manager.hpp','physics2/collision_callback.hpp',
          'physics2/collision_obstacle.hpp','resmgr/auto_config.hpp','resmgr/bwresource.hpp',
          'space/client_space.hpp','space/deprecated_space_helpers.hpp','d3d9.h')


def compile_probe(directory,source):
    if not shutil.which('c++'):
        raise unittest.SkipTest('C++ compiler unavailable: c++')
    path=directory/'probe.cpp';path.write_text(source)
    binary=directory/'probe'
    subprocess.run(['c++', *native_test_compile_flags('c++'),'-std=c++14','-Wall','-Wextra','-Werror','-UNDEBUG',
        '-I'+str(directory/'include'),'-I'+str(ROOT/'bigworld'),'-I'+str(ROOT/'tests'),str(path),'-o',str(binary)],check=True)
    return binary


def binding_host(test,directory):
    engine=engine_sources(test);source=generated_source(engine)
    source=source.replace('PyWatermarkersWrapper','WGOcclusionDecal').replace('"_screener_host"','"_vehicle_decals_host"')
    extra='''
namespace BW {
bool failure(const char*){return false;}
using uint=unsigned;using uint32=unsigned;
struct Matrix {int value=7;};
namespace Script {
int setData(PyObject*,uint&,const char*="");
int setData(PyObject* o,Matrix& m,const char* name="") {
 if(!PyCapsule_IsValid(o,"MatrixProvider")){PyErr_Format(PyExc_TypeError,"%s must be a MatrixProvider",name);return -1;}
 m=*static_cast<Matrix*>(PyCapsule_GetPointer(o,"MatrixProvider"));return 0;
}
PyObject* getData(bool value){return PyBool_FromLong(value);}
PyObject* getRetData(bool value){return getData(value);}
}
@UINT_BODY@
struct VehicleDecalAttachment:PyObjectPlus {
 explicit VehicleDecalAttachment(PyTypeObject* t):PyObjectPlus(t){}
 void setLocalTransform(const Matrix&){}
 bool configure(const BW::string& texture,unsigned material,unsigned priority,unsigned influence) {
  PyObject* value=Py_BuildValue("(ssIII)","configure",texture.c_str(),material,priority,influence);
  PyList_Append(events,value);Py_DECREF(value);return true;
 }
 bool collide_=false;
 static PyObject* events;
};
PyObject* VehicleDecalAttachment::events=NULL;
using PyAttachment=PyObjectPlus;
}
#define PY_FACTORY_METHOD_LINK_DECLARE()
@FACTORY@
#define PY_FACTORY_NAMED(T,N,M) \\
 static PyObject* factory_##T(){if(PyType_Ready(&T::s_type_)<0)return NULL;static PyMethodDef method={N,(PyCFunction)T::_pyNew,METH_VARARGS,NULL};return PyCFunction_New(&method,NULL);} \\
 static BW::PyModuleResultLink link_##T(#M,N,&factory_##T);
BW_BEGIN_NAMESPACE
@CLASSES@
BW_END_NAMESPACE
'''
    extra=extra.replace('@UINT_BODY@',between(engine['lib/pyscript/script.cpp'],'int Script::setData( PyObject * pObject, uint & rUint,','\n}\n'))
    extra=extra.replace('@FACTORY@',between(engine['lib/pyscript/pyobject_plus.hpp'],'#define PY_AUTO_CONSTRUCTOR_FACTORY_DECLARE(', '\n/// This macro is used internally',False))
    production=(ROOT/'bigworld/stock_vehicle_decals.cpp').read_text()
    extra=extra.replace('@CLASSES@',production[production.index('class WGOcclusionDecal :'):production.index('bool initialiseStockVehicleDecals()')])
    source=source.replace('#include "stock_screener.cpp"',extra)
    source=source.replace('static PyMethodDef hostMethods[] = {','''
static PyObject* events(PyObject*,PyObject*){Py_INCREF(BW::VehicleDecalAttachment::events);return BW::VehicleDecalAttachment::events;}
static PyObject* matrix(PyObject*,PyObject*){static BW::Matrix m;return PyCapsule_New(&m,"MatrixProvider",NULL);}
static PyMethodDef hostMethods[] = {
 {"events",events,METH_NOARGS,NULL},{"matrix",matrix,METH_NOARGS,NULL},''')
    source=source.replace('Py_Initialize();','Py_Initialize();BW::VehicleDecalAttachment::events=PyList_New(0);')
    source=source.replace('Py_Finalize();','Py_DECREF(BW::VehicleDecalAttachment::events);BW::VehicleDecalAttachment::events=NULL;Py_Finalize();')
    # Keep the exact unsigned parser body. The fixture uses 64-bit host longs;
    # all producer arguments are within the original 32-bit uint contract.
    source=source.replace('const char*=""','const char* = ""')
    python=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config=python.with_name('python2.7-config')
    if not config.exists():test.skipTest('CPython 2.7 development files required')
    flags=[f for f in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True)) if f!='-DNDEBUG']
    libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
    (directory/'binding.cpp').write_text(source);binary=directory/'binding'
    subprocess.run(['c++', *native_test_compile_flags('c++', sanitise=False),'-std=c++14','-UNDEBUG',*flags,'-Wall','-Werror','-Wno-writable-strings',
        '-Wno-deprecated-register','-Wno-unused-function',str(directory/'binding.cpp'),*libs,'-o',str(binary)],check=True)
    return binary


class VehicleDecalsTests(unittest.TestCase):
    def test_complete_production_owner_and_real_phase_packets(self):
        with tempfile.TemporaryDirectory(prefix='vehicle-decals-owner-') as name:
            directory=Path(name)
            for include in INCLUDES:
                path=directory/'include'/include;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#pragma once\n')
            binary=compile_probe(directory,'#include "stock_vehicle_decals_host.cpp"\n')
            subprocess.run([str(binary)],check=True)

    def test_original_vehicle_pyc_and_public_binding_parser(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        with tempfile.TemporaryDirectory(prefix='vehicle-decals-binding-') as name:
            binary=binding_host(self,Path(name))
            subprocess.run([str(binary),str(ROOT/'tests/stock_vehicle_decals_consumer.py'),
                            str(Path(client)/'res/packages/scripts.pkg')],check=True)

    def test_original_descriptor_and_persistent_gpu_records(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        import stock_vehicle_decals_oracle as oracle
        executable=Path(client)/'WorldOfTanks.exe'
        rows=oracle.wrapper_contract(executable)
        self.assertEqual(len(rows),36)
        for name,argc,fail,calls in rows:
            if fail is None:
                self.assertEqual(calls[0][0],'argCountError')
            else:
                self.assertEqual(calls[-1][0],'setData')
                self.assertEqual(calls[-1][2],fail)
        self.assertEqual(oracle.original_descriptor(executable,False,3,30),[(bytes(64),'ao',(3,30,2),1)])
        self.assertEqual(oracle.original_descriptor(executable,True,5,62),[(bytes(64),'ao',(5,62,6),1)])
        cases=list(itertools.product((2,6),(0,17,0xffffffff),(30,62,0xff00003e),(0.,9999.,10000.,16000.,22400.)))
        inputs=[(m,p,i,d,(0.25,-0.5,0.75,-1.),(1.,2.,3.),(4.,5.,6.),(3,7,255,511)) for m,p,i,d in cases]
        expected=b''.join(oracle.original_records(executable,inputs))
        source='''#include "stock_vehicle_decals_data.hpp"
#include <cstdio>
int main(){unsigned m,p,i;float d;const float q[]={.25f,-.5f,.75f,-1.f},pos[]={1,2,3},scale[]={4,5,6};
while(std::scanf("%u %u %u %f",&m,&p,&i,&d)==4){float alpha=1.f-wot::stockDecalMultiply(wot::stockDecalSubtract(d,10000.f),1.f/12500.f);
if(alpha<0)alpha=0;if(alpha>1)alpha=1;auto r=wot::stockVehicleDecalRecord(m,p,i,d,alpha,q,pos,scale,{{3,7,255,511}});std::fwrite(r.data(),1,r.size(),stdout);}}
'''
        with tempfile.TemporaryDirectory(prefix='vehicle-decals-oracle-') as name:
            binary=compile_probe(Path(name),source)
            data=''.join('%d %d %d %f\n'%case for case in cases).encode()
            actual=subprocess.check_output([str(binary)],input=data)
        self.assertEqual(actual,expected)
        print('Original persistent provider matched %d complete 124-byte records'%len(cases))

    def test_exact_authored_vehicle_scope_and_original_texture(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('Exact #1513 client required')
        import collections
        import zipfile
        sys.path.insert(0,str(ROOT.parents[1]/'tools'))
        from packed_xml import read_packed_xml
        counts=collections.Counter();vehicles=set()
        def walk(element,part,member):
            for name,value in element.children:
                if name==b'AODecals':
                    counts[part]+=len(value.value.children);vehicles.add(member)
                    for unused,decal in value.value.children:
                        transform=next(v for n,v in decal.value.children if n==b'transform')
                        self.assertEqual(len(transform.value),48)
                elif value.value_type==0:walk(value.value,part or name.decode(),member)
        with zipfile.ZipFile(Path(client)/'res/packages/scripts.pkg') as archive:
            for member in archive.namelist():
                if member.startswith('scripts/item_defs/vehicles/') and member.endswith('.xml'):
                    walk(read_packed_xml(archive.read(member)),'',member)
        self.assertEqual(counts,{'chassis':1015,'hull':1});self.assertEqual(len(vehicles),684)
        with zipfile.ZipFile(Path(client)/'res/packages/shared_content_sandbox.pkg') as archive:
            texture=archive.read('maps/spots/TankOcclusion/TankOcclusionMap.dds')
        self.assertEqual(texture[:4],b'DDS ');self.assertEqual(struct.unpack_from('<II',texture,12),(512,512))
        config=read_packed_xml((Path(client)/'res/decals_config.xml').read_bytes())
        semi=next(v.value for n,v in config.children if n==b'semiDynamic')
        self.assertEqual(next(v.value for n,v in semi.children if n==b'anisotropy'),4)

    def test_patcher_composition_idempotence_and_atomic_failure(self):
        engine=Path(os.environ.get('WOT_BIGWORLD_ROOT','/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))
        names=('lib/moo/deferred_decals_manager.cpp','lib/moo/deferred_pipeline.cpp','lib/moo/shadow_manager.cpp')
        if not all((engine/n).exists() for n in names):self.skipTest('Pinned engine source required')
        with tempfile.TemporaryDirectory(prefix='vehicle-decals-patch-') as name:
            root=Path(name)
            for n in names:
                path=root/n;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes((engine/n).read_bytes())
            before={n:(root/n).read_bytes() for n in names}
            with self.assertRaises(ValueError):vehicle.patch(root)
            self.assertEqual(before,{n:(root/n).read_bytes() for n in names})
            scene.patch(root);dynamic.patch(root);vehicle.patch(root)
            before={n:(root/n).read_bytes() for n in names}
            scene.patch(root);dynamic.patch(root);vehicle.patch(root)
            self.assertEqual(before,{n:(root/n).read_bytes() for n in names})
            manager=(root/names[0]).read_text()
            self.assertLess(manager.index('finaliseStockVehicleDecals();'),manager.index('finaliseStockDynamicDecals();'))
            self.assertIn('initialiseStockSpaceDecals() && initialiseStockDynamicDecals() && initialiseStockVehicleDecals()',manager)
            # A call elsewhere is not evidence that the init owner is installed.
            target=root/names[0]
            target.write_text(manager.replace('return initialiseStockSpaceDecals() && initialiseStockDynamicDecals() && initialiseStockVehicleDecals();','return unknownInitialiser();')+'\nvoid unrelated(){initialiseStockSpaceDecals();initialiseStockDynamicDecals();}\n')
            before={n:(root/n).read_bytes() for n in names}
            for operation in (scene.patch,dynamic.patch,vehicle.patch):
                with self.assertRaises(ValueError):operation(root)
                self.assertEqual(before,{n:(root/n).read_bytes() for n in names})


if __name__=='__main__':unittest.main()
