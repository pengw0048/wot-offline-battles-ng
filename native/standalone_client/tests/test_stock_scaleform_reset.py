"""ResetEx must invalidate Scaleform's backbuffer and render-buffer cache."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('flash_reset_patch', ROOT / 'bigworld/patch_stock_scaleform_render_thread.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class ScaleformResetTests(unittest.TestCase):
    def test_real_callback_gate_recreates_hal_state_for_reset_ex(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        source_root = os.environ.get('WOT_PUBLIC_ENGINE_SOURCE')
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if (not archive.is_file() and not source_root) or not compiler:
            self.skipTest('Pinned public SDK and host compiler required')
        def read(suffix):
            if source_root:
                return (Path(source_root) / suffix.lstrip('/')).read_text(encoding='utf-8-sig')
            with tarfile.open(archive) as sdk:
                name = next(name for name in sdk.getnames() if name.endswith(suffix))
                return sdk.extractfile(name).read().decode('utf-8-sig')
        callbacks = read('/lib/moo/device_callback.cpp')
        manager = read('/lib/scaleform/manager.cpp')
        # Use the same synchronous HAL callbacks installed in production.
        for old, new in PATCH.EDITS['lib/scaleform/manager.cpp']:
            if 'hal->PrepareForReset' in new or 'hal->RestoreAfterReset' in new:
                self.assertEqual(manager.count(old), 1)
                manager = manager.replace(old, PATCH.render_job_ownership(new))
        bodies = '\n'.join(function(callbacks, signature) for signature in (
            'void DeviceCallback::deleteAllUnmanaged(', 'void DeviceCallback::createAllUnmanaged('))
        bodies += '\n}\nnamespace ScaleformBW {\n' + '\n'.join(function(manager, signature) for signature in (
            'void Manager::deleteUnmanagedObjects()', 'void Manager::createUnmanagedObjects()')) + '\n}\n'
        with tempfile.TemporaryDirectory(prefix='flash-reset-ex-') as temp:
            folder = Path(temp)
            header = folder / 'lib/scaleform/manager.hpp'
            header.parent.mkdir(parents=True)
            header.write_text(read('/lib/scaleform/manager.hpp'))
            PATCH.patch_reset_lifecycle(folder)
            patched = header.read_text()
            PATCH.patch_reset_lifecycle(folder)
            self.assertEqual(patched, header.read_text())
            override = function(patched, 'bool recreateForD3DExDevice() const')
            for name, method in (('production', override), ('old-owner', '')):
                target = folder / (name + '.cpp')
                target.write_text(FIXTURE.replace('@OVERRIDE@', method).replace('@BODIES@', bodies))
                binary = folder / name
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(target), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if name == 'production' else 41,
                                 (name, result.returncode, result.stdout, result.stderr))


FIXTURE = r'''
#include <cassert>
#include <vector>
#define BW_GUARD
#define TRACE_MSG(...) ((void)0)
unsigned restoreErrors=0;
#define ERROR_MSG(...) ((void)++restoreErrors)
namespace Render {namespace D3D9 {
struct HAL {
 unsigned backbuffer=1920,cached=1920,prepared=0,restored=0;bool ready=false,fail=false;
 void PrepareForReset(){ready=true;cached=0;++prepared;}
 bool RestoreAfterReset(){assert(ready);if(fail)return false;cached=backbuffer;ready=false;++restored;return true;}
};}}
struct HALPtr {Render::D3D9::HAL*value;operator bool()const{return value;}Render::D3D9::HAL*GetPtr(){return value;}};
struct StockRenderThread {static unsigned syncs;template<class F>static void runSync(F&f){++syncs;f();}};
unsigned StockRenderThread::syncs=0;
namespace Moo {
struct DeviceCallback {
 using CallbackList=std::vector<DeviceCallback*>;virtual ~DeviceCallback()=default;
 virtual bool recreateForD3DExDevice()const{return false;}
 virtual void deleteUnmanagedObjects(){}virtual void createUnmanagedObjects(){}
 static void deleteAllUnmanaged(bool);static void createAllUnmanaged(bool);
};
struct Lock {void grab(){}void give(){}};
struct DeviceCallbackStatics {
 DeviceCallback::CallbackList values;DeviceCallback::CallbackList*callbacks_=&values;
 Lock lock;Lock*callbacksLock_=&lock;DeviceCallback*s_curCallback=nullptr;
 static DeviceCallbackStatics&instance(){static DeviceCallbackStatics v;return v;}
};
bool&getLocked(){static bool locked=false;return locked;}
struct RC {bool ex=true;bool usingD3DDeviceEx(){return ex;}};RC&rc(){static RC v;return v;}
}
namespace ScaleformBW {
struct Manager:Moo::DeviceCallback {
 HALPtr pRenderHAL_;@OVERRIDE@
 void deleteUnmanagedObjects()override;void createUnmanagedObjects()override;
};}
namespace Moo {
@BODIES@
int main(){
 Render::D3D9::HAL hal;ScaleformBW::Manager manager;manager.pRenderHAL_={&hal};
 Moo::DeviceCallback retainedTexture;auto&list=Moo::DeviceCallbackStatics::instance().values;
 list={&manager,&retainedTexture};
 for(unsigned width:{1280u,2560u,1920u}){
  Moo::DeviceCallback::deleteAllUnmanaged(false);hal.backbuffer=width;
  Moo::DeviceCallback::createAllUnmanaged(false);
  if(hal.cached!=width||hal.ready)return 41;
 }
 assert(hal.prepared==3&&hal.restored==3&&StockRenderThread::syncs==6);
 // Ordinary D3D9 still uses the forced callbacks.
 Moo::rc().ex=false;Moo::DeviceCallback::deleteAllUnmanaged(true);
 hal.backbuffer=800;Moo::DeviceCallback::createAllUnmanaged(true);assert(hal.cached==800);
 assert(!restoreErrors);
 hal.fail=true;Moo::DeviceCallback::deleteAllUnmanaged(true);Moo::DeviceCallback::createAllUnmanaged(true);
 assert(hal.ready&&restoreErrors==1); // Failure remains visible and is never reported as a restored HAL.
}
'''


if __name__ == '__main__':
    unittest.main()
