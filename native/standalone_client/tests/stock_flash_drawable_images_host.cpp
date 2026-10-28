// Compile the production queue and executor with the actual SDK completion body.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "stock_flash_drawable_images.hpp"
using Scaleform::Ptr;
namespace Render = Scaleform::Render;
using HRESULT = long;
const HRESULT E_FAIL = -1, S_OK = 0, D3DERR_NOTFOUND = -2;
const int D3DSBT_ALL = 1;
#define SUCCEEDED(value) ((value) >= 0)
#define FAILED(value) ((value) < 0)
struct D3DCAPS9 { unsigned NumSimultaneousRTs=4; };
struct D3DVIEWPORT9 { unsigned X=1, Y=2, Width=800, Height=600; };
struct IDirect3DSurface9 { int id, refs=1; explicit IDirect3DSurface9(int value):id(value){} void AddRef(){++refs;} void Release(){--refs;} };
struct IDirect3DStateBlock9;
struct IDirect3DDevice9
{
    IDirect3DSurface9 a{1}, b{2}, c{3}, d{4}, z{5}, changed{99};
    IDirect3DSurface9* targets[4] = {&a,&b,&c,&d};
    IDirect3DSurface9* depth = &z;
    D3DVIEWPORT9 viewport;
    int state=37;
    unsigned targetCount=4;
    HRESULT GetDeviceCaps(D3DCAPS9* caps){caps->NumSimultaneousRTs=targetCount;return S_OK;}
    bool captureFail=false, restoreFail=false;
    HRESULT CreateStateBlock(int, IDirect3DStateBlock9**);
    HRESULT GetViewport(D3DVIEWPORT9* value){*value=viewport;return S_OK;}
    HRESULT GetDepthStencilSurface(IDirect3DSurface9** value){*value=depth;if(depth)depth->AddRef();return depth?S_OK:D3DERR_NOTFOUND;}
    HRESULT GetRenderTarget(unsigned index,IDirect3DSurface9** value){assert(index<targetCount);*value=targets[index];if(*value)(*value)->AddRef();return *value?S_OK:D3DERR_NOTFOUND;}
    HRESULT SetViewport(const D3DVIEWPORT9* value){if(restoreFail)return E_FAIL;viewport=*value;return S_OK;}
    HRESULT SetDepthStencilSurface(IDirect3DSurface9* value){if(restoreFail)return E_FAIL;depth=value;return S_OK;}
    HRESULT SetRenderTarget(unsigned index, IDirect3DSurface9* value){if(restoreFail)return E_FAIL;targets[index]=value;return S_OK;}
};
struct IDirect3DStateBlock9
{
    IDirect3DDevice9* device;
    int state=0;
    explicit IDirect3DStateBlock9(IDirect3DDevice9* value):device(value){}
    HRESULT Capture(){state=device->state;return device->captureFail?E_FAIL:S_OK;}
    HRESULT Apply(){device->state=state;return device->restoreFail?E_FAIL:S_OK;}
    void Release(){delete this;}
};
HRESULT IDirect3DDevice9::CreateStateBlock(int,IDirect3DStateBlock9** value){*value=new IDirect3DStateBlock9(this);return S_OK;}
namespace Moo
{
struct Context
{
    IDirect3DDevice9* value=NULL;
    int sceneCount=0, beginCalls=0, endCalls=0, invalidations=0;
    bool sceneFail=false;
    IDirect3DDevice9* device(){return value;}
    HRESULT beginScene(){++beginCalls;++sceneCount;return sceneFail?E_FAIL:S_OK;}
    HRESULT endScene(){++endCalls;--sceneCount;return S_OK;}
    void invalidateStateCache(){++invalidations;}
};
Context& rc(){static Context value;return value;}
}
std::vector<std::string> errors;
void logError(const char* value,...){errors.push_back(value);}
#define ERROR_MSG(...) logError(__VA_ARGS__)
@EXECUTOR@

