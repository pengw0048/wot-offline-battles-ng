"""Actual Movie/BitmapData creation contract and render-command owner regression."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
sys.path.insert(0,str(HERE))
from test_stock_vector4_basic import block


def load_patch(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/'bigworld'/(name+'.py'))
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value)
    return value


patcher=load_patch('patch_stock_flash_drawable_images')


def sdk(test):
    name='WOT_SCALEFORM_SDK_ROOT'
    root=Path(os.environ.get(name,'/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    if not root.is_dir():
        if name in os.environ:test.fail('Configured Scaleform SDK missing: '+str(root))
        test.skipTest('Pinned Scaleform 4.2.22 SDK required')
    test.assertIn('"4.2.22"',(root/'Include/GFxVersion.h').read_text())
    return root


def engine(test):
    name='WOT_PUBLIC_ENGINE_ARCHIVE'
    path=Path(os.environ.get(name,'/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not path.is_file():
        if name in os.environ:test.fail('Configured public engine archive missing: '+str(path))
        test.skipTest('Pinned public engine archive required')
    files={}
    with tarfile.open(path) as archive:
        for member in archive:
            for relative in patcher.FILES:
                if member.name.endswith('/programming/bigworld/'+relative):
                    files[relative]=archive.extractfile(member).read().decode('utf-8-sig')
    test.assertEqual(set(files),set(patcher.FILES))
    return files


def compiler(test):
    value=shlex.split(os.environ.get('CXX','c++'))
    if not value or not shutil.which(value[0]):
        if 'CXX' in os.environ:test.fail('Configured C++ compiler missing')
        test.skipTest('C++ compiler required')
    return value


FAKE_SDK=r'''#include <cassert>
namespace Scaleform {
using ThreadId=unsigned;
static unsigned currentThread=1;
inline ThreadId GetCurrentThreadId(){return currentThread;}
#define SF_ASSERT(value) assert(value)
template<class T> struct Ptr {
    T* value=nullptr;
    Ptr(){} Ptr(T* v):value(v){if(value)value->AddRef();}
    Ptr(const Ptr& o):Ptr(o.value){}
    ~Ptr(){if(value)value->Release();}
    Ptr& operator=(const Ptr& o){T* next=o.value;if(next)next->AddRef();if(value)value->Release();value=next;return *this;}
    Ptr& operator=(T& v){if(value)value->Release();value=&v;return *this;}
    T* GetPtr()const{return value;}
};
namespace Render {
struct ThreadCommand {
    unsigned refs=1;
    virtual ~ThreadCommand(){}
    void AddRef(){++refs;} void Release(){if(--refs==0)delete this;}
    virtual void Execute()=0;
};
struct TextureManager {int GetDrawableImageFormat(){return 27;}};
struct Interfaces {
    TextureManager* pTextureManager=nullptr;
    void* pHAL=nullptr;void* pRenderer2D=nullptr;ThreadId RenderThreadID=0;
};
struct ThreadCommandQueue {
    virtual ~ThreadCommandQueue(){}
    virtual void PushThreadCommand(ThreadCommand*)=0;
    virtual void GetRenderInterfaces(Interfaces* p){*p=Interfaces();}
};
}}
'''


def build(test,directory,name,source):
    directory=Path(directory)
    include=directory/'Render';include.mkdir(exist_ok=True)
    (include/'Render_ThreadCommandQueue.h').write_text(FAKE_SDK)
    cpp=directory/(name+'.cpp');exe=directory/(name+('.exe' if os.name=='nt' else ''))
    cpp.write_text(source)
    subprocess.run(compiler(test)+['-std=c++14','-O0','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-I'+str(directory),'-I'+str(ROOT/'bigworld'),str(cpp),'-o',str(exe)],check=True)
    return exe


class FlashDrawableImagesTest(unittest.TestCase):
    def test_patch_is_atomic_idempotent_and_keeps_existing_image_owners(self):
        original=engine(self);patched=patcher.patch_sources(original)
        self.assertEqual(patcher.patch_sources(patched),patched)
        self.assertNotIn('BitmapData',patched[patcher.MOVIE])
        self.assertIn('HAL(&drawableImageQueue_)',patched[patcher.SOURCE])
        self.assertIn('Manager::instance().drawableImageQueue()',patched[patcher.MOVIE])
        factory=block(patched[patcher.MOVIE],'PyObject* PyMovieDef::createInstance(')
        self.assertLess(factory.index('if (!movieView)'),factory.index('movieView->SetViewport'))
        for name,before,after in patcher.SEAMS:
            bad=dict(original);bad[name]=bad[name].replace(before,'/* changed queue seam */',1)
            with tempfile.TemporaryDirectory() as directory:
                root=Path(directory)
                for member,text in bad.items():
                    path=root/member;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
                with self.assertRaises(ValueError):patcher.patch(root)
                self.assertEqual({member:(root/member).read_text() for member in bad},bad)
        # SDK BitmapData and bitmap-fill strong leases are deliberately untouched.
        self.assertEqual(patcher.FILES,(patcher.HEADER,patcher.SOURCE,patcher.MOVIE))

    def test_actual_sdk_completion_and_device_state_owner(self):
        source=(HERE/'stock_flash_drawable_images_host.cpp').read_text()
        queue=(sdk(self)/'Src/Render/Render_DrawableImage_Queue.cpp').read_text(encoding='utf-8-sig')
        source=source.replace('@EXECUTOR@',patcher.EXECUTOR).replace('@SDK_COMPLETION@',
            block(queue,'void DICommandQueue::ExecuteCommand::Execute()'))
        with tempfile.TemporaryDirectory() as directory:
            exe=build(self,directory,'queue',source)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('SDK completion/refs',result.stdout)
            wrong=subprocess.run([str(exe),'wrong-thread'],capture_output=True,text=True)
            self.assertNotEqual(wrong.returncode,0,'Unsupported cross-thread access was silently admitted')

    def test_original_factory_call_reproduces_missing_image_and_fixed_call_allocates(self):
        original=engine(self);patched=patcher.patch_sources(original)
        constructor=(sdk(self)/'Src/GFx/AS3/Obj/Display/AS3_Obj_Display_BitmapData.cpp').read_text(encoding='utf-8-sig')
        constructor=block(constructor,'    void BitmapData::AS3Constructor(')
        with tempfile.TemporaryDirectory() as directory:
            for name,files,success in (('before',original,False),('after',patched,True)):
                factory=block(files[patcher.MOVIE],'PyObject* PyMovieDef::createInstance(')
                prefix=factory[factory.index('{')+1:factory.index('//Sets the viewport')]
                source=(HERE/'stock_flash_drawable_images_constructor_host.cpp').read_text()
                source=source.replace('@CONSTRUCTOR@',constructor).replace('@FACTORY_PREFIX@',prefix)
                exe=build(self,directory,name,source)
                result=subprocess.run([str(exe)],capture_output=True,text=True)
                if success:self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                else:
                    self.assertNotEqual(result.returncode,0,'No-queue negative control created a BitmapData image')
                    self.assertIn('created==1',result.stdout+result.stderr)

    def test_real_windows_sdk_pixels_lifecycle_and_negative_control(self):
        executable=os.environ.get('WOT_SCALEFORM_DRAWABLE_IMAGE_PROBE')
        if not executable:self.skipTest('Set WOT_SCALEFORM_DRAWABLE_IMAGE_PROBE for real Windows D3D9/SDK pixels')
        self.assertTrue(Path(executable).is_file(),'Configured SDK probe missing')
        result=subprocess.run([executable,'before'],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('old no-queue BitmapData has no image',result.stdout+result.stderr)
        for args in ([],['text']):
            result=subprocess.run([executable]+args,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('PASS real SDK DrawableImage',result.stdout)


if __name__=='__main__':unittest.main()
