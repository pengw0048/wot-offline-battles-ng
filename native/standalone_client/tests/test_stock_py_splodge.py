"""#1513 Splodge factory, forward render submission and attachment lifecycle."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_stock_math_oscillators import engine_sources, generated_source, between, client, macro


def patcher():
    s=importlib.util.spec_from_file_location('splodge_patch',ROOT/'bigworld/patch_stock_py_splodge.py')
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m


def sources(test):
    import tarfile
    p=Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE','/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not p.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:test.fail('Configured SDK archive missing')
        test.skipTest('Pinned SDK archive required')
    with tarfile.open(p) as archive:
        return {n:archive.extractfile(next(m for m in archive if m.name.endswith('/programming/bigworld/'+n))).read().decode('utf-8')
                for n in ('lib/duplo/py_splodge.hpp','lib/duplo/py_splodge.cpp')}

HOST_METHODS=r'''
using namespace BW;
static PyObject* init(PyObject*,PyObject*){
 PyType_Ready(&PyObjectPlus::s_type_);PyAttachment::s_type_.tp_base=&PyObjectPlus::s_type_;PyType_Ready(&PyAttachment::s_type_);
 for(const auto& link:hostFactoryLinks()){if(PyType_Ready(link.type)<0)return NULL;PyObject_SetAttrString(PyImport_AddModule(link.module),link.name,(PyObject*)link.type);}Py_RETURN_NONE;
}
static PyObject* pipeline(PyObject*,PyObject* arg){Moo::pipeline=int(PyInt_AsLong(arg));Py_RETURN_NONE;}
static PyObject* inspect(PyObject*,PyObject* arg){auto* o=PySplodge::getSelf(arg);return Py_BuildValue("(ff)",o->maxLod_,o->heightOffset_);}
static PyObject* queue(PyObject*,PyObject* args){PyObject *obj,*mat;float lod;int outside=1,pass=0;
 if(!PyArg_ParseTuple(args,"OOf|ii",&obj,&mat,&lod,&outside,&pass))return NULL;
 Matrix m;if(BW::Script::setData(mat,m,"world")!=0)return NULL;auto* o=PySplodge::getSelf(obj);Moo::DrawContext context;context.pass=pass;
 o->updateAnimations(m,lod);o->tossed(outside!=0);o->draw(context,m);Py_RETURN_NONE;
}
static PyObject* flush(PyObject*,PyObject*){StockSplodge::task.draw();Py_RETURN_NONE;}
static PyObject* fini(PyObject*,PyObject*){StockSplodge::task.fini();Py_RETURN_NONE;}
static PyObject* counts(PyObject*,PyObject*){return Py_BuildValue("(iiii)",Moo::draws,Moo::callbacks,Moo::loads,Moo::clears);}
static PyObject* matrixTuple(const Matrix& m){PyObject* r=PyTuple_New(16);for(int i=0;i<16;++i)PyTuple_SET_ITEM(r,i,PyFloat_FromDouble(m.m[i]));return r;}
static PyObject* lastWorld(PyObject*,PyObject*){return matrixTuple(Moo::worlds.back());}
static PyObject* lastLocal(PyObject*,PyObject*){return matrixTuple(Moo::locals.back());}
static PyObject* retire(PyObject*,PyObject* args){PyObject* o;char* action;if(!PyArg_ParseTuple(args,"Os",&o,&action))return NULL;if(!strcmp(action,"detach"))PySplodge::getSelf(o)->detach();else PySplodge::getSelf(o)->leaveWorld();Py_RETURN_NONE;}
static PyObject* changeSpace(PyObject*,PyObject*){currentSpace.reset(new ClientSpace);Py_RETURN_NONE;}
static PyObject* resetDevice(PyObject*,PyObject*){StockSplodge::task.owner->deleteUnmanagedObjects();StockSplodge::task.owner->deleteManagedObjects();Py_RETURN_NONE;}
static PyObject* bounds(PyObject*,PyObject* args){PyObject *o,*mat;if(!PyArg_ParseTuple(args,"OO",&o,&mat))return NULL;Matrix m;if(BW::Script::setData(mat,m)!=0)return NULL;BoundingBox b;PySplodge::getSelf(o)->worldBoundingBox(b,m);return Py_BuildValue("((fff)(fff))",b.low.x,b.low.y,b.low.z,b.high.x,b.high.y,b.high.z);}
static PyMethodDef methods[]={
 {"initScripts",init,METH_NOARGS,NULL},{"pipeline",pipeline,METH_O,NULL},{"inspect",inspect,METH_O,NULL},
 {"queue",queue,METH_VARARGS,NULL},{"flush",flush,METH_NOARGS,NULL},{"fini",fini,METH_NOARGS,NULL},
 {"counts",counts,METH_NOARGS,NULL},{"lastWorld",lastWorld,METH_NOARGS,NULL},{"lastLocal",lastLocal,METH_NOARGS,NULL},
 {"retire",retire,METH_VARARGS,NULL},{"changeSpace",changeSpace,METH_NOARGS,NULL},{"resetDevice",resetDevice,METH_NOARGS,NULL},
 {"bounds",bounds,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
int main(int argc,char** argv){Py_Initialize();PySys_SetArgv(argc-1,argv+1);Py_InitModule("_splodge_host",methods);FILE* script=fopen(argv[1],"r");if(!script)return 2;int result=PyRun_SimpleFileEx(script,argv[1],1);Py_Finalize();return result?1:0;}
'''


def generated(test,original):
    engine=engine_sources(test)
    macros=generated_source(engine).split('#include "stock_math_oscillators.cpp"')[0]
    macros+='\n'+''.join(macro(engine['lib/pyscript/pyobject_plus.hpp'],n) for n in ('PY_RW_ATTRIBUTE_DECLARE','PY_WRITABLE_ATTRIBUTE_SET','PY_RO_ATTRIBUTE_GET','PY_RO_ATTRIBUTE_SET'))
    fixtures=(ROOT/'tests/stock_py_splodge_host.hpp').read_text()
    p=patcher()
    with tempfile.TemporaryDirectory() as directory:
        tree=Path(directory)
        for n,s in original.items():(tree/n).parent.mkdir(parents=True,exist_ok=True);(tree/n).write_text(s)
        p.patch(tree)
        h=(tree/p.HEADER).read_text().replace('private:','public:')
        implementation=(ROOT/'bigworld/stock_py_splodge.hpp').read_text()
    strip=lambda text:'\n'.join(l for l in text.splitlines() if not l.startswith(('#include','#pragma')))
    # Matrix conversion must be declared before factory argument macros instantiate.
    return macros+'\n'+fixtures+'\n'+strip(h)+'\n'+strip(implementation)+'\n'+HOST_METHODS


class SplodgeTest(unittest.TestCase):
    def test_patch_is_scoped_idempotent_and_transactional(self):
        original=sources(self);p=patcher()
        with tempfile.TemporaryDirectory() as directory:
            tree=Path(directory)
            for n,s in original.items():(tree/n).parent.mkdir(parents=True,exist_ok=True);(tree/n).write_text(s)
            p.patch(tree);first={str(f.relative_to(tree)):f.read_bytes() for f in tree.rglob('*') if f.is_file()}
            p.patch(tree);self.assertEqual(first,{str(f.relative_to(tree)):f.read_bytes() for f in tree.rglob('*') if f.is_file()})
            for n,s in original.items():(tree/n).write_text(s)
            (tree/p.SOURCE).write_text('unknown owner')
            before=(tree/p.HEADER).read_bytes()
            with self.assertRaises(ValueError):p.patch(tree)
            self.assertEqual(before,(tree/p.HEADER).read_bytes())

    def test_exact_forward_effect_and_geometry_resources(self):
        import io, zipfile
        path=client(self)
        package=path/'res/packages'
        with zipfile.ZipFile(package/'shaders.pkg') as z:
            fxo=z.read('shaders/shadow/wg_forward_tank_shadow.11.dx9.fxo')
            effect=zipfile.ZipFile(io.BytesIO(fxo)).read('effect')
        with zipfile.ZipFile(package/'misc.pkg') as z:
            for name in ('system/models/fx_unite_cube.visual_processed','system/models/fx_unite_cube.primitives_processed',
                         'system/models/plane.visual_processed','system/models/plane.primitives_processed'):
                self.assertTrue(z.read(name))
        with zipfile.ZipFile(package/'shared_content_sandbox.pkg') as z:
            self.assertEqual(z.read('maps/spots/TankOcclusion/TankOcclusionMap_forward.dds')[:4],b'DDS ')
        cpp=r"""
