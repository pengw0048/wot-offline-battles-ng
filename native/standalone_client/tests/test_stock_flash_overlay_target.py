"""Exercise the real overlay owner against the SDK's actual stack-bottom setter."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function
from test_stock_scaleform_scene import REVISION

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('overlay_patch', ROOT / 'bigworld/patch_stock_scaleform_render_thread.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class FlashOverlayTargetTests(unittest.TestCase):
    def test_native_surface_owner_forward_deferred_msaa_resize_reset_and_world_text(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        sdk = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT', '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        try:
            source = subprocess.check_output(['git', '-C', str(sdk), 'show',
                REVISION + ':Src/Render/D3D9/D3D9_HAL.cpp']).decode('utf-8-sig').replace('\r\n', '\n')
        except (OSError, subprocess.CalledProcessError):
            self.skipTest('Pinned Scaleform SDK required')
        setter = function(source, 'bool HAL::SetRenderTarget(')
        # Native allocations, COM calls, and the filter GPU commands remain
        # explicit boundaries. The production RAII owner and actual SDK
        # stack-bottom update execute without being reimplemented by the test.
        with tempfile.TemporaryDirectory(prefix='flash-overlay-target-') as folder:
            root = Path(folder)
            for name, owner in (('production', (ROOT / 'bigworld/stock_flash_overlay_target.hpp').read_text()),
                                ('old-owner', OLD_OWNER)):
                path = root / (name + '.cpp')
                path.write_text(HOST.replace('@SETTER@', setter).replace('@OWNER@', owner))
                binary = root / name
                subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if name == 'production' else 41,
                                 (name, result.returncode, result.stdout, result.stderr))

    def test_all_display_and_retirement_producers_use_the_overlay_owner(self):
        for relative, marker in (('lib/scaleform/manager.cpp', 'struct GlobalMoviesJob'),
                                 ('lib/scaleform/flash_gui_component.cpp', 'struct DisplayJob'),
                                 ('lib/scaleform/flash_text_gui_component.cpp', 'struct TextDisplayJob')):
            previous = next(new for unused, new in PATCH.EDITS[relative] if marker in new)
            result = PATCH.overlay_surface_ownership(PATCH.render_job_ownership(PATCH.viewport_before_capture(previous)))
            self.assertLess(result.index('StockFlashOverlayTarget::Scope target('), result.index('renderer->BeginFrame()'))
            self.assertIn('if (!target) return;', result)
            expected = 'true' if 'GlobalMoviesJob' in marker else 'overlay'
            self.assertIn('Moo::rc().device(), ' + expected + ');', result)
        for unused, previous in PATCH.EDITS['lib/scaleform/manager.cpp']:
            result = PATCH.overlay_surface_ownership(PATCH.render_job_ownership(previous))
            if 'hal->PrepareForReset' in previous:
                self.assertLess(result.index('overlayTarget_.reset();'), result.index('hal->PrepareForReset();'))
            if 'Finish every posted HAL frame' in previous:
                self.assertIn('auto drain = [this]() { overlayTarget_.reset(); };', result)


OLD_OWNER = r'''
namespace BW {namespace ScaleformBW {
struct StockFlashOverlayTarget {
 void reset(){}
 struct Scope {
  Scope(StockFlashOverlayTarget&,Scaleform::Render::D3D9::HAL*,IDirect3DDevice9*,bool){}
  explicit operator bool()const{return true;}
 };
};}}
'''


HOST = r'''
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <vector>
using HRESULT=std::int32_t;using DWORD=unsigned;using BOOL=int;
const HRESULT E_FAIL=-1,D3DERR_NOTFOUND=-2;
enum {D3DFMT_D24S8=75,FALSE=0,HS_InScene=1,HS_InDisplay=2};
#define FAILED(value) ((value)<0)
#define SUCCEEDED(value) ((value)>=0)
#define CHECK(value) do{if(!(value))return 41;}while(0)
unsigned errors=0;
void errorLog(const char*,...){++errors;}
#define ERROR_MSG errorLog
struct D3DSURFACE_DESC {unsigned Width,Height,Format,MultiSampleType,MultiSampleQuality;};
struct D3DVIEWPORT9{unsigned X,Y,Width,Height;float MinZ,MaxZ;};
struct RECT{int left,top,right,bottom;};
unsigned liveSurfaces=0;
struct IDirect3DSurface9 {
 unsigned refs=1;D3DSURFACE_DESC desc;
 explicit IDirect3DSurface9(D3DSURFACE_DESC d):desc(d){++liveSurfaces;}
 void AddRef(){++refs;}void Release(){if(!--refs){--liveSurfaces;delete this;}}
 HRESULT GetDesc(D3DSURFACE_DESC*out){*out=desc;return 0;}
};
struct IDirect3DDevice9 {
 IDirect3DSurface9*color=nullptr,*depth=nullptr;D3DVIEWPORT9 vp{10,20,937,571,.2f,.8f};RECT scissor{30,40,900,550};
 unsigned created=0,mutations=0;bool failCreate=false,failBind=false;
 IDirect3DDevice9(IDirect3DSurface9*c,IDirect3DSurface9*d){SetRenderTarget(0,c);SetDepthStencilSurface(d);}
 ~IDirect3DDevice9(){if(color)color->Release();if(depth)depth->Release();}
 HRESULT GetRenderTarget(unsigned,IDirect3DSurface9**out){*out=color;if(color)color->AddRef();return color?0:E_FAIL;}
 HRESULT GetDepthStencilSurface(IDirect3DSurface9**out){*out=depth;if(depth)depth->AddRef();return depth?0:D3DERR_NOTFOUND;}
 HRESULT GetViewport(D3DVIEWPORT9*out){*out=vp;return 0;}HRESULT GetScissorRect(RECT*out){*out=scissor;return 0;}
 HRESULT SetDepthStencilSurface(IDirect3DSurface9*d){++mutations;if(d&&failBind){failBind=false;return E_FAIL;}if(d)d->AddRef();if(depth)depth->Release();depth=d;return 0;}
 HRESULT SetRenderTarget(unsigned,IDirect3DSurface9*c){++mutations;if(c)c->AddRef();if(color)color->Release();color=c;vp={0,0,c->desc.Width,c->desc.Height,0,1};return 0;}
 HRESULT SetViewport(const D3DVIEWPORT9*v){++mutations;vp=*v;return 0;}HRESULT SetScissorRect(const RECT*r){++mutations;scissor=*r;return 0;}
 HRESULT CreateDepthStencilSurface(unsigned w,unsigned h,unsigned f,unsigned ms,unsigned quality,BOOL,IDirect3DSurface9**out,void*){
  ++created;if(failCreate){*out=nullptr;return E_FAIL;}*out=new IDirect3DSurface9({w,h,f,ms,quality});return 0;
 }
};
namespace Scaleform {
template<class T>struct Ptr {
 T*p=nullptr;Ptr(){}Ptr(T*value):p(value){if(p)p->AddRef();}Ptr(T&value):p(&value){}
 Ptr(const Ptr&o):Ptr(o.p){}~Ptr(){if(p)p->Release();}
 Ptr&operator=(const Ptr&o){return *this=o.p;}
 Ptr&operator=(T*value){if(value)value->AddRef();if(p)p->Release();p=value;return *this;}
 void Clear(){*this=static_cast<T*>(nullptr);}operator T*()const{return p;}T*operator->()const{return p;}
};
namespace Render {
struct RenderTargetData{IDirect3DSurface9*pRenderSurface,*pDSSurface;};
struct RenderTarget {
 unsigned refs=1;RenderTargetData data;
 RenderTarget(IDirect3DSurface9*c,IDirect3DSurface9*d):data{c,d}{c->AddRef();if(d)d->AddRef();}
 void AddRef(){++refs;}void Release(){if(!--refs)delete this;}
 ~RenderTarget(){data.pRenderSurface->Release();if(data.pDSSurface)data.pDSSurface->Release();}
 RenderTargetData*GetRenderTargetData(){return &data;}
};
namespace D3D9 {
using RenderTargetData=::Scaleform::Render::RenderTargetData;
struct HAL {
 unsigned HALState=0;IDirect3DDevice9*pDevice;
 struct RenderTargetEntry{Ptr<RenderTarget>pRenderTarget;};
 struct Stack:std::vector<RenderTargetEntry>{unsigned GetSize(){return unsigned(size());}void PushBack(const RenderTargetEntry&e){push_back(e);}}RenderTargetStack;
 explicit HAL(IDirect3DDevice9*d):pDevice(d){}
 void Flush(){}bool SetRenderTarget(RenderTarget*,bool setState=true);
 RenderTarget*GetDefaultRenderTarget(){return RenderTargetStack.empty()?nullptr:RenderTargetStack[0].pRenderTarget.p;}
 RenderTarget*CreateRenderTarget(IDirect3DSurface9*c,IDirect3DSurface9*d){return new RenderTarget(c,d);}
 void uncachedFilter(){
  // Native filter push/pop surface changes are the explicit GPU boundary.
  auto*temp=new IDirect3DSurface9({160,64,21,0,0});pDevice->SetDepthStencilSurface(nullptr);pDevice->SetRenderTarget(0,temp);temp->Release();
  auto*data=GetDefaultRenderTarget()->GetRenderTargetData();
  pDevice->SetRenderTarget(0,data->pRenderSurface);pDevice->SetDepthStencilSurface(data->pDSSurface);
 }
};
@SETTER@
}}}
@OWNER@
int main(){
 using Scaleform::Render::D3D9::HAL;using Scaleform::Render::RenderTarget;
 using BW::ScaleformBW::StockFlashOverlayTarget;
 for(unsigned format:{75u,1515474505u})for(unsigned ms:{0u,2u}){
  auto*color=new IDirect3DSurface9({1470,897,21,ms,ms?3u:0u});
  auto*scene=new IDirect3DSurface9({1470,897,format,ms,ms?3u:0u});
  auto*oldDefault=new IDirect3DSurface9({1470,897,75,ms,ms?3u:0u});
  {
   IDirect3DDevice9 device(color,scene);device.vp={10,20,937,571,.2f,.8f};HAL hal(&device);
   Scaleform::Ptr<RenderTarget>original=*new RenderTarget(color,oldDefault);CHECK(hal.SetRenderTarget(original,false));
   StockFlashOverlayTarget owner;
   for(unsigned repeat=0;repeat<3;++repeat){
    const auto viewport=device.vp;const auto scissors=device.scissor;
    {
     StockFlashOverlayTarget::Scope scope(owner,&hal,&device,true);CHECK(bool(scope));
     CHECK(device.color==color&&device.depth!=scene&&device.depth->desc.Format==75);
     CHECK(device.depth->desc.Width==1470&&device.depth->desc.MultiSampleType==ms);
     auto*stencil=device.depth;
     CHECK(hal.GetDefaultRenderTarget()->data.pDSSurface==stencil);
     // Cached draw uses the current pair. Uncached draw must return to it.
     hal.uncachedFilter();CHECK(device.color==color&&device.depth==stencil);
    }
    CHECK(device.depth==scene&&device.color==color&&hal.GetDefaultRenderTarget()==original.p);
    CHECK(!memcmp(&device.vp,&viewport,sizeof(viewport))&&!memcmp(&device.scissor,&scissors,sizeof(scissors)));
   }
   CHECK(device.created==1);
   unsigned mutations=device.mutations;
   {StockFlashOverlayTarget::Scope world(owner,&hal,&device,false);CHECK(bool(world));CHECK(device.depth==scene);}
   CHECK(device.mutations==mutations&&device.created==1);
   owner.reset();CHECK(liveSurfaces==3);
   {StockFlashOverlayTarget::Scope scope(owner,&hal,&device,true);CHECK(bool(scope));}CHECK(device.created==2);
   // A different color target/DPI/scene-resolution output gets a matching pair.
   auto*small=new IDirect3DSurface9({640,480,21,0,0});device.SetDepthStencilSurface(nullptr);device.SetRenderTarget(0,small);
   {StockFlashOverlayTarget::Scope scope(owner,&hal,&device,true);CHECK(bool(scope));CHECK(device.color==small&&device.depth->desc.Width==640);hal.uncachedFilter();CHECK(device.color==small);}
   CHECK(device.color==small&&device.depth==nullptr&&hal.GetDefaultRenderTarget()==original.p);
   owner.reset();device.SetRenderTarget(0,color);device.SetDepthStencilSurface(scene);small->Release();
   device.failCreate=true;{StockFlashOverlayTarget::Scope scope(owner,&hal,&device,true);CHECK(!scope);}CHECK(device.depth==scene);device.failCreate=false;
   device.failBind=true;{StockFlashOverlayTarget::Scope scope(owner,&hal,&device,true);CHECK(!scope);}CHECK(device.depth==scene&&hal.GetDefaultRenderTarget()==original.p);
   owner.reset();owner.reset();
  }
  color->Release();scene->Release();oldDefault->Release();CHECK(liveSurfaces==0);
 }
 CHECK(errors==8);
}
'''


if __name__ == '__main__':
    unittest.main()