bool queueReleased=false, completed=false, commandDestroyed=false;
struct DICommandContext { explicit DICommandContext(Render::ThreadCommandQueue*){} };
struct DICommandSet;
const int DICommand_All=1;
struct DICommandQueue
{
    struct Event { void NotifyAll(){} } CommandSetWC;
    int CommandSetMutex=0;
    DICommandSet* pRTCommands=NULL;
    Render::ThreadCommandQueue* pRTCommandQueue=NULL;
    void popCommandSet(DICommandSet*,int){}
    void Release(){queueReleased=true;}
    struct ExecuteCommand : Render::ThreadCommand
    {
        DICommandQueue* pQueue;
        struct Completion { void SetEvent(){assert(queueReleased);completed=true;} } ExecuteDone;
        explicit ExecuteCommand(DICommandQueue* value):pQueue(value){}
        ~ExecuteCommand(){assert(completed);commandDestroyed=true;}
        void Execute() override;
    };
};
struct Mutex { struct Locker { explicit Locker(int*){} }; };
struct DICommandSet
{
    explicit DICommandSet(DICommandQueue*){}
    void ExecuteCommandsRT(DICommandContext&)
    {
        auto& context=Moo::rc();
        if(context.value)
        {
            assert(context.sceneCount>=1);
            context.value->state=99;
            context.value->viewport.X=123;
            for(unsigned i=0;i<context.value->targetCount;++i)context.value->targets[i]=&context.value->changed;
            context.value->depth=&context.value->changed;
        }
    }
};
@SDK_COMPLETION@

static void one(bool nullDevice, bool sceneFail, bool captureFail, bool restoreFail, int nested,unsigned targets=4)
{
    IDirect3DDevice9 device;
    device.targetCount=targets;
    Moo::rc()=Moo::Context();
    Moo::rc().value=nullDevice?NULL:&device;
    Moo::rc().sceneCount=nested;
    Moo::rc().sceneFail=sceneFail;
    device.captureFail=captureFail;device.restoreFail=restoreFail;
    BW::ScaleformBW::StockFlashDrawableImageQueue queue(&executeStockFlashDrawableImageCommand);
    Render::Interfaces expected;
    expected.RenderThreadID=Scaleform::GetCurrentThreadId();
    queue.configure(expected);
    Render::Interfaces actual;queue.GetRenderInterfaces(&actual);
    assert(actual.RenderThreadID==expected.RenderThreadID);
    DICommandQueue owner;owner.pRTCommandQueue=&queue;
    auto* command=new DICommandQueue::ExecuteCommand(&owner);
    // Actual ExecuteCommandsAndWait adds this SDK reference, then waits for its event.
    command->AddRef();
    queueReleased=completed=commandDestroyed=false;errors.clear();
    queue.PushThreadCommand(command);
    assert(queueReleased && completed && !commandDestroyed);
    command->Release();
    assert(commandDestroyed);
    assert(Moo::rc().sceneCount==nested);
    assert(Moo::rc().beginCalls==(nullDevice?0:1));
    assert(Moo::rc().endCalls==(nullDevice?0:1));
    if(!nullDevice)
    {
        assert(Moo::rc().invalidations==1);
        for(auto* surface:{&device.a,&device.b,&device.c,&device.d,&device.z})assert(surface->refs==1);
        if(!captureFail && !restoreFail)assert(device.state==37);
        if(!restoreFail)
        {
            assert(device.viewport.X==1 && device.depth==&device.z);
            assert(device.targets[0]==&device.a && device.targets[3]==&device.d);
        }
    }
    assert(errors.empty()==(!sceneFail && !captureFail && !restoreFail));
    // A service command after renderer/context retirement still completes without
    // opening a D3D scene. No queued closure survives this call or queue destruction.
    queue.configure(Render::Interfaces());
}
int main(int argc,char**)
{
    if(argc>1)
    {
        BW::ScaleformBW::StockFlashDrawableImageQueue queue(&executeStockFlashDrawableImageCommand);
        Scaleform::currentThread=2;
        queue.configure(Render::Interfaces());
        return 0;
    }
    one(false,false,false,false,0);
    one(false,false,false,false,2);
    one(false,false,false,false,0,1);
    one(false,true,false,false,0);
    one(false,false,true,false,0);
    one(false,false,false,true,0);
    one(true,false,false,false,0);
    std::puts("PASS production DrawableImage queue, SDK completion/refs, scene/state restoration and null-device service");
}
