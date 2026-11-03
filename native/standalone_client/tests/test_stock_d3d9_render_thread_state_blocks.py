"""Execute real state-block wrappers, shadow snapshots and retirement over ordered native owners."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_stock_d3d9_render_thread_queries import body

ROOT = Path(__file__).resolve().parents[1]


HOST = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
using LONG=int;using ULONG=unsigned;using DWORD=std::uint32_t;using UINT=unsigned;using HRESULT=std::int32_t;
using D3DSTATEBLOCKTYPE=int;
constexpr D3DSTATEBLOCKTYPE D3DSBT_ALL=1,D3DSBT_VERTEXSTATE=2;
constexpr HRESULT S_OK=0,D3D_OK=0,D3DERR_INVALIDCALL=-1,D3DERR_DEVICELOST=-2;
constexpr DWORD D3DVERTEXTEXTURESAMPLER0=256,D3DVERTEXTEXTURESAMPLER3=259;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define STDMETHODCALLTYPE
struct SRWLOCK{bool held=false;};
void AcquireSRWLockExclusive(SRWLOCK*p){assert(!p->held);p->held=true;}
void ReleaseSRWLockExclusive(SRWLOCK*p){assert(p->held);p->held=false;}
LONG InterlockedIncrement(volatile LONG*p){return ++*p;}
LONG InterlockedDecrement(volatile LONG*p){return --*p;}
struct Resource {int refs=1;void AddRef(){++refs;}void Release(){assert(refs>0);--refs;}};
using IUnknown=Resource;using IDirect3DSurface9=Resource;using IDirect3DVertexBuffer9=Resource;
using IDirect3DIndexBuffer9=Resource;using IDirect3DVertexDeclaration9=Resource;
using IDirect3DVertexShader9=Resource;using IDirect3DPixelShader9=Resource;using IDirect3DBaseTexture9=Resource;
struct D3DVIEWPORT9{int value;};struct RECT{int value;};
struct IDirect3DStateBlock9 {
 virtual ~IDirect3DStateBlock9(){}virtual ULONG AddRef()=0;virtual ULONG Release()=0;
 virtual HRESULT Capture()=0;virtual HRESULT Apply()=0;
};
struct NativeDevice;
static unsigned liveBlocks=0,refreshes=0;
namespace rt {
static bool main=true,render=false;static unsigned syncs=0;
static std::deque<std::function<void()>>queue;
static std::vector<std::function<void()>>retired;
static SRWLOCK g_mapLock;static std::unordered_map<void*,void*>g_identity;
static void(*g_onDestroyMain)(void*)=nullptr;
bool onMain(){return main;}bool onRender(){return render;}
template<class F>void post(F call){queue.push_back(call);}
void drain(){while(!queue.empty()){auto call=queue.front();queue.pop_front();bool m=main,r=render;
 main=false;render=true;call();main=m;render=r;}}
template<class F>auto sync(F call,const char*)->decltype(call()){
 if(render)return call();++syncs;drain();main=false;render=true;
 auto result=call();render=false;main=true;return result;
}
HRESULT reportResult(const char*,HRESULT result){return result;}
void retire(void(*fn)(void*),void*p){retired.push_back([=]{fn(p);});}
void runDeferred(){auto list=std::move(retired);retired.clear();for(auto&call:list)call();}
template<class T>T*unwrap(T*p){return p;}
NativeDevice*device();IDirect3DStateBlock9*wrapOut(void*,IDirect3DStateBlock9*);
}
@REF_BASE@;
@SHADOWS@
static std::unordered_map<void*,sh::Snapshot*>g_sbShadow;
struct NativeBlock:IDirect3DStateBlock9 {
 ULONG refs=1;NativeDevice*device;Resource*saved=nullptr;
 HRESULT captureResult=S_OK,applyResult=S_OK;
 explicit NativeBlock(NativeDevice*d):device(d){++liveBlocks;}
 ~NativeBlock(){if(saved)saved->Release();--liveBlocks;}
 ULONG AddRef()override{return ++refs;}
 ULONG Release()override{ULONG n=--refs;if(!n)delete this;return n;}
 HRESULT Capture()override;HRESULT Apply()override;
};
struct NativeDevice {
 Resource*binding=nullptr;HRESULT createResult=S_OK;bool nullSuccess=false;NativeBlock*last=nullptr;
 void bind(Resource*p){assert(rt::onRender());if(p)p->AddRef();if(binding)binding->Release();binding=p;}
 HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**out){
  assert(rt::onRender());*out=nullptr;if(FAILED(createResult))return createResult;
  if(!nullSuccess){last=new NativeBlock(this);assert(last->Capture()==S_OK);*out=last;}return createResult;
 }
};
using IDirect3DDevice9=NativeDevice;
static NativeDevice native;
HRESULT NativeBlock::Capture(){assert(rt::onRender());if(FAILED(captureResult))return captureResult;
 if(device->binding)device->binding->AddRef();if(saved)saved->Release();saved=device->binding;return captureResult;}
HRESULT NativeBlock::Apply(){assert(rt::onRender());device->bind(saved);return applyResult;}
namespace rt {NativeDevice*device(){return &native;}}
void refreshShadows(NativeDevice*d){assert(rt::onRender());++refreshes;sh::set(sh::tex[0],d->binding);sh::known=true;}
void ensureShadows(){if(!sh::known)rt::sync([]{refreshShadows(&native);return 0;},"shadow.refresh");}
struct WSB:RefBase<IDirect3DStateBlock9>{using RefBase::RefBase;HRESULT Capture()override;HRESULT Apply()override;};
struct WDevice {NativeDevice*real;HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**);};
namespace rt {IDirect3DStateBlock9*wrapOut(void*,IDirect3DStateBlock9*block){assert(block);return new WSB(block);}}
#define REAL real
@STATE_BLOCKS@
static void bind(Resource*p){assert(rt::main);sh::set(sh::tex[0],p);rt::post([=]{native.bind(p);});}
static WSB*create(WDevice&device,D3DSTATEBLOCKTYPE type=D3DSBT_ALL){IDirect3DStateBlock9*block=nullptr;
 assert(device.CreateStateBlock(type,&block)==S_OK && block);return static_cast<WSB*>(block);}
static void collect(){rt::drain();rt::runDeferred();assert(g_sbShadow.empty() && liveBlocks==0);}
int main(int argc,char**argv){assert(argc==2);std::string scenario=argv[1];Resource a,b;WDevice device{&native};
 rt::g_onDestroyMain = @DESTROY_MAIN@;
 bind(&a);
 if(scenario=="creation_failure" || scenario=="null_creation"){
  native.createResult=scenario=="creation_failure"?D3DERR_DEVICELOST:S_OK;native.nullSuccess=scenario=="null_creation";
  IDirect3DStateBlock9*out=reinterpret_cast<IDirect3DStateBlock9*>(1);
  assert(device.CreateStateBlock(D3DSBT_ALL,&out)==(native.nullSuccess?D3DERR_INVALIDCALL:D3DERR_DEVICELOST));
  assert(!out && g_sbShadow.empty() && rt::queue.empty());
 }else if(scenario=="render_owner"){
  rt::drain();const unsigned before=rt::syncs;rt::main=false;rt::render=true;
  auto*block=create(device);assert(g_sbShadow.empty());
  assert(block->Capture()==S_OK && block->Apply()==S_OK && rt::syncs==before);
  block->Release();rt::runDeferred();rt::render=false;rt::main=true;collect();
 }else if(scenario=="null_wrapper"){
  WSB*block=new WSB(nullptr);assert(block->Capture()==D3DERR_INVALIDCALL);
  assert(block->Apply()==D3DERR_INVALIDCALL && !sh::known);block->Release();collect();
 }else {
  const bool partial=scenario=="partial";auto*block=create(device,partial?D3DSBT_VERTEXSTATE:D3DSBT_ALL);
  assert(rt::queue.empty() && native.binding==&a && (partial||g_sbShadow.at(block)->tex[0]==&a));
  if(scenario=="capture_failure"){
   native.last->captureResult=D3DERR_DEVICELOST;bind(&b);unsigned admissions=0;
   if(SUCCEEDED(block->Capture()))++admissions;
   assert(admissions==0 && g_sbShadow.at(block)->tex[0]==&a && native.last->saved==&a);
  }else if(scenario=="apply_failure"){
   bind(&b);const unsigned before=refreshes;native.last->applyResult=D3DERR_DEVICELOST;
   assert(block->Apply()==D3DERR_DEVICELOST && !sh::known && refreshes==before);
   assert(sh::tex[0]==&b && native.binding==&a);ensureShadows();assert(sh::known && sh::tex[0]==&a);
  }else if(scenario=="cross_owner_capture"){
   rt::main=false;rt::render=true;native.bind(&b);assert(block->Capture()==S_OK);rt::render=false;rt::main=true;
   assert(g_sbShadow.at(block)->tex[0]==&a);bind(&a);
   assert(block->Apply()==S_OK && native.binding==&b && sh::known && sh::tex[0]==&b);
  }else {
   bind(&b);assert(block->Capture()==S_OK && native.last->saved==&b);
   if(!partial)assert(g_sbShadow.at(block)->tex[0]==&b);
   bind(&a);assert(block->Apply()==S_OK && native.binding==&b && sh::tex[0]==&b && sh::known);
  }
  block->Release();collect();
 }
 assert(device.CreateStateBlock(D3DSBT_ALL,nullptr)==D3DERR_INVALIDCALL);
 sh::clearAll();rt::main=false;rt::render=true;native.bind(nullptr);rt::render=false;rt::main=true;
 collect();assert(a.refs==1 && b.refs==1);
}
'''


class RenderThreadStateBlockTest(unittest.TestCase):
    def test_real_state_blocks_acknowledge_failures_and_restore_current_native_bindings(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            self.skipTest('C++ compiler required')
        source = (ROOT/'bigworld/stock_d3d9_render_thread.cpp').read_text()
        start = source.index('HRESULT WDevice::CreateStateBlock(')
        state_blocks = source[start:source.index('// ------------------------------------------------------------------ resources:', start)]
        start = source.index('namespace sh {')
        shadows = source[start:source.index('static std::unordered_map<void*, sh::Snapshot*>', start)]
        cpp = (HOST.replace('@REF_BASE@', body(source, 'template<class I> struct RefBase : I'))
               .replace('@STATE_BLOCKS@', state_blocks).replace('@SHADOWS@', shadows)
               .replace('@DESTROY_MAIN@', body(source, '[](void* w)')))
        cases = ('creation_failure', 'null_creation', 'null_wrapper', 'render_owner', 'capture_failure',
                 'apply_failure', 'cross_owner_capture', 'success', 'partial')
        capture = body(source, 'HRESULT WSB::Capture()')
        old_restore = '{auto it=g_sbShadow.find(this);if(it!=g_sbShadow.end())sh::load(*it->second);}'
        variants = (
            ('production', cpp, cases, True),
            ('lost_capture_failure', cpp.replace(capture, capture.replace('return result;', 'return D3D_OK;')),
             ('capture_failure',), False),
            ('failed_apply_known', cpp.replace('sh::known = false;', 'sh::known = true;'),
             ('apply_failure',), False),
            ('stale_frontend_restore', cpp.replace(
                'refreshShadows(reinterpret_cast<IDirect3DDevice9*>(rt::unwrap(rt::device())));', old_restore),
             ('cross_owner_capture',), False),
            ('null_creation_accepted', cpp.replace('if (!block) return D3DERR_INVALIDCALL;', ''),
             ('null_creation',), False),
        )
        with tempfile.TemporaryDirectory(prefix='render-state-block-host-') as temp:
            root = Path(temp)
            for name, code, scenarios, success in variants:
                path=root/(name+'.cpp');binary=root/name;path.write_text(code)
                result = subprocess.run([compiler, '-std=c++17', '-O1', '-UNDEBUG',
                                         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                         str(path), '-o', str(binary)], text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                for scenario in scenarios:
                    with self.subTest(variant=name, scenario=scenario):
                        result = subprocess.run([str(binary), scenario], text=True, capture_output=True)
                        if success:
                            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                        else:
                            self.assertNotEqual(result.returncode, 0, 'Regression control unexpectedly passed')


if __name__ == '__main__':
    unittest.main()