#include "wot/stock_shader_binary.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int n,char** v){(void)n;std::ifstream f(v[1],std::ios::binary);std::vector<char>b((std::istreambuf_iterator<char>(f)),{});wot::StockEffectBinary e;std::string error;if(!wot::decode_stock_dx9_effect(b.data(),b.size(),e,error)){std::cerr<<error;return 1;}for(auto& t:e.techniques)std::cout<<t.name<<" "<<t.passes.size()<<"\n";for(auto& buffer:e.uniform_buffers)for(auto& u:buffer.uniforms)std::cout<<u.name<<"\n";for(auto& r:e.resource_views)std::cout<<r.name<<"\n";}
"""
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'probe.cpp').write_text(cpp);(root/'effect').write_bytes(effect)
            subprocess.run(['c++','-std=c++17','-I',str(ROOT/'include'),str(root/'probe.cpp'),
                            str(ROOT/'src/stock_shader_binary.cpp'),'-o',str(root/'probe')],check=True)
            out=subprocess.check_output([str(root/'probe'),str(root/'effect')],text=True)
            self.assertIn('g_worldMat',out);self.assertIn('g_transform',out);self.assertIn('g_borderTC',out)
            self.assertIn('g_shadowMap',out)
            for name,count in (('CLEAR_STENCIL',2),('STENCIL_PREPARATION',3),('SHADOW_PLANE',1)):self.assertIn(name+' '+str(count),out)

    def test_original_x86_factory_and_draw_receipts(self):
        path=client(self)
        try:
            import pefile, unicorn
            from stock_py_splodge_oracle import OriginalSplodge
        except ImportError:self.skipTest('Optional pefile and Unicorn required')
        e=OriginalSplodge(path/'WorldOfTanks.exe')
        local=[2.,0.,0.,0.,0.,3.,0.,0.,0.,0.,4.,0.,5.,6.,7.,1.]
        world=[1.,0.,0.,0.,0.,2.,0.,0.,0.,0.,1.,0.,11.,12.,13.,1.]
        for args in ((local,),(local,25.),(local,25.,2.5)):
            owner=e.factory(*args)
            self.assertEqual(e.floats(owner+0x24,16),local)
            self.assertEqual(e.floats(owner+0x68,2),[args[1] if len(args)>1 else 50.,args[2] if len(args)>2 else 1.])
            receipt=e.draw(owner,world,lod=25.)
            expected=world[:];expected[13]-=2.*(args[2] if len(args)>2 else 1.)
            self.assertEqual(receipt,expected+local)
            self.assertIsNone(e.draw(owner,world,lod=51.))
            self.assertIsNotNone(e.draw(owner,world,lod=float('nan')))
            self.assertIsNone(e.draw(owner,world,outside=False))
            self.assertIsNone(e.draw(owner,world,enabled=False))
            e.forward=False;self.assertIsNone(e.draw(owner,world));e.forward=True
        for args in ((),(local,1,2,3),((1,2,3),),(local,'bad'),(local,1,'bad')):
            self.assertEqual(e.factory(*args),'error')
        owner=e.factory(local,float('nan'),2.)
        self.assertIsNotNone(e.draw(owner,world,lod=100.))
        e.forward=False;self.assertIsNone(e.factory(local,50.,2.))

    def test_production_factory_renderer_attachment_and_original_pyc(self):
        path=client(self)
        py=Path(os.environ.get('WOT_PYTHON27','/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'));config=py.with_name('python2.7-config')
        if not config.is_file():
            if 'WOT_PYTHON27' in os.environ:self.fail('Configured Python 2.7 development files missing')
            self.skipTest('CPython 2.7 development files required')
        self.assertTrue((path/'res/packages/scripts.pkg').is_file(),'Configured exact client missing')
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/'host.cpp').write_text(generated(self,sources(self)))
            flags=[f for f in shlex.split(subprocess.check_output([str(config),'--cflags'],text=True)) if not f.startswith('-O') and f!='-DNDEBUG']
            libs=shlex.split(subprocess.check_output([str(config),'--ldflags'],text=True))
            subprocess.run(['c++','-std=c++14','-O0','-Wall','-Werror','-Wno-missing-field-initializers','-Wno-writable-strings','-Wno-deprecated-register','-Wno-inconsistent-missing-override',*flags,str(root/'host.cpp'),*libs,'-o',str(root/'host')],check=True)
            r=subprocess.run([str(root/'host'),str(ROOT/'tests/stock_py_splodge_consumer.py'),str(path)],text=True,capture_output=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertIn('original PYC passed',r.stdout)


if __name__=='__main__':unittest.main()
