#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define WINAPI
#ifdef __uuidof
#undef __uuidof
#endif
#define __uuidof(x) 0
using int64=long long; using UINT=unsigned; using DWORD=unsigned; using UINT64=unsigned long long;
using SIZE_T=unsigned; using HRESULT=int; using HMONITOR=void*; using HMODULE=void*; using REFIID=int;
const int S_OK=0,S_FALSE=1,E_FAIL=-1,D3DSBT_ALL=0,D3DISSUE_END=1,D3DGETDATA_FLUSH=1;
const int D3DQUERYTYPE_TIMESTAMPFREQ=3,D3DQUERYTYPE_TIMESTAMP=2,D3DFMT_A16B16G16R16F=113,D3DFMT_X8R8G8B8=22,D3DFMT_A8B8G8R8=32,D3D_SDK_VERSION=32;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
struct MEMORYSTATUSEX { unsigned dwLength=0; UINT64 ullTotalPhys=0,ullAvailVirtual=0; };
bool GlobalMemoryStatusEx(MEMORYSTATUSEX* m){m->ullTotalPhys=8192ULL<<20;m->ullAvailVirtual=2048ULL<<20;return true;}
DWORD GetTickCount(){static DWORD t; return ++t;} void Sleep(int){}
struct DXGI_OUTPUT_DESC { HMONITOR Monitor; }; struct DXGI_ADAPTER_DESC { SIZE_T DedicatedVideoMemory,DedicatedSystemMemory; };
int failDxgi=0,zeroDxgi=0,wmiCalls=0;
struct IDXGIOutput { int GetDesc(DXGI_OUTPUT_DESC* d){d->Monitor=failDxgi?(void*)2:(void*)1;return 0;} void Release(){delete this;} };
struct IDXGIAdapter { int EnumOutputs(UINT i,IDXGIOutput** p){if(i)return -1;*p=new IDXGIOutput;return 0;} int GetDesc(DXGI_ADAPTER_DESC* d){d->DedicatedVideoMemory=zeroDxgi?0:512u<<20;d->DedicatedSystemMemory=zeroDxgi?0:128u<<20;return 0;} void Release(){delete this;} };
struct IDXGIFactory { int EnumAdapters(UINT i,IDXGIAdapter** p){if(i)return -1;*p=new IDXGIAdapter;return 0;} void Release(){delete this;} };
int createFactory(REFIID,void** p){*p=new IDXGIFactory;return 0;}
HMODULE LoadLibraryW(const wchar_t*){return (void*)1;}
void* GetProcAddress(HMODULE,const char*){return (void*)&createFactory;} void FreeLibrary(HMODULE){}
using ULONG=unsigned long; using BSTR=wchar_t*;
const int COINIT_APARTMENTTHREADED=0,RPC_C_AUTHN_LEVEL_DEFAULT=0,RPC_C_IMP_LEVEL_IMPERSONATE=0;
const int EOAC_NONE=0,CLSID_WbemLocator=0,CLSCTX_INPROC_SERVER=0,IID_IWbemLocator=0;
const int RPC_C_AUTHN_WINNT=0,RPC_C_AUTHZ_NONE=0,RPC_C_AUTHN_LEVEL_CALL=0;
const int WBEM_FLAG_FORWARD_ONLY=1,WBEM_FLAG_RETURN_IMMEDIATELY=2,WBEM_INFINITE=-1;
struct VARIANT {int intVal=0;}; void VariantInit(VARIANT* v){v->intVal=0;} void VariantClear(VARIANT*){}
BSTR SysAllocString(const wchar_t* s){return const_cast<wchar_t*>(s);} void SysFreeString(BSTR){}
int CoInitializeEx(void*,int){return 0;} void CoUninitialize(){} int CoInitializeSecurity(void*,int,void*,void*,int,int,void*,int,void*){return 0;}
int failWmiSecond=0;
struct IWbemClassObject {unsigned index; int Get(const wchar_t*,int,VARIANT* v,void*,void*){if(index && failWmiSecond)return -1;v->intVal=index?int(768u<<20):int(256u<<20);return 0;}void Release(){delete this;}};
struct IEnumWbemClassObject {unsigned index=0; int Next(int,int,IWbemClassObject** o,ULONG* count){if(index==2){*count=0;return 0;}*o=new IWbemClassObject{index++};*count=1;return 0;}void Release(){delete this;}};
struct IWbemServices {int ExecQuery(BSTR,BSTR,int,void*,IEnumWbemClassObject** e){++wmiCalls;*e=new IEnumWbemClassObject;return 0;}void Release(){delete this;}};
struct IWbemLocator {int ConnectServer(BSTR,void*,void*,void*,int,void*,void*,IWbemServices** p){*p=new IWbemServices;return 0;}void Release(){delete this;}};
int CoCreateInstance(int,void*,int,int,void** p){*p=new IWbemLocator;return 0;}
int CoSetProxyBlanket(IWbemServices*,int,int,void*,int,int,void*,int){return 0;}
int drawCount=0, pushes=0,pops=0,scene=0,stateRestore=0,invalidated=0,liveQueries=0,liveTargets=0;
int failQuery=0, failDraw=0, failTarget=0, failScene=0, failGet=0, slowTimes=0, failPush=0, failGeometry=0; bool available=true;
std::vector<std::string> techniques, targetNames; std::vector<unsigned> dimensions;
struct IDirect3DQuery9 { int type,index; explicit IDirect3DQuery9(int t,int n):type(t),index(n){++liveQueries;} void Release(){--liveQueries;delete this;} int Issue(int){return 0;} int GetData(void* p,unsigned,int){if(failGet)return -1;*(UINT64*)p=type==3?1000:(index%3==1?0:(slowTimes?900:1));return 0;} };
struct IDirect3DStateBlock9 { int Capture(){return 0;} int Apply(){++stateRestore;return 0;} void Release(){delete this;} };
struct IDirect3DDevice9 { int queryIndex=0; int CreateQuery(int type, IDirect3DQuery9** p){if(failQuery)return -1;*p=new IDirect3DQuery9(type,queryIndex++);return 0;} int CreateStateBlock(int,IDirect3DStateBlock9** p){*p=new IDirect3DStateBlock9;return 0;} };
struct IDirect3D9 { HMONITOR GetAdapterMonitor(unsigned){return (void*)1;} void Release(){delete this;} };
IDirect3D9* Direct3DCreate9(int){return new IDirect3D9;}
struct D3DDISPLAYMODE { unsigned Width=0,Height=0,RefreshRate=0,Format=0; };
struct DynamicState {float scale=1;void set_scale(float v){scale=v;}};
struct StockDynamicResolutionSupport {DynamicState value;DynamicState& state(){return value;}};
StockDynamicResolutionSupport* stockDynamicResolution(){static StockDynamicResolutionSupport o;return &o;}
struct IRendererPipeline { enum EType {TYPE_DEFERRED_SHADING,TYPE_FORWARD_SHADING}; };
struct Renderer { static Renderer& instance(){static Renderer r;return r;} IRendererPipeline::EType type=IRendererPipeline::TYPE_DEFERRED_SHADING; IRendererPipeline::EType pipelineType(){return type;} };
namespace Moo {
struct DeviceInfo { struct { unsigned VendorId=0x8086; } identifier_; unsigned adapterID_=0; D3DDISPLAYMODE windowedDisplayMode_; std::vector<D3DDISPLAYMODE> displayModes_; };
struct RC { IDirect3DDevice9 dev; DeviceInfo info; bool scaled=true; int beginScene(){++scene;return failScene?-1:0;} int endScene(){--scene;return 0;} bool stockSceneResolution(){return scaled;} void stockSceneResolution(bool v){scaled=v;} void invalidateStateCache(){++invalidated;} IDirect3DDevice9* device(){return available?&dev:nullptr;} unsigned deviceIndex(){return 0;} unsigned modeIndex(){return mode;} bool windowed(){return true;} bool changeMode(unsigned n,bool){mode=n;return true;} unsigned mode=0; const DeviceInfo& deviceInfo(unsigned){return info;} };
RC& rc(){static RC value;return value;} bool isInitialised(){return true;}
struct BaseTexture { virtual ~BaseTexture(){} }; using BaseTexturePtr=std::shared_ptr<BaseTexture>;
struct RenderTarget:BaseTexture { unsigned size; RenderTarget(unsigned s):size(s){++liveTargets;} ~RenderTarget(){--liveTargets;} bool push(){if(failPush)return false;++pushes;return true;} void pop(){++pops;} };
using RenderTargetPtr=std::shared_ptr<RenderTarget>;
struct EffectMaterial { bool setTechnique(const char* t){techniques.push_back(t);return true;} bool setProperty(const char*,BaseTexturePtr,bool){return true;} };
using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
bool createEffect(EffectMaterialPtr& p,const char*){p=std::make_shared<EffectMaterial>();return true;}
bool createRenderTarget(RenderTargetPtr& p,unsigned w,unsigned,int,const char* name,bool,bool){if(failTarget)return false;targetNames.push_back(name);p=std::make_shared<RenderTarget>(w);return true;}
struct FullscreenQuad { bool initialiseStockDrawGeometry(){return !failGeometry;} bool drawChecked(EffectMaterial&,unsigned w,unsigned){++drawCount;dimensions.push_back(w);return !failDraw;} };
}
int64 stockMeasureCpuFrequency(){return 12345;}
#include "stock_gpu_detection.cpp"
void clean(){assert(scene==0&&pushes==pops&&liveQueries==0&&liveTargets==0&&Moo::rc().scaled);}
int main(){
 int64 scores[5]; assert(stockMeasureGraphicsHardware(scores)); clean();
 assert(scores[0]==640 && scores[1]==100 && scores[2]==8192 && scores[3]==2048 && scores[4]==12345);
 assert(drawCount==184 && techniques.size()==184 && dimensions[0]==2048 && dimensions[2]==128);
 assert((targetNames==std::vector<std::string>{"GPU Tester Main RT", "GPU Tester Random RT1",
 "GPU Tester Random RT2", "GPU Tester Main RT (Light)", "GPU Tester Random RT1 (Light)",
 "GPU Tester Random RT2 (Light)"}));
 assert(stateRestore==1 && invalidated==1);
 for(unsigned vendor:{0x163cu,0x8087u,0x8085u,0x8088u,0x10deu}) {
  Moo::rc().info.identifier_.VendorId=vendor; assert(stockMeasureGraphicsHardware(scores)); clean();
  assert(scores[0]==(vendor==0x163c || vendor==0x8087 ? 640 : 512));
 }
 Moo::rc().info.identifier_.VendorId=0x8086;
 failDxgi=1;assert(stockMeasureGraphicsHardware(scores));clean();assert(scores[0]==768 && wmiCalls==1);
 failWmiSecond=1;assert(stockMeasureGraphicsHardware(scores));clean();assert(scores[0]==256 && wmiCalls==2);failWmiSecond=0;failDxgi=0;
 zeroDxgi=1;assert(stockMeasureGraphicsHardware(scores));clean();assert(scores[0]==0 && wmiCalls==2);zeroDxgi=0;
 available=false; assert(!stockMeasureGraphicsHardware(scores)); for(int i=0;i<5;++i)assert(scores[i]==0); clean();available=true;
 failQuery=1; assert(stockMeasureGpuPerformance()==1); clean();failQuery=0;
 failTarget=1; assert(stockMeasureGpuPerformance()==1); clean();failTarget=0;
 failGeometry=1; assert(stockMeasureGpuPerformance()==1); clean();failGeometry=0;
 failDraw=1; assert(stockMeasureGpuPerformance()==1); clean();failDraw=0;
 failScene=1; assert(stockMeasureGpuPerformance()==1); clean();failScene=0;
 failPush=1; assert(stockMeasureGpuPerformance()==1); clean();failPush=0;
 failGet=1; assert(stockMeasureGpuPerformance()==2); clean();failGet=0;
 slowTimes=1; assert(stockMeasureGpuPerformance()==2); clean();slowTimes=0;
 auto& info=Moo::rc().info; info.windowedDisplayMode_={1920,1080,60,22};
 info.displayModes_={{1280,720,60,22},{1920,1080,60,22}};
 Renderer::instance().type=IRendererPipeline::TYPE_FORWARD_SHADING;
 stockAdjustDetectedResolution(1); assert(stockDynamicResolution()->state().scale==1);
 stockAdjustDetectedResolution(2); assert(stockDynamicResolution()->state().scale==0.65f && Moo::rc().mode==1);

}
